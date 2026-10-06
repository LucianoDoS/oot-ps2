"""Validate private locale data before packaging; contains no translated assets."""
import math
import struct
import zlib


def inspect_locale(data):
    if not 48 <= len(data) <= 2*1024*1024:
        raise ValueError('Language archive size outside runtime limits')
    magic,version,size,nm,nt,mo,to,wo,lo,po,crc=struct.unpack_from('<8s10I',data)
    if (magic!=b'OOTESP01' or version!=2 or size!=len(data) or not 0<nm<=4096 or nt>4096
        or mo!=48 or to!=mo+12*nm or wo!=to+12*nt or lo!=wo+576 or po!=lo+6144
        or po>size or crc!=zlib.crc32(data[48:])):
        raise ValueError('Invalid language header, layout or checksum')
    previous=-1
    for i in range(nm):
        text_id,_,reserved,offset,length=struct.unpack_from('<HBBII',data,mo+i*12)
        if text_id<=previous or reserved or not 0<length<=1280 or not po<=offset<=size-length:
            raise ValueError('Invalid language message')
        previous=text_id
    previous=0
    for i in range(nt):
        vrom,offset,length=struct.unpack_from('<III',data,to+i*12)
        if not length or vrom<previous or vrom+length>0x4000000 or not po<=offset<=size-length:
            raise ValueError('Invalid language texture')
        previous=vrom+length
    if any(not math.isfinite(v) or not 0<=v<=32 for v in struct.unpack_from('<144f',data,wo)):
        raise ValueError('Invalid font widths')
    return dict(messages=nm,textures=nt,bytes=size,version=version)
