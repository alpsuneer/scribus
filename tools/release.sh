#!/bin/bash
# Build, package, sign, upload and verify one Scribus release for the
# built-in updater (scupdateclient + signed latest.json + pkexec dpkg -i).
#
#   tools/release.sh [--dry-run] [-m "changelog text"] [--config FILE] [--allow-dirty]
#
#   --dry-run      everything except the upload, the read-back from the server
#                  and the git tag. Leaves the .deb and latest.json in
#                  build/release/<version>/ for inspection.
#   -m TEXT        changelog shown to users. Default: the commit subjects since
#                  the last release tag.
#   --config FILE  site settings (default: ~/scribus-keys/release.conf).
#   --allow-dirty  only with --dry-run: skip the "everything committed" check.
#
# Before building, the laptop's "dbi" shortcut set is copied into the shipped
# keyset (tools/update-newspaper-shortcuts.sh) and checked for conflicts
# (tools/check-keyset-conflicts.py); a changed keyset is committed with the
# release and summarised in the changelog. SCRIBUS_PROFILE overrides the profile.
# The laptop's PDF export presets marked "Office preset" are copied the same way
# (tools/update-office-pdf-presets.py) into resources/pdf-presets, passwords
# left out, and committed with the release when they changed.
#
# Site settings are NOT in this repository (it is public). The config file is
# a shell fragment, created interactively on the first real run:
#
#   UPDATE_BASE_URL=http://debian.local:8081               # what /etc/scribus/update.conf on the office PCs says
#   UPDATE_API_KEY=                                        # empty for the LAN server; else the key the PCs send
#   UPLOAD_METHOD=copy           # copy | scp | rsync | http-put
#   UPLOAD_TARGET=/srv/scribus-updates                     # copy: local folder nginx serves; scp/rsync: user@host:/path/
#   UPLOAD_URL=https://updates.example.org/scribus-upload  # http-put (curl -T), optional UPLOAD_AUTH_HEADER='X-Upload-Token: ...'
#   PRIVATE_KEY=~/scribus-keys/release-key.pem             # optional, this is the default
#
# Version: <upstream>-YYYYMMDD-N, e.g. 1.7.3-20260930-1. N counts releases on
# the same day. The same string goes into the binary (what the updater
# compares) and into the .deb, and must be strictly higher than the previous
# release and than whatever the server currently offers.
set -euo pipefail

DRY_RUN=0; ALLOW_DIRTY=0; CHANGELOG_TEXT=""; CONFIG="${HOME}/scribus-keys/release.conf"
while [ $# -gt 0 ]; do
	case "$1" in
		--dry-run) DRY_RUN=1 ;;
		--allow-dirty) ALLOW_DIRTY=1 ;;
		-m) shift; CHANGELOG_TEXT="${1:?-m needs a text}" ;;
		--config) shift; CONFIG="${1:?--config needs a file}" ;;
		-h|--help) sed -n '2,31p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
	esac
	shift
done

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BUILD="$ROOT/build"
SIGN="$ROOT/tools/sign-update-manifest.sh"

say()  { printf '\n== %s\n' "$*"; }
die()  { printf '\nrelease.sh: %s\n' "$*" >&2; exit 1; }
for tool in git cmake openssl curl python3 sha256sum dpkg-deb objdump; do
	command -v "$tool" >/dev/null || die "missing tool: $tool"
done
[ $ALLOW_DIRTY -eq 0 ] || [ $DRY_RUN -eq 1 ] || die "--allow-dirty is only accepted together with --dry-run"

# --- version comparison: the same rule as ScUpdateClient::compareVersions ---
vercmp() { python3 - "$1" "$2" <<'PY'
import re,sys
def parse(v):
    m=re.search(r'(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?(?:-(\d+))?(?:-(\d+))?',v)
    return [int(x) if x else 0 for x in m.groups()] if m else [0]*6
a,b=parse(sys.argv[1]),parse(sys.argv[2])
print((a>b)-(a<b))
PY
}

# --- verify a manifest + installer exactly as the client does -------------
# $1 latest.json  $2 .deb  $3 base URL  $4 public key (base64 raw)  $5 version that must be older
verify_like_client() { python3 - "$@" <<'PY'
import base64,hashlib,json,re,subprocess,sys,tempfile,os
from urllib.parse import urlsplit
manifest,deb,base,pub,older=sys.argv[1:6]
def fail(msg): print("   FAIL:",msg); sys.exit(1)
obj=json.load(open(manifest))
version=obj.get("version",""); url=obj.get("url","") or obj.get("download_url","") or obj.get("deb_url","")
sha=(obj.get("sha256","") or "").lower(); sig=obj.get("signature","")
if not version: fail("manifest has no version")
def origin(u):
    s=urlsplit(u); port=s.port or (443 if s.scheme.lower()=="https" else 80)
    return (s.scheme.lower(), (s.hostname or "").lower(), port)
check_url=base.rstrip("/")+"/latest.json"
if origin(url)!=origin(check_url): fail("download URL %s is not on the update server %s"%(url,check_url))
if not re.fullmatch(r"[0-9a-f]{64}",sha): fail("manifest has no valid sha256")
key=base64.b64decode(pub)
if len(key)!=32: fail("public key is not 32 bytes")
rawsig=base64.b64decode(sig)
if len(rawsig)!=64: fail("signature is not 64 bytes")
msg=("scribus-update-v1\n%s\n%s\n%s\n"%(version,url,sha)).encode()
with tempfile.TemporaryDirectory() as d:
    der=bytes.fromhex("302a300506032b6570032100")+key
    open(os.path.join(d,"pub.der"),"wb").write(der); open(os.path.join(d,"msg"),"wb").write(msg); open(os.path.join(d,"sig"),"wb").write(rawsig)
    r=subprocess.run(["openssl","pkeyutl","-verify","-pubin","-inkey",os.path.join(d,"pub.der"),"-keyform","DER","-rawin","-in",os.path.join(d,"msg"),"-sigfile",os.path.join(d,"sig")],capture_output=True,text=True)
    if r.returncode!=0: fail("signature does not verify with the public key compiled into the build")
h=hashlib.sha256()
with open(deb,"rb") as f:
    for chunk in iter(lambda:f.read(1<<20),b""): h.update(chunk)
if h.hexdigest()!=sha: fail("installer sha256 %s does not match the signed %s"%(h.hexdigest(),sha))
def parse(v):
    m=re.search(r'(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?(?:-(\d+))?(?:-(\d+))?',v)
    return [int(x) if x else 0 for x in m.groups()] if m else [0]*6
if older and not parse(version)>parse(older): fail("version %s is not newer than %s"%(version,older))
print("   ok: same origin, sha256 %s..., signature valid, version %s%s"%(sha[:16],version,(" > "+older) if older else ""))
PY
}

# ---------------------------------------------------------------- 0. key ---
say "Release key"
[ -f "$CONFIG" ] && . "$CONFIG"
PRIVATE_KEY="${PRIVATE_KEY:-${HOME}/scribus-keys/release-key.pem}"
PRIVATE_KEY="${PRIVATE_KEY/#\~/$HOME}"
if [ ! -f "$PRIVATE_KEY" ]; then
	cat >&2 <<TXT

No release key at $PRIVATE_KEY. Create it once, outside the repository:

  mkdir -p ~/scribus-keys && chmod 700 ~/scribus-keys
  tools/sign-update-manifest.sh keygen ~/scribus-keys/release-key.pem

That prints the PUBLIC key. Build Scribus with it, so every copy can verify
what this key signs:

  cmake -S . -B build -DSCRIBUS_UPDATE_PUBKEY=<the printed value>

Keep the .pem private: off the update server, out of git (*.pem is ignored),
and backed up somewhere safe. Whoever has it can push software to every
office PC. Then run tools/release.sh again.
TXT
	exit 1
fi
PUB_FROM_KEY=$(openssl pkey -in "$PRIVATE_KEY" -pubout -outform DER | tail -c 32 | base64 -w0)
PUB_IN_BUILD=$(sed -n 's/^SCRIBUS_UPDATE_PUBKEY:STRING=//p' "$BUILD/CMakeCache.txt" 2>/dev/null || true)
if [ "$PUB_FROM_KEY" != "$PUB_IN_BUILD" ]; then
	die "the build's SCRIBUS_UPDATE_PUBKEY (${PUB_IN_BUILD:-empty}) is not the public half of $PRIVATE_KEY.
Office PCs would refuse everything this key signs. Reconfigure once:
  cmake -S . -B build -DSCRIBUS_UPDATE_PUBKEY=$PUB_FROM_KEY"
fi
echo "   private key: $PRIVATE_KEY"
echo "   public key compiled into the build: $PUB_IN_BUILD"

# ------------------------------------------------------------ 1. config ---
say "Update server settings"
if [ -z "${UPDATE_BASE_URL:-}" ]; then
	if [ $DRY_RUN -eq 1 ]; then
		UPDATE_BASE_URL="https://update-server.invalid/scribus"
		echo "   no $CONFIG yet: dry run uses the placeholder $UPDATE_BASE_URL"
		echo "   (the URL is part of what is signed, so this dry-run manifest is not uploadable)"
	else
		[ -t 0 ] || die "no $CONFIG and no terminal to ask on. Create it (see --help)."
		echo "   No $CONFIG yet. Answer once; it is saved there (mode 600), outside the repo."
		read -r -p "   Update server URL the office PCs use (e.g. https://host/scribus): " UPDATE_BASE_URL
		read -r -p "   API key those PCs send, empty for none (used to read the upload back): " UPDATE_API_KEY
		read -r -p "   Upload method [copy/scp/rsync/http-put]: " UPLOAD_METHOD
		case "$UPLOAD_METHOD" in
			copy)      read -r -p "   Local folder the web server serves at that URL (e.g. /srv/scribus-updates): " UPLOAD_TARGET ;;
			scp|rsync) read -r -p "   Upload target (user@host:/path/served/at/that/URL/): " UPLOAD_TARGET ;;
			http-put)  read -r -p "   Upload URL (files are PUT to <this>/<name>): " UPLOAD_URL
			           read -r -p "   Extra upload header, or empty (e.g. X-Upload-Token: abc): " UPLOAD_AUTH_HEADER ;;
			*) die "unknown upload method: $UPLOAD_METHOD" ;;
		esac
		mkdir -p "$(dirname "$CONFIG")"
		( umask 077; {
			printf 'UPDATE_BASE_URL=%q\nUPDATE_API_KEY=%q\nUPLOAD_METHOD=%q\n' "$UPDATE_BASE_URL" "$UPDATE_API_KEY" "$UPLOAD_METHOD"
			printf 'UPLOAD_TARGET=%q\nUPLOAD_URL=%q\nUPLOAD_AUTH_HEADER=%q\n' "${UPLOAD_TARGET:-}" "${UPLOAD_URL:-}" "${UPLOAD_AUTH_HEADER:-}"
		} > "$CONFIG" )
		echo "   saved $CONFIG"
	fi
fi
UPDATE_BASE_URL="${UPDATE_BASE_URL%/}"
echo "   server URL: $UPDATE_BASE_URL"
if [ $DRY_RUN -eq 0 ]; then
	case "${UPLOAD_METHOD:-}" in
		copy)      [ -d "${UPLOAD_TARGET:-}" ] || die "UPLOAD_TARGET ($UPLOAD_TARGET) is not a folder"; [ -w "$UPLOAD_TARGET" ] || die "cannot write to $UPLOAD_TARGET (owner/permissions)" ;;
		scp|rsync) [ -n "${UPLOAD_TARGET:-}" ] || die "UPLOAD_TARGET is not set in $CONFIG" ; command -v "$UPLOAD_METHOD" >/dev/null || die "missing tool: $UPLOAD_METHOD" ;;
		http-put)  [ -n "${UPLOAD_URL:-}" ] || die "UPLOAD_URL is not set in $CONFIG" ;;
		*) die "UPLOAD_METHOD in $CONFIG must be copy, scp, rsync or http-put" ;;
	esac
	[ -n "${UPDATE_API_KEY:-}" ] || echo "   no API key: the read-back is done without authentication, as the office PCs do it"
	echo "   upload: ${UPLOAD_METHOD} -> ${UPLOAD_TARGET:-${UPLOAD_URL:-}}"
fi
fetch() { # $1 url  $2 output   (authenticates the way the client does: no key, no headers)
	if [ -n "${UPDATE_API_KEY:-}" ]; then
		curl -fsS --connect-timeout 15 -H "Authorization: Bearer $UPDATE_API_KEY" -H "X-API-Key: $UPDATE_API_KEY" -o "$2" "$1"
	else
		curl -fsS --connect-timeout 15 -o "$2" "$1"
	fi
}

# ------------------------------------------------------------ 3. version ---
say "Version"
UPSTREAM=$(python3 - <<'PY'
import re
s=open("CMakeLists.txt").read()
g=lambda k: re.search(r'set \(%s "(\d+)"\)'%k,s).group(1)
print("%s.%s.%s"%(g("VERSION_MAJOR"),g("VERSION_MINOR"),g("VERSION_PATCH")))
PY
)
TODAY=$(date +%Y%m%d)
LAST_TAG=$(git tag --list 'release/*' --sort=-creatordate | head -1)
LAST_VERSION="${LAST_TAG#release/}"
SERVER_VERSION=""
if [ "$UPDATE_BASE_URL" != "https://update-server.invalid/scribus" ]; then
	TMPM=$(mktemp)
	if fetch "$UPDATE_BASE_URL/latest.json" "$TMPM" 2>/dev/null; then
		SERVER_VERSION=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("version",""))' "$TMPM" 2>/dev/null || true)
	fi
	rm -f "$TMPM"
fi
N=1
for v in "$LAST_VERSION" "$SERVER_VERSION" "$(cat "$BUILD/release/.last-version" 2>/dev/null || true)"; do
	case "$v" in "$UPSTREAM-$TODAY-"*) n=${v##*-}; [ "$n" -ge "$N" ] 2>/dev/null && N=$((n+1)) ;; esac
done
VERSION="$UPSTREAM-$TODAY-$N"
echo "   last release tag:      ${LAST_VERSION:-none}"
echo "   on the server now:     ${SERVER_VERSION:-unknown (not reachable or not configured)}"
echo "   this release:          $VERSION"
for older in "$LAST_VERSION" "$SERVER_VERSION"; do
	[ -z "$older" ] || [ "$(vercmp "$VERSION" "$older")" = "1" ] || die "$VERSION is not strictly higher than $older"
done
INSTALLED=$(dpkg-query -W -f='${Version}' scribus 2>/dev/null || true)
if [ -n "$INSTALLED" ]; then
	dpkg --compare-versions "$VERSION" gt "$INSTALLED" && echo "   dpkg agrees it is newer than the installed $INSTALLED" \
		|| echo "   NOTE: dpkg does not rank it above the installed $INSTALLED (dpkg -i would still install it)"
fi

# ------------------------------------------------ 3b. shortcut set "dbi" ---
say "Shortcut set"
# The laptop's current "dbi" set becomes the keyset the package ships. Stops
# with a clear message when the set is missing, and before shipping a set with
# keys that cannot work (duplicates inside the set, clashes with the Column
# Style / Design Style / Next Style Chain keys).
KEYSET_SUMMARY=$(mktemp)
tools/update-newspaper-shortcuts.sh --version "$VERSION" --summary "$KEYSET_SUMMARY" | sed 's/^/   /' || die "shortcut set not updated (see above)"
tools/check-keyset-conflicts.py resources/keysets/malayalam-dtp.xml --profile "${SCRIBUS_PROFILE:-/home/s1/.config/scribus}/.." | sed 's/^/   /' || die "the dbi shortcut set has conflicts; fix them in Preferences > Keyboard Shortcuts and release again"
KEYSET_CHANGELOG=$(head -1 "$KEYSET_SUMMARY")
KEYSET_DETAIL=$(tail -n +2 "$KEYSET_SUMMARY")
rm -f "$KEYSET_SUMMARY"
if ! git -c core.fileMode=false diff --quiet -- resources/keysets/malayalam-dtp.xml; then
	if [ $DRY_RUN -eq 1 ]; then
		echo "   keyset changed; left uncommitted (dry run)"
	else
		git add resources/keysets/malayalam-dtp.xml
		git commit -q -m "suneer: keyset: dbi shortcut set for $VERSION

$KEYSET_CHANGELOG
$KEYSET_DETAIL" && echo "   keyset change committed: $(git rev-parse --short HEAD)"
	fi
else
	echo "   keyset unchanged since the committed one"
fi

# ------------------------------------------- 3b2. office PDF presets ---
say "Office PDF presets"
# The presets ticked "Office preset" in Save as PDF on this laptop become the
# read-only presets the package ships; the laptop's Default, when it is one of
# them, becomes the office Default. A user's own Default on an office PC is
# kept: it lives in that user's profile, which the package never writes to.
PRESET_SUMMARY=$(mktemp)
tools/update-office-pdf-presets.py --profile "${SCRIBUS_PROFILE:-/home/s1/.config/scribus}" --dest resources/pdf-presets --summary "$PRESET_SUMMARY" | sed 's/^/   /' || die "office PDF presets not updated (see above)"
PRESET_CHANGELOG=$(head -1 "$PRESET_SUMMARY")
PRESET_DETAIL=$(tail -n +2 "$PRESET_SUMMARY")
rm -f "$PRESET_SUMMARY"
if [ -n "$(git -c core.fileMode=false status --porcelain -- resources/pdf-presets)" ]; then
	if [ $DRY_RUN -eq 1 ]; then
		echo "   office PDF presets changed; left uncommitted (dry run)"
	else
		git add -A resources/pdf-presets
		git commit -q -m "suneer: pdf presets: office presets for $VERSION

$PRESET_CHANGELOG
$PRESET_DETAIL" && echo "   office PDF presets committed: $(git rev-parse --short HEAD)"
	fi
else
	echo "   office PDF presets unchanged since the committed ones"
fi

# ------------------------------------------------- 3c. working tree clean ---
say "Working tree"
# core.fileMode=false: this tree carries hundreds of mode-only changes (files
# that merely lost or gained the executable bit); they are not content.
DIRTY=$(git -c core.fileMode=false status --porcelain --untracked-files=no)
if [ -n "$DIRTY" ]; then
	echo "$DIRTY" | head -20 | sed 's/^/   /'
	[ "$(echo "$DIRTY" | wc -l)" -le 20 ] || echo "   ... ($(echo "$DIRTY" | wc -l) files)"
	if [ $ALLOW_DIRTY -eq 1 ]; then
		echo "   uncommitted changes present; continuing because of --dry-run --allow-dirty"
	else
		die "uncommitted changes. A release must be built from committed code, so the tag says exactly what shipped."
	fi
else
	echo "   clean: $(git rev-parse --short HEAD) on $(git rev-parse --abbrev-ref HEAD)"
fi

# ---------------------------------------------------------- 4. changelog ---
say "Changelog"
if [ -n "$CHANGELOG_TEXT" ]; then
	CHANGELOG="$CHANGELOG_TEXT"
elif [ -n "$LAST_TAG" ]; then
	CHANGELOG=$(git log --no-merges --pretty='- %s' "$LAST_TAG..HEAD" | sed 's/^- suneer: /- /')
else
	CHANGELOG=$(git log --no-merges --pretty='- %s' -n 15 | sed 's/^- suneer: /- /')
fi
[ -n "$CHANGELOG" ] || CHANGELOG="- Maintenance build."
case "$KEYSET_CHANGELOG" in *" 0 key(s) added, 0 removed, 0 changed") ;; *) CHANGELOG="$CHANGELOG
- $KEYSET_CHANGELOG" ;; esac
echo "$CHANGELOG" | sed 's/^/   /'

# -------------------------------------------------------------- 5. build ---
say "Build ($VERSION)"
mkdir -p "$BUILD/release"
BUILD_LOG="$BUILD/release/build-$VERSION.log"
cmake -S "$ROOT" -B "$BUILD" -DSCRIBUS_RELEASE_VERSION="$VERSION" >"$BUILD_LOG" 2>&1 || { tail -20 "$BUILD_LOG"; die "cmake configure failed (log: $BUILD_LOG)"; }
cmake --build "$BUILD" -j"$(nproc)" >>"$BUILD_LOG" 2>&1 || { grep -E "error" "$BUILD_LOG" | head -20; die "build failed (log: $BUILD_LOG)"; }
BIN="$BUILD/scribus/scribus"
grep -aqF "$VERSION" "$BIN" || die "the built binary does not contain $VERSION"
grep -aqF "$PUB_IN_BUILD" "$BIN" || die "the built binary does not contain the release public key"
echo "   binary carries version $VERSION and the release public key"

# ---------------------------------------------------------------- 6. deb ---
say "Package"
OUT="$BUILD/release/$VERSION"
rm -rf "$OUT"; mkdir -p "$OUT"
DEB_NAME="scribus_${VERSION}_amd64.deb"
# Depends: the packages that own the shared libraries the binary and its
# plugins link to. Names only, no versions: the installer is dpkg -i, which
# does not fetch anything, so a version floor taken from this machine would
# only make installs fail on a PC that is a few point releases behind.
LDCONFIG=$(command -v ldconfig || echo /sbin/ldconfig)     # not on PATH for ordinary users
LDCACHE=$("$LDCONFIG" -p)
DEPENDS=$(
	{ echo "$BIN"; find "$BUILD/scribus" -name '*.so' -type f; } | while read -r f; do objdump -p "$f" 2>/dev/null | awk '/NEEDED/{print $2}'; done | sort -u |
	while read -r lib; do
		path=$(echo "$LDCACHE" | awk -v l="$lib" '$1==l && /x86-64/ {print $NF; exit}')
		[ -n "$path" ] || continue            # our own libraries, shipped in the package
		dpkg -S "$(readlink -f "$path")" 2>/dev/null | cut -d: -f1
	done | sort -u | grep -vx scribus | paste -sd, | sed 's/,/, /g'
)
[ -n "$DEPENDS" ] || die "could not work out the package dependencies"
# The push helper (/usr/local/sbin/scribus-install-update) is a Python script;
# no shared library points at the interpreter, so it is named here.
case ", $DEPENDS," in *", python3,"*) ;; *) DEPENDS="$DEPENDS, python3" ;; esac
echo "   Depends: $(echo "$DEPENDS" | tr ',' '\n' | wc -l) packages"
# Built with dpkg-deb from a DESTDIR install, not with `cpack -G DEB`: on this
# tree CPack's DEB generator spins for more than ten minutes before it writes
# anything. The staged tree is the same one CPack would package (the install
# rules honour DESTDIR, including the scribus -> scribus.bin launcher wrapper).
STAGE="$OUT/stage"
DESTDIR="$STAGE" cmake --install "$BUILD" >"$OUT/install.log" 2>&1 || { tail -20 "$OUT/install.log"; die "staging install failed (log: $OUT/install.log)"; }
[ -x "$STAGE/usr/local/bin/scribus.bin" ] || die "staged tree has no usr/local/bin/scribus.bin"
mkdir -p "$STAGE/DEBIAN"
MAINTAINER=$(sed -n 's/.*set(CPACK_PACKAGE_CONTACT "\(.*\)").*/\1/p' "$ROOT/CMakeLists.txt" | head -1)
cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: scribus
Version: $VERSION
Architecture: $(dpkg --print-architecture)
Maintainer: ${MAINTAINER:-Scribus release <root@localhost>}
Installed-Size: $(du -sk "$STAGE/usr" | cut -f1)
Depends: $DEPENDS
Recommends: gdb
Section: graphics
Priority: optional
Description: Scribus $UPSTREAM page layout, Faircode newspaper build
 Release $VERSION, built by tools/release.sh for the built-in updater.
CONTROL
# No maintainer scripts on purpose. The crash-capture postinst/postrm that the
# CPack config attaches (housekeeping timer, .sla association pointed at
# scribus-debug) are left out of updater releases: the office PCs never had
# them, and an update must not change what opens a document. The crash-capture
# files themselves still ship; they are simply not activated.
# /etc/scribus/update.conf (server URL) is a conffile: dpkg keeps a locally
# edited copy on upgrade and asks before replacing it.
[ -f "$STAGE/etc/scribus/update.conf" ] || die "staged tree has no etc/scribus/update.conf (configure with -DSCRIBUS_UPDATE_DEFAULT_URL=...)"
grep -q '^url=.\+' "$STAGE/etc/scribus/update.conf" || echo "   NOTE: update.conf has an empty url= (SCRIBUS_UPDATE_DEFAULT_URL not set); PCs will need Update Settings"
echo "/etc/scribus/update.conf" > "$STAGE/DEBIAN/conffiles"
( cd "$STAGE" && find usr etc -type f -print0 | xargs -0 md5sum > DEBIAN/md5sums )
DEB="$OUT/$DEB_NAME"
dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$DEB" >"$OUT/dpkg-deb.log" 2>&1 || { cat "$OUT/dpkg-deb.log"; die "dpkg-deb failed"; }
cp "$STAGE/etc/scribus/update.conf" "$OUT/update.conf.check"
rm -rf "$STAGE"
[ "$(dpkg-deb -f "$DEB" Package)" = "scribus" ] || die "package name is not scribus"
[ "$(dpkg-deb -f "$DEB" Version)" = "$VERSION" ] || die ".deb version is $(dpkg-deb -f "$DEB" Version), expected $VERSION"
# Checked from files, not through pipes: grep -q closing a pipe early would
# look like a failure under pipefail.
dpkg-deb -c "$DEB" > "$OUT/contents.txt"
grep -q '/usr/local/lib/scribus/plugins/.*\.so' "$OUT/contents.txt" || die ".deb has no plugins under /usr/local/lib/scribus/plugins"
grep -q '/usr/local/bin/scribus$' "$OUT/contents.txt" || die ".deb has no /usr/local/bin/scribus launcher"
# Push installs from the update server: the helper must be root-owned and
# carry this build's public key; the sudoers entry, when the build names a push
# account, must be 0440 and parse - a broken file in sudoers.d breaks sudo.
grep -q '^-rwxr-xr-x root/root .* \./usr/local/sbin/scribus-install-update$' "$OUT/contents.txt" || die ".deb has no root-owned /usr/local/sbin/scribus-install-update"
dpkg-deb --fsys-tarfile "$DEB" | tar -xO ./usr/local/sbin/scribus-install-update > "$OUT/helper.check"
grep -qF "PUBKEY = \"$PUB_IN_BUILD\"" "$OUT/helper.check" || die "the install helper inside the .deb does not carry the release public key"
rm -f "$OUT/helper.check"
PUSH_USER=$(sed -n 's/^SCRIBUS_PUSH_USER:STRING=//p' "$BUILD/CMakeCache.txt" 2>/dev/null || true)
if [ -n "$PUSH_USER" ]; then
	grep -q '^-r--r----- root/root .* \./etc/sudoers.d/scribus-push$' "$OUT/contents.txt" || die ".deb has no 0440 root-owned /etc/sudoers.d/scribus-push"
	dpkg-deb --fsys-tarfile "$DEB" | tar -xO ./etc/sudoers.d/scribus-push > "$OUT/sudoers.check"
	VISUDO=$(command -v visudo || echo /usr/sbin/visudo)
	"$VISUDO" -cf "$OUT/sudoers.check" >/dev/null || die "the sudoers entry inside the .deb does not parse"
	[ "$(grep -vc '^#' "$OUT/sudoers.check")" = "1" ] && grep -qx "$PUSH_USER ALL=(root) NOPASSWD: /usr/local/sbin/scribus-install-update" "$OUT/sudoers.check" \
		|| die "the sudoers entry inside the .deb allows something other than the install helper"
	rm -f "$OUT/sudoers.check"
	echo "   push installs: $PUSH_USER may run scribus-install-update (and nothing else) without a password"
else
	echo "   NOTE: SCRIBUS_PUSH_USER is not set; the .deb ships the install helper but no sudoers entry"
fi
dpkg-deb --fsys-tarfile "$DEB" | tar -xO ./usr/local/bin/scribus.bin > "$OUT/scribus.bin.check"
grep -aqF "$VERSION" "$OUT/scribus.bin.check" || die "the scribus.bin inside the .deb does not carry $VERSION"
grep -aqF "$PUB_IN_BUILD" "$OUT/scribus.bin.check" || die "the scribus.bin inside the .deb does not carry the release public key"
rm -f "$OUT/scribus.bin.check"
echo "   $DEB_NAME  $(du -h "$DEB" | cut -f1)  Version: $(dpkg-deb -f "$DEB" Version)"

# --------------------------------------------------------------- 7. sign ---
say "Sign"
DEB_URL="$UPDATE_BASE_URL/$DEB_NAME"
MANIFEST="$OUT/latest.json"
"$SIGN" sign "$PRIVATE_KEY" "$VERSION" "$DEB_URL" "$DEB" "$CHANGELOG" > "$MANIFEST"
echo "   $MANIFEST"
echo "   local check, same rules as the client:"
verify_like_client "$MANIFEST" "$DEB" "$UPDATE_BASE_URL" "$PUB_IN_BUILD" "${SERVER_VERSION:-$LAST_VERSION}"
mkdir -p "$BUILD/release"

if [ $DRY_RUN -eq 1 ]; then
	say "Dry run: nothing uploaded, no tag"
	echo "   would upload: $DEB"
	echo "                 $MANIFEST"
	echo "   to:           ${UPLOAD_METHOD:-<method>} ${UPLOAD_TARGET:-${UPLOAD_URL:-<target>}}"
	echo "   update.conf in the package: $(grep '^url=' "$OUT/update.conf.check" 2>/dev/null || echo '(see package)')"
	exit 0
fi

# ------------------------------------------------------------- 8. upload ---
say "Upload"
upload() { # $1 file
	case "$UPLOAD_METHOD" in
		copy)  install -m 644 "$1" "$UPLOAD_TARGET/" ;;
		scp)   scp -q "$1" "$UPLOAD_TARGET" ;;
		rsync) rsync -t --chmod=F644 "$1" "$UPLOAD_TARGET" ;;
		http-put) curl -fsS --connect-timeout 15 ${UPLOAD_AUTH_HEADER:+-H "$UPLOAD_AUTH_HEADER"} -T "$1" "${UPLOAD_URL%/}/$(basename "$1")" >/dev/null ;;
	esac
}
# The installer first: a client must never see a manifest that points at a
# file that is not there yet.
upload "$DEB";      echo "   uploaded $DEB_NAME"
upload "$MANIFEST"; echo "   uploaded latest.json"

# ---------------------------------------------------------- 9. read back ---
say "Read back from the server, as a client"
BACK="$OUT/readback"; mkdir -p "$BACK"
fetch "$UPDATE_BASE_URL/latest.json" "$BACK/latest.json" || die "could not download $UPDATE_BASE_URL/latest.json with the API key"
BACK_URL=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["url"])' "$BACK/latest.json")
fetch "$BACK_URL" "$BACK/$DEB_NAME" || die "could not download $BACK_URL with the API key"
verify_like_client "$BACK/latest.json" "$BACK/$DEB_NAME" "$UPDATE_BASE_URL" "$PUB_IN_BUILD" "${SERVER_VERSION:-$LAST_VERSION}"
cmp -s "$BACK/latest.json" "$MANIFEST" || die "the server is serving a different latest.json than the one just uploaded"
rm -rf "$BACK"

# ----------------------------------------------------------------- 10. tag --
echo "$VERSION" > "$BUILD/release/.last-version"
git tag -a "release/$VERSION" -m "Release $VERSION

$CHANGELOG"
say "Released $VERSION"
echo "   tag release/$VERSION created locally (git push origin release/$VERSION to publish it)"
echo "   office PCs will be offered it at their next daily check, or from Help > Check for Updates..."
