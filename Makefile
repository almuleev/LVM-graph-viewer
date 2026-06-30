# Build the LVM Reader CLI with g++ (MSYS2/MinGW or any C++17 compiler).
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -finput-charset=UTF-8
# Static linking keeps the binary self-contained (no libstdc++/libgcc DLLs).
LDFLAGS  ?= -static
TARGET   := lvm_reader
# Extract version from the latest git tag (e.g. v0.4.4).
VERSION  := $(shell git describe --tags --abbrev=0 2>/dev/null || echo v0.0.0)

# Parser/analysis library shared by the CLI and the tests.
LIB_SRC  := lvm_parser.cpp fft.cpp analysis.cpp
APP_SRC  := main.cpp $(LIB_SRC)
APP_OBJ  := $(APP_SRC:.cpp=.o)
HDRS     := lvm_parser.hpp fft.hpp analysis.hpp export_helpers.hpp formula_engine.hpp

ifeq ($(OS),Windows_NT)
    BIN      := $(TARGET).exe
    TEST_BIN := tests/run_tests.exe
    GUI_BIN  := LVM-graph-viewer-win-x64.exe
else
    BIN      := $(TARGET)
    TEST_BIN := tests/run_tests
    GUI_BIN  := LVM-graph-viewer
endif

.PHONY: all clean run test gui

all: $(BIN)

$(BIN): $(APP_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(APP_OBJ) $(LDFLAGS)

%.o: %.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN) lvm_files_for_tests/test.lvm

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): tests/run_tests.cpp $(LIB_SRC) export_helpers.cpp formula_engine.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -I. -o $@ tests/run_tests.cpp $(LIB_SRC) export_helpers.cpp formula_engine.cpp $(LDFLAGS)

# Native Win32 GUI viewer (Windows only). Needs -municode for wWinMain and the
# Win32 import libraries. On Windows you can also run: powershell ./build_gui.ps1
gui: $(GUI_BIN)

$(GUI_BIN): gui_main.cpp $(LIB_SRC) $(HDRS)
	$(CXX) $(CXXFLAGS) -DAPP_VERSION_W=L\"$(VERSION)\" -municode -mwindows -o $@ gui_main.cpp $(LIB_SRC) export_helpers.cpp formula_engine.cpp $(LDFLAGS) -lcomdlg32 -lgdi32 -luser32 -lgdiplus -lcomctl32

clean:
	rm -f $(APP_OBJ) $(BIN) $(TEST_BIN) $(GUI_BIN)
