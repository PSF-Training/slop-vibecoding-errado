# Makefile - Explorador de Collatz (GTK3 / Win32)
#
# Alvos:
#   make            compila o executável em build/collatz
#   make run        compila (se preciso) e executa
#   make win        compila a versão Windows (.exe) com winegcc
#   make win-run    compila e roda a versão Windows usando wine
#   make win-test   compila e roda os testes do motor no Windows/Wine
#   make macos      compila o app macOS (precisa rodar no macOS)
#   make macos-run  compila e abre o app macOS
#   make gnustep    compila a interface Obj-C/AppKit no Linux (GNUstep)
#   make gnustep-run compila e abre a versão GNUstep
#   make test       roda os testes do motor e da árvore (Linux)
#   make icon       gera assets/collatz.png (ícone PNG)
#   make install    instala em ~/.local (atalho + ícone)
#   make uninstall  remove a instalação
#   make clean      remove artefatos de build

CC      ?= gcc
PKGS     = gtk+-3.0
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lpthread -lm

SRC      = src/main.c src/collatz.c src/collatz_tree.c
OBJ      = $(SRC:src/%.c=build/%.o)
BIN      = build/collatz

# --- Versão Windows (Win32/GDI via winegcc) ---
WINCC    ?= winegcc
WINBIN    = build/collatz.exe
WINTESTBIN = build/test_win32.exe
WINSRC    = src/win32_main.c src/collatz.c src/collatz_tree.c
WINLDLIBS = -lgdi32 -luser32 -lm
WINE      = wine

PREFIX  ?= $(HOME)/.local
BINDIR   = $(PREFIX)/bin
APPDIR   = $(PREFIX)/share/applications
ICONDIR  = $(PREFIX)/share/icons/hicolor

.PHONY: all run icon install uninstall clean win win-run win-clean win-test \
        macos macos-run gnustep gnustep-run gnustep-clean test tree-test

all: $(BIN)

build/%.o: src/%.c src/collatz.h src/thread_compat.h | build
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDLIBS)

build:
	mkdir -p build

run: $(BIN)
	./$(BIN)

# --- Windows -------------------------------------------------------
# Compila com winegcc. Observação: winegcc gera um .exe (script) que
# aponta para o .exe.so compilado; ambos ficam em build/.
win: $(WINBIN)

$(WINBIN): $(WINSRC) src/collatz.h src/thread_compat.h | build
	$(WINCC) -O2 $(WINSRC) -o $@ $(WINLDLIBS)

win-run: $(WINBIN)
	-$(WINE) $(WINBIN)

win-test: $(WINTESTBIN)
	-$(WINE) $(WINTESTBIN)

$(WINTESTBIN): tests/test_win32.c src/collatz.c src/collatz.h src/thread_compat.h | build
	$(WINCC) -O2 -Isrc tests/test_win32.c src/collatz.c -o $@ -lm

win-clean:
	rm -f build/collatz.exe build/collatz.exe.so \
	      build/test_win32.exe build/test_win32.exe.so
	rm -rf tmp????????

# --- Testes (Linux) ------------------------------------------------
test: tree-test
	$(CC) -O2 -std=c11 -Wall -Isrc tests/test_collatz.c src/collatz.c \
		-o build/test_collatz -lpthread -lm
	./build/test_collatz

tree-test:
	$(CC) -O2 -std=c11 -Wall -Isrc tests/test_tree.c src/collatz_tree.c \
		src/collatz.c -o build/test_tree -lpthread -lm
	./build/test_tree

# --- macOS (precisa rodar no macOS) --------------------------------
macos:
	./build_macos.sh build

macos-run:
	./build_macos.sh run

# --- GNUstep (valida o app ObjC/AppKit no Linux) --------------------
gnustep:
	./build_gnustep.sh build

gnustep-run:
	./build_gnustep.sh run

gnustep-clean:
	./build_gnustep.sh clean


# Gera um PNG a partir do SVG (usa ImageMagick ou rsvg-convert).
icon: assets/collatz.png

assets/collatz.png: assets/collatz.svg
	@if command -v rsvg-convert >/dev/null 2>&1; then \
		rsvg-convert -w 256 -h 256 $< -o $@; \
	elif command -v magick >/dev/null 2>&1; then \
		magick -background none $< -resize 256x256 $@; \
	else \
		convert -background none $< -resize 256x256 $@; \
	fi
	@echo "Ícone gerado: $@"

install: $(BIN) icon
	mkdir -p $(BINDIR) $(APPDIR) $(ICONDIR)/256x256/apps $(ICONDIR)/scalable/apps
	install -m 755 $(BIN) $(BINDIR)/collatz
	install -m 644 assets/collatz.svg $(ICONDIR)/scalable/apps/collatz.svg
	install -m 644 assets/collatz.png $(ICONDIR)/256x256/apps/collatz.png
	printf '%s\n' \
	  '[Desktop Entry]' \
	  'Type=Application' \
	  'Name=Explorador de Collatz' \
	  'Comment=Gere e visualize a sequência de Collatz' \
	  'Exec=$(BINDIR)/collatz' \
	  'Icon=collatz' \
	  'Terminal=false' \
	  'Categories=Utility;Education;Science;Math;' \
	  > $(APPDIR)/collatz.desktop
	update-desktop-database $(APPDIR) 2>/dev/null || true
	gtk-update-icon-cache -f -t $(ICONDIR) 2>/dev/null || true
	@echo "Instalado. Procure por 'Explorador de Collatz' no menu de aplicativos."

uninstall:
	rm -f $(BINDIR)/collatz
	rm -f $(APPDIR)/collatz.desktop
	rm -f $(ICONDIR)/scalable/apps/collatz.svg
	rm -f $(ICONDIR)/256x256/apps/collatz.png
	update-desktop-database $(APPDIR) 2>/dev/null || true
	gtk-update-icon-cache -f -t $(ICONDIR) 2>/dev/null || true
	@echo "Desinstalado."

clean:
	rm -rf build tmp???????? assets/collatz.png
