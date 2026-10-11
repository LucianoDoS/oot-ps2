#!/usr/bin/env python3
"""Validate completion/captures and summarize fixed-view EE timings."""
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
    text = (root/'runtime.log').read_text(encoding='utf-8-sig', errors='replace')
    # PCSX2 prefixes EE console records with its own elapsed-time field.
    text = re.sub(r'(?m)^\[\s*\d+(?:\.\d+)?\]\s*', '', text)
    for name, field in [('oot_ps2_perf_scene.elf', 'elf_sha256'), ('oot.z64', 'image_sha256')]:
        assert hashlib.sha256((root/name).read_bytes()).hexdigest() == manifest[field], f'{name} hash mismatch'
    assert '[PS2 PERF SCENE RESULT] PASS views=3 failures=0' in text, 'Missing completion'
    assert not re.search(r'\[PS2 [^\r\n]*\] FAIL\b|\[PS2 FAULT\]|TLB Miss|Address ?Error|branch in delay slot', text), 'Runtime failure'
    result = dict(status='PASS', fixture=manifest, views=[],
                  limitations=['Emulated EE timings; physical PS2 validation is separate.',
                               'Fixed views with a frozen player; normal actors/effects still run.',
                               'Camera captures need visual review before choosing a heavy-view baseline.'])
    runtime_report = json.loads((root/'report.json').read_text(encoding='utf-8-sig'))
    result['runtime_identity'] = {key:runtime_report[key] for key in ['iniSha256','emulatorSha256','cardsDisabled','inputDisabled']}
    for view in range(3):
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
                            'day_time','camera_fixture_version','image_sha256','synthetic_save','seed_override','profile_alignment']
            fixture_equal = all(baseline['fixture'].get(key) == report['fixture'].get(key) for key in fixture_keys)
            runtime_equal = baseline.get('runtime_identity') == report.get('runtime_identity')
            report['comparison_identity'] = dict(fixture_equal=fixture_equal, runtime_equal=runtime_equal,
                                                fixture_fields=fixture_keys)
            comparisons = []
            for before, after in zip(baseline['views'], report['views']):
                geometry_equal = before['camera'] == after['camera'] and before['geometry_histogram'] == after['geometry_histogram'] and before.get('geometry_sequence_sha256') == after['geometry_sequence_sha256']
                sampling_equal = all(before.get(key) == after.get(key) for key in ['profiled_indices','timed_indices','cadence_indices'])
                comparable = fixture_equal and runtime_equal and geometry_equal and sampling_equal
                comparisons.append(dict(view=after['view'], camera_geometry_equal=geometry_equal, comparable=comparable,
                    sampling_equal=sampling_equal,
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
