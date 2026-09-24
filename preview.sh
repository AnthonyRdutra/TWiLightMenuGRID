#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# preview.sh -- compiles the frontend (romsel_dsimenutheme), copies it to the
# preview SD, and (re)launches melonDS with our build.
#
# Usage:
#   ./preview.sh              # compile, copy and launch  (default)
#   ./preview.sh --no-build   # skip compiling, just copy the current build and launch
#   ./preview.sh --launch     # just relaunch melonDS (no compile, no copy)
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

NDS="$ROOT/romsel_dsimenutheme/romsel_dsimenutheme.nds"
SD="$ROOT/.preview/sdcard"
MELONDS="/Applications/melonDS.app/Contents/MacOS/melonDS"
MELONDS_CFG="$HOME/Library/Preferences/melonDS/melonDS.toml"
DLDI_IMAGE="$ROOT/.preview/dldi.bin"
MODE="${1:-}"

if [ ! -x "$MELONDS" ]; then
	echo "!! melonDS not found at: $MELONDS" >&2
	exit 1
fi

if [ "$MODE" != "--launch" ]; then
	if [ "$MODE" != "--no-build" ]; then
		# Ensures the build docker image exists (the same one compile_docker.sh uses).
		if ! docker image inspect twilightmenu >/dev/null 2>&1; then
			echo ">> Docker image 'twilightmenu' missing; building it (this can take a while)..."
			docker build -t twilightmenu --label twilightmenu ./
		fi
		echo ">> Compiling romsel_dsimenutheme..."
		docker run --rm -v "$ROOT:/data" twilightmenu make romsel_dsimenutheme
	fi

	echo ">> Copying the build to the preview SD..."
	cp "$NDS" "$SD/dsimenu.nds"
	cp "$NDS" "$SD/_nds/TWiLightMenu/dsimenu.srldr"

	# melonDS only attaches a virtual SD card (DLDI) to a cart it detects as homebrew
	# (NDSHeader::IsHomebrew(): ARM9ROMOffset < 0x4000, OR GameCode == "####"). This build is a
	# DSi-enhanced title with a real game code ("SRLA") and, like basically every modern
	# devkitARM/ndstool output, ARM9ROMOffset == 0x4000 exactly -- one past melonDS's threshold --
	# so melonDS loads it as a plain (non-homebrew) cart and never gives it FAT access at all,
	# regardless of the DLDI settings below. Guest-side this is `sys().fatInitOk()` failing in
	# universal/arm9/source/mainAll.cpp, printing "FAT init failed!" and hanging.
	# Faking the GameCode as "####" (bytes 0x0C-0x0F of the header) makes melonDS's homebrew check
	# pass, so it patches its own DLDI driver into the ROM and FAT init succeeds. This is only
	# safe to do on this preview-only "dsimenu.nds" copy (used solely for melonDS's "Open ROM"),
	# never on "dsimenu.srldr" -- that copy sits in the real SD layout and stays byte-identical to
	# the real build in case this SD card is ever used on actual hardware.
	printf '####' | dd of="$SD/dsimenu.nds" bs=1 seek=12 count=4 conv=notrunc status=none
fi

echo ">> (Re)launching melonDS..."
pkill -x melonDS 2>/dev/null || true
sleep 1

# Ensures melonDS's [DLDI] config points at our preview SD folder, so the (now homebrew-flagged)
# ROM above actually gets a working "fat:" filesystem instead of just being *eligible* for one.
# Idempotent text surgery on just the [DLDI] section -- avoids round-tripping the whole TOML file
# through a generic parser/writer, which risks melonDS-specific formatting it doesn't expect.
python3 - "$MELONDS_CFG" "$DLDI_IMAGE" "$SD" <<'PYEOF'
import re, sys
cfg_path, image_path, folder_path = sys.argv[1:4]
wanted = {
	"Enable": "true",
	"ImagePath": f'"{image_path}"',
	"ImageSize": "0",  # Auto -- sized from the synced folder's contents
	"ReadOnly": "false",
	"FolderSync": "true",
	"FolderPath": f'"{folder_path}"',
}
try:
	with open(cfg_path) as f:
		lines = f.readlines()
except FileNotFoundError:
	lines = []

def section_bounds(lines, name):
	start = next((i for i, l in enumerate(lines) if l.strip() == f"[{name}]"), None)
	if start is None:
		return None, None
	end = next((i for i in range(start + 1, len(lines)) if re.match(r"^\[", lines[i].strip())), len(lines))
	return start, end

start, end = section_bounds(lines, "DLDI")
if start is None:
	lines += (["\n"] if lines and lines[-1].strip() else []) + ["[DLDI]\n"]
	start, end = len(lines) - 1, len(lines)

section = lines[start + 1:end]
seen = set()
for i, line in enumerate(section):
	m = re.match(r"^(\w+)\s*=", line)
	if m and m.group(1) in wanted:
		section[i] = f"{m.group(1)} = {wanted[m.group(1)]}\n"
		seen.add(m.group(1))
for key, val in wanted.items():
	if key not in seen:
		section.append(f"{key} = {val}\n")

lines[start + 1:end] = section
with open(cfg_path, "w") as f:
	f.writelines(lines)
PYEOF

# Clears melonDS's SD/DLDI folder-sync caches. They regenerate from the real folder on next boot,
# and clearing them avoids a known melonDS issue where a stale/large sync image eventually causes
# a crash or white screen on boot -- worth the resync cost (folder is a few GB) for reliability.
rm -f "$HOME/Library/Preferences/melonDS/dsisd.bin" "$HOME/Library/Preferences/melonDS/dsisd.bin.idx"
rm -f "$DLDI_IMAGE" "$DLDI_IMAGE.idx"
# NOT pagefile.sys: unlike the DLDI/DSi-SD sync caches above, it doesn't have a known growth/
# corruption issue -- deleting it just forces nds-bootstrap to recreate it (and print "Creating
# pagefile.sys...") on every single relaunch, which is exactly the repeated message this comment
# used to cause. Left alone, it's created once and reused across boots, like on a real SD card.
rm -f "$SD/NDSBTSRP.LOG"

"$MELONDS" "$SD/dsimenu.nds" >/tmp/melonds_preview.log 2>&1 &
echo ">> melonDS started (PID $!). Log: /tmp/melonds_preview.log"
