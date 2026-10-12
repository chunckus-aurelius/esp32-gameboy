#pragma once
#include <stdbool.h>
#include <stddef.h>
#define GAME_NAME_BYTES 256
#define GAME_PATH_BYTES 288
#define GAME_CATALOG_MAX 64
typedef struct { char name[GAME_NAME_BYTES]; } game_entry_t;
/* Flat ROM directory; ignores hidden files and directories. */
bool game_catalog_scan(const char *directory, game_entry_t *entries, size_t capacity,
                       size_t *count, bool *truncated);
bool game_catalog_paths(const char *directory, const game_entry_t *entry,
                        char rom[GAME_PATH_BYTES], char save[GAME_PATH_BYTES]);
