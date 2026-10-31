#!/bin/bash
# News Browser login never blocks, never makes the keyring prompt
# (suneer_news_panel.cpp loginDialog / ensureSecretsLoaded / saveSession,
# scupdatekeystore.cpp keyringState):
#   each case runs Scribus in its own D-Bus session with gnome-keyring in one
#   state, against a mock server (fixtures/news_login_mock.py, never a real
#   one), and SUNEER_LOGIN_SELFTEST logs in through the real dialog:
#     normal        unlocked default keyring        -> tokens in the keyring
#     nocollection  daemon, no default keyring (chn-dtp26) -> session only, no prompt
#     locked        default keyring locked          -> session only, no prompt
#     nodaemon      no Secret Service at all        -> session only
#     notstarted    keyring activatable, not running -> session only, not started by Scribus
#     refused       nothing listens on the port     -> clear error
#     hang          server never answers            -> error after 10 s, Cancel enabled
#     wrongpass     HTTP 401                         -> server's message
#     cancel        Cancel 2 s into a hanging request -> dialog closes at once
#   plus "before": libsecret's own store into the default collection (what the
#   old code did) with no default keyring - it waits for the hidden prompt.
# usage: tests/suneer/news_login.sh <work dir> [scribus]   (PREFS for an isolated profile)
set -u
W=${1:?work dir}; SCRIBUS=${2:-/usr/local/bin/scribus}; HERE="$(cd "$(dirname "$0")" && pwd)"
PR=(); [ -n "${PREFS:-}" ] && PR=(-pr "$PREFS")
DOC=${DOC:-}
mkdir -p "$W"; W="$(cd "$W" && pwd)"
PORT_OK=18731; PORT_HANG=18732; PORT_DEAD=18739
DISP=:93

python3 "$HERE/fixtures/news_login_mock.py" $PORT_OK normal 2>"$W/mock-normal.log" & MOCK1=$!
python3 "$HERE/fixtures/news_login_mock.py" $PORT_HANG hang 2>"$W/mock-hang.log" & MOCK2=$!
Xvfb $DISP -screen 0 1400x900x24 -nolisten tcp >/dev/null 2>&1 & XVFB=$!
sleep 1.5
cleanup() { kill $MOCK1 $MOCK2 $XVFB 2>/dev/null; }
trap cleanup EXIT

# A session bus that can start nothing (no service files): "no keyring daemon".
cat > "$W/bare-session.conf" <<'CONF'
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
CONF

# The probe a shell can make: who owns the Secret Service, the default alias, Locked.
cat > "$W/probe.sh" <<'EOF'
o=$(gdbus call --session -d org.freedesktop.DBus -o /org/freedesktop/DBus -m org.freedesktop.DBus.NameHasOwner org.freedesktop.secrets 2>&1)
echo "owner $o"
case "$o" in *true*)
  a=$(timeout 5 gdbus call --session -d org.freedesktop.secrets -o /org/freedesktop/secrets -m org.freedesktop.Secret.Service.ReadAlias default 2>&1)
  echo "alias $a"
  p=$(echo "$a" | sed -nE "s/.*'(\/org[^']*)'.*/\1/p")
  [ -n "$p" ] && echo "locked $(timeout 5 gdbus call --session -d org.freedesktop.secrets -o "$p" -m org.freedesktop.DBus.Properties.Get org.freedesktop.Secret.Collection Locked 2>&1)";;
esac
EOF

# setup_case <dir> <api host>: an isolated HOME with the server address
setup_case() {
	rm -rf "$1"; mkdir -p "$1/home/.config/Faircode" "$1/home/.local/share/keyrings" "$1/run"; chmod 700 "$1/run" "$1/home/.local/share/keyrings"
	printf '[General]\napiHost=%s\nloginEmail=test@example.com\n' "$2" > "$1/home/.config/Faircode/ScribusNews.conf"
}

# inner script run inside the case's own D-Bus session
cat > "$W/inner.sh" <<'EOF'
C=$1; KR=$2; shift 2
KPID=
case "$KR" in
  normal) echo -n testpass | gnome-keyring-daemon --foreground --unlock --components=secrets >"$C/keyring.log" 2>&1 & KPID=$! ;;
  nocollection|locked) gnome-keyring-daemon --foreground --components=secrets >"$C/keyring.log" 2>&1 & KPID=$! ;;
  noanswer) python3 "$HERE/fixtures/news_login_silent_secrets.py" >"$C/keyring.log" 2>&1 & KPID=$! ;;
esac
sleep 1.5
bash "$W/probe.sh" > "$C/probe-before.txt" 2>&1
t0=$(date +%s%N)
"$@"
rc=$?
echo "command exit $rc after $(( ($(date +%s%N) - t0) / 1000000 )) ms" > "$C/command.txt"
bash "$W/probe.sh" > "$C/probe-after.txt" 2>&1
[ -n "$KPID" ] && kill $KPID 2>/dev/null
exit $rc
EOF

# run_case <name> <keyring mode> <api host> <password> [cancel ms]
run_case() {
	[ -n "${ONLY:-}" ] && [[ " $ONLY " != *" $1 "* ]] && return
	local name=$1 kr=$2 api=$3 pass=$4 cancel=${5:-} early=${6:-} C="$W/$1" cfg=() ev=()
	setup_case "$C" "$api"
	[ "$kr" = locked ] && cp -a "$W/keyring-template/." "$C/home/.local/share/keyrings/"
	[ "$kr" = nodaemon ] && cfg=(--config-file="$W/bare-session.conf")
	[ -n "$early" ] && ev=(SUNEER_LOGIN_SELFTEST_EARLY=1)
	local t0=$(date +%s%N)
	HOME="$C/home" XDG_CONFIG_HOME="$C/home/.config" XDG_DATA_HOME="$C/home/.local/share" XDG_RUNTIME_DIR="$C/run" \
	DISPLAY=$DISP W="$W" HERE="$HERE" QT_IM_MODULE= \
	SUNEER_LOGIN_SELFTEST="$C/report.txt" SUNEER_LOGIN_SELFTEST_EMAIL=test@example.com SUNEER_LOGIN_SELFTEST_PASS="$pass" \
	SUNEER_LOGIN_SELFTEST_CANCEL_MS="$cancel" SUNEER_LOGIN_SELFTEST_SHOT="$C/shot" \
	timeout 120 env "${ev[@]}" dbus-run-session "${cfg[@]}" -- bash "$W/inner.sh" "$C" "$kr" \
		"$SCRIBUS" "${PR[@]}" $DOC > "$C/scribus.log" 2>&1
	echo "$(( ($(date +%s%N) - t0) / 1000000 ))" > "$C/wall-ms.txt"
}

# The locked case needs a keyring file: made once by an unlocking session.
setup_case "$W/template" "http://127.0.0.1:$PORT_OK"
HOME="$W/template/home" XDG_DATA_HOME="$W/template/home/.local/share" XDG_RUNTIME_DIR="$W/template/run" W="$W" \
	dbus-run-session -- bash "$W/inner.sh" "$W/template" normal true
rm -rf "$W/keyring-template"; cp -a "$W/template/home/.local/share/keyrings" "$W/keyring-template"

# "before": the old code path - a libsecret store into the default collection.
cat > "$W/store.py" <<'EOF'
import gi, time
gi.require_version('Secret', '1')
from gi.repository import Secret
schema = Secret.Schema.new("org.scribus.test", Secret.SchemaFlags.NONE, {"k": Secret.SchemaAttributeType.STRING})
t = time.time()
ok = Secret.password_store_sync(schema, {"k": "v"}, Secret.COLLECTION_DEFAULT, "test", "secret", None)
print("stored %s after %.1f s" % (ok, time.time() - t))
EOF
for kr in nocollection normal; do
	C="$W/before-$kr"; setup_case "$C" "http://127.0.0.1:$PORT_OK"
	HOME="$C/home" XDG_DATA_HOME="$C/home/.local/share" XDG_RUNTIME_DIR="$C/run" DISPLAY=$DISP W="$W" \
	timeout 60 dbus-run-session -- bash "$W/inner.sh" "$C" $kr timeout 15 python3 "$W/store.py" > "$C/store.txt" 2>&1
done

run_case normal       normal       "http://127.0.0.1:$PORT_OK"   S3cret-right
run_case nocollection nocollection "http://127.0.0.1:$PORT_OK"   S3cret-right
run_case locked       locked       "http://127.0.0.1:$PORT_OK"   S3cret-right
run_case nodaemon     nodaemon     "http://127.0.0.1:$PORT_OK"   S3cret-right
run_case notstarted   notstarted   "http://127.0.0.1:$PORT_OK"   S3cret-right
run_case noanswer     noanswer     "http://127.0.0.1:$PORT_OK"   S3cret-right
run_case early        noanswer     "http://127.0.0.1:$PORT_OK"   S3cret-right "" early
run_case refused      normal       "http://127.0.0.1:$PORT_DEAD" S3cret-right
run_case hang         normal       "http://127.0.0.1:$PORT_HANG" S3cret-right
run_case wrongpass    normal       "http://127.0.0.1:$PORT_OK"   S3cret-wrong
run_case cancel       normal       "http://127.0.0.1:$PORT_HANG" S3cret-right 2000

python3 - "$W" <<'PY'
import sys, os, re
W = sys.argv[1]; fails = 0
def check(what, ok):
    global fails
    print(("PASS " if ok else "FAIL ") + what); fails += 0 if ok else 1
def rep(name):
    p = os.path.join(W, name, "report.txt")
    d = {}
    if os.path.isfile(p):
        for l in open(p, encoding="utf-8"):
            k, _, v = l.rstrip("\n").partition(": ")
            d[k] = v
    return d
def read(name, f):
    p = os.path.join(W, name, f)
    return open(p, encoding="utf-8", errors="replace").read() if os.path.isfile(p) else ""
def ms(name):
    return int(read(name, "wall-ms.txt").strip() or 0)

b0, b1 = read("before-nocollection", "command.txt").strip(), read("before-normal", "command.txt").strip()
check("before (old code path): libsecret store with no default keyring waits for the hidden prompt until killed [%s; prompter started: %s]"
      % (b0, "yes" if "gcr-prompter" in read("before-nocollection", "store.txt") else "no"), b0.startswith("command exit 124"))
check("before: the same store with a normal keyring returns at once [%s; %s]" % (b1, read("before-normal", "store.txt").strip().splitlines()[-1]),
      b1.startswith("command exit 0") and "stored True" in read("before-normal", "store.txt"))

for name in ("normal", "nocollection", "locked", "nodaemon", "notstarted", "noanswer"):
    r = rep(name)
    print("  %-12s %s | probe %s" % (name, " ".join("%s=%s" % (k, r.get(k, "?")) for k in ("keyring", "sessionOnly", "loggedIn", "requestMs", "dialogMs")),
                                    read(name, "probe-before.txt").replace("\n", " ")))
r = rep("normal")
check("normal: logged in, keyring used, refresh token in the keyring, no notice",
      r.get("loggedIn") == "yes" and r.get("sessionOnly") == "no" and r.get("keyringHasRefreshToken") == "yes" and not r.get("notice"))
# notstarted: Qt's desktop portal (xdg-desktop-portal) may start the Secret
# Service itself at start-up; Scribus's own code never does. Either way: no prompt.
for name, state in (("nocollection", "no default (login) keyring"), ("locked", "default keyring is locked"),
                    ("nodaemon", "no keyring service running"), ("notstarted", "no keyring service running|no default (login) keyring")):
    r = rep(name)
    check("%s: logged in without waiting (dialog %s ms, wall %d ms), keyring '%s', session only, notice shown, nothing in the prefs file"
          % (name, r.get("dialogMs"), ms(name), r.get("keyring")),
          r.get("loggedIn") == "yes" and r.get("keyring") in state.split("|") and r.get("sessionOnly") == "yes"
          and r.get("notice", "").startswith("Login saved for this session only") and r.get("prefsFileHasRefreshToken") == "no"
          and int(r.get("dialogMs") or 99999) < 5000)
check("nocollection: still no default keyring after the login (Scribus created none)", "objectpath '/'" in read("nocollection", "probe-after.txt"))
check("locked: the keyring is still locked after the login (Scribus unlocked nothing)", "<true>" in read("locked", "probe-after.txt"))
nsl = read("notstarted", "scribus.log")
who = re.findall(r"Activating service name='org.freedesktop.secrets' requested by \S+ \(uid=\d+ pid=\d+ comm=\"([^\"]*)\"", nsl)
check("notstarted: Scribus itself never starts the Secret Service (started by: %s), and no default keyring was created"
      % (", ".join(who) or "nobody"),
      "(false,)" in read("notstarted", "probe-before.txt") and not any(w.startswith("scribus") for w in who)
      and ("(false,)" in read("notstarted", "probe-after.txt") or "objectpath '/'" in read("notstarted", "probe-after.txt")))
r = rep("noanswer")
lg = read("noanswer", "home/scribus-crashlogs/login.log")
m = re.search(r"keyring check: (.*?) \((\d+) ms\)", lg)
check("noanswer: keyring check gives up after ~3 s [%s], login still works, session only" % (m.group(0) if m else "no log line"),
      bool(m) and m.group(1) == "keyring service did not answer" and 2800 <= int(m.group(2)) <= 3600
      and r.get("loggedIn") == "yes" and r.get("sessionOnly") == "yes")
r = rep("early")
lg = read("early", "home/scribus-crashlogs/login.log")
check("early: Login during the 3 s keyring check works and the finishing check keeps that session (panel unlocked: %s)" % r.get("panelUnlocked"),
      r.get("loggedIn") == "yes" and r.get("panelUnlocked") == "yes" and r.get("startupDone") == "yes"
      and "keyring check finished after a login" in lg and "startup: no saved session" not in lg)
for name in ("normal", "nocollection", "locked", "nodaemon", "notstarted", "noanswer"):
    check("%s: panel unlocked after the login" % name, rep(name).get("panelUnlocked") == "yes")
r = rep("refused")
check("refused: clear error [%s] in %s ms" % (r.get("error"), r.get("requestMs")), r.get("loggedIn") == "no" and "refused the connection" in r.get("error", "") and int(r.get("requestMs") or 99999) < 3000)
r = rep("hang")
check("hang: error after ~10 s [%s] (request %s ms)" % (r.get("error"), r.get("requestMs")),
      r.get("loggedIn") == "no" and "No answer from the server" in r.get("error", "") and 9950 <= int(r.get("requestMs") or 0) <= 11500)
check("hang: UI kept running (%s ticks of 100 ms), Login disabled and Cancel enabled while waiting" % r.get("ticksWhileBusy"),
      int(r.get("ticksWhileBusy") or 0) >= 80 and r.get("loginEnabledWhileBusy") == "no" and r.get("cancelDisabledWhileBusy") == "no")
r = rep("wrongpass")
check("wrong password: server message shown [%s]" % r.get("error"), r.get("loggedIn") == "no" and "Invalid email or password (HTTP 401)" in r.get("error", ""))
r = rep("cancel")
check("cancel: Cancel 2 s into a hanging request closes the dialog at once (close %s ms, dialog %s ms)" % (r.get("cancelCloseMs"), r.get("dialogMs")),
      r.get("loggedIn") == "no" and r.get("cancelCloseMs") not in (None, "-1") and int(r.get("dialogMs") or 99999) < 4000)
log = read("nocollection", "home/scribus-crashlogs/login.log")
check("login.log written with timings (%d lines)" % len(log.splitlines()), "keyring check:" in log and "login reply: HTTP 200 after" in log)
alllogs = "".join(read(n, "home/scribus-crashlogs/login.log") for n in ("normal", "nocollection", "locked", "nodaemon", "notstarted", "noanswer", "early", "refused", "hang", "wrongpass", "cancel"))
check("no password or token in any login.log", not any(s in alllogs for s in ("S3cret", "AT-mock", "RT-mock", "testpass")))
print("ALL PASS" if fails == 0 else "FAILED: %d" % fails)
sys.exit(1 if fails else 0)
PY
