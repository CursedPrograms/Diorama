@echo off
setlocal
cd /d "%~dp0"
rem Installs Diorama for this user: %LOCALAPPDATA%\Programs\Diorama, with Start menu and Desktop shortcuts.
rem Run build.bat first (or unzip a release and run this beside it).
set "SRC=dist\Diorama"
if exist "Diorama.exe" set "SRC=."
if not exist "%SRC%\Diorama.exe" ( echo Build it first: build.bat & exit /b 1 )
tasklist /fi "imagename eq Diorama.exe" | "%SystemRoot%\System32\find.exe" /i "Diorama.exe" >nul
if not errorlevel 1 ( echo Diorama is open: close it and run this again. & exit /b 1 )
set "INST=%LOCALAPPDATA%\Programs\Diorama"
if not exist "%INST%" mkdir "%INST%"
xcopy /e /i /q /y "%SRC%\*" "%INST%\" >nul || ( echo Couldn't copy to %INST% & exit /b 1 )
powershell -NoProfile -Command "$s=New-Object -ComObject WScript.Shell; foreach($d in @([Environment]::GetFolderPath('Programs'),[Environment]::GetFolderPath('Desktop'))){ $l=$s.CreateShortcut((Join-Path $d 'Diorama.lnk')); $l.TargetPath='%INST%\Diorama.exe'; $l.WorkingDirectory='%INST%'; $l.IconLocation='%INST%\Diorama.exe,0'; $l.Description='Diorama: little worlds on AR markers'; $l.Save() }"
echo Installed: %INST%
if /i not "%~1"=="--quiet" echo Start it from the Start menu or the Desktop.
exit /b 0
