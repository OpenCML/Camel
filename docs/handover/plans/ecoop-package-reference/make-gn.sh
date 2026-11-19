#!/bin/bash
# Build the anonymous, trimmed GN source package from one commit of the Camel tree.
set -euo pipefail
H=${H:-ae874ce04}
ROOT=/work/gn-pkg
OUT=$ROOT/gn
rm -rf "$ROOT"; mkdir -p "$OUT"
cd /work/Camel
git archive "$H" CMakeLists.txt conanfile.txt .clang-format .gitignore src include modules stdlib cmake \
    third_party tools test diagnostics docs/language/en LICENSE \
    scripts/build.js scripts/test.js scripts/common.js scripts/cmake-opts.js scripts/diagnostics-catalog.mjs scripts/check-source-artifacts.mjs scripts/collect-out.js scripts/cxx-runtime-guard.mjs scripts/build-info.js scripts/clean.js \
  | tar -x -C "$OUT"
cd "$OUT"

# 1. Trim: modules the study does not use, the Python embedding and binding, GPU tiles, the C++ translator.
DROP_MODS="python pyplot tiles sim ckpt"
for m in $DROP_MODS; do
  rm -rf "modules/$m" "stdlib/$m.cml" "stdlib/signatures/$m.cml"
  sed -i "\#add_subdirectory(modules/$m)#d" CMakeLists.txt
done
sed -i '\#add_subdirectory(bindings/python)#d' CMakeLists.txt
for t in camel-cpp lang-server debugger format codegen profiler parsecheck; do
  rm -rf "tools/$t"; sed -i "\#add_subdirectory($t)#d" tools/CMakeLists.txt
done
rm -rf src/passes/trans/cpp
sed -i '\#include "passes/trans/cpp/cpp_export.h"#d; /registerCoreKernel("cpp/d; /{"std::cxx", "std::cpp"}/d; /{"std::cppmod",/d; /{"std::cppinspect",/d' src/execute/pass.cpp
sed -i '/^pybind11\//d' conanfile.txt
# options: no Python by default
sed -i '/"name": "CAMEL_ENABLE_PYTHON"/,/"type"/ s/"default": "ON"/"default": "OFF"/' cmake/options.json
mv docs/language/en/* docs/language/ && rmdir docs/language/en

# 2. Identity and internal references.
mapfile -t TEXT < <(grep -rIl --exclude-dir=third_party . || true)
sed -i -E \
  -e 's/Copyright \(c\) the OpenCML Organization/Copyright (c) the GN authors/g' \
  -e 's/Copyright \(c\) 2023 Wei Zhenjie/Copyright (c) 2023 the GN authors/g' \
  -e 's/(Wei Zhenjie|Zhenjie Wei)/the GN authors/g' \
  -e 's#Kogler7/Camel-Archive#the study archive#g; s/Kogler7/anonymous/g' \
  -e 's#/home/zjwei#/home/user#g; s/zjwei/user/g' \
  -e 's#`?origin/fsh-dev`?#the development branch#g; s/fsh-dev/the development branch/g' \
  -e 's#Camel-Archive/[A-Za-z0-9_./-]*#the study archive#g' \
  -e 's#ICPC2027/[A-Za-z0-9_./-]*#the study archive#g; s/\bICPC\b ?//g; s#ECOOP2027/[A-Za-z0-9_./-]*#the study archive#g' \
  -e 's/decision-maker/design/g' \
  "${TEXT[@]}"

# 3. The name: Camel -> GN, extension .cml -> .gn, binary camel -> gn.
sed -i -E \
  -e 's/OpenCML/GN/g; s/opencml/gn/g; s/OPENCML/GN/g' \
  -e 's/\bCamel\b/GN/g; s/Camel/Gn/g; s/CAMEL/GN/g; s/camel/gn/g' \
  -e 's/\.cml\b/.gn/g; s/CML/GN/g; s/OpenCml/Gn/g; s/Cml/Gn/g; s/cml/gn/g' \
  "${TEXT[@]}"
rm -rf tools/graph-diff
sed -i '/^\.claude/d' .gitignore
# paths: deepest first
find . -depth -not -path './third_party/*' \( -iname '*camel*' -o -iname '*cml*' \) -print0 |
while IFS= read -r -d '' p; do
  d=$(dirname "$p"); b=$(basename "$p")
  nb=$(printf '%s' "$b" | sed -E 's/OpenCML/GN/g; s/opencml/gn/g; s/Camel/Gn/g; s/CAMEL/GN/g; s/camel/gn/g; s/\.cml$/.gn/; s/OpenCml/Gn/g; s/Cml/Gn/g; s/cml/gn/g')
  [ "$b" != "$nb" ] && mv "$p" "$d/$nb"
done

# 4. A minimal package.json.
cat > package.json <<'JSON'
{
    "name": "gn",
    "private": true,
    "type": "module",
    "scripts": {
        "build": "node ./scripts/build.js",
        "test": "node ./scripts/test.js --tier fast"
    }
}
JSON
echo "staged: $(du -sh . | cut -f1)"
bash /work/docs-gn.sh
printf 'R"(\n\nGN, a language whose source is the dataflow graph.\nCopyright (c) the GN authors.\nGN is licensed under the MIT license.\n)"\n' > "$OUT/ABOUT"
