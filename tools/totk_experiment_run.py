"""One isolated TotK bring-up, stopping at the first logged missing behavior.

The pty keeps the runtime's normal output on the terminal instead of its fatal
dialog path. This runner does not retry, supply stub results, or patch game data.
Its timeout is an observation bound, not a successful boot criterion.
"""
import argparse
import errno
import json
import os
from pathlib import Path
import pty
import re
import selectors
import signal
import subprocess
import time


MISSING = re.compile(r'\b(?:UNIMPLEMENTED|STUB|unhandled|unknown cmd|unknown command|'
                     r'no HLE implementation|unsupported SVC)\b|FATAL:', re.I)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', required=True, type=Path)
    ap.add_argument('--library', required=True, type=Path)
    ap.add_argument('--data', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--seconds', type=float, default=60)
    args = ap.parse_args()
    repo = Path(__file__).resolve().parent.parent
    out = args.out.resolve()
    if not out.is_relative_to(repo / 'build' / 'totk-experiment'):
        ap.error('--out must be inside this checkout/build/totk-experiment')
    if out.exists():
        ap.error('--out must be new: preserve previous evidence')
    command = [str(args.exe.resolve()), '--game', 'totk', '--play', str(args.data.resolve())]
    env = {k: v for k, v in os.environ.items() if not k.startswith('SAVAGEWIND_')}
    env.update(SAVAGEWIND_DATA_ROOT=str(out / 'state'), SAVAGEWIND_SAVE_DIR=str(out / 'state' / 'save'),
               SAVAGEWIND_GAME_LIB=str(args.library.resolve()), SAVAGEWIND_HIDDEN='1',
               SAVAGEWIND_NO_USER_INPUT='1')
    (out / 'state' / 'build').mkdir(parents=True)
    master, slave = pty.openpty()
    start = time.monotonic()
    proc = subprocess.Popen(command, env=env, stdin=subprocess.DEVNULL, stdout=slave,
                            stderr=slave, start_new_session=True)
    os.close(slave)
    sel = selectors.DefaultSelector()
    sel.register(master, selectors.EVENT_READ)
    pending = ''
    reason, first = 'exit', ''
    try:
        with (out / 'run.log').open('wb') as log:
            while True:
                if time.monotonic() - start >= args.seconds:
                    reason = 'observation_timeout'
                    break
                if not sel.select(0.1):
                    if proc.poll() is not None:
                        break
                    continue
                try:
                    data = os.read(master, 65536)
                except OSError as e:
                    if e.errno != errno.EIO:
                        raise
                    break
                if not data:
                    break
                log.write(data)
                log.flush()
                pending += data.decode('utf-8', errors='replace')
                lines = pending.split('\n')
                pending = lines.pop()
                for line in lines:
                    if MISSING.search(line):
                        reason, first = 'first_missing_behavior', line.strip()
                        break
                if first:
                    break
    finally:
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
        sel.close()
        os.close(master)
    result = dict(reason=reason, first=first, child_exit=proc.returncode,
                  seconds=round(time.monotonic() - start, 3), command=command)
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 3 if first else 4 if reason == 'observation_timeout' else proc.returncode


if __name__ == '__main__':
    raise SystemExit(main())
