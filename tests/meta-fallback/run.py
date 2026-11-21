#!/usr/bin/env python3
"""Compile the actual metadata section with deterministic async HTTP/cache stubs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/core/stremio.c").read_text()
section = source.split("/* ---- metadata cache", 1)[1].split("/* ---- library", 1)[0]
section = section[section.index("typedef struct"):]
with tempfile.TemporaryDirectory(prefix="origin-meta-fallback-") as tmp:
    tmp = Path(tmp)
    (tmp / "metadata-under-test.inc").write_text(section)
    exe = tmp / "test"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-fsanitize=undefined", "-fno-sanitize-recover=all", "-g",
        "-I" + str(tmp), "-I" + str(root / "src"), "-I" + str(root / "third_party/cjson"),
        str(root / "tests/meta-fallback/test.c"), str(root / "third_party/cjson/cJSON.c"),
        "-o", str(exe)
    ], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
