"""Read the project's language standard for host compiler fixtures."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
CXX_STANDARD = subprocess.check_output(
    ["make", "--no-print-directory", "-s", "cxx-standard"], cwd=ROOT, text=True,
    env=dict(os.environ, MAKEFLAGS="", MFLAGS=""), timeout=15
).strip()
CXX_STANDARD_FLAG = "-std=" + CXX_STANDARD
ZSTL_FLAGS = ["-DZSTL_FREESTANDING", "-I" + str(ROOT / "external/zstl/include")]
