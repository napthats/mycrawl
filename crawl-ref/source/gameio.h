/**
 * @file
 * @brief Machine-readable game I/O (mycrawl): play records of games.
 *
 * Records go to <crawl_dir>/records/<name>-<start time>/:
 *   meta.json    - who/what/which seed, for analysis and replay
 *   input.jsonl  - every input event consumed by the game, plus sync markers
 *                  and anything else needed to replay the game
 *   events.jsonl - messages, notes and a snapshot of the visible state at
 *                  every command prompt
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
struct newgame_def;
struct wm_event;

namespace gameio
{
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
    // Wraps the window manager's wait_event() to record consumed input.
    int wait_event(wm_event *event, int timeout,
                   const std::function<int(wm_event *, int)> &raw_wait);
    bool filter_kbhit(bool real);
#endif

    // Bones: records which ghosts level generation loaded.
    void record_ghosts(const char *kind, const vector<ghost_demon> &ghosts);
}
