::@echo off
::template
cd "%~dp0"
cdibuilder -d ./test -V "MYGAME" -l 11702 -t audio -o test_rebuilded.cdi
pause&&exit
