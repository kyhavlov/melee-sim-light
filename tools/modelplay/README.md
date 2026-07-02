# Modelplay

Pipeline for:

- running pre-existing `slippi-ai` models against the lite sim
- exporting an `MSLTRACE1` trace
- scrubbing that trace in the shared viewer

Current v0 scope:

- singles and experimental 2v2 doubles
- Fox/Falco on Final Destination, including `fox/falco` vs `fox/falco` doubles
- starts from C-owned sim match init
- model inputs/outputs stay in Python
- default rollout starts from C match initialization.

Main entrypoint:

```bash
uv run python -m tools.modelplay.run_model_match \
  --slippi-ai-root /media/kyle/Windows/Users/kyleh/git/slippi-ai \
  --p1-model /path/to/model1.pkl \
  --p2-model /path/to/model2.pkl
```

This writes `trace.msltrace.json` under `reports/triage/...`.

Diverse traces with the same checkpoint on both ports can use fused batched inference. This stacks
all port/env views into one Slippi-AI `DelayedAgent` call per frame and writes one trace directory
per env:

```bash
uv run python -m tools.modelplay.run_model_batch \
  --slippi-ai-root /media/kyle/Windows/Users/kyleh/git/slippi-ai \
  --model /path/to/model.pkl \
  --num-traces 5 \
  --out reports/modelplay/<run_name>
```

Batch output layout:

```text
reports/modelplay/<run_name>/
  summary.txt
  env_000/trace.msltrace.json
  env_001/trace.msltrace.json
  ...
```

Doubles uses the C-owned sim-init path and defaults to red `fox/falco` vs blue `fox/falco` on FD:

```bash
uv run python -m tools.modelplay.run_model_match \
  --doubles \
  --slippi-ai-root /media/kyle/Windows/Users/kyleh/git/slippi-ai \
  --p1-model /path/to/model.pkl \
  --p2-model /path/to/model.pkl \
  --p3-model /path/to/model.pkl \
  --p4-model /path/to/model.pkl \
  --p1-char fox --p2-char falco --p3-char fox --p4-char falco \
  --team-ids 0,0,1,1 \
  --out reports/modelplay/<run_name>
```

For FD doubles sim-init, the C core applies Slippi's 2v2 neutral-spawn table, so team starts are
`AA BB` (`[-60, -20, 60, 20]`) rather than vanilla port-order `ABBA`.

The default `--max-frames` is `30000`, so the runner will usually carry the game from match start
through the final stock without needing extra flags.

The runner also detects sustained static-state failures. If the same state repeats for
`--static-frame-threshold` consecutive frames (default `600`), it stops early, records
`termination_reason = "static_failure"` in `summary.txt`, and writes a trimmed
`trace_to_failure.msltrace.json` ending at the first repeated bad frame.

Viewer:

```bash
make viewer-build
make viewer
```

Then open:

`http://127.0.0.1:8001/tools/viewer/`

and load the generated `trace.msltrace.json`.
