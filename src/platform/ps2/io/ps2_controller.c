/* Alternate validated SRAM files preserve the last complete image if a write
 * is interrupted. The game never formats memory cards. */
#include <kernel.h>
#include <libmc.h>
/* libmc is an RPC API, not a newlib file descriptor API. */
#define NEWLIB_PORT_AWARE
#include <io_common.h>
#include <string.h>
#include <stdio.h>
#include "ps2_types.h"

#define SAVE_PORT 0
#define SAVE_SLOT 0
#ifndef PS2_SAVE_DIR
#define PS2_SAVE_DIR "OOT_PS2"
#endif
#define SAVE_DIR PS2_SAVE_DIR
#define SAVE_MAGIC 0x4F4F5432u
#define SAVE_VERSION 2
#define OOT_SRAM_SIZE 0x8000
static const char* s_paths[] = {"/" SAVE_DIR "/save.dat", "/" SAVE_DIR "/save.bak"};
typedef struct {
    u32 magic, version, size, checksum, generation;
    u8 data[OOT_SRAM_SIZE];
} PS2SaveFile;
static PS2SaveFile s_file __attribute__((aligned(64)));
static u8 s_latestData[OOT_SRAM_SIZE];
static u32 s_generation, s_latestSize;
static int s_latestSlot = -1;
static bool s_mcReady, s_scanned;

static u32 save_checksum(const u8* data, u32 size) {
    u32 sum = 0x12345678u;
    for (u32 i = 0; i < size; i++) sum = (sum << 3) ^ (sum >> 29) ^ data[i];
    return sum;
}
static int wait_result(int submitted) {
    int result;
    if (submitted < 0 || mcSync(MC_WAIT, NULL, &result) < 0) return -1;
    return result;
}
void ps2_save_init(void) {
    int type = MC_TYPE_NONE, freeSpace = 0, formatted = 0;
    s_mcReady = s_scanned = false;
    s_latestSlot = -1;
    int result = wait_result(mcGetInfo(SAVE_PORT, SAVE_SLOT, &type, &freeSpace, &formatted));
    if (result < -1 || type != MC_TYPE_PS2 || !formatted) {
        printf("[Save] No hay memory card PS2 formateada en el puerto %d\n", SAVE_PORT);
        return;
    }
    result = wait_result(mcMkDir(SAVE_PORT, SAVE_SLOT, SAVE_DIR));
    if (result < 0 && result != -4) {
        printf("[Save] No se pudo preparar el directorio (%d)\n", result);
        return;
    }
    s_mcReady = true;
    printf("[Save] Memory card lista (%s)\n", SAVE_DIR);
}
/* Read the original 16-byte v1 header as well as the new revision header. */
static int read_file(int slot) {
    /* libmc forwards IOP flags unchanged; newlib's O_RDONLY is zero. */
    int fd = wait_result(mcOpen(SAVE_PORT, SAVE_SLOT, s_paths[slot], FIO_O_RDONLY));
    if (fd < 0) return -1;
    memset(&s_file, 0, sizeof(s_file));
    int bytes = wait_result(mcRead(fd, &s_file, sizeof(s_file)));
    int closed = wait_result(mcClose(fd));
    if (closed < 0 || s_file.magic != SAVE_MAGIC || s_file.size > OOT_SRAM_SIZE) {
        printf("[Save] Read bytes=%d close=%d magic=%08x size=%u\n", bytes, closed,
               (unsigned)s_file.magic, (unsigned)s_file.size);
        return -1;
    }
    if (s_file.version == 1 && bytes == sizeof(s_file) - 4) {
        memmove(s_file.data, &s_file.generation, OOT_SRAM_SIZE);
        s_file.generation = 0;
    } else if (s_file.version != SAVE_VERSION || bytes != sizeof(s_file)) {
        printf("[Save] Read header version=%u bytes=%d\n", (unsigned)s_file.version, bytes);
        return -1;
    }
    u32 expected = save_checksum(s_file.data, s_file.size);
    if (s_file.version == SAVE_VERSION) expected ^= s_file.generation;
    if (expected != s_file.checksum) printf("[Save] Read checksum=%08x expected=%08x\n", (unsigned)s_file.checksum, (unsigned)expected);
    return expected == s_file.checksum ? 0 : -1;
}
static int scan_card(void) {
    if (!s_mcReady) ps2_save_init();
    if (!s_mcReady) return -1;
    if (s_scanned) return 0;
    s_latestSlot = -1;
    for (int slot = 0; slot < 2; slot++) {
        if (read_file(slot)) continue;
        if (s_latestSlot < 0 || (s32)(s_file.generation - s_generation) > 0) {
            s_latestSlot = slot;
            s_generation = s_file.generation;
            s_latestSize = s_file.size;
            memcpy(s_latestData, s_file.data, OOT_SRAM_SIZE);
        }
    }
    if (s_latestSlot < 0) { s_generation = 0; s_latestSize = 0; }
    s_scanned = true;
    return 0;
}
int ps2_save_write(const void* data, u32 size) {
    if (!data || !size || size > OOT_SRAM_SIZE || scan_card()) return -1;
    int slot = s_latestSlot == 0 ? 1 : 0;
    u32 generation = s_generation + 1;
    memset(&s_file, 0, sizeof(s_file));
    s_file.magic = SAVE_MAGIC;
    s_file.version = SAVE_VERSION;
    s_file.size = size;
    s_file.generation = generation;
    memcpy(s_file.data, data, size);
    s_file.checksum = save_checksum(s_file.data, size) ^ generation;
    int fd = wait_result(mcOpen(SAVE_PORT, SAVE_SLOT, s_paths[slot], FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC));
    if (fd < 0) { printf("[Save] Open error %d\n", fd); goto failed; }
    int bytes = wait_result(mcWrite(fd, &s_file, sizeof(s_file)));
    int closed = wait_result(mcClose(fd));
    if (bytes != sizeof(s_file) || closed < 0) {
        printf("[Save] Write bytes=%d expected=%u close=%d\n", bytes, (unsigned)sizeof(s_file), closed);
        goto failed;
    }
    if (read_file(slot) || s_file.generation != generation || s_file.size != size || memcmp(s_file.data, data, size)) {
        printf("[Save] Readback validation failed\n");
        goto failed;
    }
    s_generation = generation;
    s_latestSlot = slot;
    s_latestSize = size;
    memcpy(s_latestData, s_file.data, OOT_SRAM_SIZE);
    printf("[Save] Guardados y verificados %u bytes (copia %d, revision %u)\n",
           (unsigned)size, slot, (unsigned)generation);
    return 0;
failed:
    s_mcReady = s_scanned = false;
    printf("[Save] Escritura fallida; se conserva la copia anterior\n");
    return -1;
}
int ps2_save_read(void* data, u32 size) {
    if (!data || size > OOT_SRAM_SIZE || scan_card() || s_latestSlot < 0) return -1;
    memset(data, 0, size);
    memcpy(data, s_latestData, size < s_latestSize ? size : s_latestSize);
    printf("[Save] Cargados %u bytes (copia %d, revision %u)\n",
           (unsigned)size, s_latestSlot, (unsigned)s_generation);
    return 0;
}
int ps2_sram_flush_to_card(const void* sram, u32 size) { return ps2_save_write(sram, size); }
int ps2_sram_load_from_card(void* sram, u32 size) { return ps2_save_read(sram, size); }
