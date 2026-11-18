#!/usr/bin/env python3
"""Regenerate M8 affine reference data from separately obtained original source.

No historical implementation is embedded here. The pinned Apophysis source is
read into a temporary Pascal harness and removed after compilation/execution.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

XFORM_SHA256 = "0509deeacdbcd5878c4ef20bcc2488f07228b13c1a375fab9b37e4a6f08e6950"

DECLARATIONS = """program script_affine_reference;
{$mode delphi}
type TMatrix = array[0..2,0..2] of double;
TXForm=class
  c: array[0..2,0..1] of double;
  function Mul33(const M1,M2:TMatrix):TMatrix;
  function Identity:TMatrix;
  procedure Rotate(const degrees:double);
  procedure Translate(const x,y:double);
  procedure Multiply(const a,b,c,d:double);
  procedure Scale(const s:double);
end;
"""

EXPERIMENT = """
var x:TXForm; i,j:integer;
begin
  x:=TXForm.Create;
  x.c[0,0]:=0.4; x.c[0,1]:=0.2; x.c[1,0]:=-0.3;
  x.c[1,1]:=0.5; x.c[2,0]:=1; x.c[2,1]:=-2;
  x.Rotate(37); x.Scale(0.75); x.Translate(1.2,-0.7);
  x.Multiply(0.9,-0.4,0.2,1.1);
  for i:=0 to 2 do for j:=0 to 1 do writeln(x.c[i,j]:0:17);
  x.Free;
end.
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original_source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--fpc", default="fpc")
    parser.add_argument("--unit-path", action="append", default=[], type=Path)
    args = parser.parse_args()
    source = args.original_source / "Source" / "XForm.pas"
    raw = source.read_bytes()
    actual = hashlib.sha256(raw).hexdigest()
    if actual != XFORM_SHA256:
        raise SystemExit(f"Original XForm.pas hash mismatch: {actual}")
    text = raw.decode("latin1")
    start = text.index("function TXForm.Mul33(const M1, M2: TMatrix): TMatrix;", text.index("implementation"))
    scale = text.index("procedure TXForm.Scale", start)
    end = text.index("\nend;", scale) + len("\nend;")
    methods = text[start:end]
    compiler = shutil.which(args.fpc)
    if not compiler:
        raise SystemExit(f"Free Pascal compiler not found: {args.fpc}")
    environment = dict(os.environ, LC_ALL="C")
    with tempfile.TemporaryDirectory(prefix="apo-script-oracle-") as name:
        work = Path(name)
        program = work / "script_affine_reference.pas"
        program.write_text(DECLARATIONS + methods + EXPERIMENT, encoding="utf-8")
        executable = work / ("script_affine_reference.exe" if os.name == "nt" else "script_affine_reference")
        command = [compiler, f"-FE{work}", f"-FU{work}", f"-o{executable}"]
        command += [f"-Fu{path.resolve()}" for path in args.unit_path]
        command.append(str(program))
        built = subprocess.run(command, cwd=work, env=environment, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        if built.returncode:
            raise SystemExit(built.stdout)
        output = subprocess.run([str(executable)], cwd=work, env=environment,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                check=True, timeout=10).stdout
    values = output.split()
    if len(values) != 6:
        raise SystemExit(f"Expected six affine coefficients, received: {output!r}")
    for value in values:
        float(value)
    args.output.write_text("a\tb\tc\td\te\tf\n" + "\t".join(values) + "\n", encoding="utf-8")
    print(f"Wrote six original-Pascal affine values to {args.output}")


if __name__ == "__main__":
    main()
