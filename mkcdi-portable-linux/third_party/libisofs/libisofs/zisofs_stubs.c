/* Stubs for zisofs symbols when zlib is unavailable (aarch64 cross-compile).
   Dreamcast ISOs never use compressed streams, so these are never called. */
#include <stdint.h>
#include "libisofs.h"

int iso_zisofs2_enable_susp_z2 = 0;

int iso_stream_zisofs_discard_bpt(IsoStream *stream, int flag) { return 0; }

int ziso_is_zisofs_stream(IsoStream *stream, int *stream_type,
                          int *algo, int *header_size_div4, int *block_size_log2) {
    return 0;
}
