#!/usr/bin/env python3
"""Download the ProCo RAT recordings from the Aalto dataset (Zenodo 10.5281/zenodo.20406285,
CC-BY-NC-4.0; paper arXiv:2509.15622) into private/reference/aalto/.

These are real-pedal recordings, not neural captures: each WAV holds the dry guitar in channel
1 and the RAT's output in channel 2, with the three knob positions (0-100) in the file name.
Only the RAT's evaluation clips are fetched by default (HTTP range requests into the 1.3 GB
zip, so nothing else is downloaded); --train adds the training clips.

Usage: fetch_aalto.py [--train] [--limit N]     (stdlib only)
"""
import io
import json
import pathlib
import sys
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "private/reference/aalto"
URL = "https://zenodo.org/api/records/20406285/files/datasets.zip/content"


class RangeFile(io.RawIOBase):
    """A read-only file over HTTP range requests (enough for zipfile)."""

    def __init__(self, url):
        r = urllib.request.urlopen(urllib.request.Request(url, method="HEAD"))
        self.url, self.size, self.pos = r.geturl(), int(r.headers["Content-Length"]), 0

    def seekable(self):
        return True

    def readable(self):
        return True

    def tell(self):
        return self.pos

    def seek(self, off, whence=0):
        self.pos = off if whence == 0 else self.pos + off if whence == 1 else self.size + off
        return self.pos

    def readinto(self, b):
        n = min(len(b), self.size - self.pos)
        if n <= 0:
            return 0
        req = urllib.request.Request(self.url, headers={"Range": f"bytes={self.pos}-{self.pos + n - 1}"})
        d = urllib.request.urlopen(req, timeout=120).read()
        b[: len(d)] = d
        self.pos += len(d)
        return len(d)


def main():
    train = "--train" in sys.argv
    limit = int(sys.argv[sys.argv.index("--limit") + 1]) if "--limit" in sys.argv else None
    z = zipfile.ZipFile(io.BufferedReader(RangeFile(URL), buffer_size=1 << 20))
    want = [i for i in z.infolist() if i.filename.startswith("datasets/rat/") and i.filename.endswith(".wav")
            and ("/eval/" in i.filename or train)]
    if limit:
        want = want[:limit]
    index = []
    for k, info in enumerate(want):
        split = info.filename.split("/")[2]
        dest = OUT / split / pathlib.Path(info.filename).name
        dest.parent.mkdir(parents=True, exist_ok=True)
        if not dest.exists():
            dest.write_bytes(z.read(info))
        knobs = [int(v) for v in dest.stem.split(",")]
        index.append({"file": str(dest.relative_to(ROOT)), "split": split, "knobs": knobs})
        if k % 25 == 0:
            print(f"{k + 1}/{len(want)} {dest.name}", flush=True)
    (OUT / "index.json").write_text(json.dumps(index, indent=1))
    (OUT / "README.md").write_text(
        "# Aalto RAT recordings\n\nFrom Zenodo record 10.5281/zenodo.20406285 (\"Datasets for Asymptotically "
        "Stable Recurrent Neural Networks\", 2026, CC-BY-NC-4.0; arXiv:2509.15622), fetched by "
        "`tools/compare/fetch_aalto.py`. Channel 1 is the dry guitar, channel 2 the pedal's output; the file name "
        "is the three knob positions, 0-100. Personal research use only.\n")
    print(len(index), "files in", OUT)


if __name__ == "__main__":
    main()
