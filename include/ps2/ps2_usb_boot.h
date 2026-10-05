#ifndef PS2_USB_BOOT_H
#define PS2_USB_BOOT_H

/* Called after IOP reset/RPC initialization. The selected data file stays
 * open only during validation; osRom opens exactly the same path afterwards. */
int ps2_usb_prepare(const char* launch_path);
/* Optional progress display; BOOTTEST remains independent of game tracing. */
void ps2_usb_set_progress(void (*callback)(const char*));
const char* ps2_usb_data_path(void);
void ps2_usb_fail_screen(void) __attribute__((noreturn));

#endif
