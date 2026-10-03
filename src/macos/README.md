# Explorador de Collatz — versão macOS (AppKit)

Este é um app **nativo de macOS** escrito em Objective-C/AppKit. Ele tem
**três abas**, iguais às da versão GTK3 do Linux:

1. **Árvore** — mostra a **árvore de Collatz com fusões**: cada número
   escolhido é uma folha no topo; quando dois caminhos chegam ao mesmo
   valor, passam a compartilhar o mesmo nó, formando o tronco comum até o
   `1`. Os caminhos digitados ficam destacados.
2. **Sequência** — painel animado 2D/3D da sequência de um número, em
   formato de **hélice** com nós pulsantes e setas coloridas pela
   paridade.
3. **Recordes** — varre uma faixa em paralelo e encontra o número que
   gera a sequência mais longa, com barra de progresso e botão Cancelar.

> **Importante:** o código Obj-C/AppKit foi feito para compilar tanto no
> macOS (Cocoa) quanto no Linux via **GNUstep**, que serve para validar a
> interface aqui mesmo. O binário final para o Mac continua sendo gerado
> pelo `build_macos.sh`.

## Requisitos

- macOS 11 (Big Sur) ou mais recente, Intel ou Apple Silicon.
- Command Line Tools do Xcode (só o `clang`, sem Xcode completo):

  ```bash
  xcode-select --install
  ```

## Compilar e executar

Na raiz do projeto:

```bash
make macos        # compila e cria build-macos/CollatzMac.app
make macos-run    # compila e abre o app
```

Ou direto:

```bash
./build_macos.sh run
```

O resultado é um bundle `CollatzMac.app` pronto para dar duplo clique e
arrastar para a pasta Aplicativos.

### Validar a interface no Linux (GNUstep)

Para testar a mesma interface Obj-C no Linux antes de ir ao Mac:

```bash
make gnustep       # compila em build-gnustep/CollatzGNUstep
make gnustep-run   # compila e abre
```

Requer `gnustep-devel gnustep-base-runtime gnustep-gui-runtime
libgnustep-gui-dev`.

## Como usar

### Aba Árvore

1. O campo aceita **um ou mais números**, separados por espaço, vírgula ou
   ponto e vírgula (ex.: `27, 97, 871, 41`).
2. Clique em **Desenhar árvore**. Cada número vira uma folha no topo.
3. Os caminhos descem e **se fundem** quando chegam ao mesmo valor,
   formando o tronco até o `1`.
4. Botões de **exemplo** preenchem rapidamente combinações prontas.
5. **Arraste** para mover o desenho e use a **roda do mouse** para dar zoom.
6. Passe o **mouse sobre um nó** para ver o número e a profundidade.

### Aba Sequência

1. Digite um número de partida (ex.: `27`) e clique em **Gerar sequência**.
2. A sequência aparece como uma **hélice 3D** que gira automaticamente.
3. **Arraste** no painel para girar manualmente.
4. Alterne **Modo 3D** e **Girar automaticamente** nos checkboxes.
5. Passe o **mouse sobre um nó** para ver valor e passo.

### Aba Recordes

1. Informe **Início** e **Fim** da faixa (ex.: `1` a `1000000`).
2. Ajuste **Threads** e, se quiser, o **limite de passos por número**.
3. Clique em **Buscar recorde** e acompanhe a barra de progresso.
4. **Cancelar** interrompe a busca; o melhor resultado até então é mantido.

### Cores

- **Laranja** — passo que cresce (`3n+1`, número ímpar).
- **Verde** — passo que cai (`n/2`, número par) **e** os nós de fusão
  (valores por onde passam mais de um caminho).
- Os nós das **folhas** (topo) usam a cor de cada número escolhido.
- O caminho do número digitado aparece **destacado**; o restante da
  árvore fica mais apagado.

## Opções de inicialização

Além de abrir normalmente, o app aceita variáveis de ambiente úteis para
demonstração e teste:

```bash
# Abre já na aba Sequência com o número 27
COLLATZ_TAB=1 COLLATZ_DEMO=27 ./build-gnustep/CollatzGNUstep

# Abas: 0 = Árvore, 1 = Sequência, 2 = Recordes
COLLATZ_TAB=2 ./build-gnustep/CollatzGNUstep
```

## Visual macOS

A janela usa os elementos nativos do sistema:

- Title bar unificada e transparente (`fullSizeContentView`).
- Cores semânticas do sistema, com suporte automático a **modo claro/escuro**.
- Controles nativos: `NSTextField`, `NSButton`, `NSSegmentedControl`,
  `NSProgressIndicator`.

## Estrutura

```
src/macos/
├── main.m          ponto de entrada e opções de inicialização
├── AppDelegate.h   declaração do delegate
├── AppDelegate.m   janela e as três abas (Árvore, Sequência, Recordes)
├── TreeView.h      interface da view de colunas em zigue-zague
├── TreeView.m      desenho e animação das colunas
├── SequenceView.h  interface da view da sequência animada
├── SequenceView.m  desenho e animação da hélice 2D/3D
├── GNUstepCompat.h macros de compatibilidade macOS/GNUstep
└── Info.plist      metadados do bundle .app
```

O motor de cálculo fica em `../collatz_tree.c` (caminhos) e `../collatz.c`
(sequências e recordes), reaproveitados por todas as plataformas.

## Observação sobre comprimento

Números com centenas de passos (como o recordista `837799`, com 524) são
comprimidos verticalmente por uma curva suave, para caber na tela sem
perder a forma. Ajuste o limite em `TreeView.m` (`_wrapLen`, padrão 160).
