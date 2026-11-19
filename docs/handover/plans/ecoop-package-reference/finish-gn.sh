#!/bin/bash
# Final touches on the staged package: declarations of the removed translator, tests of removed parts.
set -e
cd /work/gn-pkg/gn
# the C++ translator's pass declarations
perl -0pi -e 's{\n/// Prints the graph as standalone C\+\+ source and consumes it\.\nexport func cpp::<F>\(f: F\): F! when is_function::<F>\(\)\n}{\n}; s{\nnamespace cpp \{\n.*?\n\}\n}{\n}s' stdlib/std.gn
grep -n "cpp" stdlib/std.gn || echo "std.gn: no cpp declarations"
# whole plan directories about removed parts
rm -rf test/plans/gpu test/plans/feat/bindings test/plans/feat/format test/plans/feat/docs test/plans/feat/docs_links test/plans/feat/docs_ai_reference test/plans/feat/headers test/plans/trans/cpp test/plans/feat/trans/cpp
rm -f test/plans/feat/tutorial_examples.plan.toml test/plans/feat/examples.plan.toml test/plans/feat/exemplars.plan.toml test/plans/feat/showcase_*.plan.toml test/plans/feat/docs_*.plan.toml
rm -f test/plans/feat/tools/lsp*.plan.toml test/plans/feat/tools/debugger*.plan.toml
# single entries
node /work/prune.mjs /work/gn-pkg/prune-list2.txt .
echo "plans: $(find test/plans -name '*.plan.toml' | wc -l)"
