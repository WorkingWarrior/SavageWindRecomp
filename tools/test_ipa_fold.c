// Focused regression for the textual IPA fold; include its implementation to test the pre-wrapper body.
// Run: clang -O2 tools/test_ipa_fold.c -lm -o /tmp/test_ipa_fold && /tmp/test_ipa_fold
#include "../runtime/nvn/maxwell/translate.c"

int main(void) {
    const char *indents[] = {"", " ", "  ", "    ", "\t"};
    int failed = 0;
    for (size_t i = 0; i < sizeof indents / sizeof *indents; i++) {
        Str body = {0};
        sp(&body, "%scase 8u:\n"
                  "    r0 = as_u(gl_FragCoord[3]);\n"
                  "    r1 = floatBitsToUint((1.0f / uintBitsToFloat(r0)));\n"
                  "%scase 32u:\n"
                  "    r2 = as_u(v0[0] * gl_FragCoord.w * uintBitsToFloat(r1));\n"
                  "    r1 = 0u;\n"
                  "    pc = 32u; continue;\n", indents[i], indents[i]);
        char *before = strdup(body.buf);
        glsl_fold_ipa_w(&body);
        if (strcmp(before, body.buf)) {
            fprintf(stderr, "FAIL: backward jump body changed (indent variant %zu)\n", i);
            failed++;
        }
        free(before);
        free(body.buf);
    }
    // A straight-line body must retain the optimization.
    Str body = {0};
    sp(&body, "    r0 = as_u(gl_FragCoord[3]);\n"
              "    r1 = floatBitsToUint((1.0f / uintBitsToFloat(r0)));\n"
              "    r2 = as_u(v0[0] * gl_FragCoord.w * uintBitsToFloat(r1));\n");
    glsl_fold_ipa_w(&body);
    if (!strstr(body.buf, "r2 = as_u(v0[0]);")) {
        fprintf(stderr, "FAIL: straight-line varying was not folded\n");
        failed++;
    }
    free(body.buf);
    if (!failed) puts("IPA fold: 5 backward-jump variants unchanged; straight-line fold passed");
    return failed ? 1 : 0;
}
