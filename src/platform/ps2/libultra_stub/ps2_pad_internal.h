#ifndef PS2_PAD_INTERNAL_H
#define PS2_PAD_INTERNAL_H

#include "ps2_types.h"

/* Shared by controller queries and the libultra motor entry points. */
bool ps2_pad_rumble_ready(int port);
s32 ps2_pad_set_rumble(int port, bool on);
void ps2_pad_shutdown(void);

#endif
