#!/usr/bin/env python3
"""Summarize DIAG.LOG; observations do not certify a hardware root cause."""
import argparse
import json
from pathlib import Path
import re
import statistics

def analyze(text):
    samples, reads, rooms, flushes, audio, usb = [], [], [], [], [], []
    current_sample = None
    for line in text.splitlines():
        row = {key: int(value) for key, value in re.findall(r'(\w+)=([0-9]+)(?=\s|$)', line)}
        if ' SAMPLE ' in line and all(key in row for key in ('intervalMs', 'frames', 'fps10')):
            render = re.search(r'\bgfxMs=(\d+),(\d+),(\d+)(?=\s|$)', line)
            if render:
                row['gfx_ms'] = [int(value) for value in render.groups()]
            samples.append(row)
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
    fps = [row['fps10'] / 10 for row in samples]
    # Inspect the final minute separately from startup/menus/loads. Only use
    # intervals with zero read requests and no overlap with a measured flush.
    # Render times still include audio and other thread preemption.
    final_ms = samples[-1].get('tMs', 0) if samples else 0
    steady = [row for row in samples if row.get('tMs', 0) >= final_ms - 60000 and
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
            limitation='Final-minute intervals only; render phases include thread preemption. No read requests or overlapping measured logger flushes.')
    return {
        'invalid_header': not text.startswith('OOT PS2 runtime log v'),
        'nul_bytes': text.count('\0'),
        'header_only': not any(line.startswith('tMs=') for line in text.splitlines()),
        'logger_started': ' LOGGER_START ' in text,
        'logger_entered': ' LOGGER_ENTER ' in text,
        'logger_errors': [line for line in text.splitlines() if 'ERROR logger' in line],
        'samples': len(samples), 'fps_min': min(fps) if fps else None,
        'fps_median': statistics.median(fps) if fps else None,
        'fps_max': max(fps) if fps else None,
        'zero_frame_intervals': [row for row in samples if row['frames'] == 0],
        'clock_fallback_intervals': [row for row in samples if row.get('clockFallback', 0)],
        'slow_or_failed_reads': reads, 'room_and_scene_loads': rooms,
        'audio_zero_queue_intervals': [row for row in audio if row.get('zeroPolls', 0)],
        'last_audio': audio[-1] if audio else None,
        'usb_errors': sum(row.get('errors', 0) for row in usb),
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
