/* Decode a room's bounded JPEG into its reserved RGBA16 background buffer.
 * Decode into temporary storage: failure must leave the original data intact. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>
#include "ps2_background.h"

typedef struct { struct jpeg_error_mgr base; jmp_buf recovery; } DecodeError;
static void decode_error(j_common_ptr decoder) {
    DecodeError* error = (DecodeError*)decoder->err;
    longjmp(error->recovery, 1);
}
static void decode_warning(j_common_ptr decoder, int level) {
    /* Reject truncated/corrupt input, including libjpeg's synthetic EOI. */
    if (level < 0) decode_error(decoder);
}
int ps2_jpeg_decode(void* data, unsigned int capacity, unsigned int width, unsigned int height) {
    if (!data || !width || width > 320 || !height || height > 240 ||
        width * height * 2 > capacity || capacity < 4) return -1;
    unsigned char* source = data;
    if (source[0] != 0xff || source[1] != 0xd8) return 0;
    struct jpeg_decompress_struct decoder;
    DecodeError error;
    unsigned char* volatile output = NULL;
    volatile int created = 0;
    memset(&decoder, 0, sizeof(decoder));
    decoder.err = jpeg_std_error(&error.base);
    error.base.error_exit = decode_error;
    error.base.emit_message = decode_warning;
    if (setjmp(error.recovery)) {
        if (created) jpeg_destroy_decompress(&decoder);
        free(output);
        printf("[PS2 JPEG] Decode failed; source preserved\n");
        return -1;
    }
    jpeg_create_decompress(&decoder);
    created = 1;
    jpeg_mem_src(&decoder, source, capacity);
    jpeg_read_header(&decoder, TRUE);
    if (decoder.image_width != width || decoder.image_height != height) {
        jpeg_destroy_decompress(&decoder);
        return -1;
    }
    decoder.out_color_space = JCS_RGB;
    jpeg_start_decompress(&decoder);
    output = malloc(width * height * 2);
    if (!output) { jpeg_destroy_decompress(&decoder); return -1; }
    unsigned char row[320 * 3];
    while (decoder.output_scanline < height) {
        unsigned int y = decoder.output_scanline;
        JSAMPROW rows[] = {row};
        if (jpeg_read_scanlines(&decoder, rows, 1) != 1) decode_error((j_common_ptr)&decoder);
        for (unsigned int x = 0; x < width; x++) {
            unsigned int color = ((row[x * 3] >> 3) << 11) | ((row[x * 3 + 1] >> 3) << 6) |
                                 ((row[x * 3 + 2] >> 3) << 1) | 1;
            unsigned int offset = (y * width + x) * 2;
            output[offset] = color >> 8;
            output[offset + 1] = color;
        }
    }
    jpeg_finish_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
    memcpy(data, output, width * height * 2);
    free(output);
    printf("[PS2 JPEG] Decoded %ux%u RGBA16 background\n", width, height);
    return 0;
}
