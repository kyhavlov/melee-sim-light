from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path


def _run(argv: list[str], *, cwd: Path) -> str:
    env = os.environ.copy()
    env.pop("VIRTUAL_ENV", None)
    env.pop("MSL_DATA_DIR", None)
    proc = subprocess.run(argv, cwd=cwd, env=env, check=True, text=True, capture_output=True)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    return proc.stdout


def main() -> None:
    repo = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="msl-package-smoke-") as td:
        project = Path(td)
        (project / "pyproject.toml").write_text(
            textwrap.dedent(
                f"""\
                [project]
                name = "melee-sim-light-package-smoke"
                version = "0.0.0"
                requires-python = ">=3.11"
                dependencies = [
                  "melee-sim-light @ file://{repo}",
                ]
                """
            )
        )

        _run(
            [
                "uv",
                "sync",
                "--refresh-package",
                "melee-sim-light",
                "--reinstall-package",
                "melee-sim-light",
            ],
            cwd=project,
        )
        packages = json.loads(_run(["uv", "pip", "list", "--format", "json"], cwd=project))
        installed = {str(pkg["name"]).lower() for pkg in packages}
        unexpected = {"peppi-py", "pyarrow"} & installed
        if unexpected:
            raise SystemExit(f"unexpected runtime dependencies installed: {sorted(unexpected)!r}")

        smoke = f"""
        import importlib.util
        import shutil
        import struct
        from importlib import resources
        from pathlib import Path
        import melee_sim as msl
        import msl_binding
        from melee_sim import _native as native
        import tools.extraction.known_data_artifacts as artifacts
        import tools.extraction.extract_fighter_anims as extract_fighter_anims

        if importlib.util.find_spec("tools.slippi") is not None:
            raise SystemExit("tools.slippi should not be installed in the public package")
        if msl_binding.sizes()["gamestate"] != native.sizes()["gamestate"]:
            raise SystemExit("legacy msl_binding shim did not forward to packaged extension")
        if msl_binding.sizes()["rl_observation"] != native.sizes()["gamestate"]:
            raise SystemExit("legacy msl_binding size alias did not forward to gamestate")
        if msl_binding.write_rl_observation is not native.write_gamestate:
            raise SystemExit("legacy msl_binding observation writer did not alias gamestate writer")
        if artifacts.STAGE_MAGIC != b"MSLSTG01":
            raise SystemExit("artifact constants did not import")
        if native.sizes()["gamestate"] != msl.gamestate_dtype().itemsize:
            raise SystemExit("packaged native extension did not resolve")
        if not hasattr(extract_fighter_anims, "extract_one_character"):
            raise SystemExit("fighter animation extractor did not import")

        expected_chars = ("falco", "falcon", "fox", "marth", "sheik", "zelda")
        artifact_root = resources.files("tools.extraction").joinpath("source_artifacts")
        families = (
            ("motion_state/owners", b"MSLMSO01", 23),
            ("staling/move_id", b"MSLSTID1", 1),
            ("attack_id/move_id", b"MSLACID1", 3),
        )
        for rel, magic, version in families:
            family = artifact_root.joinpath(rel)
            rows = tuple(sorted(p.name.removesuffix(".bin") for p in family.iterdir()
                                if p.name.endswith(".bin")))
            if rows != expected_chars:
                raise SystemExit(f"installed {{rel}} registry rows mismatch: {{rows!r}}")
            for char_name in expected_chars:
                header = family.joinpath(f"{{char_name}}.bin").read_bytes()[:16]
                if header[:8] != magic or struct.unpack_from("<I", header, 8)[0] != version:
                    raise SystemExit(f"installed {{rel}}/{{char_name}}.bin header mismatch")
                if struct.unpack_from("<H", header, 12)[0] <= 0:
                    raise SystemExit(f"installed {{rel}}/{{char_name}}.bin has no rows")

        # Use an isolated data root whose three full-registry families come from the installed
        # wheel, not from the checkout. Other ISO-derived assets remain the normal smoke fixture.
        overlay = Path.cwd() / "installed-resource-data"
        shutil.copytree(Path({str(repo / "data")!r}), overlay)
        for rel, _magic, _version in families:
            target = overlay / rel
            shutil.rmtree(target)
            with resources.as_file(artifact_root.joinpath(rel)) as family_path:
                shutil.copytree(family_path, target)

        def reset_step(first, second):
            with msl.EnvBatch(batch_size=1, length=2, data_dir=overlay) as env:
                buffers = env.allocate_buffers()
                env.configure_match(
                    buffers,
                    stage=msl.Stage.FINAL_DESTINATION,
                    players=[msl.PlayerConfig(character=first), msl.PlayerConfig(character=second)],
                )
                controller = msl.neutral_controller((env.length, env.batch_size))
                msl.write_controller(buffers.controller_action_view, controller, player=0)
                msl.write_controller(buffers.controller_action_view, controller, player=1)
                env.bind(buffers)
                env.reset_all()
                env.step()
                if int(buffers.gamestate_view[1]["frame_id"][0]) != -122:
                    raise SystemExit("EnvBatch did not step")

        reset_step(msl.Character.FOX, msl.Character.FALCO)
        reset_step(msl.Character.FALCON, msl.Character.FOX)

        print("package smoke passed")
        """
        _run(["uv", "run", "python", "-c", textwrap.dedent(smoke)], cwd=project)


if __name__ == "__main__":
    main()
