@echo off
setlocal EnableDelayedExpansion
if defined PLATFORMIO_CORE_DIR if exist "%PLATFORMIO_CORE_DIR%\penv\Scripts\python.exe" (
  "%PLATFORMIO_CORE_DIR%\penv\Scripts\python.exe" "%~dp0platformio_cli.py" %*
  exit /b !errorlevel!
)
if defined VIRTUAL_ENV if exist "%VIRTUAL_ENV%\Scripts\python.exe" (
  "%VIRTUAL_ENV%\Scripts\python.exe" "%~dp0platformio_cli.py" %*
  exit /b !errorlevel!
)
if exist "%~dp0..\.venv\Scripts\python.exe" (
  "%~dp0..\.venv\Scripts\python.exe" "%~dp0platformio_cli.py" %*
  exit /b !errorlevel!
)
if exist "%USERPROFILE%\.platformio\penv\Scripts\python.exe" (
  "%USERPROFILE%\.platformio\penv\Scripts\python.exe" "%~dp0platformio_cli.py" %*
  exit /b !errorlevel!
)
where py >nul 2>nul
if %errorlevel% equ 0 (
  py -3 "%~dp0platformio_cli.py" %*
  exit /b !errorlevel!
)
python "%~dp0platformio_cli.py" %*
exit /b !errorlevel!
