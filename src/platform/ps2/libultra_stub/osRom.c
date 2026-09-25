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
#include <fileio.h>
#include <string.h>
#include <stdio.h>

#include "ps2_os_internal.h"

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

static OSPiHandle s_cartHandle;

static const char* const kRomPaths[] = {
    "host:oot.z64",
    "host:oot.n64",
    "cdrom0:\\OOT.Z64;1",
    "mass:oot.z64",
    NULL
};

/* ───────────────────────────────────────────────────────────────── *
 *  Apertura de la ROM
 * ───────────────────────────────────────────────────────────────── */
static bool rom_open(void) {
    if (s_romOpen) return true;

    for (int i = 0; kRomPaths[i] != NULL; i++) {
        int fd = fioOpen(kRomPaths[i], O_RDONLY);
        if (fd >= 0) {
            s_romFd   = fd;
            s_romOpen = true;
            int size = fioLseek(fd, 0, SEEK_END);
            fioLseek(fd, 0, SEEK_SET);
            printf("[osRom] ROM abierta: %s (%d bytes)\n",
                   kRomPaths[i], size);
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
static s32 rom_read(u32 devAddr, void* dram, u32 size) {
    if (!s_romOpen && !rom_open()) return -1;
    if (!dram || size == 0) return -1;

    u32 offset = devAddr;
    if (offset >= 0x10000000u) offset -= 0x10000000u;

    if (fioLseek(s_romFd, (int)offset, SEEK_SET) < 0) return -1;

    int got = fioRead(s_romFd, dram, (int)size);
    if (got < 0) return -1;

    /* Si la lectura se quedó corta, rellenamos con ceros en vez de
     * dejar basura: es mucho más fácil de diagnosticar. */
    if ((u32)got < size) {
        memset((u8*)dram + got, 0, size - (u32)got);
    }

    /* La GIF y el resto de DMAs leen de RAM física: hay que volcar la
     * caché o verán los datos viejos. */
    FlushCache(0);
    return 0;
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
    *data = value;
    return 0;
}

s32 osEPiWriteIo(OSPiHandle* handle, u32 devAddr, u32 data) {
    (void)handle; (void)devAddr; (void)data;
    return -1;   /* la ROM es de sólo lectura */
}

/* ───────────────────────────────────────────────────────────────── *
 *  osCreatePiManager — en N64 lanza el hilo que serializa los DMA.
 *  Aquí no hace falta: osEPiStartDma ya es síncrono y los semáforos de
 *  las colas se encargan de la exclusión mutua.
 * ───────────────────────────────────────────────────────────────── */
void osCreatePiManager(OSPri pri, struct OSMesgQueue* cmdQ,
                       OSMesg* cmdBuf, s32 cmdMsgCnt) {
    (void)pri; (void)cmdQ; (void)cmdBuf; (void)cmdMsgCnt;
}

void ps2_rom_shutdown(void) {
    if (s_romOpen) {
        fioClose(s_romFd);
        s_romFd   = -1;
        s_romOpen = false;
    }
}

/* MEJORA PENDIENTE (no es un bug, es rendimiento):
 * añadir una caché LRU de bloques de 64 KB delante de rom_read().
 * Las escenas se releen mucho y el CD es ~10x más lento que el
 * cartucho original. */
