// The player's mods (runtime/mods.c): each folder in <data root>/mods/<game> (botw, totk) is one mod, laid out as
// emulators and the Switch take them (runtime/mods_scan.h); a name starting with '_' or '.' is left out. A whole
// emulator mods folder copied in (yuzu's load/<title>, Ryujinx's mods/contents/<title>) is one mod per folder in it,
// named <folder>/<mod>. Their order (the top mod wins a file two mods change) and switches are in mods.ini in that
// folder, a line per mod, top first: "+name" on, "-name" off. New mods come on, below the known ones, by name. The mods
// on when the game starts are the run's: fs.c serves their RomFS files (below the SAVAGEWIND_ROMFS_OVERLAY folder), the
// loader their ExeFS patches (mods_exefs_apply); the Mods tab (runtime/ui/menu.mm) edits the list for the next start.
// Cheats switch at once, each on its own.
// ExeFS patches are applied to the module images as Atmosphere does, but the game's code is recompiled ahead of time:
// only what they change in its data (.rodata, .data) takes effect; what they change in its code (.text) is counted and
// left out. Atmosphere cheats (cheats/<build id>.txt): each has a switch of its own, off until the player switches it
// on (cheats.ini in the mods folder), whatever its mod's switch (that one is for the game files); a file's cheats on
// run with its master codes, twelve times a second, on a thread of their own (runtime/cheat_vm.h), from the moment
// they are switched on. A cheat that patches the game's code is left out whole.
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MOD_OK, MOD_EMPTY /* no RomFS files, no ExeFS patches, no cheats */, MOD_OTHER_GAME };

typedef struct {
    char name[256];          // the folder's name (<folder>/<mod> in a collection): its key in mods.ini
    char layout[200];        // where its RomFS was found (mods_scan.h)
    char other_title[17];    // MOD_OTHER_GAME: the title it is for
    int problem;             // MOD_*
    bool on;                 // the player's switch
    bool aoc;                // it changes add-on content files too
    bool exefs;              // it has ExeFS patch folders
    bool cheats;             // it has an Atmosphere cheats folder
    // fs.c, once the game opened its RomFS (counted: false until then)
    bool counted;
    int replaced, added;     // game files it serves instead of the game's, files it adds
    int shadowed;            // files a mod above it (or the overlay folder) serves instead
    bool in_run;             // it is one of this run's mods (on and usable when the game started)
    // the loader (mods_exefs_apply)
    int patch_files;         // ExeFS patch files for this game's build
    int patch_data, patch_code;  // their records: applied (data), left out (code)
    // its cheats, once the game is loaded (mods_cheats_start)
    bool cheats_checked;     // the game's build id is known: the counts below are its
    int cheat_files;         // cheat files for this game's build
    int cheat_count;         // the cheats in them that can run (master codes not counted)
    int cheat_on;            // ... and switched on
    bool cheat_bad;          // one stopped at a code it can't decode, or never ended
    bool cheat_code;         // one writes to the game's code (left out)
} Mod;

typedef struct {
    char name[64];
    bool on;                 // its switch
    bool usable;             // it can run: no broken code (not 8 hex digits) and no patch to the game's code
    bool code;               // ... it patches the game's code (left out whole)
    bool stopped;            // on, and its file stopped at a code it can't run (until a switch changes)
} ModCheat;

// The list as it is now (the tab's edits included), top first; scanned at the first call. Copies: any thread.
int mods_count(void);
bool mods_get(int i, Mod *out);
const char *mods_folder(void);  // <data root>/mods/<game> (made at the first scan)
void mods_rescan(void);         // the folder again (files copied in meanwhile); order and switches stay
// The tab's edits, saved at once
void mods_set_on(int i, bool on);
void mods_move(int i, int delta);  // -1 up, +1 down
// Mod i's cheats (its files' for this game's build, in their order; master codes not listed), once the game is loaded
int mods_cheat_count(int i);
bool mods_cheat_get(int i, int j, ModCheat *out);
void mods_cheat_set(int i, int j, bool on);  // at once, and saved
bool mods_changed(void);           // order or switches differ from what the game started with (false before it did)
// fs.c: the RomFS roots of the mods on and usable, top first, fixed for the run at the first call. image 0: the
// game's, n: add-on content n (only 1 has mod files). mod[] gets each one's index in the start list (mods_note).
int mods_roots(int image, const char **roots, int *mod, int max);
void mods_note(int mod, int replaced, int added, int shadowed);
// runtime/loader.c: the ExeFS patches of the run's mods for module m (its build id: 64 hex digits), on its image as
// loaded, before relocation; the lowest mod first, so the top one wins a byte two change
struct Module;
void mods_exefs_apply(struct Module *m, const char *build_id);
// runtime/loader.c, once the modules are in: the mods' cheats for the main module's build id loaded, and their thread
// started (once a run of the program: a reboot of the game keeps them)
void mods_cheats_start(const char *main_build_id);

#ifdef __cplusplus
}
#endif
