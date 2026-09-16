# Signed-zero handoff from ExPhil — 2026-09-15

User-requested context for the Codex working on melee-sim-light. This is an
evidence handoff, not a change to this repository's runtime or comparison policy.

## Finding: the capture can encode historical Dolphin arithmetic

ExPhil's four failing human replay prefixes were **four prefixes of one game**,
not four independent games. Source:

`../exphil/eval_runs/local_multishine_20260913_224806/2026-09-Mainline/Game_20260913T224817.slp`

Handoffs: 1155, 4234, 4452 and 1319. The complete diagnostic isolated one field:
P1 vertical knockback velocity (`speed_y_attack`), frames **783–800**.

| Value | Original Mainline recording | Old Ishiiruka JIT replay |
| --- | --- | --- |
| f32 bits | `0x80000000` (`-0.0`) | `0x00000000` (`+0.0`) |

Every other exposed state field matched through frame 4451. Both ports' recorded
inputs, physical input fields, raw sticks and RNG also matched. Shorter prefixes
ending before the discrepancy were exact. This was **not an input-injection or
state-restoration mismatch**.

## Proven arithmetic owner

PowerPC `fnmsub` computes `-(a*c - b)`. The old Ishiiruka JIT implemented
`b - a*c`, including x86 `VFNMADD` on FMA hosts. With exact cancellation under
the normal rounding mode, these produce negative and positive zero respectively.
Melee's airborne vertical knockback decay executes **`fnmsubs` at `0x8006B9E4`**
and stores the result at **fighter offset `0x90`**. Slippi records that word.

- Switching only to the interpreter made the 1,194-frame human prefix exact.
- Correcting JIT subtraction/negation made all six tested human prefixes exact.
- The corrected non-FMA path also passed the 1,194-frame case.
- No state overwrite, input change or comparison tolerance was needed.

**An unconditional correction then broke seven of twelve older CPU source
games**, with the opposite zero-sign discrepancy. The early three-game smoke
test had missed this. Accurate PPC arithmetic and historical replay compatibility
are separate questions. Do not infer an arithmetic profile from human/CPU status.

The external Dolphin fork now has an explicit `[Core] AccurateNmsub` option,
default false. libmelee writes true or false every session to prevent a stale
setting. With explicit source profiles, the longest prefix of each source passed
**13/13 exact** (twelve legacy CPU sources, one accurate Mainline source), and the
public CLI test passed **6/6 exact human prefixes**. Those longest intervals cover
the original 77 mined prefixes; this was not 77 independently rerun tests.

## Relevance to this simulator

This supports the execution-profile explanation already discussed in
[CORRECTNESS_COMPLETION_2026-09-08.md](CORRECTNESS_COMPLETION_2026-09-08.md),
especially its historical float/zero and Doubles 5423 discussion. It does **not**
prove that every classified zero or ULP difference has this same instruction
owner. Trace each earliest differing writer and arbitrate equal inputs against
the PPC/retail result before assigning the cause.

- Preserve strict bitwise comparison. A signed-zero-equal diagnostic may reveal
  a later mismatch; it does not establish bit-exact compatibility.
- Keep sign through copy/save/restore and capture tooling. Ordinary numeric
  equality cannot distinguish these values; report f32 hex bits.
- Signed zero can affect later behavior. This repo already documents a DI/atan2
  branch difference from the sign in Doubles 5423; do not globally canonicalize
  zeros just because the first mismatch has zero magnitude.
- Treat the external Dolphin option as evidence about capture provenance, not
  a request to add compatibility switches or replay-specific arithmetic to this
  simulator. Follow this repo's source-owner and representation rules.

There is also a **separate input transport pitfall**: converting original stick
floats from `[-1,1]` to `[0,1]` and back can erase negative zero and tiny values.
Finite f32-to-f64 promotion itself preserves them. ExPhil retains original
processed input floats separately from normalized training axes. That lossy
normalization was ruled out for the knockback-velocity discrepancy above.

## Local evidence and implementation pointers

All paths below are relative to the root of melee-sim-light:

- `../exphil/docs/planning/HUMAN_REPLAY_FAILURES.md`: full diagnosis, compatibility
  experiment and installation details.
- `../exphil/docs/planning/FLOAT_INPUT_INJECTION_REVIEW.md`: processed versus
  physical input fields, raw UCF sticks and exact-audit contract.
- `../exphil/eval_runs/0915_float_input/human_audit.exs` and `human_audit.json`:
  reproducible Elixir full-field diagnostic and retained bit patterns.
- `../exphil/eval_runs/0915_float_input/human_checks_summary.json`: interpreter,
  corrected JIT, non-FMA and matched-source results. Check `prefix_audit.valid`
  and the aggregate `exact` counts: a top-level `pass: false` can refer to the
  separate policy-response/chain gate, despite an exact replay prefix.
- `../exphil/eval_runs/0915_float_input/nmsub_matched_manifest.json` and
  `nmsub_profiles.exs`: explicit source profiles and manifest generation.
- `../slippi-Ishiiruka/Source/Core/Core/PowerPC/Jit64/Jit_FloatingPoint.cpp`:
  `accurate_nmsub` branches; relevant published commit `42f7c9d94`.
- `../libmelee_ex/lib/melee/dolphin.ex`: per-session `AccurateNmsub` configuration;
  relevant published commit `9d0f4af`.

These findings distinguish the old Ishiiruka implementation from modern Dolphin;
the ExPhil report links the modern implementation that already handles the
signed-zero semantics. No new simulator experiment or gate was run for this note.
