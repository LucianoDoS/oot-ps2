#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Separate native game initializers from a locally linked reference ELF.

This prepares PRIVATE test output. Passing this structural audit is not a
license clearance or permission to distribute a combined executable.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from ps2_release_assets import Elf, inventory
from audit_ps2_release_assets import audit
from ps2_release_relocations import descriptor

def prepare(reference, generated, output, object_root=None):
    project=Path(__file__).resolve().parents[1]
    original=reference.read_bytes()
    executable=bytearray(original)
    elf=Elf(executable)
    catalogue=elf.find('gPS2ReleaseAssets')
    if catalogue.size!=16384 or original[catalogue.file_offset:catalogue.file_offset+16]!=b'OOTPS2RELASSET01':
        raise ValueError('Reference ELF was not built with PS2_EXTERNAL_ASSETS=1')
    selected, absent=inventory(project,elf,generated,include_private_icon=False)
    if not selected or len(selected)>512:
        raise ValueError('External native asset count outside catalogue capacity')
    public_metadata=descriptor(project,reference,generated,object_root)
    # The icon must match the procedural generator byte-for-byte.
    icon=elf.find('ps2_save_icon_model')
    expected=(generated/'public/triforce.icn').read_bytes()
    if original[icon.file_offset:icon.file_offset+icon.size]!=expected:
        raise ValueError('Reference contains an unverified memory-card mesh')
    payload=bytearray()
    records=[]
    manifest=[]
    for symbol, info in selected:
        start,end=symbol.address,symbol.address+symbol.size
        if not 0x100000<=start<end<=0x2000000 or (start<catalogue.address+catalogue.size and end>catalogue.address):
            raise ValueError('Invalid catalogue destination')
        data=original[symbol.file_offset:symbol.file_offset+symbol.size]
        checksum=zlib.crc32(data)
        records.append((symbol.address,symbol.size,len(payload),checksum))
        manifest.append(dict(info,address=symbol.address,bytes=symbol.size,offset=len(payload),crc32=checksum))
        payload.extend(data)
        executable[symbol.file_offset:symbol.file_offset+symbol.size]=bytes(symbol.size)
    if len(payload)>4*1024*1024:
        raise ValueError('Native private data exceeds bounded startup allocation')
    checksum=zlib.crc32(payload)
    if public_metadata['native_data_bytes']!=len(payload) or public_metadata['payload_crc32']!=checksum or \
        [(row['address'],row['bytes'],row['offset'],row['crc32']) for row in public_metadata['records']]!=records:
        raise ValueError('Reference ELF changed while preparing the native asset descriptor')
    header=struct.pack('<4I',1,len(records),len(payload),checksum)
    table=b'OOTPS2RELASSET01'+header+b''.join(struct.pack('<4I',*record) for record in records)
    executable[catalogue.file_offset:catalogue.file_offset+catalogue.size]=table.ljust(catalogue.size,b'\0')
    private=b'OOTPS2DATA01'.ljust(16,b'\0')+header+payload
    output.mkdir(parents=True,exist_ok=True)
    binary=output/'OOTPS2.ELF'
    binary.write_bytes(executable)
    (output/'PS2DATA.DAT').write_bytes(private)
    report=audit(project,binary,generated,procedural_icon=True)
    if report['status']!='PASS':
        raise ValueError('External ELF still contains private initializers')
    (output/'asset-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    result=dict(schema=1,status='PREPARED_PRIVATE_TEST',records=manifest,
                native_data_bytes=len(payload),elf_sha256=hashlib.sha256(executable).hexdigest(),
                private_dat_sha256=hashlib.sha256(private).hexdigest(),
                reference_sha256=hashlib.sha256(original).hexdigest(),
                public_distribution_cleared=False,unlinked_arrays=absent)
    (output/'external-assets.json').write_text(json.dumps(result,indent=2)+'\n')
    public_metadata['elf_sha256']=result['elf_sha256']
    (output/'native-assets.json').write_text(json.dumps(public_metadata,indent=2)+'\n')
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference',type=Path,required=True)
    parser.add_argument('--generated',type=Path,default=Path('build/PS2-ntsc12/generated'))
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--object-root',type=Path,help='Private build objects; defaults to generated.parent')
    args=parser.parse_args()
    result=prepare(args.reference,args.generated,args.output,args.object_root)
    print(json.dumps({key:result[key] for key in ['status','native_data_bytes','elf_sha256','public_distribution_cleared']}))
if __name__=='__main__':
    main()
