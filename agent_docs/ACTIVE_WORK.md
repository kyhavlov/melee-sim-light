# Active work

## CPU replay PR #25 review

Rebase onto cleaned main, review source input timing, port ownership, wire
consumers and save/restore, resolve findings, and run the relevant gates.
Prepare the branch for a separate merge decision; merging #25 is not authorized.

Final owners: source player slot/level and Fighter input state; pending recorded
samples belong to each match's Slippi state, keyed by physical port. Consumers
are the input callback, replay decoder and copy/save/restore. The private wire
layout and benchmark version cut over together; no legacy decoder or alternate
gameplay state is retained. Followers keep source AI input ownership.

## Protected workspace work

The unrelated `experiment/million-fps-proof` changes remain in stash
`pre-mewtwo-pr-23-review-2026-09-17` (`10f5e030`). Do not apply or discard them.
