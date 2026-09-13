#include "compiler/abi/abi.h"

#include <stddef.h>

int miga80_abi_scalar_argument_register(
    unsigned int index, enum miga80_abi_register *result)
{
    if (result == NULL || index >= MIGA80_ABI_MAX_SCALAR_ARGUMENTS) {
        return 0;
    }
    *result = (enum miga80_abi_register)(MIGA80_ABI_D0 + index);
    return 1;
}

int miga80_abi_address_argument_register(
    unsigned int index, enum miga80_abi_register *result)
{
    if (result == NULL || index >= MIGA80_ABI_MAX_ADDRESS_ARGUMENTS) {
        return 0;
    }
    *result = (enum miga80_abi_register)(MIGA80_ABI_A0 + index);
    return 1;
}

int miga80_abi_register_is_caller_saved(enum miga80_abi_register reg)
{
    return (reg >= MIGA80_ABI_D0 && reg <= MIGA80_ABI_D2) ||
           (reg >= MIGA80_ABI_A0 && reg <= MIGA80_ABI_A1);
}

int miga80_abi_register_is_callee_saved(enum miga80_abi_register reg)
{
    return (reg >= MIGA80_ABI_D3 && reg <= MIGA80_ABI_D7) ||
           (reg >= MIGA80_ABI_A2 && reg <= MIGA80_ABI_A6);
}

int miga80_abi_frame_size_is_valid(unsigned int size)
{
    return size <= MIGA80_ABI_MAX_FRAME_SIZE &&
           size % MIGA80_ABI_STACK_ALIGNMENT == 0U;
}

const char *miga80_abi_gnu_register_name(enum miga80_abi_register reg)
{
    static const char *const names[MIGA80_ABI_REGISTER_COUNT] = {
        "%d0", "%d1", "%d2", "%d3", "%d4", "%d5", "%d6", "%d7",
        "%a0", "%a1", "%a2", "%a3", "%a4", "%a5", "%a6", "%a7"
    };

    if (reg < MIGA80_ABI_D0 || reg >= MIGA80_ABI_REGISTER_COUNT) {
        return NULL;
    }
    return names[reg];
}

/* Quarter-wave Q16.16 table, round(sin(i*pi/512)*65536), i=0..256.
 * Q32 range reduction avoids cumulative error at large Q16.16 angles. */
static const uint32_t sine_quarter[257] = {
    0U, 402U, 804U, 1206U, 1608U, 2010U, 2412U, 2814U,
    3216U, 3617U, 4019U, 4420U, 4821U, 5222U, 5623U, 6023U,
    6424U, 6824U, 7224U, 7623U, 8022U, 8421U, 8820U, 9218U,
    9616U, 10014U, 10411U, 10808U, 11204U, 11600U, 11996U, 12391U,
    12785U, 13180U, 13573U, 13966U, 14359U, 14751U, 15143U, 15534U,
    15924U, 16314U, 16703U, 17091U, 17479U, 17867U, 18253U, 18639U,
    19024U, 19409U, 19792U, 20175U, 20557U, 20939U, 21320U, 21699U,
    22078U, 22457U, 22834U, 23210U, 23586U, 23961U, 24335U, 24708U,
    25080U, 25451U, 25821U, 26190U, 26558U, 26925U, 27291U, 27656U,
    28020U, 28383U, 28745U, 29106U, 29466U, 29824U, 30182U, 30538U,
    30893U, 31248U, 31600U, 31952U, 32303U, 32652U, 33000U, 33347U,
    33692U, 34037U, 34380U, 34721U, 35062U, 35401U, 35738U, 36075U,
    36410U, 36744U, 37076U, 37407U, 37736U, 38064U, 38391U, 38716U,
    39040U, 39362U, 39683U, 40002U, 40320U, 40636U, 40951U, 41264U,
    41576U, 41886U, 42194U, 42501U, 42806U, 43110U, 43412U, 43713U,
    44011U, 44308U, 44604U, 44898U, 45190U, 45480U, 45769U, 46056U,
    46341U, 46624U, 46906U, 47186U, 47464U, 47741U, 48015U, 48288U,
    48559U, 48828U, 49095U, 49361U, 49624U, 49886U, 50146U, 50404U,
    50660U, 50914U, 51166U, 51417U, 51665U, 51911U, 52156U, 52398U,
    52639U, 52878U, 53114U, 53349U, 53581U, 53812U, 54040U, 54267U,
    54491U, 54714U, 54934U, 55152U, 55368U, 55582U, 55794U, 56004U,
    56212U, 56418U, 56621U, 56823U, 57022U, 57219U, 57414U, 57607U,
    57798U, 57986U, 58172U, 58356U, 58538U, 58718U, 58896U, 59071U,
    59244U, 59415U, 59583U, 59750U, 59914U, 60075U, 60235U, 60392U,
    60547U, 60700U, 60851U, 60999U, 61145U, 61288U, 61429U, 61568U,
    61705U, 61839U, 61971U, 62101U, 62228U, 62353U, 62476U, 62596U,
    62714U, 62830U, 62943U, 63054U, 63162U, 63268U, 63372U, 63473U,
    63572U, 63668U, 63763U, 63854U, 63944U, 64031U, 64115U, 64197U,
    64277U, 64354U, 64429U, 64501U, 64571U, 64639U, 64704U, 64766U,
    64827U, 64884U, 64940U, 64993U, 65043U, 65091U, 65137U, 65180U,
    65220U, 65259U, 65294U, 65328U, 65358U, 65387U, 65413U, 65436U,
    65457U, 65476U, 65492U, 65505U, 65516U, 65525U, 65531U, 65535U,
    65536U,
};

static int32_t fixed_trig(int32_t radians, uint32_t quarter_turn)
{
    const int64_t tau = INT64_C(26986075409);
    int64_t angle = ((int64_t)radians * 65536) % tau;
    uint32_t phase, position, index, fraction, value;
    if (angle < 0) { angle += tau; }
    phase = (uint32_t)((uint64_t)angle * 65536U / (uint64_t)tau);
    phase = (phase + quarter_turn) & 65535U;
    position = phase & 16383U;
    if ((phase & 16384U) != 0U) { position = 16384U - position; }
    index = position >> 6;
    fraction = position & 63U;
    value = sine_quarter[index];
    if (fraction != 0U) {
        value += ((sine_quarter[index + 1U] - value) * fraction + 32U) >> 6;
    }
    return (phase & 32768U) != 0U ? -(int32_t)value : (int32_t)value;
}

int32_t miga80_fix_sin(int32_t radians) { return fixed_trig(radians, 0U); }
int32_t miga80_fix_cos(int32_t radians) { return fixed_trig(radians, 16384U); }
