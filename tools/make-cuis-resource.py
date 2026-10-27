#!/usr/bin/env python3
"""Build a small real big-endian GEM resource used by host and x32 tests."""
from pathlib import Path
import struct
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", type=Path)
parser.add_argument("--application-directory", action="store_true")
parser.add_argument("--colour-icons", action="store_true")
args = parser.parse_args()

image = bytearray(36)

def reserve(size):
    offset = len(image)
    image.extend(bytes(size))
    return offset

def data(value):
    offset = len(image)
    image.extend(value)
    return offset

def word(offset, value):
    struct.pack_into(">H", image, offset, value & 0xffff)

def long(offset, value):
    struct.pack_into(">I", image, offset, value & 0xffffffff)

objects = reserve(8*24)
ted = reserve(28)
icon = reserve(34)
bit = reserve(14)
trees = reserve(4)
strings = reserve(8)
images = reserve(8)
text = data(b"abc\0\0\0\0")
template = data(b"______\0")
valid = data(b"F\0")
label = data(b"Alias\0")
button = data(b"OK\0")
free1 = data(b"Free one\0")
free2 = data(b"Free two\0")
icon_label = data(b"X\0")
if len(image) & 1:
    data(b"\0")
bitmap = data(struct.pack(">H", 0xa55a))
mask = data(struct.pack(">H", 0xffff))
if len(image) & 3:
    data(bytes(4-(len(image) & 3)))
indirect = data(struct.pack(">I", label))
# A pointer-sized native write must not overwrite this adjacent string.
adjacent = data(b"Intact\0")
user = data(struct.pack(">II", 0xdeadbeef, 0x12345678))

for index, value in enumerate((0,objects,ted,icon,bit,strings,text,bitmap,images,trees,
                               8,1,1,1,1,2,2,len(image))):
    word(index*2,value)
types = [25,29,28,28,23,31,24,26]
specs = [0,ted,indirect,indirect,bit,icon,user,button]
for index in range(8):
    offset = objects+index*24
    flags = 0x100 if index in (2,3) else 0
    if index == 1:
        flags |= 8
    if index == 7:
        flags |= 0x27
    values = (-1 if index == 0 else index+1 if index < 7 else 0,
              1 if index == 0 else -1, 7 if index == 0 else -1,
              types[index], flags, 0)
    for field, value in enumerate(values):
        word(offset+field*2,value)
    long(offset+12,specs[index])
    for field, value in enumerate((0 if index == 0 else 0xfe02,0 if index == 0 else index,
                                   20 if index == 0 else 6,8 if index == 0 else 1)):
        word(offset+16+field*2,value)
for offset,value in ((ted,text),(ted+4,template),(ted+8,valid),(icon,mask),(icon+4,bitmap),
                     (icon+8,icon_label),(bit,bitmap),(trees,objects),(strings,free1),
                     (strings+4,free2),(images,bit),(images+4,bit)):
    long(offset,value)
for offset,value in ((ted+12,3),(ted+18,0x1100),(ted+24,7),(ted+26,7),
                     (icon+22,16),(icon+24,1),(icon+30,8),(icon+32,1),
                     (bit+4,2),(bit+6,1),(bit+12,1)):
    word(offset,value)
# Retain the adjacent raw string as the second free string.
long(strings+4,adjacent)
if args.application_directory:
    image = image.replace(b"Free one\0", b"App file\0")
if args.colour_icons:
    # Extended RSC: one shared CICONBLK with 2- and 4-plane variants.
    while len(image) & 3:
        data(b"\0")
    extension = reserve(12)
    table = reserve(8)
    block = reserve(38)
    long(table, block)
    long(table+4, 0xffffffff)
    word(block+12, 0x2400)
    word(block+22, 16)
    word(block+24, 4)
    long(block+34, 2)
    data(struct.pack(">4H", *([0xa000]*4)))
    data(struct.pack(">4H", *([0xffff]*4)))
    data(bytes(12))
    for planes in (2, 4):
        header = reserve(22)
        word(header, planes)
        long(header+10, planes == 4)
        long(header+18, planes == 2)
        values = [0]*8 if planes == 2 else [v for v in (0x5555,0x3333,0x0f0f,0x00ff) for _ in range(4)]
        data(struct.pack(">"+str(len(values))+"H", *values))
        data(struct.pack(">4H", *([0xffff]*4)))
        if planes == 4:
            data(struct.pack(">16H", *[v^0xffff for v in values]))
            data(struct.pack(">4H", *([0xffff]*4)))
    word(0, 4)
    word(34, extension)
    long(extension, len(image))
    long(extension+4, table)
    word(objects+2*24+6, 33)
    word(objects+3*24+6, 33)
    long(indirect, 0)
args.output.write_bytes(image)
