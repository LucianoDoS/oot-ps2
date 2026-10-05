#ifndef PS2_ROM_CACHE_H
#define PS2_ROM_CACHE_H
/* Keep bulk loading separate from the sample working set. */
#define PS2_ROM_CACHE_BLOCK 4096u
#if defined(PS2_USB_BOOT) || defined(PS2_ROM_USB_CACHE)
#define PS2_ROM_CACHE_SLOTS 64
#define PS2_ROM_BULK_BLOCK 16384u
#define PS2_ROM_BULK_SLOTS 4
#else
#define PS2_ROM_CACHE_SLOTS 16
#endif
#endif
