@ECHO ON

set BASEDIR=%~dp0
PUSHD %BASEDIR%

@REM RMDIR /Q /S build

@REM conan install . --output-folder=build --build=missing
cd build
cmake .. -G "Visual Studio 17" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake
cmake --build . --config Release
copy Release\camel.exe Release\camel-stable.exe
@REM cmake --build . --config Debug
@REM Release\camel.exe --profile --repeat 10 "D:\Projects\Camel\demo\std.cml"
@REM Release\camel.exe --dump-cst "D:\Projects\Camel\demo\std.cml"
@REM camel --syntax-only --error-format json "D:\Projects\Camel\demo\error.cml"
@REM Debug\camel.exe --dump-cst "D:\Projects\Camel\test\full.cml"