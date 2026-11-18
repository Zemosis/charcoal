CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -pedantic -D_POSIX_C_SOURCE=200809L \
         -Iinclude -MMD -MP
LDLIBS = -lm -lpthread
RELEASE_FLAGS = -O2
TARGET = charcoal

# Install location, e.g. make install PREFIX=~/.local
PREFIX ?= /usr/local
BINDIR = $(DESTDIR)$(PREFIX)/bin

# ASan + UBSan in debug builds, only when their runtimes are installed
SAN_FLAGS := $(shell echo 'int main(void){return 0;}' | \
    $(CC) -x c -fsanitize=address,undefined -o /dev/null - 2>/dev/null && \
    echo -fsanitize=address,undefined)
DEBUG_FLAGS = -g -O0 $(SAN_FLAGS)
TEST_TARGET = build/test_runner

# FFmpeg, found through pkg-config (only the app needs it, not the tests)
FFMPEG_PKGS = libavformat libavcodec libavutil libswscale
FFMPEG_CFLAGS = $(shell pkg-config --cflags $(FFMPEG_PKGS) 2>/dev/null)
FFMPEG_LIBS = $(shell pkg-config --libs $(FFMPEG_PKGS) 2>/dev/null)

# Directories
SRC_DIR = src
TEST_DIR = tests
BUILD_DIR = build

# Pure code with no terminal or FFmpeg calls, shared by the app and the tests
CORE_SRCS = $(SRC_DIR)/buf.c $(SRC_DIR)/media/frame.c \
            $(wildcard $(SRC_DIR)/render/*.c) \
            $(SRC_DIR)/tui/screen.c $(SRC_DIR)/tui/draw.c $(SRC_DIR)/tui/theme.c
APP_SRCS = $(CORE_SRCS) $(SRC_DIR)/main.c $(SRC_DIR)/app.c \
           $(SRC_DIR)/media/decoder.c $(SRC_DIR)/tui/term.c \
           $(wildcard $(SRC_DIR)/tui/views/*.c)
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)

CORE_OBJS = $(CORE_SRCS:%.c=$(BUILD_DIR)/%.o)
APP_OBJS = $(APP_SRCS:%.c=$(BUILD_DIR)/%.o)
TEST_OBJS = $(TEST_SRCS:%.c=$(BUILD_DIR)/%.o)
DEPS = $(APP_OBJS:.o=.d) $(TEST_OBJS:.o=.d)

# Records the flags of the last build so switching debug/release rebuilds
FLAGS_FILE = $(BUILD_DIR)/flags

# Default target: debug build
all: debug

debug: CFLAGS += $(DEBUG_FLAGS)
debug: $(TARGET)

release: CFLAGS += $(RELEASE_FLAGS)
release: $(TARGET)

test: CFLAGS += $(DEBUG_FLAGS)
test: $(TEST_TARGET)
	./$(TEST_TARGET)

# Renders the samples and compares them with tests/golden/
golden: debug
	tests/golden.sh

$(TARGET): $(APP_OBJS) | check-ffmpeg
	$(CC) $(CFLAGS) -o $@ $(APP_OBJS) $(FFMPEG_LIBS) $(LDLIBS)

$(TEST_TARGET): $(CORE_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Only the decoder includes FFmpeg headers
$(BUILD_DIR)/$(SRC_DIR)/media/decoder.o: EXTRA_CFLAGS = $(FFMPEG_CFLAGS)

$(BUILD_DIR)/%.o: %.c $(FLAGS_FILE)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c $< -o $@

# Only touched when the flags change, so objects rebuild only then
$(FLAGS_FILE): FORCE
	@mkdir -p $(BUILD_DIR)
	@echo '$(CFLAGS)' | cmp -s - $@ || echo '$(CFLAGS)' > $@

# Installs the release build (make release && sudo make install)
install: CFLAGS += $(RELEASE_FLAGS)
install: $(TARGET)
	install -Dm755 $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	rm -f $(BINDIR)/$(TARGET)

check-ffmpeg:
	@pkg-config --exists $(FFMPEG_PKGS) || { \
	    echo "FFmpeg development files not found."; \
	    echo "Fedora: sudo dnf install ffmpeg-free-devel"; \
	    echo "Debian/Ubuntu: sudo apt install libavformat-dev libavcodec-dev libswscale-dev"; \
	    exit 1; }

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

FORCE:

-include $(DEPS)

.PHONY: all debug release test golden install uninstall clean check-ffmpeg FORCE
