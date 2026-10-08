#include "catch_amalgamated.hpp"

#include "AppHdr.h"
#include "branch.h"
#include "item-def.h"
#include "item-prop-enum.h"
#include "ng-init.h"
#include "player.h"
#include "potion-type.h"
#include "rapid-descent.h"
#include "state.h"
#include "unwind.h"

// mycrawl: Rapid Descent's floors and how much each of them counts for.

static const vector<branch_type> descent_branches =
{
    BRANCH_DUNGEON, BRANCH_LAIR, BRANCH_ORC, BRANCH_SWAMP, BRANCH_SHOALS,
    BRANCH_SNAKE, BRANCH_SPIDER, BRANCH_ELF, BRANCH_CRYPT, BRANCH_VAULTS,
    BRANCH_SLIME, BRANCH_DEPTHS, BRANCH_ZOT,
};

TEST_CASE( "rapid_descent_floors", "[single-file]" )
{
    {
        unwind_var<game_type> type(crawl_state.type,
                                   GAME_TYPE_RAPID_DESCENT);
        initialise_branch_depths();

        REQUIRE(brdepth[BRANCH_DUNGEON] == 12);
        REQUIRE(!level_is_skipped(level_id(BRANCH_DUNGEON, 1)));

        int floors = 0;
        int full_floors = 0;
        for (branch_type br : descent_branches)
        {
            CAPTURE(br);
            const int bottom = brdepth[br];

            // The last floor holds the rune or the next branches' entrances.
            REQUIRE(!level_is_skipped(level_id(br, bottom)));

            // Walking down the stairs visits exactly the kept floors.
            int seen = 0;
            int prev = 0;
            double counted = 0;
            for (int depth = branch_first_depth(br); depth <= bottom;
                 depth = branch_next_depth(br, depth))
            {
                REQUIRE(!level_is_skipped(level_id(br, depth)));
                REQUIRE(branch_prev_depth(br, depth) == prev);
                prev = depth;
                ++seen;

                int num, den;
                rapid_descent_ratio(level_id(br, depth), num, den);
                REQUIRE(num >= den);
                counted += double(num) / den;
            }
            REQUIRE(prev == bottom);
            REQUIRE(seen == branch_floor_count(br));
            // About half of the floors are left...
            REQUIRE(seen * 2 <= bottom + 1);
            // ... and together they still count for the whole branch, unless
            // only the last floor is left (the Orcish Mines): it counts once.
            if (seen > 1)
                REQUIRE(counted == Catch::Approx(bottom));
            else
                REQUIRE(counted == Catch::Approx(1));

            floors += seen;
            full_floors += bottom;
        }
        REQUIRE(floors == 33);
        REQUIRE(full_floors == 60);

        // Floors outside the shortened branches are all there.
        REQUIRE(!level_is_skipped(level_id(BRANCH_ABYSS, 2)));
        REQUIRE(branch_next_depth(BRANCH_ABYSS, 2) == 3);
    }

    {
        unwind_var<game_type> type(crawl_state.type, GAME_TYPE_DESCENT);
        initialise_branch_depths();

        for (branch_type br : descent_branches)
        {
            CAPTURE(br);
            REQUIRE(branch_first_depth(br) == 1);
            REQUIRE(branch_floor_count(br) == brdepth[br]);
            for (int depth = 1; depth <= brdepth[br]; ++depth)
            {
                REQUIRE(!level_is_skipped(level_id(br, depth)));
                REQUIRE(branch_next_depth(br, depth) == depth + 1);
                REQUIRE(branch_prev_depth(br, depth) == depth - 1);
                REQUIRE(rapid_descent_scale(100, level_id(br, depth))
                        == 100);
            }
        }
    }

    initialise_branch_depths();
}

TEST_CASE( "rapid_descent_stand_ins", "[single-file]" )
{
    {
        unwind_var<game_type> type(crawl_state.type,
                                   GAME_TYPE_RAPID_DESCENT);
        initialise_branch_depths();

        // Every skipped floor is stood in for by exactly one kept floor.
        for (branch_type br : descent_branches)
        {
            CAPTURE(br);
            set<int> covered;
            for (int depth = 1; depth <= brdepth[br]; ++depth)
            {
                const level_id lev(br, depth);
                const vector<level_id> stand_ins = rapid_descent_stand_ins(lev);
                if (level_is_skipped(lev))
                    REQUIRE(stand_ins.empty());
                REQUIRE(rapid_descent_rolls(lev) == 1 + (int)stand_ins.size());
                for (const level_id &s : stand_ins)
                {
                    REQUIRE(s.branch == br);
                    REQUIRE(level_is_skipped(s));
                    REQUIRE(covered.insert(s.depth).second);
                }
            }
            REQUIRE((int)covered.size()
                    == brdepth[br] - branch_floor_count(br));
        }

        const vector<level_id> d5 =
            rapid_descent_stand_ins(level_id(BRANCH_DUNGEON, 5));
        REQUIRE(d5.size() == 2);
        REQUIRE(d5[0].depth == 6);
        REQUIRE(d5[1].depth == 7);
        // The first kept floor also stands in for the floors above it.
        const vector<level_id> orc2 =
            rapid_descent_stand_ins(level_id(BRANCH_ORC, 2));
        REQUIRE(orc2.size() == 1);
        REQUIRE(orc2[0].depth == 1);
        REQUIRE(rapid_descent_stand_ins(level_id(BRANCH_SWAMP, 2)).size()
                == 2);
        REQUIRE(rapid_descent_stand_ins(level_id(BRANCH_ZOT, 5)).empty());
        REQUIRE(rapid_descent_stand_ins(level_id(BRANCH_ABYSS, 2)).empty());
    }

    {
        unwind_var<game_type> type(crawl_state.type, GAME_TYPE_DESCENT);
        initialise_branch_depths();
        for (branch_type br : descent_branches)
            for (int depth = 1; depth <= brdepth[br]; ++depth)
                REQUIRE(rapid_descent_rolls(level_id(br, depth)) == 1);
    }

    initialise_branch_depths();
}

static item_def _consumable(object_class_type base, int sub)
{
    item_def item;
    item.base_type = base;
    item.sub_type = sub;
    item.quantity = 1;
    return item;
}

static int _kept_of(const item_def &item, int tries)
{
    int kept = 0;
    for (int i = 0; i < tries; ++i)
        if (rapid_descent_keep_floor_item(item))
            ++kept;
    return kept;
}

TEST_CASE( "rapid_descent_floor_consumables", "[single-file]" )
{
    const item_def teleport = _consumable(OBJ_SCROLLS, SCR_TELEPORTATION);
    const item_def curing = _consumable(OBJ_POTIONS, POT_CURING);
    const item_def flame = _consumable(OBJ_WANDS, WAND_FLAME);
    const vector<item_def> lasting =
    {
        _consumable(OBJ_SCROLLS, SCR_ENCHANT_ARMOUR),
        _consumable(OBJ_SCROLLS, SCR_ENCHANT_WEAPON),
        _consumable(OBJ_SCROLLS, SCR_BRAND_WEAPON),
        _consumable(OBJ_SCROLLS, SCR_ACQUIREMENT),
        _consumable(OBJ_POTIONS, POT_EXPERIENCE),
    };
    item_def gold;
    gold.base_type = OBJ_GOLD;
    gold.quantity = 10;

    unwind_var<branch_type> branch(you.where_are_you, BRANCH_DUNGEON);
    unwind_var<int> depth(you.depth, 3);

    {
        unwind_var<game_type> type(crawl_state.type,
                                   GAME_TYPE_RAPID_DESCENT);
        initialise_branch_depths();

        // About half of the consumables used up in fights are left out...
        for (const item_def &item : { teleport, curing, flame })
        {
            const int kept = _kept_of(item, 2000);
            REQUIRE(kept > 2000 * (RAPID_DESCENT_CONSUMABLE_KEEP - 5) / 100);
            REQUIRE(kept < 2000 * (RAPID_DESCENT_CONSUMABLE_KEEP + 5) / 100);
        }
        // ... but not those that give lasting power, nor anything else.
        for (const item_def &item : lasting)
            REQUIRE(_kept_of(item, 200) == 200);
        REQUIRE(_kept_of(gold, 200) == 200);

        // Branches that Rapid Descent doesn't shorten keep everything.
        unwind_var<branch_type> abyss(you.where_are_you, BRANCH_ABYSS);
        unwind_var<int> abyss_depth(you.depth, 2);
        REQUIRE(_kept_of(teleport, 200) == 200);
    }

    {
        unwind_var<game_type> type(crawl_state.type, GAME_TYPE_DESCENT);
        initialise_branch_depths();
        REQUIRE(_kept_of(teleport, 200) == 200);
        REQUIRE(_kept_of(curing, 200) == 200);
    }

    initialise_branch_depths();
}
