#!/bin/bash
# A laptop test package ("the usual way": checkinstall + apt install), numbered so that it can never pull a
# PC back to an older release:
#   package   <upstream>-YYYYMMDD-0.localN     e.g. 1.7.3-20261031-0.local2
#   binary    <upstream>-YYYYMMDD-0            (what the built-in updater compares)
# Above every earlier day's release, below the same day's real release (tools/release.sh: -N, N >= 1), so a
# release always replaces a test package and a test package always replaces anything older.
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
# next N of today: the packages already in build/ and what is installed
N=0
for v in $(ls "$BUILD" 2>/dev/null | sed -n "s/^scribus_${UPSTREAM}-${TODAY}-0\.local\([0-9]\+\)_amd64\.deb$/\1/p") \
         $(dpkg-query -W -f='${Version}' scribus 2>/dev/null | sed -n "s/^${UPSTREAM}-${TODAY}-0\.local\([0-9]\+\)$/\1/p"); do
	[ "$v" -gt "$N" ] && N=$v
done
N=$((N + 1))
PKGREL="${TODAY}-0.local${N}"
BINVER="${UPSTREAM}-${TODAY}-0"
[[ "$PKGREL" =~ ^20[2-9][0-9](0[1-9]|1[0-2])(0[1-9]|[12][0-9]|3[01])-0\.local[1-9][0-9]*$ ]] || die "package release $PKGREL is not YYYYMMDD-0.localN"
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
dpkg-deb --fsys-tarfile "$DEB" | tar -xO ./usr/local/bin/scribus.bin | grep -aqF "$BINVER" || die "the binary in $DEB does not carry $BINVER"
# the build tree stays the repository owner's (release.sh runs as that user)
chown -R "$OWNER" "$BUILD"
echo "   $DEB  $(du -h "$DEB" | cut -f1)"
if [ $INSTALL -eq 1 ]; then
	apt install -y --allow-downgrades "$DEB" > "$BUILD/apt-$PKGREL.log" 2>&1 || die "apt install failed (log: $BUILD/apt-$PKGREL.log)"
	[ "$(dpkg-query -W -f='${Version}' scribus)" = "$VERSION" ] || die "installed version is not $VERSION"
	echo "   installed: $VERSION"
fi
