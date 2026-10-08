#!/bin/bash
# Run batched objstat for the Rapid Descent loot experiments (mycrawl).
#
# usage: run_objstat.sh <crawl-dir> <out-dir> <-descent|-rapid-descent> \
#                       <iters-per-batch> <batches> [range]
#
# <crawl-dir> holds a crawl binary built with
#   make EXTERNAL_DEFINES=-DDEBUG_STATISTICS
# next to a dat/ directory from the same tree (copy both, so later edits to
# the tree don't leak into a running measurement), and a des cache made with
#   <crawl-dir>/crawl -dir <crawl-dir>/db -builddb
# The experimental flags are taken from $RAPID_DESCENT_LOOT, e.g.
#   RAPID_DESCENT_LOOT=shops,portals run_objstat.sh ...
# Each batch writes objstat_*.tsv to <out-dir>/b<N>; analyze.py reads them.
set -e
CRAWL=$(cd "$1" && pwd); OUT=$2; MODE=$3; IT=$4; NB=$5
R=${6:-D,Lair,Orc,Swamp,Shoals,Snake,Spider,Elf,Crypt,Vaults,Slime,Depths,Zot}
mkdir -p "$OUT"
echo "RAPID_DESCENT_LOOT=$RAPID_DESCENT_LOOT mode=$MODE iters=$IT batches=$NB range=$R" \
    > "$OUT/cmd.txt"
for b in $(seq 1 "$NB"); do
    mkdir -p "$OUT/b$b"
    (cd "$OUT/b$b" && "$CRAWL/crawl" -dir "$CRAWL/db" "$MODE" -objstat "$R" \
        -iters "$IT" > out.txt 2>&1)
    # objstat aborts the whole run on a failed level and still exits 0.
    if [ ! -f "$OUT/b$b/objstat_Info.tsv" ]; then
        echo "batch $b failed, see $OUT/b$b/out.txt" >&2
    fi
done
