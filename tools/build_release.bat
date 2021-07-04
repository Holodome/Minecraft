@echo off

cls
if not exist .\build mkdir build 
pushd build 

where cl /q
if %ERRORLEVEL% neq 0 call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

set "build_options=-nologo -DEBUG -fp:fast -O2 -Oi -Zi -FC -MTd -wd4201 -WX -I"..\src" -I"..\thirdparty" -std:c++17 -D_CRT_SECURE_NO_WARNINGS"
cl %build_options% ../src/compile.cc -link -opt:ref gdi32.lib user32.lib kernel32.lib -out:game_release.exe

popd 
copy build\game_release.exe game\