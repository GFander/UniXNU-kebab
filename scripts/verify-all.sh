#!/usr/bin/env bash
#
# verify-all.sh - prove the kebab-dev tree is self-consistent, without a phone.
#
# Every check below used to be run by hand.  This script runs all of them that
# can run on this machine right now and prints a single table.  Inputs that are
# not here (an IPSW, the extracted userland, the split shared cache) produce
# SKIP with the reason and never fail the run.
#
# The pristine xnu-8796 tree is never touched: the patch series is applied to a
# throwaway clone that is deleted on exit.  The default run needs no network.
#
# Exit status: 0 when nothing that ran failed, 1 when a check that should have
# run failed, 2 on a usage or environment error.

set -u
LC_ALL=C
export LC_ALL

prog=${0##*/}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." 2>/dev/null && pwd) || {
	printf '%s: cannot locate the repository root\n' "$prog" >&2
	exit 2
}
python=${PYTHON:-python3}

verbose=0
fast=0
ipsw_arg=

usage() {
	cat <<EOF
usage: $prog [--fast] [--verbose] [--ipsw PATH] [--help]

  --fast         skip the patch-series apply (the slow check)
  --verbose      echo every command and its output
  --ipsw PATH    local IPSW for the optional userland / APFS checks
  -h, --help     this text

environment:
  KEBAB_XNU            pristine xnu-8796 checkout (default /home/albert/ios-work/xnu8796)
  KEBAB_USERLAND       extracted userland        (default \$HOME/ios-userland)
  KEBAB_CACHE          split cache directory     (default \$KEBAB_USERLAND/cryptex-cache)
  KEBAB_IPSW           same as --ipsw
  KEBAB_SLIDE          slide for check-shared-cache.py (default 0x0)
  KEBAB_PANEL_SOURCE   dsi-panel-*.dtsi to use instead of the cached copy
  KEBAB_MACHO          Mach-O for ios-deps.py instead of the probed one
  PYTHON               python interpreter        (default python3)

Missing optional inputs are reported as SKIP and never make the run fail.
Nothing here writes to the network or to the pristine tree.
EOF
}

die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 2; }

while [ $# -gt 0 ]; do
	case $1 in
	--fast) fast=1 ;;
	--verbose | -v) verbose=1 ;;
	--ipsw) shift; [ $# -gt 0 ] || die "--ipsw needs a path"; ipsw_arg=$1 ;;
	--ipsw=*) ipsw_arg=${1#*=} ;;
	-h | --help) usage; exit 0 ;;
	*) usage >&2; die "unknown option: $1" ;;
	esac
	shift
done

command -v git >/dev/null 2>&1 || die "git not found"
command -v "$python" >/dev/null 2>&1 || die "$python not found"

# /tmp is a quota-limited tmpfs on some hosts: writing check logs there fails with
# exit 120 and looks like five broken checks. Prefer a directory under $HOME.
scratch_root=${TMPDIR:-}
if [ -z "$scratch_root" ] || ! mkdir -p "$scratch_root" 2>/dev/null; then
	scratch_root=$HOME/.cache
	mkdir -p "$scratch_root" || die "cannot create a scratch directory"
fi
tmp=$(mktemp -d "$scratch_root/kebab-verify.XXXXXX") || die "cannot create a scratch directory"
logdir=$tmp/log
mkdir -p "$logdir"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

names=()
results=()
details=()
npass=0
nfail=0
nskip=0

record() { names+=("$1"); results+=("$2"); details+=("$3"); }
pass() { record "$1" PASS "$2"; npass=$((npass + 1)); }
fail() { record "$1" FAIL "$2"; nfail=$((nfail + 1)); }
skip() { record "$1" SKIP "$2"; nskip=$((nskip + 1)); }

note() { printf '%s: %s\n' "$prog" "$*" >&2; }
vnote() { [ "$verbose" -eq 1 ] && printf '%s: %s\n' "$prog" "$*" >&2; return 0; }

# run <check> <command...> - capture stdout+stderr in $logdir/<check>.log
run() {
	local name=$1 log rc
	shift
	log=$logdir/$name.log
	if [ "$verbose" -eq 1 ]; then
		printf '  + %s\n' "$*" >&2
	fi
	"$@" >"$log" 2>&1
	rc=$?
	if [ "$verbose" -eq 1 ] && [ -s "$log" ]; then
		sed 's/^/    | /' "$log" >&2
	fi
	return $rc
}

# trim <string> [width] - first line, printable, shortened, for a table cell
trim() {
	local s
	s=$(head -n 1 <<<"$1" | tr -d '\r\n' | tr -c '[:print:]' '?')
	printf '%s' "${s:0:${2:-88}}"
}

# why <log> <rc> - best one-line explanation of a failure
why() {
	local log=$1 rc=$2 line
	line=$(grep -iE 'error|failed|stale|missing|traceback|not a ' "$log" 2>/dev/null | tail -n 1)
	[ -z "$line" ] && line=$(grep -v '^[[:space:]]*$' "$log" 2>/dev/null | tail -n 1)
	[ -z "$line" ] && line="exit status $rc"
	trim "$line"
}

# ratio <log> - the "N/M" a selftest prints, if any
ratio() {
	local r
	r=$(grep -oE '[0-9]+/[0-9]+' "$1" 2>/dev/null | tail -n 1)
	printf '%s' "${r:-}"
}

# ---------------------------------------------------------------------------
# 1. the patch series

check_patches() {
	local name=patches
	local xnu=${KEBAB_XNU:-/home/albert/ios-work/xnu8796}
	local scratch=$tmp/xnu common total ok fail_at p src elapsed t0 crlf=0 why_txt
	local patches=()

	shopt -s nullglob
	patches=("$repo"/patches/xnu-8796-kebab/*.patch)
	shopt -u nullglob
	total=${#patches[@]}

	if [ "$total" -eq 0 ]; then
		fail $name "no patches under patches/xnu-8796-kebab"
		return
	fi

	if [ ! -d "$xnu" ] || ! git -C "$xnu" rev-parse --git-dir >/dev/null 2>&1; then
		skip $name "no pristine xnu tree at $xnu"
		return
	fi
	if ! git -C "$xnu" rev-parse --verify --quiet '1b191cb^{commit}' >/dev/null 2>&1; then
		skip $name "commit 1b191cb is not in $xnu"
		return
	fi

	# Resolve through the common git dir so a linked worktree (which is what
	# the pristine tree often is) is read, never written.
	common=$(git -C "$xnu" rev-parse --git-common-dir 2>/dev/null) || {
		fail $name "cannot read the git dir of $xnu"
		return
	}
	case $common in /*) ;; *) common=$xnu/$common ;; esac

	note "patch series: applying $total patches to a scratch clone of $common"
	if ! git clone -q --shared --no-checkout "$common" "$scratch" \
		>>"$logdir/$name.log" 2>&1; then
		fail $name "cannot clone $common into a scratch copy"
		return
	fi
	if ! git -C "$scratch" reset -q --hard 1b191cb >>"$logdir/$name.log" 2>&1 ||
		! git -C "$scratch" clean -qfdx >>"$logdir/$name.log" 2>&1; then
		fail $name "cannot reset the scratch copy to 1b191cb"
		return
	fi

	ok=0
	fail_at=
	t0=$(date +%s)
	for p in "${patches[@]}"; do
		src=$p
		# A checkout made on Windows can carry CRLF patches.  git apply would
		# then fail on (or apply CRLF into) an LF tree, so feed it the same
		# bytes the committed blob has.
		if grep -q $'\r' "$p" 2>/dev/null; then
			if ! tr -d '\r' <"$p" >"$tmp/patch.diff"; then
				fail_at=${p##*/}
				break
			fi
			src=$tmp/patch.diff
			crlf=$((crlf + 1))
		fi
		vnote "apply ${p##*/}"
		if git -C "$scratch" apply --check --whitespace=nowarn "$src" \
			>>"$logdir/$name.log" 2>&1 &&
			git -C "$scratch" apply --whitespace=nowarn "$src" \
				>>"$logdir/$name.log" 2>&1; then
			ok=$((ok + 1))
		else
			fail_at=${p##*/}
			break
		fi
	done
	elapsed=$(($(date +%s) - t0))

	if [ -n "$fail_at" ]; then
		fail $name "$ok/$total applied; $fail_at does not apply to 1b191cb"
		return
	fi

	why_txt="$ok/$total patches apply in filename order to 1b191cb (xnu-8796.141.3), ${elapsed}s"
	[ "$crlf" -gt 0 ] && why_txt="$why_txt; CRLF checkout normalised"
	pass $name "$why_txt"
}

# ---------------------------------------------------------------------------
# 2. the committed panel table

check_panel() {
	local name=panel rc args=()
	if [ -n "${KEBAB_PANEL_SOURCE:-}" ]; then
		[ -f "${KEBAB_PANEL_SOURCE}" ] || {
			skip $name "KEBAB_PANEL_SOURCE=$KEBAB_PANEL_SOURCE does not exist"
			return
		}
		args=(--source "$KEBAB_PANEL_SOURCE")
	elif ! ls "$HOME"/.cache/kebab-panel/*.dtsi >/dev/null 2>&1; then
		skip $name "pinned DTS not cached under ~/.cache/kebab-panel"
		return
	fi

	run $name "$python" "$repo/scripts/extract-panel-init.py" "${args[@]+"${args[@]}"}" --check
	rc=$?
	if [ $rc -eq 0 ]; then
		pass $name "committed table and header match the pinned source"
	elif [ $rc -eq 2 ] && grep -qi 'cannot fetch' "$logdir/$name.log"; then
		skip $name "pinned DTS would have to be downloaded"
	else
		fail $name "$(why "$logdir/$name.log" $rc)"
	fi
}

# ---------------------------------------------------------------------------
# 3. the selftests

check_selftest() {
	local name=$1 script=$2 rc r
	run $name "$python" "$repo/scripts/$script" --selftest
	rc=$?
	if [ $rc -ne 0 ]; then
		fail $name "$(why "$logdir/$name.log" $rc)"
		return
	fi
	r=$(ratio "$logdir/$name.log")
	if [ -n "$r" ]; then
		pass $name "$r assertions"
	else
		pass $name "selftest exited 0"
	fi
}

# ---------------------------------------------------------------------------
# 4. the split shared cache

check_shared_cache() {
	local name=shared-cache
	local dir=${KEBAB_CACHE:-${KEBAB_USERLAND:-$HOME/ios-userland}/cryptex-cache}
	local slide=${KEBAB_SLIDE:-0x0} rc n

	if [ ! -d "$dir" ]; then
		skip $name "no split cache at $dir"
		return
	fi
	run $name "$python" "$repo/scripts/check-shared-cache.py" "$dir" --slide "$slide"
	rc=$?
	if [ $rc -eq 0 ]; then
		n=$(find "$dir" -maxdepth 1 -type f 2>/dev/null | wc -l)
		pass $name "$n files, slide $slide, mapping plan accepted"
	else
		fail $name "$(why "$logdir/$name.log" $rc)"
	fi
}

# ---------------------------------------------------------------------------
# 5. the dependency closure

check_ios_deps() {
	local name=ios-deps
	local userland=${KEBAB_USERLAND:-$HOME/ios-userland}
	local cache=${KEBAB_CACHE:-$userland/cryptex-cache}/dyld_shared_cache_arm64
	local macho= f rc images subs

	if [ ! -f "$cache" ]; then
		skip $name "no main shared cache at $cache"
		return
	fi

	macho=${KEBAB_MACHO:-}
	if [ -z "$macho" ]; then
		shopt -s nullglob
		for f in "$userland"/*.macho "$userland"/bin/* "$userland"/usr/bin/*; do
			[ -f "$f" ] && {
				macho=$f
				break
			}
		done
		shopt -u nullglob
	fi

	if [ -n "$macho" ] && [ -f "$macho" ]; then
		run $name "$python" "$repo/scripts/ios-deps.py" "$macho" --cache "$cache"
		rc=$?
		if [ $rc -eq 0 ]; then
			pass $name "closure for ${macho##*/}"
		else
			fail $name "$(why "$logdir/$name.log" $rc)"
		fi
		return
	fi

	# No Mach-O to close over: still validate the cache header and image table,
	# which is what the closure reads.
	run $name "$python" "$repo/scripts/ios-deps.py" --cache "$cache" --cache-info
	rc=$?
	if [ $rc -ne 0 ]; then
		fail $name "$(why "$logdir/$name.log" $rc)"
		return
	fi
	images=$(awk '/image paths/ { print $3; exit }' "$logdir/$name.log")
	subs=$(awk '/subcache array/ { print $3; exit }' "$logdir/$name.log")
	pass $name "cache-info: ${images:-?} images, ${subs:-?} subcaches (no Mach-O to close over)"
}

# ---------------------------------------------------------------------------
# 6. the IPSW-only checks (optional)

ipsw_path() {
	local f
	if [ -n "$ipsw_arg" ]; then
		printf '%s' "$ipsw_arg"
		return
	fi
	if [ -n "${KEBAB_IPSW:-}" ]; then
		printf '%s' "$KEBAB_IPSW"
		return
	fi
	shopt -s nullglob
	local cands=("$HOME"/ipsw/*.ipsw "$HOME"/Downloads/*.ipsw "$HOME"/*.ipsw)
	shopt -u nullglob
	for f in "${cands[@]+"${cands[@]}"}"; do
		[ -f "$f" ] && {
			printf '%s' "$f"
			return
		}
	done
}

check_ipsw_list() {
	local name=ipsw-list ipsw rc n
	ipsw=$(ipsw_path)
	if [ -z "$ipsw" ] || [ ! -f "$ipsw" ]; then
		skip $name "no local IPSW (--ipsw or KEBAB_IPSW)"
		return
	fi
	run $name "$python" "$repo/scripts/extract-ios-userland.py" "$ipsw" --list
	rc=$?
	if [ $rc -ne 0 ]; then
		fail $name "$(why "$logdir/$name.log" $rc)"
		return
	fi
	n=$(grep -cE '^[[:space:]]*[0-9]' "$logdir/$name.log" 2>/dev/null)
	pass $name "${ipsw##*/}: ${n:-?} members listed"
}

check_ipsw_dmg() {
	local name=ipsw-dmg ipsw rc
	ipsw=$(ipsw_path)
	if [ -z "$ipsw" ] || [ ! -f "$ipsw" ]; then
		skip $name "no local IPSW (--ipsw or KEBAB_IPSW)"
		return
	fi
	run $name "$python" "$repo/scripts/extract-ios-userland.py" "$ipsw" --dmg-info
	rc=$?
	if [ $rc -ne 0 ]; then
		fail $name "$(why "$logdir/$name.log" $rc)"
		return
	fi
	pass $name "DMG header parsed from ${ipsw##*/}"
}

check_apfs_known() {
	local name=apfs-known ipsw rc
	ipsw=$(ipsw_path)
	if [ -z "$ipsw" ] || [ ! -f "$ipsw" ]; then
		skip $name "no local IPSW (--ipsw or KEBAB_IPSW)"
		return
	fi
	run $name "$python" "$repo/scripts/probe-ios-apfs.py" --ipsw "$ipsw" --known
	rc=$?
	if [ $rc -ne 0 ]; then
		fail $name "$(why "$logdir/$name.log" $rc)"
		return
	fi
	pass $name "read /usr/lib/dyld and /sbin/launchd inodes"
}

check_apfs_get() {
	local name=apfs-get ipsw rc
	ipsw=$(ipsw_path)
	if [ -z "$ipsw" ] || [ ! -f "$ipsw" ]; then
		skip $name "no local IPSW (--ipsw or KEBAB_IPSW)"
		return
	fi
	run $name "$python" "$repo/scripts/apfs-get.py" --ipsw "$ipsw" \
		--out "$tmp/apfs-get" --get /System/Library/CoreServices/SystemVersion.plist
	rc=$?
	if [ $rc -ne 0 ]; then
		fail $name "$(why "$logdir/$name.log" $rc)"
		return
	fi
	pass $name "extracted SystemVersion.plist from the root volume"
}

# ---------------------------------------------------------------------------
# the run

branch=$(git -C "$repo" rev-parse --abbrev-ref HEAD 2>/dev/null || printf '?')
printf 'verify-all: %s [%s]\n\n' "$repo" "$branch"

if [ "$fast" -eq 1 ]; then
	skip patches "skipped by --fast"
else
	check_patches
fi
check_panel
check_selftest ep0 decode-ep0-probe.py
check_shared_cache
check_selftest kdp kdp-cdc-bridge.py
check_ios_deps
check_ipsw_list
check_ipsw_dmg
check_apfs_known
check_apfs_get

width=5
for n in "${names[@]}"; do
	[ "${#n}" -gt "$width" ] && width=${#n}
done

printf '%-*s  %-6s  %s\n' "$width" "check" "result" "detail"
printf '%-*s  %-6s  %s\n' "$width" "-----" "------" "------"
for i in "${!names[@]}"; do
	printf '%-*s  %-6s  %s\n' "$width" "${names[$i]}" "${results[$i]}" "${details[$i]}"
done

if [ "$nfail" -eq 0 ]; then
	verdict=PASS
else
	verdict=FAIL
fi
printf '\nverdict: %s  (%d passed, %d failed, %d skipped)\n' \
	"$verdict" "$npass" "$nfail" "$nskip"

[ "$nfail" -eq 0 ] && exit 0
exit 1
