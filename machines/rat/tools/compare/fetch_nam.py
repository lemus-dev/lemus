#!/usr/bin/env python3
"""Download NAM captures of real RAT pedals from Tone3000 into private/reference/nam/.

Each tone page embeds its data in the Next.js (React Server Components) payload: the tone (title,
description, makes, tags, license, photos) and its model list (name, public model_url, input and
output level, training run). No login or API key is needed. Only the A2 models are listed there
(a pack's "models_count" also counts the older A1 copies of the same captures).

For every pack this writes, under private/reference/nam/<slug>/:
  - page.txt: the decoded page payload (what the page says, verbatim);
  - <model name>.nam for every RAT model (packs with other pedals are filtered by name);
  - images/: the pack's photos (they often show the knob positions).
Then private/reference/nam/index.json (all metadata: the page's, each .nam file's own metadata
block and architecture, and what PACKS below says about each pedal) and index.md (readable;
its knob columns are make_suite.py's parse of the names, so run it with the repo venv).

Saved pages are reused: --refresh fetches them again (that can add models to a pack, so the
suite changes). Files that already exist are never downloaded again.

Usage: tools/compare/fetch_nam.py [--refresh] [SLUG ...]     (repo venv, ../../.venv)
"""
import hashlib
import json
import pathlib
import re
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "private/reference/nam"

# What each pack's page says about the pedal, read by hand (the page text is in page.txt):
#   year, opamp: as stated ("unknown" if not; "by era" when only the year implies it);
#   variant: None for a stock RAT/RAT2 circuit; otherwise what differs ("turbo", "large-box",
#            "modded", "fat-rat"). compare.py leaves out every capture with a variant;
#   mods: the stated modifications;
#   knobs: how the names (or the description) give the knob positions, and the mapping used.
#          make_suite.py does the parsing;
#   models: a regex on the model name for packs that also hold other pedals or chains.
PACKS = [
    dict(slug="stomptones-rat2", url="https://www.tone3000.com/tones/pro-co-rat-2-45294",
         year="unknown", opamp="unknown", variant=None, mods="none stated",
         knobs="clock positions in the names (MIN, 0900 ... MAX); 7:00 = 0, 12:00 = 0.5, 5:00 = 1. VOLUME not stated"),
    dict(slug="romanrecords-rat-90s", url="https://www.tone3000.com/tones/proco-rat-36551",
         year="1990s", opamp="unknown", variant=None, mods="none stated",
         knobs="7 x 7 index grid (Distortion i Filter j), positions not stated; VOLUME not stated"),
    dict(slug="jakkwiat-rat2", url="https://www.tone3000.com/tones/proco-rat-2-82634",
         year="unknown", opamp="unknown", variant=None, mods="none stated",
         knobs="7 x 7 index grid (D{i}_F{j}), positions not stated; VOLUME not stated"),
    dict(slug="outmoded-rat2-11dbu",
         url="https://www.tone3000.com/tones/proco-rat2-115dbu-sendreturn-ttsv10-input-30444",
         year="unknown", opamp="unknown", variant=None, mods="none stated",
         knobs="clock positions in the names (D-12_00 F-1_30 V-3_00); 7:00 = 0, 12:00 = 0.5, 5:00 = 1"),
    dict(slug="lorenzobona-largebox-reissue", url="https://www.tone3000.com/tones/rat-large-box-reissue-2000s-46960",
         year="early 2000s (Vintage reissue)", opamp="unknown", variant="large-box", mods="none stated",
         knobs="DISTORTION in % in the names; FILTER 0, VOLUME half (description)"),
    dict(slug="returntozero-turbo-rat", url="https://www.tone3000.com/tones/turbo-rat-29177",
         year="unknown", opamp="unknown", variant="turbo", mods="none stated",
         knobs="DISTORTION max, FILTER min (description); VOLUME not stated"),
    # Added 2026-10.
    dict(slug="pong-rat2-1996-usa", url="https://www.tone3000.com/tones/proco-rat-2-1996-usa-78883",
         year="1996 (USA)", opamp="LM308", variant=None, mods="none stated",
         knobs="0-10 numbers in the description (Crunch D2.8 F4 V4, Drive D4 F3 V4, Fuzz D6 F1 V4); "
               "rotation = n/10 (0 = fully CCW, 10 = fully CW). The photo shows another setting"),
    dict(slug="rbrt-rat-1990s", url="https://www.tone3000.com/tones/proco-rat-1990s-6335",
         year="about 1994 (\"I guess\")", opamp="unknown (LM308 by era)", variant=None, mods="none stated",
         knobs="'RAT D F V' in % of rotation (RAT 15 50 70, RAT 40 50 70); the description confirms the first "
               "number is the drive"),
    dict(slug="ideofunk-vintage-reissue", url="https://www.tone3000.com/tones/proco-rat-vintage-reissue-2298",
         year="1990s (Vintage reissue)", opamp="LM308 (fitted by the owner in place of the stock TL072)",
         variant="modded",
         mods="large-box Vintage reissue; some parts replaced with the same values or NOS; TL072 replaced by an "
              "LM308; switch to swap the diodes for LEDs (LED captures say LED)",
         knobs="D/F/V numbers in the names, read as the 7 printed ticks 0-6 (no photo): in 16 settings tagged "
               "high-gain/metal no number exceeds 6 (max D5, F5.3, V6), and the large-box panel has 7 ticks, which "
               "imposteringtheband counts as 'D 4/6'. Rotation = 0.05 + 0.15 n (tick n at -135 + 45 n degrees of "
               "the 300-degree travel); on a 0-10 reading every value would be lower. 'Nuno Beato' (Nuno "
               "Bettencourt's 'cross' setting) is not given"),
    dict(slug="imposteringtheband-1984-whiteface", url="https://www.tone3000.com/tones/1984-whiteface-rat-72402",
         year="1984 (whiteface)", opamp="unknown (LM308 by era)", variant="large-box", mods="none stated",
         knobs="'D 4/6, F 2/6, V 4/6': tick n of the 7 scale ticks (0-6); rotation = 0.05 + 0.15 n. The photo "
               "agrees (D and V about 1:30-2:00, F about 10:30)"),
    dict(slug="pablobarrasaa-bigbox-1995", url="https://www.tone3000.com/tones/proco-the-rat-big-box-85590",
         year="1995 (big-box reissue)", opamp="unknown", variant="large-box", mods="none stated",
         knobs="clock positions in the description (Gain 11:00, Filter 2:00, Volume 3:00); 7:00 = 0 ... 5:00 = 1"),
    dict(slug="mmasonmusic-rat2-lm308", url="https://www.tone3000.com/tones/pedals-from-my-pedal-board-51809",
         year="unknown", opamp="LM308 (fitted by the owner)", variant="modded",
         mods="LM308 added; 1k linear pot in place of the 47 ohm feedback resistor (Ruetz mod), set to about "
              "half (about 500 ohm) in every capture",
         knobs="0-10 numbers in the names (D5, F3, V8); rotation = n/10",
         models=r"^Pro Co Rat"),
    dict(slug="autoguitarrig-rat2", url="https://www.tone3000.com/tones/proco-rat-2-89711",
         year="unknown", opamp="unknown", variant=None, mods="none stated",
         knobs="0-10 numbers in the names (D3/5/8/10, F5, L6 = VOLUME; D10 is called MAX); rotation = n/10"),
    dict(slug="cripplingguitaraddiction-rat", url="https://www.tone3000.com/tones/proco-rat-89948",
         year="unknown", opamp="unknown", variant=None, mods="none (\"completely stock\")",
         knobs="description: distortion 'almost 0', filter noon, volume max. The photo shows FILTER at noon, VOLUME "
               "near max and DISTORTION near the 9:00 tick (0.2); taken as 0.15 (approximate)"),
    dict(slug="octaviofotografia-rat", url="https://www.tone3000.com/tones/direct-drive-prco-rat-33919",
         year="unknown", opamp="unknown", variant=None, mods="none stated",
         knobs="0-10 numbers in the name (D4 F3 V6); rotation = n/10, which the photo bears out (D about 11:30, F "
               "10:00, V 1:00; 0-6 ticks would not fit). Only the RAT on its own is kept (the pack also has a Barber "
               "Direct Drive and the chain)",
         models=r"^ProCoRAT"),
    dict(slug="hk-rat", url="https://www.tone3000.com/tones/proco-rat-26704",
         year="unknown", opamp="unknown", variant=None, mods="none stated",
         knobs="not stated ('crunch/mid to high overdrive'); the photo is a product shot with every knob at noon"),
    dict(slug="colossaldave-modded-rat", url="https://www.tone3000.com/tones/modded-rat-89012",
         year="unknown", opamp="LM308N (vintage 1985 Motorola, fitted by the owner)", variant="modded",
         mods="LM308N fitted, C2 removed, cosmetic mods",
         knobs="not stated (Modded_Rat_1 ... 6)"),
    dict(slug="jtn191-80s-rat", url="https://www.tone3000.com/tones/procorat-3-way-clipping-ruetz-knob-5257",
         year="1980s (tag)", opamp="LM308", variant="modded",
         mods="3-way clipping switch (LED / standard 1N914), extra Ruetz knob, tantalum caps",
         knobs="not stated"),
    dict(slug="omarplastiko78-fat-rat", url="https://www.tone3000.com/tones/fat-rat-chip-lm308-motorola-73662",
         year="unknown", opamp="LM308 (Motorola)", variant="fat-rat",
         mods="a FAT RAT: RAT-style clone (not ProCo's panel) with STOCK/FAT and MOSFET/STOCK toggles, positions "
              "not stated",
         knobs="0-10 numbers in the names (V8 F6 D6, V9 F7 D5); rotation = n/10. The photo (graduated scales) "
               "agrees with V8 F6 D6"),
    dict(slug="tone3000slm-turbo-rat", url="https://www.tone3000.com/tones/proco-turbo-rat-74392",
         year="unknown", opamp="unknown", variant="turbo", mods="none stated",
         knobs="'RAT-D{d}-F{f}-V{v}' in % of rotation (the legend: Distortion, Frequency, Volume)"),
]

UA = {"User-Agent": "Mozilla/5.0 (Macintosh) rat-emulation-research"}


def get(url):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def payload_text(html):
    """Concatenate the self.__next_f.push string chunks and unescape them."""
    chunks = re.findall(rb'self\.__next_f\.push\(\[1,"(.*?)"\]\)', html, re.S)
    out = []
    for c in chunks:
        try:
            out.append(json.loads(b'"' + c + b'"'))
        except json.JSONDecodeError:
            pass
    return "".join(out)


def rsc_rows(text):
    """Split an RSC payload into its rows: {id: value}. Rows are "id:JSON\\n" or "id:T<hex len>,<text>"
    (a text row, its length in UTF-8 bytes); other rows are kept as strings."""
    b = text.encode()
    rows, pos, n = {}, 0, len(b)
    head = re.compile(rb"([0-9a-f]+):")
    while pos < n:
        m = head.match(b, pos)
        if not m:
            nl = b.find(b"\n", pos)
            pos = n if nl < 0 else nl + 1
            continue
        key, pos = m.group(1).decode(), m.end()
        if b[pos:pos + 1] == b"T":
            comma = b.index(b",", pos)
            size = int(b[pos + 1:comma], 16)
            rows[key] = b[comma + 1:comma + 1 + size].decode("utf-8", "replace")
            pos = comma + 1 + size
            continue
        nl = b.find(b"\n", pos)
        end = n if nl < 0 else nl
        raw = b[pos:end]
        try:
            rows[key] = json.loads(raw)
        except json.JSONDecodeError:
            rows[key] = raw.decode("utf-8", "replace")
        pos = end + 1
    return rows


def resolve(v, rows, depth=0):
    """Replace "$<id>" references with the rows they point to."""
    if depth > 40:
        return v
    if isinstance(v, str) and re.fullmatch(r"\$[0-9a-f]+", v) and v[1:] in rows:
        return resolve(rows[v[1:]], rows, depth + 1)
    if isinstance(v, list):
        return [resolve(x, rows, depth + 1) for x in v]
    if isinstance(v, dict):
        return {k: resolve(x, rows, depth + 1) for k, x in v.items()}
    return v


def tone_from(text, url):
    """The tone on this page (the page also lists the author's other tones)."""
    tone_id = int(re.search(r"-(\d+)/?$", url).group(1))
    rows = rsc_rows(text)
    for v in rows.values():
        if isinstance(v, dict) and v.get("id") == tone_id and "models_count" in v:
            return resolve(v, rows)
    raise RuntimeError(f"tone {tone_id} not found in the page")


def arch_summary(d):
    """One line for a .nam model's architecture: WaveNet layer channels, LSTM size, slimmable sizes."""
    a, c = d.get("architecture"), d.get("config") or {}
    if a == "SlimmableContainer":
        return "Slimmable[" + ", ".join(f"{s.get('max_value')}: {arch_summary(s['model'])}"
                                        for s in c.get("submodels", [])) + "]"
    if a == "WaveNet":
        layers = c.get("layers", [])
        return "WaveNet " + "+".join(f"{l.get('channels')}ch x{len(l.get('dilations', []))}" for l in layers)
    if a == "LSTM":
        return f"LSTM {c.get('num_layers')}x{c.get('hidden_size')}"
    return str(a)


def nam_info(path):
    d = json.loads(path.read_text())
    md = d.get("metadata") or {}
    return {
        "version": d.get("version"),
        "architecture": d.get("architecture"),
        "architecture_summary": arch_summary(d),
        "sample_rate": d.get("sample_rate"),
        "metadata": md,
    }


def fetch_pack(p, refresh):
    slug, url = p["slug"], p["url"]
    d = OUT / slug
    d.mkdir(parents=True, exist_ok=True)
    page = d / "page.txt"
    if refresh or not page.exists():
        print(f"== {slug}: fetching {url}")
        page.write_text(payload_text(get(url)))
    else:
        print(f"== {slug}: saved page")
    text = page.read_text()
    tone = tone_from(text, url)
    keep = re.compile(p["models"]) if p.get("models") else None
    models, seen = [], set()
    for obj in tone.get("models") or []:
        murl = obj.get("model_url") or ""
        if not murl.endswith(".nam") or murl in seen:
            continue
        if keep and not keep.search(obj.get("name") or ""):
            continue
        seen.add(murl)
        models.append(obj)
    images = []
    for iurl in tone.get("images") or []:
        dest = d / "images" / pathlib.Path(iurl).name
        if not dest.exists():
            dest.parent.mkdir(exist_ok=True)
            dest.write_bytes(get(iurl))
        images.append(str(dest.relative_to(ROOT)))
    user = tone.get("user") if isinstance(tone.get("user"), dict) else {}
    entry = {
        "slug": slug,
        "page": url,
        "tone_id": tone.get("id"),
        "title": tone.get("title"),
        "user": user.get("username"),
        "license": tone.get("license"),
        "published": (tone.get("published_at") or "")[:10],
        "description": tone.get("description"),
        "gear": tone.get("gear"),
        "makes": [m.get("name") for m in tone.get("makes") or [] if isinstance(m, dict)],
        "tags": [m.get("name") for m in tone.get("tags") or [] if isinstance(m, dict)],
        "links": tone.get("links") or [],
        "images": images,
        "sizes": tone.get("sizes"),
        "has_model_with_calibration": tone.get("has_model_with_calibration"),
        "models_count": tone.get("models_count"),
        "a2_models_count": tone.get("a2_models_count"),
        "pack": {k: p.get(k) for k in ("year", "opamp", "variant", "mods", "knobs")},
        "models": [],
    }
    for obj in models:
        name = re.sub(r"[^A-Za-z0-9._ -]+", "_", obj.get("name") or str(obj["id"])).strip()
        dest = d / f"{name}.nam"
        if not dest.exists():
            dest.write_bytes(get(obj["model_url"]))
        sha1 = hashlib.sha1(dest.read_bytes()).hexdigest()
        info = nam_info(dest)
        md = info["metadata"]
        # The page's level and the file's own metadata should agree; prefer the page's.
        lin, lout = obj.get("input_level"), obj.get("output_level")
        src = "page" if lin is not None else None
        if lin is None and md.get("input_level_dbu") is not None:
            lin, lout, src = md.get("input_level_dbu"), md.get("output_level_dbu"), ".nam metadata"
        elif lin is not None and md.get("input_level_dbu") not in (None, lin):
            print(f"  WARNING {name}: page input {lin} dBu, .nam {md.get('input_level_dbu')} dBu")
        tr = obj.get("training") if isinstance(obj.get("training"), dict) else {}
        entry["models"].append({
            "name": obj.get("name"),
            "file": str(dest.relative_to(ROOT)),
            "url": obj["model_url"],
            "sha1": sha1,
            "input_level_dbu": lin,
            "output_level_dbu": lout,
            "level_source": src,
            "architecture_version": obj.get("architecture_version"),
            "size": obj.get("size"),
            "training": {
                "type": tr.get("type"),
                "epochs": tr.get("epochs"),
                "esr_by_size": {str(e.get("max_value")): e.get("esr") for e in tr.get("esrs") or []
                                if isinstance(e, dict)},
            },
            "nam": info,
        })
        print(f"  {name}  {sha1[:10]}  {info['architecture_summary'][:60]}")
    if tone.get("a2_models_count") and not keep and len(models) < tone["a2_models_count"]:
        print(f"  note: page lists {len(models)} of {tone['a2_models_count']} A2 models")
    return entry


def fmt_knob(v):
    return "" if v is None else f"{v:.2f}"


def write_index_md(index):
    try:
        sys.path.insert(0, str(pathlib.Path(__file__).parent))
        import make_suite
    except ImportError as e:  # numpy/soundfile missing: no knob columns
        print("no knob columns in index.md:", e)
        make_suite = None
    lines = ["# NAM captures of real RAT pedals (Tone3000)", "",
             "Downloaded by `tools/compare/fetch_nam.py`. Personal research use only; the captures",
             "belong to their authors (see each pack's license).", "",
             "Knob positions are rotations, 0 = fully counter-clockwise, 1 = fully clockwise (300 degrees),",
             "as `make_suite.py` reads them from the names or the description. Clock positions: 7:00 = 0,",
             "12:00 = 0.5, 5:00 = 1 (0.1 per hour). A 0-10 number n: n/10 (the RAT2 panel has no numbers;",
             "0 and 10 are taken as the ends of the travel). A tick index n of the 7 printed ticks (0-6):",
             "0.05 + 0.15 n. \"idx\" is a grid index with no stated position. \"Calibrated\" means the",
             "capture's input level in dBu at 0 dBFS is known (from the page or the .nam metadata).", "",
             "| Pack | Models | Year | Op amp | Variant | Calibrated | Knobs | Fits |",
             "|---|---|---|---|---|---|---|---|"]
    specs = {}
    for e in index:
        for m in e["models"]:
            specs[m["file"]] = make_suite.spec_for(e, m) if make_suite else {}
    for e in index:
        p = e["pack"]
        cal = sorted({m["input_level_dbu"] for m in e["models"] if m["input_level_dbu"] is not None})
        sp = [specs[m["file"]] for m in e["models"]]
        excl = sorted({x for s in sp for x in s.get("exclude", [])})
        kn = sorted({s.get("knobs", "?") for s in sp})
        lines.append(f"| `{e['slug']}` | {len(e['models'])} | {p['year']} | {p['opamp']} | {p['variant'] or 'stock'} | "
                     f"{', '.join(f'{c:+g} dBu' for c in cal) if cal else 'no'} | {', '.join(kn)} | "
                     f"{'excluded: ' + ', '.join(excl) if excl else 'usable'} |")
    lines.append("")
    for e in index:
        p = e["pack"]
        cal = sorted({(m["input_level_dbu"], m["output_level_dbu"], m["level_source"]) for m in e["models"]
                      if m["input_level_dbu"] is not None})
        lines += [f"## {e['title']} (`{e['slug']}`)", "",
                  f"<{e['page']}> by @{e['user']}, published {e['published']}, license `{e['license']}`.", "",
                  f"- **Year:** {p['year']}",
                  f"- **Op amp:** {p['opamp']}",
                  f"- **Variant:** {p['variant'] or 'stock RAT/RAT2 circuit'}; mods: {p['mods']}",
                  "- **Calibrated:** " + ("; ".join(f"input {a:+g} dBu, output {b:+g} dBu (from the {s})"
                                                   for a, b, s in cal) if cal else "no (no level on the page or in "
                                                                                   "the .nam metadata)"),
                  f"- **Knobs:** {p['knobs']}",
                  f"- **Makes / tags:** {', '.join(e['makes']) or '-'} / {', '.join(e['tags']) or '-'}",
                  f"- **Models:** {len(e['models'])} kept (page: {e['models_count']} incl. A1 copies, "
                  f"{e['a2_models_count']} A2); photos: {len(e['images'])}", "",
                  "> " + (e["description"] or "").strip().replace("\n", "\n> "), "",
                  "| Model | D | F | V | Use | In / out dBu | .nam gear (make / model / type / tone) | Architecture | Rate | Training | ESR |",
                  "|---|---|---|---|---|---|---|---|---|---|---|"]
        for m in e["models"]:
            s = specs[m["file"]]
            md = m["nam"]["metadata"]
            if s.get("knobs") == "unknown":  # make_suite's noon placeholder
                d = f = "?"
            else:
                d = fmt_knob(s.get("dist")) or (f"idx {s['dist_index']}" if "dist_index" in s else "?")
                f = fmt_knob(s.get("filter")) or (f"idx {s['filter_index']}" if "filter_index" in s else "?")
            v = fmt_knob(s.get("volume")) or "-"
            use = "fit" if not s.get("exclude") else ", ".join(s["exclude"])
            lv = "-" if m["input_level_dbu"] is None else f"{m['input_level_dbu']:+g} / {m['output_level_dbu']:+g}"
            gear = " / ".join(str(md.get(k) or "-") for k in ("gear_make", "gear_model", "gear_type", "tone_type"))
            esr = md.get("training", {}).get("validation_esr") if isinstance(md.get("training"), dict) else None
            if esr is None:
                esr = m["training"]["esr_by_size"].get("1")
            tr = f"{m['training']['type'] or '-'} {m['training']['epochs'] or ''}".strip()
            lines.append(f"| `{m['name']}` | {d} | {f} | {v} | {use} | {lv} | {gear} | "
                         f"{m['nam']['architecture_summary']} | {m['nam']['sample_rate']:g} | {tr} | "
                         f"{'-' if esr is None else f'{esr:.4f}'} |")
        lines.append("")
    (OUT / "index.md").write_text("\n".join(lines))


def main():
    refresh = "--refresh" in sys.argv
    want = [a for a in sys.argv[1:] if not a.startswith("--")]
    OUT.mkdir(parents=True, exist_ok=True)
    old = {}
    if (OUT / "index.json").exists():
        old = {e["slug"]: e for e in json.loads((OUT / "index.json").read_text())}
    index = []
    for p in PACKS:
        if want and p["slug"] not in want and p["slug"] in old:
            index.append(old[p["slug"]])
            continue
        index.append(fetch_pack(p, refresh))
    (OUT / "index.json").write_text(json.dumps(index, indent=1))
    write_index_md(index)
    print("total models:", sum(len(e["models"]) for e in index), "in", len(index), "packs")


if __name__ == "__main__":
    sys.exit(main())
