/* planted.c -- a program that must NEVER run (tools/ CI, docs/security SEC-002).
 * Built as schtasks.exe, mountvol.exe, manage-bde.exe and powercfg.exe and put
 * beside aurbridge.exe, where CreateProcessW(NULL, "schtasks ...") used to
 * find it first. If it ever starts it leaves PLANTED-RAN.txt in its working
 * directory, and the test goes red. */
#include <stdio.h>
int main(void)
{
    FILE *f = fopen("PLANTED-RAN.txt", "w");
    if (f) { fputs("a program planted beside the installer was started\n", f); fclose(f); }
    f = fopen("C:\\PLANTED-RAN-ELEVATED.txt", "w");
    if (f) { fputs("started\n", f); fclose(f); }
    return 0;
}
