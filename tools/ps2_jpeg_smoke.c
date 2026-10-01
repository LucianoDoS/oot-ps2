/* Synthetic JPEG exercises the production decoder without game assets. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>
#include "ps2_background.h"
int ps2_jpeg_smoke(void) {
    struct jpeg_compress_struct encoder;
    struct jpeg_error_mgr error;
    unsigned char* compressed = NULL;
    unsigned long length = 0;
    encoder.err = jpeg_std_error(&error);
    jpeg_create_compress(&encoder);
    jpeg_mem_dest(&encoder, &compressed, &length);
    encoder.image_width = encoder.image_height = 32;
    encoder.input_components = 3;
    encoder.in_color_space = JCS_RGB;
    jpeg_set_defaults(&encoder);
    jpeg_set_quality(&encoder, 100, TRUE);
    jpeg_start_compress(&encoder, TRUE);
    unsigned char row[32 * 3];
    while (encoder.next_scanline < 32) {
        for (int x = 0; x < 32; x++) {
            row[x * 3] = encoder.next_scanline < 16 ? 255 : 0;
            row[x * 3 + 1] = encoder.next_scanline < 16 ? 0 : 255;
            row[x * 3 + 2] = 0;
        }
        JSAMPROW rows[] = {row};
        jpeg_write_scanlines(&encoder, rows, 1);
    }
    jpeg_finish_compress(&encoder);
    jpeg_destroy_compress(&encoder);
    int failures = 0;
    unsigned char data[2048], saved[2048];
    if (length > sizeof(data)) { free(compressed); return 1; }
    memset(data, 0, sizeof(data));
    memcpy(data, compressed, length);
    memcpy(saved, data, sizeof(data));
    free(compressed);
    if (ps2_jpeg_decode(data, sizeof(data), 16, 32) != -1 || memcmp(data, saved, sizeof(data))) failures++;
    if (ps2_jpeg_decode(data, sizeof(data), 32, 32) || data[0] != 0xf8 || data[1] != 1 ||
        data[2046] != 7 || data[2047] != 0xc1) failures++;
    memcpy(saved, data, sizeof(data));
    if (ps2_jpeg_decode(data, sizeof(data), 32, 32) || memcmp(saved,data,sizeof(data))) failures++;
    memset(data, 0, sizeof(data)); data[0] = 0xff; data[1] = 0xd8;
    memcpy(saved,data,sizeof(data));
    if (ps2_jpeg_decode(data, sizeof(data), 32, 32) != -1 || memcmp(saved,data,sizeof(data))) failures++;
    printf("[PS2 JPEG TEST] %s: RGBA16 pixels, repeat, dimensions and corrupt-input preservation (%d)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures;
}
