#!/bin/bash
# A laptop test package ("the usual way": checkinstall + apt install), numbered so that it can never pull a
# PC back to an older release:
#   package = binary  <upstream>-YYYYMMDD-N.localM   e.g. 1.7.3-20261031-1.local2
# N = today's latest published release (0 when none), M counts test packages. dpkg ranks it above that release
# and below the next one (N+1); the built-in updater ignores ".localM" and treats it as release N, so it never
# offers the same-day release over it (that replaced a test package on 2026-10-31) and the next release still
# replaces it.
# The old DDMMYY numbers (1.7.3-311026-1) ranked BELOW every YYYYMMDD release in dpkg and in the updater.
#
#   sudo tools/local-deb.sh [--no-install]
#
# The number is worked out here (today, next free N) and checked; there is no way to pass another one.
# A test package is not signed and not a release: it is not for the office PCs.
set -euo pipefail
die() { printf '\nlocal-deb.sh: %s\n' "$*" >&2; exit 1; }
INSTALL=1
[ "${1:-}" = "--no-install" ] && INSTALL=0
[ "$(id -u)" = 0 ] || die "run as root (checkinstall installs into /usr/local)"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
OWNER=$(stat -c %u:%g "$ROOT")
cd "$ROOT"
UPSTREAM=$(python3 - <<'PY'
import re
s=open("CMakeLists.txt").read()
g=lambda k: re.search(r'set \(%s "(\d+)"\)'%k,s).group(1)
print("%s.%s.%s"%(g("VERSION_MAJOR"),g("VERSION_MINOR"),g("VERSION_PATCH")))
PY
)
TODAY=$(date +%Y%m%d)
# N = today's latest published release (git tag release/<upstream>-<today>-N, or the server's version from
# build/release/.last-version), 0 when there is none: the test package ranks above that release and below the
# next one. A package below today's release was replaced by the laptop's own updater (2026-10-31).
REL=0
for v in $(git tag --list "release/${UPSTREAM}-${TODAY}-*" | sed "s|^release/${UPSTREAM}-${TODAY}-||") \
         $(sed -n "s/^${UPSTREAM}-${TODAY}-\([0-9]\+\)$/\1/p" "$BUILD/release/.last-version" 2>/dev/null); do
	[ "$v" -gt "$REL" ] 2>/dev/null && REL=$v
done
# next M of today for this N: the packages already in build/ and what is installed
N=0
for v in $(ls "$BUILD" 2>/dev/null | sed -n "s/^scribus_${UPSTREAM}-${TODAY}-${REL}\.local\([0-9]\+\)_amd64\.deb$/\1/p") \
         $(dpkg-query -W -f='${Version}' scribus 2>/dev/null | sed -n "s/^${UPSTREAM}-${TODAY}-${REL}\.local\([0-9]\+\)$/\1/p"); do
	[ "$v" -gt "$N" ] && N=$v
done
N=$((N + 1))
PKGREL="${TODAY}-${REL}.local${N}"
BINVER="${UPSTREAM}-${PKGREL}"
[[ "$PKGREL" =~ ^20[2-9][0-9](0[1-9]|1[0-2])(0[1-9]|[12][0-9]|3[01])-(0|[1-9][0-9]*)\.local[1-9][0-9]*$ ]] || die "package release $PKGREL is not YYYYMMDD-N.localM"
VERSION="${UPSTREAM}-${PKGREL}"
INSTALLED=$(dpkg-query -W -f='${Version}' scribus 2>/dev/null || true)
if [ -n "$INSTALLED" ] && ! dpkg --compare-versions "$VERSION" gt "$INSTALLED"; then
	die "$VERSION would not be newer than the installed $INSTALLED (a release of today is installed? use that)"
fi
echo "== test package $VERSION (binary $BINVER)"
cmake -S "$ROOT" -B "$BUILD" -DSCRIBUS_RELEASE_VERSION="$BINVER" > /dev/null
cmake --build "$BUILD" -j"$(nproc)" > "$BUILD/build-$PKGREL.log" 2>&1 \
	|| { grep -E " error" "$BUILD/build-$PKGREL.log" | head -10; die "build failed (log: $BUILD/build-$PKGREL.log)"; }
cd "$BUILD"
PATH=/usr/sbin:/sbin:$PATH checkinstall --type=debian --pkgname=scribus --pkgversion="$UPSTREAM" \
	--pkgrelease="$PKGREL" --pkggroup=graphics --pkgsource="https://www.scribus.net" --pkglicense=GPL \
	--maintainer="alp.suneer@gmail.com" --requires="cups-bsd,cups-client" --deldoc=yes --deldesc=yes \
	--delspec=yes --backup=no --fstrans=no --install=no --default make install > "$BUILD/checkinstall-$PKGREL.log" 2>&1 \
	|| die "checkinstall failed (log: $BUILD/checkinstall-$PKGREL.log)"
DEB="$BUILD/scribus_${VERSION}_amd64.deb"
[ -f "$DEB" ] || die "no $DEB"
[ "$(dpkg-deb -f "$DEB" Version)" = "$VERSION" ] || die "$DEB is not version $VERSION"
# checked from a file, not through a pipe: grep -q closing the pipe early looks like a failure under pipefail
dpkg-deb --fsys-tarfile "$DEB" | tar -xO ./usr/local/bin/scribus.bin > "$BUILD/scribus.bin.check"
grep -aqF "$BINVER" "$BUILD/scribus.bin.check" || die "the binary in $DEB does not carry $BINVER"
rm -f "$BUILD/scribus.bin.check"
# the build tree stays the repository owner's (release.sh runs as that user)
chown -R "$OWNER" "$BUILD"
echo "   $DEB  $(du -h "$DEB" | cut -f1)"
if [ $INSTALL -eq 1 ]; then
	apt install -y --allow-downgrades "$DEB" > "$BUILD/apt-$PKGREL.log" 2>&1 || die "apt install failed (log: $BUILD/apt-$PKGREL.log)"
	[ "$(dpkg-query -W -f='${Version}' scribus)" = "$VERSION" ] || die "installed version is not $VERSION"
	echo "   installed: $VERSION"
fi
