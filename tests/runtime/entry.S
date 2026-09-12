#include "compiler/abi/runtime.h"
        .text
        .even
test_entry:
        lea     _miga80_runtime_fault(%pc),%a0
        move.l  %a0,MIGA80_ABI_RUNTIME_FAULT_HANDLER_OFFSET(%a5)
#ifdef FORCE_FAULT
        lea     _miga80_runtime_test_fault(%pc),%a0
#else
        lea     _miga80_runtime_pset(%pc),%a0
#endif
        move.l  %a0,MIGA80_ABI_RUNTIME_PSET_HANDLER_OFFSET(%a5)
        move.l  %a5,-(%a7)
        lea     .L_generated(%pc),%a0
        move.l  %a0,-(%a7)
        bsr     _miga80_execute_generated
        addq.l  #8,%a7
        rts

#include "src/demo/runtime_guarded.S"
        .balign 4
.L_generated:
        .incbin CODE_FILE
