from __future__ import annotations

import sys
from pathlib import Path

# Make the repo root importable so `tools.*` modules can be imported in tests.
ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

