#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Flush the replacement before rotating the existing file to .bak. */
bool save_file_replace(const char *path, const uint8_t *data, size_t size);
