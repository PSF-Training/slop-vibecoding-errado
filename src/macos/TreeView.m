//
//  TreeView.m
//  Explorador de Collatz - versão macOS/GNUstep
//
//  Desenha a ÁRVORE de Collatz em camadas: cada número escolhido é uma
//  folha no topo; os caminhos descem aplicando as regras e, quando dois
//  caminhos chegam ao mesmo valor, passam a compartilhar o MESMO nó
//  (fusão), formando o tronco comum até o 1. É o desenho clássico da
//  Collatz tree.
//
//  Os caminhos dos números digitados ficam destacados; o restante da
//  árvore aparece mais apagado, como fundo.
//
//  Animação por NSTimer; gerenciamento manual de memória (compatível
//  com o runtime GNU do GNUstep e com ARC no macOS).
//

#import "TreeView.h"
#import "GNUstepCompat.h"
#include "collatz_tree.h"
#import <math.h>
#include <stdlib.h>
#include <stdint.h>

@implementation TreeView

@synthesize wrapLen = _wrapLen;

- (instancetype)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if (self) {
        _wrapLen = 160;
        _zoom = 1.0;
        _panX = 0.0;
        _panY = 0.0;
        _hasGraph = NO;
        _hoverNode = -1;
        WANT_LAYER(self);
        [self startTimer];
    }
    return self;
}

- (void)dealloc
{
    [self stopTimer];
    if (_hasGraph)
        collatz_graph_free(&_graph);
    [super dealloc];
}

- (void)stopTimer
{
    if (_timer != nil) {
        [_timer invalidate];
        [_timer release];
        _timer = nil;
    }
}

- (BOOL)isFlipped
{
    return YES; // topo em y=0
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)isOpaque
{
    return YES;
}

MARK("Animacao")

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

- (void)tick:(NSTimer *)timer
{
    (void)timer;
    _clock = COLLATZ_TIME() - _startTime;
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

- (void)restartAnimation
{
    _startTime = COLLATZ_TIME();
    _clock = 0.0;
    [self setNeedsDisplay:YES];
}

MARK("Grafo")

- (void)setNumbers:(NSArray *)numbers
{
    if (_hasGraph) {
        collatz_graph_free(&_graph);
        _hasGraph = NO;
    }

    NSUInteger n = [numbers count];
    if (n == 0) {
        [self setNeedsDisplay:YES];
        return;
    }

    uint64_t *vals = (uint64_t *)malloc(n * sizeof(uint64_t));
    NSUInteger k = 0;
    for (NSUInteger i = 0; i < n; i++) {
        NSNumber *num = [numbers objectAtIndex:i];
        unsigned long long v = [num unsignedLongLongValue];
        if (v > 0)
            vals[k++] = (uint64_t)v;
    }
    if (k == 0) {
        free(vals);
        [self setNeedsDisplay:YES];
        return;
    }

    if (collatz_graph_build(vals, k, &_graph) == 0)
        _hasGraph = YES;
    free(vals);

    _hoverNode = -1;
    [self restartAnimation];
    [self fitToView];
}

MARK("Zoom")

- (double)usableWidth:(NSRect)b
{
    double w = b.size.width - 2 * 70.0;
    return w < 1.0 ? 1.0 : w;
}

- (double)usableHeight:(NSRect)b
{
    double h = b.size.height - 2 * 90.0;
    return h < 1.0 ? 1.0 : h;
}

- (void)fitToView
{
    _zoom = 1.0;
    _panX = 0.0;
    _panY = 0.0;
    [self setNeedsDisplay:YES];
}

- (NSPoint)screenPointX:(double)nx y:(double)ny view:(NSRect)b
{
    double uw = [self usableWidth:b];
    double uh = [self usableHeight:b];
    double cx = b.origin.x + 70.0 + _panX;
    double cy = b.origin.y + 90.0 + _panY;
    return NSMakePoint(cx + nx * uw * _zoom, cy + ny * uh * _zoom);
}

MARK("Desenho")

- (void)drawRect:(NSRect)dirtyRect
{
    NSRect b = [self bounds];

    NSColor *bg = [NSColor colorWithCalibratedRed:0.055 green:0.063
                                            blue:0.102 alpha:1.0];
    [bg setFill];
    NSRectFill(dirtyRect);

    if (!_hasGraph || _graph.node_count == 0) {
        [self drawPlaceholder:b];
        return;
    }

    int maxDepth = _graph.max_depth > 0 ? _graph.max_depth : 1;
    double revealSpeed = maxDepth / 3.0;
    if (revealSpeed < 8.0) revealSpeed = 8.0;
    double revealDepth = _clock * revealSpeed;

    /* Arestas: primeiro as de fundo, depois as destacadas por cima. */
    for (int pass = 0; pass < 2; pass++) {
        for (size_t e = 0; e < _graph.edge_count; e++) {
            CollatzGraphEdge *ed = &_graph.edges[e];
            if ((ed->highlighted ? 1 : 0) != pass)
                continue;
            CollatzGraphNode *a = &_graph.nodes[ed->from];
            /* Revela pela profundidade do nó de origem (mais raso = topo). */
            double reveal = (double)(maxDepth - a->depth);
            if (reveal > revealDepth && !ed->highlighted)
                continue;
            if (reveal > revealDepth + 6.0)
                continue;

            NSPoint pa = [self screenPointX:a->x y:a->y view:b];
            NSPoint pb = [self screenPointX:_graph.nodes[ed->to].x
                                          y:_graph.nodes[ed->to].y
                                         view:b];

            NSColor *col = ed->grew
                ? [NSColor colorWithCalibratedRed:1.0 green:0.51 blue:0.28 alpha:1.0]
                : [NSColor colorWithCalibratedRed:0.22 green:0.76 blue:0.42 alpha:1.0];

            /* Glow + traço. */
            if (ed->highlighted) {
                NSBezierPath *glow = [NSBezierPath bezierPath];
                [glow setLineWidth:6.0];
                [glow setLineCapStyle:NSLineCapStyleRound];
                [glow moveToPoint:pa];
                [glow lineToPoint:pb];
                [[col colorWithAlphaComponent:0.16] setStroke];
                [glow stroke];
            }

            NSBezierPath *edge = [NSBezierPath bezierPath];
            [edge setLineWidth:ed->highlighted ? 2.2 : 1.2];
            [edge setLineCapStyle:NSLineCapStyleRound];
            [edge moveToPoint:pa];
            [edge lineToPoint:pb];
            [[col colorWithAlphaComponent:ed->highlighted ? 1.0 : 0.35]
                setStroke];
            [edge stroke];

            /* Ponta da seta. */
            double dx = pb.x - pa.x, dy = pb.y - pa.y;
            double len = sqrt(dx * dx + dy * dy);
            if (len > 10.0) {
                double ux = dx / len, uy = dy / len;
                double hs = ed->highlighted ? 8.0 : 6.0;
                double ax = pb.x - ux * hs;
                double ay = pb.y - uy * hs;
                double px = -uy, py = ux;
                NSBezierPath *arrow = [NSBezierPath bezierPath];
                [arrow moveToPoint:pb];
                [arrow lineToPoint:NSMakePoint(ax + px * hs * 0.45,
                                               ay + py * hs * 0.45)];
                [arrow lineToPoint:NSMakePoint(ax - px * hs * 0.45,
                                               ay - py * hs * 0.45)];
                [arrow closePath];
                [[col colorWithAlphaComponent:ed->highlighted ? 1.0 : 0.35]
                    setFill];
                [arrow fill];
            }
        }
    }

    /* Nós. */
    double pulse = 0.5 + 0.5 * sin(_clock * 3.0);
    for (size_t i = 0; i < _graph.node_count; i++) {
        CollatzGraphNode *nd = &_graph.nodes[i];
        double reveal = (double)(maxDepth - nd->depth);
        if (reveal > revealDepth && !nd->highlighted)
            continue;
        if (reveal > revealDepth + 6.0)
            continue;

        NSPoint pt = [self screenPointX:nd->x y:nd->y view:b];
        BOOL one = (nd->value == 1);
        BOOL leaf = nd->is_leaf;
        BOOL trunk = nd->is_trunk;

        double r = 2.6;
        if (nd->highlighted) r = 3.4;
        if (trunk) r = 5.0;
        if (leaf) r = 6.5;
        if (one) r = 8.0;
        r += 1.2 * pulse;
        if (!nd->highlighted && !leaf && !trunk && !one)
            r *= 0.9;

        NSColor *col;
        if (one)
            col = [NSColor colorWithCalibratedRed:0.47 green:0.94
                                            blue:0.71 alpha:1.0];
        else if (leaf)
            col = nd->highlighted ? [self columnColor:nd->leaf_color]
                                  : [NSColor colorWithCalibratedWhite:0.55
                                                                alpha:1.0];
        else if (trunk)
            col = [NSColor colorWithCalibratedRed:0.22 green:0.76
                                            blue:0.42 alpha:1.0];
        else if (nd->highlighted)
            col = [NSColor colorWithCalibratedRed:0.56 green:0.77
                                            blue:1.0 alpha:1.0];
        else
            col = [NSColor colorWithCalibratedRed:0.30 green:0.42
                                            blue:0.62 alpha:0.7];

        double haloAlpha = (nd->highlighted || leaf || trunk || one) ? 0.14 : 0.05;
        NSBezierPath *halo = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(pt.x - r * 2.4, pt.y - r * 2.4, r * 4.8, r * 4.8)];
        [[col colorWithAlphaComponent:haloAlpha] setFill];
        [halo fill];

        NSBezierPath *dot = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(pt.x - r, pt.y - r, r * 2, r * 2)];
        [col setFill];
        [dot fill];

        /* Rótulo: folhas, troncos e o 1. */
        if (leaf || trunk || one) {
            NSString *label = [NSString stringWithFormat:@"%llu",
                               (unsigned long long)nd->value];
            NSDictionary *attrs = DICT2(
                MONO_FONT_BOLD(leaf || one ? 13 : 11), NSFontAttributeName,
                (nd->highlighted || one || trunk
                    ? GRAY_COLOR(0.96, 1.0) : GRAY_COLOR(0.66, 1.0)),
                NSForegroundColorAttributeName);
            [label drawAtPoint:NSMakePoint(pt.x + r + 3, pt.y - 14)
                withAttributes:attrs];
        }
    }

    /* Tooltip do nó sob o cursor. */
    if (_hoverNode >= 0 && (size_t)_hoverNode < _graph.node_count) {
        CollatzGraphNode *nd = &_graph.nodes[_hoverNode];
        NSPoint pt = [self screenPointX:nd->x y:nd->y view:b];
        NSString *info = [NSString stringWithFormat:@"n = %llu  -  %d passos ate 1",
                          (unsigned long long)nd->value, nd->depth];
        NSDictionary *attrs = DICT2(
            SYS_FONT_BOLD(13), NSFontAttributeName,
            GRAY_COLOR(0.97, 1.0), NSForegroundColorAttributeName);
        NSSize sz = [info sizeWithAttributes:attrs];
        NSRect box = NSMakeRect(pt.x + 12, pt.y - 30,
                                sz.width + 14, sz.height + 10);
        if (box.origin.x + box.size.width > b.size.width)
            box.origin.x = b.size.width - box.size.width - 6;
        [GRAY_COLOR(0.05, 0.9) setFill];
        NSBezierPath *bp = [NSBezierPath bezierPathWithRoundedRect:box
                                                           xRadius:6
                                                           yRadius:6];
        [bp fill];
        [info drawAtPoint:NSMakePoint(box.origin.x + 7, box.origin.y + 5)
           withAttributes:attrs];
    }

    [self drawLegend:b];
}

- (NSColor *)columnColor:(int)index
{
    static const double pal[8][3] = {
        {1.00, 0.55, 0.25}, {0.44, 0.86, 0.55}, {0.55, 0.66, 1.00},
        {1.00, 0.45, 0.62}, {0.95, 0.82, 0.30}, {0.45, 0.90, 0.90},
        {0.75, 0.55, 0.95}, {0.98, 0.68, 0.40}
    };
    int k = index < 0 ? 0 : index % 8;
    return [NSColor colorWithCalibratedRed:pal[k][0]
                                     green:pal[k][1]
                                      blue:pal[k][2]
                                     alpha:1.0];
}

- (void)drawPlaceholder:(NSRect)b
{
    NSString *msg = @"Digite um ou mais números e clique em Desenhar árvore.";
    NSDictionary *attrs = DICT2(
        SYS_FONT(15), NSFontAttributeName,
        GRAY_COLOR(0.6, 1.0), NSForegroundColorAttributeName);
    NSSize sz = [msg sizeWithAttributes:attrs];
    [msg drawAtPoint:NSMakePoint((b.size.width - sz.width) / 2.0,
                                 (b.size.height - sz.height) / 2.0)
      withAttributes:attrs];
}

- (void)drawLegend:(NSRect)b
{
    double x = 22.0, y = b.size.height - 28.0;
    NSDictionary *attrs = DICT2(
        SYS_FONT(12), NSFontAttributeName,
        GRAY_COLOR(0.80, 1.0), NSForegroundColorAttributeName);

    NSColor *cresce = [NSColor colorWithCalibratedRed:1.0 green:0.51
                                                blue:0.28 alpha:1.0];
    [cresce setFill];
    NSRectFill(NSMakeRect(x, y + 2, 11, 11));
    [@"3n+1 (cresce)" drawAtPoint:NSMakePoint(x + 17, y) withAttributes:attrs];

    x += 140.0;
    NSColor *cai = [NSColor colorWithCalibratedRed:0.22 green:0.76
                                             blue:0.42 alpha:1.0];
    [cai setFill];
    NSRectFill(NSMakeRect(x, y + 2, 11, 11));
    [@"n/2 (cai)" drawAtPoint:NSMakePoint(x + 17, y) withAttributes:attrs];

    x += 130.0;
    NSColor *trunk = [NSColor colorWithCalibratedRed:0.22 green:0.76
                                               blue:0.42 alpha:1.0];
    [trunk setFill];
    NSBezierPath *tdot = [NSBezierPath bezierPathWithOvalInRect:
        NSMakeRect(x + 1, y + 2, 10, 10)];
    [tdot fill];
    [@"fusão" drawAtPoint:NSMakePoint(x + 17, y) withAttributes:attrs];

    size_t trunks = 0;
    for (size_t i = 0; i < _graph.node_count; i++)
        if (_graph.nodes[i].is_trunk) trunks++;

    NSString *info = [NSString stringWithFormat:
        @"%zu nós • %zu fusões • %d níveis",
        _graph.node_count, trunks, _graph.max_depth];
    NSSize isz = [info sizeWithAttributes:attrs];
    [info drawAtPoint:NSMakePoint(b.size.width - isz.width - 20, y)
       withAttributes:attrs];
}

MARK("Interacao")

- (void)mouseDown:(NSEvent *)event
{
    _dragging = YES;
    _lastDrag = [self convertPoint:[event locationInWindow] fromView:nil];
}

- (void)mouseDragged:(NSEvent *)event
{
    NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
    _panX += (p.x - _lastDrag.x);
    _panY += (p.y - _lastDrag.y);
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
    double bestD2 = 16.0 * 16.0;

    for (size_t i = 0; i < _graph.node_count; i++) {
        NSPoint pt = [self screenPointX:_graph.nodes[i].x
                                      y:_graph.nodes[i].y
                                     view:b];
        double dx = pt.x - m.x, dy = pt.y - m.y;
        double d2 = dx * dx + dy * dy;
        if (d2 < bestD2) {
            bestD2 = d2;
            best = (NSInteger)i;
        }
    }
    if (best != _hoverNode) {
        _hoverNode = best;
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

- (void)scrollWheel:(NSEvent *)event
{
#if COLLATZ_GNUSTEP
    double d = [event deltaY];
#else
    double d = [event scrollingDeltaY];
#endif
    _zoom *= (1.0 + d * 0.01);
    if (_zoom < 0.3) _zoom = 0.3;
    if (_zoom > 8.0) _zoom = 8.0;
    [self setNeedsDisplay:YES];
}

@end
