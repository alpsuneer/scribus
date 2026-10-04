#!/bin/bash
# Run ONCE on the update server, as root, from the folder
# tools/update-server/deploy-push-server.sh copied there:
#
#   sudo bash ~/scribus-push-setup/setup-push-server.sh
#
# Sets up, and nothing else:
#   - nginx (nginx-light) serving UPDATES_DIR read-only on the server's LAN
#     address, port 8081, to SUBNET only; enabled at boot. The default site
#     that a fresh nginx puts on port 80 is switched off - but only when this
#     script installed nginx itself. An nginx that was already there is left
#     as it is and only gets the one extra site.
#   - UPDATES_DIR, owned by the push account so tools/release.sh can rsync into it.
#   - /usr/local/bin/scribus-push-update and /etc/scribus-push/ (push.conf, an
#     empty pcs list on the first run, known_hosts).
#   - a dedicated SSH key for pushing, /etc/scribus-push/push_key, readable only
#     by the push account. It is generated here and never leaves the server.
#
# The release PRIVATE signing key is not needed here and must never be copied
# here. push.conf carries the PUBLIC key only.
# Safe to run again: it rewrites its own files and keeps the pcs list and the key.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
[ -f "$HERE/push.conf" ] && [ -f "$HERE/push-update.sh" ] || { echo "push.conf / push-update.sh are not next to this script" >&2; exit 1; }
# shellcheck disable=SC1091
. "$HERE/push.conf"
: "${SUBNET:?}" "${UPDATE_BASE_URL:?}" "${PUBKEY:?}"
UPDATES_DIR="${UPDATES_DIR:-/srv/scribus-updates}"
PUSH_ACCOUNT="${SUDO_USER:-${PUSH_ACCOUNT:-}}"
[ -n "$PUSH_ACCOUNT" ] && id "$PUSH_ACCOUNT" >/dev/null || { echo "cannot tell which account runs the push (use sudo from that account)" >&2; exit 1; }
LISTEN=$(echo "$UPDATE_BASE_URL" | sed -E 's|^https?://||; s|/.*$||')      # address:port the PCs use
case "$LISTEN" in *:*) ;; *) LISTEN="$LISTEN:80" ;; esac
say() { printf '\n== %s\n' "$*"; }

say "Packages"
HAD_NGINX=0; dpkg-query -W -f='${Status}' nginx-common 2>/dev/null | grep -q "install ok installed" && HAD_NGINX=1
NEED=""
[ $HAD_NGINX -eq 1 ] || NEED="$NEED nginx-light"
for p in rsync nmap openssl python3 openssh-client; do dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "install ok installed" || NEED="$NEED $p"; done
if [ -n "$NEED" ]; then
	echo "   installing:$NEED"
	DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $NEED >/tmp/scribus-push-setup-apt.log 2>&1 \
		|| { tail -15 /tmp/scribus-push-setup-apt.log; echo "apt-get failed (log: /tmp/scribus-push-setup-apt.log)" >&2; exit 1; }
else
	echo "   nothing to install"
fi

say "Update folder"
install -d -o "$PUSH_ACCOUNT" -g "$PUSH_ACCOUNT" -m 755 "$UPDATES_DIR"
echo "   $UPDATES_DIR (owner $PUSH_ACCOUNT)"

say "nginx: $LISTEN, only for $SUBNET"
cat > /etc/nginx/sites-available/scribus-updates <<NGINX
# Written by setup-push-server.sh. Scribus update files, read-only.
server {
	listen $LISTEN;
	server_name _;
	root $UPDATES_DIR;
	autoindex off;
	default_type application/octet-stream;
	add_header Cache-Control "no-cache" always;
	allow $SUBNET;
	deny all;
	location = / { return 404; }
	location ~ /\\. { return 404; }
	location / { try_files \$uri =404; }
	access_log /var/log/nginx/scribus-updates.access.log;
}
NGINX
ln -sfn /etc/nginx/sites-available/scribus-updates /etc/nginx/sites-enabled/scribus-updates
if [ $HAD_NGINX -eq 0 ] && [ -L /etc/nginx/sites-enabled/default ]; then
	rm -f /etc/nginx/sites-enabled/default
	echo "   nginx was installed by this script: its default site on port 80 is switched off"
elif [ $HAD_NGINX -eq 1 ]; then
	echo "   nginx was already installed: its other sites are left as they are"
fi
nginx -t 2>&1 | sed 's/^/   /'
systemctl enable nginx >/dev/null 2>&1
systemctl restart nginx
systemctl is-enabled nginx | sed 's/^/   enabled at boot: /'
systemctl is-active nginx | sed 's/^/   running: /'

say "Push tool"
install -m 755 "$HERE/push-update.sh" /usr/local/bin/scribus-push-update
install -d -m 755 /etc/scribus-push
install -m 644 "$HERE/push.conf" /etc/scribus-push/push.conf
if [ ! -f /etc/scribus-push/pcs ]; then
	cat > /etc/scribus-push/pcs <<'PCS'
# Office PCs that scribus-push-update --all installs on. One per line:
#   name address
# (scribus-push-update --scan shows what answers on the office subnet.)
PCS
fi
chown "$PUSH_ACCOUNT:$PUSH_ACCOUNT" /etc/scribus-push/pcs
touch /etc/scribus-push/known_hosts && chown "$PUSH_ACCOUNT:$PUSH_ACCOUNT" /etc/scribus-push/known_hosts && chmod 644 /etc/scribus-push/known_hosts
echo "   /usr/local/bin/scribus-push-update, /etc/scribus-push/{push.conf,pcs}"

say "Push SSH key"
if [ ! -f /etc/scribus-push/push_key ]; then
	ssh-keygen -q -t ed25519 -N "" -C "scribus-push@$(hostname)" -f /etc/scribus-push/push_key
	echo "   new key generated"
else
	echo "   existing key kept"
fi
chown "$PUSH_ACCOUNT:$PUSH_ACCOUNT" /etc/scribus-push/push_key /etc/scribus-push/push_key.pub
chmod 600 /etc/scribus-push/push_key; chmod 644 /etc/scribus-push/push_key.pub
ls -l /etc/scribus-push/push_key | awk '{print "   " $1, $3, $NF}'

say "Check"
ss -ltnH | awk '{print $4}' | grep -q ":${LISTEN##*:}\$" && echo "   listening on port ${LISTEN##*:}" || echo "   NOT listening on port ${LISTEN##*:}"
echo "ok" > "$UPDATES_DIR/.setup-check" 2>/dev/null || true
# python3, not curl: a minimal Debian server has no curl.
code=$(python3 - "$UPDATE_BASE_URL/latest.json" <<'PY' 2>/dev/null || true
import sys, urllib.request, urllib.error
try:
    print(urllib.request.urlopen(sys.argv[1], timeout=5).status)
except urllib.error.HTTPError as e:
    print(e.code)
except Exception:
    pass
PY
)
rm -f "$UPDATES_DIR/.setup-check"
echo "   $UPDATE_BASE_URL/latest.json -> HTTP ${code:-no answer} (404 is right until the first release is uploaded)"
echo
echo "Done. Next: put the office PCs into /etc/scribus-push/pcs, then  scribus-push-update --copy-keys"
