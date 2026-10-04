#!/bin/bash
# Run on the release laptop. Copies what the update server needs into
# ~/scribus-push-setup/ on the server and prints the one command that
# finishes the job there with sudo. Copies no private key: push.conf gets the
# release PUBLIC key from the build, and the signing key stays on this laptop.
#
#   tools/update-server/deploy-push-server.sh [--config FILE]
#
# Site settings (default ~/scribus-keys/push-update.conf, not in the repository):
#   SERVER=s1@192.0.2.20            # SSH login on the update server
#   SUBNET, SKIP_HOSTS, UPDATE_BASE_URL, PC_SSH_USER   # as used on the server
set -euo pipefail
CONFIG="${HOME}/scribus-keys/push-update.conf"
[ "${1:-}" = "--config" ] && CONFIG="${2:?--config needs a file}"
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
die() { printf 'deploy-push-server.sh: %s\n' "$*" >&2; exit 1; }
[ -f "$CONFIG" ] || die "no site settings at $CONFIG"
# shellcheck disable=SC1090
. "$CONFIG"
: "${SERVER:?SERVER is not set in $CONFIG}" "${SUBNET:?}" "${UPDATE_BASE_URL:?}"
PUBKEY=$(sed -n 's/^SCRIBUS_UPDATE_PUBKEY:STRING=//p' "$ROOT/build/CMakeCache.txt" 2>/dev/null || true)
[ -n "$PUBKEY" ] || die "no SCRIBUS_UPDATE_PUBKEY in build/CMakeCache.txt"
STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
cp "$ROOT/tools/push-update.sh" "$ROOT/tools/update-server/setup-push-server.sh" "$STAGE/"
cat > "$STAGE/push.conf" <<CONF
# /etc/scribus-push/push.conf - written by deploy-push-server.sh on the release laptop.
SUBNET=$SUBNET
SKIP_HOSTS="${SKIP_HOSTS:-}"
UPDATE_BASE_URL=${UPDATE_BASE_URL%/}
UPDATES_DIR=${UPDATES_DIR:-/srv/scribus-updates}
PUBKEY=$PUBKEY
PC_USER=${PC_SSH_USER:-s1}
SSH_KEY=/etc/scribus-push/push_key
CONF
# Belt and braces: nothing that looks like a private key goes to the server.
! grep -rlE "PRIVATE KEY|release-key" "$STAGE" >/dev/null || die "refusing to copy: a private key reference is in the staged files"
ssh -o BatchMode=yes "$SERVER" 'mkdir -p ~/scribus-push-setup'
rsync -t --chmod=F644 "$STAGE"/push-update.sh "$STAGE"/setup-push-server.sh "$STAGE"/push.conf "$SERVER:scribus-push-setup/"
echo "Copied to $SERVER:~/scribus-push-setup/ : push-update.sh, setup-push-server.sh, push.conf"
echo
echo "Now run this and type the sudo password of the server when asked:"
echo "  ssh -t $SERVER 'sudo bash ~/scribus-push-setup/setup-push-server.sh'"
