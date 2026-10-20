#!/bin/bash
# What compiling one program costs today: heap allocations, the compiler's own spaces, time.
#   bash docs/design/graph-model/tools/compile_cost.sh FILE.cml [more files...]
# For each file, with the graph dumped and thrown away (`std::rgir > /dev/null`, nothing runs):
#   malloc calls / bytes / peak (malloc_count.c), graph / perm / meta space use (CAMEL_GC_STATS),
#   graphs / nodes / edges (std::stats), and wall time of the best of 5 runs.
# With GM_PASSES="std::opt::inline" (any pipeline) the same after those passes.
set -u
here=$(cd "$(dirname "$0")" && pwd)
so=/tmp/gm_malloc_count_$$.so
clang -O2 -shared -fPIC -o "$so" "$here/malloc_count.c" -ldl || exit 1
passes=${GM_PASSES:-}
for f in "$@"; do
    echo "== $f ${passes:+[$passes]}"
    wc -c "$f" | awk '{printf "source_bytes=%s ", $1}'; grep -c '' "$f" | awk '{print "source_lines=" $1}'
    camel "$f" $passes std::stats std::rgir 2>/dev/null | grep '^GRAPH_STATS' | head -1
    LD_PRELOAD="$so" CAMEL_GC_STATS=1 camel "$f" $passes std::rgir 2>&1 >/dev/null |
        grep -E '^\[malloc_count\]|^\[camel\] spaces' |
        sed -E 's/graph_reserved_mb=[0-9]+ //; s/perm_reserved_mb=[0-9]+ //; s/meta_reserved_mb=[0-9]+ //; s/(graph|perm|meta)_(segments|limit_mb)=[0-9]+ ?//g'
    best=999999
    for i in 1 2 3 4 5; do
        t0=$(date +%s%N); camel "$f" $passes std::rgir >/dev/null 2>&1; t1=$(date +%s%N)
        ms=$(( (t1 - t0) / 1000000 )); [ "$ms" -lt "$best" ] && best=$ms
    done
    echo "wall_ms_best_of_5=$best"
done
rm -f "$so"
