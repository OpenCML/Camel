@ECHO ON

set BASEDIR=%~dp0
PUSHD %BASEDIR%

@REM RMDIR /Q /S build

conan install . --output-folder=build --build=missing
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake
cmake --build . --config Release
Release\camel.exe "D:\CodeBase\C++Prjs\Camel\demo\std.cml"