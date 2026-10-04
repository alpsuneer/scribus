# LAN update server (laptop)

nginx serves `/srv/scribus-updates` read-only on port 8081, nothing else:

    sudo apt install nginx-light
    sudo mkdir -p /srv/scribus-updates && sudo chown $USER /srv/scribus-updates
    sudo cp tools/update-server/nginx-scribus-updates.conf /etc/nginx/sites-available/scribus-updates
    sudo ln -sfn /etc/nginx/sites-available/scribus-updates /etc/nginx/sites-enabled/scribus-updates
    sudo rm -f /etc/nginx/sites-enabled/default      # do not serve /var/www on port 80
    sudo nginx -t && sudo systemctl reload nginx && sudo systemctl enable nginx

Office PCs reach it as `http://<hostname>.local:8081` (avahi/mDNS) or by a
fixed IP (DHCP reservation on the router). Build Scribus with
`-DSCRIBUS_UPDATE_DEFAULT_URL=http://<hostname>.local:8081` so the .deb
installs that URL into /etc/scribus/update.conf.

The release signing key (~/scribus-keys/release-key.pem) must never be under
/srv/scribus-updates. `tools/release.sh` with `UPLOAD_METHOD=copy` copies the
.deb and latest.json in and reads them back over HTTP.

# Dedicated update server that also pushes

When a separate machine on the office LAN serves the updates and installs
them on the PCs (`tools/push-update.sh`, there `scribus-push-update`):

    tools/update-server/deploy-push-server.sh                 # on the release laptop; copies three files, no key
    ssh -t <login>@<server> 'sudo bash ~/scribus-push-setup/setup-push-server.sh'

Site settings come from `~/scribus-keys/push-update.conf` on the laptop
(`SERVER`, `SUBNET`, `SKIP_HOSTS`, `UPDATE_BASE_URL`, `PC_SSH_USER`). Then, on
the server: list the PCs in `/etc/scribus-push/pcs`, run
`scribus-push-update --copy-keys` once, and `scribus-push-update --first <pc>`
once per PC. After that `scribus-push-update --all` needs no password.

`tools/release.sh` uploads with `UPLOAD_METHOD=rsync`,
`UPLOAD_TARGET=<login>@<server>:/srv/scribus-updates/`, and
`UPDATE_BASE_URL=http://<server address>:8081`; build with
`-DSCRIBUS_UPDATE_DEFAULT_URL=<that URL> -DSCRIBUS_PUSH_USER=<account on the PCs>`.
The release signing key stays on the laptop.

