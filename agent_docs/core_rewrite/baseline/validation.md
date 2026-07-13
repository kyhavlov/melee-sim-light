# Validation Baseline

Baseline revision: `a293980533bd`, using the committed aggregate reports:

- `reports/validation/aggregate_recent_one_step_suite_eval.txt`
- `reports/validation/aggregate_recent_rollout_suite_eval.txt`

The suite contains 97 replays covering Fox, Falco, Marth, Sheik, Captain Falcon, and Zelda across
the six supported legal stages.

## One-step baseline

The suite has 9,948 scored discrete mismatches across 102,632,442 comparisons. Only 2 of 97 replays
are scored-discrete clean. Per-replay mismatch count has median 93, p90 194, and maximum 565.

| Field | Mismatches |
|---|---:|
| `state_flags` | 1,471 |
| `animation_index` | 1,044 |
| `action_frame` | 986 |
| `action_id` | 951 |
| `last_hit_by` | 920 |
| `instance_id` | 739 |
| `hitlag` | 626 |
| `jumps_left` | 517 |
| `on_ground` | 495 |
| `hitstun` | 407 |
| `ground_id` | 263 |
| `hurtbox_state` | 226 |
| `instance_hit_by` | 212 |
| `last_attack_landed` | 206 |
| `combo_count` | 150 |
| all five public item fields | 109-117 each |
| `facing` | 98 |
| `l_cancel` | 71 |
| `stocks`, `is_dead` | 0 |

These fields are correlated symptoms, not independent bugs. For example, one wrong collision
callback may produce the wrong grounded state, motion entry, animation, instance identity, and raw
flags on the same frame. The table is useful for ranking shared owners; it is not a license to make
five downstream fixes.

## Rollout baseline

- 26 of 97 replays are `RAW-CLEAN`; 71 are not.
- Aggregate first mismatches: 1,674.
- Aggregate seeded first mismatches: 883.
- Median first-mismatch count per replay: 16; maximum: 82.
- Aggregate streak-count: 1,673.
- Aggregate streak-length median: 249 frames; p90: 982; p95: 1,487.

Rollout is the stronger guard against replacing a one-step bridge with another one-step bridge. A
rewrite is not successful merely because it reconstructs the next public row more accurately; its
new hidden state must remain causal in free-running play.

## Candidate-specific signals

### Motion-state lifecycle and callbacks

The five largest lifecycle-shaped fields are `state_flags`, `animation_index`, `action_frame`,
`action_id`, and `instance_id`, totaling 5,191 observations. This does not prove a single scheduler
bug, but it is consistent with the discovered distribution of motion-state writes and partial entry
side effects.

### Map collision

The newer-character replays account for 475 of 495 `on_ground` mismatches and 257 of 263
`ground_id` mismatches. Mature Fox/Falco Final Destination controls have no mismatch in either
field. This is a strong concentration signal, not causal classification of every row. Collision
callbacks also own landing, floor loss, wall/ceiling reactions, tech, and ledge entry, so a
source-complete rewrite can affect action and animation fields without any row-specific logic.

### Fighter contact

The directly contact-shaped public fields contain at least:

- `last_hit_by`: 920;
- `hitlag`: 626;
- `hitstun`: 407;
- `instance_hit_by`: 212;
- `last_attack_landed`: 206;
- `combo_count`: 150.

Contact also causes Damage/Guard/Rebound motion changes and raw flag changes, so this is a lower
bound on its potential leverage.

## Validation protocol for source rewrites

Every behavior-changing cutover must:

1. declare the complete gameplay-relevant source boundary and explicit exclusions;
2. run focused source-owner positive/negative tests;
3. delete the displaced runtime path and compatibility ownership;
4. refresh all required validation reports;
5. investigate broad regressions and any mismatch that disproves the claimed source boundary;
6. record other movements as evidence for the source owner that will naturally consume them;
7. run random and replay-derived performance gates when the hot path changed materially.

Improved one-step and rollout metrics are strong supporting evidence, especially in mature
Fox/Falco controls, but neither improvement nor zero replay-level reds defines source completion.
An unchanged or locally worse metric can accompany a correct source cutover when old compensations
are removed or connected owners remain unported. Do not keep or reintroduce those compensations to
make the report green.

## What not to do with this baseline

- Do not choose a replay name or record ID as a runtime predicate.
- Do not infer exact causal counts by summing correlated fields.
- Do not turn the largest replay regressions into the implementation queue. Map them to source
  ownership and work from the source inventory.
- Do not preserve an old bridge merely because a teacher-forced row currently needs it. Determine
  whether native reseed preprocessing should initialize real hidden state instead.
- Do not weaken the validation profile to make a rewrite appear successful.
