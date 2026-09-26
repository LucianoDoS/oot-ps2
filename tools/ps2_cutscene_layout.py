"""Native field ranges for ROM cutscenes (include/cutscene.h).

Command lengths and actor command IDs come from the pinned upstream parser.
Byte fields must retain their positions even inside CutsceneData unions.
"""
def cutscene_groups(data, start, end, csdis):
    def word(offset):
        if offset + 4 > end:
            raise ValueError('Truncated cutscene')
        return int.from_bytes(data[offset:offset + 4], 'big')

    groups = [(start, start + 8, 4)]
    cursor = start + 8
    for _ in range(word(start) + 1):
        opcode = word(cursor)
        if opcode == 0xFFFFFFFF:
            groups.append((cursor, cursor + 8, 4))
            cursor += 8
            break
        entry = csdis.multi_key(opcode)
        if entry is None:
            # The game skips these 0x30-byte records (z_demo.c default case).
            entry = csdis.unk_data_entry
        _, header_words, stop_byte, count_word, item_macro, item_words = entry
        groups.append((cursor, cursor + 4, 4))
        if opcode in (1, 2, 5, 6, 7, 8):
            groups.append((cursor + 4, cursor + 12, 2))
        elif opcode in (45, 1000):
            groups.extend(((cursor + 4, cursor + 8, 4), (cursor + 8, cursor + 16, 2)))
        else:
            groups.append((cursor + 4, cursor + header_words * 4, 4))
        count = None if count_word is None else (-count_word if count_word < 0 else word(cursor + (count_word + 1) * 4))
        cursor += header_words * 4
        index = 0
        while item_macro is not None and (stop_byte is not None or index < count):
            size = item_words * 4
            if cursor + size > end:
                raise ValueError('Truncated cutscene entry')
            if opcode in (1, 2, 5, 6, 7, 8):
                groups.extend(((cursor + 2, cursor + 4, 2), (cursor + 4, cursor + 8, 4),
                               (cursor + 8, cursor + 16, 2)))
            elif opcode in (4, 86, 87):
                groups.extend(((cursor + 2, cursor + 8, 2), (cursor + 8, cursor + size, 4)))
            elif opcode in (9, 140):
                groups.append((cursor, cursor + 6, 2))
            elif opcode == 19:
                groups.append((cursor, cursor + size, 2))
            elif opcode in (3, 124):
                groups.extend(((cursor, cursor + 8, 2), (cursor + 8, cursor + size, 4)))
            elif item_macro.startswith(('CS_ACTOR_CUE(', 'CS_PLAYER_CUE(')):
                groups.extend(((cursor, cursor + 12, 2), (cursor + 12, cursor + size, 4)))
            elif item_macro.startswith('CS_UNK_DATA('):
                groups.append((cursor, cursor + size, 4))
            else:
                raise ValueError(f'Unsupported cutscene entry {item_macro}')
            stopped = stop_byte is not None and data[cursor + stop_byte] != 0
            cursor += size
            index += 1
            if stopped:
                break
    if cursor != end:
        raise ValueError(f'Cutscene size mismatch: {cursor - start} != {end - start}')
    return groups
