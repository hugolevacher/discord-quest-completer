/*
 * procs.c - finding and stopping the games this tool has spawned.
 */
#include "procs.h"

#include "spawn.h"

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Seconds since the process started; 0 if that can't be read. */
static unsigned long long process_age(HANDLE process)
{
    FILETIME created, exited, kernel, user, now_ft;
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        return 0;
    }
    GetSystemTimeAsFileTime(&now_ft);

    ULARGE_INTEGER then, now;
    then.LowPart = created.dwLowDateTime;
    then.HighPart = created.dwHighDateTime;
    now.LowPart = now_ft.dwLowDateTime;
    now.HighPart = now_ft.dwHighDateTime;
    return now.QuadPart > then.QuadPart ? (now.QuadPart - then.QuadPart) / 10000000ULL : 0;
}

/* Longest-running first. */
static int compare_age(const void *a, const void *b)
{
    const struct running_game *x = a;
    const struct running_game *y = b;
    if (x->seconds != y->seconds) {
        return x->seconds > y->seconds ? -1 : 1;
    }
    return x->pid < y->pid ? -1 : x->pid > y->pid;
}

size_t procs_list(struct running_game **games)
{
    *games = NULL;

    char root[MAX_PATH];
    if (!spawn_root(root, sizeof(root))) {
        return 0;
    }
    size_t root_len = strlen(root);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    struct running_game *list = NULL;
    size_t n = 0;
    size_t cap = 0;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    for (BOOL more = Process32First(snap, &pe); more; more = Process32Next(snap, &pe)) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) {
            continue;  /* system / protected process */
        }

        char image[MAX_PATH];
        DWORD size = sizeof(image);
        bool ours = QueryFullProcessImageNameA(h, 0, image, &size) &&
                    size > root_len + 1 &&
                    _strnicmp(image, root, root_len) == 0 && image[root_len] == '\\';
        if (ours) {
            if (n == cap) {
                size_t new_cap = cap ? cap * 2 : 8;
                struct running_game *bigger = realloc(list, new_cap * sizeof(*list));
                if (!bigger) {
                    CloseHandle(h);
                    break;
                }
                list = bigger;
                cap = new_cap;
            }
            list[n].pid = pe.th32ProcessID;
            snprintf(list[n].path, sizeof(list[n].path), "%s", image + root_len + 1);
            list[n].seconds = process_age(h);
            n++;
        }
        CloseHandle(h);
    }
    CloseHandle(snap);

    if (n > 1) {
        qsort(list, n, sizeof(*list), compare_age);
    }
    *games = list;
    return n;
}

bool procs_stop(unsigned long pid)
{
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) {
        return false;
    }
    bool gone = TerminateProcess(h, 0) &&
                WaitForSingleObject(h, 3000) == WAIT_OBJECT_0;
    CloseHandle(h);
    return gone;
}
