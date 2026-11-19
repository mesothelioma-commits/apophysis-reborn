# Compatible with Windows PowerShell 5.1. Standard output for device mode is the CLI device ID.
param(
    [Parameter(Mandatory=$true,Position=0)][ValidateSet('build','starter','device','png')][string] $Mode,
    [Parameter(Mandatory=$true,Position=1)][string] $InputFile,
    [Parameter(Position=2)][string] $Value1,
    [Parameter(Position=3)][string] $Value2
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Assert-That([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}

try {
    if ($Mode -ne 'png') {
        $document = Get-Content -LiteralPath $InputFile -Raw -Encoding UTF8 | ConvertFrom-Json
    }
    switch ($Mode) {
        'build' {
            Assert-That ($document.schema -ceq 'apophysis-reborn.build-info' -and $document.schema_version -eq 1) 'Unknown build-info schema.'
            Assert-That ($document.version -ceq '0.13.0') "Expected version 0.13.0, found $($document.version)."
            Assert-That ($document.target_os -ieq 'Windows' -and $document.target_arch -in @('x86_64','AMD64','x64') -and $document.pointer_bits -eq 64 -and $document.build_type -ieq 'Release') 'Expected a Windows x64 Release executable.'
            Assert-That ($document.vulkan_compiled -is [bool] -and $document.vulkan_compiled -and $document.editor_compiled -is [bool] -and $document.editor_compiled -and $document.qt_version -match '^6\.') 'Expected the Qt 6 editor and Vulkan support in this build.'
            $revision = $document.PSObject.Properties['source_revision']
            if ($null -ne $revision -and $revision.Value -match '^[0-9a-fA-F]{40}$') {
                Write-Output $revision.Value.ToLowerInvariant()
            } else {
                Write-Output 'unknown'
            }
        }
        'starter' {
            $names = @('Ember spiral','Glass fern','Ruby rosette','Prism lace','Tidal filaments','Copper dragon','Violet nebula','Golden nautilus')
            Assert-That ($document.entries -is [array] -and $document.entries.Count -eq $names.Count) 'Expected all eight starter flames.'
            for ($i = 0; $i -lt $names.Count; ++$i) {
                $entry = $document.entries[$i]
                Assert-That ($entry.index -eq $i + 1 -and $entry.name -ceq $names[$i] -and $entry.status -ceq 'renderable') "Entry $($i+1) must be the renderable starter '$($names[$i])'."
            }
            Write-Output 'Verified all eight renderable starter flames in release order.'
        }
        'device' {
            Assert-That ($document.devices -is [array] -and $document.compiled -is [bool] -and $document.loader_available -is [bool]) 'Invalid Vulkan inventory.'
            foreach ($candidate in $document.devices) {
                Assert-That ($candidate.usable -is [bool] -and $candidate.software -is [bool] -and $candidate.id -is [string] -and $candidate.id -match '^vk:[A-Za-z0-9:._-]+$' -and $candidate.type -is [string] -and $null -ne $candidate.index) 'Invalid Vulkan device record.'
            }
            $eligible = @($document.devices | Where-Object {
                $document.compiled -and $document.loader_available -and $_.usable -and -not $_.software -and
                    $_.type -in @('Discrete GPU','Integrated GPU')
            })
            if ($Value1) {
                $match = @($eligible | Where-Object { $_.id -ceq $Value1 -or [string]$_.index -ceq $Value1 })
                if ($match.Count -ne 1) { [Console]::Error.WriteLine('The requested device is not a usable physical integrated or discrete GPU.'); exit 4 }
                Write-Output $match[0].id
            } else {
                if ($eligible.Count -eq 0) { [Console]::Error.WriteLine('The inventory lists no usable integrated or discrete GPU.'); exit 3 }
                $preferred = @($eligible | Where-Object { $_.type -ceq 'Discrete GPU' })
                if ($preferred.Count) { Write-Output $preferred[0].id } else { Write-Output $eligible[0].id }
            }
        }
        'png' {
            [int]$width = $Value1
            [int]$height = $Value2
            Assert-That ($width -ge 1 -and $height -ge 1 -and $width -le 16384 -and $height -le 16384) 'Supply expected dimensions from 1 to 16384.'
            # Decode without allocating a full canvas. Supports noninterlaced RGB/RGBA output from Qt and the native image encoder.
            Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.IO;
using System.IO.Compression;
using System.Collections.Generic;
using System.Text;

public static class ApoTestPng {
    struct Segment {
        public long Position; public int Count;
        public Segment(long pos, int n) { Position=pos; Count=n; }
    }
    // Present the PNG's IDAT chunks as one raw DeflateStream, excluding the zlib header and trailer.
    sealed class ImageData : Stream {
        readonly FileStream file; readonly List<Segment> parts;
        int index, offset; long remaining;
        public ImageData(FileStream f, List<Segment> s, long length) {
            file=f; parts=s; remaining=length;
        }
        public void ExcludeTrailer() { remaining -= 4; }
        public long Remaining { get { return remaining; } }
        public override int Read(byte[] b, int at, int count) {
            if (remaining==0 || count==0) return 0;
            while (index<parts.Count && offset==parts[index].Count) { index++; offset=0; }
            if (index>=parts.Count) throw new EndOfStreamException();
            Segment p=parts[index];
            file.Position=p.Position+offset;
            int n=Math.Min((int)Math.Min(count,remaining),p.Count-offset);
            n=file.Read(b,at,n);
            if (n==0) throw new EndOfStreamException();
            offset+=n; remaining-=n; return n;
        }
        public override bool CanRead { get { return true; } }
        public override bool CanSeek { get { return false; } }
        public override bool CanWrite { get { return false; } }
        public override long Length { get { throw new NotSupportedException(); } }
        public override long Position { get { throw new NotSupportedException(); } set { throw new NotSupportedException(); } }
        public override void Flush() { }
        public override long Seek(long off, SeekOrigin how) { throw new NotSupportedException(); }
        public override void SetLength(long n) { throw new NotSupportedException(); }
        public override void Write(byte[] b,int at,int n) { throw new NotSupportedException(); }
    }
    static void Need(bool ok, string message) { if(!ok) throw new InvalidDataException(message); }
    static void ReadFully(Stream f, byte[] b, int at, int n) {
        while (n>0) { int have=f.Read(b,at,n); if(have<=0) throw new EndOfStreamException("Truncated PNG."); at+=have; n-=have; }
    }
    static uint U32(byte[] b,int at) { return ((uint)b[at]<<24) | ((uint)b[at+1]<<16) | ((uint)b[at+2]<<8) | b[at+3]; }
    static uint[] BuildCrcTable() {
        uint[] table=new uint[256];
        for (uint i=0;i<256;i++) { uint v=i; for (int k=0;k<8;k++) v=(v&1)==0 ? v>>1 : (v>>1)^0xedb88320U; table[i]=v; }
        return table;
    }
    static readonly uint[] CrcTable=BuildCrcTable();
    static uint Crc(uint v, byte[] b,int at,int count) {
        for (int end=at+count;at<end;at++) v=(v>>8)^CrcTable[(v^b[at])&255]; return v;
    }
    static int Paeth(int a,int b,int c) {
        int p=a+b-c, x=Math.Abs(p-a), y=Math.Abs(p-b), z=Math.Abs(p-c);
        return x<=y && x<=z ? a : y<=z ? b : c;
    }
    static void Unfilter(byte[] row,byte[] prev,int stride) {
        int f=row[0];
        for (int i=1;f!=0 && i<row.Length;i++) {
            int left=i>stride ? row[i-stride] : 0, above=prev[i], upperLeft=i>stride ? prev[i-stride] : 0;
            int add=f==1 ? left : f==2 ? above : f==3 ? (left+above)/2 : Paeth(left,above,upperLeft);
            row[i]=(byte)((row[i]+add)&255);
        }
    }
    public static string Verify(string path,int w,int h) {
        using(FileStream file=File.OpenRead(path)) {
            byte[] header=new byte[8], buf=new byte[65536], crcBuffer=new byte[4];
            byte[] signature={137,80,78,71,13,10,26,10}; ReadFully(file,header,0,8);
            for (int i=0;i<8;i++) Need(signature[i]==header[i],"Invalid PNG signature.");
            List<Segment> parts=new List<Segment>(); int depth=0,channels=0; bool endData=false;
            long total=0; uint trailer=0;
            while(true) {
                ReadFully(file,header,0,8);
                uint size=U32(header,0); string kind=Encoding.ASCII.GetString(header,4,4);
                Need(size<=int.MaxValue && size<=file.Length-file.Position-4,"Invalid PNG chunk length.");
                int n=(int)size; uint crc=Crc(uint.MaxValue,header,4,4);
                if(depth==0) Need(kind=="IHDR" && n==13,"Missing PNG header.");
                if(kind=="IDAT") {
                    Need(!endData && parts.Count<131072,"Unexpected image data or too many chunks.");
                    parts.Add(new Segment(file.Position,n)); total+=n;
                } else if(parts.Count!=0) endData=true;
                if(kind=="IHDR") Need(depth==0 && n==13,"Invalid or repeated PNG header.");
                if(kind=="IEND") Need(n==0 && total>=6,"Incomplete PNG image data.");
                for (int rem=n; rem>0;) {
                    int have=Math.Min(rem,buf.Length); ReadFully(file,buf,0,have); rem-=have;
                    crc=Crc(crc,buf,0,have);
                    if(kind=="IHDR") {
                        Need(U32(buf,0)==(uint)w && U32(buf,4)==(uint)h,"Wrong PNG dimensions.");
                        depth=buf[8]; channels=buf[9]==2 ? 3 : buf[9]==6 ? 4 : 0;
                        Need((depth==8 || depth==16) && channels>0 && buf[10]==0 && buf[11]==0 && buf[12]==0,"Expected noninterlaced RGB or RGBA PNG.");
                    }
                    if(kind=="IDAT") {
                        if(have>=4) trailer=U32(buf,have-4);
                        else for(int j=0;j<have;j++) trailer=(trailer<<8)|buf[j];
                    }
                }
                ReadFully(file,crcBuffer,0,4); Need(~crc==U32(crcBuffer,0),"PNG chunk checksum mismatch.");
                if(kind=="IEND") { Need(file.Position==file.Length,"Unexpected bytes after PNG end."); break; }
            }
            ImageData input=new ImageData(file,parts,total);
            byte[] zHeader=new byte[2]; ReadFully(input,zHeader,0,2); input.ExcludeTrailer();
            Need((zHeader[0]&15)==8 && zHeader[0]>>4<=7 && (zHeader[1]&32)==0 && ((zHeader[0]<<8)+zHeader[1])%31==0,"Invalid zlib header.");
            int stride=channels*(depth/8), rgbBytes=3*(depth/8);
            byte[] cur=new byte[1+w*stride], prev=new byte[cur.Length], first=new byte[rgbBytes];
            bool varied=false, visible=false; ulong a=1,b=0;
            using(DeflateStream decoder=new DeflateStream(input,CompressionMode.Decompress,true)) {
                for(int y=0;y<h;y++) {
                    ReadFully(decoder,cur,0,cur.Length); Need(cur[0]<=4,"Invalid PNG scanline filter.");
                    for(int k=0;k<cur.Length;k++) { a+=cur[k]; b+=a; } a%=65521; b%=65521;
                    // Once a visible non-uniform image has been demonstrated, continue validating every remaining compressed scanline and checksum.
                    if(varied && visible) continue;
                    Unfilter(cur,prev,stride);
                    if(y==0) Array.Copy(cur,1,first,0,rgbBytes);
                    for(int x=1;x<cur.Length && !(varied && visible);x+=stride) {
                        bool nonzero=false, opaque=channels==3;
                        for(int c=0;c<rgbBytes;c++) { nonzero |= cur[x+c]!=0; varied |= cur[x+c]!=first[c]; }
                        for(int c=rgbBytes;c<stride;c++) opaque |= cur[x+c]!=0;
                        visible |= nonzero && opaque;
                    }
                    byte[] swap=cur; cur=prev; prev=swap;
                }
                Need(decoder.ReadByte()==-1 && input.Remaining==0,"Unexpected PNG image length.");
            }
            Need((((uint)b<<16)|(uint)a)==trailer,"PNG payload checksum mismatch.");
            Need(varied && visible,"PNG has no visible colour detail or is uniformly filled.");
            return String.Format("Verified {0} x {1} PNG, {2} bits per channel, {3} channels, complete non-uniform pixels.",w,h,depth,channels);
        }
    }
}
'@
            [ApoTestPng]::Verify($InputFile, $width, $height)
        }
    }
    exit 0
} catch {
    [Console]::Error.WriteLine($_.Exception.GetBaseException().Message)
    exit 1
}
