/* Immutable translation data is loaded once, outside OoT's arena. Switching
 * languages never changes native language indices or a save-slot layout. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ps2_locale.h"

typedef struct {
    char magic[8];
    u32 version, bytes, messages, textures, messageOffset, textureOffset;
    u32 widthOffset, labelOffset, payloadOffset, crc;
} Header;
typedef struct { u16 id; u8 typePos, reserved; u32 offset, size; } Message;
typedef struct { u32 vrom, offset, size; } Texture;
_Static_assert(sizeof(Header)==48 && sizeof(Message)==12 && sizeof(Texture)==12,
    "Translation archive layout");
static Header* sData;
static s32 sLanguage, sChecked;
static int range(u32 offset, u32 bytes, u32 extent) {
    return offset <= extent && bytes <= extent - offset;
}
static u32 crc32(const u8* data, u32 bytes) {
    u32 crc = ~0u;
    for (u32 i=0;i<bytes;i++) {
        crc ^= data[i];
        for (unsigned bit=0;bit<8;bit++) crc=(crc>>1) ^ (0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}
static int valid(const Header* h, u32 bytes) {
    if (bytes<sizeof(Header) || memcmp(h->magic,"OOTESP01",8) || h->version!=2 || h->bytes!=bytes ||
        h->messages>4096 || !h->messages || h->textures>4096 ||
        h->messageOffset!=sizeof(Header) ||
        h->textureOffset!=h->messageOffset+h->messages*sizeof(Message) ||
        h->widthOffset!=h->textureOffset+h->textures*sizeof(Texture) ||
        h->labelOffset!=h->widthOffset+144*sizeof(f32) ||
        h->payloadOffset!=h->labelOffset+4*96*16 || h->payloadOffset>bytes ||
        crc32((const u8*)h+sizeof(Header),bytes-sizeof(Header))!=h->crc) return 0;
    const Message* messages=(const Message*)((const u8*)h+h->messageOffset);
    for (u32 i=0;i<h->messages;i++) {
        const Message* m=&messages[i];
        if ((i && messages[i-1].id>=m->id) || !m->size || m->size>1280 ||
            m->offset<h->payloadOffset || !range(m->offset,m->size,bytes)) return 0;
    }
    const Texture* textures=(const Texture*)((const u8*)h+h->textureOffset);
    for (u32 i=0;i<h->textures;i++) {
        const Texture* t=&textures[i];
        if (!t->size || t->offset<h->payloadOffset || !range(t->offset,t->size,bytes) ||
            !range(t->vrom,t->size,0x04000000) ||
            (i && textures[i-1].vrom+textures[i-1].size>t->vrom)) return 0;
    }
    const f32* widths=(const f32*)((const u8*)h+h->widthOffset);
    for (u32 i=0;i<144;i++) if (!(widths[i]>=0 && widths[i]<=32)) return 0;
    return 1;
}
void ps2_locale_init(const char* romPath) {
    if (sChecked) return;
    sChecked=1;
    char path[256];
    if (!romPath || strlen(romPath)>=sizeof(path)) return;
    strcpy(path,romPath);
    char* name=strrchr(path,'/');
    char* backslash=strrchr(path,'\\');
    if (backslash && (!name || backslash>name)) name=backslash;
    if (!name) name=strrchr(path,':');
    if (!name || (u32)(name-path)+1+sizeof("ESP.DAT")>sizeof(path)) return;
    strcpy(name+1,"ESP.DAT");
    FILE* file=fopen(path,"rb");
    if (!file) { printf("[PS2 LANGUAGE] ESP.DAT unavailable; English retained\n"); return; }
    Header header;
    Header* data=NULL;
    if (fread(&header,1,sizeof(header),file)!=sizeof(header) ||
        header.bytes<sizeof(header) || header.bytes>2*1024*1024) goto done;
    data=malloc(header.bytes);
    if (!data) goto done;
    memcpy(data,&header,sizeof(header));
    /* Large sequential reads prevent a separate USB operation per glyph. */
    for (u32 offset=sizeof(header);offset<header.bytes;) {
        u32 count=header.bytes-offset;
        if (count>65536) count=65536;
        if (fread((u8*)data+offset,1,count,file)!=count) goto done;
        offset+=count;
    }
    if (fgetc(file)!=EOF || !valid(data,header.bytes)) goto done;
    sData=data;
    printf("[PS2 LANGUAGE] Loaded %u messages, %u textures, %u bytes\n",
        (unsigned)data->messages,(unsigned)data->textures,(unsigned)data->bytes);
done:
    fclose(file);
    if (data && data!=sData) { free(data); printf("[PS2 LANGUAGE] Invalid ESP.DAT; English retained\n"); }
}
s32 ps2_locale_available(void) { return sData!=NULL; }
s32 ps2_locale_language(void) { return sLanguage; }
void ps2_locale_set_language(s32 language) {
    sLanguage=sData && language==PS2_LANGUAGE_SPANISH ? PS2_LANGUAGE_SPANISH : PS2_LANGUAGE_ENGLISH;
}
s32 ps2_locale_message(u16 id, void* destination, u32 capacity, u8* typePos, u32* length) {
    if (!sLanguage || !destination || !typePos || !length) return 0;
    const Message* table=(const Message*)((u8*)sData+sData->messageOffset);
    u32 lo=0,hi=sData->messages;
    while (lo<hi) { u32 mid=(lo+hi)/2; if (table[mid].id<id) lo=mid+1; else hi=mid; }
    if (lo==sData->messages || table[lo].id!=id || table[lo].size>capacity) return 0;
    memcpy(destination,(u8*)sData+table[lo].offset,table[lo].size);
    *typePos=table[lo].typePos; *length=table[lo].size;
    return 1;
}
static u32 first_texture(u32 vrom) {
    const Texture* table=(const Texture*)((u8*)sData+sData->textureOffset);
    u32 lo=0,hi=sData->textures;
    while (lo<hi) { u32 mid=(lo+hi)/2; if (table[mid].vrom+table[mid].size<=vrom) lo=mid+1; else hi=mid; }
    return lo;
}
s32 ps2_locale_read(u32 vrom, void* destination, u32 size) {
    if (!sLanguage || !destination || !size || !range(vrom,size,0x04000000)) return 0;
    const Texture* table=(const Texture*)((u8*)sData+sData->textureOffset);
    u32 i=first_texture(vrom);
    if (i==sData->textures || vrom<table[i].vrom || vrom+size>table[i].vrom+table[i].size) return 0;
    memcpy(destination,(u8*)sData+table[i].offset+vrom-table[i].vrom,size);
    return 1;
}
void ps2_locale_overlay(u32 vrom, void* destination, u32 size) {
    if (!sLanguage || !destination || !size || !range(vrom,size,0x04000000)) return;
    const Texture* table=(const Texture*)((u8*)sData+sData->textureOffset);
    for (u32 i=first_texture(vrom);i<sData->textures && table[i].vrom<vrom+size;i++) {
        u32 start=table[i].vrom>vrom?table[i].vrom:vrom;
        u32 end=table[i].vrom+table[i].size;
        if (end>vrom+size) end=vrom+size;
        memcpy((u8*)destination+start-vrom,(u8*)sData+table[i].offset+start-table[i].vrom,end-start);
    }
}
f32 ps2_locale_char_width(u8 character, f32 original) {
    if (!sLanguage || character<32 || character>=176) return original;
    return ((f32*)((u8*)sData+sData->widthOffset))[character-32];
}
const u8* ps2_locale_label(unsigned index) {
    return sData && index<4 ? (u8*)sData+sData->labelOffset+index*96*16 : NULL;
}
