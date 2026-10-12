#!/usr/bin/env python3
"""Scan a real temporary card directory and verify per-ROM save isolation."""
from pathlib import Path
import ctypes as C, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
class Entry(C.Structure):
    _fields_=[('name',C.c_char*256)]
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)
    subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror',
        str(root/'src/storage/game_catalog.c'),'-o',str(p/'catalog.so')],check=True)
    lib=C.CDLL(str(p/'catalog.so'))
    lib.game_catalog_scan.argtypes=[C.c_char_p,C.POINTER(Entry),C.c_size_t,C.POINTER(C.c_size_t),C.POINTER(C.c_bool)]
    lib.game_catalog_scan.restype=C.c_bool
    lib.game_catalog_paths.argtypes=[C.c_char_p,C.POINTER(Entry),C.c_char_p,C.c_char_p]
    lib.game_catalog_paths.restype=C.c_bool
    card=p/'roms';card.mkdir()
    for name in ['z.GB','Big2Small.gb','Big2Small.gbc','a.gb','ignore.sav','game.gb.bak','.hidden.gb','._Apple.gb']:
        (card/name).write_bytes(b'test')
    (card/'folder.gb').mkdir()
    (card/('x'*248+'.gb')).write_bytes(b'test')
    entries=(Entry*64)();count=C.c_size_t();truncated=C.c_bool()
    assert lib.game_catalog_scan(bytes(card),entries,64,C.byref(count),C.byref(truncated))
    assert [entries[i].name for i in range(count.value)]==[b'a.gb',b'Big2Small.gb',b'Big2Small.gbc',b'z.GB']
    assert not truncated.value
    saves=[]
    for i in range(count.value):
        rom=C.create_string_buffer(288);save=C.create_string_buffer(288)
        assert lib.game_catalog_paths(bytes(card),C.byref(entries[i]),rom,save)
        assert Path(rom.value.decode()).exists()
        saves.append(save.value)
    assert len(set(saves))==4
    assert saves[1].endswith(b'Big2Small.sav') and saves[2].endswith(b'Big2Small.gbc.sav')
    assert lib.game_catalog_scan(bytes(card),entries,2,C.byref(count),C.byref(truncated))
    assert count.value==2 and truncated.value
    for name in [b'../bad.gb',b'/bad.gb',b'a\\bad.gb',b'.gb',b'game.sav',b'x'*255]:
        entry=Entry(name);assert not lib.game_catalog_paths(b'/sdcard/roms',C.byref(entry),rom,save)
    entry=Entry(b'long.gb')
    assert not lib.game_catalog_paths(b'x'*280,C.byref(entry),rom,save)
    assert not lib.game_catalog_scan(bytes(p/'missing'),entries,64,C.byref(count),C.byref(truncated))
print('PASS: sorted catalog, case-insensitive extensions, hidden/directory filtering, truncation, path bounds and separate saves')
