# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Capture repeated Tiles GPU benchmark records with source/hardware provenance.

Usage: python test/tools/benchmark-tiles.py --runs 3 --output results.jsonl
The compiler emits raw CUDA-event samples, host phase timings and correctness
checks. This runner adds the source fingerprint and never sets speed thresholds.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--runs', type=int, default=3)
parser.add_argument('--suite', choices=['default', 'rows'], default='default')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--devices', default='',
                    help='value of camel --devices (cpu, or CUDA ordinals such as 7)')
parser.add_argument('--source-revision', help='Full commit ID for an exported source archive without .git')
options = parser.parse_args()
if not 1 <= options.runs <= 30:
    parser.error('--runs must be between 1 and 30')
binary = root / 'out/latest/bin' / ('camel.exe' if os.name == 'nt' else 'camel')
case = root / 'test/cases/modules/tiles' / ('row_benchmark.cml' if options.suite == 'rows' else 'benchmark.cml')
workloads = (['softmax_128x257', 'softmax_1024x1024', 'layernorm_128x257', 'layernorm_1024x1024']
             if options.suite == 'rows' else ['elementwise', 'softmax', 'matmul', 'maximum'])
env = os.environ.copy()
env['CAMEL_HOME'] = str(root / 'out/latest')
env['PATH'] = str(binary.parent) + os.pathsep + env.get('PATH', '')
revision = options.source_revision or subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
if not re.fullmatch(r'[0-9a-f]{40}', revision):
    parser.error('--source-revision must be a full commit ID')
fingerprint = hashlib.sha256()
files = sorted(p for p in (root / 'modules/tiles').rglob('*')
               if p.is_file() and (p.suffix in {'.cpp', '.h'} or p.name == 'CMakeLists.txt'))
for path in files + [case]:
    fingerprint.update(path.relative_to(root).as_posix().encode())
    fingerprint.update(path.read_bytes().replace(b'\r\n', b'\n'))
records = []
for run in range(options.runs):
    command = [str(binary)]
    if options.devices:
        command += ['--devices', options.devices]
    command += [str(case)]
    result = subprocess.run(command, cwd=root, env=env,
                            capture_output=True, text=True, check=True, timeout=120)
    reports = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
    if len(reports) != 4:
        raise RuntimeError(f'expected four workloads, got {result.stdout!r}; {result.stderr}')
    for workload, report in zip(workloads, reports):
        report.update(workload=workload, run=run, git_revision=revision,
                      revision_source='archive_argument' if options.source_revision else 'git',
                      tiles_source_sha256=fingerprint.hexdigest())
        if report['correctness_after']['mismatches'] != 0:
            raise RuntimeError('benchmark correctness failure')
        records.append(report)
        print(f"run={run} {workload}: optimized={report['optimized']['device_median_ms']:.6f} ms "
              f"unfused={report['unfused']['device_median_ms']:.6f} ms "
              f"speedup={report['median_speedup']:.3f}")
options.output.parent.mkdir(parents=True, exist_ok=True)
options.output.write_text(''.join(json.dumps(r, sort_keys=True) + '\n' for r in records), encoding='utf-8')
