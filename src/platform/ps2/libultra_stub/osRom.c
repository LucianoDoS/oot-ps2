/**
 * osRom.c — Acceso a la "ROM" por DMA (NUEVO)
 *
 * ── POR QUÉ HACÍA FALTA ───────────────────────────────────────────────
 *
 * OoT no tiene sus datos en RAM: los lee del cartucho bajo demanda con
 * DMA. Cada escena, cada objeto, cada textura y cada overlay pasa por
 * aquí (src/boot/z_std_dma.c). Sin esto el juego no carga absolutamente
 * nada, así que era un agujero tan grande como el del renderer.
 *
 * ── CÓMO FUNCIONA ─────────────────────────────────────────────────────
 *
 * En PS2 la "ROM" es un fichero. Se busca en varios sitios por orden:
 *   1. host:oot.z64        (PCSX2 con host filesystem — lo más cómodo
 *                           para desarrollar: no hay que reconstruir
 *                           la ISO en cada prueba)
 *   2. cdrom0:\OOT.Z64;1   (disco/ISO de verdad)
 *   3. mass:oot.z64        (USB)
 *
 * La transferencia es SÍNCRONA: leemos y acto seguido mandamos el
 * mensaje de "terminado" a la cola que indica el OSIoMesg. El juego
 * hace osRecvMesg justo después, así que funciona igual que el DMA
 * asíncrono del hardware, sólo que sin solapamiento.
 *
 * ── LIMITACIÓN CONOCIDA ───────────────────────────────────────────────
 *
 * Leer del CD en cada DMA es LENTO comparado con el cartucho. Si notas
 * tirones al cambiar de sala, la mejora natural es una caché de
 * bloques en RAM (el EE tiene 32 MB y la ROM son 32 MB, así que con
 * cachear las zonas calientes basta). Se deja indicado abajo.
 */

#include <kernel.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "ps2_os_internal.h"
#ifdef PS2_USB_BOOT
#include "ps2_usb_boot.h"
#include "ps2_boot_trace.h"
#endif

typedef void* OSMesg;
struct OSMesgQueue;

extern s32 osSendMesg(struct OSMesgQueue* mq, OSMesg msg, s32 flags);
#define OS_MESG_NOBLOCK  0
#define OS_MESG_BLOCK    1

#define OS_READ    0
#define OS_WRITE   1

/* ───────────────────────────────────────────────────────────────── *
 *  Structs de libultra — layout EXACTO (ultra64/pi.h)
 * ───────────────────────────────────────────────────────────────── */
typedef struct OSIoMesgHdr {
    /* 0x00 */ u16 type;
    /* 0x02 */ u8  pri;
    /* 0x03 */ u8  status;
    /* 0x04 */ struct OSMesgQueue* retQueue;
} OSIoMesgHdr;   /* size = 0x08 */

typedef struct OSIoMesg {
    /* 0x00 */ OSIoMesgHdr hdr;
    /* 0x08 */ void*       dramAddr;
    /* 0x0C */ u32         devAddr;
    /* 0x10 */ u32         size;
    /* 0x14 */ void*       piHandle;
} OSIoMesg;      /* size = 0x18 */

/* OSPiHandle mide 0x74. Sólo tocamos la cabecera; el resto es relleno
 * para conservar el tamaño exacto que espera el juego. */
typedef struct OSPiHandle {
    /* 0x00 */ struct OSPiHandle* next;
    /* 0x04 */ u8  type;
    /* 0x05 */ u8  latency;
    /* 0x06 */ u8  pageSize;
    /* 0x07 */ u8  relDuration;
    /* 0x08 */ u8  pulse;
    /* 0x09 */ u8  domain;
    /* 0x0A */ u8  pad0[2];
    /* 0x0C */ u32 baseAddress;
    /* 0x10 */ u32 speed;
    /* 0x14 */ u8  transferInfo[0x60];
} OSPiHandle;    /* size = 0x74 */

_Static_assert(sizeof(OSPiHandle) == 0x74, "OSPiHandle debe medir 0x74");
_Static_assert(sizeof(OSIoMesg)   == 0x18, "OSIoMesg debe medir 0x18");

/* ───────────────────────────────────────────────────────────────── *
 *  Estado de la ROM
 * ───────────────────────────────────────────────────────────────── */
static int  s_romFd   = -1;
static bool s_romOpen = false;
static u32 s_romSize;
static int s_romMutex = -1;
/* Small sample DMAs repeatedly read overlapping parts of immutable ROM.
 * Keep 64 KiB of sectors; large scene loads bypass it to retain hot audio. */
#define ROM_CACHE_BLOCK 4096u
#define ROM_CACHE_SLOTS 16
static struct {
    u32 offset, bytes, used;
    u8 data[ROM_CACHE_BLOCK];
} s_romCache[ROM_CACHE_SLOTS] __attribute__((aligned(64)));
static u32 s_romClock, s_romHits, s_romReads;

void ps2_rom_cache_stats(u32* hits, u32* reads) {
    if (hits) *hits = s_romHits;
    if (reads) *reads = s_romReads;
}

static OSPiHandle s_cartHandle;
OSPiHandle* __osPiTable = &s_cartHandle;

static const char* kRomPaths[] = {
#ifdef PS2_USB_BOOT
    NULL, /* Filled with the path already validated at boot. */
#elif defined(PS2_ROM_TEST_PATH)
    PS2_ROM_TEST_PATH,
#else
    "host:oot.z64",
    "cdrom0:\\OOT.Z64;1",
    "mass:oot.z64",
#endif
    NULL
};

static int rom_lock(void) {
    int previous = ps2_os_lock();
    if (s_romMutex < 0) {
        ee_sema_t semaphore;
        memset(&semaphore, 0, sizeof(semaphore));
        semaphore.init_count = semaphore.max_count = 1;
        s_romMutex = CreateSema(&semaphore);
    }
    ps2_os_unlock(previous);
    return s_romMutex < 0 ? -1 : WaitSema(s_romMutex);
}

/* ───────────────────────────────────────────────────────────────── *
 *  Apertura de la ROM
 * ───────────────────────────────────────────────────────────────── */
static bool rom_open(void) {
    if (s_romOpen) return true;
#ifdef PS2_USB_BOOT
    kRomPaths[0] = ps2_usb_data_path();
    ps2_boot_trace_stage("DATOS: abriendo archivo para el juego");
#endif

    for (int i = 0; kRomPaths[i] != NULL; i++) {
        int fd = open(kRomPaths[i], O_RDONLY);
        if (fd >= 0) {
            int size = lseek(fd, 0, SEEK_END);
            if (size <= 0 || lseek(fd, 0, SEEK_SET) < 0) {
                close(fd);
                continue;
            }
            s_romFd   = fd;
            s_romOpen = true;
            s_romSize = (u32)size;
            printf("[osRom] ROM abierta: %s (%d bytes)\n",
                   kRomPaths[i], size);
#ifdef PS2_USB_BOOT
            ps2_boot_trace_stage("DATOS: archivo abierto para el juego");
#endif
            return true;
        }
    }

    printf("[osRom] ERROR: no encuentro la ROM.\n");
    printf("[osRom]        Coloca oot.z64 junto al ELF (host:) o en el disco.\n");
    return false;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osCartRomInit — devuelve el handle del "cartucho"
 * ───────────────────────────────────────────────────────────────── */
OSPiHandle* osCartRomInit(void) {
    if (rom_lock() < 0) return NULL;
    memset(&s_cartHandle, 0, sizeof(s_cartHandle));
    s_cartHandle.type        = 0;      /* DEVICE_TYPE_CART */
    s_cartHandle.baseAddress = 0x10000000;  /* base de cartucho de N64 */
    s_cartHandle.latency     = 5;
    s_cartHandle.pageSize    = 13;
    s_cartHandle.relDuration = 3;
    s_cartHandle.pulse       = 0x0C;
    s_cartHandle.domain      = 0;
    s_cartHandle.speed       = 0;

    rom_open();
    SignalSema(s_romMutex);
    return &s_cartHandle;
}

OSPiHandle* osDriveRomInit(void) {
    /* El 64DD no existe en este port */
    return NULL;
}

/* ───────────────────────────────────────────────────────────────── *
 *  Lectura efectiva
 *
 *  devAddr llega como offset dentro del espacio del cartucho. El juego
 *  a veces lo pasa ya relativo y a veces con la base 0x10000000, así
 *  que normalizamos.
 * ───────────────────────────────────────────────────────────────── */
static s32 rom_file_read(u32 offset, void* dram, u32 size) {
    if (lseek(s_romFd, (int)offset, SEEK_SET) < 0) return -1;
    s_romReads++;
    for (u32 completed = 0; completed < size;) {
        int got = read(s_romFd, (u8*)dram + completed, (int)(size - completed));
        if (got <= 0) return -1;
        completed += (u32)got;
    }
    return 0;
}

static s32 rom_read(u32 devAddr, void* dram, u32 size) {
    if (!dram || size == 0) return -1;
    if (rom_lock() < 0) return -1;
    s32 result = -1;
    if (!s_romOpen && !rom_open()) goto done;

    u32 offset = devAddr & 0x1FFFFFFFu;
    if (offset >= 0x10000000u) offset -= 0x10000000u;
    if (offset > s_romSize || size > s_romSize - offset) goto done;

    if (size >= ROM_CACHE_BLOCK) {
        if (rom_file_read(offset, dram, size) < 0) goto done;
    } else {
        for (u32 completed = 0; completed < size;) {
            u32 at = offset + completed, block = at & ~(ROM_CACHE_BLOCK - 1);
            int slot = -1, oldest = 0;
            for (int i = 0; i < ROM_CACHE_SLOTS; i++) {
                if (s_romCache[i].bytes && s_romCache[i].offset == block) { slot = i; break; }
                if (s_romCache[i].used < s_romCache[oldest].used) oldest = i;
            }
            if (slot >= 0) {
                s_romHits++;
            } else {
                slot = oldest;
                s_romCache[slot].bytes = 0;
                u32 bytes = s_romSize - block;
                if (bytes > ROM_CACHE_BLOCK) bytes = ROM_CACHE_BLOCK;
                if (rom_file_read(block, s_romCache[slot].data, bytes) < 0) goto done;
                s_romCache[slot].offset = block;
                s_romCache[slot].bytes = bytes;
            }
            s_romCache[slot].used = ++s_romClock;
            u32 within = at - block, count = s_romCache[slot].bytes - within;
            if (count > size - completed) count = size - completed;
            memcpy((u8*)dram + completed, s_romCache[slot].data + within, count);
            completed += count;
        }
    }

    /* La GIF y el resto de DMAs leen de RAM física: hay que volcar la
     * caché o verán los datos viejos. */
    SyncDCache(dram, (u8*)dram + size);
    result = 0;
done:
    SignalSema(s_romMutex);
    return result;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osEPiStartDma — el camino que usa TODO el juego para cargar datos
 * ───────────────────────────────────────────────────────────────── */
s32 osEPiStartDma(OSPiHandle* handle, OSIoMesg* mb, s32 direction) {
    (void)handle;
    if (!mb) return -1;

    s32 ret = 0;

    if (direction == OS_READ) {
        ret = rom_read(mb->devAddr, mb->dramAddr, mb->size);
    } else {
        /* Escribir al cartucho no tiene sentido; el guardado va por
         * memory card (ver io/ps2_controller.c). */
        ret = -1;
    }

    /* Avisar de que la transferencia terminó. El juego se bloquea
     * esperando justo este mensaje. */
    if (mb->hdr.retQueue) {
        osSendMesg(mb->hdr.retQueue, (OSMesg)mb, OS_MESG_NOBLOCK);
    }

    return ret;
}

s32 osPiStartDma(OSIoMesg* mb, s32 priority, s32 direction,
                 u32 devAddr, void* dramAddr, u32 size,
                 struct OSMesgQueue* mq) {
    (void)priority;
    if (!mb) return -1;
    mb->hdr.retQueue = mq;
    mb->devAddr      = devAddr;
    mb->dramAddr     = dramAddr;
    mb->size         = size;
    return osEPiStartDma(NULL, mb, direction);
}

/* ───────────────────────────────────────────────────────────────── *
 *  E/S registro a registro (poco usada, pero el juego la referencia)
 * ───────────────────────────────────────────────────────────────── */
s32 osEPiReadIo(OSPiHandle* handle, u32 devAddr, u32* data) {
    (void)handle;
    if (!data) return -1;
    u32 value = 0;
    if (rom_read(devAddr, &value, sizeof(value)) != 0) return -1;
    // Register reads return an integer; DMA payloads retain their byte stream.
    *data = __builtin_bswap32(value);
    return 0;
}

s32 osEPiWriteIo(OSPiHandle* handle, u32 devAddr, u32 data) {
    (void)handle; (void)devAddr; (void)data;
    return -1;   /* la ROM es de sólo lectura */
}

/* ───────────────────────────────────────────────────────────────── *
 *  osCreatePiManager — en N64 lanza el hilo que serializa los DMA.
 *  En PS2 las transferencias son síncronas. s_romMutex serializa apertura,
 *  seek y lectura entre el hilo DMA y el hilo de audio.
 * ───────────────────────────────────────────────────────────────── */
void osCreatePiManager(OSPri pri, struct OSMesgQueue* cmdQ,
                       OSMesg* cmdBuf, s32 cmdMsgCnt) {
    (void)pri; (void)cmdQ; (void)cmdBuf; (void)cmdMsgCnt;
}

void ps2_rom_shutdown(void) {
    if (rom_lock() < 0) return;
    if (s_romOpen) {
        close(s_romFd);
        s_romFd   = -1;
        s_romOpen = false;
        s_romSize = 0;
        memset(s_romCache, 0, sizeof(s_romCache));
        s_romClock = s_romHits = s_romReads = 0;
    }
    SignalSema(s_romMutex);
}
