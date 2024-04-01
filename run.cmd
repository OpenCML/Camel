@ECHO ON

set BASEDIR=%~dp0
PUSHD %BASEDIR%

RMDIR /Q /S build

conan install . --output-folder=build --build=missing
cd build
cmake .. -G "Visual Studio 17" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake
cmake --build . --config Release
@REM cmake --build . --config Debug
@REM Release\camel.exe --profile "D:\Projects\Camel\demo\std.cml"
Release\camel.exe --dump-cst "D:\Projects\Camel\demo\std.cml"
@REM Debug\camel.exe --dump-cst "D:\Projects\Camel\test\full.cml"