@ECHO ON

set BASEDIR=%~dp0
PUSHD %BASEDIR%

RMDIR /Q /S build
conan install . --output-folder=build --build=missing --settings=build_type=Debug
cd build
cmake .. -G "Visual Studio 17" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake
cmake --build . --config Debug
copy Debug\camel.exe camel.exe