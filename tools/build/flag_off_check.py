"""The headless build is unchanged by a viewer-only change: object for object.

  python tools/build/flag_off_check.py run --base REF [--head REF] [--jobs N] [--expect OBJ ...]
  python tools/build/flag_off_check.py compare DIR_A DIR_B [--expect OBJ ...]

`run` builds the native objects (`make native`) of BASE and then of HEAD in one
worktree, build/flag-off/tree, so both builds have the same paths and the same
command lines. Each object is copied with its debug information stripped into
build/flag-off/base and build/flag-off/head. `compare` then needs every object
to be byte-identical, apart from the ones named with --expect (a change that is
meant to alter the headless build, such as a fix of it). An object only one side
has is a difference too.

`make flag-off-check FLAG_OFF_BASE=<ref>` runs it. Exit status: 0 when the two
builds agree, 1 when they differ, 2 when a build fails.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "build" / "flag-off"
OBJECTS = Path("build/melee_core/native/obj")


def git(*args: str, cwd: Path = ROOT) -> str:
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True,
                          text=True).stdout.strip()


def stripped_objects(tree: Path, dest: Path, objcopy: str) -> int:
    """Each object of TREE's native build, debug information stripped, to DEST."""
    if dest.exists():
        shutil.rmtree(dest)
    count = 0
    for obj in sorted((tree / OBJECTS).rglob("*.o")):
        out = dest / obj.relative_to(tree / OBJECTS)
        out.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([objcopy, "--strip-debug", str(obj), str(out)], check=True)
        count += 1
    return count


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compare(a: Path, b: Path, expect: set[str]) -> tuple[list[str], list[str], int]:
    """(unexpected differences, expected differences seen, identical count)."""
    names_a = {p.relative_to(a).as_posix() for p in a.rglob("*.o")}
    names_b = {p.relative_to(b).as_posix() for p in b.rglob("*.o")}
    unexpected, expected, same = [], [], 0
    for name in sorted(names_a | names_b):
        if name in names_a and name in names_b and digest(a / name) == digest(b / name):
            same += 1
            continue
        side = "" if name in names_a and name in names_b else (
            " (only in base)" if name in names_a else " (only in head)")
        (expected if name in expect else unexpected).append(name + side)
    return unexpected, expected, same


def report(unexpected: list[str], expected: list[str], same: int, objdump: str | None,
           a: Path | None = None, b: Path | None = None) -> int:
    print(f"flag-off check: {same} objects identical, {len(expected)} changed as expected, "
          f"{len(unexpected)} changed unexpectedly")
    for name in expected:
        print(f"  expected: {name}")
    for name in unexpected:
        print(f"  CHANGED:  {name}")
    if unexpected and objdump and a is not None and b is not None:
        first = next((n for n in unexpected if " (only" not in n), None)
        if first:
            dis = [subprocess.run([objdump, "-d", "-w", "--no-show-raw-insn", str(d / first)],
                                  capture_output=True, text=True).stdout.splitlines()[2:]
                   for d in (a, b)]
            import difflib
            lines = list(difflib.unified_diff(dis[0], dis[1], "base/" + first, "head/" + first,
                                              lineterm="", n=1))
            print("\n".join(lines[:40]))
    return 1 if unexpected else 0


def build(tree: Path, rev: str, args: argparse.Namespace) -> None:
    git("checkout", "--quiet", "--detach", rev, cwd=tree)
    shutil.rmtree(tree / "build", ignore_errors=True)
    data = ROOT / "data"
    if data.exists() and not (tree / "data").exists():
        (tree / "data").symlink_to(data, target_is_directory=True)
    cmd = [args.make, "-C", str(tree), "native", f"-j{args.jobs}", f"PY={args.py}",
           f"HOST_CC={args.host_cc}"]
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print(f"flag-off check: the native build of {rev} failed", file=sys.stderr)
        sys.exit(2)


def run(args: argparse.Namespace) -> int:
    base = git("rev-parse", "--verify", args.base + "^{commit}")
    head = git("rev-parse", "--verify", args.head + "^{commit}")
    tree = WORK / "tree"
    WORK.mkdir(parents=True, exist_ok=True)
    if not tree.exists():
        git("worktree", "add", "--detach", str(tree), base)
    try:
        for label, rev in (("base", base), ("head", head)):
            print(f"flag-off check: building {label} {rev[:12]}")
            build(tree, rev, args)
            stripped_objects(tree, WORK / label, args.objcopy)
    finally:
        if not args.keep:
            link = tree / "data"
            if link.is_symlink():
                link.unlink()
            git("worktree", "remove", "--force", str(tree))
    unexpected, expected, same = compare(WORK / "base", WORK / "head", set(args.expect))
    return report(unexpected, expected, same, args.objdump, WORK / "base", WORK / "head")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run")
    r.add_argument("--base", required=True)
    r.add_argument("--head", default="HEAD")
    r.add_argument("--jobs", type=int, default=4)
    r.add_argument("--py", default=sys.executable)
    r.add_argument("--host-cc", default=os.environ.get("HOST_CC", "gcc"))
    r.add_argument("--make", default=os.environ.get("MAKE", "make"))
    r.add_argument("--keep", action="store_true", help="keep build/flag-off/tree")
    c = sub.add_parser("compare")
    c.add_argument("a", type=Path)
    c.add_argument("b", type=Path)
    for p in (r, c):
        p.add_argument("--expect", action="append", default=[],
                       help="an object (path under the object directory) meant to change")
        p.add_argument("--objcopy", default=os.environ.get("OBJCOPY", "objcopy"))
        p.add_argument("--objdump", default=os.environ.get("OBJDUMP", "objdump"))
    args = ap.parse_args(argv)
    if args.command == "run":
        return run(args)
    unexpected, expected, same = compare(args.a, args.b, set(args.expect))
    return report(unexpected, expected, same, args.objdump, args.a, args.b)


if __name__ == "__main__":
    sys.exit(main())
