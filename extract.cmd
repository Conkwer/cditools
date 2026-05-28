@echo off
::template
cd "%~dp0"
cdiextractor test.cdi -x -b -C test/
pause&&exit
