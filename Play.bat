@echo off
setlocal
if not defined UE_EDITOR set "UE_EDITOR=%ProgramFiles%\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
if not exist "%UE_EDITOR%" (
    echo Set UE_EDITOR to the full path of UnrealEditor.exe.
    pause
    exit /b 1
)
start "" "%UE_EDITOR%" "%~dp0SpaceFlight.uproject" -game -windowed -ResX=1280 -ResY=720
