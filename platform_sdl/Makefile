# phoenixomatic — desktop and web builds of the same C++ the Cardputer runs.
#
#   make          desktop binary
#   make run      build and launch it
#   make wasm     web build via the emscripten docker image
#                 (EMCC_NATIVE=1 to use an em++ already on PATH)
#   make bundle   macOS .app
#
# Needs SDL2 (brew install sdl2). SDL2_gfx is deliberately not used.

CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -I..

SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS := $(shell sdl2-config --libs 2>/dev/null)

ifeq ($(strip $(SDL_CFLAGS)),)
  SDL_CFLAGS := -I/opt/homebrew/include/SDL2 -D_THREAD_SAFE
  SDL_LIBS := -L/opt/homebrew/lib -lSDL2
endif

UNAME_S := $(shell uname -s)
# The audio-output menu is a macOS menu bar, so it is Objective-C++ and only
# builds there. Everywhere else installAudioMenu is an empty function.
ifeq ($(UNAME_S),Darwin)
  PLATFORM_SOURCES := mac_menu.mm
  PLATFORM_LIBS := -framework Cocoa
endif

TARGET := phoenixomatic

SOURCES := \
  ../display.cpp \
  ../src/core/model.cpp \
  ../src/dsp/chaos_osc.cpp \
  ../src/dsp/osc.cpp \
  ../src/dsp/drum_voices.cpp \
  ../src/dsp/filter.cpp \
  ../src/dsp/delay.cpp \
  ../src/dsp/space.cpp \
  ../src/dsp/phoenix_engine.cpp \
  ../src/ui/text_screen.cpp \
  ../src/ui/phoenix_display.cpp \
  ../src/ui/components/mod_bank_view.cpp \
  ../src/ui/components/phoenix_sprite.cpp \
  ../src/ui/pages/home_page.cpp \
  ../src/ui/pages/chaos_page.cpp \
  ../src/ui/pages/osc_page.cpp \
  ../src/ui/pages/seq_page.cpp \
  ../src/ui/pages/logic_page.cpp \
  ../src/ui/pages/filter_page.cpp \
  ../src/ui/pages/drum_page.cpp \
  ../src/ui/pages/delay_page.cpp \
  ../src/ui/pages/space_page.cpp \
  ../src/ui/pages/mix_page.cpp \
  ../src/ui/pages/config_page.cpp \
  ../src/ui/pages/project_page.cpp \
  sdl_display.cpp \
  sdl_audio.cpp \
  sdl_main.cpp

HEADERS := $(shell find .. -name '*.h' -not -path '../web/*' 2>/dev/null)

ROOT := $(abspath ..)
DOCKER ?= docker
EMCC_IMAGE ?= emscripten/emsdk
WASM_FLAGS := -I.. -std=c++17 -O2 -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 \
              -sSTACK_SIZE=262144 -sEXPORTED_RUNTIME_METHODS='[UTF8ToString]'

APP_BUNDLE := phoenixomatic.app
SDL2_DYLIB := /opt/homebrew/opt/sdl2/lib/libSDL2-2.0.0.dylib

all: $(TARGET)

$(TARGET): $(SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $(SOURCES) $(PLATFORM_SOURCES) \
	  $(SDL_LIBS) $(PLATFORM_LIBS) -o $@

run: $(TARGET)
	./$(TARGET)

# EMCC_NATIVE=1 uses an em++ already on PATH instead of the docker image —
# that is how CI builds it, where the toolchain is already installed and
# nesting docker inside the runner buys nothing. Same sources, same flags,
# one place to change them.
ifeq ($(EMCC_NATIVE),1)
  EM_RUN :=
  EM_SHELL := shell.html
  EM_OUT := $(ROOT)/web/phoenixomatic.html
else
  EM_RUN := $(DOCKER) run --rm -v $(ROOT):/src -w /src/platform_sdl $(EMCC_IMAGE)
  EM_SHELL := /src/platform_sdl/shell.html
  EM_OUT := /src/web/phoenixomatic.html
endif

wasm: $(SOURCES)
	mkdir -p $(ROOT)/web
	$(EM_RUN) em++ $(SOURCES) $(WASM_FLAGS) --shell-file $(EM_SHELL) -o $(EM_OUT)

serve: wasm
	cd $(ROOT)/web && python3 -m http.server 8080

bundle: $(TARGET)
	mkdir -p $(APP_BUNDLE)/Contents/MacOS $(APP_BUNDLE)/Contents/Frameworks
	cp $(TARGET) $(APP_BUNDLE)/Contents/MacOS/
	cp $(SDL2_DYLIB) $(APP_BUNDLE)/Contents/Frameworks/
	install_name_tool -change $(SDL2_DYLIB) \
	  @executable_path/../Frameworks/libSDL2-2.0.0.dylib \
	  $(APP_BUNDLE)/Contents/MacOS/$(TARGET)
	codesign --force --sign - $(APP_BUNDLE)/Contents/Frameworks/libSDL2-2.0.0.dylib
	codesign --force --sign - $(APP_BUNDLE)/Contents/MacOS/$(TARGET)
	printf '%s\n' \
	  '<?xml version="1.0" encoding="UTF-8"?>' \
	  '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	  '<plist version="1.0"><dict>' \
	  '<key>CFBundleExecutable</key><string>$(TARGET)</string>' \
	  '<key>CFBundleIdentifier</key><string>org.modularcore.phoenixomatic</string>' \
	  '<key>CFBundleName</key><string>Phoenixomatic</string>' \
	  '<key>CFBundleVersion</key><string>0.1</string>' \
	  '<key>CFBundlePackageType</key><string>APPL</string>' \
	  '</dict></plist>' > $(APP_BUNDLE)/Contents/Info.plist
	@echo "open $(APP_BUNDLE)"

clean:
	rm -f $(TARGET)
	rm -rf $(APP_BUNDLE)

.PHONY: all run wasm serve bundle clean
