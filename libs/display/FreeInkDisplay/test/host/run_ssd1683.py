#!/usr/bin/env python3
"""Compile the SSD1683 driver as a Glyph build and check its byte sequences."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LIB = HERE.parents[1]
with tempfile.TemporaryDirectory(prefix="freeink-ssd1683-test-") as directory:
    root = Path(directory)
    for folder in ("src/driver", "src/bus", "include"):
        (root / folder).mkdir(parents=True, exist_ok=True)
    for ext in ("h", "cpp"):
        shutil.copy2(LIB / f"src/driver/Ssd1683Driver.{ext}", root / f"src/driver/Ssd1683Driver.{ext}")
    shutil.copy2(LIB / "src/driver/PanelDriver.h", root / "src/driver/PanelDriver.h")
    shutil.copy2(LIB / "include/GrayscaleCapabilities.h", root / "include/GrayscaleCapabilities.h")
    for name in ("Arduino.h", "SPI.h", "esp_heap_caps.h"):
        shutil.copy2(HERE / "pro_stubs" / name, root / name)
    shutil.copy2(HERE / "pro_stubs/EpdBus.h", root / "src/bus/EpdBus.h")
    board = (HERE / "pro_stubs/BoardConfig.h").read_text()
    for old, new in (
        ("MetalioEInk4 };", "MetalioEInk4, Glyph };"),
        ("enum class DisplayController { SSD1677, UC8179, UC8279 };",
         "enum class DisplayController { SSD1677, UC8179, UC8279, SSD1683 };"),
        ("displayWidth=800, displayHeight=480;", "displayWidth=400, displayHeight=300;"),
        ("displaySpiHz=10000000;", "displaySpiHz=4000000;"),
        ("Board board=Board::XteinkX4Pro;", "Board board=Board::Glyph;"),
        ("displayController=DisplayController::SSD1677;", "displayController=DisplayController::SSD1683;"),
    ):
        assert old in board, old
        board = board.replace(old, new)
    (root / "BoardConfig.h").write_text(board)
    cxx = os.environ.get("CXX", "c++")
    defines = ["-DARDUINO=1", "-DFREEINK_DEVICE_GLYPH=1", "-DFREEINK_DRIVER_SSD1683=1"]
    exe = root / "ssd1683"
    subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-function",
                    *defines, "-I" + str(root), str(HERE / "test_ssd1683.cpp"),
                    str(root / "src/driver/Ssd1683Driver.cpp"), "-o", str(exe)], check=True)
    # The facade's Glyph driver selection must compile with only SSD1683 linked.
    shutil.copy2(LIB / "src/FreeInkDisplay.cpp", root / "src/FreeInkDisplay.cpp")
    shutil.copy2(LIB / "include/FreeInkDisplay.h", root / "include/FreeInkDisplay.h")
    subprocess.run([cxx, "-std=c++17", "-fsyntax-only", *defines, "-I" + str(root), "-I" + str(root / "include"),
                    str(root / "src/FreeInkDisplay.cpp")], check=True)
    subprocess.run([str(exe)], check=True)
