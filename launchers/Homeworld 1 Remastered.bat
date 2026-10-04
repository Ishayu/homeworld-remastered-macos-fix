@echo off
rem Starts Homeworld 1 Remastered directly, bypassing the Homeworld Remastered launcher.
rem Also tells Wine to load the hwgl opengl32.dll shim from the game folder (HomeworldRM.exe only).
reg add "HKCU\Software\Wine\AppDefaults\HomeworldRM.exe\DllOverrides" /v opengl32 /t REG_SZ /d native,builtin /f >nul
cd /d "%~dp0HomeworldRM\Bin\Release"
start "" HomeworldRM.exe -dlccampaign HW1Campaign.big -campaign HomeworldClassic -moviepath DataHW1Campaign
