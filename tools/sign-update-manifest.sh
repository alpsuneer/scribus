#!/bin/sh
# Sign a Scribus update and print its latest.json.
#
#   tools/sign-update-manifest.sh keygen <private-key.pem>
#       Makes a new Ed25519 release key and prints the public key to build
#       Scribus with:  cmake -DSCRIBUS_UPDATE_PUBKEY=<printed value> ...
#       Keep the private key OFF the update server.
#
#   tools/sign-update-manifest.sh sign <private-key.pem> <version> <url> <file.deb> [changelog]
#       Prints latest.json for <file.deb>. <url> must be exactly the URL the
#       server serves the .deb at, on the same scheme/host/port as the server.
#
# The signed message must match ScUpdateClient::manifestSigningMessage():
#   "scribus-update-v1\n<version>\n<url>\n<sha256>\n"
set -eu

case "${1:-}" in
keygen)
	[ $# -eq 2 ] || { echo "usage: $0 keygen <private-key.pem>" >&2; exit 2; }
	[ ! -e "$2" ] || { echo "$2 already exists" >&2; exit 1; }
	(umask 077; openssl genpkey -algorithm ed25519 -out "$2")
	openssl pkey -in "$2" -pubout -outform DER | tail -c 32 | base64 -w0
	echo
	;;
sign)
	[ $# -ge 5 ] || { echo "usage: $0 sign <private-key.pem> <version> <url> <file.deb> [changelog]" >&2; exit 2; }
	key=$2 version=$3 url=$4 deb=$5 changelog=${6:-}
	sha=$(sha256sum "$deb" | cut -d' ' -f1)
	msg=$(mktemp)
	trap 'rm -f "$msg"' EXIT
	printf 'scribus-update-v1\n%s\n%s\n%s\n' "$version" "$url" "$sha" > "$msg"
	sig=$(openssl pkeyutl -sign -inkey "$key" -rawin -in "$msg" | base64 -w0)
	python3 -c 'import json,sys; print(json.dumps({"version":sys.argv[1],"url":sys.argv[2],"sha256":sys.argv[3],"signature":sys.argv[4],"changelog":sys.argv[5]}, indent=2))' \
		"$version" "$url" "$sha" "$sig" "$changelog"
	;;
*)
	echo "usage: $0 keygen <private-key.pem> | sign <private-key.pem> <version> <url> <file.deb> [changelog]" >&2
	exit 2
	;;
esac
