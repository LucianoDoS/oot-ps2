/* SPDX-License-Identifier: MIT
 * Optional external native initializers. The post-link catalogue belongs to
 * the trusted executable; private data files never supply write addresses. */
#include <kernel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ps2_release_data.h"
typedef struct { uint32_t address, size, offset, crc; } Record;
typedef struct {
    char magic[16];
    uint32_t version, count, dataBytes, crc;
    Record records[512];
    unsigned char reserved[16384-32-512*16];
} Catalogue;
_Static_assert(sizeof(Catalogue)==16384,"Release catalogue layout");
Catalogue gPS2ReleaseAssets __attribute__((aligned(64))) = {
    .magic={'O','O','T','P','S','2','R','E','L','A','S','S','E','T','0','1'}, .version=1
};
static uint32_t checksum(const unsigned char* bytes, unsigned count) {
    uint32_t crc=~0u;
    for (unsigned i=0;i<count;i++) {
        crc^=bytes[i];
        for (unsigned bit=0;bit<8;bit++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));
    }
    return ~crc;
}
int ps2_release_data_load(const char* imagePath) {
    const Catalogue* catalog=&gPS2ReleaseAssets;
    char path[256];
    unsigned char header[32];
    uint32_t metadata[4];
    FILE* file=NULL;
    unsigned char* payload=NULL;
    int result=0;
    const char* reason="catalogue is not finalized";
    if (memcmp(catalog->magic,"OOTPS2RELASSET01",16) || catalog->version!=1 ||
        !catalog->count || catalog->count>512 || !catalog->dataBytes || catalog->dataBytes>4*1024*1024) goto done;
    unsigned offset=0, previousEnd=0;
    for (unsigned i=0;i<catalog->count;i++) {
        const Record* record=&catalog->records[i];
        if (!record->size || record->offset!=offset || record->size>catalog->dataBytes-offset ||
            record->address<0x100000 || record->address<previousEnd ||
            record->address>0x2000000-record->size ||
            (record->address<(uintptr_t)catalog+sizeof(*catalog) &&
             record->address+record->size>(uintptr_t)catalog)) goto done;
        offset+=record->size; previousEnd=record->address+record->size;
    }
    if (offset!=catalog->dataBytes) goto done;
    reason="invalid data path";
    if (!imagePath || strlen(imagePath)>220) goto done;
    strcpy(path,imagePath);
    char* separator=strrchr(path,'/');
    if (!separator) separator=strrchr(path,':');
    if (!separator) goto done;
    strcpy(separator+1,"PS2DATA.DAT");
    reason="missing PS2DATA.DAT";
    file=fopen(path,"rb");
    if (!file) goto done;
    reason="wrong PS2DATA.DAT build or incomplete file";
    if (fread(header,1,32,file)!=32 || memcmp(header,"OOTPS2DATA01\0\0\0\0\0",16)) goto done;
    memcpy(metadata,header+16,sizeof(metadata));
    if (metadata[0]!=catalog->version || metadata[1]!=catalog->count ||
        metadata[2]!=catalog->dataBytes || metadata[3]!=catalog->crc) goto done;
    reason="not enough memory for PS2DATA.DAT";
    payload=malloc(catalog->dataBytes);
    if (!payload) goto done;
    reason="damaged or incomplete PS2DATA.DAT";
    unsigned readBytes=0;
    while (readBytes<catalog->dataBytes) {
        unsigned chunk=catalog->dataBytes-readBytes;
        if (chunk>16384) chunk=16384;
        unsigned got=fread(payload+readBytes,1,chunk,file);
        if (!got) goto done;
        readBytes+=got;
    }
    if (fgetc(file)!=EOF || ferror(file) || checksum(payload,catalog->dataBytes)!=catalog->crc) goto done;
    for (unsigned i=0;i<catalog->count;i++) {
        const Record* record=&catalog->records[i];
        if (checksum(payload+record->offset,record->size)!=record->crc) goto done;
    }
    /* All validation completes before touching the native initializers. */
    for (unsigned i=0;i<catalog->count;i++) {
        const Record* record=&catalog->records[i];
        void* destination=(void*)(uintptr_t)record->address;
        memcpy(destination,payload+record->offset,record->size);
        SyncDCache(destination,(unsigned char*)destination+record->size);
    }
    printf("[PS2 EXTERNAL ASSETS] PASS records=%u bytes=%u\n",catalog->count,catalog->dataBytes);
    result=1;
done:
    if (file) fclose(file);
    free(payload);
    if (!result) printf("[PS2 EXTERNAL ASSETS] FAIL %s\n",reason);
    return result;
}
