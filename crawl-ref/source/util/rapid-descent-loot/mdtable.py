#!/usr/bin/env python3
"""Markdown table of selected metrics: ratio to Descent (+-SE).
Usage: python3 -I mdtable.py RESDIR REF RUN..."""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A

SEL = ["Monster XP (x r)", "Scrolls", "Potions", "Wands", "Gold",
       "Jewellery", "  not in shops", "Staves", "Manuals", "Spellbooks",
       "Parchments", "Talismans", "Misc evocables", "Weapon artefacts",
       "Armour artefacts", "Shop stock (all)", "Shops", "Portals (10 kinds)",
       "Abyss/Pan gates"]


def main():
    resdir, ref = sys.argv[1], sys.argv[2]
    runs = [A.Run(resdir, n) for n in [ref] + sys.argv[3:]]
    base = runs[0]
    print("| metric | Descent | " + " | ".join(r.name for r in runs[1:]) + " |")
    print("|---|---:|" + "---:|" * (len(runs) - 1))
    last = None
    for label, fname, key, field, xp in A.METRICS:
        if label.startswith("  ") and last not in ("Jewellery", "Manuals"):
            continue
        if not label.startswith("  "):
            last = label
        if label not in SEL and not label.startswith("  "):
            continue
        keys = None
        if key is None and fname is not None:
            keys = list(A.PORTALS) if label.startswith("Portals") else list(A.GATES)
        bm, bse = base.metric(fname, key, field, xp, keys)
        cells = []
        for r in runs[1:]:
            m, se = r.metric(fname, key, field, xp, keys)
            rel = m / bm
            rse = rel * math.sqrt((se / m if m else 0) ** 2 + (bse / bm) ** 2)
            cells.append("%.2f±%.2f" % (rel, rse))
        name = label.strip() if not label.startswith("  ") else last + " (" + label.strip() + ")"
        print("| %s | %.4g | %s |" % (name, bm, " | ".join(cells)))
    # artefacts total
    def arte(r):
        tot, var = 0.0, 0.0
        for f, k in [("objstat_Weapons.tsv", "All Weapons"),
                     ("objstat_Armour.tsv", "All Armour"),
                     ("objstat_Jewellery.tsv", "All Jewellery"),
                     ("objstat_Talismans.tsv", "All Talismans")]:
            m, se = r.metric(f, k, "NumArte")
            tot += m
            var += (se if se == se else 0) ** 2
        return tot, math.sqrt(var)
    bm, bse = arte(base)
    cells = []
    for r in runs[1:]:
        m, se = arte(r)
        rel = m / bm
        cells.append("%.2f±%.2f" % (rel, rel * math.sqrt((se / m) ** 2 + (bse / bm) ** 2)))
    print("| Artefacts (all) | %.4g | %s |" % (bm, " | ".join(cells)))


if __name__ == "__main__":
    main()
