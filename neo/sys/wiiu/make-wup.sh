#!/bin/sh
# Packs build-wiiu/dhewm3.rpx into a package that installs Doom 3 as a title on
# the Wii U Menu (with WUP Installer GX2 under Aroma). The game still loads its
# data from sd:/wiiu/apps/dhewm3/ like the .wuhb does.
#
# Usage: make-wup.sh <common key>
#   <common key>: the Wii U common key, either as otp.bin dumped from your console
#   or as a file that holds the key as 32 hex digits. It's needed to encrypt the
#   package's title key and isn't printed or stored anywhere else.
#
# Output: build-wiiu/wup/out, copy it to sd:/install/<some folder>/
set -e
cd "$(dirname "$0")/../../.."

KEYSRC="$1"
if [ -z "$KEYSRC" ] || [ ! -f "$KEYSRC" ]; then
	echo "usage: $0 <otp.bin or file with the common key>"
	exit 1
fi
if [ ! -f build-wiiu/dhewm3.rpx ]; then
	echo "build-wiiu/dhewm3.rpx is missing, build it first (sys/wiiu/build.sh)"
	exit 1
fi

WUP=build-wiiu/wup
SRC=neo/sys/wiiu
NUSPACKER_URL=https://github.com/Maschell/nuspacker/releases/download/0.0.1-SNAPSHOT-fd7cbdb/NUSPacker-0.0.1-SNAPSHOT-nightly-jar-with-dependencies.jar

rm -rf "$WUP/in" "$WUP/out"
mkdir -p "$WUP/in/code" "$WUP/in/meta" "$WUP/in/content" build-wiiu/tools

# code: the program and how the system runs it
cp build-wiiu/dhewm3.rpx "$WUP/in/code/"
cp "$SRC/wup/app.xml" "$SRC/wup/cos.xml" "$WUP/in/code/"

# meta: name, icon and boot screens for the Wii U Menu
cp "$SRC/wup/meta.xml" "$WUP/in/meta/"
TMP="$WUP/tmp"
mkdir -p "$TMP"
sips -s format bmp "$SRC/assets/icon.png" --out "$TMP/icon.bmp" >/dev/null
sips -s format bmp "$SRC/assets/splash_tv.png" --out "$TMP/tv.bmp" >/dev/null
sips -s format bmp "$SRC/assets/splash_drc.png" --out "$TMP/drc.bmp" >/dev/null
python3 -I "$SRC/wup/wup_assets.py" tga "$TMP/icon.bmp" "$WUP/in/meta/iconTex.tga" 32
python3 -I "$SRC/wup/wup_assets.py" tga "$TMP/tv.bmp" "$WUP/in/meta/bootTvTex.tga" 24
python3 -I "$SRC/wup/wup_assets.py" tga "$TMP/drc.bmp" "$WUP/in/meta/bootDrcTex.tga" 24
python3 -I "$SRC/wup/wup_assets.py" logo "$WUP/in/meta/bootLogoTex.tga"
python3 -I "$SRC/wup/wup_assets.py" btsnd "$WUP/in/meta/bootSound.btsnd"
rm -rf "$TMP"

# content: mustn't be empty. The game data stays on the SD card.
echo "Doom 3 (dhewm3) loads its data from sd:/wiiu/apps/dhewm3/base/" > "$WUP/in/content/readme.txt"

if [ ! -f build-wiiu/tools/NUSPacker.jar ]; then
	curl -fL -o build-wiiu/tools/NUSPacker.jar "$NUSPACKER_URL"
fi

# NUSPacker reads the key from a file "encryptKeyWith" in the current directory
KEYFILE="$WUP/encryptKeyWith"
umask 077
if [ "$(wc -c < "$KEYSRC")" -eq 1024 ]; then
	# otp.bin: the common key is the 16 bytes at 0xE0
	dd if="$KEYSRC" bs=1 skip=224 count=16 2>/dev/null | xxd -p | tr -d '\n' > "$KEYFILE"
else
	grep -oE '[0-9a-fA-F]{32}' "$KEYSRC" | head -1 | tr -d '\n' > "$KEYFILE"
fi
if [ "$(wc -c < "$KEYFILE")" -ne 32 ]; then
	rm -f "$KEYFILE"
	echo "couldn't read a common key from $KEYSRC"
	exit 1
fi

# (its output includes the keys, leave those out)
( cd "$WUP" && java -jar ../tools/NUSPacker.jar -in in -out out ) 2>&1 | grep -viE "key" || true
rm -f "$KEYFILE"

if [ ! -f "$WUP/out/title.tmd" ]; then
	echo "packing failed"
	exit 1
fi
echo
echo "Done: $WUP/out - copy it to sd:/install/doom3/ and install it with WUP Installer GX2"
