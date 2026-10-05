"""Bounded, noninteractive verification; logs and exact command retained."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[4]
evidence = Path(__file__).resolve().parent
name, *command = sys.argv[1:]
env = dict(os.environ)
started = time.monotonic()
with (evidence / (name + '.stdout.log')).open('wb') as out, (evidence / (name + '.stderr.log')).open('wb') as err:
    try:
        result = subprocess.run(command, cwd=root, env=env, stdout=out, stderr=err, timeout=900)
        code = result.returncode
    except subprocess.TimeoutExpired:
        code = 'timeout-900s'
record = {'command': command, 'cwd': str(root), 'exit': code, 'seconds': time.monotonic() - started}
(evidence / (name + '.result.json')).write_text(json.dumps(record, indent=2), encoding='utf-8')
print(json.dumps(record))
sys.exit(0 if code == 0 else 1)
