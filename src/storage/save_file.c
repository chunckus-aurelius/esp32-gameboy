#include "save_file.h"
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

bool save_file_replace(const char *path, const uint8_t *data, size_t size)
{
    if (!path || !data || !size) return false;
    char temporary[512], backup[512];
    int n = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(temporary)) return false;
    n = snprintf(backup, sizeof(backup), "%s.bak", path);
    if (n < 0 || (size_t)n >= sizeof(backup)) return false;
    FILE *file = fopen(temporary, "wb");
    if (!file) return false;
    bool ok = fwrite(data, 1, size, file) == size;
    if (ok) ok = fflush(file) == 0;
    if (ok) ok = fsync(fileno(file)) == 0;
    if (fclose(file) != 0) ok = false;
    if (!ok) return false;
    struct stat info;
    bool exists = stat(path, &info) == 0;
    if (!exists && errno != ENOENT) return false;
    if (exists) {
        if (unlink(backup) != 0 && errno != ENOENT) return false;
        if (rename(path, backup) != 0) return false;
    }
    if (rename(temporary, path) == 0) return true;
    if (exists) (void)rename(backup, path);
    return false;
}
