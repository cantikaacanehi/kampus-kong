@echo off
g++ kampus_kong.cpp -o kampus_kong -lncurses -DNCURSES_STATIC

if %errorlevel% == 0 (
    echo Build Berhasil.
    kampus_kong.exe
)
else (
    echo Build Gagal.
    pause
)