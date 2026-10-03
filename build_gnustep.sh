#!/usr/bin/env bash
#
# build_gnustep.sh - Compila o Explorador de Collatz com GNUstep no Linux.
#
# O objetivo é validar, no Linux, o mesmo código Obj-C/AppKit que roda no
# macOS. O GNUstep fornece Foundation e AppKit, então o app compila e roda
# aqui; o binário final para o Mac continua sendo gerado pelo build_macos.sh.
#
# Requisitos (Debian/Ubuntu):
#   sudo apt install gnustep-devel gnustep-base-runtime gnustep-gui-runtime \
#                    libgnustep-gui-dev
#
# Uso:
#   ./build_gnustep.sh          compila em build-gnustep/CollatzGNUstep
#   ./build_gnustep.sh run      compila e executa
#   ./build_gnustep.sh clean    remove os artefatos
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/src"
MAC="$SRC/macos"
OUT="$ROOT/build-gnustep"
BIN="$OUT/CollatzGNUstep"

usage() { echo "Uso: $0 [run|clean]"; }

check_deps() {
    if ! command -v gnustep-config >/dev/null 2>&1; then
        echo "ERRO: gnustep-config não encontrado."
        echo "Instale com:"
        echo "  sudo apt install gnustep-devel gnustep-base-runtime \\"
        echo "                   gnustep-gui-runtime libgnustep-gui-dev"
        exit 1
    fi
}

build() {
    check_deps
    mkdir -p "$OUT"

    # Flags do Objective-C (runtime GNU) e flags comuns.
    # O motor C é compilado como C puro; só a interface é Objective-C, pois
    # -std=gnu11 com .c trataria tudo como C e o GNU runtime não aceitaria
    # as mensagens [obj ...].
    OBJCFLAGS="$(gnustep-config --objc-flags) -std=gnu11 -O2 -Wall -Wextra \
                -Wno-unused-parameter -Isrc -Isrc/macos -DGNUSTEP"
    GUILIBS="$(gnustep-config --gui-libs)"
    CFLAGS="-O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -Isrc"

    echo "==> Compilando motor C"
    gcc $CFLAGS -c "$SRC/collatz.c"      -o "$OUT/collatz.o"
    gcc $CFLAGS -c "$SRC/collatz_tree.c" -o "$OUT/collatz_tree.o"

    echo "==> Compilando interface (Objective-C / GNUstep)"
    gcc $OBJCFLAGS -c "$MAC/main.m"         -o "$OUT/main.o"
    gcc $OBJCFLAGS -c "$MAC/AppDelegate.m"  -o "$OUT/AppDelegate.o"
    gcc $OBJCFLAGS -c "$MAC/TreeView.m"     -o "$OUT/TreeView.o"
    gcc $OBJCFLAGS -c "$MAC/SequenceView.m" -o "$OUT/SequenceView.o"

    echo "==> Ligando"
    gcc "$OUT/main.o" "$OUT/AppDelegate.o" "$OUT/TreeView.o" \
        "$OUT/SequenceView.o" \
        "$OUT/collatz.o" "$OUT/collatz_tree.o" \
        -o "$BIN" $GUILIBS -lm

    echo "==> Pronto: $BIN"
    echo "    Execute com: $0 run"
}

run() {
    build
    echo "==> Abrindo o app (GNUstep)"
    DISPLAY="${DISPLAY:-:0}" "$BIN"
}

clean() {
    rm -rf "$OUT"
    echo "Artefatos removidos: $OUT"
}

cmd="${1:-build}"
case "$cmd" in
    clean) clean ;;
    run)   run ;;
    build) build ;;
    *)     usage; exit 1 ;;
esac
