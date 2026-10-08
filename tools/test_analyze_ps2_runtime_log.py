"""Prevent startup, USB reads and logger pauses from becoming steady render evidence."""
import unittest
from analyze_ps2_runtime_log import analyze


class RuntimeAnalysisTests(unittest.TestCase):
    def test_sparse_profiles_are_separate_and_damaged_rows_are_rejected(self):
        log = '''OOT PS2 runtime log v3 test AUTO-PLAY
tMs=0 [PS2 PERF SAMPLING] every=300 no-HUD automatic logger
tMs=1000 [PS2 GFX PROFILE] frame=300 totalUs=70000 vtxUs=10000 triUs=30000 rectUs=900 lookupUs=4000 convertUs=2000 uploadUs=1000 calls=12,34,5,6,7,8 (included)
tMs=1001 [PS2 GEOMETRY PROFILE] frame=300 matrixUs=1000 transformUs=2000 lightUs=3000 texgenUs=4000 clipUs=5000 submitUs=6000 calls=1,2,3,4,5,6
tMs=1002 [PS2 PROFILE PREEMPTION] frame=300 wallUs=80000 dspExcludedUs=10000
tMs=1003 [PS2 TEXTURE CONTENT PROFILE] frame=300 timeUs=2000 calls=100
tMs=1003 [PS2 STATE PROFILE] frame=300 timeUs=200 calls=100 writes=30 reused=70
tMs=1003 [PS2 VERTEX COLOR PROFILE] frame=300 timeUs=500 calls=300
tMs=1003 [PS2 MATERIAL POINT PROFILE] frame=300 materialUs=600 materialCalls=4 pointUs=9000 pointCalls=1200
tMs=1003 [PS2 COMBINER ANALYSIS PROFILE] frame=300 timeUs=80 lookups=100 lastHits=60 savedHits=30 rebuilds=10 entries=32 storageBytes=2716
tMs=1004 [PS2 GFX PROFILE] frame=600 totalUs=\0
tMs=1005 [PS2 GFX PROFILE] frame=900 totalUs=1
'''
        result = analyze(log)
        self.assertEqual(result['sparse_profile_every'], 300)
        self.assertEqual(len(result['graphics_profiles']), 1)
        self.assertEqual(result['graphics_profiles'][0]['calls'], [12, 34, 5, 6, 7, 8])
        self.assertEqual(result['geometry_profiles'][0]['submitUs'], 6000)
        self.assertEqual(result['profile_preemption'][0]['dspExcludedUs'], 10000)
        self.assertEqual(result['texture_content_profiles'][0]['timeUs'], 2000)
        self.assertEqual(result['state_profiles'][0]['reused'], 70)
        self.assertEqual(result['vertex_color_profiles'][0]['timeUs'], 500)
        self.assertEqual(result['material_point_profiles'][0]['pointCalls'], 1200)
        self.assertEqual(result['combiner_analysis_profiles'][0]['savedHits'], 30)
        self.assertEqual(len(result['damaged_lines']), 2)
        self.assertIsNone(result['fps_median'])

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

    def test_duplicate_and_backwards_intervals_cannot_supply_steady_fps(self):
        log = """OOT PS2 runtime log v3 test
tMs=1000 SAMPLE seq=1 intervalMs=1000 frames=20 fps10=200 gfxMs=600,40,280
tMs=1001 USB reads=0 errors=0
tMs=3000 SAMPLE seq=3 intervalMs=1000 frames=99 fps10=990 gfxMs=0,0,0
tMs=3001 USB reads=0 errors=0
tMs=2000 SAMPLE seq=2 intervalMs=1000 frames=99 fps10=990 gfxMs=0,0,0
tMs=2001 USB reads=0 errors=0
tMs=3100 SAMPLE seq=3 intervalMs=1000 frames=99 fps10=990 gfxMs=0,0,0
tMs=3101 USB reads=0 errors=0
tMs=4100 SAMPLE seq=4 intervalMs=1000 frames=10 fps10=100 gfxMs=700,20,140
tMs=4101 USB reads=0 errors=0
"""
        report = analyze(log)
        self.assertEqual(report['samples'], 5)
        self.assertEqual(report['usable_samples'], 2)
        self.assertEqual([row['seq'] for row in report['rejected_sample_intervals']], [3,2,3])
        self.assertEqual(report['recent_no_read_render']['frames'], 30)
        self.assertEqual(report['fps_max'], 20)
        self.assertEqual(report['fps_median'], 15)

    def test_draw_detail_requires_complete_fields(self):
        log = """OOT PS2 runtime log v3 test
tMs=1000 [PS2 DRAW DETAIL PROFILE] frame=300 keyUs=12 keyCalls=3 packetUs=70 packetCalls=20 wrapUs=40 wrapCalls=15
tMs=2000 [PS2 DRAW DETAIL PROFILE] frame=600 keyUs=12 keyCalls=3 packetUs=70 packetCalls=20 wrapUs=40
"""
        report = analyze(log)
        self.assertEqual(len(report['draw_detail_profiles']), 1)
        self.assertEqual(report['draw_detail_profiles'][0]['packetUs'], 70)
        self.assertEqual(len(report['damaged_lines']), 1)

    def test_draw_setup_requires_complete_fields_and_preserves_snapshot_reasons(self):
        log = """OOT PS2 runtime log v3 test
tMs=1000 [PS2 DRAW SETUP PROFILE] frame=300 setupUs=12 setupCalls=3 tileUs=70 tileCalls=20 metadataUs=40 metadataCalls=15
tMs=1001 [PS2 TEXTURE CONTENT] exactHits=91 hashes=12 comparedBytes=130208 hashedBytes=1280 smallBypass=1 largeBypass=2 mutations=3 newSnapshots=6 descriptorEvictions=4 overlapEvictions=5
tMs=2000 [PS2 DRAW SETUP PROFILE] frame=600 setupUs=12 setupCalls=3 tileUs=70 tileCalls=20 metadataUs=40
"""
        report = analyze(log)
        self.assertEqual(len(report['draw_setup_profiles']), 1)
        self.assertEqual(report['draw_setup_profiles'][0]['metadataUs'], 40)
        self.assertEqual(report['texture_content_snapshots'][0]['mutations'], 3)
        self.assertEqual(report['texture_content_snapshots'][0]['overlapEvictions'], 5)
        self.assertEqual(len(report['damaged_lines']), 1)


if __name__ == '__main__':
    unittest.main()
