#ifndef PS2_ROM_DMA_H
#define PS2_ROM_DMA_H
/* Include ultra64.h first. Audio and game DMA have the same libultra priority;
 * identify their working sets explicitly, without changing message layout. */
s32 ps2_rom_start_audio_dma(OSPiHandle* handle, OSIoMesg* message, s32 direction);
#endif
