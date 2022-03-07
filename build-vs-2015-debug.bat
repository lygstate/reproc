call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64_x86 10.0.19041.0 -vcvars_ver=14.0
cd /d "%~dp0"
rd /s /q build\i686-pc-windows-msvc-14-debug-install
mkdir build\i686-pc-windows-msvc-14-debug-install
cd build\i686-pc-windows-msvc-14-debug-install
cmake -G"Ninja" ..\.. -DREPROC++=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=..\i686-pc-windows-msvc-14-install\debug
cmake --build . --target install
pause
