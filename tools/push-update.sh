#!/bin/bash
# Find the office PCs on the LAN and push a released Scribus .deb, or the
# update server's URL, to them over SSH - for the PCs the built-in updater
# cannot reach yet (wrong URL in /etc/scribus/update.conf, or no Scribus).
#
#   tools/push-update.sh --scan
#   tools/push-update.sh --set-url  [--all | HOST...] [--dry-run] [--yes]
#   tools/push-update.sh --install  [--all | HOST...] [--dry-run] [--yes] [--deb FILE]
#
#   --scan      read-only. Lists every machine that answers in SUBNET except
#               SKIP_HOSTS (this laptop, the router, the update server), and
#               for those it can log in to: host name, installed Scribus
#               version and the update URL it is set to.
#   --set-url   writes url=<UPDATE_BASE_URL> into /etc/scribus/update.conf on
#               the PCs, so their own updater asks the right server from then on.
#   --install   copies the .deb to the PCs and runs dpkg -i there.
#   --all       every PC the scan can log in to. Otherwise name the hosts.
#   --dry-run   say what would be done, change nothing on any PC.
#   --yes       do not ask before changing the PCs.
#   --deb FILE  another .deb than the last released one (its latest.json must
#               be next to it).
#   --config FILE   site settings (default: ~/scribus-keys/push-update.conf).
#
# Nothing here uploads to the update server: tools/release.sh does that, and
# signs the manifest for the server's URL. This script only talks to the PCs.
#
# --install never pushes an unreleased build. The .deb must belong to a
# release/<version> git tag, match the sha256 in its latest.json, and that
# latest.json must carry a valid signature for the public key compiled into
# the build - the same three things an office PC checks before it installs.
#
# Site settings are NOT in this repository (it is public). The config file is
# a shell fragment:
#
#   SUBNET=192.0.2.0/24                       # where the office PCs are
#   SKIP_HOSTS="192.0.2.1 192.0.2.10 192.0.2.20"   # this laptop, the router, the update server
#   UPDATE_BASE_URL=http://192.0.2.20:8081    # what the PCs should ask; default: the one in release.conf
#   PC_SSH_USER=office                        # account on the office PCs (key login)
#   PC_SUDO="sudo -n"                         # how that account becomes root; empty when it is root
set -euo pipefail

MODE=""; DRY_RUN=0; YES=0; ALL=0; DEB=""; HOSTS=()
CONFIG="${HOME}/scribus-keys/push-update.conf"
while [ $# -gt 0 ]; do
	case "$1" in
		--scan|--set-url|--install) [ -z "$MODE" ] || { echo "only one of --scan, --set-url, --install" >&2; exit 2; }; MODE="${1#--}" ;;
		--all) ALL=1 ;;
		--dry-run) DRY_RUN=1 ;;
		--yes) YES=1 ;;
		--deb) shift; DEB="${1:?--deb needs a file}" ;;
		--config) shift; CONFIG="${1:?--config needs a file}" ;;
		-h|--help) sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-*) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
		*) HOSTS+=("$1") ;;
	esac
	shift
done
[ -n "$MODE" ] || { sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2; }

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
say() { printf '\n== %s\n' "$*"; }
die() { printf '\npush-update.sh: %s\n' "$*" >&2; exit 1; }
for tool in ssh scp nmap python3 openssl sha256sum dpkg-deb git; do
	command -v "$tool" >/dev/null || die "missing tool: $tool"
done

[ -f "$CONFIG" ] || die "no site settings at $CONFIG (see --help for what goes in it)"
CONFIG_DIR=$(cd "$(dirname "$CONFIG")" && pwd)
# shellcheck disable=SC1090
. "$CONFIG"
if [ -z "${UPDATE_BASE_URL:-}" ] && [ -f "$CONFIG_DIR/release.conf" ]; then
	UPDATE_BASE_URL=$(sed -n 's/^UPDATE_BASE_URL=//p' "$CONFIG_DIR/release.conf" | tail -1)
fi
UPDATE_BASE_URL="${UPDATE_BASE_URL%/}"
[ -n "${SUBNET:-}" ] || die "SUBNET is not set in $CONFIG"
SKIP_HOSTS="${SKIP_HOSTS:-}"
PC_SSH_USER="${PC_SSH_USER:-}"
PC_SUDO="${PC_SUDO-sudo -n}"

# Host keys of the office PCs are kept apart from the user's own known_hosts.
KNOWN="$CONFIG_DIR/push-update.known_hosts"
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=6 -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile="$KNOWN" -o LogLevel=ERROR)
is_skipped() { case " $SKIP_HOSTS " in *" $1 "*) return 0 ;; esac; return 1; }
remote() { # $1 host, rest: command
	local h=$1; shift
	ssh "${SSH_OPTS[@]}" "${PC_SSH_USER:+$PC_SSH_USER@}$h" "$@"
}

# ----------------------------------------------------------------- scan ---
# Fills FOUND (every address that answers, skip list removed) and, for those
# that accept the login, INFO[address]="hostname|scribus version|update url".
declare -a FOUND=(); declare -A INFO=(); declare -A STATE=()
scan() {
	local excl; excl=$(echo $SKIP_HOSTS | tr ' ' ',')
	mapfile -t FOUND < <(nmap -sn -n ${excl:+--exclude "$excl"} "$SUBNET" -oG - 2>/dev/null | awk '/Status: Up/{print $2}' | sort -t. -k4,4n)
	local h out
	for h in "${FOUND[@]}"; do
		if ! timeout 3 bash -c "exec 3<>/dev/tcp/$h/22" 2>/dev/null; then
			STATE[$h]="no SSH (port 22 closed)"; continue
		fi
		if [ -z "$PC_SSH_USER" ]; then
			STATE[$h]="SSH open; PC_SSH_USER not set"; continue
		fi
		if out=$(remote "$h" 'printf "%s|%s|%s\n" "$(hostname)" "$(dpkg-query -W -f="\${Version}" scribus 2>/dev/null || echo none)" "$(sed -n "s/^url=//p" /etc/scribus/update.conf 2>/dev/null | tail -1)"' 2>/dev/null); then
			INFO[$h]="$out"; STATE[$h]="ok"
		else
			STATE[$h]="SSH open; login as $PC_SSH_USER refused"
		fi
	done
}
print_scan() {
	printf '   %-16s %-18s %-20s %s\n' "address" "host name" "Scribus" "update URL / state"
	local h name ver url
	for h in "${FOUND[@]}"; do
		if [ "${STATE[$h]}" = "ok" ]; then
			IFS='|' read -r name ver url <<< "${INFO[$h]}"
			printf '   %-16s %-18s %-20s %s\n' "$h" "$name" "$ver" "${url:-(no update.conf)}"
		else
			printf '   %-16s %-18s %-20s %s\n' "$h" "-" "-" "${STATE[$h]}"
		fi
	done
	local n=0; for h in "${FOUND[@]}"; do [ "${STATE[$h]}" = "ok" ] && n=$((n+1)); done
	echo "   ${#FOUND[@]} machine(s) answered, $n reachable as an office PC. Skipped: ${SKIP_HOSTS:-nothing}"
}

say "Scan $SUBNET"
scan
print_scan
[ "$MODE" = "scan" ] && exit 0

# -------------------------------------------------------------- targets ---
declare -a TARGETS=()
if [ $ALL -eq 1 ]; then
	[ ${#HOSTS[@]} -eq 0 ] || die "--all and host names together: pick one"
	for h in "${FOUND[@]}"; do [ "${STATE[$h]}" = "ok" ] && TARGETS+=("$h"); done
else
	[ ${#HOSTS[@]} -gt 0 ] || die "name the PCs, or use --all"
	for h in "${HOSTS[@]}"; do
		is_skipped "$h" && die "$h is in SKIP_HOSTS (this laptop, the router or the update server): not an office PC"
		[ "${STATE[$h]:-}" = "ok" ] || die "$h: ${STATE[$h]:-did not answer the scan}"
		TARGETS+=("$h")
	done
fi
[ ${#TARGETS[@]} -gt 0 ] || die "no office PC to work on (see the scan above)"

confirm() { # $1 question
	[ $DRY_RUN -eq 1 ] && return 0
	[ $YES -eq 1 ] && return 0
	[ -t 0 ] || die "this changes ${#TARGETS[@]} PC(s); run it in a terminal, or add --yes"
	read -r -p "   $1 [y/N] " answer
	[ "$answer" = "y" ] || [ "$answer" = "Y" ] || die "stopped; nothing changed"
}

# -------------------------------------------------------------- set-url ---
if [ "$MODE" = "set-url" ]; then
	[ -n "$UPDATE_BASE_URL" ] || die "UPDATE_BASE_URL is not set in $CONFIG or in $CONFIG_DIR/release.conf"
	say "Update URL -> $UPDATE_BASE_URL"
	confirm "Write this URL into /etc/scribus/update.conf on ${#TARGETS[@]} PC(s): ${TARGETS[*]} ?"
	FAILED=0
	for h in "${TARGETS[@]}"; do
		IFS='|' read -r name ver url <<< "${INFO[$h]}"
		if [ "$url" = "$UPDATE_BASE_URL" ]; then echo "   $h ($name): already set"; continue; fi
		if [ $DRY_RUN -eq 1 ]; then echo "   $h ($name): would change url=${url:-<none>} -> $UPDATE_BASE_URL"; continue; fi
		# Only the url= line changes; a missing file is created with just that line.
		if remote "$h" "$PC_SUDO sh -c 'mkdir -p /etc/scribus && if grep -q \"^url=\" /etc/scribus/update.conf 2>/dev/null; then sed -i \"s|^url=.*|url=$UPDATE_BASE_URL|\" /etc/scribus/update.conf; else echo \"url=$UPDATE_BASE_URL\" >> /etc/scribus/update.conf; fi'" \
		   && [ "$(remote "$h" 'sed -n "s/^url=//p" /etc/scribus/update.conf | tail -1')" = "$UPDATE_BASE_URL" ]; then
			echo "   $h ($name): url=${url:-<none>} -> $UPDATE_BASE_URL"
		else
			echo "   $h ($name): FAILED (is $PC_SSH_USER allowed '$PC_SUDO' without a password?)"; FAILED=$((FAILED+1))
		fi
	done
	[ $FAILED -eq 0 ] || die "$FAILED PC(s) not changed"
	exit 0
fi

# -------------------------------------------------------------- install ---
say "Package"
if [ -z "$DEB" ]; then
	LAST_TAG=$(git -C "$ROOT" tag --list 'release/*' --sort=-creatordate | head -1)
	[ -n "$LAST_TAG" ] || die "no release/* tag: nothing has been released yet"
	VERSION="${LAST_TAG#release/}"
	DEB="$BUILD/release/$VERSION/scribus_${VERSION}_amd64.deb"
fi
[ -f "$DEB" ] || die "no .deb at $DEB"
MANIFEST="$(dirname "$DEB")/latest.json"
[ -f "$MANIFEST" ] || die "no latest.json next to $DEB"
VERSION=$(dpkg-deb -f "$DEB" Version)
git -C "$ROOT" rev-parse -q --verify "refs/tags/release/$VERSION" >/dev/null \
	|| die "$VERSION was never released (no release/$VERSION tag) - a --dry-run build is not pushed to the office"
PUB=$(sed -n 's/^SCRIBUS_UPDATE_PUBKEY:STRING=//p' "$BUILD/CMakeCache.txt" 2>/dev/null || true)
[ -n "$PUB" ] || die "no SCRIBUS_UPDATE_PUBKEY in $BUILD/CMakeCache.txt: cannot check the signature"
python3 - "$MANIFEST" "$DEB" "$PUB" "$VERSION" <<'PY' || die "the package does not pass the checks an office PC makes"
import base64,hashlib,json,os,subprocess,sys,tempfile
manifest,deb,pub,version=sys.argv[1:5]
def fail(m): print("   FAIL:",m); sys.exit(1)
obj=json.load(open(manifest))
if obj.get("version")!=version: fail("latest.json is for %s, the .deb is %s"%(obj.get("version"),version))
h=hashlib.sha256()
with open(deb,"rb") as f:
    for chunk in iter(lambda:f.read(1<<20),b""): h.update(chunk)
if h.hexdigest()!=(obj.get("sha256") or "").lower(): fail("the .deb does not match the sha256 in latest.json")
msg=("scribus-update-v1\n%s\n%s\n%s\n"%(obj["version"],obj["url"],obj["sha256"].lower())).encode()
with tempfile.TemporaryDirectory() as d:
    open(os.path.join(d,"pub.der"),"wb").write(bytes.fromhex("302a300506032b6570032100")+base64.b64decode(pub))
    open(os.path.join(d,"msg"),"wb").write(msg); open(os.path.join(d,"sig"),"wb").write(base64.b64decode(obj.get("signature","")))
    r=subprocess.run(["openssl","pkeyutl","-verify","-pubin","-inkey",os.path.join(d,"pub.der"),"-keyform","DER","-rawin","-in",os.path.join(d,"msg"),"-sigfile",os.path.join(d,"sig")],capture_output=True)
    if r.returncode!=0: fail("the signature in latest.json does not verify with the build's public key")
print("   ok: released, sha256 %s..., signature valid"%h.hexdigest()[:16])
PY
echo "   $(basename "$DEB")  $(du -h "$DEB" | cut -f1)"

say "Install $VERSION"
confirm "Install $VERSION with dpkg -i on ${#TARGETS[@]} PC(s): ${TARGETS[*]} ?"
FAILED=0
for h in "${TARGETS[@]}"; do
	IFS='|' read -r name ver url <<< "${INFO[$h]}"
	if [ "$ver" = "$VERSION" ]; then echo "   $h ($name): already at $VERSION"; continue; fi
	if [ "$ver" != "none" ] && dpkg --compare-versions "$ver" gt "$VERSION"; then echo "   $h ($name): has the newer $ver, left alone"; continue; fi
	if [ $DRY_RUN -eq 1 ]; then echo "   $h ($name): would install $VERSION over $ver"; continue; fi
	TMP="/tmp/$(basename "$DEB")"
	if scp -q "${SSH_OPTS[@]}" "$DEB" "${PC_SSH_USER:+$PC_SSH_USER@}$h:$TMP" \
	   && remote "$h" "$PC_SUDO dpkg -i '$TMP' >/tmp/scribus-push-update.log 2>&1; rc=\$?; rm -f '$TMP'; exit \$rc" \
	   && [ "$(remote "$h" 'dpkg-query -W -f="\${Version}" scribus')" = "$VERSION" ]; then
		echo "   $h ($name): $ver -> $VERSION"
	else
		echo "   $h ($name): FAILED (log on the PC: /tmp/scribus-push-update.log)"; FAILED=$((FAILED+1))
	fi
done
[ $FAILED -eq 0 ] || die "$FAILED PC(s) not updated"
echo "   A Scribus that is open on a PC keeps running the old version until it is restarted."
