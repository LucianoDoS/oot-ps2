import struct
import unittest
from ps2_soundfont_layout import convert_font


class SoundfontLayoutTest(unittest.TestCase):
    def fixture(self):
        bank = bytearray(0x140)
        # One instrument, one drum, one effect all share one sample/envelope.
        struct.pack_into('>III', bank, 0, 0x10, 0x20, 0x30)
        struct.pack_into('>I', bank, 0x10, 0x50)
        struct.pack_into('>If', bank, 0x20, 0x70, 1.25)
        bank[0x30:0x34] = bytes((0, 12, 80, 90))
        struct.pack_into('>I', bank, 0x34, 0xa0)
        for offset in (0x38, 0x40, 0x48):
            struct.pack_into('>If', bank, offset, 0x70, 0.75)
        bank[0x50:0x54] = bytes((50, 64, 0, 0xab))
        struct.pack_into('>IfI', bank, 0x54, 0x70, 2.0, 0xa0)
        struct.pack_into('>IIII', bank, 0x70, 0x28001234, 0x4321, 0xb0, 0xe0)
        struct.pack_into('>hhhh', bank, 0xa0, 10, 32000, -1, 0)
        struct.pack_into('>III', bank, 0xb0, 16, 200, 0xffffffff)
        bank[0xbc:0xc0] = b'LOOP'
        struct.pack_into('>16h', bank, 0xc0, *range(-8, 8))
        struct.pack_into('>II16h', bank, 0xe0, 2, 1, *range(-16, 0))
        return bank

    def test_shared_resources_and_bitfields(self):
        source = self.fixture()
        native, record = convert_font(source, 1, 1, 1)
        self.assertEqual(struct.unpack_from('<I', native, 0x70)[0], 0x00123422)
        self.assertEqual(struct.unpack_from('<If', native, 0x20), (0x70, 1.25))
        self.assertEqual(struct.unpack_from('<16h', native, 0xc0), tuple(range(-8, 8)))
        self.assertEqual(struct.unpack_from('<II16h', native, 0xe0), (2, 1, *range(-16, 0)))
        self.assertEqual(native[0x30:0x34], source[0x30:0x34])
        self.assertEqual(native[0xbc:0xc0], b'LOOP')
        self.assertEqual(record['resources'], 6)

    def test_bad_pointer_is_rejected(self):
        source = self.fixture()
        struct.pack_into('>I', source, 0x54, 0x4000)
        with self.assertRaises(ValueError):
            convert_font(source, 1, 1, 1)

    def test_invalid_predictor_size_is_rejected(self):
        source = self.fixture()
        struct.pack_into('>I', source, 0xe4, 0xffffffff)
        with self.assertRaises(ValueError):
            convert_font(source, 1, 1, 1)


if __name__ == '__main__':
    unittest.main()
