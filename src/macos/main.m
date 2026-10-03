//
//  main.m
//  Explorador de Collatz - versão macOS/GNUstep (AppKit)
//
//  Ponto de entrada. Compatível com o runtime GNU (GNUstep) e com
//  AppKit no macOS.
//

#import <Cocoa/Cocoa.h>
#import "AppDelegate.h"
#import "GNUstepCompat.h"
#include <stdlib.h>

int main(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    // No GNUstep é preciso criar o NSApplication antes de usar fontes e
    // outros recursos que dependem do backend.
    NSApplication *app = [NSApplication sharedApplication];
    AppDelegate *delegate = [[AppDelegate alloc] init];

    // Opções de inicialização (mesma ideia do --tree/--demo do GTK):
    //   COLLATZ_TAB=0|1|2   abre na aba Árvore|Sequência|Recordes
    //   COLLATZ_DEMO=N      já preenche/gera o número N
    const char *tabEnv = getenv("COLLATZ_TAB");
    const char *demoEnv = getenv("COLLATZ_DEMO");
    if (tabEnv != NULL)
        [delegate setStartupTab:atoi(tabEnv)];
    if (demoEnv != NULL)
        [delegate setStartupNumber:strtoull(demoEnv, NULL, 10)];

    [app setDelegate:delegate];
#if !COLLATZ_GNUSTEP
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
#endif
    [app run];

    [pool release];
    return 0;
}
