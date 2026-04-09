# Modelplay

Throwaway pipeline for:

- running pre-existing `slippi-ai` models against the lite sim
- exporting a browser-viewable trace
- scrubbing that trace in a forked `slippi-viewer`

Current v0 scope:

- singles only
- Fox/Falco on Final Destination
- seeded from an existing local `.msl` dataset row
- model inputs/outputs stay in Python
- default rollout starts from the dataset opening row (`start_record=0`), which is the real 4-stock
  match-start `Entry` state for the sampled game

Main entrypoint:

```bash
uv run python -m tools.modelplay.run_model_match \
  --slippi-ai-root /media/kyle/Windows/Users/kyleh/git/slippi-ai \
  --p1-model /path/to/model1.pkl \
  --p2-model /path/to/model2.pkl
```

This writes `trace.json` under `reports/triage/...`.

The default `--max-frames` is `30000`, so the runner will usually carry the game from match start
through the final stock without needing extra flags.

The runner also detects sustained static-state failures. If the same state repeats for
`--static-frame-threshold` consecutive frames (default `600`), it stops early, records
`termination_reason = "static_failure"` in `summary.txt`, and writes a trimmed
`trace_to_failure.json` ending at the first repeated bad frame.

Viewer:

```bash
cd tools/modelplay/viewer
npm install
npm run build
python -m http.server 8000
```

Then open:

`http://127.0.0.1:8000/examples/sim/`

and load the generated `trace.json`.
