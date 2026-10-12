#!/usr/bin/env python3
"""Validate isolated house-view or original title-cinematic EE timings."""
import argparse
import collections
import hashlib
import json
import pathlib
import re
import statistics
import sys

def pairs(line):
    return {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)(?=\s|$)', line)}

def metrics(values):
    ordered = sorted(values)
    return dict(mean=statistics.mean(values), median=statistics.median(values),
                p95=ordered[min(len(ordered)-1, int(len(ordered)*.95))], worst=max(values))

def inspect(root):
    manifest = json.loads((root/'fixture.json').read_text(encoding='utf-8-sig'))
    mode = manifest.get('mode', 'house_path')
    assert mode in ('house_path', 'title_cinematic', 'castle_gate'), 'Unknown fixture mode'
    cinematic = mode == 'title_cinematic'
    castle = mode == 'castle_gate'
    if castle:
        assert manifest['schema'] == 5 and manifest['scene'] == 95 and manifest['entrance'] == 0x47e, 'Incorrect castle identity'
        assert manifest['camera_fixture_version'] == 4 and manifest['camera'] == 'gate_hill_sun', 'Incorrect castle cameras'
        assert 0 <= manifest['day_time'] <= 65535 and manifest['environment_frame_aligned'], 'Invalid castle environment identity'
    assert manifest['views'] == (1 if cinematic else 3), 'Incorrect view count for mode'
    if cinematic:
        assert manifest['schema'] == 4 and 300 <= manifest['frames'] <= 600, 'Invalid cinematic window'
        assert manifest['scene'] == 81 and manifest['entrance'] == 0xcd, 'Incorrect title scene/entrance'
        assert manifest['camera'] == 'original_moving' and manifest['camera_fixture_version'] == 3, 'Incorrect cinematic camera mode'
        assert manifest['title_initializer'] == 'TitleSetup_Init' and manifest['cutscene_index'] == 0xfff3, 'Incorrect title initializer'
        assert manifest['game_mode'] == 1 and manifest['link_age'] == 0 and manifest['motion_frame_aligned'], 'Incorrect title state identity'
        assert manifest['seed_override'] and manifest['profile_alignment'] == 'fixture_frame', 'Cinematic seed/profile alignment is required'
    text = (root/'runtime.log').read_text(encoding='utf-8-sig', errors='replace')
    # PCSX2 prefixes EE console records with its own elapsed-time field.
    text = re.sub(r'(?m)^\[\s*\d+(?:\.\d+)?\]\s*', '', text)
    for name, field in [('oot_ps2_perf_scene.elf', 'elf_sha256'), ('oot.z64', 'image_sha256')]:
        assert hashlib.sha256((root/name).read_bytes()).hexdigest() == manifest[field], f'{name} hash mismatch'
    assert f'[PS2 PERF SCENE RESULT] PASS views={manifest["views"]} failures=0' in text, 'Missing completion'
    assert not re.search(r'\[PS2 [^\r\n]*\] FAIL\b|\[PS2 FAULT\]|TLB Miss|Address ?Error|branch in delay slot', text), 'Runtime failure'
    result = dict(status='PASS', fixture=manifest, views=[],
                  limitations=['Emulated EE timings; physical PS2 validation is separate.',
                               ('Original moving title camera and actors; no player input.' if cinematic else
                                'Fixed views with a frozen player; normal actors/effects still run.'),
                               'Camera captures need visual review before choosing a heavy-view baseline.'])
    runtime_report = json.loads((root/'report.json').read_text(encoding='utf-8-sig'))
    result['runtime_identity'] = {key:runtime_report[key] for key in ['iniSha256','emulatorSha256','cardsDisabled','inputDisabled']}
    for view in range(manifest['views']):
        rows = [pairs(line) for line in text.splitlines() if line.startswith('[PS2 PERF SCENE FRAME] ') and pairs(line)['view'] == view]
        expected = list(range(manifest['warmup']+1, manifest['warmup']+manifest['frames']+1))
        assert [row['frame'] for row in rows] == expected, f'View {view}: missing/duplicate/out-of-order samples'
        if manifest.get('seed_override'):
            seeds = [pairs(line) for line in text.splitlines() if line.startswith(f'[PS2 PERF SCENE RNG] view={view} ')]
            assert len(seeds) >= 2 and all(row['effectiveSeed'] == manifest['seed'] for row in seeds), f'View {view}: initialization seed was not overridden'
        if manifest.get('profile_alignment') == 'fixture_frame':
            assert [row['frame'] for row in rows if row['profiled']] == [frame for frame in expected if frame % manifest['profile_every'] == 0], f'View {view}: profiling differs from fixture frames'
        assert all(row['triangles'] > 0 and row['submitted'] > 0 and row['translateUs'] > 0 for row in rows), f'View {view}: empty geometry/timing records'
        assert all(row['invalid'] == row['textureFailed'] == 0 for row in rows), f'View {view}: invalid geometry or failed texture uploads'
        motion = []
        if castle:
            environment = [pairs(line) for line in text.splitlines() if line.startswith(f'[PS2 PERF SCENE ENVIRONMENT] view={view} ')]
            assert [row['frame'] for row in environment] == expected, 'Missing castle environment samples'
            # Environment_Update advances the retail daytime by ten ticks
            # after the harness resets it, consistently on every task.
            actual_day_time = manifest.get('environment_day_time', (manifest['day_time']+10)&65535)
            assert all(row['scene'] == 95 and row['room'] == 0 and row['dayTime'] == actual_day_time for row in environment), 'Castle environment changed during timing'
        if cinematic:
            motion = [pairs(line) for line in text.splitlines() if line.startswith(f'[PS2 PERF SCENE MOTION] view={view} ')]
            assert [row['frame'] for row in motion] == expected, 'Missing/duplicate/out-of-order cinematic motion records'
            assert all(row['scene'] == manifest['scene'] and row['room'] == 0 and
                       row['gameMode'] == manifest['game_mode'] and row['linkAge'] == manifest['link_age']
                       for row in motion), 'Cinematic left the expected title state'
            assert all(row['cutsceneState'] in (1, 2, 3, 4) for row in motion), 'Title cutscene is not active'
            assert all(a['cutsceneFrame'] <= b['cutsceneFrame'] for a, b in zip(motion, motion[1:])), 'Cutscene reset during the window'
            assert motion[-1]['cutsceneFrame'] - motion[0]['cutsceneFrame'] >= manifest['frames']*.9, 'Cutscene did not advance normally'
            eyes = {(row['eyeX'], row['eyeY'], row['eyeZ']) for row in motion}
            assert len(eyes) >= manifest['frames']*.25, 'Original title camera did not move'
            assert all(10000 <= row['fovMilli'] <= 120000 for row in motion), 'Invalid camera FOV'
            assert sum(row['riding'] and row['horseDrawable'] for row in motion) >= 60, 'Window does not include Link riding the loaded horse'
            camera = ['original_moving']
        else:
            camera = [line for line in text.splitlines() if line.startswith(f'[PS2 PERF SCENE CAMERA] view={view} ')]
            assert len(camera) == 1, f'View {view}: missing camera coordinates'
            if manifest.get('camera_fixture_version', 1) >= 2:
                assert camera[0].endswith('settled=1'), f'View {view}: anchor was not settled'
        conditions = re.findall(rf'\[PS2 PERF SCENE VIEW\] PASS view={view} scene=(\d+) room=(\d+) frames=(\d+) capture=(\d+)', text)
        assert len(conditions) == 1 and int(conditions[0][0]) == manifest['scene'] and int(conditions[0][1]) == 0 and int(conditions[0][2]) > expected[-1] and int(conditions[0][3]) == 1, f'View {view}: incorrect scene/room/capture conditions'
        assert f'[PS2 PERF SCENE MEASURED] PASS view={view} count={manifest["frames"]} expected={manifest["frames"]} captureOutsideTiming=1' in text
        assert f'[PS2 PERF SCENE CAPTURE] PASS view={view} file=host:perf-view-{view}.ppm' in text
        raw = (root/f'perf-view-{view}.ppm').read_bytes()
        assert raw.startswith(b'P6\n320 240\n255\n') and len(raw) == len(b'P6\n320 240\n255\n')+320*240*3, f'View {view}: invalid capture'
        pixels = raw[len(b'P6\n320 240\n255\n'):]
        nonblack = sum(any(pixels[offset:offset+3]) for offset in range(0, len(pixels), 3))
        assert nonblack > 320*240*.10, f'View {view}: nearly black capture'
        clean = [row for row in rows if not row['profiled'] and row['intervalUs'] > 0]
        assert len(clean) >= manifest['frames']*.90, f'View {view}: too few unprofiled frames'
        # The first measured interval begins in an unrecorded warmup frame,
        # whose profiling state is unavailable. Exclude it consistently.
        cadence = [row for i, row in enumerate(rows) if i > 0 and not row['profiled'] and row['intervalUs'] > 0 and
                   not rows[i-1]['profiled']]
        signature = collections.Counter((row['triangles'], row['submitted'], row['clipped'], row['culled'], row['invalid'], row['textureFailed']) for row in rows)
        ordered_geometry = [[row[key] for key in ['frame','triangles','submitted','clipped','culled','invalid','textureFailed']] for row in rows]
        view_report = dict(view=view, camera=camera[0], samples=len(rows), timed_samples=len(clean),
                           excluded_profiled=sum(row['profiled'] for row in rows),
                           profiled_indices=[row['frame'] for row in rows if row['profiled']],
                           timed_indices=[row['frame'] for row in clean],
                           cadence_indices=[row['frame'] for row in cadence],
                           geometry_histogram=[dict(values=list(key), frames=count) for key, count in sorted(signature.items())],
                           geometry_sequence_sha256=hashlib.sha256(json.dumps(ordered_geometry, separators=(',', ':')).encode()).hexdigest(),
                           geometry_fields=['triangles','submitted','clipped','culled','invalid','textureFailed'],
                           capture_sha256=hashlib.sha256(raw).hexdigest(), nonblack_pixels=nonblack,
                           cadence_samples=len(cadence),
                           color_invalidation={key:sum(row.get(key, 0) for row in rows)
                                               for key in ['colorRetained','colorInvalidated','colorForced']},
                           timing_us={key:metrics([row[key] for row in (cadence if key == 'intervalUs' else clean)])
                                      for key in ['translateUs','submitUs','presentUs','intervalUs']})
        view_report['fps'] = 1e6 / view_report['timing_us']['intervalUs']['mean']
        if castle:
            view_report['environment_sequence_sha256'] = hashlib.sha256(json.dumps(environment, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
            view_report['sun_screen_milli'] = [environment[-1]['screenX'], environment[-1]['screenY']]
        if cinematic:
            view_report['motion_sequence_sha256'] = hashlib.sha256(json.dumps(motion, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
            view_report['cutscene_frames'] = [motion[0]['cutsceneFrame'], motion[-1]['cutsceneFrame']]
            view_report['moving_camera_positions'] = len(eyes)
            view_report['riding_frames'] = sum(row['riding'] and row['horseDrawable'] for row in motion)
            view_report['blocks'] = []
            for start in range(expected[0], expected[-1]+1, 60):
                end = min(start+59, expected[-1])
                block = [row for row in clean if start <= row['frame'] <= end]
                intervals = [row['intervalUs'] for row in cadence if start <= row['frame'] <= end]
                view_report['blocks'].append(dict(first_frame=start, last_frame=end,
                    translate_us=metrics([row['translateUs'] for row in block]),
                    cadence_us=metrics(intervals), fps=1e6/statistics.mean(intervals),
                    riding_frames=sum(row['riding'] and row['horseDrawable'] for row in motion if start <= row['frame'] <= end)))
        result['views'].append(view_report)
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=pathlib.Path)
    parser.add_argument('--baseline', type=pathlib.Path, help='Previous summary.json for same fixture/cameras')
    args = parser.parse_args()
    try:
        report = inspect(args.folder)
        if args.baseline:
            baseline = json.loads(args.baseline.read_text(encoding='utf-8-sig'))
            fixture_keys = ['schema','views','frames','warmup','entrance','scene','seed','profile_every',
                            'day_time','camera_fixture_version','image_sha256','synthetic_save','seed_override','profile_alignment',
                            'mode','camera','title_initializer','cutscene_index','link_age','game_mode','motion_frame_aligned']
            fixture_equal = all(baseline['fixture'].get(key) == report['fixture'].get(key) for key in fixture_keys)
            runtime_equal = baseline.get('runtime_identity') == report.get('runtime_identity')
            report['comparison_identity'] = dict(fixture_equal=fixture_equal, runtime_equal=runtime_equal,
                                                fixture_fields=fixture_keys)
            comparisons = []
            for before, after in zip(baseline['views'], report['views']):
                geometry_equal = before['camera'] == after['camera'] and before['geometry_histogram'] == after['geometry_histogram'] and before.get('geometry_sequence_sha256') == after['geometry_sequence_sha256']
                motion_equal = before.get('motion_sequence_sha256') == after.get('motion_sequence_sha256')
                motion_equal = motion_equal and before.get('environment_sequence_sha256') == after.get('environment_sequence_sha256')
                sampling_equal = all(before.get(key) == after.get(key) for key in ['profiled_indices','timed_indices','cadence_indices'])
                comparable = fixture_equal and runtime_equal and geometry_equal and sampling_equal and motion_equal
                comparisons.append(dict(view=after['view'], camera_geometry_equal=geometry_equal, comparable=comparable,
                    sampling_equal=sampling_equal,
                    motion_equal=motion_equal,
                    capture_equal=before['capture_sha256']==after['capture_sha256'],
                    translate_percent=(after['timing_us']['translateUs']['mean']/before['timing_us']['translateUs']['mean']-1)*100 if comparable else None,
                    fps_before=before['fps'], fps_after=after['fps']))
            report['comparison'] = comparisons
        (args.folder/'summary.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        for view in report['views']:
            print(f'View {view["view"]}: {view["fps"]:.2f} FPS, translation {view["timing_us"]["translateUs"]["mean"]/1000:.2f} ms, {view["timed_samples"]} timed frames')
        return 0
    except (AssertionError, OSError, KeyError, ValueError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1
if __name__ == '__main__':
    raise SystemExit(main())
