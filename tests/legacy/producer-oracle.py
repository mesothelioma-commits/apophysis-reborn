"""Compile original Apophysis Pascal image-maker methods in an in-memory bitmap harness.

Run separately with 2.09 and 7X source directories. This tool regenerates reference
pixels. It is optional when running the project's normal tests.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


def method(text, name):
    return re.search(r"^(?:procedure|function) TImageMaker\." + name + r"\b.*?^end;", text, re.S | re.M).group()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_root", type=Path)
    parser.add_argument("out_dir", type=Path)
    args = parser.parse_args()
    root = args.source_root
    seven = (root / "src/Rendering/ImageMaker.pas").is_file()
    source = root / ("src/Rendering/ImageMaker.pas" if seven else "Source/ImageMaker.pas")
    expected = "c27ee92aa59c05c2da8097c1da50c35aecfaf687b76a5071cebfd2e11e52bb61" if seven else "66567190a6843b61a5949a3b97924cfe04edcb1d58a0ccf55b11f25c3a73df9e"
    if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
        raise ValueError("ImageMaker.pas does not match the pinned upstream snapshot")
    original = source.read_text(encoding="latin1")
    cp_path = root / ("src/Flame/ControlPoint.pas" if seven else "Source/ControlPoint.pas")
    cp_digest = "8ffa29355b7ef957c0c4f1f22de2535973a3408d165893b520d5d3d9f98427db" if seven else "fff973828c6669777641108279b2ce51a5cd8f4c5c11c412bc08b5d45e54f5d3"
    if hashlib.sha256(cp_path.read_bytes()).hexdigest() != cp_digest:
        raise ValueError("ControlPoint.pas does not match the pinned upstream snapshot")
    cp = cp_path.read_text(encoding="latin1")
    constants = "\n".join(re.search(r"^\s*" + key + r"\s*=\s*[^;]+;", cp, re.M).group()
                          for key in ("FILTER_CUTOFF", "PREFILTER_WHITE", "BRIGHT_ADJUST"))
    methods = ["CreateFilter", "NormalizeFilter", "SafeGetBucket", "CreateImage"]
    if seven:
        methods.append("InitDE")
    template = Path(__file__).with_name("producer-oracle.pas.in").read_text()
    unit = template.replace("@CONSTANTS@", constants).replace("@ORIGINAL_METHODS@", "\n".join(method(original, m) for m in methods))
    args.out_dir.mkdir(parents=True, exist_ok=True)
    output = args.out_dir.resolve()
    with tempfile.TemporaryDirectory() as tmp:
        build = Path(tmp)
        (build / "harness.pas").write_text(unit, encoding="latin1")
        if seven:
            bezier = root / "src/Core/Bezier.pas"
            if hashlib.sha256(bezier.read_bytes()).hexdigest() != "caf01f28de79a4afd511ec48eea67333a3a9732697012e89ec24c176b6a0b0de":
                raise ValueError("Bezier.pas does not match the pinned upstream snapshot")
            shutil.copyfile(bezier, build / "Bezier.pas")
        compiler = shlex.split(os.environ.get("FPC", "fpc"))
        subprocess.run(compiler + ["-Mdelphi", "-O2", *(["-dSEVEN"] if seven else []), "harness.pas"], cwd=build, check=True)
        filters = []
        for name, oversample, radius, threshold in (("direct",1,0,0.0), ("low",1,0,0.8),
                ("gaussian",1,0.75,0.05), ("even",2,0.5,0.15), ("wide",3,0.8,0.4)):
            ppm = output / f"producer-oracle-{name}.ppm"
            result = subprocess.check_output([build/"harness",str(oversample),str(radius),str(threshold),ppm])
            if name != "low":
                filters.append(result)
        (output / "producer-oracle-kernels.tsv").write_bytes(b"os\tradius\twidth\trow\tcol\tweight\n"+b"".join(filters))


if __name__ == "__main__":
    main()
