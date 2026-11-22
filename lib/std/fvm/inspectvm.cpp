// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The interpreter loop of the bytecode VM with the hooks of the execution observer
 * (camel/execute/observer.h): what a program runs on while a debugger is attached.
 *
 * It is gotovm.cpp compiled a second time under another name, with its two hook points defined:
 * before each instruction, and where a fault is caught. The loop of a normal run
 * (FastVMSchedPass::callBorrowed, gotovm.cpp compiled by itself) has neither. A hook there costs
 * even when it is never taken: one call at the dispatch point, behind the inspection-mode
 * branch, moved the program counter of the loop from a register to the stack and made
 * call-bound programs 13-19% slower without a debugger (docs/technical/35_debugger.md, 9).
 * FastVMSchedPass::call chooses the loop per activation.
 */

#include "fastvm.h"

#define CAMEL_FVM_INSPECTED_LOOP 1
// Every use of the name in gotovm.cpp is the definition of the loop; fastvm.h is already read.
#define callBorrowed callBorrowedInspected
#define SYNC_RUNTIME_ORIGIN()                                                                      \
    do {                                                                                           \
        if (InternalGlobalConfig::IsInspectionMode() && context_) {                                \
            inspectInstruction(pc, currFrame);                                                     \
        }                                                                                          \
    } while (0)
#define OBSERVE_RUNTIME_FAULT(fault) observeFault(fault)

#include "gotovm.cpp" // NOLINT(bugprone-suspicious-include)
