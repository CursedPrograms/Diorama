@echo off
setlocal
cd /d "%~dp0"
rem Builds Diorama (Release) and dist\Diorama-win64.zip.
rem Needs Visual Studio 2022 (C++) and CMake. SDL2 and Dear ImGui are fetched into external\ once.
if not exist "external\SDL2-2.30.9\cmake" (
    echo Getting SDL2's development package, once...
    if not exist external mkdir external
    curl -sL -o external\SDL2-devel.zip https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-VC.zip || goto :error
    tar -xf external\SDL2-devel.zip -C external || goto :error
    del external\SDL2-devel.zip
)
if not defined BUILD_DIR set "BUILD_DIR=build"
cmake -B %BUILD_DIR% -G "Visual Studio 17 2022" -A x64 || goto :error
cmake --build %BUILD_DIR% --config Release || goto :error
if exist dist\Diorama rmdir /s /q dist\Diorama
mkdir dist\Diorama
copy /y %BUILD_DIR%\Release\Diorama.exe dist\Diorama\ >nul || goto :error
copy /y %BUILD_DIR%\Release\SDL2.dll dist\Diorama\ >nul || goto :error
xcopy /e /i /q /y %BUILD_DIR%\Release\assets dist\Diorama\assets >nul
xcopy /e /i /q /y %BUILD_DIR%\Release\templates dist\Diorama\templates >nul
rem the players Windows builds are made from: WebView2 (JavaScript) and Diorama's native one (C++, with SDL2 and OpenCV)
mkdir dist\Diorama\player
for %%f in (DioramaGame.exe DioramaNative.exe SDL2.dll opencv_world4100.dll) do if exist %BUILD_DIR%\Release\player\%%f copy /y %BUILD_DIR%\Release\player\%%f dist\Diorama\player\ >nul
for %%f in (README.md LICENSE NOTICE install.bat install.ps1) do if exist %%f copy /y %%f dist\Diorama\ >nul
if exist dist\Diorama-win64.zip del dist\Diorama-win64.zip
"%SystemRoot%\System32\tar.exe" -a -c -f dist\Diorama-win64.zip -C dist Diorama || goto :error
echo Built: dist\Diorama-win64.zip
rem installed on this PC: this build replaces that copy too
if exist "%LOCALAPPDATA%\Programs\Diorama\Diorama.exe" call "%~dp0install.bat" --quiet
exit /b 0
:error
echo Build failed, see above.
pause
exit /b 1
