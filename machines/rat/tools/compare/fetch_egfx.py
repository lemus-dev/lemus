#!/usr/bin/env python3
"""Download EGFxSet's ProCo RAT2 recordings and their clean (dry) sources into private/reference/egfx/.

EGFxSet (Pedroza, Meza, Roman, ISMIR 2022 LBD; Zenodo 10.5281/zenodo.7044411, CC BY 4.0;
https://egfxset.github.io): every note of a Stratocaster (6 strings x frets 0-22 = 138 notes, 5 s
each) on each of its 5 pickup settings, recorded clean through an Audient iD14's instrument input,
then re-amped from the iD14's output through each effect into its line input. The RAT is "a later
2010 model made in China" (the project page; an OP07-era RAT2), set to DISTORTION 1.0, FILTER 0.5,
VOLUME 1.0 for every file (the record's effect table and egfxset_metadata.csv). Both the clean and
the processed recordings were normalized, so the levels are lost: the input level is one unknown
for the whole set (compare_egfx.py --scan finds it) and the output level is matched per file.

Downloads RAT.zip (487 MB) and Clean.zip (431 MB) whole, checks size and MD5 against the Zenodo
API, unzips into private/reference/egfx/{RAT,Clean}/<pickup>/<string>-<fret>.wav and deletes the
zip (--keep-zips keeps it). Writes private/reference/egfx/index.json (the dry/wet pairs and the
knob settings), README.md and the record's metadata (record.json, egfxset_metadata.csv).
Re-running skips a zip whose files are already unpacked and verified.

Usage: tools/compare/fetch_egfx.py [--keep-zips]     (stdlib only)
"""
import ast
import csv
import hashlib
import io
import json
import pathlib
import sys
import time
import urllib.error
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "private/reference/egfx"
RECORD = "https://zenodo.org/api/records/7044411"
ZIPS = ["RAT.zip", "Clean.zip"]
UA = {"User-Agent": "rat-emulation-research"}


def retry(fn, tries=6):
    """Zenodo answers 429/5xx now and then: try again, waiting longer each time."""
    for k in range(tries):
        try:
            return fn()
        except (urllib.error.URLError, TimeoutError, ConnectionError) as e:
            if k == tries - 1:
                raise
            print(f"  retrying after {e}", flush=True)
            time.sleep(5 * 2 ** k)


def get(url):
    def once():
        with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as r:
            return r.read()
    return retry(once)


def download(url, dest, size, md5):
    """Stream url to dest (resuming a partial download), checking its size and MD5."""
    part = dest.with_suffix(dest.suffix + ".part")

    def once():
        have = part.stat().st_size if part.exists() else 0
        if have >= size:
            return
        hdr = dict(UA, **({"Range": f"bytes={have}-"} if have else {}))
        with urllib.request.urlopen(urllib.request.Request(url, headers=hdr), timeout=120) as r:
            mode = "ab" if have and r.status == 206 else "wb"
            n = have if mode == "ab" else 0
            with open(part, mode) as f:
                while True:
                    b = r.read(1 << 20)
                    if not b:
                        break
                    f.write(b)
                    n += len(b)
                    if n // (50 << 20) != (n - len(b)) // (50 << 20):
                        print(f"  {dest.name}: {n >> 20} / {size >> 20} MB", flush=True)
        if n < size:  # the server closed early: resume
            raise ConnectionError(f"{dest.name}: stream ended at {n} of {size} bytes")

    retry(once, tries=12)
    h = hashlib.md5()
    with open(part, "rb") as fh:
        for b in iter(lambda: fh.read(1 << 20), b""):
            h.update(b)
    n = part.stat().st_size
    if n != size or h.hexdigest() != md5:
        part.unlink()
        raise RuntimeError(f"{dest.name}: got {n} bytes, MD5 {h.hexdigest()}; expected {size}, {md5}")
    part.rename(dest)


def fetch_zip(f, keep):
    key, size, md5 = f["key"], f["size"], f["checksum"].split(":", 1)[1]
    name = key[:-4]
    marker = OUT / name / ".verified"
    if marker.exists() and marker.read_text().strip() == md5:
        print(f"{key}: unpacked and verified before (MD5 {md5})")
        return
    dest = OUT / key
    if dest.exists() and dest.stat().st_size == size:
        h = hashlib.md5()
        with open(dest, "rb") as fh:
            for b in iter(lambda: fh.read(1 << 20), b""):
                h.update(b)
        if h.hexdigest() != md5:
            dest.unlink()
    if not dest.exists():
        print(f"{key}: downloading {size >> 20} MB")
        download(f["links"]["self"], dest, size, md5)
    with zipfile.ZipFile(dest) as z:
        wavs = [i for i in z.infolist() if i.filename.lower().endswith(".wav")]
        for i in wavs:
            target = (OUT / i.filename).resolve()
            if not str(target).startswith(str(OUT.resolve()) + "/"):
                raise RuntimeError(f"unsafe path in zip: {i.filename}")
            target.parent.mkdir(parents=True, exist_ok=True)
            with z.open(i) as src, open(target, "wb") as dst:
                dst.write(src.read())
    print(f"{key}: MD5 {md5} ok, {len(wavs)} WAVs unpacked")
    marker.write_text(md5 + "\n")
    if not keep:
        dest.unlink()


def wav_info(path):
    """Rate, channels, bits and length of a PCM/float WAV, from its header (stdlib only)."""
    b = path.read_bytes()
    pos, fmt, frames = 12, None, None
    while pos + 8 <= len(b):
        cid, size = b[pos:pos + 4], int.from_bytes(b[pos + 4:pos + 8], "little")
        if cid == b"fmt ":
            tag, ch, rate = (int.from_bytes(b[pos + 8:pos + 10], "little"), int.from_bytes(b[pos + 10:pos + 12], "little"),
                             int.from_bytes(b[pos + 12:pos + 16], "little"))
            bits = int.from_bytes(b[pos + 22:pos + 24], "little")
            fmt = (tag, ch, rate, bits)
        elif cid == b"data" and fmt:
            frames = size // (fmt[1] * fmt[3] // 8)
        pos += 8 + size + (size & 1)
    return {"format_tag": fmt[0], "channels": fmt[1], "rate": fmt[2], "bits": fmt[3], "frames": frames}


def main():
    keep = "--keep-zips" in sys.argv
    OUT.mkdir(parents=True, exist_ok=True)
    rec = json.loads(get(RECORD))
    (OUT / "record.json").write_text(json.dumps(rec, indent=1))
    files = {f["key"]: f for f in rec["files"]}
    meta = files["egfxset_metadata.csv"]
    (OUT / "egfxset_metadata.csv").write_bytes(get(meta["links"]["self"]))
    if hashlib.md5((OUT / "egfxset_metadata.csv").read_bytes()).hexdigest() != meta["checksum"].split(":", 1)[1]:
        raise RuntimeError("egfxset_metadata.csv: MD5 mismatch")
    for key in ZIPS:
        fetch_zip(files[key], keep)
    # The RAT's knob settings, from the dataset's own metadata.
    rows = list(csv.reader(io.StringIO((OUT / "egfxset_metadata.csv").read_text())))
    head = [h.strip() for h in rows[0]]
    rat = next(dict(zip(head, (c.strip() for c in r))) for r in rows[1:] if "RAT" in r[1])
    names = ast.literal_eval(rat["Knob Names"])
    setting = ast.literal_eval(rat["Setting"])
    knobs = dict(zip(names, setting))
    pairs = []
    for wet in sorted((OUT / "RAT").glob("*/*.wav")):
        dry = OUT / "Clean" / wet.parent.name / wet.name
        if not dry.exists():
            print("no clean file for", wet.relative_to(OUT))
            continue
        string, fret = (int(v) for v in wet.stem.split("-"))
        pairs.append({"dry": str(dry.relative_to(ROOT)), "wet": str(wet.relative_to(ROOT)),
                      "pickup": wet.parent.name, "string": string, "fret": fret})
    info = {"dry": wav_info(ROOT / pairs[0]["dry"]), "wet": wav_info(ROOT / pairs[0]["wet"])} if pairs else {}
    index = {
        "source": "EGFxSet, Zenodo 10.5281/zenodo.7044411 (CC BY 4.0), https://egfxset.github.io",
        "pedal": rat["Model"] + ": a 2010 RAT2 made in China (project page), OP07 era",
        "knobs": {"dist": knobs["distortion"], "filter": knobs["filter"], "volume": knobs["volume"]},
        "knob_note": "from egfxset_metadata.csv (" + rat["Knob Names"] + " = " + rat["Setting"] + "), 0-1 rotation",
        "levels": "both the clean and the processed files are normalized: input level unknown (scan it), "
                  "output level unknown (gain-match)",
        "chain": "Strat -> Audient iD14 instrument input (clean); iD14 output -> RAT2 -> iD14 line input (wet)",
        "format": info,
        "pairs": pairs,
    }
    (OUT / "index.json").write_text(json.dumps(index, indent=1))
    (OUT / "README.md").write_text(
        "# EGFxSet: ProCo RAT2 and clean recordings\n\n"
        "From EGFxSet (H. Pedroza, G. Meza, I. R. Roman, \"EGFxSet: Electric guitar tones processed through real "
        "effects of distortion, modulation, delay and reverb\", ISMIR 2022 Late Breaking Demo; Zenodo "
        "10.5281/zenodo.7044411, CC BY 4.0; https://egfxset.github.io), fetched by `tools/compare/fetch_egfx.py`.\n\n"
        "- `Clean/<pickup>/<string>-<fret>.wav`: the dry Stratocaster notes (138 notes x 5 pickup settings, 5 s).\n"
        "- `RAT/<pickup>/<string>-<fret>.wav`: the same notes re-amped through the dataset's RAT2, \"a later 2010 "
        "model made in China\", at DISTORTION 1.0, FILTER 0.5, VOLUME 1.0.\n"
        "- Both sets were normalized, so neither level is known; the files are not sample-aligned.\n"
        f"- {len(pairs)} dry/wet pairs in `index.json`; record metadata in `record.json`, `egfxset_metadata.csv`.\n")
    print(f"{len(pairs)} pairs in {OUT.relative_to(ROOT)}/index.json; knobs {index['knobs']}; format {info}")


if __name__ == "__main__":
    sys.exit(main())
