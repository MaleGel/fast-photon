@echo off
setlocal

set SCRIPT_DIR=%~dp0
set REPO_ROOT=%SCRIPT_DIR%..\..
set VENV_PY=%REPO_ROOT%\venv\Scripts\python.exe

if exist "%VENV_PY%" (
    "%VENV_PY%" "%SCRIPT_DIR%bake.py" "%REPO_ROOT%" %*
) else (
    where python >nul 2>nul
    if %ERRORLEVEL%==0 (
        python "%SCRIPT_DIR%bake.py" "%REPO_ROOT%" %*
    ) else (
        py "%SCRIPT_DIR%bake.py" "%REPO_ROOT%" %*
    )
)
exit /b %ERRORLEVEL%
