# Scribus update server (Debian host, 2026-10-08)

Office PCs get Scribus updates from the newsroom's Debian server:

    http://<server>:8095/scribus-updates/latest.json      (the address lives in ~/scribus-keys/release.conf, not here)

- **Host nginx on port 8095** serves `/srv/scribus-updates` read-only
  (`nginx-scribus-updates.conf` → `/etc/nginx/sites-available/scribus-updates`,
  enabled; default site disabled). `latest.json` is `Cache-Control: no-store`;
  directory listing off; everything else 404.
- **Ports 80/443 and the docker containers on that server belong to the
  newsroom workflow (production).** Never change `docker-compose-prd.yml`, its
  nginx container, or the docker/nft rules.
- **Upload**: rsync as the key-only account `scribusupd`, restricted with
  rrsync to `/srv/scribus-updates` (no shell). `UPLOAD_TARGET=scribusupd@<server>:/`.
- **Network**: office PCs (the office subnets) reach the server
  only on 80, 443 and 8095. They have no curl: the updater is Qt code, the
  install helper is Python.

`tools/publish-update.sh` builds, signs (laptop only), uploads in a safe order
(.deb, .sha256, .sig, then latest.json), keeps the last three releases, and
reads everything back over HTTP as a PC would. `--list` shows the server,
`--rollback <version>` re-signs latest.json for an older .deb still there.
Site settings: `~/scribus-keys/release.conf` (not in the repo).

Checks on the server:  `systemctl status nginx`, `ss -ltnp | grep 8095`,
`ls -l /srv/scribus-updates`, `tail /var/log/nginx/scribus-updates.access.log`.

The push machinery (`push-update.sh`, `setup-push-server.sh`,
`deploy-push-server.sh`) is the older design where the server also installs on
the PCs over SSH; it is not used with this server.
