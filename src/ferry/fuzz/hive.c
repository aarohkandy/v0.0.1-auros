/* libFuzzer harness: ferry-hivepeek on a hostile hive.
 *
 * The hive is the most hostile input Ferry reads as root: a binary
 * B-tree from a disk we did not build. The harness skips main()'s
 * "regf" check (so mutations reach the tree walk) and asks every
 * question Ferry asks: the keys and values of the paths it uses, a
 * subkey listing, and one value by name. Output goes to /dev/null. */
#define main hivepeek_main
#include "../hivepeek.c"
#undef main

static const char *PATHS[] = {
    "",
    "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",
    "Control Panel\\Desktop",
    "Microsoft\\Windows NT\\CurrentVersion\\ProfileList",
    "Select",
    "ControlSet001\\Control\\TimeZoneInformation",
};

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    if (!freopen("/dev/null", "w", stdout)) return 1;
    return 0;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
    if (size < HIVE_BASE + 4 || size > (4u << 20)) return 0;
    img = malloc(size);
    if (!img) return 0;
    memcpy(img, data, size);
    imglen = size;
    for (size_t i = 0; i < sizeof PATHS / sizeof PATHS[0]; i++) {
        size_t nk = key_find(PATHS[i]);
        if (!nk) continue;
        int budget = MAX_VISITS;
        subkeys_walk(rd32(nk + NK_SUBKEY_LIST), NULL, &budget, 0);
        values(nk, NULL, 1);
        values(nk, "Wallpaper", 1);
    }
    free(img);
    img = NULL;
    return 0;
}
