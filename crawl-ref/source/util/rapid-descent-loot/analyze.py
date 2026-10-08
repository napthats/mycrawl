#!/usr/bin/env python3
"""Compare objstat runs (batched) against the Descent baseline, along the
usual Descent route. Usage: python3 -I analyze.py RESDIR REF RUN [RUN...]
Each run dir holds b1..bN batch dirs with objstat_*.tsv (see run_objstat.sh).
Route = D+Lair+Orc+Vaults+Slime+Depths+Zot + 1/4 (Swamp+Shoals+Snake+Spider)
        + 1/2 (Elf+Crypt). Values are per game; +- is the SE from the spread
of batch means."""
import sys, os, math, re, glob

W = {"D": 1, "Lair": 1, "Orc": 1, "Vaults": 1, "Slime": 1, "Depths": 1,
     "Zot": 1, "Swamp": .25, "Shoals": .25, "Snake": .25, "Spider": .25,
     "Elf": .5, "Crypt": .5}
# For a run over one route (e.g. D,Lair,Orc,Swamp,Elf,...), weigh every
# branch fully: ANALYZE=plain.
if "plain" in os.environ.get("ANALYZE", ""):
    W = {b: 1 for b in W}
N = {"D": 12, "Lair": 5, "Orc": 2, "Swamp": 4, "Shoals": 4, "Snake": 4,
     "Spider": 4, "Elf": 3, "Crypt": 3, "Vaults": 5, "Slime": 5, "Depths": 4,
     "Zot": 5}
KEPT = {"D": [1, 3, 5, 8, 10, 12], "Lair": [1, 3, 5], "Orc": [2],
        "Swamp": [2, 4], "Shoals": [2, 4], "Snake": [2, 4], "Spider": [2, 4],
        "Elf": [1, 3], "Crypt": [1, 3], "Vaults": [1, 3, 5],
        "Slime": [1, 3, 5], "Depths": [2, 4], "Zot": [1, 3, 5]}
LEVRE = re.compile(r"^([A-Za-z]+):(\d+)$")

PORTALS = {"glowing drain": "Sewer", "sand-covered staircase": "Ossuary",
           "flagged portal": "Bailey", "frozen archway": "IceCave",
           "dark tunnel": "Volcano", "magical portal": "Wizlab",
           "crumbling gateway": "Desolation",
           "phantasmal passage": "Necropolis",
           "gate leading to a gauntlet": "Gauntlet",
           "gateway to a bazaar": "Bazaar"}
GATES = {"one-way gate to the infinite horrors of the Abyss": "Abyss",
         "one-way gate leading to the halls of Pandemonium": "Pan"}


def ratio(br, depth, rapid):
    if not rapid or br not in KEPT:
        return 1.0
    kept = KEPT[br]
    if depth == N[br]:
        return 1.0
    return (N[br] - 1) / (len(kept) - 1)


def load(path):
    out = {}
    if not os.path.exists(path):
        return out
    with open(path) as f:
        hdr = f.readline().rstrip("\n").split("\t")
        for line in f:
            p = line.rstrip("\n").split("\t")
            d = {}
            for i in range(2, min(len(hdr), len(p))):
                if hdr[i] in d:
                    continue
                try:
                    d[hdr[i]] = float(p[i])
                except ValueError:
                    pass
            if "Num" in d:
                d["Free"] = d["Num"] - d.get("NumShop", 0.0)
            lv = out.setdefault(p[0], {})
            if p[1] in lv:  # duplicate feature names: sum counts
                for k in ("Num", "NumVault", "NumShop", "NumMons", "NumArte",
                          "TotalXP", "TotalXPVault"):
                    if k in d:
                        lv[p[1]][k] = lv[p[1]].get(k, 0) + d[k]
            else:
                lv[p[1]] = d
    return out


def route(rows, field, rapid, xp=False):
    """Sum level rows of the route branches with weights (and r for XP)."""
    tot = 0.0
    for lev, d in rows.items():
        m = LEVRE.match(lev)
        if not m:
            continue
        br, depth = m.group(1), int(m.group(2))
        if br not in W:
            continue
        r = ratio(br, depth, rapid) if xp else 1.0
        tot += W[br] * d.get(field, 0.0) * r
    return tot


class Run:
    def __init__(self, resdir, name):
        self.name = name
        self.dirs = sorted(glob.glob(os.path.join(resdir, name, "b*")))
        self.dirs = [d for d in self.dirs
                     if os.path.exists(os.path.join(d, "objstat_Info.tsv"))]
        cmd = open(os.path.join(resdir, name, "cmd.txt")).read()
        self.rapid = "-rapid-descent" in cmd
        self.cache = {}

    def files(self, fname):
        if fname not in self.cache:
            self.cache[fname] = [load(os.path.join(d, fname))
                                 for d in self.dirs]
        return self.cache[fname]

    def metric(self, fname, key, field="Num", xp=False, keys=None):
        if fname is None:  # shop stock: NumShop summed over item classes
            pairs = [("objstat_%s.tsv" % c, "All " + n) for c, n in SHOP_CLASSES]
        else:
            pairs = [(fname, k) for k in (keys or [key])]
        vals = [0.0] * len(self.dirs)
        for f, k in pairs:
            for i, rows in enumerate(self.files(f)):
                if k in rows:
                    vals[i] += route(rows[k], field, self.rapid, xp)
        n = len(vals)
        if n == 0:
            return float("nan"), float("nan")
        mean = sum(vals) / n
        if n > 1:
            sd = math.sqrt(sum((x - mean) ** 2 for x in vals) / (n - 1))
            se = sd / math.sqrt(n)
        else:
            se = float("nan")
        return mean, se


SHOP_CLASSES = [("Scrolls", "Scrolls"), ("Potions", "Potions"),
                ("Wands", "Wands"), ("Weapons", "Weapons"),
                ("Armour", "Armour"), ("Missiles", "Missiles"),
                ("Jewellery", "Jewellery"), ("MagicalStaves", "Magical Staves"),
                ("Manuals", "Manuals"), ("Spellbooks", "Spellbooks"),
                ("Parchments", "Parchments"), ("Talismans", "Talismans"),
                ("Miscellaneous", "Miscellaneous")]

METRICS = [
    # label, file, key, field, xp
    ("Monster XP (x r)", "objstat_Monsters.tsv", "All Monsters", "TotalXP", True),
    ("Monsters", "objstat_Monsters.tsv", "All Monsters", "Num", False),
    ("Scrolls", "objstat_Scrolls.tsv", "All Scrolls", "Num", False),
    ("Potions", "objstat_Potions.tsv", "All Potions", "Num", False),
    ("Wands", "objstat_Wands.tsv", "All Wands", "Num", False),
    ("Gold", "objstat_Gold.tsv", "gold piece", "Num", False),
    ("Jewellery", "objstat_Jewellery.tsv", "All Jewellery", "Num", False),
    ("  not in shops", "objstat_Jewellery.tsv", "All Jewellery", "Free", False),
    ("  in shops", "objstat_Jewellery.tsv", "All Jewellery", "NumShop", False),
    ("  artefacts", "objstat_Jewellery.tsv", "All Jewellery", "NumArte", False),
    ("Staves", "objstat_MagicalStaves.tsv", "All Magical Staves", "Num", False),
    ("  not in shops", "objstat_MagicalStaves.tsv", "All Magical Staves", "Free", False),
    ("Manuals", "objstat_Manuals.tsv", "All Manuals", "Num", False),
    ("  not in shops", "objstat_Manuals.tsv", "All Manuals", "Free", False),
    ("  in shops", "objstat_Manuals.tsv", "All Manuals", "NumShop", False),
    ("Spellbooks", "objstat_Spellbooks.tsv", "All Spellbooks", "Num", False),
    ("  not in shops", "objstat_Spellbooks.tsv", "All Spellbooks", "Free", False),
    ("Parchments", "objstat_Parchments.tsv", "All Parchments", "Num", False),
    ("  not in shops", "objstat_Parchments.tsv", "All Parchments", "Free", False),
    ("Shop stock (all)", None, None, "NumShop", False),
    ("Talismans", "objstat_Talismans.tsv", "All Talismans", "Num", False),
    ("Misc evocables", "objstat_Miscellaneous.tsv", "All Miscellaneous", "Num", False),
    ("Weapon artefacts", "objstat_Weapons.tsv", "All Weapons", "NumArte", False),
    ("Armour artefacts", "objstat_Armour.tsv", "All Armour", "NumArte", False),
    ("Talisman artefacts", "objstat_Talismans.tsv", "All Talismans", "NumArte", False),
    ("Acquirement", "objstat_Scrolls.tsv", "scroll of acquirement", "Num", False),
    ("Enchant armour", "objstat_Scrolls.tsv", "scroll of enchant armour", "Num", False),
    ("Brand weapon", "objstat_Scrolls.tsv", "scroll of brand weapon", "Num", False),
    ("Potion of experience", "objstat_Potions.tsv", "potion of experience", "Num", False),
    ("Shops", "objstat_Features.tsv", "shop", "Num", False),
    ("Portals (10 kinds)", "objstat_Features.tsv", None, "Num", False),
    ("Abyss/Pan gates", "objstat_Features.tsv", None, "Num", False),
]


def main():
    resdir, ref = sys.argv[1], sys.argv[2]
    runs = [Run(resdir, n) for n in [ref] + sys.argv[3:]]
    base = runs[0]
    w = 22
    print("%-*s" % (w, "metric (per game)") + "".join(
        "%17s" % ("%s[%d]" % (r.name[:12], len(r.dirs))) for r in runs))
    for label, fname, key, field, xp in METRICS:
        keys = None
        if key is None and fname is not None:
            keys = list(PORTALS) if label.startswith("Portals") else list(GATES)
        cells = []
        bm, bse = base.metric(fname, key, field, xp, keys)
        for r in runs:
            m, se = r.metric(fname, key, field, xp, keys)
            if r is base:
                cells.append("%17s" % ("%.4g" % m))
            else:
                rel = m / bm if bm else float("nan")
                # SE of a ratio of independent means
                rse = rel * math.sqrt((se / m if m else 0) ** 2
                                      + (bse / bm if bm else 0) ** 2)
                cells.append("%17s" % ("%.3f+-%.3f" % (rel, rse)))
        print("%-*s" % (w, label) + "".join(cells))
    # artefact total
    cells = []
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
    bm, _ = arte(base)
    for r in runs:
        m, se = arte(r)
        cells.append("%17s" % ("%.4g" % m if r is base else
                               "%.3f+-%.3f" % (m / bm, se / bm)))
    print("%-*s" % (w, "Artefacts (all)") + "".join(cells))
    if "--portals" in os.environ.get("ANALYZE", ""):
        for feat, nm in list(PORTALS.items()) + list(GATES.items()):
            cells = []
            bm, _ = base.metric("objstat_Features.tsv", feat)
            for r in runs:
                m, se = r.metric("objstat_Features.tsv", feat)
                cells.append("%17s" % ("%.3f" % m))
            print("%-*s" % (w, "  " + nm) + "".join(cells))


if __name__ == "__main__":
    main()
