#!/usr/bin/env python3
"""Static & compiler invariant checks for Stremio Origin (webOS C app)."""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def parse_scr_enum() -> list[str]:
    txt = read("src/core/app.h")
    m = re.search(r"typedef\s+enum\s*\{([^}]+)\}\s*Scr\s*;", txt)
    assert m, "Scr enum not found in src/core/app.h"
    items = []
    for raw in m.group(1).split(","):
        tok = raw.strip().split("=")[0].strip()
        if tok and tok != "SCR_N":
            items.append(tok)
    return items


def parse_c_str_array(rel: str, pattern: str) -> list[str]:
    txt = read(rel)
    m = re.search(pattern, txt)
    assert m, f"Array {pattern!r} not found in {rel}"
    return re.findall(r'"([^"]+)"', m.group(1))


def main() -> int:
    scrs = parse_scr_enum()
    expected_names = {
        "SCR_LOGIN": "sign-in",
        "SCR_HOME": "home",
        "SCR_DISC": "discover",
        "SCR_LIB": "library",
        "SCR_ANIME": "anime",
        "SCR_DETAIL": "title",
        "SCR_SIM": "similar",
        "SCR_SEARCH": "search",
        "SCR_SET": "settings",
    }

    main_sn = parse_c_str_array("src/main.c", r"SN\[\]\s*=\s*\{([^}]+)\}")
    stress_nm = parse_c_str_array("src/core/stress.c", r"NAMES\[\]\s*=\s*\{([^}]+)\}")
    nerds_sn = parse_c_str_array("src/ui/nerds.c", r"SN\[\]\s*=\s*\{([^}]+)\}")

    exp = [expected_names[s] for s in scrs]
    assert main_sn[: len(scrs)] == exp, f"main.c SN mismatch: {main_sn} != {exp}"
    assert stress_nm[: len(scrs)] == exp, f"stress.c NAMES mismatch: {stress_nm} != {exp}"
    assert stress_nm[len(scrs) :] == ["player", "menu"], f"stress.c tail mismatch: {stress_nm}"
    assert nerds_sn == exp, f"nerds.c SN mismatch: {nerds_sn} != {exp}"

    apph = read("src/core/app.h")
    nav_disc = int(re.search(r"#define\s+NAV_DISC\s+(\d+)", apph).group(1))
    nav_anime = int(re.search(r"#define\s+NAV_ANIME\s+(\d+)", apph).group(1))
    nav_tabs = int(re.search(r"#define\s+NAV_TABS\s+(\d+)", apph).group(1))
    assert nav_tabs == nav_anime - nav_disc + 1, f"NAV_TABS={nav_tabs} != {nav_anime - nav_disc + 1}"

    boardc = read("src/screens/board.c")
    assert "#define SPOT_ROW 255" in boardc, "SPOT_ROW 255 missing in board.c"
    for bad in ("row == 15", "row != 15", "sp ? 15"):
        assert bad not in boardc, f"Stale spotlight sentinel {bad!r} still in board.c"

    appc = read("src/core/app.c")
    assert "cFirst = app_rtl() ? NAV_ANIME : NAV_DISC" in appc, "RTL capsule fix missing in app.c"
    assert "search_fade(sh) < 0.05f" in appc, "Ghost search hitbox guard missing in app.c"

    # Cross-compile modified TUs with strict switch checking if Zig toolchain is present
    ws = Path("/tmp/sbws")
    zig = ws / ".toolchain/zig/zig-aarch64-macos-0.17.0/zig"
    sdk = ws / ".build/sysroot"
    if zig.exists() and sdk.exists():
        env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ws / ".toolchain/zig-cache"))
        tp = ROOT / "third_party"
        incs = [
            f"-I{ROOT / 'src'}",
            f"-I{ROOT / 'build/webos'}",
            f"-I{tp / 'freetype/include'}",
            f"-I{tp / 'harfbuzz/src'}",
            f"-I{tp / 'fribidi/lib'}",
            f"-I{tp / 'config/fribidi'}",
            f"-I{tp / 'cjson'}",
            f"-I{tp / 'stb'}",
            f"-I{tp / 'qrcodegen'}",
            f"-I{tp / 'webp'}",
            f"-I{tp / 'crypto'}",
            f"-I{tp / 'libass'}",
            "-idirafter", str(sdk / "usr/include"),
        ]
        for tu in ("src/core/app.c", "src/main.c", "src/ui/nerds.c", "src/core/stress.c", "src/screens/board.c"):
            cmd = [
                str(zig), "cc", "-target", "arm-linux-gnueabi.2.12", "-mcpu=cortex_a9", "-mfloat-abi=softfp",
                "-std=c11", "-O2", "-DSB_WEBOS=1", "-D_FILE_OFFSET_BITS=64", "-D_GNU_SOURCE",
                "-DSB_APP_ID=\"tv.stremioorigin.app\"",
                "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-missing-field-initializers",
                "-Werror",
                *incs, "-c", str(ROOT / tu), "-o", f"/tmp/inv_{Path(tu).stem}.o",
            ]
            subprocess.run(cmd, check=True, env=env)

    print(f"OK: {len(scrs)} screens aligned across app.h/main.c/nerds.c/stress.c; board & topnav invariants verified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
