# Development setup

Follow the [quick start](README.md#quick-start) for the native prerequisites and
ISO extraction. Runtime users do not need the replay corpus, reference checkouts
or the additional validation tools below.

## Optional Nix shell

With Nix and devenv installed, run `devenv shell` for the native and viewer
build tools, Git LFS, debugger and `uv`. Then create the Python environment:

```bash
devenv shell
uv sync --dev
```

Game data extraction and the PPC validation toolchain still use the setup
steps below and in [host requirements](agent_docs/ENVIRONMENT.md). Local shell
overrides can go in the ignored `devenv.local.nix`.

## Validation replays

The files under `replays/validation/` use Git LFS. The committed `.lfsconfig`
excludes them from ordinary clone/pull downloads. To run replay validation:

```bash
git lfs install --local
git lfs pull --include="replays/validation/**" --exclude=""
uv sync --dev
```

The explicit pull downloads the current checkout's corpus (about 222 MB).
To opt a development checkout into future automatic replay downloads as well:

```bash
git config --local lfs.fetchexclude ""
```

Remove that local override with `git config --local --unset lfs.fetchexclude` to
return to the repository default. Replays are optional for simulation and are
not included in Python packages. An unfetched replay is a small text pointer;
the replay loader reports the download command when it encounters one.

## Checks

From a checkout with extracted game data:

```bash
make source-check native-smoke
uv run --dev pytest tests/test_melee_sim_api.py tests/test_slpz_replay_storage.py
```

For changes to gameplay, use the relevant focused checks and the replay gate:

```bash
make validation-supported-domain
```

The complete suite is a material checkpoint, not an edit loop. Exact output
locks are authoritative only on certified Linux/amd64 GNU builds. See
[host requirements](agent_docs/ENVIRONMENT.md) and the
[validation contract](agent_docs/VALIDATION.md). Viewer-only dependencies and
commands are in the [viewer guide](tools/viewer/README.md).

## Package builds

```bash
uv build
```

This builds a source distribution, then builds its platform wheel without Git
metadata. The source distribution includes the native source and build tools;
the wheel contains the Python API, data extractor and native library without
compiler debug information (the build-tree library keeps it). Neither
includes the ISO, extracted game data, replay corpus, reference repositories,
viewer or engineering worklogs. Builds currently tune native code for the build
CPU; do not distribute these wheels as portable binaries for arbitrary CPUs.

For a local installation outside the checkout:

```bash
uv venv /tmp/msl-package-test
uv pip install --python /tmp/msl-package-test/bin/python dist/*.whl
cd /tmp
/tmp/msl-package-test/bin/python -m melee_sim.extract_data --help
```

## CI and locally supplied game data

Pull requests run source/package builds and replay-format tests that require no
game data. The full replay gate runs trusted `main` code on push or manual
workflow dispatch and uses the repository's `SSBM_ISO_URL` secret.

Never put the ISO or extracted game data into Actions caches or downloadable
artifacts. Fork pull requests can read base-branch caches. Only validation LFS
objects are cached. The trusted `main` job owns one cache for the current corpus;
after a successful cache restore/save, it removes superseded replay caches and
legacy ISO/game-data entries. PR jobs do not create branch-specific copies.
The cleanup requires `actions: write` only in the trusted aggregate job; public
PR jobs retain read-only permissions.

Keep GitHub Settings → Archives → “Include Git LFS objects
in archives” disabled as well; `.lfsconfig` controls clones, not GitHub-generated
ZIP/tar downloads.

## Source and documentation ownership

- [Architecture](src/README.md) describes the implemented simulator.
- [Reference checkouts](refs/README.md) are optional source/forensics tools.
- [Engineering docs](agent_docs/README.md) index active instructions and retained evidence.
- [AGENTS.md](AGENTS.md) records source ownership and implementation constraints.

Keep generated outputs under ignored `build/` or `reports/triage/`. Preserve
upstream notices and source provenance. Licensing for the original project code
and the imported source needs to be settled before a public release; there is
currently no repository-wide license grant.
