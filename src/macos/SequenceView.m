//
//  SequenceView.m
//  Explorador de Collatz - versão macOS/GNUstep
//
//  Painel animado da sequência: cada valor vira um nó brilhante e as
//  transições viram setas coloridas pela paridade (azul para n/2, laranja
//  para 3n+1). Os nós são dispostos numa hélice 3D que gira; arraste para
//  girar manualmente e passe o mouse sobre um nó para ver valor e passo.
//
//  É uma tradução fiel de draw_sequence_view() do main.c (GTK3), para que
//  as duas plataformas mostrem exatamente a mesma coisa.
//

#import "SequenceView.h"
#import "GNUstepCompat.h"
#import <math.h>
#include <stdlib.h>
#include <string.h>

/* Máximo de nós desenhados, para manter fluido (igual ao GTK). */
#define SEQ_MAX_NODES 160

typedef struct { double x, y, z; } V3;

@implementation SequenceView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if (self) {
        _seq = NULL;
        _seqLen = 0;
        _start = 0;
        _overflow = 0;
        _yaw = -0.35;
        _pitch = 0.12;
        _autoRotate = 1;
        _use3d = 1;
        _hoverIndex = -1;
        WANT_LAYER(self);
        [self startTimer];
    }
    return self;
}

- (void)dealloc
{
    [self stopTimer];
    free(_seq);
    [super dealloc];
}

- (BOOL)isFlipped { return YES; }   /* topo em y=0, como o GTK */
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

MARK("Timer")

- (void)startTimer
{
    [self stopTimer];
    _startTime = COLLATZ_TIME();
    _timer = [[NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
                                               target:self
                                             selector:@selector(tick:)
                                             userInfo:nil
                                              repeats:YES] retain];
    [[NSRunLoop currentRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopTimer
{
    if (_timer != nil) {
        [_timer invalidate];
        [_timer release];
        _timer = nil;
    }
}

- (void)tick:(NSTimer *)timer
{
    (void)timer;
    _clock = COLLATZ_TIME() - _startTime;
    if (_autoRotate && _use3d) {
        _yaw += 0.006;
        if (_yaw > 2 * M_PI) _yaw -= 2 * M_PI;
        _pitch = sin(_clock * 0.4) * 0.18;
    }
    [self setNeedsDisplay:YES];
}

- (void)viewWillMoveToWindow:(NSWindow *)newWindow
{
    [super viewWillMoveToWindow:newWindow];
    if (newWindow == nil)
        [self stopTimer];
    else if (_timer == nil)
        [self startTimer];
}

MARK("Conteudo")

- (void)setSequence:(const uint64_t *)values length:(size_t)len
              start:(uint64_t)start overflow:(int)overflow
{
    free(_seq);
    _seq = NULL;
    _seqLen = 0;
    _start = start;
    _overflow = overflow;

    if (values != NULL && len > 0) {
        _seq = malloc(len * sizeof(uint64_t));
        if (_seq != NULL) {
            memcpy(_seq, values, len * sizeof(uint64_t));
            _seqLen = len;
        }
    }

    _hoverIndex = -1;
    _clock = 0.0;
    _startTime = COLLATZ_TIME();
    _yaw = -0.35;
    _pitch = 0.12;
    [self setNeedsDisplay:YES];
}

- (void)clear
{
    free(_seq);
    _seq = NULL;
    _seqLen = 0;
    _overflow = 0;
    _hoverIndex = -1;
    [self setNeedsDisplay:YES];
}

- (void)setUse3D:(BOOL)on
{
    _use3d = on ? 1 : 0;
    [self setNeedsDisplay:YES];
}

- (void)setAutoRotate:(BOOL)on
{
    _autoRotate = on ? 1 : 0;
    [self setNeedsDisplay:YES];
}

MARK("Projecao")

/* Matriz de projeção: yaw/pitch + perspectiva simples (igual ao GTK). */
- (V3)project:(const V3 *)p yaw:(double)yaw pitch:(double)pitch
            cx:(double)cx cy:(double)cy scale:(double)scale
{
    double cyaw = cos(yaw), syaw = sin(yaw);
    double x1 = p->x * cyaw + p->z * syaw;
    double z1 = -p->x * syaw + p->z * cyaw;
    double y1 = p->y;

    double cpit = cos(pitch), spit = sin(pitch);
    double y2 = y1 * cpit - z1 * spit;
    double z2 = y1 * spit + z1 * cpit;

    const double d = 900.0;
    double f = d / (d + z2);
    V3 r;
    r.x = cx + x1 * scale * f;
    r.y = cy + y2 * scale * f;
    r.z = z2;
    return r;
}

/* Constrói a hélice e projeta todos os pontos. */
- (void)buildPoints:(V3 *)pts projected:(V3 *)sp count:(size_t)visible
                b:(NSRect)b
{
    double max_v = 1.0;
    for (size_t i = 0; i < visible; i++)
        if ((double)_seq[i] > max_v) max_v = (double)_seq[i];
    double log_max = log(max_v);
    if (log_max <= 0) log_max = 1.0;

    const double span_x = 900.0;
    for (size_t i = 0; i < visible; i++) {
        double t = (visible > 1) ? (double)i / (double)(visible - 1) : 0.0;
        double v = (double)_seq[i];
        double ly = (log(v) > 0) ? log(v) : 0.0;
        pts[i].x = (t - 0.5) * span_x;
        pts[i].y = -(ly / log_max) * 380.0 + 190.0;
        pts[i].z = _use3d ? sin(t * 16.0) * 130.0 : 0.0;
    }

    double cx = b.size.width * 0.5;
    double cy = b.size.height * 0.5 + 30.0;
    double scale = 0.85;
    if (b.size.width < 700)
        scale = b.size.width / 700.0;

    double yaw = _use3d ? _yaw : 0.0;
    double pitch = _use3d ? _pitch : 0.0;
    for (size_t i = 0; i < visible; i++)
        sp[i] = [self project:&pts[i] yaw:yaw pitch:pitch cx:cx cy:cy
                        scale:scale];
}

MARK("Desenho")

- (void)drawRect:(NSRect)dirtyRect
{
    NSRect b = [self bounds];

    /* Fundo com vinheta (mesmas cores do GTK). */
    NSColor *bg0 = [NSColor colorWithCalibratedRed:0.09 green:0.10
                                             blue:0.15 alpha:1.0];
    NSColor *bg1 = [NSColor colorWithCalibratedRed:0.03 green:0.03
                                             blue:0.06 alpha:1.0];
    NSGradient *grad = [[NSGradient alloc] initWithStartingColor:bg0
                                                     endingColor:bg1];
    [grad drawInRect:dirtyRect angle:90.0];
    [grad release];

    if (_seqLen == 0) {
        [self drawPlaceholder:b];
        return;
    }

    size_t visible = _seqLen;
    if (visible > SEQ_MAX_NODES) visible = SEQ_MAX_NODES;

    V3 *pts = malloc(visible * sizeof(V3));
    V3 *sp = malloc(visible * sizeof(V3));
    size_t *order = malloc((visible ? visible : 1) * sizeof(size_t));
    if (pts == NULL || sp == NULL || order == NULL) {
        free(pts); free(sp); free(order);
        return;
    }

    [self buildPoints:pts projected:sp count:visible b:b];

    size_t edges = visible > 0 ? visible - 1 : 0;
    for (size_t i = 0; i < edges; i++)
        order[i] = i;
    /* insertion sort por z médio (edges é pequeno). */
    for (size_t i = 1; i < edges; i++) {
        size_t key = order[i];
        double kz = (sp[key].z + sp[key + 1].z) * 0.5;
        size_t j = i;
        while (j > 0) {
            size_t prev = order[j - 1];
            double pz = (sp[prev].z + sp[prev + 1].z) * 0.5;
            if (pz > kz) break;
            order[j] = prev;
            j--;
        }
        order[j] = key;
    }

    /* Arestas com brilho, coloridas pela paridade. */
    for (size_t k = 0; k < edges; k++) {
        size_t i = order[k];
        int is_odd = (int)(_seq[i] & 1u); /* ímpar => 3n+1 (cresce) */
        double r = is_odd ? 1.00 : 0.27;
        double g = is_odd ? 0.51 : 0.63;
        double bl = is_odd ? 0.28 : 0.98;

        double depth = (sp[i].z + sp[i + 1].z) * 0.5;
        double dfac = 1.0 - (depth + 200.0) / 500.0;
        if (dfac < 0.25) dfac = 0.25;
        if (dfac > 1.0) dfac = 1.0;

        NSPoint a = NSMakePoint(sp[i].x, sp[i].y);
        NSPoint c = NSMakePoint(sp[i + 1].x, sp[i + 1].y);

        NSBezierPath *glow = [NSBezierPath bezierPath];
        [glow setLineWidth:9.0];
        [glow setLineCapStyle:NSLineCapStyleRound];
        [glow moveToPoint:a];
        [glow lineToPoint:c];
        [[NSColor colorWithCalibratedRed:r green:g blue:bl
                                  alpha:0.16 * dfac] setStroke];
        [glow stroke];

        NSBezierPath *edge = [NSBezierPath bezierPath];
        [edge setLineWidth:2.4];
        [edge setLineCapStyle:NSLineCapStyleRound];
        [edge moveToPoint:a];
        [edge lineToPoint:c];
        [[NSColor colorWithCalibratedRed:r green:g blue:bl
                                  alpha:0.9 * dfac] setStroke];
        [edge stroke];

        /* Ponta da seta. */
        double dx = c.x - a.x, dy = c.y - a.y;
        double len = sqrt(dx * dx + dy * dy);
        if (len > 8.0) {
            double ux = dx / len, uy = dy / len;
            double hs = 9.0;
            double ax = c.x - ux * hs;
            double ay = c.y - uy * hs;
            double px = -uy, py = ux;
            NSBezierPath *arrow = [NSBezierPath bezierPath];
            [arrow moveToPoint:c];
            [arrow lineToPoint:NSMakePoint(ax + px * hs * 0.5,
                                           ay + py * hs * 0.5)];
            [arrow lineToPoint:NSMakePoint(ax - px * hs * 0.5,
                                           ay - py * hs * 0.5)];
            [arrow closePath];
            [[NSColor colorWithCalibratedRed:r green:g blue:bl
                                      alpha:0.9 * dfac] setFill];
            [arrow fill];
        }
    }

    /* Nós (bolhas pulsantes). */
    double pulse = 0.5 + 0.5 * sin(_clock * 3.0);
    double scale = (b.size.width < 700) ? b.size.width / 700.0 : 0.85;
    double cy = b.size.height * 0.5 + 30.0;

    for (size_t i = 0; i < visible; i++) {
        double depth = sp[i].z;
        double dfac = 1.0 - (depth + 200.0) / 500.0;
        if (dfac < 0.3) dfac = 0.3;
        if (dfac > 1.0) dfac = 1.0;

        double rad = (4.0 + 2.0 * pulse) * dfac;
        if (i == 0) rad *= 1.5;
        if (i == visible - 1 && visible == _seqLen) rad *= 1.4;

        double hgt = (sp[i].y - (cy + 190.0 * scale)) / (380.0 * scale);
        double nn = (hgt > 1) ? 1 : (hgt < 0 ? 0 : hgt);
        double nr = 0.35 + 0.25 * nn;
        double ng = 0.80 - 0.15 * nn;
        double nb = 1.00 - 0.35 * nn;

        NSBezierPath *halo = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(sp[i].x - rad * 3.2, sp[i].y - rad * 3.2,
                       rad * 6.4, rad * 6.4)];
        [[NSColor colorWithCalibratedRed:nr green:ng blue:nb
                                  alpha:0.12 * dfac] setFill];
        [halo fill];

        NSBezierPath *dot = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(sp[i].x - rad, sp[i].y - rad, rad * 2, rad * 2)];
        [[NSColor colorWithCalibratedRed:nr green:ng blue:nb
                                  alpha:0.95 * dfac] setFill];
        [dot fill];

        if ((NSInteger)i == _hoverIndex) {
            NSBezierPath *ring = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(sp[i].x - rad - 5, sp[i].y - rad - 5,
                           (rad + 5) * 2, (rad + 5) * 2)];
            [ring setLineWidth:2.0];
            [[NSColor colorWithCalibratedWhite:1.0 alpha:0.9] setStroke];
            [ring stroke];
        }
    }

    /* Rótulo do nó sob o cursor. */
    if (_hoverIndex >= 0 && (size_t)_hoverIndex < visible) {
        size_t i = (size_t)_hoverIndex;
        NSString *info = [NSString stringWithFormat:@"n = %llu   -   passo %zu",
                          (unsigned long long)_seq[i], i];
        NSDictionary *attrs = DICT2(
            SYS_FONT_BOLD(13), NSFontAttributeName,
            GRAY_COLOR(0.96, 1.0), NSForegroundColorAttributeName);
        NSSize sz = [info sizeWithAttributes:attrs];
        NSRect box = NSMakeRect(sp[i].x + 12, sp[i].y - 30,
                                sz.width + 14, sz.height + 10);
        if (box.origin.x + box.size.width > b.size.width)
            box.origin.x = b.size.width - box.size.width - 6;
        if (box.origin.x < 6) box.origin.x = 6;
        if (box.origin.y < 6) box.origin.y = 6;
        [GRAY_COLOR(0.05, 0.9) setFill];
        NSBezierPath *bp = [NSBezierPath bezierPathWithRoundedRect:box
                                                           xRadius:6
                                                           yRadius:6];
        [bp fill];
        [info drawAtPoint:NSMakePoint(box.origin.x + 7, box.origin.y + 5)
           withAttributes:attrs];
    }

    /* Legenda (igual ao GTK). */
    [self drawLegend:b visible:visible];

    free(pts);
    free(sp);
    free(order);
}

- (void)drawPlaceholder:(NSRect)b
{
    NSString *msg =
        @"Digite um número e clique em Gerar para ver a animação.";
    NSDictionary *attrs = DICT2(
        SYS_FONT(15), NSFontAttributeName,
        GRAY_COLOR(0.58, 1.0), NSForegroundColorAttributeName);
    NSSize sz = [msg sizeWithAttributes:attrs];
    [msg drawAtPoint:NSMakePoint((b.size.width - sz.width) / 2.0,
                                 (b.size.height - sz.height) / 2.0)
      withAttributes:attrs];
}

- (void)drawLegend:(NSRect)b visible:(size_t)visible
{
    double y = b.size.height - 50.0;
    NSDictionary *attrs = DICT2(
        SYS_FONT(12), NSFontAttributeName,
        GRAY_COLOR(0.80, 1.0), NSForegroundColorAttributeName);

    [[NSColor colorWithCalibratedRed:0.55 green:0.85 blue:1.0 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(22, y + 2, 11, 11));
    [@"par (n/2)" drawAtPoint:NSMakePoint(38, y) withAttributes:attrs];

    [[NSColor colorWithCalibratedRed:1.0 green:0.55 blue:0.30 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(118, y + 2, 11, 11));
    [@"ímpar (3n+1)" drawAtPoint:NSMakePoint(134, y) withAttributes:attrs];

    NSString *info = [NSString stringWithFormat:@"%zu de %zu nós",
                      visible, _seqLen];
    NSSize isz = [info sizeWithAttributes:attrs];
    [info drawAtPoint:NSMakePoint(b.size.width - isz.width - 20, y)
       withAttributes:attrs];
}

MARK("Interacao")

- (void)mouseDown:(NSEvent *)event
{
    _dragging = YES;
    _autoRotate = 0;
    _lastDrag = [self convertPoint:[event locationInWindow] fromView:nil];
}

- (void)mouseDragged:(NSEvent *)event
{
    NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
    _yaw += (p.x - _lastDrag.x) * 0.01;
    _pitch += (p.y - _lastDrag.y) * 0.006;
    if (_pitch > 0.9) _pitch = 0.9;
    if (_pitch < -0.9) _pitch = -0.9;
    _lastDrag = p;
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent *)event
{
    (void)event;
    _dragging = NO;
}

- (void)mouseMoved:(NSEvent *)event
{
    [self updateHoverForEvent:event];
}

- (void)updateHoverForEvent:(NSEvent *)event
{
    NSPoint m = [self convertPoint:[event locationInWindow] fromView:nil];
    NSRect b = [self bounds];
    NSInteger best = -1;
    double bestD2 = 22.0 * 22.0;

    if (_seqLen > 0) {
        size_t visible = _seqLen > SEQ_MAX_NODES ? SEQ_MAX_NODES : _seqLen;
        V3 *pts = malloc(visible * sizeof(V3));
        V3 *sp = malloc(visible * sizeof(V3));
        if (pts != NULL && sp != NULL) {
            [self buildPoints:pts projected:sp count:visible b:b];
            for (size_t i = 0; i < visible; i++) {
                double dx = sp[i].x - m.x, dy = sp[i].y - m.y;
                double d2 = dx * dx + dy * dy;
                if (d2 < bestD2) { bestD2 = d2; best = (NSInteger)i; }
            }
        }
        free(pts);
        free(sp);
    }
    if (best != _hoverIndex) {
        _hoverIndex = best;
        [self setNeedsDisplay:YES];
    }
}

- (void)updateTrackingAreas
{
#if COLLATZ_GNUSTEP
    return;
#else
    [super updateTrackingAreas];
    NSArray *areas = [self trackingAreas];
    for (NSUInteger i = 0; i < [areas count]; i++)
        [self removeTrackingArea:[areas objectAtIndex:i]];
    NSTrackingArea *area = [[NSTrackingArea alloc]
        initWithRect:[self bounds]
             options:(NSTrackingMouseMoved | NSTrackingActiveInKeyWindow |
                      NSTrackingInVisibleRect)
               owner:self
            userInfo:nil];
    [self addTrackingArea:area];
    [area release];
#endif
}

@end
