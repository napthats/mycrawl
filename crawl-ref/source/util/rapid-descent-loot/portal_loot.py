#!/usr/bin/env python3
"""Loot per portal level (objstat run over portal branches only) and the
expected loot per game from portal entrance counts of other runs.
Usage: python3 -I portal_loot.py RESDIR LOOTRUN REF RUN [RUN...]"""
import sys, os, glob, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze import Run, PORTALS, load  # noqa: E402

LEVEL = {"Sewer": "Sewer", "Ossuary": "Ossuary", "Bailey": "Bailey",
         "IceCave": "IceCv", "Volcano": "Volcano", "Wizlab": "WizLab",
         "Desolation": "Desolation", "Necropolis": "Necropolis",
         "Gauntlet": "Gauntlet", "Bazaar": "Bazaar"}
CLASSES = [("Scrolls", "All Scrolls"), ("Potions", "All Potions"),
           ("Jewellery", "All Jewellery"), ("MagicalStaves", "All Magical Staves"),
           ("Manuals", "All Manuals"), ("Spellbooks", "All Spellbooks"),
           ("Miscellaneous", "All Miscellaneous"), ("Wands", "All Wands"),
           ("Talismans", "All Talismans")]
ARTE = [("Weapons", "All Weapons"), ("Armour", "All Armour"),
        ("Jewellery", "All Jewellery"), ("Talismans", "All Talismans")]


def per_portal(resdir, name):
    dirs = [d for d in sorted(glob.glob(os.path.join(resdir, name, "b*")))
            if os.path.exists(os.path.join(d, "objstat_Info.tsv"))]
    out = {p: {} for p in LEVEL}
    for cls, key in CLASSES + [("Gold", "gold piece")]:
        for p, lev in LEVEL.items():
            vals = []
            for d in dirs:
                rows = load(os.path.join(d, "objstat_%s.tsv" % cls))
                vals.append(rows.get(key, {}).get(lev, {}).get("Num", 0.0))
            out[p][cls] = sum(vals) / len(vals) if vals else 0.0
    for p, lev in LEVEL.items():
        tot = 0.0
        for cls, key in ARTE:
            vals = []
            for d in dirs:
                rows = load(os.path.join(d, "objstat_%s.tsv" % cls))
                vals.append(rows.get(key, {}).get(lev, {}).get("NumArte", 0.0))
            tot += sum(vals) / len(vals) if vals else 0.0
        out[p]["Artefacts"] = tot
    return out, len(dirs)


def main():
    resdir, lootrun = sys.argv[1], sys.argv[2]
    loot, nb = per_portal(resdir, lootrun)
    cols = [c for c, _ in CLASSES] + ["Gold", "Artefacts"]
    print("Loot per portal level (%d batches):" % nb)
    print("%-11s" % "" + "".join("%9s" % c[:8] for c in cols))
    for p in LEVEL:
        print("%-11s" % p + "".join("%9.2f" % loot[p][c] for c in cols))
    runs = [Run(resdir, n) for n in sys.argv[3:]]
    print("\nExpected portal loot per game on the route:")
    print("%-14s" % "run" + "".join("%9s" % c[:8] for c in cols))
    inv = {v: k for k, v in PORTALS.items()}
    for r in runs:
        counts = {p: r.metric("objstat_Features.tsv", inv[p])[0] for p in LEVEL}
        row = [sum(counts[p] * loot[p][c] for p in LEVEL) for c in cols]
        print("%-14s" % r.name + "".join("%9.2f" % v for v in row))


if __name__ == "__main__":
    main()
