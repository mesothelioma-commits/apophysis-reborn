"""Regenerate the numerical test vectors using an external FLAM3 source tree."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile


def c_function(source, kind, name):
    start = re.search(rf"^{kind} {name}\s*\(", source, re.MULTILINE)
    if not start:
        raise ValueError(f"Missing source function {name}")
    end = source.index("\n}", start.start())
    return source[start.start():end + 2]


def macro(source, name):
    return re.search(rf"^#define {name}\b.*$", source, re.MULTILINE).group()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("flam3_source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = (args.flam3_source / "variations.c").read_text()
    private = (args.flam3_source / "private.h").read_text()
    transform = c_function(source, "int", "apply_xform")
    cases = re.findall(r"case\s*\((VAR_\w+)\):\s*(var(\d+)_\w+)\(", transform)
    constants = [f"{symbol}={number}" for symbol, function, number in cases]
    bodies = [c_function(source, "void", function) for _, function, n in cases if int(n) < 15]
    # Unused operations abort if a future test accidentally requests one.
    unused = {function for _, function, n in cases if int(n) >= 15}
    unused.add("var67_pre_blur")
    macros = [macro(private, "EPS"), macro(source, "badvalue")]
    text = "\n".join(macros + bodies + [transform])
    # Ignore only comments/whitespace so the recorded upstream operations stay pinned.
    normalized = re.sub(r"\s", "", re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S))
    digest = hashlib.sha256(normalized.encode()).hexdigest()
    expected = "17d941bb0ede6bbd1a44ea4a441158ce758c332368f33f56ddd2d69d5f5b9294"
    if digest != expected:
        raise ValueError(f"Unexpected FLAM3 numeric source fingerprint: {digest}")
    code = """
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int randctx;
#define flam3_random_isaac_bit(p) (*(p))
#define flam3_random_isaac_11(p) (abort(), 0.0)
typedef struct {
    double c[3][2], post[3][2], color, color_speed, vis_adjusted, has_preblur;
    double active_var_weights[15];
    int precalc_atan_xy_flag, precalc_atan_yx_flag, precalc_angles_flag;
    int num_active_vars, varFunc[15], has_post;
} flam3_xform;
typedef struct { flam3_xform *xform; } flam3_genome;
typedef struct {
    randctx *rc;
    flam3_xform *xform;
    double tx, ty, p0, p1, precalc_atan, precalc_atanyx, precalc_cosa, precalc_sina;
    double precalc_sumsq, precalc_sqrt;
} flam3_iter_helper;
"""
    code += "enum { " + ", ".join(constants) + " };\n"
    code += "\n".join(f"#define {name}(...) abort()" for name in sorted(unused))
    code += "\n" + text + "\n"
    code += Path(__file__).with_name("reference_driver.c").read_text()
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        unit = root / "oracle.c"
        exe = root / "oracle"
        unit.write_text(code)
        subprocess.run([os.environ.get("CC", "cc"), "-std=gnu99", str(unit), "-lm", "-o", str(exe)], check=True)
        vectors = subprocess.check_output([str(exe)])
    args.output.write_bytes(vectors)


if __name__ == "__main__":
    main()
