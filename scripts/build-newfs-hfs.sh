#!/bin/bash
# Build newfs_hfs, the tool that creates HFS+ volumes, without root.
#
# The kernel cannot mount a root filesystem that does not exist yet, and this
# host has no hfsprogs package installed (no sudo). Debian's hfsprogs source
# is Apple's diskdev_cmds with the Linux portability patches already folded
# in, so building it in the home directory is enough.
#
#   scripts/build-newfs-hfs.sh            -> ~/.local/share/kebab-tools/newfs_hfs
#
# Note: this creates volumes; it cannot put files inside one. Populating an
# image needs a mount, which needs root or a macOS runner, so the image the
# kernel actually boots from is built in CI (see docs/FIRMWARE.md).
set -euo pipefail

VER=540.1.linux3
DEBVER=6build1
BASE="${KEBAB_TOOLS:-$HOME/.local/share/kebab-tools}"
WORK="${TMPDIR:-$HOME/tmp}/hfsprogs-build"

mkdir -p "$BASE" "$WORK"
cd "$WORK"

if [ ! -f "hfsprogs_${VER}.orig.tar.gz" ]; then
	echo "== fetching hfsprogs sources =="
	curl -sSL -O "http://archive.ubuntu.com/ubuntu/pool/universe/h/hfsprogs/hfsprogs_${VER}.orig.tar.gz"
	curl -sSL -O "http://archive.ubuntu.com/ubuntu/pool/universe/h/hfsprogs/hfsprogs_${VER}-${DEBVER}.debian.tar.xz"
fi

rm -rf "diskdev_cmds-${VER}" debian
tar xzf "hfsprogs_${VER}.orig.tar.gz"
tar xf "hfsprogs_${VER}-${DEBVER}.debian.tar.xz"

echo "== applying the Debian portability patches =="
for p in debian/patches/*.patch; do
	[ -f "$p" ] || continue
	( cd "diskdev_cmds-${VER}" && patch -p1 -s < "../$p" )
done

echo "== building =="
# The build writes to $TMPDIR; /tmp is often a small tmpfs on these hosts.
export TMPDIR="$WORK/tmp"
mkdir -p "$TMPDIR"
( cd "diskdev_cmds-${VER}" && make -s )

install -m 0755 "diskdev_cmds-${VER}/newfs_hfs.tproj/newfs_hfs" "$BASE/newfs_hfs"
install -m 0755 "diskdev_cmds-${VER}/fsck_hfs.tproj/fsck_hfs" "$BASE/fsck_hfs"
echo "== installed =="
"$BASE/newfs_hfs" 2>&1 | head -2
echo
echo "create a volume with:"
echo "  dd if=/dev/zero of=root.img bs=1M count=64 status=none"
echo "  $BASE/newfs_hfs -v KebabRoot -J root.img"
