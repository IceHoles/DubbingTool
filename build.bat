@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cd /d "c:\Users\icehole\git\DubbingTool"
cmake --build build --config Release
