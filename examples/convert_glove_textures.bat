@echo off
rem Converts every .jpg and .png in IN to a .vtf in OUT with VTFEdit's command line tool.
rem Edit the three paths, then double-click this file. The .vtf keeps the image's name.

set "VTFCMD=C:\Program Files (x86)\VTFEdit\VTFCmd.exe"
set "IN=C:\gloves_in"
set "OUT=G:\SteamLibrary\steamapps\common\csgo legacy\csgo\materials\models\weapons\customization\paints_gloves"

if not exist "%VTFCMD%" (
    echo VTFCmd.exe not found at %VTFCMD%
    echo Edit VTFCMD at the top of this file.
    pause
    exit /b 1
)
if not exist "%OUT%" mkdir "%OUT%"

rem DXT1 for images without transparency, DXT5 for ones with it, shrunk to at most 2048x2048
"%VTFCMD%" -folder "%IN%\*.jpg" -output "%OUT%" -format "DXT1" -alphaformat "DXT5" -resize -rclampwidth 2048 -rclampheight 2048
"%VTFCMD%" -folder "%IN%\*.png" -output "%OUT%" -format "DXT1" -alphaformat "DXT5" -resize -rclampwidth 2048 -rclampheight 2048

echo.
echo Done. Copy the .vmt files into %OUT% too.
pause
