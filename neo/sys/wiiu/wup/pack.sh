#!/bin/sh
# Packs Doom 3 with its game data into a package that installs as a title on the
# Wii U Menu (with WUP Installer GX2 under Aroma).
#
# 1. Put pak000.pk4 ... pak008.pk4 (Doom 3 patched to 1.3.1) into in/content/base/
# 2. Run: sh pack.sh
#    It asks for the Wii U common key (typing isn't shown) and writes the package
#    to out/. The key is only used for packing and deleted right after.
# 3. Copy out/ to sd:/install/doom3/ and install it with WUP Installer GX2.
#
# The installed game loads the .pk4 files from the title; config, savegames and
# the log stay on the SD card in sd:/wiiu/apps/dhewm3/ (create that folder if it
# doesn't exist, base/gamepad.cfg from the package goes there for the controls).
set -e
cd "$(dirname "$0")"

missing=""
for i in 0 1 2 3 4 5 6 7 8; do
	if [ ! -f "in/content/base/pak00$i.pk4" ]; then
		missing="$missing pak00$i.pk4"
	fi
done
if [ -n "$missing" ]; then
	echo "Missing in in/content/base/:$missing"
	exit 1
fi

printf "Wii U common key (32 hex digits, not shown): "
stty -echo 2>/dev/null || true
read KEY
stty echo 2>/dev/null || true
echo
if ! echo "$KEY" | grep -qE '^[0-9a-fA-F]{32}$'; then
	echo "That isn't 32 hex digits."
	exit 1
fi

rm -rf out tmp output
umask 077
printf '%s' "$KEY" > encryptKeyWith
KEY=""
trap 'rm -f encryptKeyWith' EXIT
# NUSPacker prints the keys, leave those lines out
java -jar tools/NUSPacker.jar -in in -out out 2>&1 | grep -viE "key" || true
rm -f encryptKeyWith
rm -rf tmp output

if [ ! -f out/title.tmd ]; then
	echo "Packing failed."
	exit 1
fi
chmod -R a+rX out
echo
echo "Done. Copy the out folder to sd:/install/doom3/ and install it with WUP Installer GX2."
