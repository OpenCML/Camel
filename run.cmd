@ECHO ON

set BASEDIR=%~dp0
PUSHD %BASEDIR%

@REM RMDIR /Q /S build
@REM conan install . --output-folder=build --build=missing
cd build
cmake .. -G "Visual Studio 17" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake
cmake --build . --config Release
@REM cmake --build . --config Debug
copy Release\camel.exe Release\camel-stable.exe