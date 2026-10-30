"""Instruction-word patches for a module's lifted C, switchable while the game runs (gen.py --patches <file>).

Lifted code can't be byte-patched at run time, so a patch is applied when the C is generated: at a site, the generator
lifts both the original instruction and the replacement words, and the C reads
`if (rt_patch_on[<set>]) { replacement } else { original }` (one byte load and a branch where it runs; runtime/patches.c
owns rt_patch_on and the patch page).

The format, the checks and the idea are those of totk-pc-recomp's recomp/patches.py (MIT; see
docs/totk-experiment.md for the sources of the TotK tables). A patch file has `#` comments and lines
  set  <name> <env> <default>                 a set: switched by the environment variable <env> (0/1), <default> if unset
  site <set> <module> <offset> <orig> <word> [<word> ...]
<offset> is the module offset (hex); <orig> the instruction word the module has there (checked: a mismatch stops the
generator); a <word> is a hex instruction word, or `@page:x<n>`: `adrp x<n>, <patch page>` encoded for the site's
address. Replacement words must not branch except a final `bl`; the original must not branch other than `bl`.
"""
from pathlib import Path

# The patch page: a guest page the runtime commits, inside the module window (rt.h MODULES_END is 0x7080000000) and
# within the +-4 GiB an ADRP reaches from any of main's instructions. Its contents belong to the sets that use it.
PAGE_ADDR = 0x707ff00000


class PatchError(ValueError):
    pass


def adrp(pc, target, rd):
    imm = (target >> 12) - (pc >> 12)
    if not -(1 << 20) <= imm < (1 << 20):
        raise PatchError(f'{pc:#x}: patch page {target:#x} out of ADRP range')
    imm &= 0x1FFFFF
    return 0x90000000 | (imm & 3) << 29 | (imm >> 2) << 5 | rd


class Table:
    def __init__(self, sets=(), sites=None):
        self.sets = list(sets)      # [(name, env, default)]
        self.sites = sites or {}    # {module: {offset: (set index, [word | ('page', reg)], original word)}}

    def for_module(self, name, base):
        """{absolute address: (set index, [words])} with @page resolved"""
        out = {}
        for off, (k, repl, _) in self.sites.get(name, {}).items():
            a = base + off
            out[a] = (k, [adrp(a, PAGE_ADDR, w[1]) if isinstance(w, tuple) else w for w in repl])
        return out

    def check(self, name, image, base):
        """stops on a site whose original word differs from the module's (image: the module's bytes from its base)"""
        for off, (_, _, want) in self.sites.get(name, {}).items():
            if off + 4 > len(image):
                raise PatchError(f'{name}+{off:#x}: outside the module')
            have = int.from_bytes(image[off:off + 4], 'little')
            if have != want:
                raise PatchError(f'{name}+{off:#x}: the module has {have:08x}, the patch expects {want:08x}')

    def header_text(self):
        return '\n'.join([
            '// Code patch sets (tools/wordpatches.py, gen.py --patches). Generated: do not edit or commit.',
            '#pragma once', '',
            f'#define RT_PATCH_COUNT {len(self.sets)}',
            f'#define RT_PATCH_PAGE {PAGE_ADDR:#x}ull',
            '// X(index, name, env switch, default)',
            '#define RT_PATCH_SETS(X) ' + ' '.join(f'X({i}, {n}, "{e}", {d})' for i, (n, e, d) in enumerate(self.sets)),
            ''])


def load(path, module_names):
    sets, index, sites = [], {}, {}
    for no, raw in enumerate(Path(path).read_text().splitlines(), 1):
        f = raw.split('#', 1)[0].split()
        if not f:
            continue
        where = f'{path}:{no}'
        if f[0] == 'set' and len(f) == 4:
            if f[1] in index:
                raise PatchError(f'{where}: set {f[1]} defined twice')
            index[f[1]] = len(sets)
            sets.append((f[1], f[2], int(f[3])))
        elif f[0] == 'site' and len(f) >= 6:
            if f[1] not in index:
                raise PatchError(f'{where}: unknown set {f[1]}')
            if f[2] not in module_names:
                raise PatchError(f'{where}: module {f[2]} is not in the layout')
            off, orig = int(f[3], 16), int(f[4], 16)
            repl = [('page', int(w[7:])) if w.startswith('@page:x') else int(w, 16) for w in f[5:]]
            mod = sites.setdefault(f[2], {})
            if off in mod:
                raise PatchError(f'{where}: {f[2]}+{off:#x} is patched twice')
            mod[off] = (index[f[1]], repl, orig)
        else:
            raise PatchError(f'{where}: cannot parse {raw!r}')
    return Table(sets, sites)
