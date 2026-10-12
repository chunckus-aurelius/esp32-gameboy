#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STATE_FILE_MAX_PAYLOAD (1024u * 1024u)

typedef enum {
    STATE_FILE_OK = 0,
    STATE_FILE_MISSING,
    STATE_FILE_IO_ERROR,
    STATE_FILE_INVALID,
    STATE_FILE_WRONG_ROM,
    STATE_FILE_WRONG_SIZE,
} state_file_status_t;

/* CRC-32/ISO-HDLC, suitable for a stable ROM identity across targets. */
uint32_t state_file_crc32(const void *data, size_t size);

state_file_status_t state_file_write(const char *path, uint32_t rom_crc32,
                                     uint32_t rom_size, const uint8_t *payload,
                                     size_t payload_size);
state_file_status_t state_file_read(const char *path, uint32_t rom_crc32,
                                    uint32_t rom_size, uint8_t *payload,
                                    size_t exact_payload_size);
