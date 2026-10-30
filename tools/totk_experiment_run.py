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


MISSING = re.compile(r'\b(?:UNIMPLEMENTED|STUB|unhandled|unknown (?:cmd|command|id)|'
                     r'no HLE implementation|unsupported SVC|not implemented)\b|FATAL:', re.I)
BLOCKING = re.compile(r'FATAL:|\[nvn\].*(?:not implemented|UNIMPLEMENTED)|'
                      r'\[hle\].*UNIMPLEMENTED nvn', re.I)
IPC_ERROR = re.compile(r'\[experiment\] IPC error (\S+) cmd (\d+) result (0x[0-9a-f]+)', re.I)


def blocking_ipc_error(line):
    m = IPC_ERROR.search(line)
    if not m:
        return False
    name, command, result = m[1], int(m[2]), int(m[3], 16)
    # Normal negative poll results, not missing service implementations. Preserve
    # them in the transcript/result; never rewrite the result seen by the guest.
    return (name, command, result) not in {
        ('ICommonStateGetter', 1, 0x680),  # AM_NO_MESSAGE, ReceiveMessage
        ('IApplicationFunctions', 1, 0x480),  # AM_NO_DATA, PopLaunchParameter
        # Host save/SD namespaces may lack queried paths on first boot (observed:
        # storage/CacheStorageKey.dat). Keep the actual PathNotFound response and
        # log; let guest code decide whether an absent path is required.
        ('IFileSystem', 7, 0x202),  # GetEntryType
        ('IFileSystem', 8, 0x202),
        ('IFileSystem', 9, 0x202),  # OpenDirectory
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', required=True, type=Path)
    ap.add_argument('--library', required=True, type=Path)
    ap.add_argument('--data', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--seconds', type=float, default=60, help='observation bound; 0 keeps the process running until a stop/error')
    ap.add_argument('--game', choices=('totk', 'botw'), default='totk')
    ap.add_argument('--sdk-entry-probe', action='store_true', help='enable the opt-in VammManager entry diagnostic')
    ap.add_argument('--trace-svc', action='store_true', help='enable the existing kernel SVC input/output trace')
    ap.add_argument('--policy', choices=('first-missing', 'blocking', 'observe'), default='first-missing',
                    help='observe keeps nonfatal NVN stub logs for bounded diagnostic comparisons')
    ap.add_argument('--env', action='append', default=[], metavar='SAVAGEWIND_X=V', help='extra SAVAGEWIND_* debug switch (repeatable)')
    ap.add_argument('--shots', default='', help='comma-separated presented frame numbers for visual observation')
    args = ap.parse_args()
    repo = Path(__file__).resolve().parent.parent
    out = args.out.resolve()
    if not out.is_relative_to(repo / 'build' / 'totk-experiment'):
        ap.error('--out must be inside this checkout/build/totk-experiment')
    if out.exists():
        ap.error('--out must be new: preserve previous evidence')
    command = [str(args.exe.resolve()), '--game', args.game, '--play', str(args.data.resolve())]
    env = {k: v for k, v in os.environ.items() if not k.startswith('SAVAGEWIND_')}
    env.update(SAVAGEWIND_DATA_ROOT=str(out / 'state'), SAVAGEWIND_SAVE_DIR=str(out / 'state' / 'save'),
               SAVAGEWIND_GAME_LIB=str(args.library.resolve()), SAVAGEWIND_HIDDEN='1',
               SAVAGEWIND_NO_USER_INPUT='1')
    for item in args.env:
        key, _, value = item.partition('=')
        if not key.startswith('SAVAGEWIND_'):
            ap.error('--env keys must start with SAVAGEWIND_')
        env[key] = value
    if args.sdk_entry_probe:
        env['SAVAGEWIND_SDK_ENTRY_PROBE'] = '1'
    if args.trace_svc:
        env['SAVAGEWIND_TRACE_SVC'] = '1'
    if args.shots:
        if not re.fullmatch(r'\d+(,\d+)*', args.shots):
            ap.error('--shots must contain frame numbers only')
        env['SAVAGEWIND_WINDOW_SHOT_AT'] = args.shots
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
    fallbacks, ipc_errors = [], []
    try:
        with (out / 'run.log').open('wb') as log:
            while True:
                if args.seconds > 0 and time.monotonic() - start >= args.seconds:
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
                    if IPC_ERROR.search(line):
                        ipc_errors.append(line.strip())
                    hard = (BLOCKING.search(line) if args.policy == 'blocking' else 'FATAL:' in line)
                    hard = hard or blocking_ipc_error(line)
                    if args.policy != 'first-missing' and hard:
                        reason, first = 'first_blocking_behavior', line.strip()
                        break
                    if MISSING.search(line):
                        if args.policy == 'first-missing':
                            reason, first = 'first_missing_behavior', line.strip()
                            break
                        fallbacks.append(line.strip())
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
    if reason == 'exit' and proc.returncode:
        reason, first = 'process_failure', f'child exited with {proc.returncode}'
    result = dict(reason=reason, first=first, child_exit=proc.returncode, policy=args.policy,
                  sdk_entry_probe=args.sdk_entry_probe,
                  fallbacks=fallbacks, ipc_errors=ipc_errors,
                  seconds=round(time.monotonic() - start, 3), command=command)
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 3 if first else 4 if reason == 'observation_timeout' else proc.returncode


if __name__ == '__main__':
    raise SystemExit(main())
