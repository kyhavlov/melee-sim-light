# Host environment and bring-up

Everything a working host needs that Git does not carry, the toolchain pins the bit-exact gate
depends on, and the procedure that certifies a new host as gate-authoritative. Written while
development ran on macOS with a Linux container; the Linux path below is the canonical one and the
macOS section exists only to explain the `Darwin` branches in the `Makefile`.

## What Git does not carry

| Asset | Recreate with | Symptom when missing |
|---|---|---|
| `SSBM.iso` (GALE01 rev 2) | supply it; `data/manifest.json` pins its extraction | `make bootstrap` refuses without `ISO=` |
| `data/raw`, `data/stages` | `make extract ISO=/path/to/SSBM.iso` | data-contract test failures, `msl_batch_create` aborts |
| Validation replay bytes | `git lfs pull` | `unsupported .slpz version: 1986359923` (ASCII `vers` — an unsmudged LFS pointer) |
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
git lfs pull
make bootstrap ISO=/path/to/SSBM.iso     # uv sync --dev, extract, native, python-library
make toolchain                           # pinned PPC cross gcc-13 + qemu, unpacked in-tree from ppc32_toolchain_packages.tsv
make validator
```

- `make bootstrap` invokes bare `uv`; it must be on `PATH`.
- `make toolchain` shells out to `apt-get download` + `dpkg-deb -x` and unpacks the Ubuntu 24.04
  `*-powerpc-linux-gnu` / `*-powerpc-cross` package set into `build/melee_core/toolchain/root`.
  Nothing is installed system-wide, but the host must be Debian-family with those packages
  reachable. On any other distro the PPC oracle needs a container.

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
   It must reproduce the aggregate identity recorded in `agent_docs/ACTIVE_WORK.md` under
   "Suite state" — `426 = 342 pass / 84 classified / 0 fail / 0 error` at the time of writing —
   with zero lock failures.
4. `make ppc` then one per-suite PPC run (`VALIDATION_SUITE=replays/suites/<char>.json
   VALIDATION_BACKEND=ppc`). Per-suite PPC is the gate; aggregate-PPC lock-fails by design.
5. `pytest tests`. Two tests assert wall-clock and per-case timeout budgets calibrated for the
   validated container and are deselected in CI
   (`test_native_validation_runs_current_oracle_replays_in_parallel`,
   `test_native_validation_compares_complete_classified_replays`); on a faster host they should
   pass, and a failure there is a budget-calibration question, not a correctness one.

Until steps 1-4 are green on the host, do not record aggregates, classification snapshots, or
output locks from it.

## Running the suite

- `--workers 0` resolves to `min(MAX_AUTO_WORKERS, cpu_count, task_count)` and `MAX_AUTO_WORKERS`
  is **16** (`tools/validation/validate_replay.py`). A box with more than 16 hardware threads needs
  an explicit `--workers` / `VALIDATION_WORKERS` to use them.
- Each worker runs its own native server process, so memory is the real ceiling: in an 8 GB
  container the classified-replay test OOM-killed a server at the 16-worker default and needed
  <=8-10. Size workers against RAM first, cores second.
- `make validation-supported-domain` is the native aggregate; `VALIDATION_BACKEND=ppc` with a
  single-character suite is the PPC gate. `make native` does not rebuild the PPC binary — `make ppc`
  is separate, and a stale `melee-core-ppc` reproduces pre-fix behavior long after a native fix
  lands.

## Benchmarks are host-pinned

`agent_docs/CURRENT_BASELINE.md` records its provenance host (AMD Ryzen 9 9950X3D, CPU 0 V-Cache
CCD, Linux 6.17 x86-64, GCC 13.3.0) and the pinned `benchmark-9950x3d-*` targets. Absolute FPS from
a different host is not comparable to the retained ledger; only same-host A/B pairs are. The
benchmark **digests** (`8ef126a41244d514` at 256, `6f91f23e3553a090` at 512) are host-independent
and must match anywhere. A performance commit made on a new host refreshes provenance in
`CURRENT_BASELINE.md` before its numbers enter `RETAINED_PERFORMANCE.md`.

## macOS-only machinery (does not transfer)

These exist for the macOS development host and are dead weight on Linux; they explain the `Darwin`
branches in the `Makefile` and the container recipes in older `ACTIVE_WORK.md` entries.

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
