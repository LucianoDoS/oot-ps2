"""Prevent startup, USB reads and logger pauses from becoming steady render evidence."""
import unittest
from analyze_ps2_runtime_log import analyze


class RuntimeAnalysisTests(unittest.TestCase):
    def test_reports_validation_and_storage_without_inventing_fps(self):
        log = '''OOT PS2 runtime log v3 test AUTO-PLAY
tMs=1000 [PS2 TEXTURE CONTENT] exactHits=91 hashes=12 comparedBytes=130208 hashedBytes=1280
tMs=1001 [PS2 GFX STORAGE] frame=600 scratchpadBytes=10704 capacity=16384
tMs=1002 [PS2 TEXTURE CONTENT PROFILE] frame=600 timeUs=485 calls=103
tMs=1003 [PS2 GFX STORAGE] ERROR scratchpad guard changed index=4
tMs=1004 [PS2 TEXTURE CONTENT] exactHits=99 hashes=\0
'''
        result = analyze(log)
        self.assertEqual(result['texture_content_snapshots'], [dict(
            tMs=1000, exactHits=91, hashes=12, comparedBytes=130208, hashedBytes=1280)])
        self.assertEqual(result['scratchpad_storage'], [dict(
            tMs=1001, frame=600, scratchpadBytes=10704, capacity=16384)])
        self.assertEqual(len(result['scratchpad_errors']), 1)
        self.assertEqual(result['samples'], 0)
        self.assertIsNone(result['fps_median'])
        self.assertIsNone(result['recent_no_read_render'])

    def test_excludes_reads_flushes_stalls_and_failed_clock(self):
        log = '''OOT PS2 runtime log v3 test AUTO-PLAY
tMs=1000 SAMPLE intervalMs=1000 frames=20 fps10=200 gfxMs=600,40,280
tMs=1001 USB reads=0 errors=0
tMs=2000 SAMPLE intervalMs=1000 frames=10 fps10=100 gfxMs=900,80,190
tMs=2001 USB reads=1 errors=0
tMs=3000 SAMPLE intervalMs=1000 frames=1 fps10=10 gfxMs=950,30,10
tMs=3001 USB reads=0 errors=0
tMs=3050 FLUSH durationMs=100
tMs=4000 SAMPLE intervalMs=1000 frames=0 fps10=0 gfxMs=0,0,0
tMs=4001 USB reads=0 errors=0
tMs=5000 SAMPLE intervalMs=1000 frames=10 fps10=100 gfxMs=900,50,50 clockFallback=1
tMs=5001 USB reads=0 errors=0
tMs=6000 SAMPLE intervalMs=1000 frames=10 fps10=100 gfxMs=700,20,140 clockFallback=0
tMs=6001 USB reads=0 errors=0
'''
        result = analyze(log)
        window = result['recent_no_read_render']
        self.assertEqual((window['intervals'], window['frames']), (2, 30))
        self.assertEqual(window['fps_median'], 15)
        self.assertEqual(window['translate_ms_per_frame'], 43.33)
        self.assertEqual(window['submit_ms_per_frame'], 2)
        self.assertEqual(window['present_ms_per_frame'], 14)
        self.assertEqual(result['logger_flush_total_ms'], 100)

    def test_no_inference_from_missing_or_incomplete_samples(self):
        for log in ('', 'OOT PS2 runtime log v3 test\n',
            'OOT PS2 runtime log v3 test\ntMs=1000 SAMPLE intervalMs=1000 frames=20 fps10=200\n'):
            self.assertIsNone(analyze(log)['recent_no_read_render'])

    def test_corrupt_sector_cannot_supply_a_render_sample(self):
        header = 'OOT PS2 runtime log v3 test\n' + '\n' * 6
        log = header + '''tMs=1000 SAMPLE seq=1 intervalMs=1000 frames=10 fps10=100 gfxMs=700,20,140
tMs=1001 USB reads=0 errors=0
tMs=2000 SAMPLE seq=2 intervalMs=1000 frames=10 fps10=100 gfxMs=700,20,140\0\0
tMs=2001 USB reads=0 errors=0
6 SAMPLE seq=3 intervalMs=1000 frames=99 fps10=990 gfxMs=0,0,0
tMs=3001 USB reads=0 errors=0
tMs=4000 SAMPLE seq=4 intervalMs=1000 frames=10 fps10=100 gfxMs=700,20,140
tMs=4001 USB reads=0 errors=0
'''
        result = analyze(log)
        self.assertEqual(result['samples'], 2)
        self.assertEqual(result['nul_bytes'], 2)
        self.assertEqual(len(result['damaged_lines']), 2)
        self.assertEqual(result['sample_sequence_gaps'], [dict(after=1, before=4)])
        self.assertEqual(result['recent_no_read_render']['frames'], 20)

    def test_usb_after_partial_sample_cannot_overwrite_previous_reads(self):
        log = '''OOT PS2 runtime log v3 test
tMs=1000 SAMPLE seq=1 intervalMs=1000 frames=20 fps10=200 gfxMs=600,40,280
tMs=1001 USB reads=1 errors=0
tMs=2000 SAMPLE seq=2 intervalMs=1000 fps10=200
tMs=2001 USB reads=0 errors=0
'''
        result = analyze(log)
        self.assertEqual(result['samples'], 1)
        self.assertEqual(len(result['damaged_lines']), 1)
        self.assertIsNone(result['recent_no_read_render'])


if __name__ == '__main__':
    unittest.main()
