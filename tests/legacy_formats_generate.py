"""Regenerate M8 palette and UPR fixtures with pinned Apophysis 2.09 routines.

Normal tests consume the checked-in data and do not need Delphi or Free Pascal.
This harness extracts and executes original routines; it does not use Reborn's
format implementation. VCL lists are supplied by Free Pascal Classes. UPR tests
supply a prepared control-point object and fixed magnification of 2, so they test
the original parameter writer, not its stochastic camera fitting/rendering.
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
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != digest:
        raise ValueError(f"Unexpected upstream source: {path}")
    return raw.decode("latin1").replace("\r\n", "\n")


def routine(source, declaration):
    source = re.split(r"^implementation\s*$", source, flags=re.M | re.I)[-1]
    match = re.search(r"^" + re.escape(declaration) + r".*?^end;", source, re.M | re.S)
    if not match:
        raise ValueError(f"Missing routine {declaration}")
    return match.group()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    cmap = pinned(args.source / "Source/cmap.pas", "4a6d7db9634f983346b260ee599b0b8c7334b4b6abef9a49047a57a065abe9f0")
    main_source = pinned(args.source / "Source/Main.pas", "24476b62589d45508c5c90cee1d9273ae66848f322b6f2d5a78aa792921f2b61")
    functions = "\n\n".join(routine(cmap, name) for name in (
        "procedure RGBBlend(", "function GetVal(", "function ReplaceTabs(",
        "procedure GetTokens(", "function GetPalette("))
    functions += "\n" + routine(main_source, "function TMainForm.GradientFromPalette(").replace("TMainForm.GradientFromPalette", "GradientFromPalette")
    functions += "\n" + routine(main_source, "function GradientString(")
    functions += "\n" + routine(main_source, "function TMainForm.UPRString(").replace("TMainForm.UPRString", "UPRString")
    scaffold = r'''program legacy_format_reference;
{$mode delphi}
uses Classes, SysUtils, Math;
type
  TColorMap = array[0..255] of array[0..2] of Integer;
  EFormatInvalid = class(Exception);
  TXform = class
    c: array[0..2] of array[0..1] of Double;
    weight, color, color_speed: Double;
    vars: array[0..0] of Double;
    procedure GetVariable(const name: string; out value: Double);
  end;
  TControlPoint = class
    center: array[0..1] of Double;
    width, height: Integer;
    zoom, brightness, gamma: Double;
    background: array[0..2] of Integer;
    xform: array[0..2] of TXform;
    HasFinalXForm: Boolean;
    cmap: TColorMap;
    procedure Prepare;
  end;
const NRVAR=1;
var UPRWidth, UPRHeight, UPRSampleDensity, UPROversample, Transforms: Integer;
    UPRFilterRadius: Double;
    UPRAdjustDensity: Boolean;
    UPRFormulaFile, UPRFormulaIdent, UPRColoringFile, UPRColoringIdent: string;
procedure TXform.GetVariable(const name: string; out value: Double); begin value:=0; end;
procedure TControlPoint.Prepare; begin end;
function CalcUPRMagn(cp: TControlPoint): Double; begin Result:=2; end;
function VarNames(i: Integer): string; begin Result:='linear'; end;
function GetNrVariableNames: Integer; begin Result:=0; end;
function GetVariableNameAt(i: Integer): string; begin Result:='unused'; end;
function CleanUPRTitle(const title: string): string; begin Result:=title; end;
function CleanEntry(const title: string): string; begin Result:=title; end;
@FUNCTIONS@
procedure SaveText(const filename, value: string);
var f: TextFile;
begin AssignFile(f,filename);Rewrite(f);Write(f,value);CloseFile(f);end;
procedure SaveMap(const filename: string; const p: TColorMap);
var f: TextFile; i: Integer;
begin AssignFile(f,filename);Rewrite(f);for i:=0 to 255 do WriteLn(f,p[i][0],' ',p[i][1],' ',p[i][2]);CloseFile(f);end;
var p: TColorMap; i: Integer; s: string; cp: TControlPoint;
begin
  DefaultFormatSettings.DecimalSeparator:='.';
  for i:=0 to 255 do begin p[i][0]:=i;p[i][1]:=(i*37) mod 256;p[i][2]:=255-i;end;
  SaveText('apophysis-209-export.ugr','Reference {'+#10+GradientFromPalette(p,'Reference')+'}'+#10);
  SaveMap('apophysis-209-export.map',p);
  s:='Cyclic { gradient: title="Cyclic" smooth=no index=30 color=16728064 index=145 color=8390896 index=310 color=65312 }';
  if not GetPalette(s,p) then Halt(1);
  SaveText('apophysis-209-cyclic.ugr',s+#10);SaveMap('apophysis-209-cyclic.map',p);
  s:='Ties { gradient: title="Ties" smooth=no index=0 color=0 index=399 color=16777215 }';
  if not GetPalette(s,p) then Halt(1);
  SaveText('apophysis-209-ties.ugr',s+#10);SaveMap('apophysis-209-ties.map',p);
  cp:=TControlPoint.Create;
  cp.center[0]:=0.25;cp.center[1]:=-0.375;cp.zoom:=0;cp.brightness:=1.5;cp.gamma:=2.2;
  cp.background[0]:=17;cp.background[1]:=34;cp.background[2]:=51;cp.cmap:=p;cp.HasFinalXForm:=True;
  for i:=0 to 2 do begin cp.xform[i]:=TXform.Create;cp.xform[i].c[0][0]:=0.5;cp.xform[i].c[1][1]:=0.5;cp.xform[i].weight:=1;cp.xform[i].color:=0.25*i;cp.xform[i].vars[0]:=1;end;
  cp.xform[0].c[0][1]:=0.125;cp.xform[0].c[1][0]:=-0.25;cp.xform[0].c[2][0]:=0.75;cp.xform[0].c[2][1]:=-0.5;cp.xform[0].color_speed:=-0.5;
  UPRWidth:=800;UPRHeight:=600;UPRSampleDensity:=100;UPROversample:=2;UPRFilterRadius:=0.5;UPRAdjustDensity:=False;Transforms:=2;
  UPRFormulaFile:='mt.ufm';UPRFormulaIdent:='mt-pixel';UPRColoringFile:='apophysis.ucl';UPRColoringIdent:='enr-flame-a';
  SaveText('apophysis-209-export.upr',UPRString(cp,'Reference'));
  for i:=0 to 2 do cp.xform[i].Free;cp.Free;
end.
'''
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="apo-format-reference-") as temp:
        root = Path(temp)
        program = root / "reference.pas"
        program.write_text(scaffold.replace("@FUNCTIONS@", functions))
        cmd = shlex.split(os.environ.get("FPC", "fpc"))
        cmd += shlex.split(os.environ.get("FPCFLAGS", ""))
        subprocess.run([*cmd, "-Mdelphi", "-O1", "-o" + str(root / "reference"), str(program)], check=True)
        subprocess.run([str(root / "reference")], cwd=args.output, check=True)


if __name__ == "__main__":
    main()
