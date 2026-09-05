#include <stdio.h>
#include <string.h>

#include "compiler/abi/abi.h"

static int argument_registers_are_valid(void)
{
    static const enum miga80_abi_register scalar[] = {
        MIGA80_ABI_D0, MIGA80_ABI_D1, MIGA80_ABI_D2
    };
    static const enum miga80_abi_register address[] = {
        MIGA80_ABI_A0, MIGA80_ABI_A1
    };
    enum miga80_abi_register reg;
    unsigned int index;

    for (index = 0; index < MIGA80_ABI_MAX_SCALAR_ARGUMENTS; ++index) {
        if (!miga80_abi_scalar_argument_register(index, &reg) ||
            reg != scalar[index]) {
            return 0;
        }
    }
    if (miga80_abi_scalar_argument_register(
            MIGA80_ABI_MAX_SCALAR_ARGUMENTS, &reg) ||
        miga80_abi_scalar_argument_register(0U, NULL)) {
        return 0;
    }

    for (index = 0; index < MIGA80_ABI_MAX_ADDRESS_ARGUMENTS; ++index) {
        if (!miga80_abi_address_argument_register(index, &reg) ||
            reg != address[index]) {
            return 0;
        }
    }
    return !miga80_abi_address_argument_register(
               MIGA80_ABI_MAX_ADDRESS_ARGUMENTS, &reg) &&
           !miga80_abi_address_argument_register(0U, NULL);
}

static int register_classes_are_valid(void)
{
    enum miga80_abi_register reg;

    for (reg = MIGA80_ABI_D0; reg < MIGA80_ABI_REGISTER_COUNT;
         reg = (enum miga80_abi_register)(reg + 1)) {
        const int caller = miga80_abi_register_is_caller_saved(reg);
        const int callee = miga80_abi_register_is_callee_saved(reg);

        if (caller && callee) {
            return 0;
        }
        if (reg == MIGA80_ABI_A7) {
            if (caller || callee) {
                return 0;
            }
        } else if (!caller && !callee) {
            return 0;
        }
        if (miga80_abi_gnu_register_name(reg) == NULL) {
            return 0;
        }
    }
    return miga80_abi_gnu_register_name(MIGA80_ABI_REGISTER_COUNT) == NULL &&
           strcmp(miga80_abi_gnu_register_name(MIGA80_ABI_D0), "%d0") == 0 &&
           strcmp(miga80_abi_gnu_register_name(MIGA80_ABI_A7), "%a7") == 0;
}

int main(void)
{
    if (MIGA80_ABI_VERSION_MAJOR != 0U ||
        MIGA80_ABI_VERSION_MINOR != 6U ||
        MIGA80_ABI_MAX_ARGUMENTS != 5U ||
        MIGA80_ABI_SCALAR_RETURN_REGISTER != MIGA80_ABI_D0 ||
        MIGA80_ABI_ADDRESS_RETURN_REGISTER != MIGA80_ABI_A0 ||
        MIGA80_ABI_RUNTIME_CONTEXT_REGISTER != MIGA80_ABI_A5 ||
        MIGA80_ABI_FRAME_POINTER_REGISTER != MIGA80_ABI_A6 ||
        MIGA80_ABI_STACK_POINTER_REGISTER != MIGA80_ABI_A7 ||
        MIGA80_ABI_RUNTIME_FAULT_HANDLER_OFFSET != 0U ||
        MIGA80_ABI_RUNTIME_CONTEXT_MIN_SIZE != 4U ||
        MIGA80_ABI_FAULT_DIVISION_BY_ZERO != 1U ||
        MIGA80_ABI_FAULT_CONVERSION_OUT_OF_RANGE != 2U ||
        MIGA80_ABI_FAULT_CODE_REGISTER != MIGA80_ABI_D0 ||
        MIGA80_ABI_FAULT_LINE_REGISTER != MIGA80_ABI_D1 ||
        MIGA80_ABI_FAULT_COLUMN_REGISTER != MIGA80_ABI_D2 ||
        MIGA80_ABI_FAULT_SCRATCH_REGISTER != MIGA80_ABI_A0 ||
        MIGA80_ABI_BOOL_FALSE != 0U || MIGA80_ABI_BOOL_TRUE != 1U ||
        MIGA80_ABI_FIX_FRACTION_BITS != 16U ||
        MIGA80_ABI_FIX_ONE != 0x00010000U ||
        MIGA80_ABI_FIX_MIN != 0x80000000U ||
        MIGA80_ABI_FIX_MAX != 0x7fffffffU ||
        !argument_registers_are_valid() || !register_classes_are_valid() ||
        !miga80_abi_frame_size_is_valid(0U) ||
        !miga80_abi_frame_size_is_valid(4U) ||
        !miga80_abi_frame_size_is_valid(MIGA80_ABI_MAX_FRAME_SIZE) ||
        miga80_abi_frame_size_is_valid(2U) ||
        miga80_abi_frame_size_is_valid(MIGA80_ABI_MAX_FRAME_SIZE + 4U)) {
        fprintf(stderr, "MIGA Lua native ABI regression failed\n");
        return 1;
    }

    printf("PASS  MIGA Lua native ABI 0.6 scalar/address, stack, and fault contract\n");
    return 0;
}
