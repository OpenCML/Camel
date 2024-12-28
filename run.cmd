@ECHO ON

set BASEDIR=%~dp0
PUSHD %BASEDIR%

RMDIR /Q /S build
conan install . --output-folder=build --build=missing
cd build
cmake .. -G "Visual Studio 17" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake
cmake --build . --config Release
copy Release\camel.exe camel.exe