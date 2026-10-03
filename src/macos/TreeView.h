//
//  TreeView.h
//  Explorador de Collatz - versão macOS/GNUstep (AppKit)
//
//  View que desenha e anima a ÁRVORE de Collatz em camadas, com os
//  caminhos se FUNDINDO em nós compartilhados (como na figura clássica
//  da Collatz tree). Os caminhos dos números escolhidos ficam destacados.
//

#import <Cocoa/Cocoa.h>
#include "collatz_tree.h"

@interface TreeView : NSView
{
    // Os ivars ficam aqui para compatibilidade com o runtime GNU do
    // GNUstep, que não aceita ivars em @implementation.
    CollatzGraph _graph;
    BOOL _hasGraph;

    NSTimer *_timer;
    NSTimeInterval _startTime;
    double _clock;

    double _zoom;
    double _panX;
    double _panY;

    NSPoint _lastDrag;
    BOOL _dragging;

    NSInteger _hoverNode;

    NSUInteger _wrapLen;
}

// Constrói a árvore a partir de uma lista de números (NSNumber *uint64).
- (void)setNumbers:(NSArray *)numbers;

// Reinicia a animação do zero.
- (void)restartAnimation;

// Reenquadra (zoom 1.0, sem pan).
- (void)fitToView;

// Comprime caminhos mais longos que isso (suaviza a escala vertical).
@property(nonatomic) NSUInteger wrapLen;

// Métodos privados declarados para o compilador conhecer as assinaturas.
- (void)startTimer;
- (void)stopTimer;
- (void)tick:(NSTimer *)timer;
- (void)drawPlaceholder:(NSRect)b;
- (NSColor *)columnColor:(int)index;
- (void)drawLegend:(NSRect)b;
- (void)updateHoverForEvent:(NSEvent *)event;

@end
