#include "catch_amalgamated.hpp"

#include "AppHdr.h"
#include "branch.h"
#include "ng-init.h"
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
