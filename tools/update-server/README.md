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
