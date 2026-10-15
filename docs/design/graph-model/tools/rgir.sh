#!/bin/bash
# The graph of a program in the compact notation the design document uses.
#   bash docs/design/graph-model/tools/rgir.sh FILE.cml [passes...]
# It is `camel FILE [passes] std::rgir` with the origin/span/srcfile attributes dropped and
# every line shortened:  N02 [OPER :op/mul_l]   N00 -> N02 [NORM 0|0]   N06 -> EXIT
# The edge label `i|j` is: i = position among the edges leaving the source, j = position among
# the inputs of that kind at the target.
f=${1:?file}; shift
camel "$f" "$@" std::rgir | sed -E '
    s/, origin=[0-9]+, span="[^"]*", srcfile="[^"]*"//
    /\[label="ARGS", type="ARGS"\];/d
    /\[label="EXIT", type="EXIT"\];/d
    s/subgraph cluster_F[0-9A-F]+ \{/subgraph {/
    s/^( *)label="(.*)";$/\1  name "\2"/
    s/^( *)(N[0-9A-F]+) \[label="(.*)", type="([A-Z_]+)"\];$/\1\2 [\4 \3]/
    s/^( *)F[0-9A-F]+ -> (N[0-9A-F]+) \[label="([^"]*)", type="([A-Z_]+)"\];$/\1ARGS -> \2 [\4 \3]/
    s/^( *)(N[0-9A-F]+) -> O[0-9A-F]+ \[label="[^"]*", type="EXIT"\];$/\1\2 -> EXIT/
    s/^( *)(N[0-9A-F]+) -> (N[0-9A-F]+) \[label="([^"]*)", type="([A-Z_]+)"\];$/\1\2 -> \3 [\5 \4]/
    s/\[([A-Z_]+) \1\]/[\1]/
' | awk '
    /subgraph \{$/ { pending = $0; next }
    pending != "" && /name "/ { sub(/subgraph \{/, "", pending); match($0, /"[^"]*"/); print pending "subgraph " substr($0, RSTART, RLENGTH) " {"; pending = ""; next }
    { if (pending != "") { print pending; pending = "" } print }
'
