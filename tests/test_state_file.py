#!/usr/bin/env python3
"""Host coverage for validated, crash-recoverable emulator state files."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="gb-state-test-") as tmp:
    work = Path(tmp)
    libpath = work / "state_file.so"
    subprocess.run([
        "cc", "-std=c11", "-D_DEFAULT_SOURCE", "-shared", "-fPIC", "-Wall", "-Wextra",
        "-I" + str(root / "src/storage"), str(root / "src/storage/state_file.c"),
        str(root / "src/storage/save_file.c"), "-o", str(libpath),
    ], check=True)
    lib = C.CDLL(str(libpath))
    byte_ptr = C.POINTER(C.c_uint8)
    lib.state_file_crc32.argtypes = [C.c_void_p, C.c_size_t]
    lib.state_file_crc32.restype = C.c_uint32
    lib.state_file_write.argtypes = [C.c_char_p, C.c_uint32, C.c_uint32, byte_ptr, C.c_size_t]
    lib.state_file_write.restype = C.c_int
    lib.state_file_read.argtypes = [C.c_char_p, C.c_uint32, C.c_uint32, byte_ptr, C.c_size_t]
    lib.state_file_read.restype = C.c_int

    OK, MISSING, IO, INVALID, WRONG_ROM, WRONG_SIZE = range(6)
    rom = b"example ROM identity"
    rom_crc = lib.state_file_crc32(rom, len(rom))
    payload_bytes = bytes((i * 37 + 11) & 0xff for i in range(4097))
    payload = (C.c_uint8 * len(payload_bytes)).from_buffer_copy(payload_bytes)
    output = (C.c_uint8 * len(payload_bytes))(*([0xa5] * len(payload_bytes)))
    path = work / "game.gb.state"
    p = str(path).encode()

    assert lib.state_file_read(p, rom_crc, len(rom), output, len(output)) == MISSING
    assert lib.state_file_write(p, rom_crc, len(rom), payload, len(payload)) == OK
    assert lib.state_file_read(p, rom_crc, len(rom), output, len(output)) == OK
    assert bytes(output) == payload_bytes
    assert lib.state_file_read(p, rom_crc ^ 1, len(rom), output, len(output)) == WRONG_ROM
    assert lib.state_file_read(p, rom_crc, len(rom) + 1, output, len(output)) == WRONG_ROM
    assert lib.state_file_read(p, rom_crc, len(rom), output, len(output) - 1) == WRONG_SIZE
    assert lib.state_file_read(p, rom_crc, len(rom), output, len(output) + 1) == WRONG_SIZE

    original = path.read_bytes()
    for bad in (original[:7], original + b"x", original[:24] + original[25:],
                original[:4] + b"\x02\x00" + original[6:]):
        path.write_bytes(bad)
        assert lib.state_file_read(p, rom_crc, len(rom), output, len(output)) == INVALID
        assert bytes(output) == payload_bytes, "rejected file must not partially alter caller state"

    corrupted = bytearray(original)
    corrupted[-1] ^= 1
    path.write_bytes(corrupted)
    assert lib.state_file_read(p, rom_crc, len(rom), output, len(output)) == INVALID

    # A valid backup is used after an interrupted/corrupt primary.
    Path(str(path) + ".bak").write_bytes(original)
    path.write_bytes(b"torn")
    assert lib.state_file_read(p, rom_crc, len(rom), output, len(output)) == OK
    assert bytes(output) == payload_bytes

    # Force the temporary-file open to fail; current and backup remain intact.
    path.write_bytes(original)
    backup = Path(str(path) + ".bak")
    backup.write_bytes(b"prior backup")
    Path(str(path) + ".tmp").mkdir()
    newer = (C.c_uint8 * len(payload_bytes))(*([0x5a] * len(payload_bytes)))
    assert lib.state_file_write(p, rom_crc, len(rom), newer, len(newer)) == IO
    assert path.read_bytes() == original
    assert backup.read_bytes() == b"prior backup"

    assert lib.state_file_write(str(work / "no-such-dir/state").encode(), rom_crc,
                                len(rom), payload, len(payload)) == IO
    assert lib.state_file_write(p, rom_crc, len(rom), payload, 0) == INVALID
    assert lib.state_file_crc32(b"123456789", 9) == 0xcbf43926

print("PASS: state envelope identity, strict reads, checksum, recovery, and write-fault preservation")
