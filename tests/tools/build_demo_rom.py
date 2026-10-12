#!/usr/bin/env python3
"""Build an original ROM-only DMG animation; no external assembler or assets.

The ROM maintains a VBlank counter at C000. Header logo bytes are zero: tests
start through the emulator's post-boot initialization.
Original code/artwork in this generator and its generated ROM are CC0-1.0.
"""
from pathlib import Path
import argparse
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--check',action='store_true');args=parser.parse_args()
pixels=[]
for y in range(256):
 row=[]
 for x in range(256):
  top=64+((x%96)-48)**2//70
  shade=0
  if y>=top:shade=1
  if y>=100+((x%64)-32)**2//70:shade=2
  if y>=128:shade=3 if y%8==0 or (x+(8 if y//8%2 else 0))%16==0 else 2
  dx=x%48-24;dy=y-48
  if 10<=dx*dx+dy*dy<=24:shade=3
  # Four shade swatches, including native intermediate shades.
  if 8<=y<24 and 16<=x<144:shade=(x-16)//32
  row.append(shade)
 pixels.append(row)
tiles=[];tile_ids={};background=[]
def pack_tile(rows):
 out=[]
 for row in rows:
  out += [sum((v&1)<<(7-x) for x,v in enumerate(row)),sum(((v>>1)&1)<<(7-x) for x,v in enumerate(row))]
 return bytes(out)
for ty in range(32):
 for tx in range(32):
  tile=pack_tile([row[tx*8:tx*8+8] for row in pixels[ty*8:ty*8+8]])
  if tile not in tile_ids:tile_ids[tile]=len(tiles);tiles.append(tile)
  background.append(tile_ids[tile])
if len(tiles)%2:tiles.append(bytes(16))
sprite_index=len(tiles)
# 16x16 robot as two 8x16 sprites; color 0 is transparent.
robot=[]
for y in range(16):
 row=[]
 for x in range(16):
  shape=(2<=x<=13 and 1<=y<=8) or (4<=x<=11 and 9<=y<=12) or (y>=13 and (3<=x<=6 or 9<=x<=12))
  v=3 if shape else 0
  if y in (3,4) and x in (5,6,9,10):v=1
  if 9<=y<=11 and 6<=x<=9:v=2
  row.append(v)
 robot.append(row)
for half in (0,8):
 for top in (0,8):tiles.append(pack_tile([r[half:half+8] for r in robot[top:top+8]]))
assert len(tiles)<=256
rom=bytearray(32768);pc=0x150;labels={};fixups=[]
def emit(*values):
 global pc
 rom[pc:pc+len(values)]=bytes(values);pc+=len(values)
def label(name):labels[name]=pc
def word(op,value):emit(op,value&255,value>>8)
def ref(op,name):
 emit(op);fixups.append((pc,name,False));emit(0,0)
def jr(op,name):
 emit(op);fixups.append((pc,name,True));emit(0)
rom[0x100:0x104]=bytes([0,0xc3,0x50,1])
rom[0x134:0x13c]=b'EINKDEMO'
rom[0x14b]=0x33
# DI; SP=FFFE; interrupts, LCD and audio off; clear VBlank counter.
emit(0xf3);word(0x31,0xfffe);emit(0xaf);word(0xea,0xffff);emit(0xe0,0x40,0xe0,0x26);word(0xea,0xc000)
# Copy tile graphics and tile map into VRAM while LCD is disabled.
ref(0x21,'tiles');word(0x11,0x8000);word(0x01,len(tiles)*16);ref(0xcd,'copy')
ref(0x21,'map');word(0x11,0x9800);word(0x01,1024);ref(0xcd,'copy')
# Clear OAM, then install the two halves of the robot.
word(0x21,0xfe00);emit(0x06,160,0xaf);label('clear_oam');emit(0x22,0x05);jr(0x20,'clear_oam')
ref(0x21,'objects');word(0x11,0xfe00);word(0x01,8);ref(0xcd,'copy')
emit(0x3e,0xe4,0xe0,0x47,0xe0,0x48) # BGP/OBP0 identity palette
emit(0xaf,0xe0,0x42,0xe0,0x43) # SCY/SCX = 0
emit(0x3e,0x97,0xe0,0x40) # LCD, unsigned BG tiles, 8x16 objects, BG enabled
label('leave_vblank');emit(0xf0,0x44,0xfe,144);jr(0x30,'leave_vblank') # while LY>=144
label('wait_vblank');emit(0xf0,0x44,0xfe,144);jr(0x38,'wait_vblank') # while LY<144
word(0x21,0xc000);emit(0x34,0x7e) # ++VBlank counter
emit(0x0f,0xe6,31,0xe0,0x43) # SCX = (counter/2)&31
word(0xfa,0xc000);emit(0xe6,63,0xc6,32);word(0xea,0xfe01);emit(0xc6,8);word(0xea,0xfe05)
word(0xfa,0xc000);emit(0xe6,15,0xc6,96);word(0xea,0xfe00);word(0xea,0xfe04)
ref(0xc3,'leave_vblank')
label('copy');emit(0x2a,0x12,0x13,0x0b,0x78,0xb1);jr(0x20,'copy');emit(0xc9)
label('tiles')
for tile in tiles:emit(*tile)
label('map');emit(*background)
label('objects');emit(96,32,sprite_index,0,96,40,sprite_index+2,0)
for at,name,relative in fixups:
 value=labels[name]-(at+1) if relative else labels[name]
 if relative:assert -128<=value<=127;rom[at]=value&255
 else:rom[at:at+2]=value.to_bytes(2,'little')
checksum=0
for b in rom[0x134:0x14d]:checksum=(checksum-b-1)&255
rom[0x14d]=checksum
checksum=sum(rom)&65535;rom[0x14e:0x150]=checksum.to_bytes(2,'big')
outputs={root/'tests/fixtures/eink_demo.gb':bytes(rom)}
for path,data in outputs.items():
 if args.check:assert path.read_bytes()==data,f'Stale generated file: {path}'
 else:path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
print(f'Demo ROM: {len(rom)} bytes, {len(tiles)} tiles, program/data ends at {pc:04x}; '+('verified' if args.check else 'written'))
