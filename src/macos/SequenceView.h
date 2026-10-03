//
//  SequenceView.h
//  Explorador de Collatz - versão macOS/GNUstep (AppKit)
//
//  View que desenha e anima a sequência de Collatz como um painel 2D/3D
//  estilo engine de jogos (a mesma hélice da versão GTK3).
//

#import <Cocoa/Cocoa.h>
#include <stdint.h>

@interface SequenceView : NSView
{
    // Ivars no header por compatibilidade com o runtime GNU (GNUstep).
    uint64_t *_seq;
    size_t _seqLen;
    uint64_t _start;
    int _overflow;

    NSTimer *_timer;
    NSTimeInterval _startTime;
    double _clock;

    double _yaw;      // rotação em Y (arraste horizontal)
    double _pitch;    // inclinação (arraste vertical)
    int _autoRotate;  // giro automático (controlado pelo AppDelegate)
    int _use3d;       // modo 3D ligado/desligado

    NSPoint _lastDrag;
    BOOL _dragging;

    NSInteger _hoverIndex;
}

// Define a sequência (valores já gerados pelo motor C). Copia os dados.
- (void)setSequence:(const uint64_t *)values length:(size_t)len
              start:(uint64_t)start overflow:(int)overflow;

// Limpa a visualização.
- (void)clear;

// Controles da animação.
- (void)setUse3D:(BOOL)on;
- (void)setAutoRotate:(BOOL)on;

// Métodos privados declarados para o compilador conhecer as assinaturas.
- (void)startTimer;
- (void)stopTimer;
- (void)tick:(NSTimer *)timer;
- (void)drawPlaceholder:(NSRect)b;
- (void)drawLegend:(NSRect)b visible:(size_t)visible;
- (void)updateHoverForEvent:(NSEvent *)event;

@end
