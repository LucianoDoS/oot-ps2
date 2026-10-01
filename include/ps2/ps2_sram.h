#ifndef PS2_SRAM_H
#define PS2_SRAM_H

#define PS2_SRAM_SIZE 0x8000u
#define PS2_SRAM_ADDRESS 0x08000000u
void ps2_sram_init(void);
/* Do not include the platform's graphics types in game translation units. */
int ps2_sram_transfer(unsigned int address, void* data, unsigned int size, int write);

#endif
