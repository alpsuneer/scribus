#!/bin/bash
# Runs ON THE UPDATE SERVER, installed there as /usr/local/bin/scribus-push-update
# (tools/update-server/setup-push-server.sh). Pushes the release that
# tools/release.sh uploaded to the office PCs over SSH.
#
#   scribus-push-update --all [--dry-run]         every PC in the pcs list
#   scribus-push-update PC... [--dry-run]         the named PCs (name or address from the pcs list)
#   scribus-push-update --first PC                the first install on a PC (asks for its sudo password once)
#   scribus-push-update --scan                    read-only: what answers on the office subnet
#   scribus-push-update --list                    the pcs list
#   scribus-push-update --copy-keys               ssh-copy-id the push key to every PC in the pcs list
#
# From the laptop:  ssh s1@<server> scribus-push-update --all
# (--first and --copy-keys ask for passwords: use ssh -t.)
#
# What a push does on each PC, and prints as  PC -> old -> new -> OK / SKIPPED / FAILED:
#   - SKIPPED when Scribus is open there (it is never replaced under a running program);
#   - copies the .deb and latest.json and runs the PC's own
#     /usr/local/sbin/scribus-install-update through sudo -n. That helper is the
#     only thing the push account may run as root; it checks the signature and
#     the sha256 again and sets /etc/scribus/update.conf to this server;
#   - checks the installed version, that no library is missing (ldd) and the
#     update URL.
#
# A PC whose Scribus is older than the helper needs --first once: it installs
# with plain "sudo dpkg -i" over ssh -t, so the PC's own sudo password is typed.
# From then on the PC has the helper and the sudoers entry.
#
# The server only holds what is public: the .deb, latest.json and the release
# PUBLIC key. It verifies both before every push. The private signing key never
# leaves the release laptop; a .deb it did not sign is refused here and again
# on the PC.
#
# Settings: /etc/scribus-push/push.conf (shell fragment) and /etc/scribus-push/pcs
# ("name address" per line). Neither is in the repository.
#
#   SUBNET=192.0.2.0/24
#   SKIP_HOSTS="192.0.2.1 192.0.2.10 192.0.2.20"   # release laptop, router, this server
#   UPDATE_BASE_URL=http://192.0.2.20:8081
#   UPDATES_DIR=/srv/scribus-updates
#   PUBKEY=<base64 raw Ed25519 public key the releases are signed with>
#   PC_USER=s1
#   SSH_KEY=/etc/scribus-push/push_key
set -euo pipefail

CONF_DIR="${SCRIBUS_PUSH_CONF_DIR:-/etc/scribus-push}"
MODE="push"; DRY_RUN=0; ALL=0; NAMES=()
while [ $# -gt 0 ]; do
	case "$1" in
		--all) ALL=1 ;;
		--dry-run) DRY_RUN=1 ;;
		--scan|--list|--copy-keys|--first) [ "$MODE" = "push" ] || { echo "only one of --scan, --list, --copy-keys, --first" >&2; exit 2; }; MODE="${1#--}" ;;
		-h|--help) sed -n '2,46p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-*) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
		*) NAMES+=("$1") ;;
	esac
	shift
done

die() { printf 'scribus-push-update: %s\n' "$*" >&2; exit 1; }
[ -f "$CONF_DIR/push.conf" ] || die "no settings at $CONF_DIR/push.conf"
# shellcheck disable=SC1091
. "$CONF_DIR/push.conf"
: "${SUBNET:?SUBNET is not set in push.conf}" "${UPDATE_BASE_URL:?UPDATE_BASE_URL is not set in push.conf}"
SKIP_HOSTS="${SKIP_HOSTS:-}"; UPDATES_DIR="${UPDATES_DIR:-/srv/scribus-updates}"
PC_USER="${PC_USER:-s1}"; SSH_KEY="${SSH_KEY:-$CONF_DIR/push_key}"; PUBKEY="${PUBKEY:-}"
UPDATE_BASE_URL="${UPDATE_BASE_URL%/}"
PCS_FILE="$CONF_DIR/pcs"
HELPER=/usr/local/sbin/scribus-install-update

SSH_OPTS=(-i "$SSH_KEY" -o IdentitiesOnly=yes -o ConnectTimeout=6 -o StrictHostKeyChecking=accept-new
          -o UserKnownHostsFile="$CONF_DIR/known_hosts" -o LogLevel=ERROR)
rsh()  { local h=$1; shift; ssh "${SSH_OPTS[@]}" -o BatchMode=yes "$PC_USER@$h" "$@"; }
is_skipped() { case " $SKIP_HOSTS " in *" $1 "*) return 0 ;; esac; return 1; }

# ------------------------------------------------------------- pcs list ---
declare -a PC_NAMES=(); declare -A PC_ADDR=()
if [ -f "$PCS_FILE" ]; then
	while read -r name addr _; do
		case "$name" in ''|'#'*) continue ;; esac
		[ -n "${addr:-}" ] || addr="$name"
		is_skipped "$addr" && die "$PCS_FILE lists $addr, which is in SKIP_HOSTS (release laptop, router or this server)"
		PC_NAMES+=("$name"); PC_ADDR[$name]="$addr"
	done < "$PCS_FILE"
fi
if [ "$MODE" = "list" ]; then
	[ ${#PC_NAMES[@]} -gt 0 ] || { echo "the pcs list ($PCS_FILE) is empty"; exit 0; }
	for n in "${PC_NAMES[@]}"; do printf '%-20s %s\n' "$n" "${PC_ADDR[$n]}"; done
	exit 0
fi

# ----------------------------------------------------------------- scan ---
if [ "$MODE" = "scan" ]; then
	echo "Scan $SUBNET (skipping: ${SKIP_HOSTS:-nothing})"
	if command -v nmap >/dev/null; then
		mapfile -t UP < <(nmap -sn -n ${SKIP_HOSTS:+--exclude "$(echo $SKIP_HOSTS | tr ' ' ',')"} "$SUBNET" -oG - 2>/dev/null | awk '/Status: Up/{print $2}' | sort -t. -k4,4n)
	else
		# No nmap: one ping per address of a /24, all at once.
		case "$SUBNET" in */24) ;; *) die "without nmap only a /24 can be scanned" ;; esac
		base="${SUBNET%.*}"
		mapfile -t UP < <(for i in $(seq 1 254); do ( ping -c 1 -W 1 "$base.$i" >/dev/null 2>&1 && echo "$base.$i" ) & done; wait)
		mapfile -t UP < <(printf '%s\n' "${UP[@]}" | grep -v '^$' | sort -t. -k4,4n)
	fi
	printf '%-16s %-14s %-9s %-18s %-20s %s\n' "address" "in pcs list" "ssh" "host name" "Scribus" "notes"
	count=0
	for h in "${UP[@]}"; do
		[ -n "$h" ] || continue
		is_skipped "$h" && continue
		count=$((count+1))
		listed="-"; for n in "${PC_NAMES[@]}"; do [ "${PC_ADDR[$n]}" = "$h" ] && listed="$n"; done
		if ! timeout 3 bash -c "exec 3<>/dev/tcp/$h/22" 2>/dev/null; then
			printf '%-16s %-14s %-9s %-18s %-20s %s\n' "$h" "$listed" "closed" "-" "-" "not a Linux PC with SSH"
			continue
		fi
		# The first line an SSH server sends names it: OpenSSH on a PC, dropbear on a switch, NAS or camera.
		banner=$(timeout 5 bash -c "exec 3<>/dev/tcp/$h/22; IFS= read -r -t 3 line <&3; printf '%s' \"\$line\"" 2>/dev/null | tr -d '\r' | cut -c1-32 || true)
		if [ -r "$SSH_KEY" ] && out=$(rsh "$h" 'printf "%s|%s|%s|%s\n" "$(hostname)" "$(dpkg-query -W -f="\${Version}" scribus 2>/dev/null || echo none)" "$(pgrep -x scribus.bin >/dev/null && echo open || echo closed)" "$(test -x '"$HELPER"' && echo helper || echo no-helper)"' 2>/dev/null); then
			IFS='|' read -r name ver running helper <<< "$out"
			printf '%-16s %-14s %-9s %-18s %-20s %s\n' "$h" "$listed" "key ok" "$name" "$ver" "Scribus $running, $helper"
		else
			case "$banner" in
				*dropbear*) note="$banner: an appliance, not an office PC" ;;
				*) note="${banner:-no banner}; push key not accepted yet" ;;
			esac
			printf '%-16s %-14s %-9s %-18s %-20s %s\n' "$h" "$listed" "open" "-" "-" "$note"
		fi
	done
	echo "$count machine(s) answered."
	exit 0
fi

# ------------------------------------------------------------ copy-keys ---
if [ "$MODE" = "copy-keys" ]; then
	[ ${#PC_NAMES[@]} -gt 0 ] || die "the pcs list ($PCS_FILE) is empty"
	[ -r "$SSH_KEY.pub" ] || die "no push key at $SSH_KEY.pub (or not readable by $(id -un))"
	for n in "${PC_NAMES[@]}"; do
		h="${PC_ADDR[$n]}"
		if rsh "$h" true 2>/dev/null; then echo "$n ($h): key already accepted"; continue; fi
		echo "== $n ($h): type the password of $PC_USER on that PC"
		ssh-copy-id -i "$SSH_KEY.pub" -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile="$CONF_DIR/known_hosts" "$PC_USER@$h" \
			|| echo "$n ($h): FAILED"
	done
	exit 0
fi

# -------------------------------------------------------------- package ---
# Verified here before anything leaves the server, by the rules of the client.
MANIFEST="$UPDATES_DIR/latest.json"
[ -f "$MANIFEST" ] || die "no release on this server yet ($MANIFEST is missing; tools/release.sh uploads it)"
[ -n "$PUBKEY" ] || die "PUBKEY is not set in push.conf: the release cannot be verified"
VERIFIED=$(python3 - "$MANIFEST" "$UPDATES_DIR" "$PUBKEY" "$UPDATE_BASE_URL" <<'PY'
import base64,hashlib,json,os,subprocess,sys,tempfile
from urllib.parse import urlsplit
manifest,folder,pub,base=sys.argv[1:5]
def fail(m): print("scribus-push-update: release not verified:",m,file=sys.stderr); sys.exit(1)
obj=json.load(open(manifest))
version,url,sha=obj.get("version",""),obj.get("url",""),(obj.get("sha256","") or "").lower()
def origin(u):
    s=urlsplit(u); return (s.scheme.lower(),(s.hostname or "").lower(),s.port or (443 if s.scheme.lower()=="https" else 80))
if origin(url)!=origin(base): fail("latest.json is signed for %s, not for this server (%s)"%(url,base))
deb=os.path.join(folder,os.path.basename(urlsplit(url).path))
if not os.path.isfile(deb): fail("%s is not on the server"%os.path.basename(deb))
h=hashlib.sha256()
with open(deb,"rb") as f:
    for chunk in iter(lambda:f.read(1<<20),b""): h.update(chunk)
if h.hexdigest()!=sha: fail("the .deb does not have the signed sha256")
msg=("scribus-update-v1\n%s\n%s\n%s\n"%(version,url,sha)).encode()
with tempfile.TemporaryDirectory() as d:
    open(os.path.join(d,"pub.der"),"wb").write(bytes.fromhex("302a300506032b6570032100")+base64.b64decode(pub))
    open(os.path.join(d,"msg"),"wb").write(msg); open(os.path.join(d,"sig"),"wb").write(base64.b64decode(obj.get("signature","")))
    r=subprocess.run(["openssl","pkeyutl","-verify","-pubin","-inkey",os.path.join(d,"pub.der"),"-keyform","DER","-rawin","-in",os.path.join(d,"msg"),"-sigfile",os.path.join(d,"sig")],capture_output=True)
    if r.returncode!=0: fail("the signature does not verify with the release public key")
print(version,deb)
PY
) || exit 1
read -r VERSION DEB <<< "$VERIFIED"
echo "Release on this server: $VERSION ($(basename "$DEB"), signature and sha256 verified)"

# -------------------------------------------------------------- targets ---
declare -a TARGETS=()
if [ $ALL -eq 1 ]; then
	[ ${#NAMES[@]} -eq 0 ] || die "--all and PC names together: pick one"
	TARGETS=("${PC_NAMES[@]}")
else
	[ ${#NAMES[@]} -gt 0 ] || die "name the PCs, or use --all (see --list)"
	for want in "${NAMES[@]}"; do
		found=""
		for n in "${PC_NAMES[@]}"; do { [ "$n" = "$want" ] || [ "${PC_ADDR[$n]}" = "$want" ]; } && found="$n"; done
		[ -n "$found" ] || die "$want is not in the pcs list ($PCS_FILE)"
		TARGETS+=("$found")
	done
fi
[ ${#TARGETS[@]} -gt 0 ] || die "the pcs list ($PCS_FILE) is empty"
[ "$MODE" != "first" ] || [ ${#TARGETS[@]} -eq 1 ] || die "--first takes exactly one PC"

state() { # $1 address -> "hostname|version|open/closed|helper/no-helper|url"
	rsh "$1" 'printf "%s|%s|%s|%s|%s\n" "$(hostname)" "$(dpkg-query -W -f="\${Version}" scribus 2>/dev/null || echo none)" "$(pgrep -x scribus.bin >/dev/null && echo open || echo closed)" "$(test -x '"$HELPER"' && echo helper || echo no-helper)" "$(sed -n "s/^url=//p" /etc/scribus/update.conf 2>/dev/null | tail -1)"' 2>/dev/null
}
check_after() { # $1 address -> empty when good, else what is wrong
	local out ver missing url
	out=$(rsh "$1" 'printf "%s|%s|%s\n" "$(dpkg-query -W -f="\${Version}" scribus 2>/dev/null)" "$( { ldd /usr/local/bin/scribus.bin; for f in /usr/local/lib/scribus/plugins/*.so; do ldd "$f"; done; } 2>/dev/null | grep -c "not found")" "$(sed -n "s/^url=//p" /etc/scribus/update.conf | tail -1)"' 2>/dev/null) || { echo "cannot reach the PC after the install"; return; }
	IFS='|' read -r ver missing url <<< "$out"
	[ "$ver" = "$VERSION" ] || { echo "version is $ver"; return; }
	[ "$missing" = "0" ] || { echo "$missing shared librar(y/ies) not found (ldd)"; return; }
	[ "$url" = "$UPDATE_BASE_URL" ] || { echo "update url is ${url:-unset}"; return; }
}

RESULTS=(); FAILED=0
report() { RESULTS+=("$(printf '%-16s -> %-18s -> %-18s -> %s' "$1" "$2" "$3" "$4")"); echo "   ${RESULTS[-1]}"; }
for n in "${TARGETS[@]}"; do
	h="${PC_ADDR[$n]}"
	if ! st=$(state "$h"); then
		report "$n" "?" "$VERSION" "FAILED: no SSH login with the push key (scribus-push-update --copy-keys)"; FAILED=$((FAILED+1)); continue
	fi
	IFS='|' read -r host old running helper url <<< "$st"
	if [ "$running" = "open" ]; then report "$n" "$old" "$VERSION" "SKIPPED: Scribus is open on $host"; continue; fi
	if [ "$old" != "none" ] && [ "$old" != "$VERSION" ] && dpkg --compare-versions "$old" gt "$VERSION"; then
		report "$n" "$old" "$VERSION" "SKIPPED: the PC has a newer version"; continue
	fi

	if [ "$MODE" = "first" ]; then
		if [ $DRY_RUN -eq 1 ]; then report "$n" "$old" "$VERSION" "would do the first install (sudo password of $PC_USER on $host)"; continue; fi
		tmp="/tmp/scribus-push-$$"
		rsh "$h" "mkdir -m 700 $tmp" && scp -q "${SSH_OPTS[@]}" "$DEB" "$PC_USER@$h:$tmp/scribus.deb" \
			|| { report "$n" "$old" "$VERSION" "FAILED: could not copy the .deb"; FAILED=$((FAILED+1)); continue; }
		echo "== $n ($host): type the sudo password of $PC_USER on that PC"
		# The sha256 is checked again on the PC, as root, right before dpkg sees the file.
		SHA=$(sha256sum "$DEB" | cut -d' ' -f1)
		ssh -t "${SSH_OPTS[@]}" "$PC_USER@$h" "sudo sh -c 'echo \"$SHA  $tmp/scribus.deb\" | sha256sum -c --quiet - && dpkg --force-confold -i $tmp/scribus.deb && mkdir -p /etc/scribus && if grep -q \"^url=\" /etc/scribus/update.conf 2>/dev/null; then sed -i \"s|^url=.*|url=$UPDATE_BASE_URL|\" /etc/scribus/update.conf; else echo \"url=$UPDATE_BASE_URL\" >> /etc/scribus/update.conf; fi'; rc=\$?; rm -rf $tmp; exit \$rc" \
			|| { report "$n" "$old" "$VERSION" "FAILED: the first install did not finish"; FAILED=$((FAILED+1)); continue; }
	else
		if [ "$helper" != "helper" ]; then
			report "$n" "$old" "$VERSION" "FAILED: no install helper on this PC yet (scribus-push-update --first $n)"; FAILED=$((FAILED+1)); continue
		fi
		if [ $DRY_RUN -eq 1 ]; then
			[ "$old" = "$VERSION" ] && [ "$url" = "$UPDATE_BASE_URL" ] && report "$n" "$old" "$VERSION" "OK: nothing to do" || report "$n" "$old" "$VERSION" "would install"
			continue
		fi
		tmp="/tmp/scribus-push-$$"
		if ! { rsh "$h" "mkdir -m 700 $tmp" && scp -q "${SSH_OPTS[@]}" "$DEB" "$PC_USER@$h:$tmp/scribus.deb" && scp -q "${SSH_OPTS[@]}" "$MANIFEST" "$PC_USER@$h:$tmp/latest.json"; }; then
			report "$n" "$old" "$VERSION" "FAILED: could not copy the release"; FAILED=$((FAILED+1)); continue
		fi
		if ! msg=$(rsh "$h" "sudo -n $HELPER $tmp/latest.json $tmp/scribus.deb 2>&1; rc=\$?; rm -rf $tmp; exit \$rc"); then
			report "$n" "$old" "$VERSION" "FAILED: $(echo "$msg" | tail -1)"; FAILED=$((FAILED+1)); continue
		fi
	fi
	wrong=$(check_after "$h")
	if [ -n "$wrong" ]; then report "$n" "$old" "$VERSION" "FAILED: $wrong"; FAILED=$((FAILED+1)); else report "$n" "$old" "$VERSION" "OK"; fi
done

echo
echo "PC               -> old                -> new                -> result"
printf '%s\n' "${RESULTS[@]}"
[ $FAILED -eq 0 ] || exit 1
