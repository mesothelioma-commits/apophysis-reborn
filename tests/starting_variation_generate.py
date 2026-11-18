"""Regenerate the 12 parameterized/stochastic variation fixtures from pinned FLAM3.

Run with the FLAM3 source directory and the destination TSV. The upstream
functions receive draws from the application's specified MT19937-64 uniform
sequence so that geometry, draw order and consumption can be compared directly.
"""
import argparse
import hashlib
import itertools
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from reference_generate import c_function, macro

# Use nondefault values including signed coefficients and a zero rectangle axis.
CASES = [
    (23, "blob", "blob", dict(blob_low=.23, blob_high=1.17, blob_waves=3.75)),
    (24, "pdj", "pdj", dict(pdj_a=-.85, pdj_b=1.71, pdj_c=2.19, pdj_d=-1.43)),
    (25, "fan2", "fan2", dict(fan2_x=.64, fan2_y=-.32)),
    (26, "rings2", "rings2", dict(rings2_val=.56)),
    (30, "perspective", "perspective", dict(perspective_angle=-.45, perspective_dist=1.7)),
    (33, "juliascope", "juliaScope_generic", dict(juliascope_power=-3, juliascope_dist=.84)),
    (35, "gaussian_blur", "gaussian", {}),
    (36, "radial_blur", "radial_blur", dict(radial_blur_angle=-.72)),
    (37, "pie", "pie", dict(pie_slices=7, pie_rotation=-.31, pie_thickness=.43)),
    (38, "ngon", "ngon", dict(ngon_sides=5, ngon_power=1.4, ngon_circle=.31, ngon_corners=.79)),
    (39, "curl", "curl", dict(curl_c1=.31, curl_c2=-.49)),
    (40, "rectangles", "rectangles", dict(rectangles_x=0, rectangles_y=.53)),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = (args.source / "variations.c").read_bytes()
    if hashlib.sha256(source).hexdigest() != "e5880158e4dd35cf9f4e89be23fe3d659954e6574cb218db5e20279e53cefab2":
        raise ValueError("Expected FLAM3 1cdfc772 variations.c")
    source = source.decode()
    bodies, calls, fields = [], [], []
    for number, name, function, params in CASES:
        symbol = f"var{number}_{function}"
        bodies.append(c_function(source, "void", symbol))
        fields.extend(params.keys())
        setters = " ".join(f"xf.{key} = {value};" for key, value in params.items())
        calls.append(f"case {number}: {{ {setters} prepare(&xf); {symbol}(&f, weight); break; }}")
    prep = ["perspective_precalc", "juliaScope_precalc", "radial_blur_precalc"]
    bodies.extend(c_function(source, "void", symbol) for symbol in prep)
    fields.extend("persp_vsin persp_vfcos juliascope_rN juliascope_cn radialBlur_spinvar radialBlur_zoomvar".split())
    template = Path(__file__).with_name("starting_variation_driver.cpp.in").read_text()
    code = template.replace("@FIELDS@", ", ".join(fields)).replace("@BODIES@", "\n".join(bodies))
    code = code.replace("@PREP@", " ".join(f"{symbol}(xf);" for symbol in prep)).replace("@CALLS@", "\n".join(calls))
    code = code.replace("@EPS@", macro((args.source / "private.h").read_text(), "EPS"))
    rows, inputs = [], []
    points = [(.31, -.82), (-.62, .48), (-.44, -.16), (.74, .23), (0, -.3), (1e-5, .002)]
    for number, name, _, params in CASES:
        attributes = " ".join(f"{k}='{v}'" for k, v in params.items()) or "-"
        for (x, y), weight, seed in itertools.product(points, (1., -.375), (3, 17, 916)):
            inputs.append(f"{number} {x} {y} {weight} {seed}")
            rows.append(f"{name}\t{attributes}\t{x}\t{y}\t{weight}\t{seed}")
    with tempfile.TemporaryDirectory() as folder:
        build = Path(folder)
        (build / "oracle.cpp").write_text(code)
        subprocess.run([*shlex.split(os.getenv("CXX", "c++")), "-std=c++17", "-O2", "oracle.cpp", "-o", "oracle"],
                       check=True, cwd=build)
        outputs = subprocess.check_output([build / "oracle"], input="\n".join(inputs)+"\n", text=True).splitlines()
    header = "name\tattributes\tx\ty\tweight\tseed\texpected_x\texpected_y\tnext_draw"
    args.output.write_text("\n".join([header] + [r+"\t"+v for r, v in zip(rows, outputs, strict=True)]) + "\n")


if __name__ == "__main__":
    main()
