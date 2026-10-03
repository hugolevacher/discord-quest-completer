/*
 * spawner.c
 *
 * Testing helper: create a running process with a name you choose, so that
 * detection software can be checked against its "known exe" list.
 *
 * How it works:
 *   - Run with no arguments  -> interactive mode: asks for a name, copies this
 *     exe to "<name>.exe", and launches that copy in a new console window.
 *   - The launched copy runs with the argument "--child", which just keeps the
 *     process alive (printing a heartbeat) until you close its window / Ctrl+C.
 *
 * The child process therefore appears in Task Manager / detection tools as
 * "<name>.exe", which is what you want to test against.
 *
 * Build (MinGW):   gcc -O2 -o spawner.exe spawner.c
 * Build (MSVC):    cl spawner.c
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

/* Child mode: just stay alive until the user closes the window. */
static int run_as_child(void)
{
    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);

    /* Strip directory, keep just the file name for display. */
    const char *name = strrchr(self, '\\');
    name = name ? name + 1 : self;

    printf("Running as process: %s\n", name);
    printf("PID: %lu\n", (unsigned long)GetCurrentProcessId());
    printf("This process stays alive until you close this window (or Ctrl+C).\n\n");

    unsigned long seconds = 0;
    for (;;) {
        printf("[%s] alive - %lu s\r", name, seconds);
        fflush(stdout);
        Sleep(1000);
        seconds++;
    }
    return 0; /* never reached */
}

/* Parent mode: ask for a name, clone self to <name>.exe, launch it. */
static int run_interactive(void)
{
    char name[256];

    printf("Enter a name for the process (without .exe): ");
    if (!fgets(name, sizeof(name), stdin)) {
        fprintf(stderr, "No input.\n");
        return 1;
    }

    /* Trim trailing newline / whitespace. */
    size_t len = strlen(name);
    while (len > 0 && (name[len - 1] == '\n' || name[len - 1] == '\r' ||
                       name[len - 1] == ' ' || name[len - 1] == '\t')) {
        name[--len] = '\0';
    }
    if (len == 0) {
        fprintf(stderr, "Name cannot be empty.\n");
        return 1;
    }

    /* Build the target file name "<name>.exe". */
    char target[512];
    snprintf(target, sizeof(target), "%s.exe", name);

    /* Path to this running executable. */
    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);

    /* Copy this exe to <name>.exe (overwrite allowed). */
    if (!CopyFileA(self, target, FALSE)) {
        fprintf(stderr, "Failed to create %s (error %lu).\n",
                target, (unsigned long)GetLastError());
        return 1;
    }
    printf("Created %s\n", target);

    /* Launch the copy in child mode, in its own new console window. */
    char cmdline[1024];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" --child", target);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                        CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "Failed to start %s (error %lu).\n",
                target, (unsigned long)GetLastError());
        return 1;
    }

    printf("Started %s (PID %lu) in a new window.\n",
           target, (unsigned long)pi.dwProcessId);
    printf("Close that window to stop it.\n");

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--child") == 0) {
        return run_as_child();
    }
    return run_interactive();
}
