#!/usr/bin/env python3
"""Generate the 7X blur reference points in producer_variation_tests.cpp.

Pass the root of the pinned Apophysis 7X checkout. FPC may specify a Free
Pascal compiler and additional arguments. All compilation uses a temporary
directory. The generated driver supplies a fixed stream in place of Random.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


XFORM = "src/Flame/XForm.pas"
RADIAL = "src/Variations/varRadialBlur.pas"
SOURCE_HASHES = {
    XFORM: "4f002256d00cd8c72e8437287fed10f9e5b72dd47b6c703203112960a3b8a55a",
    RADIAL: "d23adb59fc34d0560c7fe05e9b37128c7530d5c5b696abda36dd0d6cfb94387f",
}
MASK = (1 << 32) - 1


def mix(value):
    value &= MASK
    value ^= value >> 16
    value = value * 0x7FEB352D & MASK
    value ^= value >> 15
    value = value * 0x846CA68B & MASK
    return value ^ (value >> 16)


def uniform_draws():
    # Identical portable-stream inputs to Rng(81, 2). The variation geometry
    # and ring updates themselves come from the independently compiled methods.
    state = mix(81 ^ mix(0x9E3779B9) ^ mix(2 + 0x85EBCA6B) ^ mix(0xC2B2AE35))
    for _ in range(64):
        state = (state * 747796405 + 2891336453) & MASK
        word = ((state >> ((state >> 28) + 4)) ^ state) * 277803737 & MASK
        yield ((word ^ (word >> 22)) >> 8) * 2**-24


def source_text(root, name):
    data = (root / name).read_bytes().replace(b"\r\n", b"\n")
    if hashlib.sha256(data).hexdigest() != SOURCE_HASHES[name]:
        raise ValueError(f"Unexpected 7X source revision for {name}")
    return data.decode("latin1")


def method(source, name):
    pattern = rf"^procedure {re.escape(name)};.*?^end;"
    match = re.search(pattern, source, re.MULTILINE | re.IGNORECASE | re.DOTALL)
    if not match:
        raise ValueError(f"Missing original method {name}")
    return match.group().replace("Randomize;", "")


DECLARATIONS = """
type
  TXForm = class
    vars: array[0..31] of double;
    FTx, FTy, FTz, FPx, FPy, FPz: double;
    gauss_rnd: array[0..3] of double;
    gauss_N: integer;
    procedure Gaussian;
    procedure PreBlur;
  end;
  TVariationRadialBlur = class
    FTx, FTy, FTz, FPx, FPy, FPz: ^double;
    vvar, spin_var, zoom_var: double;
    rnd: array[0..3] of double;
    N: integer;
    procedure CalcFunction;
  end;

var i, j, cursor: integer; t: TXForm; rb: TVariationRadialBlur;

function Random: double;
begin
  Result := draws[cursor];
  Inc(cursor);
end;
"""

EXERCISE = """
begin
  DefaultFormatSettings.DecimalSeparator := '.';
  t := TXForm.Create;
  rb := TVariationRadialBlur.Create;
  rb.FTx := @t.FTx; rb.FTy := @t.FTy; rb.FTz := @t.FTz;
  rb.FPx := @t.FPx; rb.FPy := @t.FPy; rb.FPz := @t.FPz;
  t.vars[16] := 0.7; t.vars[19] := 0.2;
  rb.vvar := -0.61;
  rb.spin_var := rb.vvar * sin(0.37*pi/2);
  rb.zoom_var := rb.vvar * cos(0.37*pi/2);
  for i := 0 to 3 do t.gauss_rnd[i] := Random;
  for i := 0 to 6 do begin
    t.FTx := 0.63; t.FTy := -0.47;
    t.PreBlur;
    t.FPx := 0.3*t.FTx; t.FPy := 0.3*t.FTy;
    t.Gaussian;
    if i = 0 then for j := 0 to 3 do rb.rnd[j] := Random;
    rb.CalcFunction;
    writeln(FloatToStrF(t.FPx, ffGeneral, 17, 0), ' ',
            FloatToStrF(t.FPy, ffGeneral, 17, 0), ' ', cursor);
  end;
  writeln(FloatToStrF(Random, ffGeneral, 17, 0));
  rb.Free; t.Free;
end.
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apophysis_7x", type=Path)
    root = parser.parse_args().apophysis_7x
    xform = source_text(root, XFORM)
    radial = source_text(root, RADIAL)
    draws = ", ".join(map(repr, uniform_draws()))
    source = "\n".join([
        "program check; {$mode objfpc} uses math, sysutils;",
        f"const draws: array[0..63] of double = ({draws});",
        DECLARATIONS, method(xform, "TXForm.Gaussian"), method(xform, "TXForm.PreBlur"),
        method(radial, "TVariationRadialBlur.CalcFunction"), EXERCISE,
    ])
    with tempfile.TemporaryDirectory() as folder:
        directory = Path(folder)
        (directory / "check.pas").write_text(source)
        subprocess.run([*shlex.split(os.environ.get("FPC", "fpc")), "check.pas"],
                       cwd=directory, check=True, stdout=subprocess.DEVNULL)
        executable = "check.exe" if os.name == "nt" else "check"
        subprocess.run([directory / executable], check=True)


if __name__ == "__main__":
    main()
