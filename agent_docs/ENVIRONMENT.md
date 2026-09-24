# Host environment and bring-up

Everything a working host needs that Git does not carry, the toolchain pins the bit-exact gate
depends on, and the procedure that certifies a new host as gate-authoritative. Linux/amd64 is the canonical validation platform; macOS details describe the
existing build accommodations and their limits.

## What Git does not carry

| Asset | Recreate with | Symptom when missing |
|---|---|---|
| `SSBM.iso` (GALE01 rev 2) | supply it; `data/manifest.json` pins its extraction | `make bootstrap` refuses without `ISO=` |
| `data/raw` | `make extract ISO=/path/to/SSBM.iso` | data-contract test failures, `msl_batch_create` aborts |
| Validation replay bytes | `git lfs pull --include="replays/validation/**" --exclude=""` | `unsupported .slpz version: 1986359923` (ASCII `vers` — an unsmudged LFS pointer) |
| `refs/*` reference checkouts | `refs/README.md` | source-backing claims cannot be checked |
| `build/melee_core/toolchain` | `make toolchain` | no `make ppc`, no PPC oracle backend |
| `.venv` | `uv sync --dev` | `PY` default missing (`$(ROOT)/.venv/bin/python`) |
| Node + emcc | distro/emsdk install | no `wasm`, `viewer-build`, `viewer-smoke` |
| Probe Dolphin build | `refs/README.md` | no retail ground truth for ULP triage |

Every path above is `$(ROOT)`-relative, so **each worktree needs its own `.venv` and its own
`data/`** (`PY ?= $(ROOT)/.venv/bin/python`, `MSL_DATA_DIR ?= $(ROOT)/data`). `data/raw` can be
copied between checkouts and verified against `data/manifest.json`; the toolchain tree can be
copied only between hosts of the same architecture.

## Bring-up on a linux/amd64 host

```sh
git lfs pull --include="replays/validation/**" --exclude=""
make bootstrap ISO=/path/to/SSBM.iso     # uv sync --dev, extract, native, python-library
make toolchain                        # already populated by the first native build
make validator
```

- `make bootstrap` invokes bare `uv`; it must be on `PATH`.
- Native layout generation also uses the PowerPC toolchain. Its setup downloads
  exact `.deb` files from the URLs in `tools/build/ppc32_toolchain_packages.tsv`,
  checks their SHA-256 hashes, and unpacks them with `dpkg-deb`. It does not use
  the host's apt package selection or install packages globally. Linux setup
  needs curl, dpkg-deb and sha256sum; the host compiler needs GNU Make and binutils.
  An older incompatible host glibc uses the Docker compiler wrapper.
- Validation replays stay as LFS pointers until explicitly fetched. They are not
  needed for package installation, ISO extraction or normal simulation.

## Toolchain pins the bit-exact gate depends on

- **CI pins `ubuntu-24.04` for gcc-13** and says so because the replay gate asserts bit-exactness.
  The Makefile only defaults `HOST_CC ?= gcc`. If a host's default compiler is not gcc-13, build
  with `HOST_CC=gcc-13` until a newer GCC has been shown lock-clean over the full aggregate.
  Measured 2026-08-04: gcc 15.2 (Ubuntu 25.10) reproduces the full aggregate bit-exactly against
  the same locks as a gcc-13.4 build of the same tree. When the distro no longer packages gcc-13,
  `apt-get download` + `dpkg-deb -x` into `build/host-gcc13/` with a `-B` wrapper script gives the
  pinned compiler without root (see `build/host-gcc13/gcc13.sh` on the certified host).
- The gate build is `-O0 -ffp-contract=off`; only `MSL/trigf.o` and `runtime/math.o` compile at
  `-O2 -ffp-contract=fast` with `-mfma`, which is why the profile is x86-64 in practice.
- The **release** profile adds `-march=native -mtune=native` (`RELEASE_ARCH_FLAGS`), so that binary
  is CPU-specific. Contraction stays off, so output is expected to be identical; confirm it once
  per host with `make validation-release-supported-domain` rather than assuming it.
- The PPC oracle is host-independent: qemu's PPC FPU is softfloat and the cross compiler produced a
  bit-identical `melee-core-ppc` from amd64 and arm64 hosts (measured 2026-07-30). Only the host-arch
  half of the toolchain tree differs.

## Certifying a new host as gate-authoritative

`AGENTS.md` allows recorded suite identities from CI, a native Linux host, or the Linux container.
"Native Linux host" means one that has passed this, not one that merely runs Linux:

1. `make native validator python-library -j` (with the compiler pin above).
2. `make source-check`.
3. Aggregate against the committed locks:
   ```sh
   python -m tools.validation.validate_replay \
     --suite replays/suites/melee_core_aggregate.json --backend native --no-build \
     --workers N --output-locks replays/suites/melee_core_output_locks.json
   ```
   It must reproduce the current committed aggregate's strict comparisons and output locks with zero failures or errors. Suite manifests and generated locks are the
   authority; do not use a historical work-log count as the expected roster or case total.
4. `make ppc` then one per-suite PPC run (`VALIDATION_SUITE=replays/suites/<char>.json
   VALIDATION_BACKEND=ppc`). Per-suite PPC is the gate; aggregate-PPC lock-fails by design.
5. `pytest tests`. The parallel replay test asserts a host-dependent timing budget and is
   deselected in hosted CI (`test_native_validation_runs_current_oracle_replays_in_parallel`).
   Investigate its errors before distinguishing timing limits from correctness failures.

Until steps 1-4 are green on the host, do not record aggregate identities or output locks from it.

## Running the suite

- `--workers 0` resolves to `min(MAX_AUTO_WORKERS, cpu_count, task_count)` and `MAX_AUTO_WORKERS`
  is **16** (`tools/validation/validate_replay.py`). A box with more than 16 hardware threads needs
  an explicit `--workers` / `VALIDATION_WORKERS` to use them.
- Each worker runs its own native server process, so memory is the real ceiling: in an 8 GB
  container earlier replay tests OOM-killed a server at the 16-worker default and needed
  <=8-10. Size workers against RAM first, cores second.
- `make validation-supported-domain` is the native aggregate; `VALIDATION_BACKEND=ppc` with a
  single-character suite is the PPC gate. `make native` does not rebuild the PPC binary — `make ppc`
  is separate, and a stale `melee-core-ppc` reproduces pre-fix behavior long after a native fix
  lands.

## Benchmarks are host-pinned

[`performance/BASELINE.md`](performance/BASELINE.md) records the current provenance host and the pinned `benchmark-9950x3d-*` targets. Absolute FPS from
a different host is not comparable to the retained ledger; only same-host A/B pairs are. Benchmark **digests** must match for identical
workload/configuration bytes; historical digests do not describe a changed replay corpus.
Record the host, workload and validation results with performance commits. Update
`performance/BASELINE.md` when the production benchmark reference changes.

## macOS-only machinery (does not transfer)

These support the macOS development host; they explain the `Darwin`
branches in the `Makefile` and the container recipes in the
[historical work log](https://github.com/kyhavlov/melee-sim-light/blob/5018738c8b2da68330823bb20fee37a5044841cd/agent_docs/ACTIVE_WORK.md).

- Neither macOS build profile is gate-grade: arm64-native drifts on signed zero/ULP, and the
  Rosetta `HOST_TARGET_ARCH=x86_64` profile has no recorded digest equivalence (Apple clang
  codegen, not Rosetta, owns the divergence).
- `src/Runtime/` (MW library headers) and `src/runtime/` (port runtime) are distinct in Git but
  collapse on case-insensitive APFS, and a bind-mounted checkout stays collapsed inside a Linux
  container. That is why container work used `git clone /repo /work` plus a patch, rather than a
  mount. On a case-sensitive filesystem the whole recipe is unnecessary.
- `docker cp` preserves source mtime, so a copied-in edit could be older than its object file and
  `make` would skip it — a control run that silently measured the previous binary. Gone with the
  container.
- PPC cross-compilation via Homebrew LLVM (`tools/build/ppc32_cc.sh`), `qemu_ppc.sh`,
  `portable_timeout.sh`, and the `-undefined dynamic_lookup` Python-extension link flag are all
  Mach-O/macOS accommodations.
- Container platform choice mattered only on Apple Silicon: an amd64 image ran qemu-ppc itself
  under Rosetta (PPC-in-x86-in-arm64), which halved PPC suite throughput. A native amd64 host has
  no such nesting.

## Repo cost notes

- Do not content-search whole history. `git grep <pat> $(git rev-list --all)` inflates every blob of
  every commit against a ~1.3 GB object store and saturated the development machine. Use the
  pickaxe with a pathspec: `git log --all -S"..." -- agent_docs/ replays/suites/`. State that budget
  explicitly when delegating history archaeology.
- Switching branches can leave `data/` older than the checked-out extractors; re-run `make extract`
  when data-contract tests fail rather than debugging the failure.
