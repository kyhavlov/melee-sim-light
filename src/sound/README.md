# The sound core (Wasm build, `MSL_SOUND=1` only)

The game's own sound code, compiled into the WebAssembly module when it is built with `make wasm MSL_SOUND=1` (into
`build/melee_core/wasm-sound`), so a host page can play a match's sound the way the game makes it: the game's code decides
everything, only the hardware (the DSP and ARAM) is ours. Without `MSL_SOUND=1` nothing in this folder is compiled and every build is
what it was. Built in, it is still off until the host starts it; while it is off every sound call is the no-op it was.

## What runs

| Part | From | What it does |
| --- | --- | --- |
| `decomp/lb/lbaudio_ax.c.inc` | doldecomp/melee `src/melee/lb/lbaudio_ax.c` (Matching) | the game's sound requests: sound effects, sound objects that follow a fighter (pan from the camera), the banks for a match, the stage music, the volumes |
| `decomp/baselib/axdriver.c.inc` | `src/sysdolphin/baselib/axdriver.c` (Matching) | HSD's sound macros: the `.sem` command VM, its voices, the aux effects' setup, the music stream's start |
| `decomp/baselib/synth.c.inc` | `src/sysdolphin/baselib/synth.c` (Matching) | HSD synth: `.ssm` sample banks into ARAM, voices, envelopes, mixing levels, the `.hps` music stream |
| `decomp/ax/*.c.inc` | `libs/dolphin/src/dolphin/ax` (Matching) | the SDK's AX: voice allocation and priorities (AXAlloc), parameter blocks and their sync (AXVPB), aux buffers (AXAux), studio depop (AXSPB), the DSP command list (AXCL) |
| `decomp/axfx/*.c.inc` | `libs/dolphin/src/dolphin/axfx` (Matching) | the SDK's effects the game uses: reverb_std on aux A, delay on aux B |
| `dsp.c` | ours | the AX microcode's work: ADPCM/PCM sample fetch from ARAM with the accelerator's addressing, sample rate conversion, volume envelope, mixer, ITD, depop, aux in/out; 160 stereo samples a 5 ms AX frame |
| `host.c` | ours | the console around it: interrupts, the audio heap, ARAM, the audio interface, the disc and HSD's device queue, AXInit and the AX frame (`__AXOutNewFrame`'s order) |
| `core.c` | ours | start-up for a VS match, the per-frame order, the sound objects' pool, the PCM of each game frame |

The decomp files are from doldecomp/melee 1f0499ba71364046140e22a30286e008c5318ce4 (2026-09-22), later than the simulator's pin
(91b9789, where lbaudio_ax, synth and axdriver were NonMatching). Each is included by a wrapper (`snd_*.c`) only when
`MSL_SOUND` is set. `lbaudio_ax.c`'s functions are renamed `msl_snd_*` (`snd_rename.h`) so the simulator's stand-ins keep their
names and can call the game's own only while the sound core is on (`snd_route.h`). What differs from the decomp is listed in
`LEDGER.md`.

## Exact and ours

Exact (the game's code, frame-matched): which sounds play and when (the game frame of every request), every parameter the game
computes (volume, pan from the camera and the fighter's position, pitch, the hit sounds' pitch variation), the `.sem` macros' timing,
voice priorities and stealing, the samples and their ADPCM decoding, the music file and its blocks, the reverb and delay.

Ours: the DSP (the microcode is not C; the 4-tap polyphase filter's coefficients are in the DSP's ROM: ours are a windowed sinc),
where the 5 ms AX frames fall inside a game frame (frame n ends at sample (n + 1) * 8008 / 15 at 32 kHz, the console's 59.94 Hz),
the disc's timing (a request is answered in the AX frame after the one it was made in), the alternate stage music (the game rolls
for it before the match: not in the replay; the main track plays).

Sound objects (`lbAudioAx_800263E8`) are HSD GObjs in the game. Here they live in `core.c`'s pool: the game's GObj lists, object
heaps and procs stay the simulator's. Their proc runs after the frame's own procs (the game gives it s_link 23).

## Credits

The decomp of Super Smash Bros. Melee and its Dolphin SDK libraries: doldecomp/melee contributors. The DSP-ADPCM format as
documented publicly (Nintendo's DSPADPCM documentation; the formula is in `dsp.c`).
