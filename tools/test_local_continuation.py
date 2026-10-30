"""Exercise TotK-only ADR/BR continuations through the real scanner and emitted C."""
from collections import Counter
from pathlib import Path
import struct
import subprocess
import tempfile

import unicorn as uc
from unicorn import arm64_const as A

from gen import Module, fname
from lifter import Lifter
from test_layout import adr, b_, br, movz_x0


BASE = 0x7000000000
# The ADR is beyond the straight-line lookback and separated by a branch.
WORDS = [adr(19, BASE, BASE + 112), b_(BASE + 4, BASE + 108)]
WORDS += [0xD503201F] * 25
WORDS += [br(19), movz_x0(42), 0xD65F03C0]
END = BASE + 4 * len(WORDS)
BR = BASE + 108
TARGET = BASE + 112


def module(enabled):
    m = object.__new__(Module)
    m.base = m.lo = BASE
    m.hi = END
    m.words = WORDS
    m.lifter = Lifter(BASE, 'c')
    m.cache, m.stats, m.patches, m.setjmps = {}, Counter(), {}, set()
    m.cs = None
    m.fdes, m.fde_starts = [(BASE, END)], [BASE]
    m.local_adr_continuations = enabled
    # This fixture has no table; exercise ADR handling independently of emulation.
    m.jump_table = lambda *args: None
    return m


def main():
    off = module(False)
    assert off.scan(BASE, END)[2] == {}, 'default BOTW scanner must retain its old path'
    m = module(True)
    labels, ext, tables = m.scan(BASE, END)
    assert tables == {BR: [TARGET]} and TARGET in labels and not ext
    # A target in a different register or outside the function is not a local case.
    for rd, target in ((20, TARGET), (19, END + 4)):
        n = module(True)
        n.words = [adr(rd, BASE, target)] + WORDS[1:]
        assert not n.scan(BASE, END)[2]
    n = module(True)
    n.words = ([0xD65F03C0] + [0xD503201F] * 4 +
               [adr(19, BASE + 20, BASE), b_(BASE + 24, BASE + 128)] +
               [0xD503201F] * 25 + [br(19)])
    n.hi = BASE + 4 * len(n.words)
    n.fdes = [(BASE, n.hi)]
    assert n.scan(BASE, n.hi)[2] == {BASE + 128: [BASE]}, 'signed backward ADR'
    n = module(True)
    n.words = WORDS.copy()
    n.words[-4] = adr(19, BR - 4, TARGET)
    assert n.scan(BASE, END)[2] == {BR: 'all'}, 'existing computed-target path keeps priority'

    mu = uc.Uc(uc.UC_ARCH_ARM64, uc.UC_MODE_ARM)
    mu.mem_map(BASE, 0x1000)
    mu.mem_write(BASE, struct.pack('<' + 'I' * len(WORDS), *WORDS))
    mu.reg_write(A.UC_ARM64_REG_X30, END)
    mu.emu_start(BASE, END, count=100)
    expected = mu.reg_read(A.UC_ARM64_REG_X0)
    assert expected == 42
    emitted = '\n'.join(m.emit_function(BASE, END, labels, tables, {}))
    source = '''#include <stdint.h>
typedef struct { uint64_t x[31], sp, pc; } Cpu;
#define FN_ENTER(x) ((void)0)
static int dispatched;
static void rt_dispatch(Cpu *c) { (void)c; dispatched++; }
''' + emitted + f'''
int main(void) {{
    Cpu c = {{0}};
    {fname(BASE)}(&c);
    if (c.x[0] != {expected} || dispatched) return 1;
    c.pc = {BR}ull; c.x[19] = 0xdead;
    {fname(BASE)}_r(&c);
    return dispatched != 1 || c.pc != 0xdead;
}}
'''
    with tempfile.TemporaryDirectory(prefix='adr-continuation-') as tmp:
        c, exe = Path(tmp) / 'test.c', Path(tmp) / 'test'
        c.write_text(source)
        subprocess.run(['clang', '-O2', '-Werror', str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print('ok: distant ADR/BR matches Unicorn; default, register, range and dispatch-fallback controls pass')


if __name__ == '__main__':
    main()
