#include "game_catalog.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <errno.h>

static bool valid_name(const char *name)
{
    size_t n = strnlen(name, GAME_NAME_BYTES);
    if (!n || n > 247 || name[0] == '.' || strchr(name, '/') || strchr(name, '\\')) return false;
    const char *extension = strrchr(name, '.');
    return extension && extension != name &&
           (!strcasecmp(extension, ".gb") || !strcasecmp(extension, ".gbc"));
}

bool game_catalog_paths(const char *directory, const game_entry_t *entry,
                        char rom[GAME_PATH_BYTES], char save[GAME_PATH_BYTES])
{
    if (!directory || !entry || !valid_name(entry->name)) return false;
    int n = snprintf(rom, GAME_PATH_BYTES, "%s/%s", directory, entry->name);
    if (n < 0 || n >= GAME_PATH_BYTES) return false;
    memcpy(save, rom, (size_t)n + 1);
    char *extension = strrchr(save, '.');
    /* .gb maps to .sav; other names append .sav so game.gbc cannot collide with game.gb. */
    if (!strcasecmp(extension, ".gb")) {
        if (n + 1 >= GAME_PATH_BYTES) return false;
        strcpy(extension, ".sav");
    }
    else if (snprintf(save, GAME_PATH_BYTES, "%s.sav", rom) >= GAME_PATH_BYTES) return false;
    return true;
}

static int compare(const void *a, const void *b)
{
    const game_entry_t *aa = a, *bb = b;
    return strcasecmp(aa->name, bb->name);
}

bool game_catalog_scan(const char *directory, game_entry_t *entries, size_t capacity,
                       size_t *count, bool *truncated)
{
    if (!directory || !entries || !capacity || !count || !truncated) return false;
    *count = 0; *truncated = false;
    DIR *dir = opendir(directory);
    if (!dir) return false;
    struct dirent *item;
    bool ok = true;
    for (;;) {
        errno = 0;
        item = readdir(dir);
        if (!item) { if (errno) ok = false; break; }
        if (!valid_name(item->d_name)) continue;
        game_entry_t entry = {0};
        memcpy(entry.name, item->d_name, strlen(item->d_name) + 1);
        char path[GAME_PATH_BYTES], save[GAME_PATH_BYTES];
        struct stat info;
        if (!game_catalog_paths(directory, &entry, path, save) || stat(path, &info) || !S_ISREG(info.st_mode)) continue;
        if (*count == capacity) { *truncated = true; continue; }
        entries[(*count)++] = entry;
    }
    if (closedir(dir)) ok = false;
    qsort(entries, *count, sizeof(*entries), compare);
    return ok;
}
