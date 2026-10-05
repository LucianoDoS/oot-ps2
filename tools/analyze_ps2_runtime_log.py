#!/usr/bin/env python3
"""Summarize DIAG.LOG; observations do not certify a hardware root cause."""
import argparse
import json
from pathlib import Path
import re
import statistics

def analyze(text):
    samples, reads, rooms, flushes, audio = [], [], [], [], []
    for line in text.splitlines():
        row = {key: int(value) for key, value in re.findall(r'(\w+)=([0-9]+)(?=\s|$)', line)}
        if ' SAMPLE ' in line and all(key in row for key in ('intervalMs', 'frames', 'fps10')):
            samples.append(row)
        elif ' ROM_READ ' in line:
            reads.append(row)
        elif ' ROOM_READY ' in line or ' PLAY_INIT_READY ' in line:
            rooms.append(row)
        elif ' FLUSH ' in line:
            flushes.append(row)
        elif ' AUDIO ' in line:
            audio.append(row)
    fps = [row['fps10'] / 10 for row in samples]
    return {
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
        'logger_flush_max_ms': max((row.get('durationMs', 0) for row in flushes), default=None),
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
