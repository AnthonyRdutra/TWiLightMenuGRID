#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# deploy_sd.sh -- installs the build onto a real DSi SD card (developer tool):
#   1. Replaces  <SD>/_nds/TWiLightMenu/dsimenu.srldr  with dist/dsimenu.srldr
#      (backing up the previous one as dsimenu.srldr.bak).
#   2. Refreshes the "Default grid theme" folder in the .preview/sdcard checkout from its
#      canonical source (Injector/Default grid theme) -- this is the theme carrying our
#      component-based rendering standard (layout.json for GridView, grf/status_bar.bmp +
#      topscreen_titlebox.bmp/topscreen_startbox.bmp for the top-screen HUD components, the
#      flat battery/ PNG set, etc.). .preview/sdcard is only ever a build/test checkout, not
#      where theme assets are actually edited, so without this step edits made under
#      Injector/ silently never reach either melonDS or a real SD (see FRONTEND.md).
#   3. Updates the card's whole theme folder from our (now-refreshed) preview one
#      (.preview/sdcard/_nds/TWiLightMenu/dsimenu/themes).
#
# This is a developer convenience script: it expects a local build (dist/dsimenu.srldr,
# produced by build.sh) and the .preview/sdcard checkout. End users installing a release
# build should use Injector/deploy.py instead, which also handles game art scraping.
#
# Usage:
#   ./deploy_sd.sh                 # auto-detects the SD card mount point
#   ./deploy_sd.sh /Volumes/OTHER  # point at a mount manually
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRLDR="$ROOT/dist/dsimenu.srldr"
THEMES_SRC="$ROOT/.preview/sdcard/_nds/TWiLightMenu/dsimenu/themes"
GRID_THEME_NAME="Default grid theme"
GRID_THEME_CANONICAL="$ROOT/Injector/$GRID_THEME_NAME"

# ---- locate the SD ----
SD="${1:-}"
if [ -z "$SD" ]; then
	# macOS mount points.
	for cand in /Volumes/DSI /Volumes/DSi /Volumes/DSISD; do
		[ -d "$cand/_nds/TWiLightMenu" ] && SD="$cand" && break
	done
fi
if [ -z "$SD" ]; then
	# Linux auto-mount points (GNOME/KDE typically mount under one of these).
	for base in "/media/${USER:-}" "/run/media/${USER:-}"; do
		[ -d "$base" ] || continue
		for cand in "$base"/*; do
			[ -d "$cand/_nds/TWiLightMenu" ] && SD="$cand" && break 2
		done
	done
fi
if [ -z "$SD" ] || [ ! -d "$SD/_nds/TWiLightMenu" ]; then
	echo "!! DSi SD card not found (looked under /Volumes and /media). Pass the mount point as an argument." >&2
	exit 1
fi
echo ">> SD: $SD"

# ---- validation ----
if [ ! -f "$SRLDR" ]; then
	echo "!! $SRLDR does not exist. Run ./build.sh first." >&2
	exit 1
fi
if [ ! -d "$THEMES_SRC" ]; then
	echo "!! Preview themes folder not found: $THEMES_SRC" >&2
	exit 1
fi

# FAT-friendly rsync (no perms/owner, tolerate a 1s FAT timestamp granularity), falling back to a
# plain recursive copy when rsync isn't available.
sync_dir() {
	local src="$1" dst="$2"
	mkdir -p "$dst"
	if command -v rsync >/dev/null 2>&1; then
		rsync -rt --modify-window=1 --no-perms --no-owner --no-group \
			--exclude='.DS_Store' --exclude='._*' \
			"$src/" "$dst/"
	else
		cp -R "$src/." "$dst/"
	fi
}

# ---- 0) refresh the grid theme from its canonical source (Injector/) into the preview checkout ----
if [ -d "$GRID_THEME_CANONICAL" ]; then
	echo ">> Refreshing '$GRID_THEME_NAME' from Injector/ (our new component/layout.json standard)"
	sync_dir "$GRID_THEME_CANONICAL" "$THEMES_SRC/$GRID_THEME_NAME"
else
	echo "!! Canonical theme not found, skipping refresh: $GRID_THEME_CANONICAL" >&2
fi

# ---- 1) dsimenu.srldr ----
DST_SRLDR="$SD/_nds/TWiLightMenu/dsimenu.srldr"
if [ -f "$DST_SRLDR" ]; then
	cp -f "$DST_SRLDR" "$SD/_nds/TWiLightMenu/dsimenu.srldr.bak"
	echo ">> Backup: dsimenu.srldr.bak"
fi
cp -f "$SRLDR" "$DST_SRLDR"
echo ">> Copied dsimenu.srldr ($(du -h "$SRLDR" | cut -f1))"

# ---- 2) themes folder ----
DST_THEMES="$SD/_nds/TWiLightMenu/dsimenu/themes"
echo ">> Updating themes -> $DST_THEMES"
sync_dir "$THEMES_SRC" "$DST_THEMES"

# ---- flush ----
sync
echo ">> Done. Safely eject the card before removing it:"
echo "   diskutil eject \"$SD\"   # macOS"
echo "   udisksctl unmount -b \"$SD\"   # Linux"
