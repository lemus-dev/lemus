"""Write private/sh101/sources-sha1.tsv: one line per file under private/sh101/docs and
private/sh101/reference, with its SHA1, size and source id.

    .venv/bin/python -I machines/sh101/tools/sources_manifest.py

Columns: path (relative to private/sh101), SHA1, size in bytes, source. The source is an id from
docs/research.md §0 (S1..S38), a recording id (yt:<video id>, soundcloud:, freesound:, rec:<slug>),
or one of:
- inventory: listed with its URL and SHA1 in docs/sources.md;
- photos: listed with its URL and licence in docs/photos/sources.md;
- roland-plugin: Roland's own SH-101 plug-in (its manuals, our notes and renders of it);
- mame: MAME's MCS-48 core (reference/mame/README.md);
- derived: made here (indexes, scenes, measurements, scripts).
Empty when the path doesn't say.

Stdlib only. It prints nothing and writes only the tsv.
"""

import hashlib
import os
import re
import sys
from pathlib import Path

PRIVATE = Path(__file__).resolve().parents[1] / "private"
ROOTS = ("docs", "reference")
OUT = "sources-sha1.tsv"

# (regex on the path relative to private/sh101, source id); the first match wins.
RULES = [
    # docs/ outside papers
    (r"docs/sh101-service-notes", "S1"),
    (r"docs/schematic/bulletins/", "inventory"),
    (r"docs/schematic/", "S1"),
    (r"docs/sh101-owners-manual", "S2"),
    (r"docs/sh101-mod-tubbutec-", "S27"),
    (r"docs/sh101-mod-kenton-", "S32"),
    (r"docs/sh101-mod-novamod-", "S35"),
    (r"docs/datasheets/cem3340-curtis", "S3"),
    (r"docs/datasheets/as3340-alfa", "S4"),
    (r"docs/datasheets/as3109-alfa", "S8"),
    (r"docs/datasheets/ir3109-roland-datasheet", "S8"),
    (r"docs/datasheets/(ir3109|ba662)-.*openmusiclabs", "S9"),
    (r"docs/datasheets/(as662d-alfa|ba6110fs-rohm)", "S15"),
    (r"docs/(datasheets|mc202)/", "inventory"),
    (r"docs/sh101-catalogue-", "inventory"),
    (r"docs/roland-plugin/EXM004_", "S36"),
    (r"docs/roland-plugin/SH-101_", "S30"),
    (r"docs/roland-plugin/", "roland-plugin"),
    (r"docs/photos/tools/", "derived"),
    (r"docs/photos/", "photos"),
    (r"docs/sources\.md$", "derived"),
    # docs/papers (research.md §0)
    (r"docs/papers/sh101/sh101_service_notes_", "S1"),
    (r"docs/papers/ota_ir3109/img/sh101_service_notes_", "S1"),
    (r"docs/papers/vca_ba662/sh101_service_notes_", "S1"),
    (r"docs/papers/sh101/roland_sh101_owners_manual_1982", "S2"),
    (r"docs/papers/vco_cem3340/curtis_", "S3"),
    (r"docs/papers/vco_cem3340/(alfa_|coolaudio_|hordijk_)", "S4"),
    (r"docs/papers/vco_cem3340/wiltshire_", "S5"),
    (r"docs/papers/sh101/electricdruid_(study_of_sub|sh101_suboscillator)", "S5"),
    (r"docs/papers/vco_cem3340/(sdiy_|holmes_|kassutronics_|henry_)", "S6"),
    (r"docs/papers/forums/cem3340_", "S7"),
    (r"docs/papers/ota_ir3109/img/openmusiclabs_ir3109_datasheet", "S8"),
    (r"docs/papers/ota_ir3109/(img/)?alfa_", "S8"),
    (r"docs/papers/forums/ir3109_modwiggler_reverse_engineering", "S9"),
    (r"docs/papers/ota_ir3109/(img/)?openmusiclabs_", "S9"),
    (r"docs/papers/vca_ba662/openmusiclabs_", "S9"),
    (r"docs/papers/opensource/diy_openmusiclabs_sb01/", "S9"),
    (r"docs/papers/ota_ir3109/(img/)?electricdruid_", "S10"),
    (r"docs/papers/opensource/diy_electricdruid/", "S10"),
    (r"docs/papers/(ota_ir3109/(img/)?|sh101/|vca_ba662/)amsynths_", "S11"),
    (r"docs/papers/opensource/diy_amsynths_am8101/", "S11"),
    (r"docs/papers/ota_ir3109/(img/)?(atosynth_|minisystem_|syntaur_|obsoletetechnology_|"
     r"analoguerenaissance_)", "S12"),
    (r"docs/papers/forums/ir3109_(modwiggler_other|kvr_)", "S12"),
    (r"docs/papers/ota_ir3109/(bogason_|koeper_|huovilainen_)", "S13"),
    (r"docs/papers/(ota_ir3109|vca_ba662)/gratz_", "S14"),
    (r"docs/papers/vca_ba662/", "S15"),
    (r"docs/papers/forums/ba662_", "S16"),
    (r"docs/papers/zavalishin/", "S17"),
    (r"docs/papers/pirkle/", "S18"),
    (r"docs/papers/ladder_models/(Moog_ladder_tf|stinchcombe_)", "S19"),
    (r"docs/papers/ladder_models/(huovilainen_|lazzarini_)", "S20"),
    (r"docs/papers/ladder_models/", "S21"),
    (r"docs/papers/forums/ota_kvr_mystran", "S22"),
    (r"docs/papers/antialiasing/(parker_zavalishin|valimaki_pekonen_nam|esqueda_valimaki_bilbao|"
     r"esqueda_2018|holters_dafx19|dafx22_adaa|pekonen_2011|nam_valimaki_abel_smith)", "S23"),
    (r"docs/papers/antialiasing/", "S24"),
    (r"docs/papers/va_methods/", "S25"),
    (r"docs/papers/sh101/electricdruid_sh101_replacement_processor", "S26"),
    (r"docs/papers/sh101/tubbutec_", "S27"),
    (r"docs/papers/opensource/diy_tubbutec_sh1oh1/", "S27"),
    (r"docs/papers/(sh101/sos_|context/review_sos_)", "S28"),
    (r"docs/papers/sh101/muzines_", "S29"),
    (r"docs/papers/sh101/roland", "S30"),
    (r"docs/papers/forums/sh101_", "S31"),
    (r"docs/papers/sh101/(kenton_|synhouse_|anwander_|maffez_)", "S32"),
    (r"docs/papers/sh101/tal_", "S33"),
    (r"docs/papers/opensource/(diy_|dinsync_re101/)", "S35"),
    (r"docs/papers/forums/opensource_diy_", "S35"),
    (r"docs/papers/opensource/", "S34"),
    (r"docs/papers/(context/|forums/context_)", "S36"),
    (r"docs/papers/forums/anwander_", "S37"),
    (r"docs/papers/sh101/(wikipedia_|vintagesynth_|synthtopia_)", "S38"),
    # reference/
    (r"reference/shootout/[^/]+$", "derived"),
    (r"reference/scenes/", "derived"),
    (r"reference/spread/", "derived"),
    (r"reference/roland-vst/", "roland-plugin"),
    (r"reference/mame/", "mame"),
    (r"reference/online/modwiggler-(\d+)-", r"modwiggler:t\1"),
]

SLUG_IDS = [
    (r"_youtube_([A-Za-z0-9_-]{11})$", r"yt:\1"),
    (r"_soundcloud_(\d+)$", r"soundcloud:\1"),
    (r"^freesound_[^_]+_(\d+)$", r"freesound:\1"),
]


def slug_id(slug):
    for pat, rep in SLUG_IDS:
        m = re.search(pat, slug)
        if m:
            return m.expand(rep)
    return "rec:" + slug


def source_of(rel):
    m = re.match(r"reference/shootout/wav/[^/]+?__(.+?)__", rel)
    if m:
        return slug_id(m.group(1))
    m = re.match(r"reference/shootout/[^/]+/([^/]+)/", rel)
    if m:
        return slug_id(m.group(1))
    m = re.match(r"reference/online/[^/]+_([A-Za-z0-9_-]{11})/", rel)
    if m:
        return "yt:" + m.group(1)
    for pat, sid in RULES:
        m = re.match(pat, rel)
        if m:
            return m.expand(sid) if "\\" in sid else sid
    return ""


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main():
    rows = []
    for root in ROOTS:
        for dirpath, dirnames, filenames in os.walk(PRIVATE / root):
            dirnames[:] = sorted(d for d in dirnames if d != "__pycache__")
            for name in filenames:
                if name == ".DS_Store":
                    continue
                p = Path(dirpath) / name
                if not p.is_file():
                    continue
                rel = p.relative_to(PRIVATE).as_posix()
                rows.append((rel, sha1(p), p.stat().st_size, source_of(rel)))
    rows.sort()
    tmp = PRIVATE / (OUT + ".tmp")
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write("path\tsha1\tbytes\tsource\n")
        for rel, digest, size, sid in rows:
            f.write(f"{rel}\t{digest}\t{size}\t{sid}\n")
    os.replace(tmp, PRIVATE / OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
