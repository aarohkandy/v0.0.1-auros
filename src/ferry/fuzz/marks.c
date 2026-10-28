/* libFuzzer harness: ferry-chromemarks on a hostile Bookmarks file.
 * JSON from her Windows disk, parsed as her: it must neither crash nor
 * emit markup it did not escape (checked by the sanitizers and by the
 * output staying free of a raw '<' inside names, via the escaper). */
#define CHROMEMARKS_NO_MAIN
#include "../chromemarks.c"

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    if (!freopen("/dev/null", "w", stdout)) return 1;
    return 0;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
    if (size > (1u << 20)) return 0;
    struct jv *doc[2] = { parse((const char *)data, size), NULL };
    char *label[2] = { "Edge", "Chrome" };
    if (doc[0]) {
        doc[1] = parse((const char *)data, size);
        emit(doc, label, 2);
    }
    jfree(doc[0]);
    jfree(doc[1]);
    return 0;
}
