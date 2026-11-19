set -e
MODS="${MODS:-env json fs hash re}"
cd /work/Camel
T=/tmp/mods-restore; rm -rf $T; mkdir -p $T
paths=""; for m in $MODS; do paths="$paths modules/$m stdlib/$m.cml"; git cat-file -e ae874ce04:stdlib/signatures/$m.cml 2>/dev/null && paths="$paths stdlib/signatures/$m.cml"; done
git archive ae874ce04 $paths | tar -x -C $T
cd $T
grep -rIl . | xargs sed -i -E 's/Copyright \(c\) the OpenCML Organization/Copyright (c) the GN authors/g; s/(Wei Zhenjie|Zhenjie Wei)/the GN authors/g; s#/home/zjwei#/home/user#g; s/OpenCML/GN/g; s/opencml/gn/g; s/\bCamel\b/GN/g; s/Camel/Gn/g; s/CAMEL/GN/g; s/camel/gn/g; s/\.cml\b/.gn/g; s/CML/GN/g; s/OpenCml/Gn/g; s/Cml/Gn/g; s/cml/gn/g'
find . -depth -name "*.cml" | while read p; do mv "$p" "${p%.cml}.gn"; done
find . -iname "*camel*" -o -iname "*cml*"
cp -r modules/* /work/gn-pkg/gn/modules/; cp stdlib/*.gn /work/gn-pkg/gn/stdlib/; [ -d stdlib/signatures ] && cp stdlib/signatures/*.gn /work/gn-pkg/gn/stdlib/signatures/ || true
cd /work/gn-pkg/gn
for m in $MODS; do grep -q "add_subdirectory(modules/$m)" CMakeLists.txt || sed -i "s#^add_subdirectory(modules/str)\$#&\nadd_subdirectory(modules/$m)#" CMakeLists.txt; done
grep -n "add_subdirectory(modules/" CMakeLists.txt | tr "\n" " "; echo
grep -rnE "/home/[a-z]|/Users/|D:\\\\" modules/env modules/json modules/fs modules/hash modules/re stdlib/env.gn | head -3
(setsid nohup bash -c "bash /work/build-gn.sh > /work/gn-pkg/build.log 2>&1; bash /work/rerun.sh > /work/gn-pkg/rerun.out 2>&1" &)
echo restarted
