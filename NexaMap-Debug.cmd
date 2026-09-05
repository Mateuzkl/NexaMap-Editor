@echo off
setlocal EnableExtensions DisableDelayedExpansion
title NexaMap - Console de depuracao
cd /d "%~dp0"
set "NEXAMAP_DIAGNOSTICS=1"
set "NEXAMAP_DEBUG_EXE=%~dp0NexaMap Editor.exe"
rem Prefer the Visual Studio solution output over an older copied executable.
if exist "%~dp0vcproj\x64\Release\NexaMap Editor.exe" set "NEXAMAP_DEBUG_EXE=%~dp0vcproj\x64\Release\NexaMap Editor.exe"
if not exist "%NEXAMAP_DEBUG_EXE%" goto missing
echo Executavel: "%NEXAMAP_DEBUG_EXE%"
echo O terminal permanecera aberto quando o NexaMap encerrar.
echo.
"%NEXAMAP_DEBUG_EXE%"
set "NEXAMAP_EXIT_CODE=%ERRORLEVEL%"
echo.
echo NexaMap encerrou com codigo %NEXAMAP_EXIT_CODE%.
echo Envie o nexamap.log que fica junto do executavel.
echo Se essa pasta for somente leitura, veja %%LOCALAPPDATA%%\NexaMap\logs.
pause
exit /b %NEXAMAP_EXIT_CODE%
:missing
echo Executavel nao encontrado. Compile o NexaMap em Release x64 primeiro.
pause
exit /b 1
