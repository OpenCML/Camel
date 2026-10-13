#!/bin/bash
# Translate a Camel program with std::cpp and build the C++ it writes (Linux).
#
#   bash docs/design/cpp-memory/tools/cppbuild.sh <file.cml|file.cpp> [passes before std::cpp ...]
#
# Writes $CPPMEM_OUT/<stem>.cpp and $CPPMEM_OUT/<stem> (default /tmp/cppmem) and prints the
# path of the executable. Run from the repository root after `npm run build`.
# camel-cpp does the same on Windows only: it looks for build/Release/libcamel.lib.
set -u
SRC=${1:?file}; shift
ROOT=$(pwd)
CAMEL=${CAMEL:-$ROOT/out/latest/bin/camel}
OUT=${CPPMEM_OUT:-/tmp/cppmem}
LIBDIR=${CAMEL_LIBDIR:-$ROOT/build/Release}
mkdir -p "$OUT"
STEM=$(basename "$SRC"); STEM=${STEM%.*}
if [ "${SRC##*.}" = cpp ]; then
    CPP=$SRC
else
    CPP=$OUT/$STEM.cpp
    "$CAMEL" "$SRC" "$@" std::cpp > "$CPP" || { echo "std::cpp failed for $SRC" >&2; exit 2; }
fi
JSON_INC=$(ls -d "$HOME"/.conan2/p/nlohm*/p/include 2>/dev/null | head -1)
clang++ -std=c++23 -O2 -DNDEBUG -DPLATFORM_LINUX -DNOMINMAX -stdlib=libstdc++ -w \
    -I"$ROOT/include" -I"$ROOT/src" -I"$ROOT/third_party" -I"$ROOT" ${JSON_INC:+-isystem "$JSON_INC"} \
    "$CPP" -o "$OUT/$STEM" "$LIBDIR/libcamel.so" -Wl,-rpath,"$LIBDIR" \
    || { echo "C++ compilation failed for $CPP" >&2; exit 3; }
echo "$OUT/$STEM"
