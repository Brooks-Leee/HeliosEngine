@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 > nul
cd /d E:\HeliosEngine
cmake -B build -S .
cmake --build build
