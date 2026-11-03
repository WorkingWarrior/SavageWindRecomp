// Mod folder layouts (runtime/mods_scan.h) and the ResourceSizeTable merge (runtime/services/rstb_botw.h), on folders
// and tables made here. make test-mods
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../runtime/mods_scan.h"
#include "../runtime/services/rstb_botw.h"

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

static bool ends(const char *s, const char *tail) {
    size_t n = strlen(s), t = strlen(tail);
    return n >= t && !strcmp(s + n - t, tail);
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
    assert(!f.romfs[0] && f.code);
    f = scan("emptyromfs");
    assert(!f.romfs[0]);
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
    puts("mods: ok");
    return 0;
}
