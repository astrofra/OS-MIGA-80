#ifndef MIGA80_AUDIO_MOD_H
#define MIGA80_AUDIO_MOD_H
#include <stddef.h>
#include <stdint.h>
#define MIGA80_MOD_MAX_BYTES (256U * 1024U)
struct miga80_mod_info {
    uint32_t sample_offset, sample_bytes, file_bytes;
    uint16_t patterns;
    uint8_t song_length;
};
/* No allocation or mutation; validate all bounds before handing data to ASM. */
const char *miga80_mod_validate(const uint8_t *data, size_t bytes,
                               struct miga80_mod_info *info);
/* Mutable replay copy: normalize empty samples, leaving the original intact. */
void miga80_mod_prepare(uint8_t *copy, const uint8_t *data,
                        const struct miga80_mod_info *info);
#endif
