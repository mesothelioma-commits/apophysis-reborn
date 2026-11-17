"""Regenerate classic variation vectors using independently compiled upstream functions.

Requires the pinned FLAM3 3.1.1 and Apophysis 2.09 source trees, a C compiler and
Free Pascal. These tools are optional: normal CTest uses the saved TSV only.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from reference_generate import c_function, macro


OPERATIONS = ((15, "waves", "Waves"), (16, "fisheye", "Fisheye"),
              (17, "popcorn", "Popcorn"), (18, "exponential", "Exponential"),
              (19, "power", "Power"), (20, "cosine", "Cosine"),
              (21, "rings", "Rings"), (22, "fan", "Fan"),
              (27, "eyefish", "Eyefish"), (29, "cylinder", "Cylinder"))
MATRICES = ((.69, .21, -.37, .52, .33, -.46), (-.41, .63, .28, -.71, -.57, .26))
POINTS = ((.3, .7), (-.31, .73), (-.78, -.27), (.45, -.86),
          (0, -.75), (.7, 0), (1e-6, -2e-6), (0, 0))


def pinned(path, digest):
    content = path.read_bytes()
    actual = hashlib.sha256(content).hexdigest()
    if actual != digest:
        raise ValueError(f"Unexpected upstream source bytes in {path}: {actual}")
    return content.decode("latin1")


def pascal_method(source, name):
    match = re.search(r"^procedure TXForm\." + name + r";.*?^end;", source, re.M | re.S)
    if not match:
        raise ValueError(f"Missing original Pascal method {name}")
    # Select the original high-level implementation instead of Delphi's x86 assembler.
    return re.sub(r"\{\$ifndef _ASM_\}(.*?)\{\$else\}.*?\{\$endif\}",
                  r"\1", match.group(), flags=re.S | re.I)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("flam3_source", type=Path)
    parser.add_argument("apophysis_209_source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    c_source = pinned(args.flam3_source / "variations.c",
                      "e5880158e4dd35cf9f4e89be23fe3d659954e6574cb218db5e20279e53cefab2")
    private = pinned(args.flam3_source / "private.h",
                     "548a082cd1f73bb5180b69220e94a5641a61b49403cb68eb5540c2cfd685c2f0")
    pas_source = pinned(args.apophysis_209_source / "Source/XForm.pas",
                        "0509deeacdbcd5878c4ef20bcc2488f07228b13c1a375fab9b37e4a6f08e6950")
    c_bodies = "\n".join(c_function(c_source, "void", f"var{n}_{name}")
                         for n, name, _ in OPERATIONS)
    c_calls = "\n".join(f"case {n}: var{n}_{name}(&helper, weight); break;"
                         for n, name, _ in OPERATIONS)
    c_template = Path(__file__).with_name("extended_variation_driver.c").read_text()
    c_unit = c_template.replace("@EPS@", macro(private, "EPS"))
    c_unit = c_unit.replace("@FUNCTIONS@", c_bodies).replace("@CALLS@", c_calls)
    pas_bodies = "\n".join(pascal_method(pas_source, name)
                           for name in ("PrecalcAll", *(item[2] for item in OPERATIONS)))
    pas_calls = "\n".join(f"{n}: xf.{name};" for n, _, name in OPERATIONS)
    pas_template = Path(__file__).with_name("extended_variation_driver.pas.in").read_text()
    pas_unit = pas_template.replace("@METHODS@", pas_bodies).replace("@CALLS@", pas_calls)
    rows = []
    inputs = []
    for kind, _, _ in OPERATIONS:
        for matrix, coefficients in enumerate(MATRICES):
            for weight in (1., -.375):
                for x, y in POINTS:
                    rows.append((kind, matrix, weight, x, y))
                    inputs.append(" ".join(map(repr, (kind, *coefficients, weight, x, y))))
    stdin = ("\n".join(inputs) + "\n").encode()
    output = ["producer\tkind\tmatrix\tweight\tx\ty\texpected_x\texpected_y"]
    with tempfile.TemporaryDirectory(prefix="apo-variation-oracle-") as tmp:
        build = Path(tmp)
        (build / "oracle.c").write_text(c_unit)
        (build / "oracle.pas").write_text(pas_unit)
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                       ["-std=gnu99", "-O2", "oracle.c", "-lm", "-o", "flam3-oracle"],
                       cwd=build, check=True)
        subprocess.run(shlex.split(os.environ.get("FPC", "fpc")) +
                       ["-Mdelphi", "-O2", "-oapo-oracle", "oracle.pas"], cwd=build, check=True)
        for producer, exe in (("flam3", "flam3-oracle"), ("apo209", "apo-oracle")):
            results = subprocess.check_output([build / exe], input=stdin).decode().splitlines()
            if len(results) != len(rows):
                raise ValueError(f"Incomplete {producer} results")
            for row, result in zip(rows, results):
                fields = result.split()
                if len(fields) != 2:
                    raise ValueError(f"Unexpected oracle output {result!r}")
                output.append("\t".join(map(str, (producer, *row, *fields))))
    args.output.write_text("\n".join(output) + "\n")


if __name__ == "__main__":
    main()
