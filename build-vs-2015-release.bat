cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64_x86 10.0.19041.0 -vcvars_ver=14.0
rd /s /q build\i686-pc-windows-msvc-14-release-install
mkdir build\i686-pc-windows-msvc-14-release-install
cd build\i686-pc-windows-msvc-14-release-install
cmake -G"Ninja" ..\.. -DREPROC++=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=..\i686-pc-windows-msvc-14-install\release
cmake --build . --target install
pause
