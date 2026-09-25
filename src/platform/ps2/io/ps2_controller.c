/**
 * ps2_controller.c — Guardado de partida en Memory Card
 *
 * ══════════════════════════════════════════════════════════════════════
 *  QUÉ ESTABA MAL
 * ══════════════════════════════════════════════════════════════════════
 *
 * 1. NADIE LLAMABA A NADA DE ESTE ARCHIVO.
 *    Exponía ps2_controller_set_rumble(), ps2_apply_deadzone(),
 *    ps2_controller_init_actuators(), ps2_save_init/read/write()... y
 *    ninguna de ellas aparecía en el resto del proyecto. Código escrito
 *    pero desconectado, que en la tabla del README figuraba como
 *    "Saves (MemCard) ✅ Estructura".
 *
 *    Ahora:
 *      · la vibración se conecta por osMotorStart/Stop (osMisc.c), que
 *        es lo que el juego llama de verdad;
 *      · la zona muerta se aplica dentro de osContGetReadData;
 *      · el guardado se engancha a las funciones de SRAM del juego.
 *
 * 2. USABA open/read/write/close SIN INCLUIR <fcntl.h> ni <unistd.h>,
 *    y en PS2 esas funciones sobre "mc0:" ni siquiera son las de la
 *    libc estándar: hay que usar la API de libmc (mcOpen, mcRead...)
 *    con su patrón de mcSync() para esperar el resultado. Tal como
 *    estaba, no habría compilado.
 *
 * 3. mcInit() se llamaba aquí, pero MCMAN/MCSERV no se cargaban en
 *    ningún sitio, así que habría fallado igualmente.
 *    Ahora mcInit() vive en ps2_main.c, después de cargar los módulos.
 *
 * 4. NO HABÍA VERIFICACIÓN DE INTEGRIDAD más allá de un número mágico.
 *    Se añade checksum, porque una memory card a medio escribir es un
 *    clásico y OoT guarda mucho.
 */

#include <kernel.h>
#include <libmc.h>
#include <string.h>
#include <stdio.h>

#include "ps2_types.h"

/* ───────────────────────────────────────────────────────────────── *
 *  Configuración del fichero de guardado
 * ───────────────────────────────────────────────────────────────── */
#define SAVE_PORT      0
#define SAVE_SLOT      0
#define SAVE_DIR       "OOT_PS2"
#define SAVE_PATH      "/OOT_PS2/save.dat"
#define SAVE_MAGIC     0x4F4F5432u    /* 'OOT2' */
#define SAVE_VERSION   1

/* Tamaño de la SRAM de OoT */
#define OOT_SRAM_SIZE  0x8000

typedef struct {
    u32 magic;
    u32 version;
    u32 size;
    u32 checksum;
    u8  data[OOT_SRAM_SIZE];
} PS2SaveFile;

static PS2SaveFile s_saveFile __attribute__((aligned(64)));
static bool s_mcReady = false;

/* ───────────────────────────────────────────────────────────────── *
 *  Checksum sencillo pero suficiente para detectar corrupción
 * ───────────────────────────────────────────────────────────────── */
static u32 save_checksum(const u8* data, u32 size) {
    u32 sum = 0x12345678u;
    for (u32 i = 0; i < size; i++) {
        sum = (sum << 3) ^ (sum >> 29) ^ data[i];
    }
    return sum;
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_save_init()
 *
 *  Se llama desde ps2_boot_game(). ANTES no se llamaba nunca.
 *  mcInit() ya se hizo en ps2_main.c tras cargar MCMAN/MCSERV.
 * ───────────────────────────────────────────────────────────────── */
void ps2_save_init(void) {
    int cmd, result;

    /* ¿Hay tarjeta? */
    mcGetInfo(SAVE_PORT, SAVE_SLOT, &cmd, &result, &result);
    mcSync(MC_WAIT, NULL, &result);

    if (result < -1) {
        printf("[Save] No hay memory card en el puerto %d\n", SAVE_PORT);
        s_mcReady = false;
        return;
    }

    /* Crear el directorio si no existe (-4 = ya existe, no es error) */
    mcMkDir(SAVE_PORT, SAVE_SLOT, SAVE_DIR);
    mcSync(MC_WAIT, NULL, &result);

    s_mcReady = true;
    printf("[Save] Memory card lista (%s)\n", SAVE_DIR);
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_save_write()
 * ───────────────────────────────────────────────────────────────── */
int ps2_save_write(const void* saveData, u32 size) {
    int fd, result;

    if (!s_mcReady || !saveData) return -1;
    if (size > OOT_SRAM_SIZE) size = OOT_SRAM_SIZE;

    memset(&s_saveFile, 0, sizeof(s_saveFile));
    s_saveFile.magic    = SAVE_MAGIC;
    s_saveFile.version  = SAVE_VERSION;
    s_saveFile.size     = size;
    memcpy(s_saveFile.data, saveData, size);
    s_saveFile.checksum = save_checksum(s_saveFile.data, size);

    mcOpen(SAVE_PORT, SAVE_SLOT, SAVE_PATH, MC_O_WRONLY | MC_O_CREAT);
    mcSync(MC_WAIT, NULL, &fd);
    if (fd < 0) {
        printf("[Save] No se pudo abrir %s para escribir (%d)\n",
               SAVE_PATH, fd);
        return -1;
    }

    mcWrite(fd, &s_saveFile, (int)sizeof(s_saveFile));
    mcSync(MC_WAIT, NULL, &result);

    mcClose(fd);
    mcSync(MC_WAIT, NULL, NULL);

    if (result != (int)sizeof(s_saveFile)) {
        printf("[Save] Escritura incompleta (%d de %d bytes)\n",
               result, (int)sizeof(s_saveFile));
        return -1;
    }

    printf("[Save] Guardados %u bytes\n", (unsigned)size);
    return 0;
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_save_read()
 * ───────────────────────────────────────────────────────────────── */
int ps2_save_read(void* saveData, u32 size) {
    int fd, result;

    if (!s_mcReady || !saveData) return -1;

    mcOpen(SAVE_PORT, SAVE_SLOT, SAVE_PATH, MC_O_RDONLY);
    mcSync(MC_WAIT, NULL, &fd);
    if (fd < 0) {
        printf("[Save] No hay partida guardada todavia\n");
        return -1;
    }

    mcRead(fd, &s_saveFile, (int)sizeof(s_saveFile));
    mcSync(MC_WAIT, NULL, &result);

    mcClose(fd);
    mcSync(MC_WAIT, NULL, NULL);

    if (result != (int)sizeof(s_saveFile)) {
        printf("[Save] Lectura incompleta\n");
        return -1;
    }

    if (s_saveFile.magic != SAVE_MAGIC) {
        printf("[Save] Fichero no reconocido (magic 0x%08X)\n",
               (unsigned)s_saveFile.magic);
        return -1;
    }

    u32 expected = save_checksum(s_saveFile.data, s_saveFile.size);
    if (expected != s_saveFile.checksum) {
        printf("[Save] CHECKSUM INCORRECTO: la partida esta corrupta.\n");
        return -1;
    }

    if (size > s_saveFile.size) size = s_saveFile.size;
    memcpy(saveData, s_saveFile.data, size);

    printf("[Save] Cargados %u bytes\n", (unsigned)size);
    return 0;
}

/* ───────────────────────────────────────────────────────────────── *
 *  Puente con el sistema de SRAM del juego
 *
 *  OoT lee y escribe la SRAM con osEPiStartDma sobre un OSPiHandle
 *  aparte (z_ss_sram.c). Estas dos funciones son el enganche para que
 *  ese camino acabe en la memory card.
 *
 *  PENDIENTE: llamar a estas desde SsSram_ReadWrite(). Está señalado
 *  aquí en lugar de tocarlo a ciegas, porque conviene decidir si se
 *  guarda en cada escritura (lento, la memory card es lenta) o sólo al
 *  salir del menú de guardado (mejor, pero hay que detectar ese
 *  momento).
 * ───────────────────────────────────────────────────────────────── */
int ps2_sram_flush_to_card(const void* sram, u32 size) {
    return ps2_save_write(sram, size);
}

int ps2_sram_load_from_card(void* sram, u32 size) {
    return ps2_save_read(sram, size);
}
