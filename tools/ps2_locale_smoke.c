/* Optional private archive test on the EE. No player save or card access. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ps2_locale.h"

int ps2_locale_smoke(void) {
#ifndef PS2_SMOKE_LOCALE
    return 0;
#else
    int failures=0;
    FILE* file=fopen("host:ESP.DAT","rb");
    if (!file) { printf("[PS2 LANGUAGE TEST] FAIL: private archive missing\n"); return 1; }
    fseek(file,0,SEEK_END);
    long bytes=ftell(file);
    rewind(file);
    u8* archive=malloc(bytes);
    if (!archive || fread(archive,1,bytes,file)!=(unsigned long)bytes) {
        fclose(file); free(archive); return 1;
    }
    fclose(file);
    const u32* header=(const u32*)(archive+8);
    ps2_locale_set_language(PS2_LANGUAGE_SPANISH);
    if (ps2_locale_language()!=PS2_LANGUAGE_ENGLISH) failures++;
    ps2_locale_init("host:oot.z64");
    if (!ps2_locale_available()) { free(archive); printf("[PS2 LANGUAGE TEST] FAIL: load\n"); return 1; }
    ps2_locale_set_language(PS2_LANGUAGE_SPANISH);
    u8 buffer[1288],typePos=0;
    u32 length=0;
    unsigned messages=header[2],textures=header[3];
    for (unsigned i=0;i<messages;i++) {
        const u8* entry=archive+header[4]+i*12;
        u16 id; u32 offset,size;
        memcpy(&id,entry,2); memcpy(&offset,entry+4,4); memcpy(&size,entry+8,4);
        memset(buffer,0xa5,sizeof(buffer));
        if (!ps2_locale_message(id,buffer+4,1280,&typePos,&length) || typePos!=entry[2] ||
            length!=size || memcmp(buffer+4,archive+offset,size) || buffer[3]!=0xa5 || buffer[4+size]!=0xa5) failures++;
        if (id==0x00a4 && !strstr((char*)buffer+4,"Espada Kokiri")) failures++;
        memset(buffer,0xa5,sizeof(buffer));
        if (ps2_locale_message(id,buffer+4,size-1,&typePos,&length) || buffer[4]!=0xa5) failures++;
    }
    if (ps2_locale_message(0xffff,buffer,1280,&typePos,&length)) failures++;
    for (unsigned i=0;i<textures;i++) {
        const u32* entry=(const u32*)(archive+header[5]+i*12);
        u32 vrom=entry[0],offset=entry[1],size=entry[2];
        u8* output=malloc(size+8);
        if (!output) { failures++; break; }
        memset(output,0xa5,size+8);
        if (!ps2_locale_read(vrom,output+4,size) || memcmp(output+4,archive+offset,size) ||
            output[3]!=0xa5 || output[size+4]!=0xa5) failures++;
        unsigned slice=size>17?17:size;
        if (!ps2_locale_read(vrom+size-slice,output,slice) || memcmp(output,archive+offset+size-slice,slice)) failures++;
        /* Larger DMA contains bytes outside a resource; only overlaps change. */
        memset(output,0xa5,size+8);
        if (ps2_locale_read(vrom-2,output+2,size+4)) failures++;
        ps2_locale_overlay(vrom-2,output+2,size+4);
        if (memcmp(output+4,archive+offset,size) || output[1]!=0xa5 || output[size+6]!=0xa5) failures++;
        free(output);
    }
    const float* widths=(const float*)(archive+header[6]);
    for (unsigned c=32;c<176;c++) if (ps2_locale_char_width(c,0)!=widths[c-32]) failures++;
    for (unsigned i=0;i<4;i++) if (!ps2_locale_label(i) ||
        memcmp(ps2_locale_label(i),archive+header[7]+i*1536,1536)) failures++;
    if (ps2_locale_label(4)) failures++;
    ps2_locale_set_language(PS2_LANGUAGE_ENGLISH);
    memset(buffer,0xa5,sizeof(buffer));
    if (ps2_locale_message(0x00a4,buffer,1280,&typePos,&length) ||
        ps2_locale_read(0x8ed000,buffer,128) || ps2_locale_char_width('A',123)!=123) failures++;
    ps2_locale_overlay(0x8ed000,buffer,128);
    if (buffer[0]!=0xa5 || buffer[127]!=0xa5) failures++;
    free(archive);
    printf("[PS2 LANGUAGE TEST] %s: %u messages, %u textures, sliced DMA, guards, widths and English fallback (%d)\n",
        failures?"FAIL":"PASS",messages,textures,failures);
    return failures;
#endif
}
