"""Compile the kernel's actual interval bookkeeping; test against a page oracle."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    source = (ROOT / 'runtime/kernel.c').read_text()
    section = source[source.index('typedef struct { uint64_t addr, size; uint32_t type, perm; } Region;'):
                     source.index('// ------------------------------------------------------------------ threads')]
    preamble = '''#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#define TLS_BASE 0
#define FATAL(...) abort()
static bool botw;
static bool game_is_botw(void) { return botw; }
static uint64_t committed_addr, committed_size;
static void plat_commit(uint64_t a, uint64_t n) { committed_addr=a; committed_size=n; }
'''
    checks = '''
int main(void) {
    const uint64_t base=0x7100000000ull, page=4096;
    uint64_t info[5];
    botw=true;
    region_add(base,page,7,3); region_add(base+page,page,7,3);
    assert(nregions==2); region_remove(base); assert(nregions==1 && regions[0].addr==base+page);
    botw=false; nregions=0;
    region_add(base,page,7,3); region_add(base+2*page,page,7,3); region_add(base+page,page,7,3);
    assert(nregions==1 && regions[0].size==3*page);
    assert(committed_addr==base+page && committed_size==page);
    physical_region_remove(base+page,page);
    assert(nregions==2 && regions[0].size==page && regions[1].addr==base+2*page);
    query_memory(base+page,(uint64_t)info); assert(info[0]==base+page && info[1]==page && !info[2]);
    region_add(base+page,page,7,3);
    physical_region_remove(base,page); physical_region_remove(base+2*page,page);
    assert(nregions==1 && regions[0].addr==base+page && regions[0].size==page);
    physical_region_remove(base,3*page); assert(nregions==0);
    region_add(base,page,7,3); region_add(base+page,page,7,1); region_add(base+2*page,page,0xB,3);
    assert(nregions==3); physical_region_remove(base,3*page);
    assert(nregions==1 && regions[0].type==0xB);
    nregions=0;
    for(int i=0;i<3000;i++) region_add(base+i*page,page,7,3);
    assert(nregions==1 && regions[0].size==3000*page);
    nregions=0;
    unsigned char mapped[256]={0}; unsigned rng=1234567;
    for(int step=0;step<10000;step++) {
        rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
        unsigned p=rng%256;
        if(mapped[p]) physical_region_remove(base+p*page,page);
        else region_add(base+p*page,page,7,3);
        mapped[p]^=1;
        int runs=0;
        for(int j=0;j<256;j++) {
            if(mapped[j] && (!j || !mapped[j-1])) runs++;
            query_memory(base+j*page,(uint64_t)info);
            assert((info[2]==7)==!!mapped[j]);
            assert(info[0]<=base+j*page && base+j*page<info[0]+info[1]);
            if(mapped[j]) {
                int left=j,right=j+1;
                while(left && mapped[left-1])left--;
                while(right<256 && mapped[right])right++;
                assert(info[0]==base+left*page && info[1]==(right-left)*page && info[3]==3);
            }
        }
        assert(nregions==runs);
    }
    nregions=0;
'''
    if len(sys.argv) > 1:
        log = Path(sys.argv[1]).read_text(errors='replace')
        ops = [(int(op, 16), int(a, 16), int(n, 16)) for op, a, n in
               re.findall(r'\[svc\] (2[cd])\(([0-9a-f]+), ([0-9a-f]+),', log)]
        assert ops, 'trace has no physical-memory calls'
        checks += 'uint64_t ops[][3]={' + ','.join('{%d,%dull,%dull}' % op for op in ops) + '};\n'
        checks += '''int high=0;
for(unsigned i=0;i<sizeof ops/sizeof *ops;i++) {
 if(ops[i][0]==0x2c)region_add(ops[i][1],ops[i][2],7,3);
 else physical_region_remove(ops[i][1],ops[i][2]);
 if(nregions>high)high=nregions;
}
printf("trace replay: %zu calls, %d final intervals, high %d\\n",sizeof ops/sizeof *ops,nregions,high);
'''
    checks += 'puts("ok: BOTW control, bridge/trim/split, 3000 adjacent mappings and 10000 page-oracle operations");}\n'
    with tempfile.TemporaryDirectory(prefix='physical-regions-') as tmp:
        c, exe = Path(tmp) / 'test.c', Path(tmp) / 'test'
        c.write_text(preamble + section + checks)
        subprocess.run(['clang', '-O2', '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
