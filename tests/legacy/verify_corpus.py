"""Check original historical documents with the built CLI. The ordinary build stays offline."""
import argparse
import hashlib
from html.parser import HTMLParser
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from urllib.request import Request, urlopen
import xml.etree.ElementTree as ET
from zipfile import ZipFile

HERE = Path(__file__).resolve().parent
AV_RELEASES = (
    "https://master.dl.sourceforge.net/project/apophysis-av/Apophysis%20AV_v_1.0.2.zip?viasf=1",
    "https://netix.dl.sourceforge.net/project/apophysis-av/Apophysis%20AV_v_1.0.2.zip",
    "https://sourceforge.net/projects/apophysis-av/files/Apophysis%20AV_v_1.0.2.zip/download",
)
ORIGINAL_RELEASE = "https://github.com/xyrus02/apophysis-7x/releases/download/releases/7x15d/7x.15d.zip"
STARFIELD = "https://drive.usercontent.google.com/download?id=1QUgwyhkZXzOsK8NfDM26fJrHXQRS7qdC&export=download&confirm=t"
SEVEN_PREVIEW = "https://madgeniusclub.com/wp-content/uploads/2022/04/starfield-tut-1.jpg"
AV_ARTICLE = "https://samjcreations.blogspot.com/2022/08/apophysis-av.html?m=0"
AV_ARCHIVE_SHA = "03b4a7714bfc6d99a0b80b908dd69113352e35c4ad5b46568b721a70880eca12"
ORIGINAL_ARCHIVE_SHA = "737ba36c71dc0f55ca207e1ea32670cf5c437cf76d813b78d89c62e8cf0f09f5"
HASHES = {
    "Apophysis7X.temp": "4b1cd0d000daf620e168a1efa13a1c1faa6f786008659caad4c044c32460c05a",
    "ApophysisAV.temp": "93797df40ee521b9ad563a8c9f43e19ae3254e70dce86637fa060e3e1f5307b5",
    "starfield.flame": "20719152e1383423ff4362506821b70db385bad11b68ef3485569925ae5dbffb",
    "samj-demo.flame": "93ba18937c907509e65e63821bb27456460ab906dd4f490294a89defc4273bc6",
    "starfield-screen.jpg": "6cd73493e55d1044de58937ac9433e0ab477edd33248e44420cbf1851deba988",
    "samj-demo.png": "390d1b09a8059baf4783868e3e0f8fb37bb7e88397ec6bce5a6431f1a1819933",
    "fox-3d-hack.flame": "afe1241302e715b0677e5c65bf9324cb67a290a7e933b77e03ee53dc82a9846f",
}

EXPECTED_RENDERED = {
    "Apophysis7X.temp": {1, 2, 3, 4, 5},
    "ApophysisAV.temp": {1, 2, 3, 6, 10, 11},
    "starfield.flame": {1},
    "samj-demo.flame": {1},
    "fox-3d-hack.flame": {1},
}

def require(condition, message):
    if not condition:
        raise ValueError(message)

def verify_hash(data, expected, label):
    actual = hashlib.sha256(data).hexdigest()
    require(actual == expected, f"{label}: SHA-256 {actual}, expected {expected}")
    return data

def download(url):
    with urlopen(Request(url, headers={"User-Agent": "Apophysis-Reborn-Corpus/0.3"}), timeout=90) as response:
        return response.read()

class PublishedFlame(HTMLParser):
    """Extract only the named listing and the author's full-size Demo.png link."""
    def __init__(self):
        super().__init__()
        self.lines = []
        self.active = False
        self.image = None

    def handle_data(self, text):
        text = text.replace("\u00a0", " ").strip()
        if text == '<flames name="demo">':
            self.active = True
        if self.active and text:
            self.lines.append(text)
        if text == "</flames>":
            self.active = False

    def handle_starttag(self, tag, attrs):
        href = dict(attrs).get("href", "")
        if tag == "a" and href.startswith("https://blogger.googleusercontent.com/") and href.endswith("/s800/Demo.png"):
            self.image = href

def retrieve(cache):
    cache.mkdir(parents=True, exist_ok=True)
    def install(name, data):
        path = cache / name
        verify_hash(data, HASHES[name], name)
        if path.exists():
            require(path.read_bytes() == data, f"Existing file differs: {path}")
        else:
            path.write_bytes(data)

    for name, urls, bundle_hash, member in [
        ("Apophysis7X.temp", (ORIGINAL_RELEASE,), ORIGINAL_ARCHIVE_SHA, "Apophysis7X.temp"),
        ("ApophysisAV.temp", AV_RELEASES, AV_ARCHIVE_SHA, "Apophysis AV_v_1.0.2/ApophysisAV.temp"),
    ]:
        for url in urls:
            try:
                downloaded = download(url)
            except OSError as exc:
                error = exc
                continue
            archive = verify_hash(downloaded, bundle_hash, url)
            break
        else:
            raise error
        with ZipFile(io.BytesIO(archive)) as zipped:
            install(name, zipped.read(member))
    install("starfield.flame", download(STARFIELD))
    install("starfield-screen.jpg", download(SEVEN_PREVIEW))
    article = PublishedFlame()
    article.feed(download(AV_ARTICLE).decode("utf-8"))
    require(article.image and article.lines, "The AV article layout has changed")
    install("samj-demo.flame", ("\n".join(article.lines) + "\n").encode("utf-8"))
    install("samj-demo.png", download(article.image))

def command(exe, *args):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, encoding="utf-8")
    require(result.returncode == 0, f"{' '.join(map(str, args))}: {result.stderr.strip()}")
    return result.stdout

def inspect(exe, path):
    return json.loads(command(exe, "inspect", path, "--json"))["entries"]

def verify_file(exe, source, output_dir):
    data = verify_hash(source.read_bytes(), HASHES[source.name], source.name)
    model = ET.fromstring(data)
    flames = [model] if model.tag == "flame" else list(model.findall("flame"))
    entries = inspect(exe, source)
    names = [x.get("name") for x in flames]
    require(names and [e["name"] for e in entries] == names, f"{source.name}: entry names/order changed")
    require([e["version"] for e in entries] == [x.get("version") for x in flames], "Producer labels changed")
    raw_copy, edited, image = (output_dir / f for f in ("raw-copy.flame", "renamed.flame", "render.ppm"))
    command(exe, "save", source, "--output", raw_copy)
    require(raw_copy.read_bytes() == data and inspect(exe, raw_copy) == entries,
            f"{source.name}: whole-file save or reopen changed the document")
    rendered = []
    for entry in entries:
        index, name = entry["index"], entry["name"]
        new_name = f"Corpus check {index}"
        command(exe, "save", source, "--index", index, "--name", new_name, "--output", edited)
        # These particular originals have unique flame names and double-quoted name as the first attribute.
        old = rb'(<flame\s+name=")' + re.escape(name.encode("utf-8")) + rb'"'
        expected, hits = re.subn(old, lambda m: m[1] + new_name.encode() + b'"', data)
        require(hits == 1 and edited.read_bytes() == expected, f"{source.name} #{index}: unrelated bytes changed")
        reopened = inspect(exe, edited)
        expected_entries = [dict(item, name=new_name) if item["index"] == index else item for item in entries]
        # A longer name on the same line moves the diagnostic column for the selected flame.
        for rows in (reopened, expected_entries):
            selected = rows[index - 1]
            issue = selected["first_issue"]
            if issue:
                selected["first_issue"] = re.sub(r"^(XML line [0-9]+, column )[0-9]+: ", r"\1*: ", issue)
        require(reopened == expected_entries, f"{source.name} #{index}: renamed entry failed reopen")
        if entry["status"] == "renderable":
            w, h = map(int, flames[index - 1].get("size").split())
            command(exe, "--flame", source, "--index", index, "--samples", 250000,
                    "--threads", 2, "--seed", 123456, "--output", image)
            header, raster = image.read_bytes().split(b"\n255\n", 1)
            require(header.split() == [b"P6", str(w).encode(), str(h).encode()]
                    and len(raster) == w*h*3 and max(raster) > min(raster),
                    f"{source.name} #{index}: invalid, incomplete or uniform output")
            rendered.append(index)
        else:
            require(entry["status"] == "preserve-only" and entry["first_issue"],
                    f"{source.name} #{index}: missing render diagnostic")
            image.unlink(missing_ok=True)
            rejected = subprocess.run([str(exe), "--flame", str(source), "--index", str(index),
                                       "--samples", "1000", "--output", str(image)],
                                      capture_output=True)
            require(rejected.returncode != 0 and not image.exists(),
                    f"{source.name} #{index}: preserve-only entry produced an output")
    print(f"{source.name}: {len(entries)} entries; exact copy and all targeted edits passed; rendered {rendered}")
    return rendered

def verify_art(exe, cache, out):
    # Optional publisher comparison uses Pillow only to decode an actual PNG.
    from PIL import Image
    paired = cache / "samj-demo.png"
    verify_hash(paired.read_bytes(), HASHES[paired.name], paired.name)
    raster = out / "art.ppm"
    command(exe, "--flame", cache / "samj-demo.flame", "--seed", 123456, "--threads", 4,
            "--samples", 8000000, "--output", raster)
    with Image.open(paired) as published, Image.open(raster) as reborn:
        require(published.mode == reborn.mode == "RGB" and published.size == reborn.size == (800,800),
                "AV publisher image mode/dimensions mismatch")
        aa, bb = published.tobytes(), reborn.tobytes()
    mae, r = image_distance(aa, bb)
    print(f"Published AV Demo: RGB MAE={mae:.6f}/255, intensity r={r:.6f}")
    require(mae <= 3 and r >= .97, "AV published-image comparison exceeded its regression limits")

    preview = cache / "starfield-screen.jpg"
    verify_hash(preview.read_bytes(), HASHES[preview.name], preview.name)
    command(exe, "--flame", cache / "starfield.flame", "--seed", 123456, "--threads", 4,
            "--samples", 40000000, "--output", raster)
    # The screenshot has an exact gray UI boundary at all four sides of the viewport.
    with Image.open(preview) as screenshot, Image.open(raster) as reborn:
        require(screenshot.size == (954, 857) and reborn.size == (2750, 1975), "7X source dimensions changed")
        viewport = screenshot.crop((184, 267, 948, 816))
        reduced = reborn.resize(viewport.size, Image.Resampling.BOX)
        mae, r = image_distance(viewport.tobytes(), reduced.tobytes())
        print(f"Published 7X viewport: RGB MAE={mae:.6f}/255, intensity r={r:.6f}")
        # Both images are also reduced to remove high-frequency preview Monte Carlo and JPEG noise.
        low = (96, 69)
        mae_low, r_low = image_distance(viewport.resize(low, Image.Resampling.BOX).tobytes(),
                                        reduced.resize(low, Image.Resampling.BOX).tobytes())
        print(f"Published 7X viewport at 96x69: RGB MAE={mae_low:.6f}/255, intensity r={r_low:.6f}")
        require(mae <= 17.5 and r >= .60 and r_low >= .95,
                "7X published editor preview comparison exceeded its regression limits")


def image_distance(aa, bb):
    count = len(aa)//3
    absolute = sx = sy = sxx = syy = sxy = 0
    for i in range(0, len(aa), 3):
        a, b = aa[i:i+3], bb[i:i+3]
        x, y = sum(a), sum(b)
        absolute += sum(abs(m-n) for m,n in zip(a,b))
        sx += x
        sy += y
        sxx += x*x
        syy += y*y
        sxy += x*y
    mae = absolute / len(aa)
    r = (count*sxy - sx*sy) / ((count*sxx - sx*sx)*(count*syy - sy*sy))**0.5
    return mae, r

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("executable", type=Path)
    p.add_argument("--cache", type=Path, help="directory holding the publisher downloads")
    p.add_argument("--fetch", action="store_true", help="download and verify exact external fixtures")
    p.add_argument("--compare-art", action="store_true", help="also compare the AV PNG and 7X editor preview (Pillow 9.1+)")
    args = p.parse_args()
    exe = args.executable.resolve()
    if (args.fetch or args.compare_art) and args.cache is None:
        p.error("--fetch and --compare-art require --cache")
    if args.fetch:
        retrieve(args.cache)
    paths = [HERE / "fox-3d-hack.flame"]
    if args.cache:
        paths += [args.cache / key for key in ("Apophysis7X.temp", "ApophysisAV.temp", "starfield.flame", "samj-demo.flame")]
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        for path in paths:
            rendered = verify_file(exe, path, work)
            require(EXPECTED_RENDERED[path.name].issubset(rendered),
                    f"Previously renderable historical entry regressed in {path.name}")
        if args.compare_art:
            verify_art(exe, args.cache, work)

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, ImportError) as exc:
        sys.exit(str(exc))
