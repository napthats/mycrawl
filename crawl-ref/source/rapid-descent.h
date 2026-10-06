/**
 * @file
 * @brief Rapid Descent: a shortened Dungeon Descent (mycrawl).
 *
 * Rapid Descent plays like Descent, but leaves out about half of the floors
 * of every branch. The floors that remain keep their usual level_ids, so they
 * are built exactly like the same floors in Descent; only the stairs skip the
 * missing floors. To make up for the lost floors, each remaining floor gives
 * more experience, piety and floor items (see rapid_descent_scale()).
**/

#pragma once

#include "branch-type.h"
#include "level-id.h"

// Is this floor left out of the current game? Always false outside Rapid
// Descent, and for floors past the bottom of a branch.
bool level_is_skipped(const level_id &lev);

// The depth of the first floor of a branch that the game uses (normally 1).
int branch_first_depth(branch_type br);
// The depth of the next floor below `depth` (normally depth + 1).
int branch_next_depth(branch_type br, int depth);
// The depth of the previous floor above `depth` (normally depth - 1).
int branch_prev_depth(branch_type br, int depth);
// How many floors of a branch the game uses.
int branch_floor_count(branch_type br);

// Each remaining floor of a shortened branch gives this percentage of its
// usual floor items, on top of the floors it stands in for. This makes up
// for the vault loot of the skipped floors.
const int RAPID_DESCENT_ITEM_BONUS = 125;

// How many of the full branch's floors a floor stands in for, as
// numerator / denominator. 1/1 outside Rapid Descent.
void rapid_descent_ratio(const level_id &lev, int &num, int &den);
// Scale experience and piety gained on a floor by that ratio. Without a
// floor, use the player's current floor.
int rapid_descent_scale(int amount, const level_id &lev);
int rapid_descent_scale(int amount);
// Undo rapid_descent_scale(), for gains that already grew with experience.
int rapid_descent_unscale(int amount);
// Scale piety gains, for gods whose piety comes from exploring or killing.
int rapid_descent_modify_piety(int piety);
// How many items to place on the floor of the level being built.
int rapid_descent_floor_items(int amount);
