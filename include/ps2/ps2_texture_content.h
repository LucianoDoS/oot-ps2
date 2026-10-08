#ifndef PS2_TEXTURE_CONTENT_H
#define PS2_TEXTURE_CONTENT_H

#include "ps2_types.h"

/* Hash only the loaded texels, excluding RAM row padding. Some paired
 * effects chain XXH32 per row even when their source is contiguous. */
typedef struct {
    const u8* source;
    u32 rowBytes, stride, rows;
    bool hashRows;
} PS2TextureSourceSpan;

typedef struct {
    u32 hits, hashes, comparedBytes, hashedBytes;
    /* Size bypasses count bindings; mutations refresh an existing layout.
     * New admissions can lose descriptors or overwrite ring copy regions. */
    u32 smallBypass, largeBypass, mutations, newSnapshots;
    u32 descriptorEvictions, overlapEvictions;
} PS2TextureContentStats;

/* An old fingerprint is reused only after comparing every loaded byte
 * against its saved copy. The bounded EE cache has no frame/address shortcut. */
u32 ps2_texture_content_hash(const PS2TextureSourceSpan* spans, unsigned count);
PS2TextureContentStats ps2_texture_content_stats(bool reset);

#ifdef PS2_RENDERER_SMOKE_CAPTURE
void ps2_texture_content_test_reset(void);
void ps2_texture_content_test_enable(bool enabled);
bool ps2_texture_content_test_equal(const u8* source, const u8* copy, unsigned bytes);
#endif
#endif
