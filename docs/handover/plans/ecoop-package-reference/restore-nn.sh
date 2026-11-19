set -e
cd /work/Camel
T=/tmp/nn-restore; rm -rf $T; mkdir -p $T
git archive ae874ce04 modules/nn stdlib/nn.cml $(git ls-tree --name-only ae874ce04 stdlib/signatures/nn.cml) | tar -x -C $T
cd $T
grep -rIl . | xargs sed -i -E 's/OpenCML/GN/g; s/opencml/gn/g; s/\bCamel\b/GN/g; s/Camel/Gn/g; s/CAMEL/GN/g; s/camel/gn/g; s/\.cml\b/.gn/g; s/CML/GN/g; s/OpenCml/Gn/g; s/Cml/Gn/g; s/cml/gn/g; s/(Wei Zhenjie|Zhenjie Wei)/the GN authors/g; s/Copyright \(c\) the GN Organization/Copyright (c) the GN authors/g'
mv stdlib/nn.cml stdlib/nn.gn; [ -f stdlib/signatures/nn.cml ] && mv stdlib/signatures/nn.cml stdlib/signatures/nn.gn
find . -iname "*camel*" -o -iname "*cml*"
cp -r modules/nn /work/gn-pkg/gn/modules/; cp stdlib/nn.gn /work/gn-pkg/gn/stdlib/; [ -f stdlib/signatures/nn.gn ] && cp stdlib/signatures/nn.gn /work/gn-pkg/gn/stdlib/signatures/
cd /work/gn-pkg/gn
grep -q "add_subdirectory(modules/nn)" CMakeLists.txt || sed -i 's#^add_subdirectory(modules/tensor)$#&\nadd_subdirectory(modules/nn)#' CMakeLists.txt
grep -n "modules/nn\|modules/tensor" CMakeLists.txt; grep -rl "OpenCML\|Organization" modules/nn | head -3; head -2 modules/nn/*.cpp | head -4
(setsid nohup bash /work/build-gn.sh > /work/gn-pkg/build.log 2>&1 &)
echo restarted
