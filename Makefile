CC ?= cc
CPPFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
LDFLAGS ?=
LDLIBS += -lz -lm
# -pthread must reach BOTH the compile and link steps. It is placed directly on
# the recipes (not tucked into CFLAGS/LDLIBS) so it survives a command-line
# `make CFLAGS=...` override, which would otherwise replace CFLAGS wholesale.

BIN = kilix-rancher
SRC = src/main.c src/game.c src/render.c src/term.c src/sound.c
OBJ = $(SRC:.c=.o)
DEP = $(OBJ:.o=.d)

PREFIX ?= /usr/local
DESTDIR ?=
ASSET_DEST = $(DESTDIR)$(PREFIX)/share/kilix-rancher/assets
IMAGE_ASSETS = assets/kilix.ppm assets/backgrounds/ranch.ppm \
	assets/backgrounds/arena.ppm \
	assets/opponents/mossnub.ppm assets/opponents/dewdrop.ppm \
	assets/opponents/mistwing.ppm assets/opponents/stonecalf.ppm \
	assets/opponents/moonmoth.ppm assets/opponents/duskcub.ppm
SFX_ASSETS = assets/sfx/move.wav assets/sfx/confirm.wav \
	assets/sfx/train.wav assets/sfx/hit.wav assets/sfx/win.wav \
	assets/sfx/lose.wav
RUNTIME_ASSETS = $(IMAGE_ASSETS) $(SFX_ASSETS)

all: $(BIN)

# $(CFLAGS) is repeated on the link line so a sanitizer/coverage build
# (make CFLAGS="-fsanitize=address ...") links its runtime correctly.
$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -pthread -o $@ $(OBJ) $(LDLIBS)

# -MMD -MP records each object's real header dependencies (in $(DEP)); the
# explicit kilix.h prerequisite covers the very first, pre-.d build.
src/%.o: src/%.c src/kilix.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -MMD -MP -c -o $@ $<

-include $(DEP)

validate-assets: $(BIN) $(RUNTIME_ASSETS)
	./$(BIN) --validate-assets
	@set -eu; \
	for sound in $(SFX_ASSETS); do \
		test "$$(dd if="$$sound" bs=1 count=4 2>/dev/null)" = RIFF; \
		test "$$(dd if="$$sound" bs=1 skip=8 count=4 2>/dev/null)" = WAVE; \
	done

test: $(BIN) validate-assets
	./$(BIN) --selftest 1337 480
	./$(BIN) --selftest 42 240
	@set -eu; \
	render_dir=$$(mktemp -d); \
	trap 'rm -rf "$$render_dir"' EXIT HUP INT TERM; \
	./$(BIN) --render-test "$$render_dir" 1337; \
	set -- "$$render_dir"/render_*.ppm; \
	test "$$#" -eq 12; \
	for image do test -s "$$image"; done

audio:
	./tools/generate_audio.sh

install: $(BIN) validate-assets
	install -Dm755 $(BIN) "$(DESTDIR)$(PREFIX)/bin/$(BIN)"
	install -d -m755 "$(ASSET_DEST)" "$(ASSET_DEST)/backgrounds" \
		"$(ASSET_DEST)/opponents" "$(ASSET_DEST)/sfx"
	install -m644 assets/kilix.ppm "$(ASSET_DEST)/"
	install -m644 assets/backgrounds/*.ppm "$(ASSET_DEST)/backgrounds/"
	install -m644 assets/opponents/*.ppm "$(ASSET_DEST)/opponents/"
	install -m644 $(SFX_ASSETS) "$(ASSET_DEST)/sfx/"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/$(BIN)"
	rm -rf "$(DESTDIR)$(PREFIX)/share/kilix-rancher"

clean:
	rm -f $(OBJ) $(DEP) $(BIN)
	rm -rf .render-test

.PHONY: all validate-assets test audio install uninstall clean
