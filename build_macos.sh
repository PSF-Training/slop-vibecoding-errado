#!/usr/bin/env bash
#
# build_macos.sh - Compila o Explorador de Collatz para macOS e monta um
# bundle .app pronto para executar (duplo clique).
#
# Requer macOS com as Command Line Tools (clang). Nada de Xcode completo.
#
# Uso:
#   ./build_macos.sh          compila e cria build/CollatzMac.app
#   ./build_macos.sh run      compila, cria o .app e abre
#   ./build_macos.sh clean    remove os artefatos
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/src"
MAC="$SRC/macos"
OUT="$ROOT/build-macos"
APP="$OUT/CollatzMac.app"
BIN_NAME="CollatzMac"

CFLAGS=(-O2 -std=c11 -Wall -Wextra -Wno-unused-parameter)
OBJCFLAGS=(-O2 -fobjc-arc -Wall)
FRAMEWORKS=(-framework Cocoa -framework QuartzCore)

usage() {
    echo "Uso: $0 [run|clean]"
}

clean() {
    rm -rf "$OUT"
    echo "Artefatos removidos: $OUT"
}

build() {
    if [[ "$(uname -s)" != "Darwin" ]]; then
        echo "ERRO: este script precisa rodar no macOS (uname = $(uname -s))."
        exit 1
    fi

    mkdir -p "$OUT"
    mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

    echo "==> Compilando motor C (portável)"
    clang "${CFLAGS[@]}" -c "$SRC/collatz.c" -o "$OUT/collatz.o"
    clang "${CFLAGS[@]}" -c "$SRC/collatz_tree.c" -o "$OUT/collatz_tree.o"

    echo "==> Compilando interface AppKit (Objective-C)"
    clang "${OBJCFLAGS[@]}" -I"$SRC" -c "$MAC/main.m"         -o "$OUT/main.o"
    clang "${OBJCFLAGS[@]}" -I"$SRC" -I"$MAC" -c "$MAC/AppDelegate.m"  -o "$OUT/AppDelegate.o"
    clang "${OBJCFLAGS[@]}" -I"$SRC" -I"$MAC" -c "$MAC/TreeView.m"     -o "$OUT/TreeView.o"
    clang "${OBJCFLAGS[@]}" -I"$SRC" -I"$MAC" -c "$MAC/SequenceView.m" -o "$OUT/SequenceView.o"

    echo "==> Ligando"
    clang "${FRAMEWORKS[@]}" \
        "$OUT/main.o" "$OUT/AppDelegate.o" "$OUT/TreeView.o" \
        "$OUT/SequenceView.o" \
        "$OUT/collatz.o" "$OUT/collatz_tree.o" \
        -o "$APP/Contents/MacOS/$BIN_NAME"

    cp "$MAC/Info.plist" "$APP/Contents/Info.plist"

    # Ícone, se existir o SVG convertido (opcional).
    if [[ -f "$ROOT/assets/collatz.png" ]]; then
        cp "$ROOT/assets/collatz.png" "$APP/Contents/Resources/collatz.png"
    fi

    echo "==> Pronto: $APP"
}

cmd="${1:-build}"
case "$cmd" in
    clean) clean ;;
    run)   build; open "$APP" ;;
    build) build ;;
    *)     usage; exit 1 ;;
esac
