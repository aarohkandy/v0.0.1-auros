/* canary.c -- a DLL that only says it was loaded (SEC-003).
 *
 * The same file is copied under the name of every DLL the installer must
 * never take from its own folder (winhttp.dll, oleacc.dll, ...). The
 * moment any process loads one, this writes "<its own path>.loaded" and
 * does nothing else: it exports nothing, so a program that needs a real
 * function from it simply gets nothing, as it would from a missing file.
 * Built by tools/dllcanary.sh; used by tools/dlltest.sh and windows.yml.
 */
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE h, DWORD why, LPVOID reserved)
{
    (void)reserved;
    if (why == DLL_PROCESS_ATTACH) {
        wchar_t p[MAX_PATH + 16];
        DWORD n = GetModuleFileNameW(h, p, MAX_PATH);
        if (n && n < MAX_PATH) {
            lstrcatW(p, L".loaded");
            HANDLE f = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, NULL);
            if (f != INVALID_HANDLE_VALUE) {
                DWORD w;
                WriteFile(f, "loaded\n", 7, &w, NULL);
                CloseHandle(f);
            }
        }
    }
    return TRUE;
}
