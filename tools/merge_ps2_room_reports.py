#!/usr/bin/env python3
"""Merge disjoint room sweeps while preserving failures and capture provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('reports', type=Path, nargs='+')
a = p.parse_args()
a.output.parent.mkdir(parents=True, exist_ok=True)
reports = [json.loads(path.read_text()) for path in a.reports]
assert len({r['image_sha256'] for r in reports}) == 1
assert len({r['total_referenced_rooms'] for r in reports}) == 1
rows, failures, sources = {}, set(), []
for path, report in zip(a.reports, reports, strict=True):
    failures.update(report.get('failed_cases', []))
    sources.append(dict(report=str(path.resolve()), status=report['status'],
                        runtime_failures=report.get('runtime_failures', [])))
    for original in report['cases']:
        row = dict(original)
        source = Path(row['image'])
        assert source.parent.resolve() == path.parent.resolve()
        assert hashlib.sha256(source.read_bytes()).hexdigest() == row['image_sha256']
        if row['case'] in rows:
            previous = rows[row['case']]
            assert (previous['scene'], previous['room'], previous['age'], previous['segment']) == (row['scene'], row['room'], row['age'], row['segment'])
        destination = a.output.parent / f'room-{row["case"]:04}.ppm'
        shutil.copyfile(source, destination)
        row['image'] = str(destination.resolve())
        row['source_report'] = str(path.resolve())
        rows[row['case']] = row
total = reports[0]['total_referenced_rooms']
missing = sorted(set(range(total)) - rows.keys())
unresolved = sorted(failures - rows.keys())
result = dict(status='FAIL' if unresolved else ('INCOMPLETE' if missing else 'PASS'),
              rooms=len(rows), scenes=len({r['scene'] for r in rows.values()}),
              total_referenced_rooms=total, missing_cases=missing, failed_cases=unresolved,
              sources=sources, image_sha256=reports[0]['image_sha256'], scope=reports[0]['scope'],
              full_progression_validated=False, puzzles_and_boss_victories_validated=False,
              visual_correctness_validated=False, real_ps2_validated=False,
              cases=[rows[key] for key in sorted(rows)])
a.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(f'{result["status"]}: {len(rows)}/{total} completed rooms; unresolved={unresolved}; missing={missing}')
