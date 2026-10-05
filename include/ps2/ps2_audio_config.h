#ifndef PS2_AUDIO_CONFIG_H
#define PS2_AUDIO_CONFIG_H
/* audsrv holds about 106 ms at 48 kHz. Leave room above the reservoir
 * for the game's complete synthesis blocks; never fill the ring blindly. */
#if defined(PS2_USB_BOOT) || defined(PS2_USB_AUDIO_RESERVE)
#define PS2_AUDIO_RESERVE_MS 60u
#else
#define PS2_AUDIO_RESERVE_MS 20u
#endif
#endif
