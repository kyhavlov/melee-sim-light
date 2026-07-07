from __future__ import annotations

import ast
import sys
from pathlib import Path


def _extension_sources(setup_py: Path) -> list[str]:
    tree = ast.parse(setup_py.read_text(encoding="utf-8"), filename=str(setup_py))
    root = setup_py.parent
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        func = node.func
        if not (
            (isinstance(func, ast.Name) and func.id == "Extension")
            or (isinstance(func, ast.Attribute) and func.attr == "Extension")
        ):
            continue
        for kw in node.keywords:
            if kw.arg != "sources":
                continue
            if not isinstance(kw.value, (ast.List, ast.Tuple)):
                raise SystemExit("Extension sources must be a literal list/tuple")
            out: list[str] = []
            for elt in kw.value.elts:
                if not isinstance(elt, ast.Constant) or not isinstance(elt.value, str):
                    raise SystemExit("Extension sources must contain only string literals")
                out.append(elt.value)
            if not out:
                raise SystemExit("Extension sources list is empty")
            missing = [src for src in out if not (root / src).is_file()]
            if missing:
                raise SystemExit("Extension sources do not exist: " + ", ".join(missing))
            return out
    raise SystemExit("could not find Extension(..., sources=[...]) in setup.py")


def main() -> int:
    setup_py = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("setup.py")
    print(" ".join(_extension_sources(setup_py)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
