/*
 * fs.c - filesystem helpers.
 */
#include "fs.h"

#include "config.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool fs_exe_dir(char *out, size_t cap)
{
    char self[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return false;
    }

    char *last = strrchr(self, '\\');
    if (last) {
        *last = '\0';
    }
    int written = snprintf(out, cap, "%s", self);
    return written >= 0 && (size_t)written < cap;
}

bool fs_data_path(char *out, size_t cap, const char *name)
{
    char dir[MAX_PATH];
    if (!fs_exe_dir(dir, sizeof(dir))) {
        return false;
    }
    char data[MAX_PATH];
    int written = snprintf(data, sizeof(data), "%s\\%s", dir, DATA_DIR);
    if (written < 0 || (size_t)written >= sizeof(data)) {
        return false;
    }
    CreateDirectoryA(data, NULL);  /* fine if it already exists */

    written = snprintf(out, cap, "%s\\%s", data, name);
    return written >= 0 && (size_t)written < cap;
}

bool fs_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

char *fs_read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    char *buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long len = ftell(f);
        if (len >= 0 && fseek(f, 0, SEEK_SET) == 0) {
            buf = malloc((size_t)len + 1);
            if (buf && fread(buf, 1, (size_t)len, f) == (size_t)len) {
                buf[len] = '\0';
                if (size) {
                    *size = (size_t)len;
                }
            } else {
                free(buf);
                buf = NULL;
            }
        }
    }
    fclose(f);
    return buf;
}

bool fs_file_age(const char *path, long long *seconds)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
        return false;
    }

    FILETIME now_ft;
    GetSystemTimeAsFileTime(&now_ft);

    /* FILETIMEs count 100-nanosecond ticks. */
    ULARGE_INTEGER now, then;
    now.LowPart = now_ft.dwLowDateTime;
    now.HighPart = now_ft.dwHighDateTime;
    then.LowPart = data.ftLastWriteTime.dwLowDateTime;
    then.HighPart = data.ftLastWriteTime.dwHighDateTime;

    *seconds = now.QuadPart > then.QuadPart
        ? (long long)((now.QuadPart - then.QuadPart) / 10000000ULL)
        : 0;
    return true;
}

/* True for a drive root like "C:", which can't (and needn't) be created. */
static bool is_drive_root(const char *s)
{
    return strlen(s) == 2 && s[1] == ':';
}

void fs_make_parent_dirs(const char *path)
{
    char tmp[MAX_PATH];
    int written = snprintf(tmp, sizeof(tmp), "%s", path);
    if (written < 0 || (size_t)written >= sizeof(tmp)) {
        return;  /* too long; the following CopyFile will report the error */
    }

    char *last = strrchr(tmp, '\\');
    if (!last) {
        return;  /* no folder part -> nothing to create */
    }
    *last = '\0';  /* tmp is now just the folder portion */

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '\\') {
            *p = '\0';
            if (!is_drive_root(tmp)) {
                CreateDirectoryA(tmp, NULL);
            }
            *p = '\\';
        }
    }
    if (!is_drive_root(tmp)) {
        CreateDirectoryA(tmp, NULL);
    }
}

void fs_delete_tree(const char *path, int *deleted, int *failed)
{
    char pattern[MAX_PATH];
    int written = snprintf(pattern, sizeof(pattern), "%s\\*", path);
    if (written < 0 || (size_t)written >= sizeof(pattern)) {
        (*failed)++;
        return;
    }

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
                continue;
            }

            char child[MAX_PATH];
            written = snprintf(child, sizeof(child), "%s\\%s", path, fd.cFileName);
            if (written < 0 || (size_t)written >= sizeof(child)) {
                (*failed)++;
                continue;
            }

            bool is_dir  = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            bool is_link = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

            if (is_dir && !is_link) {
                fs_delete_tree(child, deleted, failed);
            } else if (is_dir) {
                /* Junction / folder symlink: remove the link, never follow it. */
                if (!RemoveDirectoryA(child)) {
                    (*failed)++;
                }
            } else {
                SetFileAttributesA(child, FILE_ATTRIBUTE_NORMAL);
                if (DeleteFileA(child)) {
                    (*deleted)++;
                } else {
                    (*failed)++;
                }
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    /* Removes the folder only if it is now empty; failure is fine. */
    RemoveDirectoryA(path);
}
