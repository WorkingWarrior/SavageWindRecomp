"""Compile and exercise the real TotK 2D texel helper against an independent layout oracle."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'build/totk-experiment/texture-region-tests'


def helper_body(source):
    start = source.index('static void totk_texels_2d(')
    brace = source.index('{', start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == '{':
            depth += 1
        elif source[pos] == '}':
            depth -= 1
            if depth == 0:
                return source[start:pos + 1]
    raise RuntimeError('unterminated totk_texels_2d')


def level_layout(width, height, levels, bw, bh, bpb):
    result, offset = [], 0
    for level in range(levels):
        w, h = max(width >> level, 1), max(height >> level, 1)
        wb, hb = (w + bw - 1) // bw, (h + bh - 1) // bh
        row_bytes = wb * bpb
        width_gobs = (row_bytes + 63) // 64
        size = width_gobs * 512 * ((hb + 7) // 8)
        result.append((offset, w, h, wb, hb, row_bytes, size))
        offset += size
    return result, offset


def gob_byte(xb, y, bpb, width_gobs):
    byte_x = xb * bpb
    gob_x, gob_y = byte_x >> 6, y >> 3
    block_y, gob_in_y = gob_y, 0  # This oracle deliberately fixes block height/depth at log2 0.
    off = (block_y * width_gobs + gob_x) * 512 + gob_in_y * 512
    xi, yi = byte_x & 63, y & 7
    return off + ((xi >> 5) << 8) + ((yi >> 1) << 6) + (((xi >> 4) & 1) << 5) + ((yi & 1) << 4) + (xi & 15)


def make_case(name, bpb, bw, bh, width, height, levels, level, rect, pitch=False, stride=None):
    layouts, block_size = level_layout(width, height, levels, bw, bh, bpb)
    if pitch:
        assert levels == 1 and stride and stride >= layouts[0][5]
        storage_size = stride * height
    else:
        storage_size = block_size
    initial = bytearray((i * 73 + 19) & 255 for i in range(storage_size))
    linear = []
    for _, _, _, wb, hb, row, _ in layouts:
        linear.append(bytearray((i * 29 + 41 + len(linear) * 11) & 255 for i in range(row * hb)))
    if pitch:
        off, _, _, wb, hb, row, _ = layouts[0]
        for y in range(hb):
            for x in range(row):
                initial[y * stride + x] = linear[0][y * row + x]
    else:
        for li, (off, _, _, wb, hb, row, _) in enumerate(layouts):
            width_gobs = (row + 63) // 64
            for y in range(hb):
                for xb in range(wb):
                    for byte in range(bpb):
                        initial[off + gob_byte(xb, y, bpb, width_gobs) + byte] = linear[li][y * row + xb * bpb + byte]

    x, y, rw, rh = rect
    bx, by = x // bw, y // bh
    nx, ny = (rw + bw - 1) // bw, (rh + bh - 1) // bh
    _, lw, lh, wb, hb, row, _ = layouts[level]
    assert x % bw == 0 and y % bh == 0
    assert x + rw <= lw and y + rh <= lh
    assert (rw % bw == 0 or x + rw == lw) and (rh % bh == 0 or y + rh == lh)
    packed_row = nx * bpb
    before = bytearray()
    patch = bytearray((i * 47 + 203) & 255 for i in range(packed_row * ny))
    for yy in range(ny):
        before.extend(linear[level][(by + yy) * row + bx * bpb:(by + yy) * row + bx * bpb + packed_row])
        start = (by + yy) * row + bx * bpb
        linear[level][start:start + packed_row] = patch[yy * packed_row:(yy + 1) * packed_row]
    expected = bytearray(initial)
    if pitch:
        for yy in range(hb):
            expected[yy * stride:yy * stride + row] = linear[0][yy * row:(yy + 1) * row]
    else:
        for li, (off, _, _, nwb, nhb, nrow, _) in enumerate(layouts):
            width_gobs = (nrow + 63) // 64
            for yy in range(nhb):
                for xb in range(nwb):
                    for byte in range(bpb):
                        expected[off + gob_byte(xb, yy, bpb, width_gobs) + byte] = linear[li][yy * nrow + xb * bpb + byte]
    # Expected packed read after the write.
    after = bytearray()
    for yy in range(ny):
        start = (by + yy) * row + bx * bpb
        after.extend(linear[level][start:start + packed_row])
    return dict(name=name, bpb=bpb, bw=bw, bh=bh, width=width, height=height, levels=levels, level=level,
                rect=rect, pitch=pitch, stride=stride or 0, initial=initial, patch=patch, before=before,
                expected=expected, after=after, levels_mask=0x80000000 | (1 << level))


def c_bytes(data):
    return ','.join(f'0x{x:02x}' for x in data) or '0'


def c_case(case, index):
    n = case['name']
    arrays = []
    for key in ('initial', 'patch', 'before', 'expected', 'after'):
        val = case[key]
        arrays.append(f'static const uint8_t {n}_{key}[{len(val)}] = {{{c_bytes(val)}}};')
    x, y, rw, rh = case['rect']
    return '\n'.join(arrays) + f'''
    {{
        NvnTexture t = {{0}};
        NvnTextureView v = {{ .setmask = 1, .base_level = {case['level']} }};
        int32_t region[6] = {{{x}, {y}, 0, {rw}, {rh}, 1}};
        uint8_t storage[{len(case['initial'])}], out[{len(case['before'])}];
        const FormatInfo fmt = {{{case['bpb']}, {case['bw']}, {case['bh']}, {4 if case['bw'] > 1 or case['bh'] > 1 else 0}, "{n}"}};
        memcpy(storage, {n}_initial, sizeof storage);
        t.layout = (TexLayout){{{case['width']}, {case['height']}, 1, 1, {case['levels']}, &fmt, 0, 0}};
        t.target = 1; t.width = {case['width']}; t.height = {case['height']}; t.levels = {case['levels']};
        t.flags = {0x10 if case['pitch'] else 0}; t.stride = {case['stride']}ull;
        t.addr = (uint64_t)(uintptr_t)storage; t.cpu_levels = 0x80000000u;
        memset(out, 0xa5, sizeof out);
        totk_texels_2d(&t, &v, region, out, false);
        assert(memcmp(out, {n}_before, sizeof out) == 0);
        totk_texels_2d(&t, &v, region, (uint8_t *){n}_patch, true);
        assert(memcmp(storage, {n}_expected, sizeof storage) == 0);
        assert(t.cpu_levels == 0x{case['levels_mask']:08x}u);
        memset(out, 0x5a, sizeof out);
        totk_texels_2d(&t, &v, region, out, false);
        assert(memcmp(out, {n}_after, sizeof out) == 0);
    }}
'''


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source = (ROOT / 'runtime/nvn/objects.c').read_text()
    helper = helper_body(source)
    cases = [
        make_case('r8_gob_edge', 1, 1, 1, 70, 17, 2, 0, (31, 6, 9, 5)),
        make_case('r8_mip1', 1, 1, 1, 70, 17, 3, 1, (8, 2, 11, 4)),
        make_case('rgba8_partial', 4, 1, 1, 35, 13, 1, 0, (4, 3, 12, 5)),
        make_case('rgba16_partial', 8, 1, 1, 20, 9, 1, 0, (3, 2, 10, 5)),
        make_case('bc1_blocks', 8, 4, 4, 36, 24, 2, 0, (4, 4, 20, 12)),
        make_case('bc1_edge', 8, 4, 4, 36, 24, 2, 1, (8, 4, 4, 4)),
        make_case('pitch_padding', 4, 1, 1, 19, 7, 1, 0, (3, 1, 7, 4), pitch=True, stride=19 * 4 + 20),
    ]
    preamble = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime/nvn/nvn.h"
#include "runtime/nvn/renderer.h"
#include "runtime/kernel.h"
#undef LOG
#undef FATAL
#define LOG(...) ((void)0)
#define FATAL(...) do { fprintf(stderr, __VA_ARGS__); abort(); } while (0)
static unsigned locks, unlocks, watches, written;
static uint64_t watch_addr, watch_size, written_addr, written_size;
void gpu_lock(void) { locks++; }
void gpu_unlock(void) { unlocks++; }
bool renderer_is_gpu_owned(NvnTexture *t) { (void)t; return false; }
void kernel_watch_write(const char *what, uint64_t addr, uint64_t size) { assert(!strcmp(what, "WriteTexels")); watches++; watch_addr=addr; watch_size=size; }
void backend_pool_written(uint64_t addr, uint64_t size) { written++; written_addr=addr; written_size=size; }
'''
    check_hooks = r'''
    assert(locks == unlocks && locks == 3u * CASE_COUNT);
    assert(watches == CASE_COUNT && written == CASE_COUNT);
    assert(watch_addr == written_addr && watch_size == written_size);
    puts("ok: extracted TotK texel helper matches independent rectangle/memory oracle");
    return 0;
}
'''.replace('CASE_COUNT', str(len(cases)))
    harness = (preamble + helper + '\n#include "runtime/nvn/tegra.c"\nint main(void) {\n' +
               '\n'.join(c_case(c, i) for i, c in enumerate(cases)) + check_hooks)
    c_path = OUT / 'texture_region_harness.c'
    exe_path = OUT / 'texture_region_harness'
    c_path.write_text(harness)
    cmd = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined',
           '-I.', '-Iruntime/nvn', '-Iruntime', str(c_path), '-o', str(exe_path)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    subprocess.run([str(exe_path)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
