# What the sound core's decomp files change (src/sound/decomp, from doldecomp/melee 1f0499b)

Every change is for this host: a little-endian 32-bit WebAssembly module where the console is big-endian PowerPC. None changes what
the game's code decides.

| File | Where | What and why |
| --- | --- | --- |
| baselib/axdriver.c.inc | `#include "axdriver.static.h"` | includes `snd_host.h` |
| baselib/axdriver.c.inc | `sqrtf_store` | the retail MSL expansion (frsqrte seed, three Newton steps) also when `MSL_SOUND`, not libm's sqrtf: the mix levels are the game's |
| baselib/axdriver.c.inc | `AXDriver_8038DA70` after `DVDClose` | the `.sem` file is all big-endian u32 words: swapped once read |
| baselib/synth.c.inc | `#include "synth.static.h"` | includes `snd_host.h` |
| baselib/synth.c.inc | `HSD_SynthSFXHeaderLoadCallback` | the `.ssm` header's four u32 words swapped |
| baselib/synth.c.inc | `HSD_SynthSFXSampleLoadCallback` | the sound entries swapped (`msl_snd_be_ssm_entries`: two u32, then u16 fields); the three `*(u32*) (e + 0x14..0x1C) +=` over AX address hi/lo pairs as `MSL_SND_HILO_ADD` |
| baselib/synth.c.inc | `HSD_SynthSFXGroupDataReaddress` | the same three hi/lo adds |
| baselib/synth.c.inc | `HSD_Synth_80389334`, `HSD_SynthPStreamFirstHakoDataCallback`, `HSD_SynthPStreamHeaderCallback` | `*(u32*) &src.ratioHi = x` as `MSL_SND_HILO_SET` (ratioHi/ratioLo are two u16) |
| baselib/synth.c.inc | `HSD_SynthPStreamMasterClockCallback` | `*(u32*) ((u8*) voice + 0x1B2)` (the voice's current address hi/lo) as `MSL_SND_HILO_GET(&voice->pb.addr.currentAddressHi)` |
| baselib/synth.c.inc | `HSD_SynthPStreamHeaderCallback` | the `.hps` header swapped (`msl_snd_be_hps_header`: four u32, then u16 fields) |
| baselib/synth.c.inc | `HSD_SynthPStreamHakoHeaderCallback`, `HSD_SynthPStreamFirstHakoHeaderCallback` | the block header swapped (`msl_snd_be_hako_header`: three u32, then u16 fields) |
| ax/AXVPB.c.inc | `AXSetVoiceAddr` (PCM16, PCM8) | `*(u32*) = 0x08000000` / `0x01000000` (the gain u16 first) as `MSL_SND_U16PAIR(0x0800, 0)` / `(0x0100, 0)`; includes `snd_host.h` |
| axfx/reverb_std.c.inc | `HandleReverb` | the SDK's PowerPC assembly kept under `__MWERKS__`; elsewhere `snd_reverb_std.h`, the same operations in C (fused multiply-adds as `fmaf`, fctiwz as a saturating truncation) |

Not compiled (the game never selects them): axfx/reverb_hi.c, axfx/chorus.c (`host.c` stubs their entry points), ax/AX.c and
ax/AXOut.c (`host.c` has AXInit and the AX frame in their order).

What the wrappers add around `lbaudio_ax.c` (`snd_rename.h`, `snd_lbaudio_prelude.h`, `core.c`):
- every function it defines renamed `msl_snd_*`;
- its HSD object calls (GObj_Create, GObj_InitUserData, HSD_GObjFree, HSD_GObj_SetupProc, HSD_ObjAlloc, HSD_ObjFree) and the
  sound objects' p_link list go to `core.c`'s pool;
- `HSD_Randi` (fn_80025FAC's left/right roll) returns the value the gameplay stub drew, as it always did, from the game's stream;
- `lbLang_IsSavedLanguageUS` is true (the US disc's /audio/us/ files), `gm_8016B184` false (a VS match), `gm_801603B0` the memory
  card's default sound balance (the middle: both volumes 0x7F), `Stage_8022519C` the VS stages' part of stage.c's table;
- names the simulator's older headers call otherwise: `StKind`, `GrKind`, `ChKind_Max`, `ABS`, `LANG_US`/`LANG_JP`;
- `gm_80169370` (the 1P mode's opponents, asked only when `gm_8016B184` says 1P mode) returns `CHKIND_MAX`.

For WebAssembly and this build:
- a call through a function pointer of another type traps in WebAssembly: axdriver.c's `AXDriverCallback`, a void(void), is
  registered as the synth's master clock callback (called with the stream's position) through `msl_snd_AXDriverCallback(int)`
  (`snd_axdriver.c`); synth.c's `HSD_SynthPStreamFirstHakoDataCallback`, a void(void) handed over as a device callback, is called
  as what it is (`host.c`);
- `DCFlushRange` / `DCInvalidateRange` (the data cache) are no-ops (`dolphin.h`, `host.c`);
- the stage's music is the track `Ground_801C24F8` (ground.c) picks with flag 1, read from the stage's table by `core.c` without
  that function's writes to `stage_info`;
- the decomp's statics are set up once, at the console's boot. A start after the first (after one that named missing files, or
  after a reset) puts the sound's whole static state back as it was: it lies between `00_state_begin.c` and `zz_state_end.c`, the
  first and last objects of this folder as the build links them; `core.c` checks that a static of each part (`core.c`, `host.c`,
  `dsp.c`, synth, axdriver, lbaudio_ax, AXVPB) is inside before it starts.

Outside src/sound:
- `Makefile`: `MSL_SOUND` (0 or 1, default 0). With 1 the Wasm build compiles this folder with `-DMSL_SOUND=1 -Isrc/sound` into
  `build/melee_core/wasm-sound`; every other target and the default Wasm build are unchanged (`make -n -B` the same line for line).
  It also exports the sound calls and `FS`.
- `src/stubs/headless_exclusions.c`: the game's function while the sound core is on (`snd_route.h`); `lbAudioAx_800263E8` draws its
  random roll where it always did and hands it to the game's `fn_80025FAC`.
- `src/melee/lb/lbaudio_ax.h`: with `MSL_SOUND`, `lbAudioAx_800264E4` returns int (the decomp's type: a voice id); its stub still
  returns 1 while the sound is off, which is what its `bool` -1 was. Without `MSL_SOUND` both are as they were.
- `src/runtime/batch.c`: `msl_core_batch_sound_start`; the sound's per-frame calls around its match's step; a reset, restore, copy
  over or destroy of that match turns the sound off.
