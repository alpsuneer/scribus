#!/bin/bash
# prefs_roundtrip.sh <profile dir> [runs] [display]
#
# Load + save the preferences N times and check that nothing drifts.
# Each run: start Scribus with a throw-away HOME holding a copy of <profile dir>
# (a ~/.config/scribus folder), cancel the startup dialog, File > Preferences > OK,
# close the window, wait for the process to end. The first WARMUP runs (default 2)
# may change the files once: the repair of a damaged profile, DisplayScale rounded
# to whole dpi, and the one-time install of the shipped keyboard set at the second
# start. Every later run must leave scribus172.rc and prefs172.xml byte for byte
# identical to the last warm-up run.
#
# Needs an X server on <display> (default :97, e.g. Xvfb :97 -screen 0 1600x1000x24),
# xdotool, python3-xlib. Never touches the real ~/.config.
set -u
SRC=${1:?profile dir}; RUNS=${2:-20}; DISP=${3:-:97}; WARMUP=${WARMUP:-2}
BIN=${SCRIBUS:-/usr/local/bin/scribus}
H=$(mktemp -d /tmp/prefs-roundtrip.XXXXXX); mkdir -p $H/.config; cp -a "$SRC" $H/.config/scribus
P=$H/.config/scribus; OUT=$H/runs; mkdir -p $OUT
X() { env DISPLAY=$DISP timeout 5 xdotool "$@"; }
wmdelete() { DISPLAY=$DISP python3 - "$1" <<'PY'
import sys
from Xlib import display, X
from Xlib.protocol import event
d = display.Display(); w = d.create_resource_object('window', int(sys.argv[1]))
p = d.intern_atom('WM_PROTOCOLS'); dl = d.intern_atom('WM_DELETE_WINDOW')
w.send_event(event.ClientMessage(window=w, client_type=p, data=(32, [dl, X.CurrentTime, 0, 0, 0])), event_mask=0); d.flush()
PY
}
# PIDs of Scribus processes started with this HOME (never another user's Scribus)
ours() { for d in /proc/[0-9]*; do case "$(readlink $d/exe 2>/dev/null)" in */scribus.bin)
	tr '\0' '\n' < $d/environ 2>/dev/null | grep -qx "HOME=$H" && echo ${d#/proc/} ;; esac; done; }
# waitwin <name regex> <seconds>: the newest window with that name
waitwin() { local w; for t in $(seq 1 $2); do w=$(X search --name "$1" 2>/dev/null | tail -1); [ -n "$w" ] && { echo $w; return 0; }; sleep 1; done; return 1; }
fail=0
for i in $(seq 1 $RUNS); do
	(cd /tmp && env HOME=$H DISPLAY=$DISP QT_IM_MODULE= setsid $BIN > $OUT/log-$i.txt 2>&1 &)
	W=$(waitwin "Scribus 1[.]7" 90) || { echo "run $i: no main window"; fail=1; }
	sleep 4
	X windowsize $W 1600 1000; X windowmove $W 0 0; sleep 1
	D=$(waitwin "^New Document$" 6) && { X windowfocus --sync $D; X key Escape; sleep 1; }   # startup dialog, if shown
	X windowfocus --sync $W; X mousemove 18 10 click 1; sleep 1; X mousemove 70 415 click 1   # File > Preferences...
	PW=$(waitwin "^Preferences$" 20) || { echo "run $i: Preferences did not open"; fail=1; }
	sleep 2
	eval $(X getwindowgeometry --shell $PW)
	X mousemove $((X + WIDTH - 54)) $((Y + HEIGHT - 24)) click 1   # OK, bottom right of the dialog
	for t in $(seq 1 10); do sleep 1; X search --name "^Preferences$" >/dev/null 2>&1 || break; done
	sleep 2
	wmdelete "$W"
	for t in $(seq 1 30); do sleep 1; [ -z "$(ours)" ] && break; done
	for pid in $(ours); do echo "run $i: process did not exit, killed"; kill -9 $pid; done
	cp $P/scribus172.rc $OUT/rc-$i; cp $P/prefs172.xml $OUT/xml-$i
	B=$WARMUP
	if [ $i -gt $B ]; then
		if cmp -s $OUT/rc-$B $OUT/rc-$i && cmp -s $OUT/xml-$B $OUT/xml-$i; then echo "run $i: identical to run $B"
		else echo "run $i: CHANGED"; diff <(sed 's/></>\n</g' $OUT/rc-$B) <(sed 's/></>\n</g' $OUT/rc-$i) | head -8; diff $OUT/xml-$B $OUT/xml-$i | head -8; fail=1; fi
	else
		echo "run $i: warm-up (Set entries: $(grep -o '<Set ' $OUT/rc-$i | wc -l), historylength: $(grep -o 'historylength" value="[0-9]*' $OUT/xml-$i | grep -o '[0-9]*$'))"
		[ $i -gt 1 ] && echo "  changed in warm-up run $i: $(diff <(sed 's/></>\n</g' $OUT/rc-$((i-1))) <(sed 's/></>\n</g' $OUT/rc-$i) | grep -c '^>') rc lines, $(diff $OUT/xml-$((i-1)) $OUT/xml-$i | grep -c '^>') xml lines"
	fi
done
echo "files: $OUT"
[ $fail = 0 ] && echo "PASS: $RUNS runs, no change after warm-up run $WARMUP" || echo "FAIL"
exit $fail
