CSTD    := -std=c11
WARN    := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes
OPT     ?= -O2
DEBUG   ?= -g

SRC_DIR := src
OBJ_DIR := build/obj
BIN_DIR := build

UNAME_S := $(shell uname -s)

PKG_CFLAGS := $(shell pkg-config --cflags glfw3)
PKG_LDLIBS := $(shell pkg-config --libs glfw3)

ifeq ($(UNAME_S),Darwin)
    CC          ?= clang
    PLAT_LDLIBS := -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo \
                   -framework CoreAudio -framework AudioToolbox -framework AudioUnit -framework CoreFoundation
    TARGET      := $(BIN_DIR)/blockworld
else ifeq ($(UNAME_S),Linux)
    CC          ?= gcc
    PLAT_LDLIBS := -lGL -lasound -lpthread -lm -ldl
    TARGET      := $(BIN_DIR)/blockworld
else
    # Windows (MinGW-w64 under MSYS2)
    CC          ?= gcc
    PLAT_LDLIBS := -lopengl32 -lwinmm -lpthread -lgdi32 -lm
    TARGET      := $(BIN_DIR)/blockworld.exe
endif

CFLAGS  := $(CSTD) $(WARN) $(OPT) $(DEBUG) -Iinclude $(PKG_CFLAGS)
LDFLAGS :=
LDLIBS  := $(PKG_LDLIBS) $(PLAT_LDLIBS)

# All .c under src/ are auto-discovered. The three audio_*.c backends are each
# guarded by a platform #if, so the two that don't match the host compile to an
# empty stub and only the host backend provides audio_backend_start/stop.
SRCS := $(shell find $(SRC_DIR) -name '*.c')
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BIN_DIR):
	@mkdir -p $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf build

-include $(DEPS)