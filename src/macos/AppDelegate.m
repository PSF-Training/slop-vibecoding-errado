//
//  AppDelegate.m
//  Explorador de Collatz - versão macOS/GNUstep
//
//  Três abas, como na versão GTK3 do Linux:
//    - Árvore:     árvore de Collatz com fusões (TreeView).
//    - Sequência:  painel animado 2D/3D da sequência (SequenceView).
//    - Recordes:   varredura paralela em busca da sequência mais longa.
//
//  No macOS usa os elementos modernos (title bar unificada, vibrancy,
//  cores do sistema); no GNUstep usa equivalentes clássicos que compilam
//  no runtime GNU.
//
//  Gerenciamento manual de memória (retain/release), compatível com ARC
//  no macOS e com o runtime GNU no GNUstep.
//

#import "AppDelegate.h"
#import "TreeView.h"
#import "SequenceView.h"
#import "GNUstepCompat.h"
#include "collatz.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* Contexto passado para a thread de busca. */
@class AppDelegate;
typedef struct {
    uint64_t start, end;
    unsigned threads;
    uint64_t max_steps;
    volatile int *canceled;
    AppDelegate *owner;      /* para escrever progresso/resultado */
    CollatzRecord rec;
} SearchArgs;

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)note
{
    (void)note;
    [self buildMenu];
    [self buildWindow];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(id)sender
{
    (void)sender;
    return YES;
}

- (void)setStartupTab:(int)tab
{
    _startupTab = (tab >= 0 && tab <= 2) ? tab : 0;
}

- (void)setStartupNumber:(unsigned long long)n
{
    _startupNumber = n;
}

MARK("Menu")

- (void)buildMenu
{
    NSMenu *menubar = [[NSMenu alloc] init];

    NSMenuItem *appItem = [[NSMenuItem alloc] init];
    [menubar addItem:appItem];
    NSMenu *appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"Sobre o Explorador de Collatz"
                       action:@selector(orderFrontStandardAboutPanel:)
                keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Ocultar"
                       action:@selector(hide:)
                keyEquivalent:@"h"];
    [appMenu addItemWithTitle:@"Sair"
                       action:@selector(terminate:)
                keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [appMenu release];
    [appItem release];

    NSMenuItem *editItem = [[NSMenuItem alloc] init];
    [menubar addItem:editItem];
    NSMenu *editMenu = [[NSMenu alloc] init];
    [editMenu addItemWithTitle:@"Recortar" action:@selector(cut:)
                 keyEquivalent:@"x"];
    [editMenu addItemWithTitle:@"Copiar" action:@selector(copy:)
                 keyEquivalent:@"c"];
    [editMenu addItemWithTitle:@"Colar" action:@selector(paste:)
                 keyEquivalent:@"v"];
    [editMenu addItemWithTitle:@"Selecionar tudo" action:@selector(selectAll:)
                 keyEquivalent:@"a"];
    [editItem setSubmenu:editMenu];
    [editMenu release];
    [editItem release];

    [NSApp setMainMenu:menubar];
    [menubar release];
}

MARK("Janela")

- (void)buildWindow
{
    unsigned int style = NSWindowStyleMaskTitled |
                         NSWindowStyleMaskClosable |
                         NSWindowStyleMaskMiniaturizable |
                         NSWindowStyleMaskResizable;

    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1080, 720)
                                          styleMask:style
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [_window setTitle:@"Explorador de Collatz"];
    [_window setMinSize:NSMakeSize(820, 560)];
    [_window center];

#if !COLLATZ_GNUSTEP
    // Elementos modernos do macOS (não existem no GNUstep).
    [_window setTitlebarAppearsTransparent:YES];
    [_window setTitleVisibility:NSWindowTitleHidden];
    [_window setToolbarStyle:NSWindowToolbarStyleUnified];
    [_window setStyleMask:[_window styleMask] | NSWindowStyleMaskFullSizeContentView];
#endif

    NSView *content = [_window contentView];

    // Barra de seleção de abas no topo.
    NSTextField *title = MAKE_LABEL(@"Explorador de Collatz");
    [title setFont:SYS_FONT_BOLD(17)];

    _tabs = [[NSSegmentedControl alloc] initWithFrame:
        NSMakeRect(0, 0, 320, 26)];
    [_tabs setSegmentCount:3];
    [_tabs setLabel:@"Árvore" forSegment:0];
    [_tabs setLabel:@"Sequência" forSegment:1];
    [_tabs setLabel:@"Recordes" forSegment:2];
#if !COLLATZ_GNUSTEP
    [_tabs setTrackingMode:NSSegmentSwitchTrackingSelectOne];
#endif
    [_tabs setSelectedSegment:0];
    [_tabs setTarget:self];
    [_tabs setAction:@selector(tabChanged:)];
    [content addSubview:title];
    [content addSubview:_tabs];

    // As três páginas ocupam a área abaixo da barra de abas.
    NSRect pageFrame = NSMakeRect(0, 0, 1080, 720 - 52);
    _treePage = [self buildTreePage:pageFrame];
    _seqPage  = [self buildSequencePage:pageFrame];
    _recPage  = [self buildRecordsPage:pageFrame];
    [content addSubview:_treePage];
    [content addSubview:_seqPage];
    [content addSubview:_recPage];

#if COLLATZ_GNUSTEP
    // Layout manual: sem autolayout robusto, posiciona por frames e usa
    // máscaras de autoresizing proporcionais. Reserva 100 px no topo para
    // o título e a barra de abas.
    CGFloat w = [content bounds].size.width;
    CGFloat h = [content bounds].size.height;
    [title setFrame:NSMakeRect(18, h - 34, 320, 24)];
    [_tabs setFrame:NSMakeRect(18, h - 62, 320, 26)];

    NSRect pf = NSMakeRect(0, 0, w, h - 100);
    [_treePage setFrame:pf];
    [_seqPage setFrame:pf];
    [_recPage setFrame:pf];
    [_treePage setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [_seqPage setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [_recPage setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    [self showPage:0];
#else
    for (NSView *v in ARRAY(title, _tabs, _treePage, _seqPage, _recPage))
        [v setTranslatesAutoresizingMaskIntoConstraints:NO];
    [NSLayoutConstraint activateConstraints:@[
        [[title leadingAnchor] constraintEqualToAnchor:[content leadingAnchor]
                                              constant:20],
        [[title topAnchor] constraintEqualToAnchor:[content topAnchor]
                                          constant:38],

        [[_tabs leadingAnchor] constraintEqualToAnchor:[content leadingAnchor]
                                             constant:20],
        [[_tabs topAnchor] constraintEqualToAnchor:[title bottomAnchor]
                                          constant:8],
        [[_tabs widthAnchor] constraintEqualToConstant:320],

        [[_treePage leadingAnchor] constraintEqualToAnchor:[content leadingAnchor]],
        [[_treePage trailingAnchor] constraintEqualToAnchor:[content trailingAnchor]],
        [[_treePage topAnchor] constraintEqualToAnchor:[_tabs bottomAnchor]
                                              constant:10],
        [[_treePage bottomAnchor] constraintEqualToAnchor:[content bottomAnchor]],

        [[_seqPage leadingAnchor] constraintEqualToAnchor:[content leadingAnchor]],
        [[_seqPage trailingAnchor] constraintEqualToAnchor:[content trailingAnchor]],
        [[_seqPage topAnchor] constraintEqualToAnchor:[_tabs bottomAnchor]
                                             constant:10],
        [[_seqPage bottomAnchor] constraintEqualToAnchor:[content bottomAnchor]],

        [[_recPage leadingAnchor] constraintEqualToAnchor:[content leadingAnchor]],
        [[_recPage trailingAnchor] constraintEqualToAnchor:[content trailingAnchor]],
        [[_recPage topAnchor] constraintEqualToAnchor:[_tabs bottomAnchor]
                                             constant:10],
        [[_recPage bottomAnchor] constraintEqualToAnchor:[content bottomAnchor]],
    ]];
    [self showPage:0];
#endif

    [_window makeKeyAndOrderFront:nil];
#if !COLLATZ_GNUSTEP
    [NSApp activateIgnoringOtherApps:YES];
#endif

    /* Opções de inicialização (COLLATZ_TAB / COLLATZ_DEMO). */
    [self showPage:_startupTab];
    if (_startupNumber != 0) {
        NSString *s = [NSString stringWithFormat:@"%llu", _startupNumber];
        [_treeInputField setStringValue:s];
        [_seqInputField setStringValue:s];
        if (_startupTab == 1) {
            [self generateFromField:nil];
        } else {
            [self drawWithInput:s];
        }
    }

    /* COLLATZ_AUTOSEARCH=1 dispara a busca ao abrir (útil para testar). */
    const char *autoSearch = getenv("COLLATZ_AUTOSEARCH");
    if (autoSearch != NULL && atoi(autoSearch) != 0) {
        [self startSearch:nil];
    }
}

/* Mostra a página `page` e esconde as outras. */
- (void)showPage:(NSInteger)page
{
    [_treePage setHidden:(page != 0)];
    [_seqPage setHidden:(page != 1)];
    [_recPage setHidden:(page != 2)];
    [_tabs setSelectedSegment:page];
}

- (void)tabChanged:(id)sender
{
    [self showPage:[sender selectedSegment]];
}

- (NSString *)defaultNumbersString
{
    return @"27, 97, 871, 41";
}

MARK("Aba Arvore")

- (NSView *)buildTreePage:(NSRect)frame
{
    NSView *page = [[NSView alloc] initWithFrame:frame];
    [page setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    NSTextField *subtitle = MAKE_WRAP_LABEL(
        @"Informe os números (separados por espaço ou vírgula). Cada um "
        @"vira uma folha; os caminhos se fundem no tronco até o 1.");
    [subtitle setFont:SYS_FONT(11)];
    [subtitle setTextColor:[NSColor secondaryLabelColor]];

    _treeInputField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)];
    [_treeInputField setPlaceholderString:@"Ex.: 27, 97, 871, 41"];
    [_treeInputField setStringValue:[self defaultNumbersString]];
    [_treeInputField setFont:MONO_FONT(13)];
    [_treeInputField setTarget:self];
    [_treeInputField setAction:@selector(drawFromField:)];

    _treeDrawButton = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 140, 30)];
    [_treeDrawButton setTitle:@"Desenhar árvore"];
    [_treeDrawButton setTarget:self];
    [_treeDrawButton setAction:@selector(drawFromField:)];
    [_treeDrawButton setBezelStyle:BEZEL_ROUNDED];
    [_treeDrawButton setKeyEquivalent:@"\r"];

    NSTextField *presetLabel = MAKE_LABEL(@"Exemplos:");
    [presetLabel setFont:SYS_FONT(11)];
    [presetLabel setTextColor:[NSColor secondaryLabelColor]];

    _presetControl = [[NSSegmentedControl alloc] initWithFrame:
        NSMakeRect(0, 0, 320, 24)];
    [_presetControl setSegmentCount:4];
#if !COLLATZ_GNUSTEP
    [_presetControl setTrackingMode:NSSegmentSwitchTrackingMomentary];
#endif
    [_presetControl setTarget:self];
    [_presetControl setAction:@selector(presetChanged:)];
    [_presetControl setLabel:@"27" forSegment:0];
    [_presetControl setLabel:@"27+97" forSegment:1];
    [_presetControl setLabel:@"Clássicos" forSegment:2];
    [_presetControl setLabel:@"Recordista" forSegment:3];

    _treeStatusLabel = MAKE_WRAP_LABEL(@"");
    [_treeStatusLabel setFont:SYS_FONT(11)];
    [_treeStatusLabel setTextColor:[NSColor secondaryLabelColor]];

    _treeView = [[TreeView alloc] initWithFrame:
        NSMakeRect(0, 0, frame.size.width, frame.size.height - 130)];
    [_treeView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    [page addSubview:_treeView];
    NSArray *views = ARRAY(subtitle, _treeInputField, _treeDrawButton,
                           presetLabel, _presetControl, _treeStatusLabel);

#if COLLATZ_GNUSTEP
    double w = frame.size.width;
    double h = frame.size.height;
    [subtitle setFrame:NSMakeRect(18, h - 42, w - 36, 34)];
    [_treeInputField setFrame:NSMakeRect(18, h - 76, w - 240, 24)];
    [_treeDrawButton setFrame:NSMakeRect(w - 210, h - 78, 190, 28)];
    [presetLabel setFrame:NSMakeRect(18, h - 104, 80, 18)];
    [_presetControl setFrame:NSMakeRect(100, h - 104, 320, 24)];
    [_treeStatusLabel setFrame:NSMakeRect(18, h - 132, w - 36, 24)];
    [_treeView setFrame:NSMakeRect(0, 0, w, h - 150)];
    for (NSView *v in views)
        [page addSubview:v];
#else
    for (NSView *v in views)
        [v setTranslatesAutoresizingMaskIntoConstraints:NO];
    double pad = 18.0;
    [NSLayoutConstraint activateConstraints:@[
        [[subtitle topAnchor] constraintEqualToAnchor:[page topAnchor]
                                             constant:2],
        [[subtitle leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]
                                                 constant:pad],
        [[subtitle trailingAnchor] constraintEqualToAnchor:[page trailingAnchor]
                                                  constant:-pad],

        [[_treeInputField topAnchor] constraintEqualToAnchor:[subtitle bottomAnchor]
                                                    constant:10],
        [[_treeInputField leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]
                                                        constant:pad],
        [[_treeDrawButton leadingAnchor] constraintEqualToAnchor:[_treeInputField trailingAnchor]
                                                        constant:10],
        [[_treeDrawButton centerYAnchor] constraintEqualToAnchor:[_treeInputField centerYAnchor]],
        [[_treeDrawButton trailingAnchor] constraintEqualToAnchor:[page trailingAnchor]
                                                         constant:-pad],
        [[_treeDrawButton widthAnchor] constraintEqualToConstant:170],

        [[presetLabel topAnchor] constraintEqualToAnchor:[_treeInputField bottomAnchor]
                                                constant:10],
        [[presetLabel leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]
                                                    constant:pad],
        [[_presetControl leadingAnchor] constraintEqualToAnchor:[presetLabel trailingAnchor]
                                                       constant:8],
        [[_presetControl centerYAnchor] constraintEqualToAnchor:[presetLabel centerYAnchor]],
        [[_presetControl widthAnchor] constraintEqualToConstant:360],

        [[_treeStatusLabel topAnchor] constraintEqualToAnchor:[_presetControl bottomAnchor]
                                                     constant:10],
        [[_treeStatusLabel leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]
                                                         constant:pad],
        [[_treeStatusLabel trailingAnchor] constraintEqualToAnchor:[page trailingAnchor]
                                                          constant:-pad],

        [[_treeView topAnchor] constraintEqualToAnchor:[_treeStatusLabel bottomAnchor]
                                              constant:6],
        [[_treeView leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]],
        [[_treeView trailingAnchor] constraintEqualToAnchor:[page trailingAnchor]],
        [[_treeView bottomAnchor] constraintEqualToAnchor:[page bottomAnchor]],
    ]];
#endif

    if (_treeView == nil)
        _treeView = [[TreeView alloc] initWithFrame:frame];
    [self drawWithInput:[self defaultNumbersString]];
    return page;
}

- (void)drawFromField:(id)sender
{
    (void)sender;
    [self drawWithInput:[_treeInputField stringValue]];
}

- (void)presetChanged:(id)sender
{
    NSInteger sel = [sender selectedSegment];
    NSString *s = nil;
    switch (sel) {
        case 0: s = @"27"; break;
        case 1: s = @"27, 97"; break;
        case 2: s = @"27, 97, 871, 41, 6171"; break;
        case 3: s = @"837799, 27"; break;
        default: s = [self defaultNumbersString]; break;
    }
    [_treeInputField setStringValue:s];
    [self drawWithInput:s];
}

- (void)drawWithInput:(NSString *)input
{
    // Separa por vírgula, espaço, ponto e vírgula e quebra de linha.
    NSCharacterSet *seps = [NSCharacterSet
        characterSetWithCharactersInString:@",; \t\n\r"];
    NSArray *parts = [input componentsSeparatedByCharactersInSet:seps];

    NSMutableArray *numbers = [NSMutableArray array];
    for (NSUInteger i = 0; i < [parts count]; i++) {
        NSString *p = [parts objectAtIndex:i];
        NSString *trim = [p stringByTrimmingCharactersInSet:
            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if ([trim length] == 0)
            continue;
        unsigned long long v = strtoull([trim UTF8String], NULL, 10);
        if (v > 0)
            [numbers addObject:[NSNumber numberWithUnsignedLongLong:v]];
    }

    if ([numbers count] == 0) {
        [_treeStatusLabel setStringValue:@"Nenhum número válido informado."];
        return;
    }

    [_treeView setNumbers:numbers];

    NSMutableString *st = [NSMutableString string];
    [st appendFormat:@"%lu número(s): ",
        (unsigned long)[numbers count]];
    for (NSUInteger i = 0; i < [numbers count]; i++) {
        if (i > 0)
            [st appendString:@", "];
        [st appendFormat:@"%@", [numbers objectAtIndex:i]];
    }
    [_treeStatusLabel setStringValue:st];
}

MARK("Aba Sequencia")

- (NSView *)buildSequencePage:(NSRect)frame
{
    NSView *page = [[NSView alloc] initWithFrame:frame];
    [page setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    NSTextField *subtitle = MAKE_WRAP_LABEL(
        @"Digite um número de partida. A sequência aparece como uma hélice "
        @"animada em 2D/3D; arraste para girar e passe o mouse sobre um nó.");
    [subtitle setFont:SYS_FONT(11)];
    [subtitle setTextColor:[NSColor secondaryLabelColor]];

    _seqInputField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 200, 24)];
    [_seqInputField setPlaceholderString:@"Ex.: 27"];
    [_seqInputField setStringValue:@"27"];
    [_seqInputField setFont:MONO_FONT(13)];
    [_seqInputField setTarget:self];
    [_seqInputField setAction:@selector(generateFromField:)];

    _seqGoButton = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 150, 30)];
    [_seqGoButton setTitle:@"Gerar sequência"];
    [_seqGoButton setTarget:self];
    [_seqGoButton setAction:@selector(generateFromField:)];
    [_seqGoButton setBezelStyle:BEZEL_ROUNDED];
    [_seqGoButton setKeyEquivalent:@"\r"];

    _seqView = [[SequenceView alloc] initWithFrame:
        NSMakeRect(0, 0, frame.size.width, frame.size.height - 130)];
    [_seqView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    _chk3d = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 90, 22)];
    [_chk3d setButtonType:COLLATZ_SWITCH_BUTTON];
    [_chk3d setTitle:@"Modo 3D"];
    [_chk3d setState:COLLATZ_STATE_ON];
    [_chk3d setTarget:self];
    [_chk3d setAction:@selector(toggle3D:)];

    _chkAutoRotate = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 190, 22)];
    [_chkAutoRotate setButtonType:COLLATZ_SWITCH_BUTTON];
    [_chkAutoRotate setTitle:@"Girar automaticamente"];
    [_chkAutoRotate setState:COLLATZ_STATE_ON];
    [_chkAutoRotate setTarget:self];
    [_chkAutoRotate setAction:@selector(toggleAutoRotate:)];

    _seqStatsLabel = MAKE_WRAP_LABEL(@"Informe um número e clique em Gerar.");
    [_seqStatsLabel setFont:SYS_FONT(11)];
    [_seqStatsLabel setTextColor:[NSColor secondaryLabelColor]];

    [page addSubview:_seqView];
    NSArray *views = ARRAY(subtitle, _seqInputField, _seqGoButton,
                           _chk3d, _chkAutoRotate, _seqStatsLabel);

#if COLLATZ_GNUSTEP
    double w = frame.size.width;
    double h = frame.size.height;
    [subtitle setFrame:NSMakeRect(18, h - 42, w - 36, 34)];
    [_seqInputField setFrame:NSMakeRect(18, h - 76, 200, 24)];
    [_seqGoButton setFrame:NSMakeRect(228, h - 78, 160, 28)];
    [_chk3d setFrame:NSMakeRect(18, h - 106, 100, 22)];
    [_chkAutoRotate setFrame:NSMakeRect(130, h - 106, 200, 22)];
    [_seqStatsLabel setFrame:NSMakeRect(18, h - 134, w - 36, 24)];
    [_seqView setFrame:NSMakeRect(0, 0, w, h - 150)];
    for (NSView *v in views)
        [page addSubview:v];
#else
    for (NSView *v in views)
        [v setTranslatesAutoresizingMaskIntoConstraints:NO];
    double pad = 18.0;
    [NSLayoutConstraint activateConstraints:@[
        [[subtitle topAnchor] constraintEqualToAnchor:[page topAnchor]
                                             constant:2],
        [[subtitle leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]
                                                 constant:pad],
        [[subtitle trailingAnchor] constraintEqualToAnchor:[page trailingAnchor]
                                                  constant:-pad],

        [[_seqInputField topAnchor] constraintEqualToAnchor:[subtitle bottomAnchor]
                                                   constant:10],
        [[_seqInputField leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]
                                                       constant:pad],
        [[_seqInputField widthAnchor] constraintEqualToConstant:220],
        [[_seqGoButton leadingAnchor] constraintEqualToAnchor:[_seqInputField trailingAnchor]
                                                     constant:10],
        [[_seqGoButton centerYAnchor] constraintEqualToAnchor:[_seqInputField centerYAnchor]],
        [[_seqGoButton widthAnchor] constraintEqualToConstant:170],

        [[_chk3d topAnchor] constraintEqualToAnchor:[_seqInputField bottomAnchor]
                                           constant:10],
        [[_chk3d leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]
                                               constant:pad],
        [[_chkAutoRotate leadingAnchor] constraintEqualToAnchor:[_chk3d trailingAnchor]
                                                        constant:12],
        [[_chkAutoRotate centerYAnchor] constraintEqualToAnchor:[_chk3d centerYAnchor]],

        [[_seqStatsLabel topAnchor] constraintEqualToAnchor:[_chk3d bottomAnchor]
                                                   constant:8],
        [[_seqStatsLabel leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]
                                                       constant:pad],
        [[_seqStatsLabel trailingAnchor] constraintEqualToAnchor:[page trailingAnchor]
                                                        constant:-pad],

        [[_seqView topAnchor] constraintEqualToAnchor:[_seqStatsLabel bottomAnchor]
                                             constant:6],
        [[_seqView leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]],
        [[_seqView trailingAnchor] constraintEqualToAnchor:[page trailingAnchor]],
        [[_seqView bottomAnchor] constraintEqualToAnchor:[page bottomAnchor]],
    ]];
#endif

    return page;
}

- (void)toggle3D:(id)sender
{
    [_seqView setUse3D:([sender state] == COLLATZ_STATE_ON)];
}

- (void)toggleAutoRotate:(id)sender
{
    [_seqView setAutoRotate:([sender state] == COLLATZ_STATE_ON)];
}

- (void)generateFromField:(id)sender
{
    (void)sender;
    const char *text = [[_seqInputField stringValue] UTF8String];
    unsigned long long start = strtoull(text, NULL, 10);
    if (start == 0) {
        [_seqStatsLabel setStringValue:
            @"Digite um número inteiro maior ou igual a 1."];
        return;
    }

    size_t cap = 4096;
    uint64_t *buf = malloc(cap * sizeof(uint64_t));
    if (buf == NULL) {
        [_seqStatsLabel setStringValue:@"Sem memória."];
        return;
    }

    size_t len = 0;
    int ovf = 0;
    while (collatz_generate(start, buf, cap, &len, &ovf) != 0) {
        cap *= 2;
        uint64_t *nb = realloc(buf, cap * sizeof(uint64_t));
        if (nb == NULL) {
            free(buf);
            [_seqStatsLabel setStringValue:@"Sem memória."];
            return;
        }
        buf = nb;
    }

    [_seqView setSequence:buf length:len start:start overflow:ovf];
    free(buf);

    /* Maior valor: recalcula a partir do motor para não manter o buffer. */
    uint64_t maxv = start;
    __uint128_t x = start;
    for (size_t i = 0; i < len; i++) {
        if ((uint64_t)x > maxv) maxv = (uint64_t)x;
        if (x == 1) break;
        if ((x & 1u) == 0u) x >>= 1;
        else {
            __uint128_t nx = 3u * x + 1u;
            if (nx > (__uint128_t)UINT64_MAX) break;
            x = nx;
        }
    }

    NSString *stats = [NSString stringWithFormat:
        @"Número: %llu    •    Passos até 1: %zu    •    Maior valor: %llu",
        start, len - 1, (unsigned long long)maxv];
    if (ovf)
        stats = [stats stringByAppendingString:
            @"\nAviso: a sequência ultrapassou 64 bits; foi truncada."];
    [_seqStatsLabel setStringValue:stats];
}

MARK("Aba Recordes")

- (NSView *)buildRecordsPage:(NSRect)frame
{
    NSView *page = [[NSView alloc] initWithFrame:frame];
    [page setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    NSTextField *subtitle = MAKE_WRAP_LABEL(
        @"Varre uma faixa (início..fim) em paralelo e encontra o número que "
        @"gera a sequência mais longa. Use o limite de passos para proteger "
        @"contra números patológicos.");
    [subtitle setFont:SYS_FONT(11)];
    [subtitle setTextColor:[NSColor secondaryLabelColor]];

    NSTextField *lStart = MAKE_LABEL(@"Início:");
    NSTextField *lEnd = MAKE_LABEL(@"Fim:");
    NSTextField *lThreads = MAKE_LABEL(@"Threads:");
    NSTextField *lMax = MAKE_LABEL(@"Máx. passos:");

    _recStartField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 120, 24)];
    [_recStartField setStringValue:@"1"];
    [_recStartField setFont:MONO_FONT(12)];
    _recEndField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 140, 24)];
    [_recEndField setStringValue:@"1000000"];
    [_recEndField setFont:MONO_FONT(12)];
    _recThreadsField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 60, 24)];
    [_recThreadsField setStringValue:[NSString stringWithFormat:@"%u",
        collatz_cpu_count()]];
    [_recThreadsField setFont:MONO_FONT(12)];
    _recMaxStepsField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 100, 24)];
    [_recMaxStepsField setStringValue:@"100000"];
    [_recMaxStepsField setFont:MONO_FONT(12)];

    _recLimitCheck = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 200, 22)];
    [_recLimitCheck setButtonType:COLLATZ_SWITCH_BUTTON];
    [_recLimitCheck setTitle:@"Limitar passos por número"];
    [_recLimitCheck setState:COLLATZ_STATE_ON];

    _recSearchButton = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 160, 30)];
    [_recSearchButton setTitle:@"Buscar recorde"];
    [_recSearchButton setTarget:self];
    [_recSearchButton setAction:@selector(startSearch:)];
    [_recSearchButton setBezelStyle:BEZEL_ROUNDED];

    _recCancelButton = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 120, 30)];
    [_recCancelButton setTitle:@"Cancelar"];
    [_recCancelButton setTarget:self];
    [_recCancelButton setAction:@selector(cancelSearch:)];
    [_recCancelButton setBezelStyle:BEZEL_ROUNDED];
    [_recCancelButton setEnabled:NO];

    _recProgress = [[NSProgressIndicator alloc]
        initWithFrame:NSMakeRect(0, 0, frame.size.width - 36, 20)];
    [_recProgress setStyle:COLLATZ_BAR_STYLE];
    [_recProgress setIndeterminate:NO];
    [_recProgress setMinValue:0.0];
    [_recProgress setMaxValue:1.0];
    [_recProgress setDoubleValue:0.0];

    _recResultLabel = MAKE_WRAP_LABEL(@"Informe a faixa e clique em Buscar recorde.");
    [_recResultLabel setFont:SYS_FONT(12)];

    NSArray *views = ARRAY(subtitle, lStart, _recStartField, lEnd, _recEndField,
                           lThreads, _recThreadsField, _recLimitCheck,
                           lMax, _recMaxStepsField, _recSearchButton,
                           _recCancelButton, _recProgress, _recResultLabel);

#if COLLATZ_GNUSTEP
    double w = frame.size.width;
    double h = frame.size.height;
    double y = h - 42;
    [subtitle setFrame:NSMakeRect(18, y, w - 36, 34)]; y -= 44;
    [lStart setFrame:NSMakeRect(18, y, 50, 20)];
    [_recStartField setFrame:NSMakeRect(70, y - 2, 120, 24)];
    [lEnd setFrame:NSMakeRect(210, y, 40, 20)];
    [_recEndField setFrame:NSMakeRect(252, y - 2, 140, 24)]; y -= 34;
    [lThreads setFrame:NSMakeRect(18, y, 60, 20)];
    [_recThreadsField setFrame:NSMakeRect(80, y - 2, 60, 24)];
    [_recLimitCheck setFrame:NSMakeRect(170, y, 200, 22)];
    [lMax setFrame:NSMakeRect(380, y, 90, 20)];
    [_recMaxStepsField setFrame:NSMakeRect(472, y - 2, 100, 24)]; y -= 40;
    [_recSearchButton setFrame:NSMakeRect(18, y, 160, 30)];
    [_recCancelButton setFrame:NSMakeRect(190, y, 120, 30)]; y -= 44;
    [_recProgress setFrame:NSMakeRect(18, y, w - 36, 20)]; y -= 40;
    [_recResultLabel setFrame:NSMakeRect(18, y, w - 36, 80)];
    for (NSView *v in views)
        [page addSubview:v];
#else
    for (NSView *v in views)
        [v setTranslatesAutoresizingMaskIntoConstraints:NO];
    double pad = 18.0;
    [NSLayoutConstraint activateConstraints:@[
        [[subtitle topAnchor] constraintEqualToAnchor:[page topAnchor]
                                             constant:2],
        [[subtitle leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]
                                                 constant:pad],
        [[subtitle trailingAnchor] constraintEqualToAnchor:[page trailingAnchor]
                                                  constant:-pad],

        [[lStart topAnchor] constraintEqualToAnchor:[subtitle bottomAnchor]
                                           constant:14],
        [[lStart leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]
                                               constant:pad],
        [[_recStartField leadingAnchor] constraintEqualToAnchor:[lStart trailingAnchor]
                                                       constant:8],
        [[_recStartField centerYAnchor] constraintEqualToAnchor:[lStart centerYAnchor]],
        [[_recStartField widthAnchor] constraintEqualToConstant:120],
        [[lEnd leadingAnchor] constraintEqualToAnchor:[_recStartField trailingAnchor]
                                             constant:16],
        [[lEnd centerYAnchor] constraintEqualToAnchor:[lStart centerYAnchor]],
        [[_recEndField leadingAnchor] constraintEqualToAnchor:[lEnd trailingAnchor]
                                                     constant:8],
        [[_recEndField centerYAnchor] constraintEqualToAnchor:[lStart centerYAnchor]],
        [[_recEndField widthAnchor] constraintEqualToConstant:150],

        [[lThreads topAnchor] constraintEqualToAnchor:[lStart bottomAnchor]
                                             constant:14],
        [[lThreads leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]
                                                 constant:pad],
        [[_recThreadsField leadingAnchor] constraintEqualToAnchor:[lThreads trailingAnchor]
                                                         constant:8],
        [[_recThreadsField centerYAnchor] constraintEqualToAnchor:[lThreads centerYAnchor]],
        [[_recThreadsField widthAnchor] constraintEqualToConstant:60],
        [[_recLimitCheck leadingAnchor] constraintEqualToAnchor:[_recThreadsField trailingAnchor]
                                                       constant:20],
        [[_recLimitCheck centerYAnchor] constraintEqualToAnchor:[lThreads centerYAnchor]],
        [[lMax leadingAnchor] constraintEqualToAnchor:[_recLimitCheck trailingAnchor]
                                             constant:16],
        [[lMax centerYAnchor] constraintEqualToAnchor:[lThreads centerYAnchor]],
        [[_recMaxStepsField leadingAnchor] constraintEqualToAnchor:[lMax trailingAnchor]
                                                          constant:8],
        [[_recMaxStepsField centerYAnchor] constraintEqualToAnchor:[lThreads centerYAnchor]],
        [[_recMaxStepsField widthAnchor] constraintEqualToConstant:110],

        [[_recSearchButton topAnchor] constraintEqualToAnchor:[lThreads bottomAnchor]
                                                     constant:16],
        [[_recSearchButton leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]
                                                         constant:pad],
        [[_recSearchButton widthAnchor] constraintEqualToConstant:170],
        [[_recCancelButton leadingAnchor] constraintEqualToAnchor:[_recSearchButton trailingAnchor]
                                                         constant:10],
        [[_recCancelButton centerYAnchor] constraintEqualToAnchor:[_recSearchButton centerYAnchor]],
        [[_recCancelButton widthAnchor] constraintEqualToConstant:120],

        [[_recProgress topAnchor] constraintEqualToAnchor:[_recSearchButton bottomAnchor]
                                                 constant:14],
        [[_recProgress leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]
                                                     constant:pad],
        [[_recProgress trailingAnchor] constraintEqualToAnchor:[page trailingAnchor]
                                                      constant:-pad],

        [[_recResultLabel topAnchor] constraintEqualToAnchor:[_recProgress bottomAnchor]
                                                    constant:12],
        [[_recResultLabel leadingAnchor] constraintEqualToAnchor:[page leadingAnchor]
                                                        constant:pad],
        [[_recResultLabel trailingAnchor] constraintEqualToAnchor:[page trailingAnchor]
                                                         constant:-pad],
    ]];
#endif

    return page;
}

/* ---- Busca em segundo plano -------------------------------------- */

static void search_progress_cb(double fraction, void *ud)
{
    /* Roda na thread de busca; só grava a fração. O timer de polling
       (na thread principal) lê esse valor e atualiza a barra. */
    AppDelegate *self = (AppDelegate *)ud;
    self->_pendingFraction = fraction;
}

static void *search_thread_main(void *arg)
{
    SearchArgs *a = (SearchArgs *)arg;
    AppDelegate *self = (AppDelegate *)a->owner;

    int status = collatz_search(a->start, a->end, a->threads, a->max_steps,
                                0, &a->rec, search_progress_cb, self,
                                a->canceled);

    self->_searchStatus = status;
    self->_pendingRecordN = a->rec.n;
    self->_pendingRecordSteps = a->rec.steps;
    self->_searchRunning = 0;
    free(a);
    return NULL;
}

- (void)startSearch:(id)sender
{
    (void)sender;
    if (_searchRunning)
        return;

    unsigned long long a = strtoull([[_recStartField stringValue] UTF8String],
                                    NULL, 10);
    unsigned long long b = strtoull([[_recEndField stringValue] UTF8String],
                                    NULL, 10);
    if (a == 0 || b < a) {
        [_recResultLabel setStringValue:
            @"Faixa inválida: use início >= 1 e fim >= início."];
        return;
    }

    SearchArgs *args = calloc(1, sizeof(SearchArgs));
    args->start = a;
    args->end = b;
    args->owner = self;
    args->threads = (unsigned)strtoul(
        [[_recThreadsField stringValue] UTF8String], NULL, 10);
    args->max_steps = [_recLimitCheck state] == COLLATZ_STATE_ON
        ? strtoull([[_recMaxStepsField stringValue] UTF8String], NULL, 10)
        : 0;
    args->canceled = &_searchCanceled;

    _searchCanceled = 0;
    _searchRunning = 1;
    _searchStatus = 0;
    _pendingRecordN = 0;
    _pendingRecordSteps = 0;
    _pendingFraction = 0.0;
    [self setControlsEnabledForSearch:YES];
    [_recResultLabel setStringValue:@"Calculando…"];

    pthread_t tid;
    if (pthread_create(&tid, NULL, search_thread_main, args) != 0) {
        _searchRunning = 0;
        free(args);
        [self setControlsEnabledForSearch:NO];
        [_recResultLabel setStringValue:@"Não foi possível iniciar a busca."];
        return;
    }
    _searchThread = (void *)tid;

    /* O timer de polling atualiza a barra e detecta o fim da busca na
       thread principal (o NSTimer só roda no run loop principal). */
    [NSTimer scheduledTimerWithTimeInterval:0.1
                                     target:self
                                   selector:@selector(pollSearch:)
                                   userInfo:nil
                                    repeats:YES];
}

- (void)setControlsEnabledForSearch:(BOOL)running
{
    [_recSearchButton setEnabled:!running];
    [_recCancelButton setEnabled:running];
    [_recStartField setEnabled:!running];
    [_recEndField setEnabled:!running];
    [_recThreadsField setEnabled:!running];
    [_recMaxStepsField setEnabled:!running];
    [_recLimitCheck setEnabled:!running];
}

- (void)cancelSearch:(id)sender
{
    (void)sender;
    _searchCanceled = 1;
    [_recResultLabel setStringValue:@"Cancelando…"];
}

- (void)pollSearch:(NSTimer *)timer
{
    [_recProgress setDoubleValue:_pendingFraction];
    if (_searchRunning)
        return;

    [timer invalidate];
    pthread_join((pthread_t)_searchThread, NULL);
    [self setControlsEnabledForSearch:NO];
    [_recProgress setDoubleValue:1.0];

    if (_searchStatus == 0 && _pendingRecordN != 0) {
        [_recResultLabel setStringValue:[NSString stringWithFormat:
            @"Recorde na faixa:  n = %llu  com  %llu passos",
            _pendingRecordN, _pendingRecordSteps]];
    } else if (_searchStatus == 1) {
        [_recResultLabel setStringValue:[NSString stringWithFormat:
            @"Busca cancelada/limitada. Melhor até agora: n = %llu (%llu passos)",
            _pendingRecordN, _pendingRecordSteps]];
    } else {
        [_recResultLabel setStringValue:@"Nenhum resultado."];
    }
}

@end
