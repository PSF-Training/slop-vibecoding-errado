//
//  AppDelegate.h
//  Explorador de Collatz - versão macOS/GNUstep (AppKit)
//

#import <Cocoa/Cocoa.h>

@class TreeView;
@class SequenceView;

@interface AppDelegate : NSObject <NSApplicationDelegate>
{
    // Ivars no header por compatibilidade com o runtime GNU (GNUstep).
    NSWindow *_window;

    // Abas.
    NSSegmentedControl *_tabs;
    NSView *_treePage;
    NSView *_seqPage;
    NSView *_recPage;

    // Aba Árvore.
    TreeView *_treeView;
    NSTextField *_treeInputField;
    NSButton *_treeDrawButton;
    NSSegmentedControl *_presetControl;
    NSTextField *_treeStatusLabel;

    // Aba Sequência.
    SequenceView *_seqView;
    NSTextField *_seqInputField;
    NSButton *_seqGoButton;
    NSButton *_chk3d;
    NSButton *_chkAutoRotate;
    NSTextField *_seqStatsLabel;

    // Aba Recordes.
    NSTextField *_recStartField;
    NSTextField *_recEndField;
    NSTextField *_recThreadsField;
    NSButton *_recLimitCheck;
    NSTextField *_recMaxStepsField;
    NSButton *_recSearchButton;
    NSButton *_recCancelButton;
    NSProgressIndicator *_recProgress;
    NSTextField *_recResultLabel;

    // Estado da busca em segundo plano.
    volatile int _searchCanceled;
    volatile int _searchRunning;
    void *_searchThread;   // pthread_t, guardado como ponteiro opaco
    int _searchStatus;
    unsigned long long _pendingRecordN;
    unsigned long long _pendingRecordSteps;
    double _pendingFraction;

    // Opções de inicialização (COLLATZ_TAB / COLLATZ_DEMO).
    NSInteger _startupTab;
    unsigned long long _startupNumber;
}

// Métodos privados declarados aqui para o compilador conhecer a assinatura
// correta. Sem isso, o GCC assume retorno `id`/args variádicos, o que pode
// causar comportamento indefinido no runtime do macOS.
- (void)buildMenu;
- (void)buildWindow;
- (NSView *)buildTreePage:(NSRect)frame;
- (NSView *)buildSequencePage:(NSRect)frame;
- (NSView *)buildRecordsPage:(NSRect)frame;
- (void)showPage:(NSInteger)page;
- (NSString *)defaultNumbersString;
- (void)tabChanged:(id)sender;
- (void)drawFromField:(id)sender;
- (void)presetChanged:(id)sender;
- (void)drawWithInput:(NSString *)input;
- (void)generateFromField:(id)sender;
- (void)toggle3D:(id)sender;
- (void)toggleAutoRotate:(id)sender;
- (void)startSearch:(id)sender;
- (void)cancelSearch:(id)sender;
- (void)setControlsEnabledForSearch:(BOOL)running;
- (void)pollSearch:(NSTimer *)timer;

// Opções de inicialização (definidas antes de [app run]).
- (void)setStartupTab:(int)tab;
- (void)setStartupNumber:(unsigned long long)n;

@end
