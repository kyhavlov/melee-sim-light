# Developer Setup

Practical notes for getting a local checkout to the point where you can run
replay validation. Complements the user-facing `README.md` and `AGENTS.md`;
this file focuses on the setup gotchas that aren't obvious from those.

## Prerequisites

- `uv` (the project's Python workflow; the `Makefile` uses `uv run python`)
- Python 3.10+
- A C compiler available as `cc`
- `git-lfs` (validation replays are stored via Git LFS)
- A valid SSBM v1.02 (GALE01) ISO — required to extract game data

## One-time setup

### 1. Reference material (`refs/`)

Clone the reference repos and init the pinned Ishiiruka submodule. See
`refs/README.md` for the full list and clone commands.

### 2. Python environment

The project is `uv`-managed and expects a `.venv` in the repo root:

```bash
uv sync            # creates .venv and installs deps, including the dev group
```

Prefer `uv sync` over `pip install` — `uv run` reconciles `.venv` against
`uv.lock` on every invocation, so packages you `pip install` manually (but that
aren't in the lockfile) can be reverted on the next `uv run`.

To build the native extension against this environment:

```bash
make build         # uses `uv run python` -> .venv
```

If imports look stale, print `melee_sim._native.__file__` before debugging further.

### 3. Extracted game data (needs the ISO)

Place your ISO at the repo root as `SSBM.iso` (gitignored by that name; several
tools default to it). There is **no ISO env var** — extraction takes `--iso` as
a CLI argument. Then extract:

```bash
uv run python -m melee_sim.extract_data --iso SSBM.iso
```

The command uses `MSL_DATA_DIR` when set and otherwise writes to `data/`. Original retail archives
go under `$MSL_DATA_DIR/raw/`; generated JSON/bin tables and both verification manifests live under
the same root. The public command always builds the complete six-character/six-stage RL 1.0
profile, so it cannot silently create a partial runtime root. Repeating it verifies the ISO, raw
archives, and generated outputs and returns without rerunning generators when everything is
current.

All ISO-derived files under `data/**` are gitignored. The only tracked file there is the
source-authored Slippi neutral-spawn table, so a fresh checkout always needs this extraction step.

### 4. Validation replays (Git LFS)

The `.slp/.slpz` files under `replays/validation/` are LFS-tracked. A fresh
clone leaves them as 131-byte pointer files (they start with
`version https://git-lfs.github.com/spec/v1`). Pull the real objects:

```bash
git lfs install
git lfs pull
```

Note: this should be run before setting up git-branchless.

## Running validation

Activate the venv first so `uv` (and `make`'s `uv run python`) resolve correctly:

```bash
source .venv/bin/activate
```

- Single replay (prints to stdout):
  ```bash
  python -m tools.eval.validate_replay --replay <path.slp|.slpz> --mode both
  ```
  `--mode one-step`, `--mode rollout`, or `--mode both`.
- Full suites + refreshed reports:
  ```bash
  make validate-all
  ```
  This writes to `reports/validation/*.txt` and **prints nothing to stdout on
  success** — an empty terminal means it worked; read the report files (or
  `git diff` them) for results.
- Unit / fast tests: `make test`
- Formatting check: `make fmt-check`

### On `make` and the interpreter

`Makefile` line 3 is `PY := uv run python`. Because it uses `:=` (not `?=`), a
`PY` **environment variable is ignored** — only a command-line override
(`make <target> PY=/path/to/python`) takes effect. The intended path is simply
to have `uv` on `PATH` (activate `.venv`) and let `uv run python` resolve the
environment; no override needed.

## Debugging silent data-load failures

Several native data loaders return an error code without logging (e.g. a stage
or character file that can't be opened). The Python side then raises
`RuntimeError: msl_batch_create failed; see stderr for data loading details`
with nothing actually on stderr. When that happens, trace the file opens to find
the missing/unreadable file directly:

```bash
strace -f -e trace=openat python -c "import melee_sim as msl; msl.EnvBatch(2,8,2)" \
  2>&1 | grep -iE "data/.*(ENOENT|EACCES)"
```

The first failing `data/...` open is almost always the culprit (the `zelda.json`
case above is the canonical example).
