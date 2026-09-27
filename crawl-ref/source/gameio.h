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
 * See docs/gameio.md in the mycrawl project.
 *
 * Nothing here may change the game: state is only read, and all serialisation
 * runs with the UI RNG.
 **/

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "game-exit-type.h"

using std::string;
using std::vector;

class ghost_demon;
class Menu;
struct newgame_def;
struct wm_event;

namespace gameio
{
    // -live on the command line: the live API regardless of gameio_live.
    void force_live();

    // What kind of input the game is waiting for, for the live API. The
    // innermost context wins.
    class context
    {
    public:
        context(const char *name, const string &text = "");
        context(const Menu *menu);
        ~context();
    };

    // Game lifecycle. game_starting() is called just before a new game is
    // set up, game_started() once it (or a loaded game) is ready to play.
    // ng is the character choice of a new game.
    void game_starting();
    void game_started(bool new_game, const newgame_def *ng);
    void game_ended(game_exit exit, const string &message);

    // Hooks.
    void on_message(int channel, const string &text);
    void on_note(int turn, const string &place, const string &text);
    void on_command_wait();

#ifdef USE_TILE_LOCAL
    // Wraps the window manager's wait_event(): records consumed input, and
    // for the live API writes the state while idle and injects keys.
    int wait_event(wm_event *event, int timeout,
                   const std::function<int(wm_event *, int)> &raw_wait);
    bool filter_kbhit(bool real);
#endif

    // Bones: records which ghosts level generation loaded.
    void record_ghosts(const char *kind, const vector<ghost_demon> &ghosts);
}
