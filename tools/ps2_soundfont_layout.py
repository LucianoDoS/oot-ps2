"""Convert explicitly described OoT soundfont structures to native EE layout.

Wave data and sequence bytecode stay untouched. Every resource is visited using
the bank's relative pointers, read from an immutable big-endian source.
"""
import struct


def convert_font(source, instruments, drums, effects):
    native = bytearray(source)
    visited = set()
    fields = {}

    def bounds(offset, size):
        if offset < 0 or offset + size > len(source):
            raise ValueError(f'Soundfont resource outside bank: {offset:#x}+{size:#x}')

    def u32(offset):
        bounds(offset, 4)
        return struct.unpack_from('>I', source, offset)[0]

    def scalar(offset, width):
        bounds(offset, width)
        if offset in fields:
            if fields[offset] != width:
                raise ValueError('Conflicting soundfont field widths')
            return
        fields[offset] = width
        native[offset:offset + width] = source[offset:offset + width][::-1]

    def pointer(offset):
        result = u32(offset)
        scalar(offset, 4)
        return result

    def begin(kind, offset, size):
        if not offset or (kind, offset) in visited:
            return False
        bounds(offset, size)
        visited.add((kind, offset))
        return True

    def envelope(offset):
        if not begin('envelope', offset, 4):
            return
        # ADSR scripts may jump/restart. Continue through the jump target and
        # stop at the explicit disable/hang instruction, preserving all points.
        pending, seen = [0], set()
        while pending:
            index = pending.pop()
            if index in seen:
                continue
            if not 0 <= index < 256:
                raise ValueError('Invalid ADSR script index')
            seen.add(index)
            point = offset + index * 4
            bounds(point, 4)
            delay, arg = struct.unpack_from('>hh', source, point)
            scalar(point, 2)
            scalar(point + 2, 2)
            if delay > 0:
                pending.append(index + 1)
            elif delay == -2:
                pending.append(arg)
            elif delay == -3:
                pending.append(0)
            elif delay not in (0, -1):
                raise ValueError(f'Unknown ADSR instruction {delay}')

    def sample(offset):
        if not begin('sample', offset, 16):
            return
        control = u32(offset)
        # GCC on little-endian EE allocates these C bitfields from bit zero.
        native_control = ((control >> 28) & 15) | (((control >> 26) & 3) << 4)
        native_control |= ((control >> 25) & 1) << 6
        native_control |= ((control >> 24) & 1) << 7
        native_control |= (control & 0xffffff) << 8
        fields[offset] = 4
        struct.pack_into('<I', native, offset, native_control)
        pointer(offset + 4)
        loop, book = pointer(offset + 8), pointer(offset + 12)
        if begin('loop', loop, 16):
            count = u32(loop + 8)
            for position in range(loop, loop + 12, 4):
                scalar(position, 4)
            if count:
                bounds(loop + 16, 32)
                for position in range(loop + 16, loop + 48, 2):
                    scalar(position, 2)
        if begin('book', book, 8):
            order, predictors = u32(book), u32(book + 4)
            if not 1 <= order <= 8 or not 1 <= predictors <= 16:
                raise ValueError(f'Invalid predictor book: {order}/{predictors}')
            length = 8 * order * predictors * 2
            bounds(book + 8, length)
            scalar(book, 4)
            scalar(book + 4, 4)
            for position in range(book + 8, book + 8 + length, 2):
                scalar(position, 2)

    def tuned(offset):
        sample(pointer(offset))
        scalar(offset + 4, 4)

    drum_list, effect_list = pointer(0), pointer(4)
    bounds(8, instruments * 4)
    for index in range(instruments):
        offset = pointer(8 + index * 4)
        if begin('instrument', offset, 32):
            envelope(pointer(offset + 4))
            for position in (8, 16, 24):
                tuned(offset + position)
    if drums and not drum_list:
        raise ValueError('Missing drum pointer list')
    bounds(drum_list, drums * 4)
    for index in range(drums):
        offset = pointer(drum_list + index * 4)
        if begin('drum', offset, 16):
            tuned(offset + 4)
            envelope(pointer(offset + 12))
    if effects and not effect_list:
        raise ValueError('Missing sound effect array')
    bounds(effect_list, effects * 8)
    for index in range(effects):
        tuned(effect_list + index * 8)
    return native, {'instruments': instruments, 'drums': drums, 'effects': effects,
                    'resources': len(visited), 'scalar_fields': len(fields)}


def convert_audiobank(data, entries, config):
    bank, code = entries['Audiobank'], entries['code']
    table = code.vrom_start + config.variables['gSoundFontTable'] - config.dmadata_segments['code'].vram
    count = struct.unpack_from('>h', data, table)[0]
    if not 0 < count < 256:
        raise ValueError('Invalid soundfont table count')
    records, converted = [], set()
    for index in range(count):
        offset, size, _, _, _, counts, effects = struct.unpack_from('>IIbbhhh', data, table + 16 + index * 16)
        if not size:  # table aliases refer to an existing bank index
            continue
        if not bank.vrom_start <= bank.vrom_start + offset < bank.vrom_start + offset + size <= bank.vrom_end:
            raise ValueError(f'Soundfont {index} outside Audiobank')
        if (offset, size) in converted:
            continue
        converted.add((offset, size))
        start = bank.vrom_start + offset
        native, record = convert_font(bytes(data[start:start + size]), (counts >> 8) & 255, counts & 255, effects & 0xffff)
        data[start:start + size] = native
        records.append(dict(record, index=index, offset=offset, size=size))
    return records
