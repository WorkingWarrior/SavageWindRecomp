// amiibo dumps (runtime/amiibo_dump.h): binary and Flipper images, what is checked, and the layouts handed to the game.
// make test-amiibo
#include <assert.h>
#include <stdio.h>
#include "../runtime/amiibo_dump.h"

int main(void) {
    uint8_t bin[540] = {0x04, 0xAA, 0xBB, 0x00, 0x11, 0x22, 0x33, 0x44};
    const uint8_t cc[4] = {0xF1, 0x10, 0xFF, 0xEE}, id[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x02};
    memcpy(bin + 0x0C, cc, 4);
    memcpy(bin + 0x54, id, 8);
    uint8_t img[1024];
    AmiiboTag t;
    assert(amiibo_image(bin, sizeof bin, img, sizeof img) == 540);
    assert(amiibo_parse(img, 540, &t) == AMIIBO_OK);
    assert(!memcmp(t.uid, "\x04\xAA\xBB\x11\x22\x33\x44", 7) && !memcmp(t.id, id, 8));
    assert(amiibo_parse(img, 0x50, &t) == AMIIBO_SIZE);
    img[0x0C] = 0;
    assert(amiibo_parse(img, 540, &t) == AMIIBO_NO_CC);
    img[0x0C] = 0xF1, img[0x5B] = 1;
    assert(amiibo_parse(img, 540, &t) == AMIIBO_FORMAT);

    // Flipper .nfc: the same tag as pages
    char nfc[8192];
    int k = snprintf(nfc, sizeof nfc, "Filetype: Flipper NFC device\nVersion: 4\nDevice type: NTAG215\n");
    for (int p = 0; p < 135; p++)
        k += snprintf(nfc + k, sizeof nfc - k, "Page %d: %02X %02X %02X %02X\n", p, bin[p * 4], bin[p * 4 + 1],
                      bin[p * 4 + 2], bin[p * 4 + 3]);
    memset(img, 0, sizeof img);
    size_t n = amiibo_image((const uint8_t *)nfc, (size_t)k, img, sizeof img);
    assert(n == 540 && !memcmp(img, bin, 540));
    assert(amiibo_parse(img, n, &t) == AMIIBO_OK);

    // layouts (switchbrew): TagInfo uid, length, protocol, type; ModelInfo character + variant as on the tag, series,
    // model number byte-swapped, type
    uint8_t ti[0x58], mi[0x40];
    amiibo_tag_info(&t, ti);
    assert(!memcmp(ti, t.uid, 7) && ti[0x0A] == 7 && ti[0x20] == 1 && ti[0x24] == 2);
    amiibo_model_info(&t, mi);
    assert(mi[0] == 0x01 && mi[1] == 0x02 && mi[2] == 0x03 && mi[3] == 0x07 && mi[4] == 0x06 && mi[5] == 0x05 && mi[6] == 0x04);
    puts("amiibo: ok");
    return 0;
}
