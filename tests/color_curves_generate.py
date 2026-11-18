"""Compile original Apophysis 7X Bezier and image-maker curve expressions.

Usage: python color_curves_generate.py /path/to/apophysis-7x fixtures/color
Set FPC to a Free Pascal compiler command when it is not on PATH. Normal tests
use committed TSV files; they do not require the upstream checkout or Pascal.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def pinned(path, digest):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError(f"Unexpected upstream bytes: {path}")
    return data


def cases():
    original = [[(0., 0., 1.), (0., 0., 1.), (1., 1., 1.), (1., 1., 1.)] for _ in range(4)]
    result = []
    for case in range(7):
        curves = [list(channel) for channel in original]
        if case == 1:  # Weights alone do not defeat the historical bypass.
            curves[0][1] = (0., 0., 3.5)
            curves[1][2] = (1., 1., 0.)
        if case == 2:  # An X-only edit activates every Y(t) channel.
            curves[0][1] = (.33, 0., 1.)
        if case == 3:
            curves[0][1:3] = [(.15, .28, .4), (.75, .83, 3.2)]
            curves[1][1:3] = [(.2, .05, 0.), (.8, .9, 16.)]
            curves[2][0] = (0., .1, .75)
            curves[2][3] = (1., .95, 2.)
            curves[3][1:3] = [(.2, .02, .5), (.8, .98, 4.)]
        if case == 4:
            curves[0] = [(0., 1., 1.), (.2, 1., 1.), (.8, 0., 1.), (1., 0., 1.)]
        if case == 5:  # Nonordered X remains defined in legacy parameter mode.
            curves[0][1:3] = [(.9, .2, .25), (.1, .8, 12.)]
            curves[2][1:3] = [(.8, .7, 2.), (.2, .1, .2)]
        if case == 6:
            curves[0] = [(0., .05, .125), (.1, .3, 8.), (.9, .7, .5), (1., .99, 16.)]
            curves[1][1] = (.4, .6, .1)
            curves[3][2] = (.6, .4, 10.)
        result.append(curves)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("upstream", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    bezier = pinned(args.upstream / "src/Core/Bezier.pas",
                    "caf01f28de79a4afd511ec48eea67333a3a9732697012e89ec24c176b6a0b0de")
    maker = pinned(args.upstream / "src/Rendering/ImageMaker.pas",
                   "c27ee92aa59c05c2da8097c1da50c35aecfaf687b76a5071cebfd2e11e52bb61").decode("latin1")
    pinned(args.upstream / "src/Flame/ControlPoint.pas",
           "8ffa29355b7ef957c0c4f1f22de2535973a3408d165893b520d5d3d9f98427db")
    bypass = re.search(r"curvesSet := true;.*?curvesSet := not curvesSet;", maker, re.S).group()
    lookup = "\n".join(re.findall(r"^\s*csa\[[0-3]\]\[i\] := BezierFunc.*?;", maker, re.M))
    mappings = []
    for channel in ("ri", "gi", "bi"):
        mappings.append(re.search(r"if \(" + channel + r" >= 0\).*?then " + channel + r" := Round\(.*?;", maker).group())
    driver = """program CurveOracle;
uses Math, Bezier;
type ControlPoint = record
  curvePoints: array [0..3] of BezierPoints;
  curveWeights: array [0..3] of BezierWeights;
end;
var fcp: ControlPoint;
    csa: array [0..3,0..256] of double;
    curvesSet: boolean;
    id, c, p, i, n, ri, gi, bi: integer;
begin
  while not eof(input) do begin
    read(id);
    for c := 0 to 3 do for p := 0 to 3 do
      read(fcp.curvePoints[c][p].x, fcp.curvePoints[c][p].y, fcp.curveWeights[c][p]);
    readln;
    @BYPASS@
    for i := 0 to 256 do begin
      @LOOKUP@
    end;
    for n := 0 to 256 do begin
      ri := n; gi := n; bi := n;
      @MAPPINGS@
      write(id, #9, n, #9, ri, #9, gi, #9, bi);
      for c := 0 to 3 do write(#9, BezierFunc(n / 256.0, fcp.curvePoints[c], fcp.curveWeights[c]):0:17);
      writeln;
    end;
  end;
end.
""".replace("@BYPASS@", bypass).replace("@LOOKUP@", lookup).replace("@MAPPINGS@", "\n".join(mappings))
    inputs = []
    point_rows = ["case\tchannel\tpoint\tx\ty\tweight"]
    for id, curves in enumerate(cases()):
        inputs.append(" ".join(map(str, [id, *(v for ch in curves for point in ch for v in point)])))
        for c, curve in enumerate(curves):
            for p, point in enumerate(curve):
                point_rows.append("\t".join(map(str, (id, c, p, *point))))
    with tempfile.TemporaryDirectory(prefix="apo-curve-oracle-") as tmp:
        build = Path(tmp)
        (build / "Bezier.pas").write_bytes(bezier)
        (build / "oracle.pas").write_text(driver)
        subprocess.run(shlex.split(os.environ.get("FPC", "fpc")) +
                       ["-Mdelphi", "-O2", "-ocurve-oracle", "oracle.pas"], cwd=build, check=True)
        output = subprocess.check_output([build / "curve-oracle"],
                                         input=("\n".join(inputs) + "\n").encode()).decode()
    if len(output.splitlines()) != len(cases()) * 257:
        raise ValueError("Incomplete curve oracle output")
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "color-curve-cases.tsv").write_text("\n".join(point_rows) + "\n")
    (args.output / "color-curve-vectors.tsv").write_text(
        "case\tinput\tred\tgreen\tblue\tmaster_raw\tred_raw\tgreen_raw\tblue_raw\n" + output)


if __name__ == "__main__":
    main()
