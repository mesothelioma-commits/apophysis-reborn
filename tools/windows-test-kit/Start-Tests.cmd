@echo off
setlocal
pushd "%~dp0" || exit /b 2
set "KIT_GIT_BASH="
if exist "%ProgramFiles%\Git\git-bash.exe" set "KIT_GIT_BASH=%ProgramFiles%\Git\git-bash.exe"
if not defined KIT_GIT_BASH if exist "%LOCALAPPDATA%\Programs\Git\git-bash.exe" set "KIT_GIT_BASH=%LOCALAPPDATA%\Programs\Git\git-bash.exe"
if not defined KIT_GIT_BASH if exist "%ProgramFiles(x86)%\Git\git-bash.exe" set "KIT_GIT_BASH=%ProgramFiles(x86)%\Git\git-bash.exe"
if not defined KIT_GIT_BASH for /f "delims=" %%G in ('where.exe git-bash.exe 2^>nul') do if not defined KIT_GIT_BASH set "KIT_GIT_BASH=%%G"
if not defined KIT_GIT_BASH (
    echo Open Git Bash in this extracted folder and type: bash ./run-tests.sh --guided
    echo This kit requires Git for Windows. No compiler or Python is required.
    pause
    popd
    exit /b 2
)
start "" /wait "%KIT_GIT_BASH%" "--cd=%CD%" -c "bash ./lib/guided-session.sh"
set "KIT_EXIT=%ERRORLEVEL%"
popd
exit /b %KIT_EXIT%
