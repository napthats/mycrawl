/**
 * @file
 * @brief Machine-readable game I/O (mycrawl). See gameio.h.
 **/

#include "AppHdr.h"

#include "gameio.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <climits>
#include <deque>
#include <map>
#include <thread>

#include "areas.h"
#include "branch.h"
#include "cio.h"
#include "cloud.h"
#include "coordit.h"
#include "directn.h"
#include "env.h"
#include "files.h"
#include "ghost.h"
#include "initfile.h"
#include "item-name.h"
#include "items.h"
#include "jobs.h"
#include "json.h"
#include "json-wrapper.h"
#include "libutil.h"
#include "macro.h"
#include "menu.h"
#include "message.h"
#include "mon-info.h"
#include "mon-util.h"
#include "newgame-def.h"
#include "options.h"
#include "outer-menu.h"
#include "player.h"
#include "prompt.h"
#include "quiver.h"
#include "religion.h"
#include "showsymb.h"
#include "skills.h"
#include "species.h"
#include "spl-cast.h"
#include "spl-util.h"
#include "stash.h"
#include "state.h"
#include "status.h"
#include "stringutil.h"
#include "syscalls.h"
#include "tags.h"
#include "terrain.h"
#include "ui.h"
#include "version.h"
#include "viewchar.h"
#ifdef USE_TILE_LOCAL
# include "windowmanager.h"
#endif

namespace gameio
{

static const int RECORD_FORMAT = 1;

/////////////////////////////////////////////////////////////////////////////
// Small helpers

static void _add(JsonNode *o, const char *key, const string &v)
{
    json_append_member(o, key, json_mkstring(v));
}

static void _add(JsonNode *o, const char *key, const char *v)
{
    json_append_member(o, key, json_mkstring(v));
}

static void _add(JsonNode *o, const char *key, double v)
{
    json_append_member(o, key, json_mknumber(v));
}

static void _add(JsonNode *o, const char *key, bool v)
{
    json_append_member(o, key, json_mkbool(v));
}

static void _add(JsonNode *o, const char *key, JsonNode *v)
{
    json_append_member(o, key, v);
}

static JsonNode *_pos(const coord_def &p)
{
    JsonNode *a = json_mkarray();
    json_append_element(a, json_mknumber(p.x));
    json_append_element(a, json_mknumber(p.y));
    return a;
}

static JsonNode *_strings(const vector<string> &v)
{
    JsonNode *a = json_mkarray();
    for (const string &s : v)
        json_append_element(a, json_mkstring(s));
    return a;
}

static string _encode(JsonNode *node)
{
    return JsonWrapper(node).to_string();
}

static int64_t _epoch_ms()
{
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::system_clock::now().time_since_epoch()).count();
}

static uint64_t _fnv1a(uint64_t h, const string &s)
{
    for (unsigned char c : s)
    {
        h ^= c;
        h *= 1099511628211ULL;
    }
    // separator, so that ("ab", "c") and ("a", "bc") differ
    h ^= 0xff;
    h *= 1099511628211ULL;
    return h;
}

static string _hex(const vector<unsigned char> &buf)
{
    static const char digits[] = "0123456789abcdef";
    string s;
    s.reserve(buf.size() * 2);
    for (unsigned char c : buf)
    {
        s += digits[c >> 4];
        s += digits[c & 0xf];
    }
    return s;
}

static bool _copy_file(const string &from, const string &to)
{
    FILE *in = fopen_u(from.c_str(), "rb");
    if (!in)
        return false;
    FILE *out = fopen_u(to.c_str(), "wb");
    if (!out)
    {
        fclose(in);
        return false;
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return true;
}

static string _exit_name(game_exit exit)
{
    switch (exit)
    {
    case game_exit::win:   return "win";
    case game_exit::leave: return "leave";
    case game_exit::quit:  return "quit";
    case game_exit::death: return "death";
    case game_exit::save:  return "save";
    case game_exit::abort: return "abort";
    case game_exit::crash: return "crash";
    default:               return "unknown";
    }
}

/////////////////////////////////////////////////////////////////////////////
// The visible game state. Only what the player can see on screen: the map
// as known to the player, monster_info for monsters, and item names as the
// player knows them.

static string _place()
{
    return level_id::current().describe();
}

static vector<string> _map_rows()
{
    vector<string> rows(GYM);
    for (int y = 0; y < GYM; ++y)
    {
        string &row = rows[y];
        for (int x = 0; x < GXM; ++x)
        {
            const coord_def p(x, y);
            char32_t ch = ' ';
            if (p == you.pos())
                ch = '@';
            else if (env.map_knowledge(p).known())
                ch = get_cell_glyph(p).ch;
            if (!ch)
                ch = ' ';
            row += stringize_glyph(ch);
        }
        row.erase(row.find_last_not_of(' ') + 1);
    }
    return rows;
}

// What the player knows of each cell, in the shape of _map_rows(): V in view,
// R remembered (seen before), M known without having been seen (magic
// mapping and the like), space unknown.
static vector<string> _vis_rows()
{
    vector<string> rows(GYM);
    for (int y = 0; y < GYM; ++y)
    {
        string &row = rows[y];
        for (int x = 0; x < GXM; ++x)
        {
            const coord_def p(x, y);
            const map_cell &cell = env.map_knowledge(p);
            if (you.see_cell(p))
                row += 'V';
            else if (cell.seen())
                row += 'R';
            else if (cell.known())
                row += 'M';
            else
                row += ' ';
        }
        row.erase(row.find_last_not_of(' ') + 1);
    }
    return rows;
}

static vector<string> _status_lights()
{
    vector<string> lights;
    status_info inf;
    for (unsigned i = 0; i <= STATUS_LAST_STATUS; ++i)
    {
        if (!fill_status_info(i, inf))
            continue;
        if (!inf.light_text.empty())
            lights.push_back(inf.light_text);
        else if (!inf.short_text.empty())
            lights.push_back(inf.short_text);
    }
    return lights;
}

static JsonNode *_player_json()
{
    JsonNode *p = json_mkobject();
    _add(p, "turn", (double)you.num_turns);
    _add(p, "time", (double)you.elapsed_time);
    _add(p, "place", _place());
    _add(p, "branch", branches[you.where_are_you].abbrevname);
    _add(p, "depth", (double)you.depth);
    _add(p, "pos", _pos(you.pos()));
    _add(p, "hp", (double)you.hp);
    _add(p, "hp_max", (double)you.hp_max);
    _add(p, "mp", (double)you.magic_points);
    _add(p, "mp_max", (double)you.max_magic_points);
    _add(p, "ac", (double)you.armour_class_scaled(1));
    _add(p, "ev", (double)you.evasion_scaled(1));
    _add(p, "sh", (double)player_displayed_shield_class());
    _add(p, "str", (double)you.strength(false));
    _add(p, "int", (double)you.intel(false));
    _add(p, "dex", (double)you.dex(false));
    _add(p, "xl", (double)you.experience_level);
    _add(p, "xl_progress", (double)get_exp_progress());
    _add(p, "gold", (double)you.gold);
    _add(p, "god", you_worship(GOD_NO_GOD) ? string() : god_name(you.religion));
    if (!you_worship(GOD_NO_GOD))
        _add(p, "piety_rank", (double)piety_rank());
    _add(p, "status", _strings(_status_lights()));
    // The rest of the status panel: the noise bar (0-1000, and the band its
    // colour shows), the quiver line and how long the last action took.
    const bool silence = silenced(you.pos());
    const int noise = silence ? 0 : you.get_noise_perception(true);
    _add(p, "noise", (double)noise);
    _add(p, "noise_level", silence       ? "silenced"
                           : noise <= 333 ? "quiet"
                           : noise <= 666 ? "loud"
                           : noise < 1000 ? "very loud"
                                          : "extremely loud");
    _add(p, "quiver",
         quiver::get_secondary_action()->quiver_description().tostring());
    _add(p, "last_action_time",
         (double)(you.elapsed_time - you.elapsed_time_at_last_input));
    return p;
}

static const char *_attitude_name(mon_attitude_type att)
{
    switch (att)
    {
    case ATT_HOSTILE:  return "hostile";
    case ATT_FRIENDLY: return "friendly";
    default:           return "neutral";
    }
}

static const char *_threat_name(mon_threat_level_type threat)
{
    switch (threat)
    {
    case MTHRT_TRIVIAL: return "trivial";
    case MTHRT_EASY:    return "easy";
    case MTHRT_TOUGH:   return "tough";
    case MTHRT_NASTY:   return "nasty";
    default:            return "";
    }
}

// Monsters that are shown on the map, in view or detected.
static JsonNode *_monsters_json()
{
    JsonNode *a = json_mkarray();
    for (rectangle_iterator ri(0); ri; ++ri)
    {
        const monster_info *mi = env.map_knowledge(*ri).monsterinfo();
        if (!mi)
            continue;
        JsonNode *m = json_mkobject();
        _add(m, "name", mi->full_name(DESC_PLAIN));
        _add(m, "db", mi->db_name());
        _add(m, "pos", _pos(*ri));
        _add(m, "rel", _pos(*ri - you.pos()));
        _add(m, "att", _attitude_name(mi->attitude));
        _add(m, "threat", _threat_name(mi->threat));
        _add(m, "health", get_damage_level_string(mi->holi, mi->dam));
        _add(m, "attrs", _strings(mi->attributes()));
        _add(m, "in_view", you.see_cell(*ri));
        json_append_element(a, m);
    }
    return a;
}

// Items on the floor, as remembered. Only cells in view unless all_known.
static JsonNode *_floor_items_json(bool all_known)
{
    JsonNode *a = json_mkarray();
    for (rectangle_iterator ri(0); ri; ++ri)
    {
        if (!all_known && !you.see_cell(*ri))
            continue;
        const map_cell &cell = env.map_knowledge(*ri);
        const item_def *item = cell.item();
        if (!item || !item->defined())
            continue;
        JsonNode *it = json_mkobject();
        _add(it, "name", item->name(DESC_A));
        _add(it, "pos", _pos(*ri));
        _add(it, "more", bool(cell.flags & MAP_MORE_ITEMS));
        // The whole pile, as seen when last in view (the stash tracker's
        // memory, which the stash search shows too).
        const vector<item_def> pile = item_list_in_stash(*ri);
        if (pile.size() > 1)
        {
            JsonNode *names = json_mkarray();
            for (const item_def &pitem : pile)
                json_append_element(names, json_mkstring(pitem.name(DESC_A)));
            _add(it, "pile", names);
        }
        json_append_element(a, it);
    }
    return a;
}

// Cells grouped by name, as runs: {"name": ..., <extra>, "runs": [[y, x1,
// x2], ...]} in the order the names were first seen.
class cell_runs
{
public:
    void add(const string &name, const coord_def &p, const string &glyph = "")
    {
        auto it = m_index.find(name);
        if (it == m_index.end())
        {
            it = m_index.emplace(name, m_groups.size()).first;
            m_groups.push_back({ name, glyph, {} });
        }
        vector<array<int, 3>> &runs = m_groups[it->second].runs;
        if (!runs.empty() && runs.back()[0] == p.y && runs.back()[2] == p.x - 1)
            runs.back()[2] = p.x;
        else
            runs.push_back({ p.y, p.x, p.x });
    }

    JsonNode *json() const
    {
        JsonNode *a = json_mkarray();
        for (const group &g : m_groups)
        {
            JsonNode *o = json_mkobject();
            _add(o, "name", g.name);
            if (!g.glyph.empty())
                _add(o, "glyph", g.glyph);
            JsonNode *runs = json_mkarray();
            for (const array<int, 3> &r : g.runs)
            {
                JsonNode *run = json_mkarray();
                for (int v : r)
                    json_append_element(run, json_mknumber(v));
                json_append_element(runs, run);
            }
            _add(o, "runs", runs);
            json_append_element(a, o);
        }
        return a;
    }

private:
    struct group
    {
        string name;
        string glyph;
        vector<array<int, 3>> runs;
    };
    map<string, size_t> m_index;
    vector<group> m_groups;
};

// The terrain of the level as the player knows it, named as the look
// command names it. Map glyphs don't tell everything: some features share a
// glyph (told apart by colour on screen: branch entrances and stairs, lava
// and deep water, altars of different gods, trap types), and items and
// monsters hide what they stand on. So:
//   legend: for each terrain glyph, the name most cells with it have;
//   cells: the cells that aren't what the legend says (and non-floor
//          terrain hidden under something), grouped by name.
static JsonNode *_terrain_json()
{
    struct cell_info
    {
        coord_def pos;
        string glyph;
        string name;
        bool covered;
    };
    vector<cell_info> cells;
    map<string, map<string, int>> counts; // glyph -> name -> cells
    for (rectangle_iterator ri(0); ri; ++ri)
    {
        const map_cell &mc = env.map_knowledge(*ri);
        if (!mc.known())
            continue;
        const dungeon_feature_type feat = mc.feat();
        if (feat == DNGN_UNSEEN)
            continue;
        string name;
        if (feat_is_wall(feat))
            name = "wall";
        else if (feat == DNGN_FLOOR)
            name = "floor";
        else
            name = feature_description_at(*ri, false, DESC_PLAIN);
        const string glyph = stringize_glyph(get_feat_symbol(feat));
        const bool covered = *ri == you.pos()
                             || get_cell_glyph(*ri).ch != get_feat_symbol(feat);
        cells.push_back({ *ri, glyph, name, covered });
        ++counts[glyph][name];
    }

    JsonNode *legend = json_mkobject();
    map<string, string> common;
    for (const auto &entry : counts)
    {
        const auto best = max_element(entry.second.begin(), entry.second.end(),
            [](const pair<const string, int> &a, const pair<const string, int> &b)
            { return a.second < b.second; });
        common[entry.first] = best->first;
        _add(legend, entry.first.c_str(), best->first);
    }

    cell_runs runs;
    for (const cell_info &c : cells)
    {
        if (c.name != common[c.glyph] || c.covered && c.name != "floor")
            runs.add(c.name, c.pos, c.glyph);
    }

    JsonNode *t = json_mkobject();
    _add(t, "legend", legend);
    _add(t, "cells", runs.json());
    return t;
}

// Clouds shown on the map, remembered ones marked as such.
static JsonNode *_clouds_json()
{
    cell_runs runs;
    for (rectangle_iterator ri(0); ri; ++ri)
    {
        const cloud_info *cloud = env.map_knowledge(*ri).cloudinfo();
        if (!cloud || cloud->type == CLOUD_NONE)
            continue;
        string name = cloud_type_name(cloud->type, false);
        if (!you.see_cell(*ri))
            name += " (remembered)";
        runs.add(name, *ri);
    }
    return runs.json();
}

static JsonNode *_inventory_json()
{
    JsonNode *a = json_mkarray();
    for (int i = 0; i < ENDOFPACK; ++i)
    {
        const item_def &item = you.inv[i];
        if (!item.defined())
            continue;
        JsonNode *it = json_mkobject();
        _add(it, "slot", string(1, index_to_letter(i)));
        _add(it, "name", item.name(DESC_INVENTORY_EQUIP));
        _add(it, "q", (double)item.quantity);
        _add(it, "equipped", item_is_equipped(item));
        json_append_element(a, it);
    }
    return a;
}

static JsonNode *_spells_json()
{
    JsonNode *a = json_mkarray();
    for (const spell_type spell : you.spells)
    {
        if (spell == SPELL_NO_SPELL)
            continue;
        JsonNode *s = json_mkobject();
        _add(s, "letter", string(1, (char)get_spell_letter(spell)));
        _add(s, "name", spell_title(spell));
        _add(s, "level", (double)spell_difficulty(spell));
        _add(s, "mp", (double)spell_mana(spell));
        _add(s, "fail", failure_rate_to_string(raw_spell_fail(spell)));
        json_append_element(a, s);
    }
    return a;
}

static JsonNode *_skills_json()
{
    JsonNode *o = json_mkobject();
    for (skill_type sk = SK_FIRST_SKILL; sk < NUM_SKILLS; ++sk)
    {
        const int level = you.skill(sk, 10, true);
        if (level > 0)
            _add(o, skill_name(sk), level / 10.0);
    }
    return o;
}

// How many numbers the gameplay RNG gave out in this session.
static int64_t _gameplay_draws()
{
    return (int64_t)rng::get_states()[rng::GAMEPLAY];
}

// A hash of the visible state, to check that a replay stays in sync.
static string _state_hash(const vector<string> &rows)
{
    uint64_t h = 14695981039346656037ULL;
    h = _fnv1a(h, make_stringf("%d/%d/%s/%d,%d/%d/%d/%d/%d/%d/%d",
                               you.num_turns, you.elapsed_time,
                               _place().c_str(), you.pos().x, you.pos().y,
                               you.hp, you.hp_max, you.magic_points,
                               you.experience_level, you.experience,
                               you.gold));
    for (int i = 0; i < ENDOFPACK; ++i)
        if (you.inv[i].defined())
            h = _fnv1a(h, you.inv[i].name(DESC_INVENTORY_EQUIP));
    for (rectangle_iterator ri(0); ri; ++ri)
        if (const monster_info *mi = env.map_knowledge(*ri).monsterinfo())
            h = _fnv1a(h, make_stringf("%s@%d,%d", mi->db_name().c_str(),
                                       ri->x, ri->y));
    for (const string &row : rows)
        h = _fnv1a(h, row);
    return make_stringf("%016" PRIx64, h);
}

// The known part of the level: rows from y0, each starting at x0, and the
// same part of vis (_vis_rows()).
static JsonNode *_map_json(const vector<string> &rows, const vector<string> &vis)
{
    int y0 = -1, y1 = -1;
    size_t x0 = SIZE_MAX;
    for (int y = 0; y < (int)rows.size(); ++y)
    {
        const size_t first = min(rows[y].find_first_not_of(' '),
                                 vis[y].find_first_not_of(' '));
        if (first == string::npos)
            continue;
        if (y0 < 0)
            y0 = y;
        y1 = y;
        x0 = min(x0, first);
    }
    JsonNode *m = json_mkobject();
    if (y0 < 0)
        x0 = y0 = 0;
    _add(m, "x0", (double)x0);
    _add(m, "y0", (double)y0);
    JsonNode *a = json_mkarray();
    // Every non-empty row starts with at least x0 spaces, so cutting x0
    // bytes is cutting x0 characters even with multi-byte glyphs.
    for (int y = y0; y <= y1; ++y)
        json_append_element(a, json_mkstring(rows[y].substr(min(x0, rows[y].size()))));
    _add(m, "rows", a);
    JsonNode *v = json_mkarray();
    for (int y = y0; y <= y1; ++y)
        json_append_element(v, json_mkstring(vis[y].substr(min(x0, vis[y].size()))));
    _add(m, "vis", v);
    return m;
}

/////////////////////////////////////////////////////////////////////////////
// Contexts: what kind of input the game is waiting for.

struct context_entry
{
    string name;
    string text;
    const Menu *menu;
    function<JsonNode *()> detail;
};

static vector<context_entry> contexts;

context::context(const char *name, const string &text)
{
    // A nested context of the same kind (msgwin_get_line() and the
    // cancellable_get_line() it calls) keeps the outer prompt.
    string t = text;
    if (t.empty() && !contexts.empty() && contexts.back().name == name)
        t = contexts.back().text;
    contexts.push_back({ name, t, nullptr, nullptr });
}

context::context(const Menu *menu)
{
    contexts.push_back({ "menu", "", menu , nullptr });
}

context::context(const char *name, function<JsonNode *()> detail)
{
    contexts.push_back({ name, "", nullptr, std::move(detail) });
}

context::~context()
{
    contexts.pop_back();
}

// Text of the widgets in the topmost UI layout (popups, menus, the main
// menu). The text of the button with the focus (e.g. the game mode picked
// in the main menu) goes to focus as well, and is marked with "> ".
static void _collect_text(ui::Widget *w, vector<string> &out, string &focus)
{
    if (!w || !w->is_visible())
        return;
    if (auto button = dynamic_cast<MenuButton *>(w))
    {
        vector<string> inner;
        string unused;
        button->for_each_child_and_internal(
            [&inner, &unused](shared_ptr<ui::Widget> &child) {
                _collect_text(child.get(), inner, unused);
            });
        string s = join_strings(inner.begin(), inner.end(), " ");
        if (s.empty())
            return;
        if (button->is_focused())
        {
            focus = s;
            s = "> " + s;
        }
        out.push_back(s);
        return;
    }
    if (auto text = dynamic_cast<ui::Text *>(w))
    {
        const string s = trimmed_string(text->get_text().tostring());
        if (!s.empty())
            out.push_back(s);
        return;
    }
    if (auto sw = dynamic_cast<ui::Switcher *>(w))
    {
        if (sw->num_children() > 0)
            _collect_text(sw->current_widget().get(), out, focus);
        return;
    }
    w->for_each_child_and_internal([&out, &focus](shared_ptr<ui::Widget> &child) {
        _collect_text(child.get(), out, focus);
    });
}

static void _add_context(JsonNode *st)
{
    string name;
    string text;
    const Menu *menu = nullptr;
    JsonNode *detail = nullptr;
    if (!contexts.empty())
    {
        name = contexts.back().name;
        text = contexts.back().text;
        menu = contexts.back().menu;
        // Something (e.g. an item description) is shown over the menu.
        if (menu && !menu->gameio_on_top())
        {
            name = "popup";
            menu = nullptr;
        }
        // No detail: the same.
        if (contexts.back().detail)
        {
            detail = contexts.back().detail();
            if (!detail)
                name = "popup";
        }
    }
    if (name.empty())
    {
        if (crawl_state.game_started && crawl_state.waiting_for_command)
            name = "command";
        else if (ui::has_layout())
            name = "popup";
        else if (!crawl_state.game_started)
            name = "startup";
        else
            name = "key"; // a prompt in the message area; see the messages
    }
    _add(st, "context", name);
    if (!text.empty())
        _add(st, "prompt", text);
    if (menu)
        _add(st, "menu", menu->gameio_json());
    if (detail)
        _add(st, name.c_str(), detail);
    if (ui::has_layout())
    {
        vector<string> screen;
        string focus;
        _collect_text(ui::top_layout().get(), screen, focus);
        _add(st, "screen", _strings(screen));
        if (!focus.empty())
            _add(st, "focus", focus);
    }
}

/////////////////////////////////////////////////////////////////////////////
// Live API

static const int LIVE_POLL_MS = 30;
static const size_t LIVE_MESSAGES = 50;

struct message_entry
{
    int n;
    int turn;
    string channel;
    string text;
};

struct live_state
{
    bool forced = false;
    bool dir_ready = false;
    string dir;
    deque<int> keys;
    // The state changed since state.json was last written.
    bool dirty = true;
    int seq = 0;
    int inputs = 0;
    deque<message_entry> messages;
};

static live_state live;
static int msg_seq = 0;

void force_live()
{
    live.forced = true;
}

static bool _live_enabled()
{
    return live.forced || Options.gameio_live;
}

static JsonNode *_message_json(const message_entry &msg, bool typed = true)
{
    JsonNode *m = json_mkobject();
    if (typed)
        _add(m, "t", "msg");
    _add(m, "n", (double)msg.n);
    _add(m, "turn", (double)msg.turn);
    _add(m, "ch", msg.channel);
    _add(m, "text", msg.text);
    return m;
}

static bool _prepare_live_dir()
{
    if (live.dir_ready)
        return true;
    string dir = catpath(SysEnv.crawl_dir, "live");
    string inbox = catpath(dir, "inbox");
    if (!check_mkdir("Live API directory", &dir, true)
        || !check_mkdir("Live API inbox", &inbox, true))
    {
        return false;
    }
    // Keys left over from an earlier run are stale.
    for (const string &name : get_dir_files_sorted(inbox))
        unlink_u(catpath(inbox, name).c_str());
    live.dir = dir;
    live.dir_ready = true;
    return true;
}

// Queues the keys of *.keys files in the inbox, oldest name first. Writers
// should write another name and rename it, so that no file is read half
// written.
static void _poll_inbox()
{
    const string inbox = catpath(live.dir, "inbox");
    for (const string &name : get_dir_files_sorted(inbox))
    {
        if (!ends_with(name, ".keys"))
            continue;
        const string path = catpath(inbox, name);
        FILE *f = fopen_u(path.c_str(), "rb");
        if (!f)
            continue;
        string content;
        char buf[1024];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            content.append(buf, n);
        fclose(f);
        // Only use keys that won't be read again.
        if (unlink_u(path.c_str()) != 0)
            continue;
        while (!content.empty()
               && (content.back() == '\n' || content.back() == '\r'))
        {
            content.pop_back();
        }
        for (int key : parse_keyseq(content))
            live.keys.push_back(key);
    }
}

static bool _write_state_file(JsonNode *st);

static bool _write_state()
{
    rng::generator ui_rng(rng::UI);

    JsonNode *st = json_mkobject();
    _add(st, "seq", (double)++live.seq);
    _add(st, "inputs", (double)live.inputs);
    _add(st, "w", (double)_epoch_ms());
    _add(st, "game", crawl_state.game_started);
    _add_context(st);

    JsonNode *msgs = json_mkarray();
    for (const message_entry &msg : live.messages)
        json_append_element(msgs, _message_json(msg, false));
    _add(st, "messages", msgs);

    if (crawl_state.game_started)
    {
        _add(st, "you", _player_json());
        _add(st, "mons", _monsters_json());
        _add(st, "items", _floor_items_json(true));
        _add(st, "terrain", _terrain_json());
        _add(st, "clouds", _clouds_json());
        _add(st, "inv", _inventory_json());
        _add(st, "spells", _spells_json());
        _add(st, "skills", _skills_json());
        _add(st, "map", _map_json(_map_rows(), _vis_rows()));
    }

    return _write_state_file(st);
}

// Writes st (and deletes it) as state.json, replacing it in one go.
static bool _write_state_file(JsonNode *st)
{
    char *s = json_stringify(st, " ");
    json_delete(st);

    const string path = catpath(live.dir, "state.json");
    const string tmp = path + ".tmp";
    bool ok = false;
    if (FILE *f = fopen_u(tmp.c_str(), "wb"))
    {
        ok = fputs(s, f) >= 0;
        ok = fclose(f) == 0 && ok;
    }
    free(s);
    // Fails while someone has state.json open; the caller tries again.
    return ok && rename_u(tmp.c_str(), path.c_str()) == 0;
}

// The last state.json: crawl is gone, so nobody waits for it in vain.
void on_exit(int exit_code, const string &message)
{
    if (!_live_enabled() || !live.dir_ready)
        return;
    JsonNode *st = json_mkobject();
    _add(st, "seq", (double)++live.seq);
    _add(st, "inputs", (double)live.inputs);
    _add(st, "w", (double)_epoch_ms());
    _add(st, "game", false);
    _add(st, "context", "exited");
    _add(st, "exited", true);
    _add(st, "exit_code", (double)exit_code);
    if (!message.empty())
        _add(st, "error", trimmed_string(message));
    JsonNode *msgs = json_mkarray();
    for (const message_entry &msg : live.messages)
        json_append_element(msgs, _message_json(msg, false));
    _add(st, "messages", msgs);
    // No later chance: wait out a reader that has state.json open.
    const string last = _encode(st);
    for (int tries = 0; tries < 50; ++tries)
    {
        if (_write_state_file(json_decode(last.c_str())))
            break;
        this_thread::sleep_for(chrono::milliseconds(10));
    }
}

/////////////////////////////////////////////////////////////////////////////
// Replay

enum class replay_kind { input, sync, ghosts, session, end, other };

struct replay_item
{
    replay_kind kind = replay_kind::other;
#ifdef USE_TILE_LOCAL
    wm_event ev = wm_event();
#endif
    // Consumed by a real wait for input, not by an animation's short wait.
    bool blocking = false;
    // key_interrupt() call (since the previous input) at which it was first
    // seen.
    int kb = -1;
    // key_interrupt() calls since the previous input.
    int kc = 0;
    int64_t w = 0;
    int n = 0;
    int turn = 0;
    string text; // sync: hash, ghosts: kind, session: new/load, end: exit
    string data; // ghosts: marshalled, in hex
    int64_t draws = -1; // sync: gameplay RNG draws so far in the session
    int win_w = 0, win_h = 0; // session: window size
};

// Milliseconds between inputs; +/- during the replay.
static const int REPLAY_SPEEDS[] = { 0, 10, 25, 50, 100, 200, 400, 800 };
static const int REPLAY_DEFAULT_SPEED = 4;

struct replay_state
{
    // Started with -replay (stays true after the replay has ended).
    bool mode = false;
    // Feeding recorded input to the game.
    bool active = false;
    bool finished = false;
    string source; // the record directory
    string root;   // <crawl_dir>/replay/: saves, morgue etc. of the replay

    // The character choice.
    string name;
    int type = 0, species = 0, job = 0, weapon = 0, pregen = -1;
    // The game type it was played as (a seeded game can be chosen as a
    // normal one).
    int state_type = 0;
    // The window size of the recorded session, and whether a different
    // one was reported.
    int win_w = 0, win_h = 0;
    bool win_warned = false;
    string map;
    uint64_t seed = 0;
    bool set_up = false;

    vector<replay_item> items;
    size_t pos = 0;
    int total_commands = 0;
    int commands = 0;

    bool paused = false;
    bool step = false; // run to the next command prompt, then pause
    int speed = REPLAY_DEFAULT_SPEED;
    unsigned int last_feed = 0;
    unsigned int hold_until = 0;
    int until_turn = -1;

    int kb_calls = 0;
    bool arrived = false;
    bool motion_sent = false;
    int64_t clock_ms = 0;

    int desyncs = 0;
    string status;
    FILE *log = nullptr;
};

static replay_state rp;

static double _jnum(const JsonNode *o, const char *key, double def = 0)
{
    const JsonNode *n = json_find_member(o, key);
    return n && n->tag == JSON_NUMBER ? n->number_ : def;
}

static string _jstr(const JsonNode *o, const char *key)
{
    const JsonNode *n = json_find_member(o, key);
    return n && n->tag == JSON_STRING ? n->string_ : "";
}

static bool _jbool(const JsonNode *o, const char *key)
{
    const JsonNode *n = json_find_member(o, key);
    return n && n->tag == JSON_BOOL && n->bool_;
}

static vector<string> _jstrings(const JsonNode *o, const char *key)
{
    vector<string> result;
    const JsonNode *a = json_find_member(o, key);
    if (!a || a->tag != JSON_ARRAY)
        return result;
    const JsonNode *e;
    json_foreach(e, a)
        if (e->tag == JSON_STRING)
            result.emplace_back(e->string_);
    return result;
}

static bool _read_file(const string &path, string &out)
{
    FILE *f = fopen_u(path.c_str(), "rb");
    if (!f)
        return false;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);
    fclose(f);
    return true;
}

static vector<unsigned char> _unhex(const string &s)
{
    vector<unsigned char> buf;
    auto val = [](char c) {
        return c <= '9' ? c - '0' : c - 'a' + 10;
    };
    for (size_t i = 0; i + 1 < s.size(); i += 2)
        buf.push_back(val(s[i]) << 4 | val(s[i + 1]));
    return buf;
}

static replay_item _parse_replay_item(const JsonNode *o)
{
    replay_item it;
    const string t = _jstr(o, "t");
    it.w = (int64_t)_jnum(o, "w");
    it.turn = (int)_jnum(o, "turn");
    it.n = (int)_jnum(o, "n");
    if (t == "key" || t == "mouse")
    {
        it.kind = replay_kind::input;
        it.blocking = _jbool(o, "blk");
        it.kb = (int)_jnum(o, "kb", -1);
        it.kc = (int)_jnum(o, "kc", 0);
#ifdef USE_TILE_LOCAL
        if (t == "key")
        {
            it.ev.type = WME_KEYDOWN;
            it.ev.key.keysym.sym = (int)_jnum(o, "k");
        }
        else
        {
            it.ev.type = (unsigned char)_jnum(o, "e");
            it.ev.mouse_event.event
                = (wm_mouse_event::mouse_event_type)(int)_jnum(o, "ev");
            it.ev.mouse_event.button
                = (wm_mouse_event::mouse_event_button)(int)_jnum(o, "b");
            it.ev.mouse_event.held = (unsigned short)_jnum(o, "h");
            it.ev.mouse_event.mod = (unsigned char)_jnum(o, "m");
            it.ev.mouse_event.px = (unsigned int)_jnum(o, "x");
            it.ev.mouse_event.py = (unsigned int)_jnum(o, "y");
        }
#endif
    }
    else if (t == "sync")
    {
        it.kind = replay_kind::sync;
        it.text = _jstr(o, "h");
        it.draws = (int64_t)_jnum(o, "g", -1);
    }
    else if (t == "ghosts")
    {
        it.kind = replay_kind::ghosts;
        it.text = _jstr(o, "kind");
        it.data = _jstr(o, "data");
    }
    else if (t == "session")
    {
        it.kind = replay_kind::session;
        it.text = _jstr(o, "ev");
        const JsonNode *win = json_find_member(o, "win");
        if (win && win->tag == JSON_ARRAY)
        {
            const JsonNode *w = json_find_element(win, 0);
            const JsonNode *h = json_find_element(win, 1);
            if (w && h && w->tag == JSON_NUMBER && h->tag == JSON_NUMBER)
            {
                it.win_w = (int)w->number_;
                it.win_h = (int)h->number_;
            }
        }
    }
    else if (t == "end")
    {
        it.kind = replay_kind::end;
        it.text = _jstr(o, "exit");
    }
    return it;
}

static void _remove_files_below(const string &dir)
{
    for (const string &f : get_dir_files_recursive(dir, "", -1, false))
        unlink_u(catpath(dir, f).c_str());
}

bool replaying()
{
    return rp.active;
}

uint64_t replay_seed()
{
    return rp.active && !rp.set_up && rp.state_type != GAME_TYPE_CUSTOM_SEED
           ? rp.seed : 0;
}

void set_replay_turn(int turn)
{
    rp.until_turn = turn;
}

bool set_replay_source(const string &path, string &error)
{
    if (rp.mode)
        return true; // the command line is parsed twice

    string dir = path;
    if (!dir_exists(dir))
        dir = get_parent_directory(dir);

    string text;
    if (!_read_file(catpath(dir, "meta.json"), text))
    {
        error = "no meta.json in " + dir;
        return false;
    }
    JsonWrapper meta(json_decode(text.c_str()));
    if (!meta.node || meta->tag != JSON_OBJECT)
    {
        error = "meta.json is broken";
        return false;
    }
    const JsonNode *ng = json_find_member(meta.node, "newgame");
    if (!_jbool(meta.node, "replayable") || !ng)
    {
        error = "this game wasn't recorded from its start";
        return false;
    }
    rp.name = _jstr(meta.node, "name");
    rp.seed = strtoull(_jstr(meta.node, "seed").c_str(), nullptr, 10);
    rp.type = (int)_jnum(ng, "type");
    rp.state_type = (int)_jnum(meta.node, "state_type", rp.type);
    rp.species = (int)_jnum(ng, "species");
    rp.job = (int)_jnum(ng, "job");
    rp.weapon = (int)_jnum(ng, "weapon");
    rp.map = _jstr(ng, "map");
    rp.pregen = (int)_jnum(meta.node, "pregen", -1);

    text.clear();
    if (!_read_file(catpath(dir, "input.jsonl"), text))
    {
        error = "no input.jsonl in " + dir;
        return false;
    }
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == string::npos)
            end = text.size();
        const string line = text.substr(start, end - start);
        start = end + 1;
        JsonWrapper o(json_decode(line.c_str()));
        // The last line of a crashed game may be cut short.
        if (!o.node || o->tag != JSON_OBJECT)
            continue;
        rp.items.push_back(_parse_replay_item(o.node));
        if (rp.items.back().kind == replay_kind::sync)
            ++rp.total_commands;
    }

    // Everything the replayed game writes goes to <crawl_dir>/replay/.
    rp.root = catpath(SysEnv.crawl_dir, "replay");
    string saves = catpath(rp.root, "saves");
    string morgue = catpath(rp.root, "morgue");
    if (!check_mkdir("Replay directory", &rp.root)
        || !check_mkdir("Replay saves", &saves)
        || !check_mkdir("Replay morgue", &morgue))
    {
        error = "can't create " + rp.root;
        return false;
    }
    _remove_files_below(rp.root);
    _copy_file(catpath(dir, "macro.txt"), catpath(rp.root, "macro.txt"));

    // The options of the recorded game.
    SysEnv.crawl_rc = catpath(dir, "init.txt");
    SysEnv.macro_dir = rp.root;
    SysEnv.extra_opts_first = _jstrings(meta.node, "extra_opts_first");
    SysEnv.extra_opts_last = _jstrings(meta.node, "extra_opts_last");

    rp.log = fopen_u(catpath(rp.root, "replay.log").c_str(), "wb");
    if (rp.log)
    {
        fprintf(rp.log, "Replaying %s (%s, this build %s): %d commands\n",
                dir.c_str(), _jstr(meta.node, "version").c_str(),
                Version::Long, rp.total_commands);
        fflush(rp.log);
    }

    rp.source = dir;
    rp.mode = true;
    return true;
}

void after_options_read()
{
    if (!rp.mode)
        return;

    Options.save_dir = catpath(rp.root, "saves/");
    Options.shared_dir = Options.save_dir; // scores, logfile, bones
    Options.morgue_dir = catpath(rp.root, "morgue/");
    Options.gameio_record = false;
    // Saving in the recorded game saved and loaded it again: so does the
    // replay.
    Options.restart_after_game = true;
    Options.restart_after_save = true;

    if (rp.finished)
        return;
    Options.name_bypasses_menu = true;
    Options.game.name = rp.name;
    // A custom seed game is started as a normal game with a seed, which
    // makes it a custom seed one (choosing a custom seed game would ask for
    // the seed). Any other game gets its seed from replay_seed(), since a
    // seed in the options would turn it into a custom seed game, and those
    // play differently (e.g. the welcome message draws no random number).
    const bool custom_seed = rp.state_type == GAME_TYPE_CUSTOM_SEED;
    Options.game.type = custom_seed ? GAME_TYPE_NORMAL : (game_type)rp.type;
    Options.game.species = (species_type)rp.species;
    Options.game.job = (job_type)rp.job;
    Options.game.weapon = (weapon_type)rp.weapon;
    Options.game.map = rp.map;
    Options.game.fully_random = false;
    Options.seed = Options.seed_from_rc = custom_seed ? rp.seed : 0;
    if (!rp.set_up && rp.pregen >= 0)
        Options.pregen_dungeon = (level_gen_type)rp.pregen;
}

static void _replay_log(const string &what)
{
    if (!rp.log)
        return;
    fprintf(rp.log, "[cmd %d, turn %d] %s\n", rp.commands,
            crawl_state.game_started ? you.num_turns : 0, what.c_str());
    fflush(rp.log);
}

static const char *_kind_name(replay_kind kind)
{
    switch (kind)
    {
    case replay_kind::input:   return "input";
    case replay_kind::sync:    return "a command prompt";
    case replay_kind::ghosts:  return "loading ghosts";
    case replay_kind::session: return "a session start";
    case replay_kind::end:     return "a game end";
    default:                   return "something else";
    }
}

static void _replay_title()
{
#ifdef USE_TILE_LOCAL
    if (!wm)
        return;
    string title;
    if (rp.active)
    {
        title = make_stringf("Replay %s | command %d/%d | %s | ",
                             rp.name.c_str(), rp.commands, rp.total_commands,
                             rp.paused ? "PAUSED"
                             : make_stringf("%dms/key",
                                            REPLAY_SPEEDS[rp.speed]).c_str());
        if (rp.desyncs)
            title += make_stringf("%d desync(s), see replay.log | ", rp.desyncs);
        title += "space: pause, +/-: speed, .: step, q: stop";
    }
    else
    {
        title = string(CRAWL " ") + Version::Long;
        if (!rp.status.empty())
            title += " | " + rp.status;
    }
    wm->set_window_title(title.c_str());
#endif
}

static void _replay_desync(const string &what)
{
    ++rp.desyncs;
    rp.paused = true;
    rp.step = false;
    _replay_log("DESYNC: " + what);
    _replay_title();
}

static void _replay_stop(const string &why)
{
    rp.active = false;
    rp.finished = true;
    rp.status = why;
    // From here on it's an ordinary game: no more automatic new games.
    Options.name_bypasses_menu = false;
    _replay_log(why);
    _replay_title();
}

// Moves past the next item of the given kind (reporting whatever was
// skipped). Returns it, or nullptr if there's none.
static const replay_item *_replay_take(replay_kind kind, const string &text = "")
{
    size_t i = rp.pos;
    while (i < rp.items.size()
           && (rp.items[i].kind != kind
               || !text.empty() && rp.items[i].text != text))
    {
        ++i;
    }
    if (i >= rp.items.size())
    {
        _replay_desync(make_stringf("expected %s, but the record has no more",
                                    _kind_name(kind)));
        return nullptr;
    }
    if (i != rp.pos)
    {
        _replay_desync(make_stringf("expected %s, but the record has %s",
                                    _kind_name(kind),
                                    _kind_name(rp.items[rp.pos].kind)));
    }
    rp.pos = i + 1;
    return &rp.items[i];
}

static bool _replay_fast_forward()
{
    return rp.until_turn >= 0
           && (!crawl_state.game_started || you.num_turns < rp.until_turn);
}

static void _replay_note_window(const replay_item &session)
{
    rp.win_w = session.win_w;
    rp.win_h = session.win_h;
}

static void _replay_game_starting()
{
    if (rp.finished)
        return;
    rp.active = true;
    rp.kb_calls = 0;
    rp.arrived = false;
    if (const replay_item *it = _replay_take(replay_kind::session, "new"))
    {
        rp.clock_ms = it->w;
        _replay_note_window(*it);
    }
#ifdef USE_TILE_LOCAL
    if (wm)
        rp.hold_until = wm->get_ticks() + 1500;
#endif
    _replay_log("new game");
    _replay_title();
}

static void _replay_game_loaded()
{
    if (!rp.active)
        return;
    rp.kb_calls = 0;
    rp.arrived = false;
    if (const replay_item *it = _replay_take(replay_kind::session, "load"))
    {
        rp.clock_ms = it->w;
        _replay_note_window(*it);
    }
    _replay_log("game loaded");
}

static void _replay_game_ended(game_exit exit)
{
    if (!rp.active)
        return;
    _replay_take(replay_kind::end, _exit_name(exit));
    _replay_log("game ended: " + _exit_name(exit));
    if (rp.pos >= rp.items.size())
        _replay_stop("replay finished");
}

static void _replay_command_wait()
{
    if (rp.pos < rp.items.size() && rp.items[rp.pos].kind == replay_kind::sync)
    {
        const replay_item &it = rp.items[rp.pos++];
        rp.commands = it.n;
        rng::generator ui_rng(rng::UI);
        const vector<string> rows = _map_rows();
        if (it.draws >= 0 && _gameplay_draws() != it.draws)
        {
            _replay_log(make_stringf("gameplay RNG draws: %" PRId64
                                     " in the record, %" PRId64 " now",
                                     it.draws, _gameplay_draws()));
        }
        if (_state_hash(rows) != it.text)
        {
            _replay_desync(make_stringf("the state differs from the record "
                                        "(recorded turn %d)", it.turn));
            // What the replay has, to compare with events.jsonl.
            if (rp.log)
            {
                JsonNode *s = json_mkobject();
                _add(s, "you", _player_json());
                _add(s, "mons", _monsters_json());
                _add(s, "inv", _inventory_json());
                _add(s, "map", _map_json(rows, _vis_rows()));
                fprintf(rp.log, "%s\n", _encode(s).c_str());
                fflush(rp.log);
            }
        }
    }
    else
    {
        _replay_desync(make_stringf("a command prompt where the record has %s",
                                    rp.pos < rp.items.size()
                                    ? _kind_name(rp.items[rp.pos].kind)
                                    : "nothing more"));
    }

    if (rp.step)
    {
        rp.step = false;
        rp.paused = true;
    }
    if (rp.until_turn >= 0 && you.num_turns >= rp.until_turn)
    {
        rp.until_turn = -1;
        rp.paused = true;
    }
    _replay_title();
}

bool replay_ghosts(const char *kind, vector<ghost_demon> &ghosts)
{
    if (!rp.active)
        return false;
    ghosts.clear();
    // Never the bones files: whatever the recorded game found.
    if (rp.pos >= rp.items.size() || rp.items[rp.pos].kind != replay_kind::ghosts
        || rp.items[rp.pos].text != kind)
    {
        _replay_desync(make_stringf("loading %s ghosts where the record has %s",
                                    kind, rp.pos < rp.items.size()
                                    ? _kind_name(rp.items[rp.pos].kind)
                                    : "nothing more"));
        return true;
    }
    const replay_item &it = rp.items[rp.pos++];
    if (!it.data.empty())
    {
        try
        {
            reader r(_unhex(it.data), TAG_MINOR_VERSION);
            ghosts = tag_read_ghosts(r);
        }
        catch (...)
        {
            _replay_desync("broken ghost data");
        }
    }
    return true;
}

chrono::system_clock::time_point clock_now()
{
    if (rp.active && rp.clock_ms)
        return chrono::system_clock::time_point(chrono::milliseconds(rp.clock_ms));
    return chrono::system_clock::now();
}

time_t time_now()
{
    return chrono::system_clock::to_time_t(clock_now());
}

/////////////////////////////////////////////////////////////////////////////
// Play records

struct record_state
{
    // Recording the game in progress.
    bool active = false;
    // A new game is being set up: collect lines until the files are opened.
    bool armed = false;
    string dir;
    FILE *input = nullptr;
    FILE *events = nullptr;
    vector<string> pending_input;
    vector<string> pending_events;

    int cmd_seq = 0;
    // Options.pregen_dungeon when the game was set up.
    int pregen = -1;

    // For the diffs in the command snapshots.
    string last_place;
    vector<string> last_rows;
    vector<string> last_vis;
    string last_inv, last_spells, last_skills, last_terrain, last_clouds;

    // key_interrupt() calls since the last recorded input, and the call at
    // which a key was first seen waiting.
    int kb_calls = 0;
    int kb_first = -1;
};

static record_state rec;

static bool _recording()
{
    return rec.active || rec.armed;
}

static void _write(FILE *f, vector<string> &pending, JsonNode *node)
{
    const string line = _encode(node);
    if (f)
        fprintf(f, "%s\n", line.c_str());
    else
        pending.push_back(line);
}

static void _write_input(JsonNode *node)
{
    _write(rec.input, rec.pending_input, node);
}

static void _write_event(JsonNode *node)
{
    _write(rec.events, rec.pending_events, node);
}

// Adds node to o unless it encodes to the same as last time.
static void _add_if_changed(JsonNode *o, const char *key, JsonNode *node,
                            string &last)
{
    char *s = json_stringify(node, nullptr);
    string encoded = s ? s : "";
    free(s);
    if (encoded == last)
    {
        json_delete(node);
        return;
    }
    _add(o, key, node);
    last = std::move(encoded);
}

static void _flush_records()
{
    if (rec.input)
        fflush(rec.input);
    if (rec.events)
        fflush(rec.events);
}

static void _close_records()
{
    if (rec.input)
        fclose(rec.input);
    if (rec.events)
        fclose(rec.events);
    rec = record_state();
}

static string _records_root()
{
    return catpath(SysEnv.crawl_dir, "records");
}

static string _record_dir_for_current_game()
{
    const string name = strip_filename_unsafe_chars(you.your_name) + "-"
                        + make_file_time(you.birth_time);
    return catpath(_records_root(), name);
}

static void _write_meta(const string &dir, const newgame_def *ng)
{
    JsonNode *m = json_mkobject();
    _add(m, "format", (double)RECORD_FORMAT);
    _add(m, "version", Version::Long);
    _add(m, "name", you.your_name);
    _add(m, "species", species::name(you.species));
    _add(m, "species_abbrev", species::get_abbrev(you.species));
    _add(m, "job", get_job_name(you.char_class));
    _add(m, "job_abbrev", get_job_abbrev(you.char_class));
    _add(m, "birth_time", make_file_time(you.birth_time));
    // Strings, since JSON numbers can't hold all 64-bit values.
    _add(m, "seed", make_stringf("%" PRIu64, you.game_seed));
    _add(m, "game_type", gametype_to_str(crawl_state.type));
    _add(m, "state_type", (double)crawl_state.type);
    _add(m, "replayable", ng != nullptr);
    if (rec.pregen >= 0)
        _add(m, "pregen", (double)rec.pregen);
    if (ng)
    {
        JsonNode *choice = json_mkobject();
        _add(choice, "type", (double)ng->type);
        _add(choice, "species", (double)ng->species);
        _add(choice, "job", (double)ng->job);
        _add(choice, "weapon", (double)ng->weapon);
        _add(choice, "map", ng->map);
        _add(m, "newgame", choice);
    }
    _add(m, "rc_file", Options.filename);
    _add(m, "extra_opts_first", _strings(SysEnv.extra_opts_first));
    _add(m, "extra_opts_last", _strings(SysEnv.extra_opts_last));

    FILE *f = fopen_u(catpath(dir, "meta.json").c_str(), "wb");
    if (!f)
        return;
    char *s = json_stringify(m, "  ");
    fprintf(f, "%s\n", s);
    free(s);
    fclose(f);
    json_delete(m);
}

static JsonNode *_session_json(const char *what)
{
    JsonNode *s = json_mkobject();
    _add(s, "t", "session");
    _add(s, "ev", what);
    _add(s, "w", (double)_epoch_ms());
    _add(s, "version", Version::Long);
    _add(s, "turn", (double)you.num_turns);
    _add(s, "time", (double)you.elapsed_time);
    _add(s, "g", (double)_gameplay_draws());
#ifdef USE_TILE_LOCAL
    if (wm)
    {
        JsonNode *win = json_mkarray();
        json_append_element(win, json_mknumber(wm->screen_width()));
        json_append_element(win, json_mknumber(wm->screen_height()));
        _add(s, "win", win);
    }
#endif
    return s;
}

void game_starting()
{
    if (rp.mode)
    {
        _replay_game_starting();
        return;
    }
    _close_records();
    if (!Options.gameio_record)
        return;
    rec.armed = true;
    rec.pregen = (int)Options.pregen_dungeon;
}

void game_started(bool new_game, const newgame_def *ng)
{
    if (rp.mode)
    {
        if (new_game)
            rp.set_up = true;
        else
            _replay_game_loaded();
        return;
    }
    if (!Options.gameio_record)
    {
        _close_records();
        return;
    }
    if (!new_game)
        _close_records(); // nothing from the loading is kept

    string dir = _record_dir_for_current_game();
    if (!check_mkdir("Record directory", &dir))
    {
        _close_records();
        return;
    }

    if (new_game || !file_exists(catpath(dir, "meta.json")))
    {
        // A loaded game that wasn't recorded from the start can still be
        // analysed, but not replayed.
        _write_meta(dir, new_game ? ng : nullptr);
        if (new_game)
        {
            _copy_file(Options.filename, catpath(dir, "init.txt"));
            _copy_file(catpath(Options.macro_dir, "macro.txt"),
                       catpath(dir, "macro.txt"));
        }
    }

    // Commands are numbered through all sessions of the game.
    string old_input;
    if (!new_game && _read_file(catpath(dir, "input.jsonl"), old_input))
    {
        for (size_t p = old_input.find("\"t\":\"sync\""); p != string::npos;
             p = old_input.find("\"t\":\"sync\"", p + 1))
        {
            ++rec.cmd_seq;
        }
    }

    rec.dir = dir;
    rec.input = fopen_u(catpath(dir, "input.jsonl").c_str(), "ab");
    rec.events = fopen_u(catpath(dir, "events.jsonl").c_str(), "ab");
    if (!rec.input || !rec.events)
    {
        mprf(MSGCH_ERROR, "Couldn't open the play record in %s", dir.c_str());
        _close_records();
        return;
    }

    const char *what = new_game ? "new" : "load";
    // The session line comes first, then whatever was collected while the
    // game was being set up.
    fprintf(rec.input, "%s\n", _encode(_session_json(what)).c_str());
    for (const string &line : rec.pending_input)
        fprintf(rec.input, "%s\n", line.c_str());
    fprintf(rec.events, "%s\n", _encode(_session_json(what)).c_str());
    for (const string &line : rec.pending_events)
        fprintf(rec.events, "%s\n", line.c_str());
    rec.pending_input.clear();
    rec.pending_events.clear();

    rec.armed = false;
    rec.active = true;
    _flush_records();
}

void game_ended(game_exit exit, const string &message)
{
    if (rp.mode)
    {
        _replay_game_ended(exit);
        return;
    }
    if (rec.active)
    {
        rng::generator ui_rng(rng::UI);
        for (int i = 0; i < 2; ++i)
        {
            JsonNode *e = json_mkobject();
            _add(e, "t", "end");
            _add(e, "exit", _exit_name(exit));
            _add(e, "w", (double)_epoch_ms());
            _add(e, "turn", (double)you.num_turns);
            _add(e, "msg", message);
            if (i == 0)
                _write_input(e);
            else
                _write_event(e);
        }
    }
    _close_records();
}

void on_message(int channel, const string &text)
{
    const message_entry msg = { ++msg_seq, you.num_turns,
                                channel_to_str(channel), text };
    if (_live_enabled())
    {
        live.messages.push_back(msg);
        if (live.messages.size() > LIVE_MESSAGES)
            live.messages.pop_front();
        live.dirty = true;
    }
    if (_recording())
        _write_event(_message_json(msg));
}

void on_note(int turn, const string &place, const string &text)
{
    if (!_recording())
        return;
    JsonNode *n = json_mkobject();
    _add(n, "t", "note");
    _add(n, "turn", (double)turn);
    _add(n, "place", place);
    _add(n, "text", text);
    _write_event(n);
}

// {"y": row} for the rows that differ from prev (empty: all rows differ), or
// nullptr if none do.
static JsonNode *_changed_rows(const vector<string> &rows,
                               const vector<string> &prev)
{
    JsonNode *changed = nullptr;
    for (int y = 0; y < (int)rows.size(); ++y)
    {
        if (rows[y] == (prev.empty() ? string() : prev[y]))
            continue;
        if (!changed)
            changed = json_mkobject();
        _add(changed, make_stringf("%d", y).c_str(), rows[y]);
    }
    return changed;
}

void on_command_wait()
{
    if (rp.active)
    {
        _replay_command_wait();
        return;
    }
    if (!rec.active)
        return;
    rng::generator ui_rng(rng::UI);

    const int seq = ++rec.cmd_seq;
    const vector<string> rows = _map_rows();
    const string hash = _state_hash(rows);

    JsonNode *c = json_mkobject();
    _add(c, "t", "cmd");
    _add(c, "n", (double)seq);
    _add(c, "you", _player_json());
    _add(c, "mons", _monsters_json());
    _add(c, "items", _floor_items_json(false));

    // Inventory, spells and skills only when they changed.
    _add_if_changed(c, "inv", _inventory_json(), rec.last_inv);
    _add_if_changed(c, "spells", _spells_json(), rec.last_spells);
    _add_if_changed(c, "skills", _skills_json(), rec.last_skills);
    _add_if_changed(c, "terrain", _terrain_json(), rec.last_terrain);
    _add_if_changed(c, "clouds", _clouds_json(), rec.last_clouds);

    // Map (and visibility) rows that changed ("" for a row that was
    // cleared); all non-empty rows on arriving at a level.
    const string place = _place();
    const bool full = place != rec.last_place || rec.last_rows.empty();
    const vector<string> vis = _vis_rows();
    JsonNode *changed = _changed_rows(rows, full ? vector<string>() : rec.last_rows);
    JsonNode *vis_changed = _changed_rows(vis, full ? vector<string>() : rec.last_vis);
    if (full || changed || vis_changed)
    {
        JsonNode *map = json_mkobject();
        _add(map, "full", full);
        _add(map, "rows", changed ? changed : json_mkobject());
        _add(map, "vis", vis_changed ? vis_changed : json_mkobject());
        _add(c, "map", map);
    }
    rec.last_place = place;
    rec.last_rows = rows;
    rec.last_vis = vis;

    _add(c, "hash", hash);
    _write_event(c);

    JsonNode *s = json_mkobject();
    _add(s, "t", "sync");
    _add(s, "n", (double)seq);
    _add(s, "turn", (double)you.num_turns);
    _add(s, "h", hash);
    // Not visible in the game, but it shows a replay going wrong before
    // anything visible does.
    _add(s, "g", (double)_gameplay_draws());
    _write_input(s);
}

void record_ghosts(const char *kind, const vector<ghost_demon> &ghosts)
{
    if (!_recording())
        return;
    JsonNode *g = json_mkobject();
    _add(g, "t", "ghosts");
    _add(g, "kind", kind);
    _add(g, "n", (double)ghosts.size());
    if (!ghosts.empty())
    {
        vector<unsigned char> buf;
        writer w(&buf);
        tag_write_ghosts(w, ghosts);
        _add(g, "data", _hex(buf));
    }
    _write_input(g);
}

/////////////////////////////////////////////////////////////////////////////
// Input

#ifdef USE_TILE_LOCAL
static JsonNode *_input_json(const wm_event &ev)
{
    JsonNode *e = json_mkobject();
    switch (ev.type)
    {
    case WME_KEYDOWN:
        _add(e, "t", "key");
        // Modifiers are part of the key code; key_mod isn't used (and isn't
        // set for text input).
        _add(e, "k", (double)ev.key.keysym.sym);
        break;
    case WME_MOUSEBUTTONDOWN:
    case WME_MOUSEBUTTONUP:
    case WME_MOUSEWHEEL:
        _add(e, "t", "mouse");
        _add(e, "e", (double)ev.type);
        _add(e, "ev", (double)ev.mouse_event.event);
        _add(e, "b", (double)ev.mouse_event.button);
        _add(e, "h", (double)ev.mouse_event.held);
        _add(e, "m", (double)ev.mouse_event.mod);
        _add(e, "x", (double)ev.mouse_event.px);
        _add(e, "y", (double)ev.mouse_event.py);
        break;
    default:
        json_delete(e);
        return nullptr;
    }
    return e;
}

static void _record_input(const wm_event &ev, bool blocking)
{
    if (!_recording())
        return;
    JsonNode *e = _input_json(ev);
    if (!e)
        return;
    _add(e, "w", (double)_epoch_ms());
    if (blocking)
        _add(e, "blk", true);
    if (rec.kb_first >= 0)
        _add(e, "kb", (double)rec.kb_first);
    if (rec.kb_calls)
        _add(e, "kc", (double)rec.kb_calls);
    _write_input(e);
    rec.kb_calls = 0;
    rec.kb_first = -1;
}

static void _consumed(const wm_event &ev, bool blocking)
{
    _record_input(ev, blocking);
    if (ev.type == WME_KEYDOWN || ev.type == WME_MOUSEBUTTONDOWN
        || ev.type == WME_MOUSEBUTTONUP || ev.type == WME_MOUSEWHEEL)
    {
        ++live.inputs;
        live.dirty = true;
    }
}

static bool _is_user_input(const wm_event &ev)
{
    return ev.type == WME_KEYDOWN || ev.type == WME_MOUSEBUTTONDOWN
           || ev.type == WME_MOUSEBUTTONUP || ev.type == WME_MOUSEWHEEL;
}

// Keys pressed during a replay control it.
static void _replay_control(int key)
{
    switch (key)
    {
    case ' ':
        rp.paused = !rp.paused;
        rp.step = false;
        break;
    case '+':
    case '=':
        rp.speed = max(0, rp.speed - 1);
        break;
    case '-':
        rp.speed = min((int)ARRAYSZ(REPLAY_SPEEDS) - 1, rp.speed + 1);
        break;
    case '.':
        rp.paused = false;
        rp.step = true;
        break;
    case 'q':
    case CK_ESCAPE:
        _replay_stop("replay stopped: you have control");
        return;
    default:
        return;
    }
    _replay_title();
}

// Whether the next recorded input may be given to this wait.
static bool _replay_ready(const replay_item &it, bool blocking)
{
    if (!blocking && it.blocking)
        return false;
    // key_interrupt() already said it's there.
    if (rp.arrived)
        return true;
    if (rp.paused)
        return false;
    // Let the new window settle (resize and lay out) first: mouse input is
    // in window coordinates.
    if (wm->get_ticks() < rp.hold_until)
        return false;
    if (_replay_fast_forward())
        return true;
    return wm->get_ticks() - rp.last_feed
           >= (unsigned int)REPLAY_SPEEDS[rp.speed];
}

static int _replay_wait(wm_event *event, int timeout,
                        const function<int(wm_event *, int)> &raw_wait)
{
    const bool blocking = timeout == INT_MAX;
    const unsigned int start = wm->get_ticks();
    while (rp.active)
    {
        // Real events first, so that e.g. a resize is handled before
        // recorded input that depends on the layout.
        if (raw_wait(event, 0))
        {
            if (!_is_user_input(*event))
                return 1; // window events, timers, mouse motion
            if (event->type == WME_KEYDOWN)
                _replay_control(event->key.keysym.sym);
            continue;
        }

        if (rp.pos >= rp.items.size())
        {
            _replay_stop("replay finished: you have control");
            break;
        }
        const replay_item &it = rp.items[rp.pos];
        if (it.kind == replay_kind::input)
        {
            if (_replay_ready(it, blocking))
            {
                // Hover first, as the mouse would have moved there.
                if (it.ev.type == WME_MOUSEBUTTONDOWN && !rp.motion_sent)
                {
                    *event = wm_event();
                    event->type = WME_MOUSEMOTION;
                    event->mouse_event = it.ev.mouse_event;
                    event->mouse_event.event = wm_mouse_event::MOVE;
                    event->mouse_event.button = wm_mouse_event::NONE;
                    event->mouse_event.held = 0; // not a drag
                    rp.motion_sent = true;
                    return 1;
                }
                // Mouse input is in window coordinates, so it only replays well
                // in a window of the same size.
                if (it.ev.type != WME_KEYDOWN && rp.win_w && !rp.win_warned
                    && (rp.win_w != wm->screen_width()
                        || rp.win_h != wm->screen_height()))
                {
                    rp.win_warned = true;
                    _replay_log(make_stringf("the window is %dx%d, but was "
                                             "%dx%d: mouse input may land "
                                             "elsewhere",
                                             wm->screen_width(),
                                             wm->screen_height(),
                                             rp.win_w, rp.win_h));
                }
                if (rp.kb_calls != it.kc)
                {
                    _replay_log(make_stringf("key_interrupt() calls before input %d: "
                                             "%d in the record, %d now",
                                             (int)rp.pos, it.kc, rp.kb_calls));
                }
                *event = it.ev;
                ++rp.pos;
                ++live.inputs;
                live.dirty = true;
                rp.kb_calls = 0;
                rp.arrived = false;
                rp.motion_sent = false;
                rp.clock_ms = it.w;
                rp.last_feed = wm->get_ticks();
                return 1;
            }
        }
        else if (blocking && crawl_state.game_started
                 && (it.kind == replay_kind::sync
                     || it.kind == replay_kind::ghosts))
        {
            // Waiting for input where the record went on without any.
            _replay_desync(make_stringf("waiting for input where the record "
                                        "has %s", _kind_name(it.kind)));
            ++rp.pos;
            continue;
        }

        // The live API can watch a replay.
        if (blocking && _live_enabled() && live.dirty && _prepare_live_dir()
            && _write_state())
        {
            live.dirty = false;
        }

        const unsigned int elapsed = wm->get_ticks() - start;
        if (!blocking && elapsed >= (unsigned int)timeout)
            return 0;
        // Wait a little for real events (handled at the top of the loop).
        const int slice = blocking ? 10 : min(10, timeout - (int)elapsed);
        wm->delay(slice);
    }
    return wait_event(event, timeout, raw_wait);
}

int wait_event(wm_event *event, int timeout,
               const function<int(wm_event *, int)> &raw_wait)
{
    if (rp.active)
        return _replay_wait(event, timeout, raw_wait);

    // Waiting for the player: a good time to get the records onto disk.
    const bool blocking = timeout == INT_MAX;
    if (blocking)
        _flush_records();

    // Live keys are only given to a real wait for input, not to the short
    // waits of animations, which is also what typing ahead does.
    if (!blocking || !_live_enabled() || !_prepare_live_dir())
    {
        const int got = raw_wait(event, timeout);
        if (got)
            _consumed(*event, blocking);
        return got;
    }

    while (true)
    {
        _poll_inbox();
        if (!live.keys.empty())
        {
            *event = wm_event();
            event->type = WME_KEYDOWN;
            event->key.keysym.sym = live.keys.front();
            live.keys.pop_front();
            _consumed(*event, blocking);
            return 1;
        }

        // Idle: tell the other side what we're waiting for.
        if (live.dirty && _write_state())
            live.dirty = false;

        const int got = raw_wait(event, LIVE_POLL_MS);
        if (got)
        {
            _consumed(*event, blocking);
            return got;
        }
    }
}

#endif

// Replaying has to know when a key interrupted something (e.g. travel), so
// the key_interrupt() call at which a waiting key was first seen is
// recorded.
bool key_interrupt()
{
#ifndef USE_TILE_LOCAL
    return kbhit();
#else
    // (During a replay, kbhit() is false: keys pressed then control it.)
    const bool real = kbhit();
    if (rp.active)
    {
        // Keys pressed now are for controlling the replay; what the game
        // sees is whether the recorded game saw a key here. That key is the
        // next input, which may come after sync markers (e.g. travel stops,
        // then the key is read at the command prompt).
        size_t next = rp.pos;
        while (next < rp.items.size()
               && (rp.items[next].kind == replay_kind::sync
                   || rp.items[next].kind == replay_kind::ghosts))
        {
            ++next;
        }
        if (next < rp.items.size() && rp.items[next].kb == rp.kb_calls)
            rp.arrived = true;
        ++rp.kb_calls;
        return rp.arrived;
    }

    if (_recording())
    {
        if (real && rec.kb_first < 0)
            rec.kb_first = rec.kb_calls;
        ++rec.kb_calls;
    }
    return real;
#endif
}

}
