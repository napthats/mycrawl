/**
 * @file
 * @brief Rapid Descent: a shortened Dungeon Descent (mycrawl).
**/

#include "AppHdr.h"

#include "rapid-descent.h"

#include "branch.h"
#include "player.h"
#include "random.h"
#include "religion.h"
#include "state.h"
#include "stringutil.h"

/**
 * The floors of each branch that Rapid Descent keeps; any other floor of a
 * listed branch is skipped. Branches that are not listed (portals, the Abyss,
 * Pandemonium...) keep all of their floors.
 *
 * Every branch keeps its last floor, which holds its rune, the Orb, and the
 * entrances to the next branches. Going up from there, every second floor is
 * kept. The Dungeon keeps D:1 instead, since the game starts there, and
 * spreads the rest evenly. This keeps 25 of Descent's 45 floors along the
 * usual route (D, Lair, Orc, a Lair branch, Elf or Crypt, Vaults, Slime,
 * Depths, Zot). See docs/rapid_descent.md.
 */
static const vector<int> *_kept_floors(branch_type br)
{
    static const map<branch_type, vector<int>> kept_floors =
    {
        { BRANCH_DUNGEON, { 1, 3, 5, 8, 10, 12 } },
        { BRANCH_LAIR,    { 1, 3, 5 } },
        { BRANCH_ORC,     { 2 } },
        { BRANCH_SWAMP,   { 2, 4 } },
        { BRANCH_SHOALS,  { 2, 4 } },
        { BRANCH_SNAKE,   { 2, 4 } },
        { BRANCH_SPIDER,  { 2, 4 } },
        { BRANCH_ELF,     { 1, 3 } },
        { BRANCH_CRYPT,   { 1, 3 } },
        { BRANCH_VAULTS,  { 1, 3, 5 } },
        { BRANCH_SLIME,   { 1, 3, 5 } },
        { BRANCH_DEPTHS,  { 2, 4 } },
        { BRANCH_ZOT,     { 1, 3, 5 } },
    };

    if (!crawl_state.game_is_rapid_descent())
        return nullptr;

    auto it = kept_floors.find(br);
    return it == kept_floors.end() ? nullptr : &it->second;
}

bool level_is_skipped(const level_id &lev)
{
    if (lev.branch < 0 || lev.branch >= NUM_BRANCHES)
        return false;

    const vector<int> *kept = _kept_floors(lev.branch);
    if (!kept)
        return false;

    // Never skip the bottom floor, even if brdepth disagrees with the table;
    // and leave anything outside the branch to the callers.
    if (lev.depth < 1 || lev.depth >= brdepth[lev.branch])
        return false;

    return find(kept->begin(), kept->end(), lev.depth) == kept->end();
}

int branch_first_depth(branch_type br)
{
    return level_is_skipped(level_id(br, 1)) ? branch_next_depth(br, 1) : 1;
}

int branch_next_depth(branch_type br, int depth)
{
    int next = depth + 1;
    while (level_is_skipped(level_id(br, next)))
        ++next;
    return next;
}

int branch_prev_depth(branch_type br, int depth)
{
    int prev = depth - 1;
    while (prev > 1 && level_is_skipped(level_id(br, prev)))
        --prev;
    // If every floor above is skipped, act as if the first floor were the
    // top of the branch.
    if (prev >= 1 && level_is_skipped(level_id(br, prev)))
        return 0;
    return prev;
}

int branch_floor_count(branch_type br)
{
    if (brdepth[br] < 1)
        return brdepth[br];

    int count = 0;
    for (int depth = 1; depth <= brdepth[br]; ++depth)
        if (!level_is_skipped(level_id(br, depth)))
            ++count;
    return count;
}

/**
 * How many of the full branch's floors this floor stands in for, as
 * num / den.
 *
 * A branch's last floor is always kept and counts only for itself: its rune
 * vault, end vault or the Orb are one-off content that skipping floors
 * doesn't take away. The other kept floors share out the floors above it,
 * so a branch still adds up to all of its floors. Floors outside Rapid
 * Descent's shortened branches count for one floor.
 */
void rapid_descent_ratio(const level_id &lev, int &num, int &den)
{
    num = den = 1;
    if (lev.branch < 0 || lev.branch >= NUM_BRANCHES
        || !_kept_floors(lev.branch)
        || lev.depth >= brdepth[lev.branch]
        || level_is_skipped(lev))
    {
        return;
    }

    num = brdepth[lev.branch] - 1;
    den = branch_floor_count(lev.branch) - 1;
    ASSERT(den > 0);
}

int rapid_descent_scale(int amount, const level_id &lev)
{
    int num, den;
    rapid_descent_ratio(lev, num, den);
    if (num == den || amount <= 0)
        return amount;
    return div_rand_round(amount * num, den);
}

int rapid_descent_scale(int amount)
{
    return rapid_descent_scale(amount, level_id::current());
}

int rapid_descent_floor_items(int amount)
{
    const level_id lev = level_id::current();
    if (!_kept_floors(lev.branch))
        return amount;

    int num, den;
    rapid_descent_ratio(lev, num, den);
    const int bonus = rapid_descent_loot_value("bonus",
                                               RAPID_DESCENT_ITEM_BONUS);
    return div_rand_round(amount * num * bonus, den * 100);
}

int rapid_descent_unscale(int amount)
{
    int num, den;
    rapid_descent_ratio(level_id::current(), num, den);
    if (num == den || amount <= 0)
        return amount;
    return div_rand_round(amount * den, num);
}

int rapid_descent_modify_piety(int piety)
{
    // Uskayaw's piety comes and goes within each fight; fewer floors don't
    // make for less of it.
    if (you_worship(GOD_USKAYAW))
        return piety;

    return rapid_descent_scale(piety);
}

// ---------------------------------------------------------------------------
// Experimental loot compensations
// ---------------------------------------------------------------------------

static const map<string, int> &_loot_flags()
{
    static const map<string, int> flags = []
    {
        map<string, int> parsed;
        const char *env = getenv("RAPID_DESCENT_LOOT");
        if (!env)
            return parsed;
        for (const string &part : split_string(",", env))
        {
            const vector<string> kv = split_string(":", part);
            if (kv.empty() || kv[0].empty())
                continue;
            parsed[kv[0]] = kv.size() > 1 ? atoi(kv[1].c_str()) : -1;
        }
        return parsed;
    }();
    return flags;
}

bool rapid_descent_loot(const string &flag)
{
    return crawl_state.game_is_rapid_descent() && _loot_flags().count(flag);
}

int rapid_descent_loot_value(const string &flag, int def)
{
    if (!rapid_descent_loot(flag))
        return def;
    const int value = _loot_flags().at(flag);
    return value < 0 ? def : value;
}

vector<level_id> rapid_descent_stand_ins(const level_id &lev)
{
    vector<level_id> stand_ins;
    if (lev.branch < 0 || lev.branch >= NUM_BRANCHES
        || !_kept_floors(lev.branch) || level_is_skipped(lev))
    {
        return stand_ins;
    }

    const branch_type br = lev.branch;
    if (lev.depth == branch_first_depth(br))
        for (int depth = 1; depth < lev.depth; ++depth)
            stand_ins.emplace_back(br, depth);
    for (int depth = lev.depth + 1;
         depth < brdepth[br] && level_is_skipped(level_id(br, depth));
         ++depth)
    {
        stand_ins.emplace_back(br, depth);
    }
    return stand_ins;
}

int rapid_descent_standin_items(const string &flag)
{
    if (!rapid_descent_loot(flag))
        return 0;

    // One item per skipped floor this floor stands in for, at 100%.
    const int floors = rapid_descent_stand_ins(level_id::current()).size();
    return div_rand_round(floors * rapid_descent_loot_value(flag, 100), 100);
}
