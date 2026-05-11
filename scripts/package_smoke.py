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
                requires-python = ">=3.10"
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
        import melee_sim as msl
        from melee_sim._native_access import native
        import tools.extraction.known_data_artifacts as artifacts
        import tools.extraction.extract_fighter_anims as extract_fighter_anims

        if importlib.util.find_spec("tools.slippi") is not None:
            raise SystemExit("tools.slippi should not be installed in the public package")
        if importlib.util.find_spec("msl_binding") is not None:
            raise SystemExit("msl_binding should not be installed in the public package")
        if artifacts.STAGE_MAGIC != b"MSLSTG01":
            raise SystemExit("artifact constants did not import")
        if native.sizes()["gamestate"] != msl.gamestate_dtype().itemsize:
            raise SystemExit("native access helper did not resolve packaged extension")
        if not hasattr(extract_fighter_anims, "extract_one_character"):
            raise SystemExit("fighter animation extractor did not import")

        with msl.EnvBatch(batch_size=1, length=2, data_dir={str(repo / "data")!r}) as env:
            buffers = env.buffers()
            env.configure_match(
                buffers,
                stage=msl.Stage.FINAL_DESTINATION,
                players=[
                    msl.PlayerConfig(character=msl.Character.FOX),
                    msl.PlayerConfig(character=msl.Character.FALCO),
                ],
            )
            controller = msl.neutral_controller((env.length, env.batch_size))
            msl.write_controller(buffers.controller_action_view, controller, player=0)
            msl.write_controller(buffers.controller_action_view, controller, player=1)
            env.bind(buffers)
            env.reset_all()
            env.step()
            if int(buffers.gamestate_view[1]["frame_id"][0]) != -122:
                raise SystemExit("EnvBatch did not step")

        print("package smoke passed")
        """
        _run(["uv", "run", "python", "-c", textwrap.dedent(smoke)], cwd=project)


if __name__ == "__main__":
    main()
