# Build the LVM Reader CLI with g++ (MSYS2/MinGW or any C++17 compiler).
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
# Static linking keeps the binary self-contained (no libstdc++/libgcc DLLs).
LDFLAGS  ?= -static
TARGET   := lvm_reader

# Parser/analysis library shared by the CLI and the tests.
LIB_SRC  := lvm_parser.cpp fft.cpp analysis.cpp
APP_SRC  := main.cpp $(LIB_SRC)
APP_OBJ  := $(APP_SRC:.cpp=.o)
HDRS     := lvm_parser.hpp fft.hpp analysis.hpp

ifeq ($(OS),Windows_NT)
    BIN      := $(TARGET).exe
    TEST_BIN := tests/run_tests.exe
    GUI_BIN  := lvm_viewer_gui.exe
else
    BIN      := $(TARGET)
    TEST_BIN := tests/run_tests
    GUI_BIN  := lvm_viewer_gui
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

$(TEST_BIN): tests/run_tests.cpp $(LIB_SRC) $(HDRS)
	$(CXX) $(CXXFLAGS) -I. -o $@ tests/run_tests.cpp $(LIB_SRC) $(LDFLAGS)

# Native Win32 GUI viewer (Windows only). Needs -municode for wWinMain and the
# Win32 import libraries. On Windows you can also run: powershell ./build_gui.ps1
gui: $(GUI_BIN)

$(GUI_BIN): gui_main.cpp $(LIB_SRC) $(HDRS)
	$(CXX) $(CXXFLAGS) -municode -mwindows -o $@ gui_main.cpp $(LIB_SRC) $(LDFLAGS) -lcomdlg32 -lgdi32 -luser32 -lgdiplus -lcomctl32

clean:
	rm -f $(APP_OBJ) $(BIN) $(TEST_BIN) $(GUI_BIN)
