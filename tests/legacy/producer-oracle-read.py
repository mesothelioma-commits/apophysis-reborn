"""Exercise Apophysis 2.09's original numeric and palette field readers on a Reborn save/export."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def pinned(path, sha256):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != sha256:
        raise ValueError(f"Unexpected source revision: {path}")
    return data.decode("latin1").replace("\r\n", "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_209", type=Path)
    parser.add_argument("reborn_exe", type=Path)
    parser.add_argument("input_xml", type=Path, help="First flame needs an explicit complete RGB palette")
    args = parser.parse_args()
    src = args.source_209 / "Source"
    mainpas = pinned(src / "Main.pas", "24476b62589d45508c5c90cee1d9273ae66848f322b6f2d5a78aa792921f2b61")
    cmap = pinned(src / "cmap.pas", "4a6d7db9634f983346b260ee599b0b8c7334b4b6abef9a49047a57a065abe9f0")
    tokens = re.search(r"^procedure GetTokens[^\n]*\nvar\n.*?^end;", cmap, re.S | re.M).group()
    packed = re.search(r"^procedure ParseCompactColors\b.*?^end;", mainpas, re.S | re.M).group()
    camera = mainpas.split("    v := Attributes.Value('scale');",1)[1].split("    try\n      v := Attributes.Value('background');",1)[0]
    single = mainpas.split("    if TagName = 'color' then",1)[1].split("    if TagName = 'colors' then",1)[0]
    unit = """
program ReadProducerFields;
{$mode delphi}
uses SysUtils, Classes, Math;
type TControlPoint=class
       center:array[0..1] of double;
       pixels_per_unit,FAngle:double;
       width,height,cmapindex:integer;
       cmap:array[0..255,0..2] of byte;
     end;
     TAttrs=class
       fields:TStringList;
       function Value(key:string):string;
     end;
var Parsecp:TControlPoint; Attributes:TAttrs; Tokens:TStringList;
    i,j,k,n:integer; line,v,data:string; floatcolor:double; f:TextFile;
function TAttrs.Value(key:string):string;
begin result:=fields.Values[key]; end;
""" + tokens + "\n" + packed + """
begin
 DefaultFormatSettings.DecimalSeparator:='.';
 Attributes:=TAttrs.Create; Attributes.fields:=TStringList.Create;
 ParseCP:=TControlPoint.Create; Tokens:=TStringList.Create;
 AssignFile(f,ParamStr(1)); reset(f);
 readln(f,v); Attributes.fields.Values['scale']:=v;
 readln(f,v); Attributes.fields.Values['center']:=v;
 readln(f,v); Attributes.fields.Values['size']:=v;
 v:=Attributes.Value('scale');
""" + camera + """
 WriteLn(Parsecp.width,' ',Parsecp.height,' ',FloatToStr(Parsecp.center[0]),' ',
         FloatToStr(Parsecp.center[1]),' ',FloatToStr(Parsecp.pixels_per_unit));
 readln(f,line);
 if line='packed' then begin
   data:=''; while not eof(f) do begin readln(f,line); data:=data+line+#10; end;
   ParseCompactColors(ParseCP,256,data,false);
 end else begin
   for k:=0 to 255 do begin
     readln(f,line); n:=Pos(' ',line);
     Attributes.fields.Values['index']:=copy(line,1,n-1);
     Attributes.fields.Values['rgb']:=copy(line,n+1,length(line));
""" + single + """
   end;
 end;
 CloseFile(f);
 for i:=0 to 255 do for j:=0 to 2 do Write(IntToHex(Parsecp.cmap[i][j],2));
 WriteLn;
 Tokens.free; Attributes.fields.free; Attributes.free; ParseCP.free;
end.
"""
    with tempfile.TemporaryDirectory() as tmp:
        build = Path(tmp)
        (build/"reader.pas").write_text(unit, encoding="latin1")
        subprocess.run(shlex.split(os.environ.get("FPC","fpc")) + ["-Mdelphi","-Sa","reader.pas"], cwd=build, check=True)
        cli = args.reborn_exe.resolve()
        saved, exported = build/"saved.flame", build/"canonical.flame"
        subprocess.run([cli,"save",args.input_xml,"--output",saved,"--index","1",
                        "--size","321","123","--center","0.125","-0.3125","--scale","42.625"], check=True)
        subprocess.run([cli,"export",saved,"--output",exported,"--index","1"], check=True)
        palette_reference = None
        for path in (args.input_xml,saved,exported):
            root = ET.parse(path).getroot()
            f = root if root.tag == "flame" else root.find("flame")
            fields = [f.attrib[k] for k in ("scale","center","size")]
            palette = f.find("palette")
            if palette is None:
                colors = f.findall("color")
                assert len(colors) == 256
                fields.extend(["indexed"]+[c.attrib["index"]+" "+c.attrib["rgb"] for c in colors])
                expected = bytes(round(float(x)) for c in sorted(colors,key=lambda c:int(c.attrib["index"])) for x in c.attrib["rgb"].split())
            else:
                assert palette.attrib["format"] == "RGB" and palette.attrib["count"] == "256"
                fields.extend(["packed",palette.text])
                expected = bytes.fromhex(palette.text)
            (build/"fields.txt").write_text("\n".join(fields)+"\n")
            actual = subprocess.check_output([build/"reader",build/"fields.txt"],text=True).splitlines()
            values = list(map(float,actual[0].split()))
            check = [*map(float,fields[2].split()),*map(float,fields[1].split()),float(fields[0])]
            if values != check or (path != args.input_xml and values != [321,123,.125,-.3125,42.625]):
                raise ValueError(f"Camera differs for {path.name}: {values}")
            if len(expected) != 768 or bytes.fromhex(actual[1]) != expected:
                raise ValueError(f"Original Apophysis reader differs on RGB palette for {path.name}")
            if palette_reference is not None and expected != palette_reference:
                raise ValueError(f"Palette changed while saving or exporting {path.name}")
            palette_reference = expected
        print("Original 2.09 routines decoded size, center, scale and all 768 palette bytes before and after Reborn save and export.")


if __name__ == "__main__":
    main()
