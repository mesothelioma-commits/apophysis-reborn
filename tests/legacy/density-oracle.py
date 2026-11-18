"""Regenerate controlled density-stage vectors with the pinned upstream FLAM3 C code."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


HASHES = {
    "private.h": "548a082cd1f73bb5180b69220e94a5641a61b49403cb68eb5540c2cfd685c2f0",
    "filters.c": "9d91be7f8133d56f1e99671315f5755396dc6fab6333a888aad0e6cd3ae97bde",
    "filters.h": "1680d533b4d29bcfe721fb564353381464bf415e8afc1f5a902b261b8c91f439",
    "rect.c": "e0efc3bd12c966777ad5197127cd8ae46d62558c0ece3034454f856bc471395c",
    "flam3.c": "60e62e32344f35b4fc8a3d71fc5c342c86325e02bc7b5cdd368b144c8ce11b33",
    "flam3.h": "a1a448f740f18915cdc91705d61930531d7d693e2651dbce72e61086c1540532",
}


def between(text, first, end):
    start = text.index(first)
    return text[start:text.index(end, start)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    files = {}
    for name, digest in HASHES.items():
        data = (args.source / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("Unexpected upstream source: " + name)
        files[name] = data.decode()
    constants = "\n".join(re.findall(r"^#define flam3_\w+_kernel \d+$", files["flam3.h"], re.M))
    constants += "\n" + "\n".join(re.findall(r"^#define\s+flam3_(?:num_spatialfilters|mitchell_[bc])\s+[^\n]+", files["private.h"], re.M))
    filters = files["filters.c"]
    kernel = between(filters, "flam3_de_helper flam3_create_de_filters", "double flam3_create_temporal_filter")
    helper = between(files["filters.h"], "#define DE_THRESH", "extern double flam3_spatial_support")
    # Only the 64-bit branch is used. Keep all additions and original bounds checks.
    scatter = between(files["flam3.c"], "#define abump_no_overflow(dest, delta) do {dest += delta;}", "/* single-threaded */")
    source = [
        "#define _GNU_SOURCE\n#include <math.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <time.h>",
        "typedef double bucket[5], abucket[4];",
        "typedef struct { int verbose; int (*progress)(void*,double,int,double); void *progress_parameter; } flam3_frame;",
        constants, helper,
        between(filters, "double flam3_spatial_support", "int flam3_create_spatial_filter"),
        kernel, scatter, between(files["rect.c"], "typedef struct {", "static void iter_thread"),
        Path(__file__).with_name("density-oracle-driver.c").read_text()
    ]
    with tempfile.TemporaryDirectory() as temp:
        folder = Path(temp)
        unit = folder / "oracle.c"
        unit.write_text("\n".join(source))
        exe = folder / "oracle"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=gnu99", "-O2", str(unit), "-lm", "-o", str(exe)], check=True)
        args.output.write_bytes(subprocess.check_output([str(exe)]))


if __name__ == "__main__":
    main()
