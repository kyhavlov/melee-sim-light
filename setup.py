from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

from setuptools import find_packages, setup
from setuptools.command.build_py import build_py


ROOT = Path(__file__).resolve().parent


class BuildPythonApi(build_py):
    def run(self) -> None:
        subprocess.run(
            ["make", "--no-print-directory", "python-library", f"PY={sys.executable}"],
            cwd=ROOT,
            check=True,
        )
        super().run()
        destination = Path(self.build_lib) / "melee_sim" / "libmelee_core.so"
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "build/melee_core/python/libmelee_core.so", destination)


setup(
    packages=find_packages(include=("melee_sim", "tools", "tools.data")),
    package_data={"melee_sim": ["libmelee_core.so"]},
    cmdclass={"build_py": BuildPythonApi},
)
