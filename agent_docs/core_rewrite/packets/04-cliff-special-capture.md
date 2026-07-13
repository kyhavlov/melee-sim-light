# Packet 4: Cliff, Special, and Capture

Status: complete.

## Delivered

- `MSLMSO01` recipes record cliff admission, ordinary wall jump, wall tech, CLIFFCATCH_BOTH,
  Falcon Dive's cmd1 gate, Dolphin Slash's descending/cmd1 gate, and capture callback suppression.
  Motion entry and explicit overrides install callback id, handler, and source plan atomically.
- The hand-written cliff action/character admission switch is deleted. Ledge selection consumes
  the installed recipe, live cooldown/direction state, extracted stage ledges, and stable player
  order.
- Supported Fox/Falco, Marth, Sheik/Zelda, and Falcon specials use the common map kernel for their
  generic geometry while retaining only their actual callback-local mechanics.
- CapturePulledHi/WaitHi/DamageHi and CapturePulledLw/WaitLw/DamageLw have exact extracted
  air-477E0 or ground-B108 plus capture-constraint recipes for all six supported characters.
  Catch connection installs and executes the live callback rather than duplicating a floor probe.
- Throw/capture attachment publishes the release-local CollData packet and runs the same source
  `471F8` geometry before damage entry. The detached-victim endpoint is retained as real hidden
  state, not a replay-only runtime bridge.

## Result

The capture correction makes `MilkyGracefulStingray` rollout RAW-CLEAN, and the throw/capture
lifecycle makes `FavorableUnwaveringPig` RAW-CLEAN. Cliff, special, capture, and throw-release
callbacks now share the same source-shaped geometry and persistent CollData lifetime as common air
and damage owners.
