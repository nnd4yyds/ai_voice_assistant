@echo off
setlocal

set "IDF_PATH=D:\esp\esp-idf"
set "IDF_TOOLS_PATH=D:\.espressif"
set "IDF_PYTHON_ENV_PATH=D:\.espressif\python_env\idf5.2_py3.11_env"
set "OPENOCD_SCRIPTS=D:\.espressif\tools\openocd-esp32\v0.12.0-esp32-20241016\openocd-esp32\share\openocd\scripts"
set "IDF_CCACHE_ENABLE=1"
set "ESP_ROM_ELF_DIR=D:\.espressif\tools\esp-rom-elfs\20230320"

set "PATH=D:\.espressif\tools\xtensa-esp-elf-gdb\14.2_20240403\xtensa-esp-elf-gdb\bin;%PATH%"
set "PATH=D:\.espressif\tools\xtensa-esp-elf\esp-13.2.0_20230928\xtensa-esp-elf\bin;%PATH%"
set "PATH=D:\.espressif\tools\riscv32-esp-elf\esp-13.2.0_20230928\riscv32-esp-elf\bin;%PATH%"
set "PATH=D:\.espressif\tools\esp32ulp-elf\2.35_20220830\esp32ulp-elf\bin;%PATH%"
set "PATH=D:\.espressif\tools\cmake\3.30.2\bin;%PATH%"
set "PATH=D:\.espressif\tools\openocd-esp32\v0.12.0-esp32-20241016\openocd-esp32\bin;%PATH%"
set "PATH=D:\.espressif\tools\ninja\1.12.1;%PATH%"
set "PATH=D:\.espressif\tools\idf-exe\1.0.3;%PATH%"
set "PATH=D:\.espressif\tools\ccache\4.10.2\ccache-4.10.2-windows-x86_64;%PATH%"
set "PATH=D:\.espressif\tools\dfu-util\0.11\dfu-util-0.11-win64;%PATH%"
set "PATH=%IDF_PYTHON_ENV_PATH%\Scripts;%IDF_PATH%\tools;%PATH%"

if "%~1"=="" (
    echo === ESP-IDF version ===
    python "%IDF_PATH%\tools\idf.py" --version
    echo.
    echo === ESP-IDF tools check ===
    python "%IDF_PATH%\tools\idf_tools.py" check
    echo.
    echo === Tool paths ===
    where python
    where cmake
    where ninja
    where xtensa-esp32s3-elf-gcc
    exit /b %ERRORLEVEL%
)

%*
