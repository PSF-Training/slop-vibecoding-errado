# Explorador de Collatz

Explorador da conjectura de Collatz com **três interfaces**:

- **Linux (GTK3)** — painel animado 2D/3D estilo engine de jogos.
- **Windows (Win32)** — versão via `winegcc`, roda no Wine.
- **macOS (AppKit)** — app nativo com as mesmas três abas.

O cálculo é feito por um mesmo motor em C, com threads e memoização.

## O que ele faz

**Aba Árvore** — digite um ou mais números e veja a **árvore de Collatz**
com fusões: cada número é uma folha no topo; os caminhos descem e, quando
chegam ao mesmo valor, passam a compartilhar o **mesmo nó**, formando o
tronco comum até o `1` (como na Collatz tree clássica). Os caminhos dos
números digitados ficam destacados. Arraste para mover, role para dar
zoom e passe o mouse sobre um nó para ver o valor e a profundidade.

**Aba Sequência** — digite um número de partida (ex.: `27`) e veja a
sequência como um **painel animado 2D/3D**, estilo engine de jogos:

- Cada valor vira um **nó brilhante** (bolha pulsante com halo).
- As transições viram **setas coloridas pela paridade**: azul para
  `n/2` (par) e laranja para `3n+1` (ímpar).
- A trajetória é disposta numa **hélice 3D** que gira automaticamente,
  com perspectiva e profundidade.
- **Arraste** no painel para girar manualmente; **passe o mouse** sobre
  um nó para ver o valor e o passo.
- Alternadores "Modo 3D" e "Girar automaticamente" no rodapé.

Também mostra estatísticas: quantidade de passos e maior valor atingido.
Usa aritmética de 128 bits, então avisa com segurança se algum valor
ultrapassar 64 bits em vez de travar ou dar resultado errado.

**Aba Recordes** — varre uma faixa (ex.: 1 a 1.000.000) e encontra o
número que gera a sequência mais longa. A varredura roda em paralelo em
vários núcleos, com barra de progresso e botão Cancelar. Há um limite de
passos por número para nunca travar em valores patológicos.

**Versão macOS** — as **mesmas três abas** da versão Linux: árvore de
Collatz com fusões (Árvore), painel animado 2D/3D da sequência
(Sequência) e busca de recordes em paralelo (Recordes). Detalhes em
[`src/macos/README.md`](src/macos/README.md).

## Linha de comando

```bash
./build/collatz         # abre a interface
./build/collatz 27      # já abre com a sequência do 27
./build/collatz --demo 27   # idem (útil para demonstrar/cmparar)
```

## Requisitos

- gcc, make
- Headers de desenvolvimento do GTK3:

```bash
sudo apt install libgtk-3-dev
```

## Compilar e executar

```bash
make          # compila em build/collatz
make run      # compila e abre a janela
```

## Versão Windows (via winegcc + Wine)

Além da versão GTK3 para Linux, o projeto tem uma **porta Win32 nativa**
(GDI) que compila com o `winegcc` — sem precisar instalar MinGW ou o GTK
para Windows. O motor de cálculo (`collatz.c`) é 100% reutilizado.

```bash
make win        # compila a versão Windows em build/collatz.exe
make win-run    # compila e roda no Wine
make win-test   # roda os testes do motor compilado para Windows/Wine
```

A versão Windows abre uma janela com campo de número, botão **Gerar
sequencia**, os alternadores **Modo 3D** / **Girar automaticamente**, o
painel animado (mesma hélice 3D) e uma seção de **Buscar recorde** com
barra de progresso. Também aceita um número na linha de comando:

```bash
wine build/collatz.exe 27
```

**Observação importante sobre o `winegcc`:** ele não gera um `.exe`
Windows portável de verdade. Ele produz um pequeno script `collatz.exe`
que aponta para um `collatz.exe.so` (binário para Linux que usa as DLLs
internas do Wine). Ou seja: essa versão **roda no Wine**, mas não é um
executável para levar a um Windows real. Para um `.exe` nativo de Windows
seria preciso compilar com MinGW + uma build do GTK3 (ou Win32) para
Windows — o que exige instalar dependências que não vêm no Debian.

## Instalar no menu de aplicativos (com ícone clicável)

```bash
make install
```

Isso copia o binário, o ícone e um atalho `.desktop` para `~/.local`.
Depois é só procurar por **"Explorador de Collatz"** no menu de
aplicativos — o ícone aparece lá e é clicável.

Para remover:

```bash
make uninstall
```

## Detalhes de desempenho

- **Multithreading** com pthreads: a faixa é dividida entre os núcleos.
- **Memoização**: um cache guarda os passos já calculados de valores
  visitados, evitando recomputar caudas inteiras da sequência.
- **Sem travar a interface**: a busca acontece fora da thread principal e
  reporta progresso via `g_idle_add`.

## Estrutura

```
├── Makefile
├── build_macos.sh          # compila o app macOS (roda no Mac)
├── build_gnustep.sh        # compila a interface AppKit no Linux (GNUstep)
├── assets/collatz.svg      # ícone (vetorial)
├── src/
│   ├── collatz.h           # API do motor de cálculo
│   ├── collatz.c           # geração, contagem, cache e busca paralela
│   ├── collatz_tree.h/c    # árvore e caminhos (colunas) de Collatz
│   ├── thread_compat.h     # threads portáveis (pthreads / Win32)
│   ├── main.c              # interface GTK3 (Linux)
│   ├── win32_main.c        # interface Win32/GDI (Windows via Wine)
│   └── macos/              # app nativo AppKit (macOS/GNUstep)
│       ├── AppDelegate.m   # janela e abas Árvore/Sequência/Recordes
│       ├── TreeView.m      # colunas em zigue-zague
│       ├── SequenceView.m  # hélice animada 2D/3D
│       └── GNUstepCompat.h # ponte macOS/GNUstep
├── tools/
│   └── render_graph.c      # pré-visualização da árvore (SVG, Linux)
└── tests/
    ├── test_collatz.c      # testes do motor (Linux)
    ├── test_tree.c         # testes da árvore/caminhos (Linux)
    └── test_win32.c        # testes no Windows/Wine
```

## macOS (AppKit)

App nativo com as três abas (Árvore, Sequência, Recordes). **Precisa ser
compilado no macOS** para gerar o bundle final (usa Cocoa/AppKit). O motor
em C é o mesmo das outras versões:

```bash
make macos        # cria build-macos/CollatzMac.app
make macos-run    # compila e abre
```

A mesma interface Obj-C/AppKit pode ser compilada e testada no **Linux via
GNUstep** (antes de ir ao Mac):

```bash
make gnustep       # compila em build-gnustep/CollatzGNUstep
make gnustep-run   # compila e abre
```

Veja [`src/macos/README.md`](src/macos/README.md) para os detalhes.

## Testes

```bash
make test         # roda os testes do motor e da árvore (Linux)
make win-test     # roda os testes com o motor compilado para Windows/Wine
```

## Pré-visualizar a árvore com fusões (sem GUI)

Há um utilitário que usa a **mesma** função de layout
(`collatz_graph_build`) que as interfaces e gera um SVG:

```bash
gcc -O2 -std=c11 -Isrc tools/render_graph.c src/collatz_tree.c -o build/render_graph -lm
./build/render_graph 5461 5460 5456 909 908 151 > arvore.svg
```

A **Árvore** mostra cada número escolhido como uma folha; quando dois
caminhos chegam ao mesmo valor, passam a compartilhar o mesmo nó (fusão),
formando o tronco comum até o `1` — exatamente como na Collatz tree
clássica. Os caminhos dos números digitados ficam destacados.

> A antiga API `collatz_paths_build` (colunas independentes, sem fusão)
> continua no motor por compatibilidade, mas não é mais usada pelo app.
