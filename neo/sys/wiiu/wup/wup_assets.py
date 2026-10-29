#!/usr/bin/env python3
# Makes the Wii U Menu files for the installable package (see make-wup.sh):
#   wup_assets.py tga <in.bmp> <out.tga> <24|32>   uncompressed TGA like the system expects
#   wup_assets.py logo <out.tga>                   empty (transparent) 170x42 boot logo
#   wup_assets.py btsnd <out.btsnd>                a moment of silence as the boot sound
# BMP input because macOS' sips can write that from PNG, nothing to install.

import struct
import sys

TGA_FOOTER = b"\0" * 8 + b"TRUEVISION-XFILE.\0"


def read_bmp(path):
	data = open(path, "rb").read()
	if data[:2] != b"BM":
		sys.exit(path + ": not a BMP")
	offset = struct.unpack_from("<I", data, 10)[0]
	width, height = struct.unpack_from("<ii", data, 18)
	bpp = struct.unpack_from("<H", data, 28)[0]
	compression = struct.unpack_from("<I", data, 30)[0]
	if bpp not in (24, 32) or compression not in (0, 3):
		sys.exit(path + ": unsupported BMP (%d bpp, compression %d)" % (bpp, compression))
	top_down = height < 0
	height = abs(height)
	stride = ( width * bpp // 8 + 3 ) & ~3
	rows = []	# top to bottom, each a list of (b, g, r, a)
	for y in range(height):
		src = y if top_down else height - 1 - y
		row = data[offset + src * stride:]
		px = []
		for x in range(width):
			if bpp == 32:
				b, g, r, a = row[x * 4:x * 4 + 4]
			else:
				b, g, r = row[x * 3:x * 3 + 3]
				a = 255
			px.append((b, g, r, a))
		rows.append(px)
	return width, height, rows


def write_tga(path, width, height, rows, bpp):
	# bottom-left origin (and 8 alpha bits for 32 bit), like the TGAs of Nintendo's titles
	descriptor = 0x08 if bpp == 32 else 0x00
	out = bytearray(struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, bpp, descriptor))
	for row in reversed(rows):
		for b, g, r, a in row:
			out += bytes((b, g, r, a)) if bpp == 32 else bytes((b, g, r))
	out += TGA_FOOTER
	open(path, "wb").write(out)


def main():
	cmd = sys.argv[1]
	if cmd == "tga":
		width, height, rows = read_bmp(sys.argv[2])
		write_tga(sys.argv[3], width, height, rows, int(sys.argv[4]))
	elif cmd == "logo":
		write_tga(sys.argv[2], 170, 42, [[(0, 0, 0, 0)] * 170 for _ in range(42)], 32)
	elif cmd == "btsnd":
		# big endian: format (2: 16 bit stereo PCM at 48kHz), loop point, then the samples
		samples = 4800
		open(sys.argv[2], "wb").write(struct.pack(">II", 2, 0) + b"\0" * samples * 4)
	else:
		sys.exit("unknown command " + cmd)


main()
