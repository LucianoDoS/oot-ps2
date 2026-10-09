#!/usr/bin/env python3
"""Summarize DIAG.LOG; observations do not certify a hardware root cause."""
from collections import Counter
import argparse
import json
from pathlib import Path
import re
import statistics

def analyze(text):
    samples, reads, rooms, flushes, audio, usb = [], [], [], [], [], []
    sample_lines = []
    damaged_lines, sequence_gaps = [], []
    texture_content, scratchpad_storage, scratchpad_errors = [], [], []
    profiles = {name: [] for name in ('graphics', 'geometry', 'content', 'preemption',
                                    'state', 'vertex_color', 'material_point', 'analysis', 'draw_detail', 'draw_setup', 'palette', 'runs', 'fence_source', 'fence_state')}
    fence_source_fields = ('texture', 'geometry', 'moveWord', 'moveMem', 'timg', 'tile', 'tileSize', 'load', 'tlut')
    fence_state_fields = ('combine', 'prim', 'env', 'fog', 'blend', 'fill', 'otherMode', 'sync', 'noop', 'other')
    profile_fields = {
        '[PS2 GFX PROFILE]': ('graphics', ('totalUs', 'vtxUs', 'triUs', 'rectUs', 'lookupUs', 'convertUs', 'uploadUs')),
        '[PS2 GEOMETRY PROFILE]': ('geometry', ('matrixUs', 'transformUs', 'lightUs', 'texgenUs', 'clipUs', 'submitUs')),
        '[PS2 TEXTURE CONTENT PROFILE]': ('content', ('timeUs', 'calls')),
        '[PS2 PROFILE PREEMPTION]': ('preemption', ('wallUs', 'dspExcludedUs')),
        '[PS2 STATE PROFILE]': ('state', ('timeUs', 'calls', 'writes', 'reused')),
        '[PS2 VERTEX COLOR PROFILE]': ('vertex_color', ('timeUs', 'calls')),
        '[PS2 MATERIAL POINT PROFILE]': ('material_point', ('materialUs', 'materialCalls', 'pointUs', 'pointCalls')),
        '[PS2 COMBINER ANALYSIS PROFILE]': ('analysis', ('timeUs', 'lookups', 'lastHits', 'savedHits', 'rebuilds', 'entries', 'storageBytes')),
        '[PS2 DRAW DETAIL PROFILE]': ('draw_detail', ('keyUs', 'keyCalls', 'packetUs', 'packetCalls', 'wrapUs', 'wrapCalls')),
        '[PS2 DRAW SETUP PROFILE]': ('draw_setup', ('setupUs', 'setupCalls', 'tileUs', 'tileCalls', 'metadataUs', 'metadataCalls')),
        '[PS2 PALETTE HASH PROFILE]': ('palette', ('timeUs', 'calls', 'hits', 'builds', 'changedEntries')),
        '[PS2 MATERIAL RUN PROFILE]': ('runs', ('vtxFences', 'dlFences', 'endFences', 'otherFences')),
        '[PS2 MATERIAL FENCE PROFILE]': ('fence_source', fence_source_fields),
        '[PS2 MATERIAL FENCE STATE PROFILE]': ('fence_state', fence_state_fields),
    }
    sparse_profile_every = None
    previous_sequence = None
    current_sample = None
    for number, line in enumerate(text.splitlines(), 1):
        if '\0' in line or (line.strip() and number > 7 and not line.startswith('tMs=') and
            not line.startswith('LOG_LIMIT')):
            damaged_lines.append(number)
            current_sample = None
            continue
        row = {key: int(value) for key, value in re.findall(r'(\w+)=([0-9]+)(?=\s|$)', line)}
        if (line.startswith('tMs=') and 'tMs' not in row) or (' SAMPLE ' in line and not
            all(key in row for key in ('tMs', 'intervalMs', 'frames', 'fps10'))):
            damaged_lines.append(number)
            current_sample = None
            continue
        if ' SAMPLE ' in line:
            if 'seq' in row:
                if previous_sequence is not None and row['seq'] != previous_sequence + 1:
                    sequence_gaps.append(dict(after=previous_sequence, before=row['seq']))
                previous_sequence = row['seq']
            render = re.search(r'\bgfxMs=(\d+),(\d+),(\d+)(?=\s|$)', line)
            if render:
                row['gfx_ms'] = [int(value) for value in render.groups()]
            samples.append(row)
            sample_lines.append(number)
            current_sample = row
        elif ' ROM_READ ' in line:
            reads.append(row)
        elif ' ROOM_READY ' in line or ' PLAY_INIT_READY ' in line:
            rooms.append(row)
        elif ' FLUSH ' in line:
            flushes.append(row)
        elif ' AUDIO ' in line:
            audio.append(row)
        elif ' USB ' in line:
            usb.append(row)
            if current_sample is not None:
                current_sample['usb'] = row
        elif '[PS2 TEXTURE CONTENT]' in line:
            required = ('tMs', 'exactHits', 'hashes', 'comparedBytes', 'hashedBytes')
            if all(key in row for key in required):
                texture_content.append(row)
            else:
                damaged_lines.append(number)
                current_sample = None
        elif '[PS2 GFX STORAGE]' in line:
            if ' ERROR ' in line:
                scratchpad_errors.append(line)
            elif all(key in row for key in ('frame', 'scratchpadBytes', 'capacity')):
                scratchpad_storage.append(row)
        elif '[PS2 PERF SAMPLING]' in line:
            sparse_profile_every = row.get('every')
        else:
            for marker, (category, fields) in profile_fields.items():
                if marker not in line:
                    continue
                if all(key in row for key in ('tMs', 'frame', *fields)):
                    calls = re.search(r'\bcalls=(\d+(?:,\d+)+)(?=\s|$)', line)
                    if calls:
                        row['calls'] = [int(value) for value in calls[1].split(',')]
                    profiles[category].append(row)
                else:
                    damaged_lines.append(number)
                    current_sample = None
                break
    # The histogram spans two bounded logger records. Expose all categories
    # only for an unambiguous pair from the same sampled frame.
    source_frames = Counter(row['frame'] for row in profiles['fence_source'])
    state_frames = Counter(row['frame'] for row in profiles['fence_state'])
    fence_states = {row['frame']: row for row in profiles['fence_state']}
    material_fences = [dict(row, **{field: fence_states[row['frame']][field] for field in fence_state_fields})
        for row in profiles['fence_source'] if source_frames[row['frame']] == 1 and state_frames[row['frame']] == 1]
    # An overwritten sector may contain plausible rows with a repeated
    # sequence. Reject every copy, rather than trusting the first; timestamps which go back
    # also cannot describe a fresh interval. Keep original rows for auditing.
    counts = Counter(row['seq'] for row in samples if 'seq' in row)
    rejected, usable, high_time = [], [], None
    for number, row in zip(sample_lines, samples):
        reasons = []
        if 'seq' in row and counts[row['seq']] > 1:
            reasons.append('duplicate_sequence')
        if high_time is not None and row['tMs'] <= high_time:
            reasons.append('nonincreasing_sample_time')
        high_time = max(high_time if high_time is not None else row['tMs'], row['tMs'])
        if reasons:
            rejected.append(dict(line=number, tMs=row['tMs'], seq=row.get('seq'), reasons=reasons))
        else:
            usable.append(row)
    fps = [row['fps10'] / 10 for row in usable]
    # Inspect the final minute separately from startup/menus/loads. Only use
    # intervals with zero read requests and no overlap with a measured flush.
    # Render times still include audio and other thread preemption.
    final_ms = usable[-1].get('tMs', 0) if usable else 0
    steady = [row for row in usable if row.get('tMs', 0) >= final_ms - 60000 and
        row['frames'] and row.get('gfx_ms') is not None and not row.get('clockFallback', 0) and
        row.get('usb', {}).get('reads') == 0 and not any(
            row['tMs'] > flush.get('tMs', 0) - flush.get('durationMs', 0) and
            row['tMs'] - row['intervalMs'] <= flush.get('tMs', 0) for flush in flushes)]
    frames = sum(row['frames'] for row in steady)
    steady_render = None
    if frames:
        steady_render = dict(intervals=len(steady), frames=frames,
            from_ms=steady[0]['tMs'] - steady[0]['intervalMs'], to_ms=steady[-1]['tMs'],
            fps_median=statistics.median(row['fps10'] / 10 for row in steady),
            translate_ms_per_frame=round(sum(row['gfx_ms'][0] for row in steady) / frames, 2),
            submit_ms_per_frame=round(sum(row['gfx_ms'][1] for row in steady) / frames, 2),
            present_ms_per_frame=round(sum(row['gfx_ms'][2] for row in steady) / frames, 2),
            limitation='Final-minute intervals only; render phases include thread preemption. No read requests or overlapping measured logger flushes. Duplicate/nonincreasing sample intervals excluded.')
    return {
        'invalid_header': not text.startswith('OOT PS2 runtime log v'),
        'nul_bytes': text.count('\0'),
        'damaged_lines': damaged_lines,
        'sample_sequence_gaps': sequence_gaps,
        'header_only': not any(line.startswith('tMs=') for line in text.splitlines()),
        'logger_started': ' LOGGER_START ' in text,
        'logger_entered': ' LOGGER_ENTER ' in text,
        'logger_errors': [line for line in text.splitlines() if 'ERROR logger' in line],
        'samples': len(samples), 'usable_samples': len(usable),
        'rejected_sample_intervals': rejected, 'fps_min': min(fps) if fps else None,
        'fps_median': statistics.median(fps) if fps else None,
        'fps_max': max(fps) if fps else None,
        'zero_frame_intervals': [row for row in samples if row['frames'] == 0],
        'clock_fallback_intervals': [row for row in samples if row.get('clockFallback', 0)],
        'slow_or_failed_reads': reads, 'room_and_scene_loads': rooms,
        'audio_zero_queue_intervals': [row for row in audio if row.get('zeroPolls', 0)],
        'last_audio': audio[-1] if audio else None,
        'usb_errors': sum(row.get('errors', 0) for row in usb),
        'texture_content_snapshots': texture_content,
        'scratchpad_storage': scratchpad_storage,
        'scratchpad_errors': scratchpad_errors,
        'sparse_profile_every': sparse_profile_every,
        'graphics_profiles': profiles['graphics'],
        'geometry_profiles': profiles['geometry'],
        'texture_content_profiles': profiles['content'],
        'profile_preemption': profiles['preemption'],
        'state_profiles': profiles['state'],
        'vertex_color_profiles': profiles['vertex_color'],
        'material_point_profiles': profiles['material_point'],
        'combiner_analysis_profiles': profiles['analysis'],
        'draw_detail_profiles': profiles['draw_detail'],
        'draw_setup_profiles': profiles['draw_setup'],
        'palette_hash_profiles': profiles['palette'],
        'material_run_profiles': profiles['runs'],
        'material_fence_profiles': material_fences,
        'progress_snapshots': [line.rstrip() for line in text.splitlines() if any(
            marker in line for marker in (' STALL_SNAPSHOT ', ' GAME_PROGRESS ', ' PLAY_PROGRESS ',
                ' GFX_WORKER ', ' THREAD ', '[PS2 GFX WAIT]'))],
        'logger_flush_max_ms': max((row.get('durationMs', 0) for row in flushes), default=None),
        'logger_flush_total_ms': sum(row.get('durationMs', 0) for row in flushes),
        'recent_no_read_render': steady_render,
        'lost_records': max((row.get('lost', 0) for row in samples), default=0),
        'file_limit_reached': 'LOG_LIMIT' in text,
        'limitations': 'FPS includes logo, menus, gameplay and loads. Queue zero polls are observations, not exact IOP underruns. USB timings include preemption and logging overhead.',
    }

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = analyze(args.log.read_text(encoding='utf-8', errors='replace'))
    result = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result, encoding='utf-8')
    print(result)

if __name__ == '__main__':
    main()
