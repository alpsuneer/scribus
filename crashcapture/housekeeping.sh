#!/bin/bash
# housekeeping.sh — bounded retention for ~/scribus-crashlogs.
#
# The folder mixes two very different things:
#   * throwaway evidence — cores, session logs, screenshots, captured PostScript
#   * the harness itself — battery scripts, patches, bug reports, test fixtures
# Only the first kind is ever deleted, and only by the explicit rules below.
# Anything this script does not positively recognise as throwaway is KEPT.
#
# Policy
#   1. Age    — throwaway older than RETAIN_DAYS is deleted.
#   2. Size   — if the folder still exceeds MAX_BYTES of real disk, keep deleting
#               oldest-first among the throwaway set, cores first (they are the
#               bulk), until it fits. Nothing outside the throwaway set is ever
#               touched, so the cap can be missed rather than violate rule 3.
#   3. Never  — .sh .py .patch .md .sla .xml anywhere; crash-*.log anywhere;
#               anything with a #! line; the stub lpr/lpoptions harness;
#               everything under repro/; this script and its log; any directory
#               containing a .keep file.
#
# Usage
#   housekeeping.sh              dry run — print what would go, delete nothing
#   housekeeping.sh --apply      actually delete, append to housekeeping.log
#   housekeeping.sh --report     full keep/delete classification of every file
#
# Sizes are real disk blocks (stat %b * 512), not apparent size: the biggest
# core here is 2.0 GB apparent but 277 MB on disk because it is sparse.

set -u

# The three env overrides exist so the policy can be exercised against a
# throwaway fixture directory without touching real evidence or having to
# manufacture 2 GB of it. Normal runs set none of them.
ROOT="${SCRIBUS_CRASHLOGS:-$HOME/scribus-crashlogs}"
RETAIN_DAYS="${SCRIBUS_RETAIN_DAYS:-7}"
MAX_BYTES="${SCRIBUS_MAX_BYTES:-$((2 * 1024 * 1024 * 1024))}"   # 2 GiB
LOGFILE="$ROOT/housekeeping.log"
LOG_MAX=$((128 * 1024))                  # rotate at 128 KB, one .1 kept => 256 KB ceiling

APPLY=0
REPORT=0
for a in "$@"; do
	case "$a" in
		--apply)  APPLY=1 ;;
		--report) REPORT=1 ;;
		--dry-run|-n) APPLY=0 ;;
		*) echo "unknown option: $a" >&2; exit 2 ;;
	esac
done

[ -d "$ROOT" ] || { echo "no such directory: $ROOT" >&2; exit 1; }
cd "$ROOT" || exit 1

NOW=$(date +%s)
CUTOFF=$((NOW - RETAIN_DAYS * 86400))

# ---------------------------------------------------------------- keep markers
# Any directory holding a .keep file is protected wholesale, subdirectories too.
KEEPDIRS=()
while IFS= read -r -d '' k; do
	KEEPDIRS+=("${k%/.keep}")
done < <(find . -name .keep -printf '%P\0' 2>/dev/null)

under_keepdir() {
	local p="$1" d
	for d in ${KEEPDIRS+"${KEEPDIRS[@]}"}; do
		[ -z "$d" ] && return 0                       # .keep at the root
		case "$p" in "$d"/*) return 0 ;; esac
	done
	return 1
}

has_shebang() {
	[ "$(head -c2 -- "$1" 2>/dev/null)" = '#!' ]
}

# ------------------------------------------------------------------ classifier
# Sets CLASS to a throwaway class name, or empty when the file must be kept.
# REASON carries the human-readable justification either way.
CLASS=""; REASON=""
classify() {
	local p="$1" base ext
	base="${p##*/}"
	ext="${base##*.}"
	[ "$ext" = "$base" ] && ext=""
	CLASS=""; REASON=""

	# --- protections, highest precedence first -------------------------------
	case "$p" in
		housekeeping.sh|housekeeping.log|housekeeping.log.[0-9])
			REASON="retention tool"; return ;;
	esac
	if under_keepdir "$p"; then REASON=".keep marker"; return; fi

	# Crash backtraces are kept forever: 30 KB each, and they are the actual
	# diagnostic evidence — the cores they pair with are the bulk and those do
	# expire. scribus-debug writes them to the top level for exactly this
	# reason, but the check sits above the runs/ rule as well so that one
	# landing in a dated folder still cannot be aged out.
	case "$base" in
		crash-*.log) REASON="crash backtrace — kept forever"; return ;;
	esac

	# Dated run folders are throwaway by construction — that is the contract of
	# runs/<date>/, and it is why batteries write there. Checked before the
	# extension protections so a stray screenshot cannot pin a whole folder.
	case "$p" in
		runs/*)
			CLASS="run"
			REASON="dated run folder"
			return ;;
	esac

	case "$ext" in
		sh|py|patch|md|sla|xml)
			REASON="protected extension .$ext"; return ;;
	esac
	case "$base" in
		lpr|lpoptions) REASON="stub print harness"; return ;;
	esac
	case "$p" in
		repro/*) REASON="repro/ is protected"; return ;;
	esac
	if has_shebang "$p"; then REASON="executable script (#!)"; return; fi

	# --- throwaway classes ---------------------------------------------------
	case "$p" in
		core-*|*/core-*)
			CLASS="core"; REASON="core file"; return ;;
		session-*.log)
			CLASS="log"; REASON="uneventful session log"; return ;;
		battery-out/*.txt)
			CLASS="battery"; REASON="battery output"; return ;;
	esac
	case "$p" in
		tabletest/*home*/*|tabletest/pd_[a-z]*/*)
			# Every one of these dirs is rm -rf'd and rebuilt by its own script.
			CLASS="testdir"; REASON="disposable test dir (script recreates it)"; return ;;
	esac
	case "$p" in
		tabletest/*)
			case "$ext" in
				png|eps|ps|pdf|log|txt)
					CLASS="testout"; REASON="test output .$ext"; return ;;
			esac ;;
	esac

	REASON="unrecognised — kept by default"
}

# ------------------------------------------------------------------- inventory
# Parallel arrays: path, class (empty = keep), reason, mtime, disk bytes.
P=(); C=(); R=(); M=(); S=()
total_disk=0; keep_n=0; del_n=0; keep_b=0; del_b=0

while IFS=$'\t' read -r mtime blocks path; do
	bytes=$((blocks * 512))
	total_disk=$((total_disk + bytes))
	classify "$path"
	P+=("$path"); C+=("$CLASS"); R+=("$REASON"); M+=("$mtime"); S+=("$bytes")
	if [ -n "$CLASS" ]; then
		del_n=$((del_n + 1)); del_b=$((del_b + bytes))
	else
		keep_n=$((keep_n + 1)); keep_b=$((keep_b + bytes))
	fi
done < <(find . -type f -printf '%T@\t%b\t%P\n' 2>/dev/null | sed 's/\.[0-9]*\t/\t/')

human() { numfmt --to=iec --suffix=B --format='%.1f' "$1" 2>/dev/null || echo "$1"; }

# --------------------------------------------------------------------- report
if [ "$REPORT" = 1 ]; then
	printf '%-9s %-10s %10s  %-40s %s\n' VERDICT CLASS SIZE PATH REASON
	for i in "${!P[@]}"; do
		if [ -n "${C[$i]}" ]; then v=DELETABLE; else v=KEEP; fi
		printf '%-9s %-10s %10s  %-40s %s\n' \
			"$v" "${C[$i]:--}" "$(human "${S[$i]}")" "${P[$i]}" "${R[$i]}"
	done | sort
	echo
	echo "keep      : $keep_n files, $(human $keep_b)"
	echo "deletable : $del_n files, $(human $del_b)"
	echo "total     : $(human $total_disk) on disk  (cap $(human $MAX_BYTES))"
	exit 0
fi

# -------------------------------------------------------------- selection pass
# Round 1: everything throwaway and older than the cutoff.
DOOMED=(); freed=0
for i in "${!P[@]}"; do
	[ -n "${C[$i]}" ] || continue
	age_ref=${M[$i]}
	# For runs/<YYYY-MM-DD>/ the folder's own date decides, not file mtime, so
	# that reading or touching an old run cannot resurrect it.
	case "${P[$i]}" in
		runs/*)
			d="${P[$i]#runs/}"; d="${d%%/*}"
			if [[ "$d" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]]; then
				folder_ts=$(date -d "$d" +%s 2>/dev/null) && age_ref=$folder_ts
			fi ;;
	esac
	if [ "${age_ref%.*}" -lt "$CUTOFF" ]; then
		DOOMED+=("$i"); freed=$((freed + ${S[$i]}))
	fi
done

# Round 2: still over the cap? Take more throwaway, cores first, oldest first.
remaining=$((total_disk - freed))
if [ "$remaining" -gt "$MAX_BYTES" ]; then
	declare -A doomed_set=()
	for i in ${DOOMED+"${DOOMED[@]}"}; do doomed_set[$i]=1; done
	while IFS=$'\t' read -r _prio _mt i; do
		[ "$remaining" -le "$MAX_BYTES" ] && break
		[ -n "${doomed_set[$i]:-}" ] && continue
		DOOMED+=("$i"); doomed_set[$i]=1
		remaining=$((remaining - ${S[$i]}))
	done < <(
		for i in "${!P[@]}"; do
			[ -n "${C[$i]}" ] || continue
			if [ "${C[$i]}" = core ]; then prio=0; else prio=1; fi
			printf '%s\t%s\t%s\n' "$prio" "${M[$i]}" "$i"
		done | sort -t$'\t' -k1,1n -k2,2n
	)
fi

# ------------------------------------------------------------------- execution
stamp=$(date -Is)
n=0; bytes=0; failed=0
lines=()
for i in ${DOOMED+"${DOOMED[@]}"}; do
	if [ "$APPLY" = 1 ]; then
		# A file left behind must be reported, not counted as freed space. This
		# folder still holds subtrees owned by root from earlier root-run
		# sessions, and unlinking needs write permission on the *directory* —
		# so the timer, which runs as the desktop user, can be silently unable
		# to delete them.
		rm -f -- "$ROOT/${P[$i]}" 2>/dev/null
		if [ -e "$ROOT/${P[$i]}" ]; then
			failed=$((failed + 1))
			lines+=("  FAILED (permission?)  ${P[$i]}")
			continue
		fi
	fi
	n=$((n + 1)); bytes=$((bytes + ${S[$i]}))
	lines+=("  ${C[$i]}  $(human "${S[$i]}")  ${P[$i]}")
done

if [ "$APPLY" = 1 ]; then
	# Sweep up directories the deletions emptied. Protected dirs stay: rmdir
	# refuses non-empty ones, and -p is deliberately not used.
	find "$ROOT" -mindepth 1 -type d -empty -delete 2>/dev/null
fi

summary="$stamp  removed $n files, $(human $bytes)  (was $(human $total_disk), cap $(human $MAX_BYTES), retain ${RETAIN_DAYS}d)"
[ "$failed" -gt 0 ] && summary="$summary  [$failed FAILED]"

if [ "$APPLY" = 1 ]; then
	if [ -f "$LOGFILE" ] && [ "$(stat -c %s "$LOGFILE" 2>/dev/null || echo 0)" -ge "$LOG_MAX" ]; then
		mv -f "$LOGFILE" "$LOGFILE.1"
	fi
	{
		echo "$summary"
		printf '%s\n' ${lines+"${lines[@]}"}
	} >> "$LOGFILE"
	echo "$summary"
else
	echo "DRY RUN — nothing deleted. Re-run with --apply to act."
	echo "$summary"
	printf '%s\n' ${lines+"${lines[@]}"}
fi
