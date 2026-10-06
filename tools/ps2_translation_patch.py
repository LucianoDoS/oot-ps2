"""Apply reviewed translation patches to private ROM copies, with BPS CRC checks."""
import struct
import zlib


def apply_bps(source: bytes, patch: bytes) -> bytes:
    if patch[:4] != b'BPS1' or len(patch) < 19:
        raise ValueError('Invalid BPS header')
    source_crc, target_crc, patch_crc = struct.unpack_from('<3I', patch, len(patch) - 12)
    if zlib.crc32(patch[:-4]) != patch_crc or zlib.crc32(source) != source_crc:
        raise ValueError('BPS source or patch checksum mismatch')
    cursor = 4

    def number():
        nonlocal cursor
        value, shift = 0, 1
        for _ in range(10):
            if cursor >= len(patch) - 12:
                raise ValueError('Truncated BPS number')
            byte = patch[cursor]
            cursor += 1
            value += (byte & 127) * shift
            if byte & 128:
                return value
            shift <<= 7
            value += shift
        raise ValueError('Oversized BPS number')

    source_size, target_size, metadata_size = number(), number(), number()
    if source_size != len(source) or target_size > 128 * 1024 * 1024:
        raise ValueError('Invalid BPS sizes')
    cursor += metadata_size
    output = bytearray()
    source_relative = target_relative = 0
    while len(output) < target_size:
        action = number()
        length, kind = (action >> 2) + 1, action & 3
        if length > target_size - len(output):
            raise ValueError('BPS write outside target')
        if kind == 0:
            offset = len(output)
            payload = source[offset:offset + length]
        elif kind == 1:
            if cursor + length > len(patch) - 12:
                raise ValueError('Truncated BPS literal')
            payload = patch[cursor:cursor + length]
            cursor += length
        else:
            delta = number()
            delta = -(delta >> 1) if delta & 1 else delta >> 1
            if kind == 2:
                source_relative += delta
                if source_relative < 0:
                    raise ValueError('Negative BPS source address')
                payload = source[source_relative:source_relative + length]
                source_relative += length
            else:
                target_relative += delta
                if not 0 <= target_relative < len(output):
                    raise ValueError('Invalid BPS target copy')
                # Overlapping copies repeat the already written prefix.
                pattern = bytes(output[target_relative:])
                payload = (pattern * ((length + len(pattern) - 1) // len(pattern)))[:length]
                target_relative += length
        if len(payload) != length:
            raise ValueError('BPS read outside source')
        output.extend(payload)
    if cursor != len(patch) - 12 or zlib.crc32(output) != target_crc:
        raise ValueError('BPS target checksum or trailing data mismatch')
    return bytes(output)


def apply_ppf3(source: bytes, patch: bytes) -> bytes:
    # The Spanish patch uses PPF3 without block checks or undo records.
    if patch[:6] != b'PPF30\x02' or len(patch) < 60 or patch[56:60] != bytes(4):
        raise ValueError('Unsupported PPF3 layout')
    output = bytearray(source)
    cursor = 60
    while cursor < len(patch):
        if cursor + 9 > len(patch):
            raise ValueError('Truncated PPF3 record')
        offset, length = struct.unpack_from('<QB', patch, cursor)
        cursor += 9
        if cursor + length > len(patch) or offset + length > len(output):
            raise ValueError('PPF3 record outside ROM')
        output[offset:offset + length] = patch[cursor:cursor + length]
        cursor += length
    return bytes(output)
