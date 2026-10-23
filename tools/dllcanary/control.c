/* control.c -- the proof that the canary test can fail (SEC-003).
 *
 * `control.exe NAME...` loads each DLL the way an unhardened program does:
 * a plain LoadLibraryW of the bare name, which looks in the program's own
 * folder first. If the canary DLLs beside it are not loaded by THIS, then
 * "the installer did not load them" proves nothing. Prints where each one
 * came from and exits 0 only when every name was found.
 */
#include <windows.h>
#include <stdio.h>

int wmain(int argc, wchar_t **argv)
{
    int bad = 0;
    for (int i = 1; i < argc; i++) {
        HMODULE m = LoadLibraryW(argv[i]);
        wchar_t p[MAX_PATH] = L"";
        if (m) GetModuleFileNameW(m, p, MAX_PATH);
        wprintf(L"%ls: %ls\n", argv[i], m ? p : L"NOT FOUND");
        if (!m) bad = 1;
    }
    return bad;
}
