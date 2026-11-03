// amiibo from the player's own dumps (runtime/services/nfp.c): the game sees one NFC reader; when it searches for a
// figure (BOTW: the amiibo rune), a list of the dumps in <data root>/amiibo comes up (runtime/ui/menu.mm) and the one
// picked goes on the reader. Any thread.
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int amiibo_count(void);  // the dumps (.bin / .nfc), read again at each search
// dump i: its name (the file's, without the extension), and why it can't be used ("" usable, English: tr() it)
bool amiibo_get(int i, char *name, int cap, const char **problem);
const char *amiibo_folder(void);
bool amiibo_searching(void);  // the game waits for a figure
bool amiibo_scan(int i);      // dump i goes on the reader (while the game searches)

#ifdef __cplusplus
}
#endif
