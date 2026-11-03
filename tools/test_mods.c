// Mod folder layouts (runtime/mods_scan.h) and the ResourceSizeTable merge (runtime/services/rstb_botw.h), on folders
// and tables made here. make test-mods
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../runtime/mods_scan.h"
#include "../runtime/services/rstb_botw.h"
#include "../runtime/services/rstb_totk.h"
#include "../runtime/exefs_patch.h"
#include "../runtime/cheat_vm.h"

#define GAME "01007EF00011E000"
#define AOC "01007EF00011F001"

static char base[512];

static void put(const char *rel) {  // a file at base/rel, its folders made
    char p[1400], cmd[1600];
    snprintf(p, sizeof p, "%s/%s", base, rel);
    snprintf(cmd, sizeof cmd, "mkdir -p \"$(dirname '%s')\" && echo x > '%s'", p, p);
    assert(!system(cmd));
}

static ModFound scan(const char *mod) {
    char p[1100];
    snprintf(p, sizeof p, "%s/%s", base, mod);
    ModFound f;
    mods_scan_one(p, GAME, AOC, &f);
    return f;
}

static uint8_t mem[0x10000];  // the cheats' memory: 0x1000..0x11000
static bool mem_rd(uint64_t a, void *b, size_t n) {
    if (a < 0x1000 || a + n > 0x1000 + sizeof mem) return false;
    memcpy(b, mem + a - 0x1000, n);
    return true;
}
static bool mem_wr(uint64_t a, const void *b, size_t n) {
    if (a < 0x1000 || a + n > 0x1000 + sizeof mem) return false;
    memcpy(mem + a - 0x1000, b, n);
    return true;
}
static uint32_t mem32(uint32_t off) { uint32_t v; memcpy(&v, mem + off, 4); return v; }
static int run_codes(CheatVm *vm, CheatEnv *e, const char *text) {
    static Cheat c[4];
    char file[2048];
    snprintf(file, sizeof file, "[t]\n%s\n", text);
    assert(cheat_parse(file, strlen(file), c, 4) == 1);
    return cheat_vm_run(vm, c[0].op, c[0].n, e);
}

static bool ends(const char *s, const char *tail) {
    size_t n = strlen(s), t = strlen(tail);
    return n >= t && !strcmp(s + n - t, tail);
}

typedef struct { int n; int64_t off[8]; uint8_t bytes[8][8]; uint32_t len[8]; } Rec;
static void rec(void *ctx, int64_t off, const uint8_t *b, uint32_t n) {
    Rec *r = ctx;
    r->off[r->n] = off, r->len[r->n] = n;
    memcpy(r->bytes[r->n], b, n < 8 ? n : 8);
    r->n++;
}

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t get32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

int main(void) {
    snprintf(base, sizeof base, "/tmp/sw-test-mods-%d", (int)getpid());
    put("emu/romfs/Actor/Pack/A.sbactorpack");
    put("sd/atmosphere/contents/" GAME "/romfs/Pack/Bootup.pack");
    put("bcml/" GAME "/romfs/Actor/x.bin");
    put("bcml/" AOC "/romfs/Pack/AocMainField.pack");
    put("bare/Actor/y.bin");
    put("bare/readme.txt");
    put("wrapped/Inner Name/romfs/UI/z.bin");
    put("wiiu/romfs/Map/m.bin");
    put("wiiu/romfs/aoc/0010/Map/n.bin");
    put("other/contents/0100F2C0115B6000/romfs/Pack/t.bin");
    put("Romfs-Case/ROMFS/Pack/c.bin");
    put("empty/notes.txt");
    put("code/exefs/main.ips");
    put("atmo/atmosphere/exefs_patches/fps/abc.ips");
    put("atmo/atmosphere/exefs_patches/res/def.ips");
    put("cheat/cheats/0123456789ABCDEF.txt");
    put("yuzu/load/" GAME "/Rain/romfs/Pack/r.bin");
    put("yuzu/load/" GAME "/Bow/romfs/Pack/b.bin");
    put("yuzu/load/" GAME "/_old/romfs/Pack/o.bin");
    put("yuzu/load/" GAME "/Empty/readme.txt");
    put("ryu/mods/contents/" GAME "/Fps/exefs/x.pchtxt");
    char cmd[600];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/emptyromfs/romfs/Pack'", base);
    assert(!system(cmd));

    ModFound f = scan("emu");
    assert(ends(f.romfs, "/emu/romfs") && !strcmp(f.layout, "romfs") && !f.aoc[0]);
    f = scan("sd");
    assert(ends(f.romfs, "/contents/" GAME "/romfs") && !strcmp(f.layout, "atmosphere/contents/" GAME "/romfs"));
    f = scan("bcml");
    assert(ends(f.romfs, "/" GAME "/romfs") && ends(f.aoc, "/" AOC "/romfs") && !f.other_title[0]);
    f = scan("bare");
    assert(ends(f.romfs, "/bare") && !strcmp(f.layout, "folder"));
    f = scan("wrapped");
    assert(ends(f.romfs, "/Inner Name/romfs"));
    f = scan("wiiu");
    assert(ends(f.romfs, "/wiiu/romfs") && ends(f.aoc, "/romfs/aoc/0010"));
    f = scan("other");
    assert(!f.romfs[0] && !strcmp(f.other_title, "0100F2C0115B6000"));
    f = scan("Romfs-Case");
    assert(ends(f.romfs, "/ROMFS"));
    f = scan("empty");
    assert(!f.romfs[0] && !f.code);
    f = scan("code");
    assert(!f.romfs[0] && f.code && f.nexefs == 1 && ends(f.exefs[0], "/code/exefs") && !f.cheats[0]);
    f = scan("atmo");
    assert(f.nexefs == 2 && !f.romfs[0]);
    f = scan("cheat");
    assert(ends(f.cheats, "/cheat/cheats") && !f.nexefs && f.code);
    f = scan("emptyromfs");
    assert(!f.romfs[0]);
    char coll[8][1100], p[1100];
    snprintf(p, sizeof p, "%s/yuzu", base);
    mods_scan_one(p, GAME, AOC, &f);
    assert(!f.romfs[0] && !f.code);
    assert(mods_scan_collection(p, GAME, AOC, coll, 8) == 2 && ends(coll[0], "/Bow") && ends(coll[1], "/Rain"));
    snprintf(p, sizeof p, "%s/ryu", base);
    assert(mods_scan_collection(p, GAME, AOC, coll, 8) == 1 && ends(coll[0], "/Fps"));
    snprintf(p, sizeof p, "%s/emu", base);
    assert(mods_scan_collection(p, GAME, AOC, coll, 8) == 0);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", base);
    assert(!system(cmd));

    // order: saved names first (those still there), then new ones by name
    const char *names[] = {"d", "b", "a", "c"}, *saved[] = {"c", "gone", "d", "c"};
    int out[4];
    assert(mods_scan_order(names, 4, saved, 4, out) == 4);
    assert(out[0] == 3 && out[1] == 0 && out[2] == 2 && out[3] == 1);  // c d | a b

    // RSTB merge: crc entries {1:10, 5:50} + {1:12, 3:30, 5:40}, names {"n":7} + {"n":9, "m":4}
    uint8_t a[12 + 16 + 132] = "RSTB", b[12 + 24 + 264] = "RSTB";
    put32(a + 4, 2), put32(a + 8, 1);
    put32(a + 12, 1), put32(a + 16, 10), put32(a + 20, 5), put32(a + 24, 50);
    strcpy((char *)a + 28, "n"), put32(a + 28 + 128, 7);
    put32(b + 4, 3), put32(b + 8, 2);
    put32(b + 12, 1), put32(b + 16, 12), put32(b + 20, 3), put32(b + 24, 30), put32(b + 28, 5), put32(b + 32, 40);
    strcpy((char *)b + 36, "n"), put32(b + 36 + 128, 9);
    strcpy((char *)b + 36 + 132, "m"), put32(b + 36 + 132 + 128, 4);
    size_t n = 0;
    int raised = 0, added = 0;
    uint8_t *m = rstb_merge(a, sizeof a, b, sizeof b, &n, &raised, &added);
    assert(m && n == 12 + 3 * 8 + 2 * 132 && get32(m + 4) == 3 && get32(m + 8) == 2);
    assert(get32(m + 12) == 1 && get32(m + 16) == 12 && get32(m + 20) == 3 && get32(m + 24) == 30);
    assert(get32(m + 28) == 5 && get32(m + 32) == 50);
    assert(!strcmp((char *)m + 36, "n") && get32(m + 36 + 128) == 9 && !strcmp((char *)m + 168, "m"));
    assert(raised == 2 && added == 2);
    free(m);
    assert(!rstb_merge(a, 8, b, sizeof b, &n, &raised, &added));
    // TotK's table: parse, merge (larger size wins), raise, serialize and back
    uint8_t ta[22 + 2 * 8 + 1 * 24] = "RESTBL";
    put32(ta + 6, 1), put32(ta + 10, 20), put32(ta + 14, 2), put32(ta + 18, 1);
    put32(ta + 22, restbl_crc32("Pack/A.pack")), put32(ta + 26, 100);
    put32(ta + 30, restbl_crc32("Pack/B.pack")), put32(ta + 34, 200);
    strcpy((char *)ta + 38, "Pack/C.pack"), put32(ta + 38 + 20, 300);
    Restbl t1, t2;
    assert(restbl_parse(ta, sizeof ta, &t1) && restbl_parse(ta, sizeof ta, &t2));
    *restbl_get(&t2, "Pack/A.pack") = 150;
    *restbl_get(&t2, "Pack/C.pack") = 250;
    assert(restbl_raise(&t2, "Pack/New.pack", 4096) && !restbl_raise(&t2, "Pack/B.pack", 10));
    raised = added = 0;
    assert(restbl_merge(&t1, &t2, &raised, &added) && raised == 1 && added == 1);
    assert(*restbl_get(&t1, "Pack/A.pack") == 150 && *restbl_get(&t1, "Pack/C.pack") == 300 &&
           *restbl_get(&t1, "Pack/New.pack") == 4096);
    size_t sn;
    uint8_t *ser = restbl_serialize(&t1, &sn);
    Restbl t3;
    assert(sn == 22 + 3 * 8 + 24 && restbl_parse(ser, sn, &t3) && *restbl_get(&t3, "Pack/New.pack") == 4096);
    for (size_t i = 1; i < t3.ncrc; i++) assert(get32(ser + 22 + (i - 1) * 8) < get32(ser + 22 + i * 8));  // (sorted)
    free(ser), restbl_free(&t1), restbl_free(&t2), restbl_free(&t3);
    char key[64];
    restbl_key("Pack/Actor/X.pack.zs", key, sizeof key);
    assert(!strcmp(key, "Pack/Actor/X.pack"));
    assert(restbl_estimate("Model/X.bfres", 1000) == 12288 && restbl_estimate("Sound/X.bwav", 1000) == 0);
    assert(restbl_grow("Model/X.bfres", 5000, 1000, 2000, false) == 6528 && restbl_grow("X.byml", 5000, 2000, 1000, false) == 5000);
    assert(restbl_grow("X.mc", 1000, 100, 200, true) == 2000);
    // ExeFS patches: IPS (header-relative offsets, RLE), IPS32, .pchtxt (blocks, offset_shift, strings), build ids
    const uint8_t ips[] = {'P', 'A', 'T', 'C', 'H', 0x00, 0x01, 0x10, 0x00, 0x02, 0xAA, 0xBB,
                           0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x7F, 'E', 'O', 'F'};
    Rec r = {0};
    assert(exefs_ips(ips, sizeof ips, rec, &r) == 2 && r.n == 2);
    assert(r.off[0] == 0x10 && r.len[0] == 2 && r.bytes[0][1] == 0xBB);
    assert(r.off[1] == 0x100 && r.len[1] == 3 && r.bytes[1][2] == 0x7F);
    const uint8_t ips32[] = {'I', 'P', 'S', '3', '2', 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, 0x42, 'E', 'E', 'O', 'F'};
    memset(&r, 0, sizeof r);
    assert(exefs_ips(ips32, sizeof ips32, rec, &r) == 1 && r.off[0] == 0x100 && r.bytes[0][0] == 0x42);
    assert(exefs_ips(ips, 10, rec, &r) == -1 && exefs_ips((const uint8_t *)"NOPE!", 5, rec, &r) == -1);
    const char *pch = "@nsobid-0123456789ABCDEF0011\n@flag offset_shift 0x100\n// a\n@enabled\n00001000 1F2003D5\n"
                      "@disabled\n00002000 00000000\n@enabled\n00003000 \"hi\"\n@stop\n00004000 FF\n";
    char bid[80];
    assert(exefs_pchtxt_bid(pch, strlen(pch), bid, sizeof bid) && !strcmp(bid, "0123456789abcdef0011"));
    memset(&r, 0, sizeof r);
    assert(exefs_pchtxt(pch, strlen(pch), rec, &r) == 2);
    assert(r.off[0] == 0x1000 && r.len[0] == 4 && r.bytes[0][0] == 0x1F && r.off[1] == 0x3000 && r.len[1] == 2 && r.bytes[1][0] == 'h');
    const char *id = "0123456789abcdef001100000000000000000000000000000000000000000000";
    assert(exefs_bid_match("0123456789ABCDEF.ips", id) && exefs_bid_match("0123456789abcdef0011", id));
    assert(!exefs_bid_match("0123456789abcde.ips", id) && !exefs_bid_match("0123456789abcdef1.ips", id));
    // cheats: the file, then the codes on memory made here (region 0 at 0x1000)
    static Cheat ch[4];
    const char *ctext = "{Master}\n04000000 00000010 0000002A\n\n[Infinite \"HP\"]\r\n040A0000 00000010 12345678\n";
    assert(cheat_parse(ctext, strlen(ctext), ch, 4) == 2 && ch[0].master && !ch[1].master);
    assert(!strcmp(ch[1].name, "Infinite \"HP\"") && ch[1].n == 3 && ch[1].op[0] == 0x040A0000);
    assert(cheat_parse("04000000", 8, ch, 4) == 0);  // (codes before a name)
    const char *shared = "[Money]\nThese need the menu open\n041F0000 0E440AC0 00999\n[Left open\n[Ok]\r\n04000000 00000010\n"
                         "00000001\n";
    assert(cheat_parse(shared, strlen(shared), ch, 4) == 2 && ch[0].broken && !ch[1].broken && ch[1].n == 3);
    CheatVm vm;
    cheat_vm_init(&vm, CHEAT_OPS_MAX);
    CheatEnv env = {.base = {0x1000, 0x2000, 0x3000, 0x4000, 0}, .rd = mem_rd, .wr = mem_wr};
    assert(run_codes(&vm, &env, "04000000 00000010 0000002A") == CHEAT_DONE && mem32(0x10) == 42);
    // if / else
    assert(run_codes(&vm, &env, "14050000 00000010 0000002A 02000000 00000020 00000007 20000000 "
                                "14060000 00000010 0000002A 01000000 00000030 00000001 21000000 "
                                "01000000 00000030 00000002 20000000") == CHEAT_DONE);
    assert(mem32(0x20) == 7 && mem[0x30] == 2);
    // a pointer: load it, store through it
    uint64_t ptr = 0x1200;
    memcpy(mem + 0x100, &ptr, 8);
    assert(run_codes(&vm, &env, "58010000 00000100 64010000 00000000 00000063") == CHEAT_DONE && mem32(0x200) == 0x63);
    // a loop adding 3 five times, a register condition, a register store
    assert(run_codes(&vm, &env, "40020000 00000000 00000000 30030000 00000005 94022100 00000003 31030000 "
                                "A4200400 00000300 C0452400 0000000F 01000000 00000310 00000001 20000000") == CHEAT_DONE);
    assert(mem32(0x300) == 15 && mem[0x310] == 1);
    // float arithmetic: 1.5 + 2.0
    assert(run_codes(&vm, &env, "40050000 00000000 3FC00000 94A55100 40000000 A4500400 00000320") == CHEAT_DONE);
    assert(mem32(0x320) == 0x40600000);
    // keys: 8 while held, C4 once a press
    const char *keys = "80000001 01000000 00000400 00000001 20000000 "
                       "C4000000 00000000 00000002 51040000 00000500 91044100 00000001 A1400400 00000500 20000000";
    env.keys = 0;
    assert(run_codes(&vm, &env, keys) == CHEAT_DONE && !mem[0x400] && !mem[0x500]);
    env.keys = 3;
    for (int i = 0; i < 3; i++) run_codes(&vm, &env, keys);
    assert(mem[0x400] == 1 && mem[0x500] == 1);
    env.keys = 0, run_codes(&vm, &env, keys);
    env.keys = 2, run_codes(&vm, &env, keys);
    assert(mem[0x500] == 2);
    // unmapped memory is skipped; a code it can't decode stops the run
    assert(run_codes(&vm, &env, "04400000 00000000 00000001") == CHEAT_DONE);
    assert(run_codes(&vm, &env, "B0000000") == CHEAT_BAD && run_codes(&vm, &env, "04000000 00000010") == CHEAT_BAD);
    cheat_vm_free(&vm);
    puts("mods: ok");
    return 0;
}
