// The player's mods (runtime/mods.c): each folder in <data root>/mods is one mod, laid out as emulators and the Switch
// take them (runtime/mods_scan.h); a name starting with '_' or '.' is left out. Their order (the top mod wins a file
// two mods change) and switches are in <data root>/mods.ini, a line per mod, top first: "+name" on, "-name" off. New
// mods come on, below the known ones, by name. fs.c serves the RomFS files of the mods that were on when the game
// opened its RomFS (below the SAVAGEWIND_ROMFS_OVERLAY folder); the Mods tab (runtime/ui/menu.mm) edits the list for
// the next start. Code patches (exefs, cheats) are not loaded: a recompiled game would need building again.
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MOD_OK, MOD_EMPTY /* no RomFS files */, MOD_OTHER_GAME, MOD_CODE_ONLY /* only exefs / cheats */ };

typedef struct {
    char name[256];          // the folder's name: its key in mods.ini
    char layout[200];        // where its RomFS was found (mods_scan.h)
    char other_title[17];    // MOD_OTHER_GAME: the title it is for
    int problem;             // MOD_*
    bool on;                 // the player's switch
    bool aoc;                // it changes add-on content files too
    bool code;               // it has code patches too (left out)
    // fs.c, once the game opened its RomFS (counted: false until then)
    bool counted;
    int replaced, added;     // game files it serves instead of the game's, files it adds
    int shadowed;            // files a mod above it (or the overlay folder) serves instead
} Mod;

// The list as it is now (the tab's edits included), top first; scanned at the first call. Copies: any thread.
int mods_count(void);
bool mods_get(int i, Mod *out);
const char *mods_folder(void);  // <data root>/mods (made at the first scan)
void mods_rescan(void);         // the folder again (files copied in meanwhile); order and switches stay
// The tab's edits, saved at once
void mods_set_on(int i, bool on);
void mods_move(int i, int delta);  // -1 up, +1 down
bool mods_changed(void);           // order or switches differ from what the game started with (false before it did)
// fs.c: the RomFS roots of the mods on and usable, top first, fixed for the run at the first call. image 0: the
// game's, n: add-on content n (only 1 has mod files). mod[] gets each one's index in the start list (mods_note).
int mods_roots(int image, const char **roots, int *mod, int max);
void mods_note(int mod, int replaced, int added, int shadowed);

#ifdef __cplusplus
}
#endif
