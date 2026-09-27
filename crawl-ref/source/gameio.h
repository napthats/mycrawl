/**
 * @file
 * @brief Machine-readable game I/O (mycrawl): play records of games and a
 *        file-based live API.
 *
 * Records go to <crawl_dir>/records/<name>-<start time>/:
 *   meta.json    - who/what/which seed, for analysis and replay
 *   input.jsonl  - every input event consumed by the game, plus sync markers
 *                  and anything else needed to replay the game
 *   events.jsonl - messages, notes and a snapshot of the visible state at
 *                  every command prompt
 * The live API lives in <crawl_dir>/live/: state.json is rewritten whenever
 * the game waits for input, and *.keys files dropped into inbox/ are fed to
 * the game as if typed.
 * -replay <record> plays a recorded game again: a new game with the same
 * character, seed and options, fed the recorded input. Its saves, morgue,
 * scores and bones stay in <crawl_dir>/replay/.
 * See docs/gameio.md in the mycrawl project.
 *
 * Nothing here may change the game: state is only read, and all serialisation
 * runs with the UI RNG.
 **/

#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "game-exit-type.h"

using std::string;
using std::vector;

class ghost_demon;
class Menu;
class PrecisionMenu;
struct JsonNode;
struct newgame_def;
struct wm_event;

namespace gameio
{
    // -live on the command line: the live API regardless of gameio_live.
    void force_live();

    // -replay <record directory>: returns false (and why) if it can't be
    // replayed.
    bool set_replay_source(const string &path, string &error);
    // -replay-turn <turn>: go as fast as possible up to that turn, then
    // pause.
    void set_replay_turn(int turn);
    // Called at the end of every read of the options.
    void after_options_read();
    // Recorded input is being fed to the game.
    bool replaying();
    // While a replayed game with a random seed is set up, its seed (for
    // rng::reset()); otherwise 0.
    uint64_t replay_seed();

    // What kind of input the game is waiting for, for the live API. The
    // innermost context wins.
    class context
    {
    public:
        context(const char *name, const string &text = "");
        context(const Menu *menu);
        // detail() gives more about the input, under the context's name in
        // the state (e.g. the target of the direction chooser).
        context(const char *name, std::function<JsonNode *()> detail);
        ~context();
    };
    // The text of a PrecisionMenu (e.g. the skill menu, which draws itself
    // and has no text widgets) as {"lines": [...]}, laid out as on screen.
    JsonNode *precision_menu_json(const PrecisionMenu &menu);

    // Game lifecycle. game_starting() is called just before a new game is
    // set up, game_started() once it (or a loaded game) is ready to play.
    // ng is the character choice of a new game.
    void game_starting();
    void game_started(bool new_game, const newgame_def *ng);
    void game_ended(game_exit exit, const string &message);
    // crawl is exiting (from end()): state.json says so.
    void on_exit(int exit_code, const string &message);

    // Hooks.
    void on_message(int channel, const string &text);
    void on_note(int turn, const string &place, const string &text);
    void on_command_wait();

#ifdef USE_TILE_LOCAL
    // Wraps the window manager's wait_event(): records consumed input, and
    // for the live API writes the state while idle and injects keys.
    int wait_event(wm_event *event, int timeout,
                   const std::function<int(wm_event *, int)> &raw_wait);
#endif

    // kbhit() for the places where a waiting key changes what the game does
    // (stopping travel, resting, command repetition, the ready() hook).
    // Replays have to reproduce when it was true; other kbhit() calls only
    // batch input and depend on unrecorded window events.
    bool key_interrupt();

    // Wall clock, for everything that the game reads it for. During a
    // replay, the recorded time of the latest input.
    std::chrono::system_clock::time_point clock_now();
    time_t time_now();

    // Bones: records which ghosts level generation loaded. During a
    // replay, replay_ghosts() gives the recorded ones instead and returns
    // true.
    void record_ghosts(const char *kind, const vector<ghost_demon> &ghosts);
    bool replay_ghosts(const char *kind, vector<ghost_demon> &ghosts);
}
