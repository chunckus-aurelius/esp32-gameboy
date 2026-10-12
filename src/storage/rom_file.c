#include "rom_file.h"
#include <errno.h>
#include <stdio.h>

rom_file_status_t rom_file_read(const char *path, uint8_t *buffer, size_t capacity)
{
    if (!path || !buffer || !capacity) return ROM_FILE_IO;
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? ROM_FILE_MISSING : ROM_FILE_IO;
    rom_file_status_t result = ROM_FILE_IO;
    if (fseek(file, 0, SEEK_END) != 0) goto close;
    long length = ftell(file);
    if (length < 0) goto close;
    if ((size_t)length != capacity) { result = ROM_FILE_SIZE; goto close; }
    if (fseek(file, 0, SEEK_SET) != 0) goto close;
    if (fread(buffer, 1, capacity, file) != capacity) goto close;
    if (fgetc(file) != EOF) { result = ROM_FILE_SIZE; goto close; }
    if (!ferror(file)) result = ROM_FILE_OK;
close:
    if (fclose(file) != 0) result = ROM_FILE_IO;
    return result;
}
