APPNAME = skyrocket

# Skyrocket has positional fireworks audio via OpenAL. Sound is OPTIONAL:
# define RSS_USE_OPENAL and link OpenAL to enable it. Without it, the saver
# builds and runs muted (the SoundEngine becomes a silent stub).
#
# Native: enabled automatically if OpenAL is found via pkg-config.
# Browser: Emscripten ships an OpenAL implementation; enable explicitly.

OPENAL_CFLAGS := $(shell pkg-config --silence-errors --cflags openal)
OPENAL_LIBS   := $(shell pkg-config --silence-errors --libs openal)

# macOS: brew's openal-soft is keg-only, so plain pkg-config never finds it
# (this bites the CI macos job too, which does install it). Fall back to the
# keg path directly.
ifeq ($(OPENAL_LIBS),)
  BREW_OPENAL := $(firstword $(wildcard /opt/homebrew/opt/openal-soft /usr/local/opt/openal-soft))
  ifneq ($(BREW_OPENAL),)
    OPENAL_CFLAGS := -I$(BREW_OPENAL)/include
    OPENAL_LIBS   := -L$(BREW_OPENAL)/lib -lopenal -Wl,-rpath,$(BREW_OPENAL)/lib
  endif
endif

ifneq ($(OPENAL_LIBS),)
  SAVER_EXTRA_CFLAGS = -DRSS_USE_OPENAL $(OPENAL_CFLAGS)
  SAVER_EXTRA_LIBS   = $(OPENAL_LIBS)
endif

# Emscripten: -lopenal pulls in emscripten's OpenAL; headers are at AL/al.h.
# The define must be a COMPILE flag (EM_SAVER_EXTRA is link-only — with the
# define only there, soundEngine.cpp compiled as the muted stub and web
# skyrocket was silently soundless).
EM_SAVER_EXTRA_CFLAGS = -DRSS_USE_OPENAL
EM_SAVER_EXTRA = -lopenal

include ../../makefiles/make_saver.mk
