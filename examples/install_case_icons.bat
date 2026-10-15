@echo off
setlocal EnableDelayedExpansion
rem Copies your custom case pictures into the game so each custom case shows its own.
rem Name each picture after its case, e.g.  aurora.png  or  crate_csgogc_aurora.png  (transparent PNG, about 256 x 171).
rem Edit the two paths, then double-click this file, and restart the game twice.

set "GAME=G:\SteamLibrary\steamapps\common\csgo legacy"
set "SRC=C:\path\to\case_icons"

set "DEST=%GAME%\csgo\resource\flash\econ\weapon_cases"
if not exist "%DEST%" mkdir "%DEST%"

set COUNT=0
for %%f in ("%SRC%\*.png") do (
    set "NAME=%%~nf"
    if /i "!NAME:~0,12!"=="crate_csgogc" (
        copy /Y "%%f" "%DEST%\%%~nxf" >nul
    ) else (
        copy /Y "%%f" "%DEST%\crate_csgogc_%%~nxf" >nul
    )
    set /a COUNT+=1
)
echo Copied !COUNT! picture(s) to %DEST%
pause
