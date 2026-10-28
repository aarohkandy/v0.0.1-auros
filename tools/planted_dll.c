/* planted_dll.c -- a DLL that must NEVER load (CI, docs/security SEC-003).
 * Built as wlanapi.dll and winspool.drv and put beside aurbridge.exe, where
 * LoadLibraryA("wlanapi.dll") used to find it first. If it is ever loaded it
 * leaves C:\PLANTED-DLL-RAN.txt, and the test goes red. */
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE h, DWORD why, LPVOID r)
{
    (void)h; (void)r;
    if (why == DLL_PROCESS_ATTACH) {
        HANDLE f = CreateFileA("C:\\PLANTED-DLL-RAN.txt", GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD w;
            WriteFile(f, "loaded\n", 7, &w, NULL);
            CloseHandle(f);
        }
    }
    return TRUE;
}
