@echo off
rem Builds "Ship of Gadeguion Packager.exe" (one file, no console window) into dist\
rem Needs Python 3 (py launcher) and payload\EBOOT.BIN from a port build.
cd /d "%~dp0"
if not exist payload\EBOOT.BIN (
  echo payload\EBOOT.BIN is missing: build the port first ^(scripts/package-real-native.sh^).
  exit /b 1
)
if not exist .venv\Scripts\pyinstaller.exe (
  py -3 -m venv .venv
  .venv\Scripts\python -m pip install --quiet pyinstaller
)
.venv\Scripts\pyinstaller --noconfirm --onefile --windowed --name "Ship of Gadeguion Packager" --add-data "payload;payload" oot_ps3_packager.py
