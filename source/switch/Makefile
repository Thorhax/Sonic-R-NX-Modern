# Sonic R Nintendo Switch build (devkitA64 + libnx)

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

include $(DEVKITPRO)/libnx/switch_rules

TARGET        := sonicr
BUILDDIR      := build
SWITCH_SRCDIR := src
SDL_SRCDIR    := ../sdl/src

APP_TITLE     := Sonic R
APP_AUTHOR    := Thorhax
APP_VERSION   := 1.0.0
ICON          := icon.jpg

# Files in sdl/src that Switch replaces
SDL_REPLACED  := \
    $(SDL_SRCDIR)/r_gl_backend.c \
    $(SDL_SRCDIR)/render_gl.c \
    $(SDL_SRCDIR)/r_soft_backend.c \
    $(SDL_SRCDIR)/platform/platform_sdl.c

SDL_ALL_C     := $(shell find $(SDL_SRCDIR) -name '*.c')
SDL_C         := $(filter-out $(SDL_REPLACED), $(SDL_ALL_C))

SWITCH_ALL_C  := $(shell find $(SWITCH_SRCDIR) -name '*.c')
SWITCH_C      := $(SWITCH_ALL_C)

# Object files
SDL_OBJS      := $(patsubst ../%.c,$(BUILDDIR)/%.o,$(SDL_C))
SWITCH_OBJS   := $(patsubst %.c,$(BUILDDIR)/switch/%.o,$(SWITCH_C))
OBJS          := $(SDL_OBJS) $(SWITCH_OBJS)

ARCH          := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

INCLUDES      := -I$(SWITCH_SRCDIR) -I$(SDL_SRCDIR) \
                 -I$(PORTLIBS)/include \
                 -I$(PORTLIBS)/include/SDL2 \
                 -I$(LIBNX)/include

CFLAGS        := -g -O2 -Wall -Wextra $(ARCH) $(INCLUDES) \
                 -D__SWITCH__ -DSONICR_SDL -DSONICR_GLES2 -DSONICR_MATCHMAKER \
                 -Wno-unused-variable -Wno-unused-function -Wno-unused-parameter \
                 -Wno-strict-aliasing -Wno-parentheses

CXXFLAGS      := $(CFLAGS) -fno-rtti -fno-exceptions

LIBDIRS       := -L$(PORTLIBS)/lib -L$(LIBNX)/lib

LIBS          := -lSDL2_mixer -lSDL2 -lGLESv2 -lEGL -lglapi -ldrm_nouveau \
                 -lvorbisidec -logg -lmodplug -lmpg123 -lopusfile -lopus \
                 -lminiupnpc -lstdc++ -lpthread -lnx -lm

LDFLAGS       := -specs=$(LIBNX)/switch.specs -g $(ARCH) $(LIBDIRS)

.PHONY: all clean

all: $(TARGET).nro

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp
	elf2nro $< $@ --nacp=$(TARGET).nacp --icon=$(ICON)

$(TARGET).nacp:
	nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(BUILDDIR)/sdl/%.o: ../sdl/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/switch/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILDDIR) $(TARGET).elf $(TARGET).nro $(TARGET).nacp
