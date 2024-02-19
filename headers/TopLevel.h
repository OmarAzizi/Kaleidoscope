#ifndef __TOPLEVEL_PARSING_H__
#define __TOPLEVEL_PARSING_H__

#include "common.h"
#include "codegen.h"

/*
    ================================================
    ========= TOP-lEVEL PARSING JIT DRIVER ========= 
    ================================================
*/

static void InitializeModuleAndManagers();
static void HandleDefinition();
static void HandleExtern();
static void HandleTopLevelExpression();
static void MainLoop();

#endif