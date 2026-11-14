"""Run CPU snapshot ownership probes with bounded, windowless supervision."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--exe', required=True)
parser.add_argument('--output', required=True)
parser.add_argument('--case', action='append', required=True,
                    help='NAME=ARGUMENT; use --case=NAME=--option')
opts = parser.parse_args()
ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
exe = Path(opts.exe).resolve(strict=True)
output = Path(opts.output).resolve()
output.mkdir(parents=True, exist_ok=True)
with exe.open('rb') as stream:
    identity = hashlib.file_digest(stream, 'sha256').hexdigest()
results = []
for case in opts.case:
    name, argument = case.split('=', 1)
    if not name.replace('-', '').isalnum() or not argument.startswith('--'):
        parser.error('Invalid case')
    print(json.dumps({'starting': name, 'argument': argument}), flush=True)
    started = time.monotonic()
    with (output / (name + '.stdout.log')).open('wb') as stdout, \
            (output / (name + '.stderr.log')).open('wb') as stderr:
        try:
            process = subprocess.run([str(exe), argument], stdout=stdout, stderr=stderr,
                                     timeout=600, creationflags=subprocess.CREATE_NO_WINDOW)
            code, timed_out = process.returncode, False
        except subprocess.TimeoutExpired:
            code, timed_out = None, True
    row = {'name': name, 'argument': argument, 'exe': str(exe), 'exe_sha256': identity,
           'exit_code': code, 'timed_out': timed_out,
           'seconds': round(time.monotonic() - started, 3),
           'passed': code == 0 and not timed_out}
    results.append(row)
    (output / 'test-results.json').write_text(json.dumps(results, indent=2) + '\n',
                                              encoding='utf-8')
    print(json.dumps(row), flush=True)
raise SystemExit(0 if all(row['passed'] for row in results) else 1)
