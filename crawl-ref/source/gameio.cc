/**
 * @file
 * @brief Machine-readable game I/O (mycrawl). See gameio.h.
 **/

#include "AppHdr.h"

#include "gameio.h"

#include <cinttypes>
#include <climits>

#include "branch.h"
#include "coordit.h"
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
#include "message.h"
#include "mon-info.h"
#include "mon-util.h"
#include "newgame-def.h"
#include "options.h"
#include "player.h"
#include "prompt.h"
#include "religion.h"
#include "showsymb.h"
#include "skills.h"
#include "species.h"
#include "spl-cast.h"
#include "spl-util.h"
#include "state.h"
#include "status.h"
#include "stringutil.h"
#include "syscalls.h"
#include "tags.h"
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
        json_append_element(a, it);
    }
    return a;
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
    int msg_seq = 0;

    // For the diffs in the command snapshots.
    string last_place;
    vector<string> last_rows;
    string last_inv, last_spells, last_skills;

    // kbhit() calls since the last recorded input, and the call at which a
    // key was first seen waiting.
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
    _add(m, "replayable", ng != nullptr);
    if (ng)
    {
        JsonNode *choice = json_mkobject();
        _add(choice, "type", (double)ng->type);
        _add(choice, "species", (double)ng->species);
        _add(choice, "job", (double)ng->job);
        _add(choice, "weapon", (double)ng->weapon);
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
    return s;
}

void game_starting()
{
    _close_records();
    if (!Options.gameio_record)
        return;
    rec.armed = true;
}

void game_started(bool new_game, const newgame_def *ng)
{
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
    if (!_recording())
        return;
    JsonNode *m = json_mkobject();
    _add(m, "t", "msg");
    _add(m, "n", (double)++rec.msg_seq);
    _add(m, "turn", (double)you.num_turns);
    _add(m, "ch", channel_to_str(channel));
    _add(m, "text", text);
    _write_event(m);
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

void on_command_wait()
{
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

    // Map rows that changed ("" for a row that was cleared); all non-empty
    // rows on arriving at a level.
    const string place = _place();
    const bool full = place != rec.last_place || rec.last_rows.empty();
    JsonNode *changed = json_mkobject();
    bool any = false;
    for (int y = 0; y < GYM; ++y)
    {
        const string prev = full ? "" : rec.last_rows[y];
        if (rows[y] == prev)
            continue;
        _add(changed, make_stringf("%d", y).c_str(), rows[y]);
        any = true;
    }
    if (full || any)
    {
        JsonNode *map = json_mkobject();
        _add(map, "full", full);
        _add(map, "rows", changed);
        _add(c, "map", map);
    }
    else
        json_delete(changed);
    rec.last_place = place;
    rec.last_rows = rows;

    _add(c, "hash", hash);
    _write_event(c);

    JsonNode *s = json_mkobject();
    _add(s, "t", "sync");
    _add(s, "n", (double)seq);
    _add(s, "turn", (double)you.num_turns);
    _add(s, "h", hash);
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
    _write_input(e);
    rec.kb_calls = 0;
    rec.kb_first = -1;
}

int wait_event(wm_event *event, int timeout,
               const function<int(wm_event *, int)> &raw_wait)
{
    // Waiting for the player: a good time to get the records onto disk.
    const bool blocking = timeout == INT_MAX;
    if (blocking)
        _flush_records();

    const int got = raw_wait(event, timeout);
    if (got)
        _record_input(*event, blocking);
    return got;
}

// Replaying has to know when a key interrupted something (e.g. travel), so
// the kbhit() call at which a waiting key was first seen is recorded.
bool filter_kbhit(bool real)
{
    if (_recording())
    {
        if (real && rec.kb_first < 0)
            rec.kb_first = rec.kb_calls;
        ++rec.kb_calls;
    }
    return real;
}
#endif

}
