#!/bin/bash
# Publish one Scribus release to the Debian update server, for the built-in
# updater (Help > Check for Updates..., Ed25519-signed latest.json).
#
#   tools/publish-update.sh [-m "changelog"] [--allow-dirty] [--no-tag] [--no-http-check]
#   tools/publish-update.sh --list                 what the server holds and what the PCs see
#   tools/publish-update.sh --rollback <version>   point latest.json at an older .deb still on the server
#
# Steps: build + package + sign on this laptop (tools/release.sh --dry-run does
# exactly that; nothing leaves the laptop), then rsync in this order: the .deb,
# its .sha256 and .sig, and latest.json LAST. rsync writes every file to a
# temporary name on the server and renames it into place, so a PC never reads
# a half-written manifest. Releases beyond KEEP_VERSIONS (default 3) are
# removed from the server. Then latest.json and the .deb are downloaded over
# HTTP exactly as a PC would and checked (signature, sha256, same origin,
# headers). Finally the git tag release/<version>.
#
# Site settings: ~/scribus-keys/release.conf (UPDATE_BASE_URL, UPLOAD_TARGET =
# scribusupd@host:/ for the rrsync-restricted account, PRIVATE_KEY,
# KEEP_VERSIONS). The private key never leaves this laptop; the server only
# ever receives the .deb, the manifest and detached signatures.
#
# Run as the user who owns ~/.ssh/id_ed25519 and ~/scribus-keys (s1).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BUILD="$ROOT/build"
CONFIG="${HOME}/scribus-keys/release.conf"
MODE=publish; CHANGELOG=(); EXTRA=(); NO_TAG=0; NO_HTTP=0; ROLLBACK_TO=""
while [ $# -gt 0 ]; do
	case "$1" in
		-m) shift; CHANGELOG=(-m "${1:?-m needs a text}") ;;
		--allow-dirty) EXTRA+=(--allow-dirty); NO_TAG=1 ;;
		--no-tag) NO_TAG=1 ;;
		--no-http-check) NO_HTTP=1 ;;
		--list) MODE=list ;;
		--rollback) MODE=rollback; shift; ROLLBACK_TO="${1:?--rollback needs a version}" ;;
		-h|--help) sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
	esac
	shift
done

say() { printf '\n== %s\n' "$*"; }
die() { printf '\npublish-update.sh: %s\n' "$*" >&2; exit 1; }
[ -f "$CONFIG" ] || die "no $CONFIG (see --help)"
. "$CONFIG"
UPDATE_BASE_URL="${UPDATE_BASE_URL%/}"
[ -n "${UPLOAD_TARGET:-}" ] || die "UPLOAD_TARGET is not set in $CONFIG"
[ "${UPLOAD_METHOD:-rsync}" = "rsync" ] || die "this script uploads with rsync (UPLOAD_METHOD=$UPLOAD_METHOD)"
KEEP="${KEEP_VERSIONS:-3}"
PRIVATE_KEY="${PRIVATE_KEY:-${HOME}/scribus-keys/release-key.pem}"; PRIVATE_KEY="${PRIVATE_KEY/#\~/$HOME}"
for tool in rsync ssh openssl python3 sha256sum curl git cmake; do command -v "$tool" >/dev/null || die "missing tool: $tool"; done
PUB=$(sed -n 's/^SCRIBUS_UPDATE_PUBKEY:STRING=//p' "$BUILD/CMakeCache.txt" 2>/dev/null || true)
[ -n "$PUB" ] || die "build has no SCRIBUS_UPDATE_PUBKEY"

# rsync to the rrsync account: "<target>" is the root of /srv/scribus-updates.
up()   { rsync -t --chmod=F644 "$@"; }
lsrv() { rsync --list-only "$UPLOAD_TARGET" 2>/dev/null | awk '$NF!="." {print $NF}'; }

# ------------------------------------------------------- read-back check ---
# $1 base url  $2 expected version (empty = any)   downloads as a PC does, verifies.
http_check() {
	local base="$1" want="$2" d
	d=$(mktemp -d)
	curl -fsS --connect-timeout 15 -o "$d/latest.json" "$base/latest.json" \
		|| die "cannot download $base/latest.json (nginx location not installed yet? see tools/update-server/nginx-scribus-updates.conf)"
	local ctype; ctype=$(curl -fsS --connect-timeout 15 -o /dev/null -w '%{content_type}' "$base/latest.json" || true)
	case "$ctype" in application/json*) ;; *) echo "   NOTE: latest.json served as '$ctype', expected application/json" ;; esac
	local cache; cache=$(curl -fsS --connect-timeout 15 -I "$base/latest.json" | tr -d '\r' | awk -F': ' 'tolower($1)=="cache-control"{print $2}')
	case "$cache" in *no-cache*|*no-store*) ;; *) echo "   NOTE: latest.json has no no-cache header (${cache:-none})" ;; esac
	local url; url=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["url"])' "$d/latest.json")
	curl -fsS --connect-timeout 15 -o "$d/pkg.deb" "$url" || die "cannot download $url"
	python3 - "$d/latest.json" "$d/pkg.deb" "$base" "$PUB" "$want" <<'PY'
import base64,hashlib,json,re,subprocess,sys,tempfile,os
from urllib.parse import urlsplit
manifest,deb,base,pub,want=sys.argv[1:6]
def fail(m): print("   FAIL:",m); sys.exit(1)
o=json.load(open(manifest)); version=o.get("version",""); url=o.get("url",""); sha=(o.get("sha256","") or "").lower(); sig=o.get("signature","")
def origin(u):
    s=urlsplit(u); return (s.scheme.lower(),(s.hostname or "").lower(),s.port or (443 if s.scheme=="https" else 80))
if origin(url)!=origin(base+"/latest.json"): fail("download URL %s is not on the update server"%url)
if not re.fullmatch(r"[0-9a-f]{64}",sha): fail("no valid sha256 in manifest")
key=base64.b64decode(pub); rawsig=base64.b64decode(sig)
if len(key)!=32 or len(rawsig)!=64: fail("bad key/signature length")
msg=("scribus-update-v1\n%s\n%s\n%s\n"%(version,url,sha)).encode()
with tempfile.TemporaryDirectory() as t:
    open(t+"/pub.der","wb").write(bytes.fromhex("302a300506032b6570032100")+key); open(t+"/msg","wb").write(msg); open(t+"/sig","wb").write(rawsig)
    r=subprocess.run(["openssl","pkeyutl","-verify","-pubin","-inkey",t+"/pub.der","-keyform","DER","-rawin","-in",t+"/msg","-sigfile",t+"/sig"],capture_output=True)
    if r.returncode!=0: fail("manifest signature does NOT verify with the public key in this build")
h=hashlib.sha256(open(deb,"rb").read()).hexdigest()
if h!=sha: fail("downloaded .deb sha256 %s != signed %s"%(h,sha))
if want and version!=want: fail("server offers %s, expected %s"%(version,want))
print("   ok: version %s, signature valid, sha256 matches, same origin, %d bytes"%(version,os.path.getsize(deb)))
PY
	local rc=$?
	rm -rf "${d:?}"
	return $rc
}

# ---------------------------------------------------------------- --list ---
if [ "$MODE" = list ]; then
	say "On the server ($UPLOAD_TARGET)"
	rsync --list-only "$UPLOAD_TARGET" | awk '$NF!="." {printf "   %10s  %s %s  %s\n",$2,$3,$4,$NF}'
	say "What the PCs see ($UPDATE_BASE_URL/latest.json)"
	TMP=$(mktemp)
	if curl -fsS --connect-timeout 10 -o "$TMP" "$UPDATE_BASE_URL/latest.json"; then
		python3 -c 'import json,sys; o=json.load(open(sys.argv[1])); print("   version:", o["version"]); print("   url:    ", o["url"]); print("   sha256: ", o["sha256"][:16]+"..."); print("   changelog:"); [print("     "+l) for l in o.get("changelog","").splitlines()]' "$TMP"
	else
		echo "   not reachable over HTTP (nginx location not set up yet, or server offline)"
	fi
	rm -f "${TMP:?}"
	exit 0
fi

# ---------------------------------------------------------- --rollback ---
if [ "$MODE" = rollback ]; then
	say "Rollback: latest.json -> $ROLLBACK_TO"
	DEB_NAME="scribus_${ROLLBACK_TO}_amd64.deb"
	lsrv | grep -qx "$DEB_NAME" || die "$DEB_NAME is not on the server (--list shows what is)"
	OUT="$BUILD/release/$ROLLBACK_TO"; mkdir -p "$OUT"
	[ -f "$OUT/$DEB_NAME" ] || curl -fsS -o "$OUT/$DEB_NAME" "$UPDATE_BASE_URL/$DEB_NAME" || die "cannot fetch the .deb back to sign it"
	tools/sign-update-manifest.sh sign "$PRIVATE_KEY" "$ROLLBACK_TO" "$UPDATE_BASE_URL/$DEB_NAME" "$OUT/$DEB_NAME" \
		"- Rolled back to $ROLLBACK_TO on $(date +%F)" > "$OUT/latest.json"
	up "$OUT/latest.json" "$UPLOAD_TARGET"   # rsync: temp file + rename on the server
	echo "   latest.json now points at $DEB_NAME"
	[ $NO_HTTP -eq 1 ] || http_check "$UPDATE_BASE_URL" "$ROLLBACK_TO"
	exit 0
fi

# -------------------------------------------------- 1. build, sign (local) ---
say "Build, package and sign on this laptop"
tools/release.sh --dry-run "${EXTRA[@]}" "${CHANGELOG[@]}" || die "release.sh failed"
VERSION=$(ls -t "$BUILD/release" | grep -E '^1\.[0-9]+\.[0-9]+-[0-9]{8}-[0-9]+$' | head -1)
OUT="$BUILD/release/$VERSION"
DEB_NAME="scribus_${VERSION}_amd64.deb"
[ -f "$OUT/$DEB_NAME" ] && [ -f "$OUT/latest.json" ] || die "release.sh left no $DEB_NAME / latest.json in $OUT"
grep -q "\"url\": \"$UPDATE_BASE_URL/$DEB_NAME\"" "$OUT/latest.json" || die "latest.json was signed for another server URL (release.conf changed after the build?)"

# ------------------------------------------- 2. detached sha256 and .sig ---
say "Detached checksum and signature"
( cd "$OUT" && sha256sum "$DEB_NAME" > "$DEB_NAME.sha256" )
openssl pkeyutl -sign -inkey "$PRIVATE_KEY" -rawin -in "$OUT/$DEB_NAME" | base64 -w0 > "$OUT/$DEB_NAME.sig"
echo "   $DEB_NAME.sha256, $DEB_NAME.sig (Ed25519 over the .deb bytes, base64)"

# ----------------------------------------------------------- 3. upload ---
say "Upload to $UPLOAD_TARGET"
rsync --list-only "$UPLOAD_TARGET" >/dev/null || die "cannot reach $UPLOAD_TARGET with rsync (ssh key of this user?)"
up "$OUT/$DEB_NAME" "$UPLOAD_TARGET";         echo "   1/4 $DEB_NAME"
up "$OUT/$DEB_NAME.sha256" "$UPLOAD_TARGET";  echo "   2/4 $DEB_NAME.sha256"
up "$OUT/$DEB_NAME.sig" "$UPLOAD_TARGET";     echo "   3/4 $DEB_NAME.sig"
up "$OUT/latest.json" "$UPLOAD_TARGET";       echo "   4/4 latest.json (last; temp name + rename on the server)"

# ------------------------------------------------------- 4. keep last N ---
say "Keep the last $KEEP releases"
ON_SERVER=$(lsrv | grep -E '^scribus_.*_amd64\.deb$' | sed -E 's/^scribus_(.*)_amd64\.deb$/\1/' | sort -t- -k2,2 -k3,3n || true)
TOTAL=$(printf '%s\n' "$ON_SERVER" | grep -c . || true)
if [ "$TOTAL" -gt "$KEEP" ]; then
	DROP=$(printf '%s\n' "$ON_SERVER" | head -n $((TOTAL-KEEP)))
	# rrsync allows no remote shell, so deletion goes through rsync itself: an
	# EMPTY source folder synced with --delete limited (by --include) to the
	# victims' names removes exactly those files and transfers nothing.
	D=$(mktemp -d)
	INCL=()
	for v in $DROP; do
		for suffix in "" .sha256 .sig; do INCL+=(--include="scribus_${v}_amd64.deb$suffix"); done
	done
	rsync -r --delete "${INCL[@]}" --exclude='*' "$D/" "$UPLOAD_TARGET" >/dev/null
	rmdir "${D:?}"
	echo "   removed: $(echo $DROP | tr '\n' ' ')"
else
	echo "   $TOTAL on the server, nothing to remove"
fi
echo "   now: $(lsrv | grep -E '^scribus_.*\.deb$' | tr '\n' ' ')"
echo "$VERSION" > "$BUILD/release/.last-version"

# --------------------------------------------------- 5. verify over HTTP ---
if [ $NO_HTTP -eq 1 ]; then
	say "HTTP check skipped (--no-http-check)"
else
	say "Read back over HTTP as a PC would"
	http_check "$UPDATE_BASE_URL" "$VERSION"
fi

# ------------------------------------------------------------- 6. tag ---
if [ $NO_TAG -eq 1 ]; then
	say "Published $VERSION (no git tag: --allow-dirty/--no-tag)"
elif git tag -a "release/$VERSION" -m "Release $VERSION" 2>/dev/null; then
	say "Published $VERSION, tagged release/$VERSION (git push origin release/$VERSION to share the tag)"
else
	say "Published $VERSION (tag release/$VERSION already exists)"
fi
