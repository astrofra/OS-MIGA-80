# ptplayer provenance

Frank Wille's **ptplayer 6.4**, released 2024-06-26, based on ProTracker 2.3B.
Upstream archive: https://aminet.net/mus/play/ptplayer.lha
Documentation: https://aminet.net/package/mus/play/ptplayer

Archive SHA-256: `5ba1285e91413f6b6cb72768c0c31cff90029af6920870d9f589689aa2e2161a`.
`ptplayer.asm`, `ptplayer.readme` and `LICENSE` are unchanged upstream files.
The author dedicates the player to the public domain; the full notice is
also distributed as `PTPLAYER.TXT` on the reference ADF.

`src/audio/ptplayer_host.asm` includes a generated copy, assembled by vasm
as 68000 Hunk code with `OSCOMPAT=1`, `MINIMAL=0`, `ENABLE_SAWRECT=1`,
`NO_TIMERS=0`, `VBLANK_MUSIC=0`, `NULL_IS_CLEARED=0`, no small-data base.
It retains upstream CIA timing and Paula register writes.

`scripts/prepare-ptplayer.py` applies checked substitutions to that copy:
empty samples and non-looping samples idle on an allocated Chip-RAM zero word,
including repeat lengths zero and one. Address zero and the first word of an
arbitrary sample are never used for silence. The final END directive is removed
so the including file can append its adapters.

The adapter uses minimum allocation priority (upstream uses maximum), resets
all channel effect memories before a new song, initializes silent sample and
period-table pointers even before the first instrument, cancels both CIA timers and any
pending DMA restart when stopping, and exposes C-callable entry points, next-row
position, tick count and the observed Paula DMA mask. The host restores the
previous audio filter bit after releasing the player. The original MOD stays
immutable; each play uses a fresh, validated, mutable Chip-RAM copy.
