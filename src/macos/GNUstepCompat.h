//
//  GNUstepCompat.h
//  Explorador de Collatz - camada de compatibilidade macOS / GNUstep
//
//  O app é escrito uma vez e compila nos dois ambientes:
//
//    - macOS (AppKit, clang + ARC): usa as APIs modernas.
//    - GNUstep (Linux, gcc + runtime GNU sem ARC): usa equivalentes
//      clássicos que existem nos dois.
//
//  Como o runtime GNU não tem ARC, o código Obj-C deste app faz
//  gerenciamento manual (retain/release). No macOS isso continua sendo
//  válido (ARC não é exigido).
//

#ifndef COLLATZ_GNUSTEP_COMPAT_H
#define COLLATZ_GNUSTEP_COMPAT_H

// Detecta o ambiente.
#if defined(GNUSTEP) || defined(__GNUstep__) || defined(GNUSTEP_BASE_LIBRARY)
#define COLLATZ_GNUSTEP 1
#else
#define COLLATZ_GNUSTEP 0
#endif

#if COLLATZ_GNUSTEP

// ---- Macros para literais (GNU runtime não aceita @[] / @{}) --------
//   Cria um NSArray a partir de uma lista, ex.:
//     NSArray *a = ARRAY(@"x", @"y");
#define ARRAY(...) [NSArray arrayWithObjects:__VA_ARGS__, nil]
//   Cria um NSDictionary. Como a lista termina em nil, não dá para usar
//   variádica aqui; use DICT2(objeto, chave) para pares:
//     NSDictionary *d = DICT2(@"texto", NSFontAttributeName);
#define DICT1(o1, k1) \
    [NSDictionary dictionaryWithObjectsAndKeys:(o1), (k1), nil]
#define DICT2(o1, k1, o2, k2) \
    [NSDictionary dictionaryWithObjectsAndKeys:(o1), (k1), (o2), (k2), nil]

// ---- Fábricas de controles (equivalentes clássicos) -----------------
// Label simples não editável.
#define MAKE_LABEL(str) \
    ({ NSTextField *_t = [[NSTextField alloc] initWithFrame:NSZeroRect]; \
       [_t setStringValue:(str)]; [_t setBezeled:NO]; \
       [_t setEditable:NO]; [_t setSelectable:NO]; \
       [_t setDrawsBackground:NO]; _t; })

// Label multilinha (quebra de linha).
#define MAKE_WRAP_LABEL(str) \
    ({ NSTextField *_t = [[NSTextField alloc] initWithFrame:NSZeroRect]; \
       [_t setStringValue:(str)]; [_t setBezeled:NO]; \
       [_t setEditable:NO]; [_t setSelectable:NO]; \
       [_t setDrawsBackground:NO]; \
       [[_t cell] setWraps:YES]; _t; })

// Fonte monoespaçada com peso (o GNUstep não tem a variante por peso).
#define MONO_FONT_BOLD(size) \
    ([NSFont boldSystemFontOfSize:(size)])
#define MONO_FONT(size) \
    ([NSFont userFixedPitchFontOfSize:(size)])

// Fonte de sistema com peso.
#define SYS_FONT_BOLD(size) ([NSFont boldSystemFontOfSize:(size)])
#define SYS_FONT(size) ([NSFont systemFontOfSize:(size)])

// Cor cinza com transparência.
#define GRAY_COLOR(w, a) [NSColor colorWithCalibratedWhite:(w) alpha:(a)]

// Estilo de botão arredondado (nomes diferentes entre GNUstep e AppKit).
#if COLLATZ_GNUSTEP
#define BEZEL_ROUNDED NSRoundedBezelStyle
#else
#define BEZEL_ROUNDED NSBezelStyleRounded
#endif

// Layer-backed views: o GNUstep não tem setWantsLayer:/layer.
#if COLLATZ_GNUSTEP
#define WANT_LAYER(v) ((void)0)
#else
#define WANT_LAYER(v) [(v) setWantsLayer:YES]
#endif

// Tipos de botão/indicador: os nomes diferem entre GNUstep e AppKit.
#if COLLATZ_GNUSTEP
#define COLLATZ_SWITCH_BUTTON NSSwitchButton
#define COLLATZ_BAR_STYLE NSProgressIndicatorBarStyle
#define COLLATZ_STATE_ON NSOnState
#else
#define COLLATZ_SWITCH_BUTTON NSButtonTypeSwitch
#define COLLATZ_BAR_STYLE NSProgressIndicatorStyleBar
#define COLLATZ_STATE_ON NSControlStateValueOn
#endif

// Tempo monotônico em segundos (portável entre Apple e GNUstep).
#define COLLATZ_TIME() ((double)[NSDate timeIntervalSinceReferenceDate])

// #pragma mark só é entendido pelo clang; no GCC é ignorado com aviso.
#if COLLATZ_GNUSTEP
#define MARK(label) /* nada */
#else
#define MARK(label) @pragma mark label
#endif

// Garante que o NSApplication exista antes de usar APIs que precisam do
// backend (fontes, imagens). Chame no início do main.
#define ENSURE_APP_KIT() \
    do { (void)[NSApplication sharedApplication]; } while (0)

#else /* macOS / AppKit nativo */

#define ARRAY(...) @[__VA_ARGS__]
#define DICT1(o1, k1) @{(k1): (o1)}
#define DICT2(o1, k1, o2, k2) @{(k1): (o1), (k2): (o2)}

#define MAKE_LABEL(str) [NSTextField labelWithString:(str)]
#define MAKE_WRAP_LABEL(str) [NSTextField wrappingLabelWithString:(str)]

#define MONO_FONT_BOLD(size) \
    ([NSFont monospacedDigitSystemFontOfSize:(size) \
                                      weight:NSFontWeightSemibold])
#define MONO_FONT(size) \
    ([NSFont monospacedDigitSystemFontOfSize:(size) \
                                      weight:NSFontWeightRegular])

#define SYS_FONT_BOLD(size) \
    ([NSFont systemFontOfSize:(size) weight:NSFontWeightBold])
#define SYS_FONT(size) [NSFont systemFontOfSize:(size)]

#define GRAY_COLOR(w, a) \
    [NSColor colorWithCalibratedWhite:(w) alpha:(a)]

#define COLLATZ_TIME() ((double)[NSDate timeIntervalSinceReferenceDate])
#define MARK(label) @pragma mark label

#define ENSURE_APP_KIT() ((void)0)

#endif /* COLLATZ_GNUSTEP */

#endif /* COLLATZ_GNUSTEP_COMPAT_H */
