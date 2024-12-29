@ECHO ON

set BASEDIR=%~dp0
PUSHD %BASEDIR%

cd build
cmake .. -G "Visual Studio 17" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake
cmake --build . --config Debug
copy Debug\camel.exe camel.exe
xcopy /E /I /Y ..\test Debug\test