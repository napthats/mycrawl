#!/usr/bin/env python3
"""Desk calculations for docs/rapid_descent_targets.md, from existing objstat
runs only (nothing is generated here).

    python3 -I targets.py RESDIR roles   descent RUN [RUN...]
    python3 -I targets.py RESDIR kept    descent
    python3 -I targets.py RESDIR threat  descent
    python3 -I targets.py RESDIR fights  descent rapid
    python3 -I targets.py RESDIR xl      descent KILLFRACTION
    python3 -I targets.py RESDIR danger  descent rapid KILLFRACTION
    python3 -I targets.py RESDIR piety   descent rapid KILLFRACTION
    python3 -I targets.py RESDIR shops   descent RUN [RUN...]
    python3 -I targets.py RESDIR thin    descent rapid RUN KEEP [PORTALRUN]

roles:  scrolls, potions and wands by role (see ROLE), route totals per game
        and ratios to the first run (all items / not in shops).
kept:   the share of Descent's route totals on the floors Rapid Descent keeps,
        i.e. what Rapid Descent would get with no compensation at all.
threat: monsters and their experience per floor; shares on kept floors,
        branch ends and the late branches.
fights: monsters actually generated in a Rapid Descent run against Descent:
        all, in and out of vaults, on branch ends and elsewhere, their
        experience, and floor cells.
xl:     experience level on arriving at each kept floor, Descent against
        Rapid Descent (experience x r, and without x r), if the player
        kills KILLFRACTION of what is generated (human experience aptitude).
danger: monsters whose HD is at least the player's experience level on
        arriving at their floor, plus an offset, along the route; Rapid
        Descent (its own run, experience x r) against Descent. Also uniques.
piety:  kill piety of a god like Trog (natural monsters, at most one point
        per kill with chance 1 - 6/(18 + HD - XL/2), x r in Rapid Descent):
        where six stars (160) are reached, and the piety left over after
        that for abilities, in total and per kill.
shops:  shop stock per item class.
thin:   RUN (e.g. C2) with every scroll, potion and wand that _builder_items
        places on Rapid Descent's floors kept with chance KEEP, except the
        permanent ones. The floor items in RUN are estimated per floor from
        rapid against descent: they are the only difference on a kept floor,
        f = r x 1.25 of Descent's (1.25 on a branch's last floor), so they are
        f / (f - 1) x (rapid - descent). Slime has no floor items and Orc's
        are all gold. RUN must use the default bonus (125%). Prints every
        category against Descent, with the portal contents if PORTALRUN (the
        portal-loot run) is given.
The route and its weights are those of analyze.py."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A  # noqa: E402

# Anything not listed is "tactical": spent in fights.
ROLE = {
    # Lasting power.
    "scroll of enchant armour": "permanent",
    "scroll of enchant weapon": "permanent",
    "scroll of brand weapon": "permanent",
    "scroll of acquirement": "permanent",
    "potion of experience": "permanent",
    "potion of mutation": "mutation",
    # Escape and healing, the stock kept for emergencies.
    "scroll of teleportation": "emergency",
    "scroll of blinking": "emergency",
    "potion of heal wounds": "emergency",
    # Per floor rather than per fight.
    "scroll of revelation": "mapping",
    "scroll of identify": "identify",
    "potion of enlightenment": "utility",
    "wand of digging": "utility",
    # Rarely worth using.
    "scroll of amnesia": "junk",
    "scroll of noise": "junk",
    "potion of attraction": "junk",
    "potion of moonshine": "junk",
}
CONSUMABLES = ["Scrolls", "Potions", "Wands"]


def _subtypes(run, cls):
    names = set()
    for rows in run.files("objstat_%s.tsv" % cls):
        names |= {k for k in rows if not k.startswith("All ")}
    return sorted(names)


def _mean(run, cls, key, field):
    files = run.files("objstat_%s.tsv" % cls)
    vals = [A.route(r[key], field, run.rapid) if key in r else 0.0
            for r in files]
    return sum(vals) / len(vals) if vals else 0.0


def roles(runs):
    base = runs[0]
    print("%-28s %-10s" % ("item", "role")
          + "".join("%10s" % r.name[:9] for r in runs))
    agg = {}
    for cls in CONSUMABLES:
        for name in _subtypes(base, cls):
            role = ROLE.get(name, "tactical")
            vals = [_mean(r, cls, name, "Num") for r in runs]
            free = [_mean(r, cls, name, "Free") for r in runs]
            for i in range(len(runs)):
                for key in (role, "all " + cls.lower(), "ALL"):
                    a = agg.setdefault((key, i), [0.0, 0.0])
                    a[0] += vals[i]
                    a[1] += free[i]
            print("%-28s %-10s" % (name[:28], role) + "%10.2f" % vals[0]
                  + "".join("%10.3f" % (v / vals[0] if vals[0] else 0)
                            for v in vals[1:]))
    print()
    print("%-22s" % "role: all / not shop"
          + "".join("%18s" % r.name[:16] for r in runs))
    for role in sorted({k for k, _ in agg}):
        b = agg[(role, 0)]
        cells = ["%18s" % ("%.2f / %.2f" % tuple(b))]
        for i in range(1, len(runs)):
            t = agg[(role, i)]
            cells.append("%18s" % ("%.3f / %.3f" % (t[0] / b[0], t[1] / b[1])))
        print("%-22s" % role + "".join(cells))


def _is_kept(br, depth):
    return depth in A.KEPT[br] or depth == A.N[br]


def _split(rows, field):
    kept = total = 0.0
    for lev, d in rows.items():
        m = A.LEVRE.match(lev)
        if not m or m.group(1) not in A.W:
            continue
        br, depth = m.group(1), int(m.group(2))
        v = A.W[br] * d.get(field, 0.0)
        total += v
        if _is_kept(br, depth):
            kept += v
    return kept, total


def kept(run):
    agg = {}

    def add(label, cls, key, field="Num"):
        files = run.files("objstat_%s.tsv" % cls)
        for rows in files:
            if key in rows:
                k, t = _split(rows[key], field)
                a = agg.setdefault(label, [0.0, 0.0])
                a[0] += k / len(files)
                a[1] += t / len(files)

    for cls in CONSUMABLES:
        for name in _subtypes(run, cls):
            add(ROLE.get(name, "tactical"), cls, name)
    add("monsters", "Monsters", "All Monsters")
    add("monster experience", "Monsters", "All Monsters", "TotalXP")
    for cls, key in [("Weapons", "All Weapons"), ("Armour", "All Armour"),
                     ("Jewellery", "All Jewellery"),
                     ("MagicalStaves", "All Magical Staves"),
                     ("Talismans", "All Talismans"),
                     ("Miscellaneous", "All Miscellaneous"),
                     ("Manuals", "All Manuals"),
                     ("Spellbooks", "All Spellbooks"),
                     ("Gold", "gold piece")]:
        add(cls, cls, key)
    for cls, key in [("Weapons", "All Weapons"), ("Armour", "All Armour"),
                     ("Jewellery", "All Jewellery"),
                     ("Talismans", "All Talismans")]:
        add("artefacts", cls, key, "NumArte")
    add("shops", "Features", "shop")
    print("%-20s %10s %10s %7s" % ("quantity", "kept", "all", "share"))
    for label, (k, t) in sorted(agg.items()):
        print("%-20s %10.2f %10.2f %7.3f" % (label, k, t, k / t if t else 0))


def _per_floor(run):
    files = run.files("objstat_Monsters.tsv")
    per = {}
    for rows in files:
        for lev, d in rows["All Monsters"].items():
            m = A.LEVRE.match(lev)
            if not m or m.group(1) not in A.W:
                continue
            p = per.setdefault((m.group(1), int(m.group(2))), [0.0, 0.0])
            p[0] += d.get("Num", 0.0) / len(files)
            p[1] += d.get("TotalXP", 0.0) / len(files)
    return per


def threat(run):
    per = _per_floor(run)
    groups = {"kept floors": lambda br, d: _is_kept(br, d),
              "branch ends": lambda br, d: d == A.N[br],
              "Vaults/Slime/Depths/Zot":
                  lambda br, d: br in ("Vaults", "Slime", "Depths", "Zot")}
    tot = [0.0, 0.0]
    acc = {g: [0.0, 0.0] for g in groups}
    for (br, d), (n, xp) in sorted(per.items()):
        w = A.W[br]
        tot[0] += w * n
        tot[1] += w * xp
        for g, pick in groups.items():
            if pick(br, d):
                acc[g][0] += w * n
                acc[g][1] += w * xp
    print("per game on the route: %.0f monsters, %.0f experience"
          % tuple(tot))
    for g, (n, xp) in acc.items():
        print("%-24s monsters %.3f  experience %.3f"
              % (g, n / tot[0], xp / tot[1]))
    late = [k for k in per if k[0] in ("Vaults", "Slime", "Depths", "Zot")]
    k = sum(per[x][1] for x in late if _is_kept(*x))
    t = sum(per[x][1] for x in late)
    print("%-24s experience kept %.3f" % ("  of which kept", k / t))


def fights(base, rapid):
    def total(run, cls, key, field, pick=lambda br, d: True):
        tot = 0.0
        files = run.files("objstat_%s.tsv" % cls)
        for rows in files:
            for lev, d in rows.get(key, {}).items():
                m = A.LEVRE.match(lev)
                if not m or m.group(1) not in A.W:
                    continue
                if pick(m.group(1), int(m.group(2))):
                    tot += A.W[m.group(1)] * d.get(field, 0.0) / len(files)
        return tot

    end = lambda br, d: d == A.N[br]
    other = lambda br, d: d != A.N[br]
    rows = [("monsters", "Monsters", "All Monsters", "Num", None),
            ("  in vaults", "Monsters", "All Monsters", "NumVault", None),
            ("  on branch ends", "Monsters", "All Monsters", "Num", end),
            ("  on other floors", "Monsters", "All Monsters", "Num", other),
            ("experience", "Monsters", "All Monsters", "TotalXP", None),
            ("floor cells", "Features", "floor", "Num", None)]
    for label, cls, key, field, pick in rows:
        args = (cls, key, field) + ((pick,) if pick else ())
        a, b = total(base, *args), total(rapid, *args)
        print("%-20s Descent %10.1f  Rapid %10.1f  ratio %.3f"
              % (label, a, b, b / a))
    a = total(base, "Monsters", "All Monsters", "Num") \
        - total(base, "Monsters", "All Monsters", "NumVault")
    b = total(rapid, "Monsters", "All Monsters", "Num") \
        - total(rapid, "Monsters", "All Monsters", "NumVault")
    print("%-20s Descent %10.1f  Rapid %10.1f  ratio %.3f"
          % ("  outside vaults", a, b, b / a))


# exp_needed() for experience aptitude 0, levels 1-27 (player.cc).
_XP = [0, 10, 30, 70, 140, 270, 520, 1010, 1980, 3910, 7760, 15450, 26895,
       45585, 72745, 108375, 152475, 205045, 266085, 335595, 413575, 500025,
       594945, 698335, 810195]
while len(_XP) < 27:
    _XP.append(2 * _XP[-1] - _XP[-2] + 8470)


def _level(xp):
    lv = max(i + 1 for i, need in enumerate(_XP) if xp >= need)
    if lv >= 27:
        return 27.0
    return lv + (xp - _XP[lv - 1]) / (_XP[lv] - _XP[lv - 1])


# The usual route in order, one Lair branch and one of Elf/Crypt (averaged),
# as in analyze.py.
_ROUTE = ([(("D",), d) for d in range(1, 13)]
          + [(("Lair",), d) for d in range(1, 6)]
          + [(("Orc",), d) for d in range(1, 3)]
          + [(("Swamp", "Shoals", "Snake", "Spider"), d) for d in range(1, 5)]
          + [(("Vaults",), d) for d in range(1, 6)]
          + [(("Elf", "Crypt"), d) for d in range(1, 4)]
          + [(("Depths",), d) for d in range(1, 5)]
          + [(("Slime",), d) for d in range(1, 6)]
          + [(("Zot",), d) for d in range(1, 6)])
_NOT_UNIQUE = {"Brimstone Fiend", "Executioner", "Ice Fiend", "Killer Klown",
               "Monster", "Orb Guardian", "Tzitzimitl"}


def _types(run):
    """(branch, depth) -> monster name -> [number, experience, HD], averaged
    over batches (HD weighted by number)."""
    files = run.files("objstat_Monsters.tsv")
    out = {}
    for rows in files:
        for name, levs in rows.items():
            if name == "All Monsters":
                continue
            for lev, d in levs.items():
                m = A.LEVRE.match(lev)
                if not m or m.group(1) not in A.W:
                    continue
                t = out.setdefault((m.group(1), int(m.group(2))), {})
                t = t.setdefault(name, [0.0, 0.0, 0.0])
                num = d.get("Num", 0.0)
                t[0] += num / len(files)
                t[1] += d.get("TotalXP", 0.0) / len(files)
                t[2] += num * d.get("MonsHD", 0.0) / len(files)
    for floor in out.values():
        for t in floor.values():
            t[2] = t[2] / t[0] if t[0] else 0.0
    return out


def _walk(run, kill):
    """Floors of the route the run visits, in order, with the experience
    level on arrival: (branches, depth, ratio r, level, monster types)."""
    types = _types(run)
    xp = 0.0
    for brs, d in _ROUTE:
        br = brs[0]
        if run.rapid and not _is_kept(br, d):
            continue
        r = A.ratio(br, d, run.rapid)
        floor = {}
        for b in brs:
            for name, (num, exp, hd) in types.get((b, d), {}).items():
                f = floor.setdefault(name, [0.0, 0.0, hd])
                f[0] += num / len(brs)
                f[1] += exp / len(brs)
        yield brs, d, r, _level(xp), floor
        xp += kill * r * sum(f[1] for f in floor.values())


def danger(base, rapid, kill):
    def count(run, off):
        n = 0.0
        for _, _, _, lv, floor in _walk(run, kill):
            n += sum(f[0] for f in floor.values() if f[2] >= lv + off)
        return n

    for off in (-5, -3, 0, 3):
        a, b = count(base, off), count(rapid, off)
        print("HD >= XL%+d on arrival: Descent %7.1f  Rapid %7.1f  ratio %.3f"
              % (off, a, b, b / a))
    uniq = []
    for run in (base, rapid):
        uniq.append(sum(f[0] for *_, floor in _walk(run, kill)
                        for name, f in floor.items()
                        if name[:1].isupper() and name not in _NOT_UNIQUE))
    print("uniques: Descent %.1f  Rapid %.1f  ratio %.3f"
          % (uniq[0], uniq[1], uniq[1] / uniq[0]))


def piety(base, rapid, kill):
    for run in (base, rapid):
        piety = left = left_kills = 0.0
        six = None
        for brs, d, r, lv, floor in _walk(run, kill):
            kills = kill * sum(f[0] for f in floor.values())
            gain = 0.0
            for num, _, hd in floor.values():
                den = 18 + hd - int(lv) // 2
                chance = max(0.0, 1 - 6.0 / den) if den > 0 else 0.0
                gain += kill * num * chance * r
            # Gains slow down above 100 and 160 piety (religion.cc).
            for _ in range(100):
                rate = 1.0 if piety < 100 else 2 / 3 if piety < 160 else 4 / 9
                if piety >= 160:
                    left += gain / 100 * rate
                    left_kills += kills / 100
                else:
                    piety = min(160.0, piety + gain / 100 * rate)
            if six is None and piety >= 160:
                six = "/".join(brs) + ":%d" % d
        print("%-10s six stars at %-28s left over %6.1f over %6.0f kills"
              " (%.3f per kill)" % (run.name, six, left, left_kills,
                                    left / left_kills if left_kills else 0))


def xl(run, kill):
    per = _per_floor(run)
    route = _ROUTE
    xd = xr = xn = 0.0
    print("%-16s %8s %8s %7s %8s" % ("arriving at", "Descent", "Rapid",
                                      "diff", "no x r"))
    for brs, d in route:
        br = brs[0]
        gained = kill * sum(per.get((b, d), (0, 0))[1]
                                for b in brs) / len(brs)
        if _is_kept(br, d):
            name = "Lair branch" if len(brs) == 4 else "/".join(brs)
            print("%-16s %8.2f %8.2f %+7.2f %8.2f"
                  % (name + ":%d" % d, _level(xd), _level(xr),
                     _level(xr) - _level(xd), _level(xn)))
            xr += gained * A.ratio(br, d, True)
            xn += gained
        xd += gained
    print("%-16s %8.2f %8.2f %+7.2f %8.2f"
          % ("end", _level(xd), _level(xr), _level(xr) - _level(xd),
             _level(xn)))


def shops(runs):
    print("%-16s" % "class" + "".join("%12s" % r.name[:11] for r in runs))
    tot = [0.0] * len(runs)
    for cls, name in A.SHOP_CLASSES:
        vals = [r.metric("objstat_%s.tsv" % cls, "All " + name, "NumShop")[0]
                for r in runs]
        tot = [a + b for a, b in zip(tot, vals)]
        print("%-16s" % cls + "".join("%12.2f" % v for v in vals))
    print("%-16s" % "total" + "".join("%12.2f" % v for v in tot))


THIN_EXEMPT = {"permanent"}
NO_FLOOR_CONSUMABLES = {"Slime", "Orc"}  # brflag::no_items; Orc: all gold


def _batches(run, cls, key, field):
    """Per batch: {(branch, depth): value} for one item key."""
    out = []
    for rows in run.files("objstat_%s.tsv" % cls):
        lv = {}
        for lev, d in rows.get(key, {}).items():
            m = A.LEVRE.match(lev)
            if m and m.group(1) in A.W:
                lv[(m.group(1), int(m.group(2)))] = d.get(field, 0.0)
        out.append(lv)
    return out


def _route_sum(levels):
    return sum(A.W[br] * v for (br, _), v in levels.items())


def _floor_items(base, rapid, cls, key, field="Num"):
    """Per batch pair: the floor items Rapid Descent places on the route."""
    out = []
    for d, r in zip(_batches(base, cls, key, field),
                    _batches(rapid, cls, key, field)):
        tot = 0.0
        for (br, depth), vr in r.items():
            if br in NO_FLOOR_CONSUMABLES or not _is_kept(br, depth):
                continue
            f = A.ratio(br, depth, True) * 1.25
            tot += A.W[br] * f * (vr - d.get((br, depth), 0.0)) / (f - 1)
        out.append(tot)
    return out


def _stat(vals):
    n = len(vals)
    m = sum(vals) / n
    se = (sum((x - m) ** 2 for x in vals) / (n - 1) / n) ** 0.5 if n > 1 else 0
    return m, se


def _ratio(num, den):
    (a, sa), (b, sb) = _stat(num), _stat(den)
    rel = a / b
    return rel, rel * ((sa / a if a else 0) ** 2 + (sb / b) ** 2) ** 0.5


def thin(base, rapid, run, keep, portal_run=None):
    agg = {}

    def add(key, vals):
        a = agg.setdefault(key, [[0.0] * len(vals[0]) for _ in range(3)])
        for i in range(3):
            a[i] = [x + y for x, y in zip(a[i], vals[i])]

    print("Scrolls, potions and wands (route, per game; ratios to Descent;"
          " floor = floor items in %s)" % run.name)
    print("%-28s %-10s %8s %7s %14s %15s"
          % ("item", "role", "Descent", "floor", run.name, "thinned"))
    for cls in CONSUMABLES:
        for name in _subtypes(base, cls):
            role = ROLE.get(name, "tactical")
            d = [_route_sum(b) for b in _batches(base, cls, name, "Num")]
            c = [_route_sum(b) for b in _batches(run, cls, name, "Num")]
            fl = _floor_items(base, rapid, cls, name)
            t = c if role in THIN_EXEMPT else \
                [x - (1 - keep) * y for x, y in zip(c, fl)]
            for key in (role, "all " + cls.lower(),
                        "all consumables", "thinned" if role not in
                        THIN_EXEMPT else "not thinned"):
                add(key, (d, c, t))
            add(("by class", cls, role), (d, c, t))
            rc, sc = _ratio(c, d)
            rt, st = _ratio(t, d)
            print("%-28s %-10s %8.2f %7.2f %7.3f+-%.3f %8.3f+-%.3f"
                  % (name[:28], role, _stat(d)[0], _stat(fl)[0], rc, sc,
                     rt, st))
    print()
    print("%-22s %8s %14s %15s" % ("group", "Descent", run.name, "thinned"))
    for key in sorted(k for k in agg if isinstance(k, str)):
        d, c, t = agg[key]
        rc, sc = _ratio(c, d)
        rt, st = _ratio(t, d)
        print("%-22s %8.2f %7.3f+-%.3f %8.3f+-%.3f"
              % (key, _stat(d)[0], rc, sc, rt, st))

    # Wand charges: Chrg is the mean charges per wand of a row.
    def charges(r):
        out = []
        for num, chrg in zip(_batches(r, "Wands", "All Wands", "Num"),
                             _batches(r, "Wands", "All Wands", "Chrg")):
            out.append({k: v * chrg.get(k, 0.0) for k, v in num.items()})
        return out
    cd, cr, cc = charges(base), charges(rapid), charges(run)
    fl = []
    for d, r in zip(cd, cr):
        tot = 0.0
        for (br, depth), vr in r.items():
            if br in NO_FLOOR_CONSUMABLES or not _is_kept(br, depth):
                continue
            f = A.ratio(br, depth, True) * 1.25
            tot += A.W[br] * f * (vr - d.get((br, depth), 0.0)) / (f - 1)
        fl.append(tot)
    d = [_route_sum(x) for x in cd]
    c = [_route_sum(x) for x in cc]
    t = [x - (1 - keep) * y for x, y in zip(c, fl)]
    print("%-22s %8.2f %7.3f+-%.3f %8.3f+-%.3f"
          % ("wand charges", _stat(d)[0], *_ratio(c, d), *_ratio(t, d)))

    print()
    print("Everything else is the same as in %s:" % run.name)
    for label, fname, key, field, xp in A.METRICS:
        if fname in ("objstat_Scrolls.tsv", "objstat_Potions.tsv",
                     "objstat_Wands.tsv"):
            continue
        keys = None
        if key is None and fname is not None:
            keys = list(A.PORTALS) if label.startswith("Portals") \
                else list(A.GATES)
        bm, bse = base.metric(fname, key, field, xp, keys)
        m, se = run.metric(fname, key, field, xp, keys)
        rel = m / bm
        rse = rel * ((se / m if m else 0) ** 2 + (bse / bm) ** 2) ** 0.5
        print("%-22s %10.4g %7.3f+-%.3f" % (label, bm, rel, rse))
    arte = [("objstat_Weapons.tsv", "All Weapons"),
            ("objstat_Armour.tsv", "All Armour"),
            ("objstat_Jewellery.tsv", "All Jewellery"),
            ("objstat_Talismans.tsv", "All Talismans")]
    ab = sum(base.metric(f, k, "NumArte")[0] for f, k in arte)
    ar = sum(run.metric(f, k, "NumArte")[0] for f, k in arte)
    print("%-22s %10.4g %7.3f" % ("Artefacts (all)", ab, ar / ab))

    if not portal_run:
        return
    import portal_loot as P
    loot, _ = P.per_portal(*portal_run)
    inv = {v: k for k, v in A.PORTALS.items()}

    def portal(r, col):
        return sum(r.metric("objstat_Features.tsv", inv[p])[0] * loot[p][col]
                   for p in P.LEVEL)
    print()
    print("Portal contents per game (unchanged), and totals with them:")
    for col, label, dungeon in [
            ("Scrolls", "scrolls", agg["all scrolls"]),
            ("Potions", "potions", agg["all potions"]),
            ("Wands", "wands", agg["all wands"])]:
        pd, pr = portal(base, col), portal(run, col)
        d = _stat(dungeon[0])[0] + pd
        t = _stat(dungeon[2])[0] + pr
        c = _stat(dungeon[1])[0] + pr
        print("%-22s portals %5.2f / %5.2f   with portals: Descent %7.2f"
              "  %s %.3f  thinned %.3f" % (label, pd, pr, d, run.name,
                                           c / d, t / d))
    for col, fname, key, field in [
            ("Artefacts", None, None, "NumArte"),
            ("Jewellery", "objstat_Jewellery.tsv", "All Jewellery", "Num")]:
        pd, pr = portal(base, col), portal(run, col)
        if fname:
            bd, br_ = base.metric(fname, key, field)[0], \
                run.metric(fname, key, field)[0]
        else:
            bd, br_ = ab, ar
        print("%-22s portals %5.2f / %5.2f   with portals: Descent %7.2f"
              "  ratio %.3f" % (col.lower(), pd, pr, bd + pd,
                                (br_ + pr) / (bd + pd)))


def main():
    resdir, what = sys.argv[1], sys.argv[2]
    if what == "xl":
        xl(A.Run(resdir, sys.argv[3]), float(sys.argv[4]))
        return
    if what == "thin":
        thin(A.Run(resdir, sys.argv[3]), A.Run(resdir, sys.argv[4]),
             A.Run(resdir, sys.argv[5]), float(sys.argv[6]),
             (resdir, sys.argv[7]) if len(sys.argv) > 7 else None)
        return
    if what == "fights":
        fights(A.Run(resdir, sys.argv[3]), A.Run(resdir, sys.argv[4]))
        return
    if what in ("danger", "piety"):
        base, rapid = A.Run(resdir, sys.argv[3]), A.Run(resdir, sys.argv[4])
        (danger if what == "danger" else piety)(base, rapid,
                                                 float(sys.argv[5]))
        return
    runs = [A.Run(resdir, n) for n in sys.argv[3:]]
    {"roles": lambda: roles(runs), "kept": lambda: kept(runs[0]),
     "threat": lambda: threat(runs[0]), "shops": lambda: shops(runs)}[what]()


if __name__ == "__main__":
    main()
