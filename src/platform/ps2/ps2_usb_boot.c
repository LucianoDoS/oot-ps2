/* USB launch support is linked only into the hardware/test variants. */
#include <kernel.h>
#include <loadfile.h>
#include <debug.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "ps2_usb_boot.h"
#include "usb_data_expected.h"

static char sDataPath[256];
static char sError[128];
static unsigned char sBlock[4096] __attribute__((aligned(64)));

static unsigned int crc32(const unsigned char* data, unsigned int size) {
    unsigned int crc = ~0u;
    for (unsigned int i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static int fail(const char* message) {
    if (message != sError) snprintf(sError, sizeof(sError), "%s", message);
    printf("[PS2 USB BOOT] FAIL %s\n", sError);
    return 0;
}

#ifndef PS2_USB_HOST_TEST
static int load_driver(unsigned char* data, unsigned int bytes, const char* name) {
    int result = -1;
    int module = SifExecModuleBuffer(data, bytes, 0, NULL, &result);
    printf("[PS2 USB MODULE] %s id=%d rv=%d\n", name, module, result);
    if (module < 0 || result != 0) {
        snprintf(sError, sizeof(sError), "No se pudo cargar %s (%d / %d)", name, module, result);
        return 0;
    }
    return 1;
}
#endif

const char* ps2_usb_data_path(void) { return sDataPath; }

int ps2_usb_prepare(const char* launch_path) {
#ifdef PS2_USB_HOST_TEST
    (void)launch_path;
    snprintf(sDataPath, sizeof(sDataPath), "host:OOT.Z64");
    printf("[PS2 USB BOOT] HOST TEST: physical USB is not exercised\n");
#else
    extern unsigned char usbd_irx[], usbhdfsd_irx[];
    extern unsigned int size_usbd_irx, size_usbhdfsd_irx;
    if (!load_driver(usbd_irx, size_usbd_irx, "USBD") ||
        !load_driver(usbhdfsd_irx, size_usbhdfsd_irx, "USBHDFSD")) return fail(sError);
    snprintf(sDataPath, sizeof(sDataPath), "mass:/OOTPS2/OOT.Z64");
    /* wLaunchELF supplies argv[0]. Accept mass:, mass0:, etc.; other
     * launch devices use the documented default USB directory. */
    if (launch_path && strncmp(launch_path, "mass", 4) == 0) {
        const char* colon = strchr(launch_path + 4, ':');
        const char* slash = strrchr(launch_path, '/');
        int valid = colon != NULL;
        for (const char* p = launch_path + 4; valid && p < colon; p++)
            if (*p < '0' || *p > '9') valid = 0;
        if (valid && slash && slash > colon) {
            unsigned int length = (unsigned int)(slash - launch_path + 1);
            if (length + sizeof("OOT.Z64") > sizeof(sDataPath)) return fail("Ruta USB demasiado larga");
            memcpy(sDataPath, launch_path, length);
            memcpy(sDataPath + length, "OOT.Z64", sizeof("OOT.Z64"));
        }
    }
#endif
    int fd = -1;
    for (int attempt = 0; attempt < 100; attempt++) {
        fd = open(sDataPath, O_RDONLY);
        if (fd >= 0) break;
        usleep(100000);
    }
    if (fd < 0) return fail("No encuentro OOT.Z64 junto al ELF en el USB");
    int size = lseek(fd, 0, SEEK_END);
    if (size < 0 || (unsigned int)size != PS2_USB_DATA_SIZE) {
        close(fd);
        return fail("Tamano incorrecto de OOT.Z64: copia los datos convertidos");
    }
    for (unsigned int i = 0; i < sizeof(sUsbSamples) / sizeof(sUsbSamples[0]); i++) {
        if (lseek(fd, (int)sUsbSamples[i].offset, SEEK_SET) < 0) {
            close(fd); return fail("No puedo buscar un bloque de OOT.Z64");
        }
        unsigned int read_bytes = 0;
        while (read_bytes < sUsbSamples[i].bytes) {
            int got = read(fd, sBlock + read_bytes, sUsbSamples[i].bytes - read_bytes);
            if (got <= 0) { close(fd); return fail("Lectura incompleta del USB"); }
            read_bytes += (unsigned int)got;
        }
        if (crc32(sBlock, read_bytes) != sUsbSamples[i].crc32) {
            close(fd); return fail("Datos incorrectos o corruptos: vuelve a copiar OOT.Z64");
        }
    }
    close(fd);
    printf("[PS2 USB BOOT] PASS path=%s bytes=%u samples=%u\n", sDataPath,
           PS2_USB_DATA_SIZE, (unsigned int)(sizeof(sUsbSamples) / sizeof(sUsbSamples[0])));
    return 1;
}

void ps2_usb_fail_screen(void) {
    init_scr();
    scr_printf("OoT PS2 - no se puede iniciar\n\n%s\n\n", sError);
    scr_printf("USB FAT32: mass:/OOTPS2/OOTPS2.ELF\n");
    scr_printf("Datos:     mass:/OOTPS2/OOT.Z64\n\n");
    scr_printf("Ejecuta BOOTTEST.ELF para comprobar la lectura.\n");
    scr_printf("Reinicia la consola para volver a wLaunchELF.\n");
    for (;;) SleepThread();
}
