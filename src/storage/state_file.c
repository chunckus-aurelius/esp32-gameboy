#include "state_file.h"
#include "save_file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

static void *state_alloc(size_t size)
{
#ifdef ESP_PLATFORM
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

#define STATE_HEADER_BYTES 24u
#define STATE_VERSION 1u
static const uint8_t state_magic[4] = {'G', 'B', 'S', 'T'};

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t state_file_crc32(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t crc = 0xffffffffu;
    if (!bytes && size) return 0;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc ^ 0xffffffffu;
}

static void make_header(uint8_t header[STATE_HEADER_BYTES], uint32_t rom_crc32,
                        uint32_t rom_size, const uint8_t *payload, size_t payload_size)
{
    memcpy(header, state_magic, sizeof(state_magic));
    put16(header + 4, STATE_VERSION);
    put16(header + 6, STATE_HEADER_BYTES);
    put32(header + 8, (uint32_t)payload_size);
    put32(header + 12, rom_crc32);
    put32(header + 16, rom_size);
    put32(header + 20, state_file_crc32(payload, payload_size));
}

state_file_status_t state_file_write(const char *path, uint32_t rom_crc32,
                                     uint32_t rom_size, const uint8_t *payload,
                                     size_t payload_size)
{
    if (!path || !*path || !payload || !rom_size || !payload_size ||
        payload_size > STATE_FILE_MAX_PAYLOAD || payload_size > UINT32_MAX)
        return STATE_FILE_INVALID;
    if (strlen(path) + 5 >= 512) return STATE_FILE_INVALID;
    size_t total_size = STATE_HEADER_BYTES + payload_size;
    uint8_t *file_data = state_alloc(total_size);
    if (!file_data) return STATE_FILE_IO_ERROR;
    make_header(file_data, rom_crc32, rom_size, payload, payload_size);
    memcpy(file_data + STATE_HEADER_BYTES, payload, payload_size);
    bool ok = save_file_replace(path, file_data, total_size);
    free(file_data);
    return ok ? STATE_FILE_OK : STATE_FILE_IO_ERROR;
}

static state_file_status_t read_one(const char *path, uint32_t wanted_rom_crc,
                                    uint32_t wanted_rom_size, uint8_t *payload,
                                    size_t exact_payload_size)
{
    struct stat info;
    if (stat(path, &info) != 0)
        return errno == ENOENT ? STATE_FILE_MISSING : STATE_FILE_IO_ERROR;
    if (!S_ISREG(info.st_mode)) return STATE_FILE_INVALID;
    if (info.st_size < (off_t)STATE_HEADER_BYTES ||
        info.st_size > (off_t)(STATE_HEADER_BYTES + STATE_FILE_MAX_PAYLOAD))
        return STATE_FILE_INVALID;

    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? STATE_FILE_MISSING : STATE_FILE_IO_ERROR;
    uint8_t header[STATE_HEADER_BYTES];
    state_file_status_t result = STATE_FILE_INVALID;
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        if (ferror(file)) result = STATE_FILE_IO_ERROR;
        goto done;
    }
    if (memcmp(header, state_magic, sizeof(state_magic)) != 0 ||
        get16(header + 6) != STATE_HEADER_BYTES || get16(header + 4) != STATE_VERSION)
        goto done;
    uint32_t payload_size = get32(header + 8);
    if (!payload_size || payload_size > STATE_FILE_MAX_PAYLOAD ||
        info.st_size != (off_t)(STATE_HEADER_BYTES + payload_size)) goto done;
    if (get32(header + 12) != wanted_rom_crc || get32(header + 16) != wanted_rom_size) {
        result = STATE_FILE_WRONG_ROM;
        goto done;
    }
    if (payload_size != exact_payload_size) {
        result = STATE_FILE_WRONG_SIZE;
        goto done;
    }
    uint8_t *temporary = state_alloc(payload_size);
    if (!temporary) { result = STATE_FILE_IO_ERROR; goto done; }
    size_t received = fread(temporary, 1, payload_size, file);
    if (received != payload_size) {
        if (ferror(file)) result = STATE_FILE_IO_ERROR;
        free(temporary);
        goto done;
    }
    int trailing = fgetc(file);
    if (trailing != EOF || ferror(file)) {
        if (ferror(file)) result = STATE_FILE_IO_ERROR;
        free(temporary);
        goto done;
    }
    if (state_file_crc32(temporary, payload_size) != get32(header + 20)) {
        free(temporary);
        goto done;
    }
    memcpy(payload, temporary, payload_size);
    free(temporary);
    result = STATE_FILE_OK;
done:
    if (fclose(file) != 0 && result == STATE_FILE_OK) result = STATE_FILE_IO_ERROR;
    return result;
}

state_file_status_t state_file_read(const char *path, uint32_t rom_crc32,
                                    uint32_t rom_size, uint8_t *payload,
                                    size_t exact_payload_size)
{
    if (!path || !*path || !payload || !rom_size || !exact_payload_size ||
        exact_payload_size > STATE_FILE_MAX_PAYLOAD) return STATE_FILE_INVALID;
    char backup[512];
    int n = snprintf(backup, sizeof(backup), "%s.bak", path);
    if (n < 0 || (size_t)n >= sizeof(backup)) return STATE_FILE_INVALID;

    state_file_status_t primary = read_one(path, rom_crc32, rom_size, payload, exact_payload_size);
    if (primary == STATE_FILE_OK) return STATE_FILE_OK;
    /* Never silently recover a snapshot for a different cartridge or core layout. */
    if (primary == STATE_FILE_WRONG_ROM || primary == STATE_FILE_WRONG_SIZE) return primary;
    state_file_status_t backup_status = read_one(backup, rom_crc32, rom_size, payload, exact_payload_size);
    if (backup_status == STATE_FILE_OK) return STATE_FILE_OK;
    if (primary == STATE_FILE_MISSING && backup_status == STATE_FILE_MISSING) return STATE_FILE_MISSING;
    if (primary == STATE_FILE_IO_ERROR || backup_status == STATE_FILE_IO_ERROR) return STATE_FILE_IO_ERROR;
    if (backup_status == STATE_FILE_WRONG_ROM || backup_status == STATE_FILE_WRONG_SIZE) return backup_status;
    return STATE_FILE_INVALID;
}
