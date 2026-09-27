/**
 * @file
 * @brief mycrawl: where local builds keep their saves.
**/

#pragma once

// Root directory for saves, relative to the directory containing the
// executable (crawl-ref/source/ in a source checkout, so this points at the
// saves/ folder next to the repository).
#define MYCRAWL_SAVE_ROOT "../../../saves/"

// Subdirectory of MYCRAWL_SAVE_ROOT used by this build. Change this whenever
// a change breaks save compatibility, so that older saves stay in their own
// directory and aren't seen by newer builds.
#define MYCRAWL_SAVE_GENERATION "0.34"
