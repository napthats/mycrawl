/**
 * @file
 * @brief Functions used to print information about various game objects.
**/

#pragma once

#include <functional>
#include <sstream>
#include <string>

#include "attack.h"
#include "command-type.h"
#include "deck-type.h"
#include "enum.h"
#include "mon-util.h"
#include "tag-version.h"
#include "trap-type.h"

struct monster_info;

// If you add any more description types, remember to also
// change item_description in externs.h
enum item_description_type
{
    IDESC_WANDS = 0,
    IDESC_POTIONS,
    IDESC_SCROLLS,                      // special field (like the others)
    IDESC_RINGS,
#if TAG_MAJOR_VERSION == 34
    IDESC_SCROLLS_II,
#endif
    IDESC_STAVES,
    NUM_IDESC
};

enum item_description_mode
{
    IDM_DEFAULT,
    IDM_PLAIN,
    IDM_DUMP,
    IDM_MONSTER,
    NUM_IDM_TYPES,
};

struct describe_info
{
    ostringstream body;
    string title;
    string prefix;
    string suffix;
    string footer;
    string quote;
};

bool is_dumpable_artefact(const item_def &item);

string get_item_description(const item_def &item,
                            item_description_mode mode = IDM_DEFAULT);

bool describe_feature_wide(const coord_def& pos, bool do_actions=false);
void describe_feature_type(dungeon_feature_type feat);
string get_cloud_desc(cloud_type cloud, bool include_title = true);
void get_feature_desc(const coord_def &gc, describe_info &inf, bool include_extra = true);

command_type describe_item_popup(const item_def &item,
                                 function<void (string&)> fixup_desc = nullptr,
                                 bool do_actions = false);
bool describe_item(item_def &item, function<void (string&)> fixup_desc = nullptr,
    bool do_actions = true);
string describe_item_rarity(const item_def &item);
void get_item_desc(const item_def &item, describe_info &inf);
void inscribe_item(item_def &item);
void target_item(item_def &item);
void desc_randart_props(const item_def &item, vector<string> &lines);
string damage_rating(const item_def *item, int *rating_value = nullptr);

int describe_monster(const monster_info &mi, const string& footer = "");

void get_monster_db_desc(const monster_info &mi, describe_info &inf,
                         bool &has_stat_desc, bool mark_spells=false);
branch_type serpent_of_hell_branch(monster_type m);
string serpent_of_hell_flavour(monster_type m);

string player_spell_desc(spell_type spell);
void get_spell_desc(const spell_type spell, describe_info &inf);
void describe_spell(spell_type spelled,
                    const monster_info *mon_owner = nullptr,
                    const item_def* item = nullptr);

void describe_ability(ability_type ability);
void describe_deck(deck_type deck);
void describe_mutation(mutation_type mut);
void describe_bane(bane_type bane);
string bane_long_description(bane_type bane, bool ignore_player = false);

string short_ghost_description(const monster *mon, bool abbrev = false);
string get_ghost_description(const monster_info &mi, bool concise = false);

string get_skill_description(skill_type skill, bool need_title = false);

void describe_skill(skill_type skill);

int hex_chance(const spell_type spell, const monster_info* mon_owner,
               bool is_wand = false);
int describe_to_hit(const monster_info& mi, ostringstream &result,
                    const item_def* weapon = nullptr, bool verbose = false,
                    attack *source = nullptr, int distance = 0);

// One row of the table of attacks in a monster's description.
struct monster_attack_row
{
    string attack;      // the "Attacks" column without the "2x "
    int count;          // how many times it hits ("2x", "each")
    bool ranged;        // a Shoot or Throw row
    int max_damage;     // with effects such as berserk
    int base_damage;    // without them ("52 (base 40)")
    string damage;      // the "Max Damage" column as shown
    string bonus;       // the "Bonus" / "After Damaging Hits" column
    bool carried;       // with the weapon it carries, not wields
    int hit_chance;     // of a carried row: monster_hit_chance() with it
};
vector<monster_attack_row> monster_attack_rows(const monster_info &mi);
vector<monster_attack_row> monster_carried_weapon_rows(const monster_info &mi);
int monster_hit_chance(const monster_info &mi);

// Other parts of a monster's description, for gameio.
string player_to_hit_description(const monster_info &mi, int &chance);
int monster_notice_chance(const monster_info &mi);
vector<pair<string, int>> monster_resists_shown(const monster_info &mi);
string monster_property_description(const monster_info &mi);

// The numbers in the description of a weapon or a throwing weapon, for
// gameio. Delays in aut (the description shows tenths of turns).
struct weapon_desc_stats
{
    bool thrown;
    int base_accuracy;     // not shown for throwing weapons
    int base_damage;
    int base_delay;
    int min_delay;
    int min_delay_skill;   // the skill level min_delay is reached at
    int attack_delay;      // "Current attack delay"
    int damage_rating;
    string damage_rating_text;
};
bool player_weapon_stats(const item_def &item, weapon_desc_stats &stats);

void describe_hit_chance(int hit_chance, ostringstream &result,
                         const item_def *weapon,
                         bool verbose = false, int distance_from = 0);

string get_command_description(const command_type cmd, bool terse);

int show_description(const string &body, const tile_def *tile = nullptr);
int show_description(const describe_info &inf, const tile_def *tile = nullptr);
string process_description(const describe_info &inf, bool include_title = true);

const char* get_size_adj(const size_type size, bool ignore_medium = false);

const char* jewellery_base_ability_string(int subtype);
string artefact_inscription(const item_def& item);
void add_inscription(item_def &item, string inscrip);

string trap_name(trap_type trap);
string full_trap_name(trap_type trap);
int str_to_trap(const string &s);

string extra_cloud_info(cloud_type cloud_type);

string desc_resist(int level, int max = 1,
                   bool immune = false, bool allow_spacing = true);

string player_species_name();

/* Public for testing purposes only: do not use elsewhere. */
string _monster_habitat_description(const monster_info& mi);
