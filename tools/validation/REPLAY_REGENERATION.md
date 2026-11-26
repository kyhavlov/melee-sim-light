# Replay regeneration

From a source checkout with the validation dependencies and extracted game data,
run `python -m tools.regenerate_replays archive.zip`. The input ZIP can contain
`.slp`, `.slp.gz`, `.slpz`, or `.slpz.gz` members. The output defaults to
`archive.regenerated.zip` and contains verified `.slp` files and a
`regeneration.jsonl` manifest. The manifest records an outcome for each
selected replay, including parse failures, unsupported captures, and the first
validation mismatch in rejected replays.

Use `--start` to skip replay members and `--limit` to select a batch. Use
`--output` to name each batch's ZIP. `--workers` sets the number of persistent
simulator workers. On Linux, `--cores 0,2,4,6` pins one worker per listed CPU.

Regenerated replays preserve the original recording version, metadata, and
finalized gameplay events. The Post-Frame payload size identifies the added
stored fighter state, so the original Pre-Frame layout still decodes correctly.
The converter requires a complete replay with a Game End event.

## Exactness

By default, the converter requires an exact match on the
[gameplay projection](../../agent_docs/VALIDATION.md). It verifies stored fighter
state on replays that already contain it.
