@echo off
setlocal

set SCRIPT_DIR=%~dp0
set REPO_ROOT=%SCRIPT_DIR%..\..
set VENV_DIR=%REPO_ROOT%\venv
set VENV_PY=%VENV_DIR%\Scripts\python.exe
set REQS=%SCRIPT_DIR%requirements.txt

if not exist "%VENV_PY%" (
    echo [particle_editor] Creating shared venv at %VENV_DIR%...
    where python >nul 2>nul
    if %ERRORLEVEL%==0 (
        python -m venv "%VENV_DIR%" || goto :error
    ) else (
        py -m venv "%VENV_DIR%" || goto :error
    )
    "%VENV_PY%" -m pip install --upgrade pip || goto :error
)

"%VENV_PY%" -m pip install -q -r "%REQS%" || goto :error

"%VENV_PY%" "%SCRIPT_DIR%main.py" %*
exit /b %ERRORLEVEL%

:error
echo Setup failed.
exit /b 1
