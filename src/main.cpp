#include "../headers/TopLevel.h"

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main() {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    // Prime the first token.
    fprintf(stderr, "ready> ");
    getNextToken();

    TheJIT = ExitOnErr(KaleidoscopeJIT::Create());
    
    // Make the module which holds all the code
    InitializeModuleAndManagers();

    // Run the main "interpreter loop" now.
    MainLoop();

    return 0;
}