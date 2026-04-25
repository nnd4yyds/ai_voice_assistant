@echo off
setlocal
set "IDF_TOOLS_PATH=D:\.espressif"
echo === ESP-IDF export ===
call D:\esp\esp-idf\export.bat
if errorlevel 1 (
    echo ESP-IDF export failed.
    exit /b 1
)

echo.
echo === idf.py version ===
idf.py --version

echo.
echo === Python ===
python --version
where python

echo.
echo === CMake ===
where cmake
cmake --version

echo.
echo === Ninja ===
where ninja
ninja --version

echo.
echo === ESP-IDF environment ===
echo IDF_PATH=%IDF_PATH%
echo IDF_TOOLS_PATH=%IDF_TOOLS_PATH%

echo.
echo === ESP serial ports ===
python -m serial.tools.list_ports

echo.
echo === Done ===
