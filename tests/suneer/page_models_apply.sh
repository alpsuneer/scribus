#!/bin/bash
# Page Models test: the skeleton of a page model on the opened page, N fixture stories dropped into slots 1..N,
# the result (frames inside their slots, overflow / short marks, page group attribute, Malayalam
# codepoints) is written, the document is saved, Remove Empty Slots runs and undo must take everything back.
#
#   tests/suneer/page_models_apply.sh --models <pages dir> --model <group>/<id> [--tick N = slots to fill] page.sla [fixture.json]
#
# <pages dir> = a layout-models/pages folder (groups.json + <group>/<id>.json), e.g. ~/.config/scribus/layout-models/pages.
set -u
MODELS=""; MODEL=""; TICK=0
while [ $# -gt 0 ]; do case "$1" in
	--models) MODELS=$2; shift 2;; --model) MODEL=$2; shift 2;; --tick) TICK=$2; shift 2;; *) break;; esac; done
[ $# -ge 1 ] && [ -n "$MODELS" ] && [ -n "$MODEL" ] || { sed -n 2,8p "$0"; exit 2; }
SCRIBUS=${SCRIBUS:-/usr/local/bin/scribus}
sla=$1; fixture=${2:-}
work=$(mktemp -d /tmp/page-models-apply.XXXXXX); mkdir -p "$work/home" "$work/prefs/layout-models"
cp -r "$MODELS" "$work/prefs/layout-models/pages"
name=$(basename "$sla" .sla); cp "$sla" "$work/$name.sla"
[ -d "${sla%.sla}_photos" ] && cp -r "${sla%.sla}_photos" "$work/"
env HOME=$work/home QT_IM_MODULE= ${fixture:+SCRIBUS_NEWS_FIXTURE=$fixture} SUNEER_PAGEMODEL_SELFTEST=$work/result.txt \
	SUNEER_PAGEMODEL=$MODEL SUNEER_PAGEMODEL_TICK=$TICK SUNEER_PAGEMODEL_SAVEAS=$work/out.sla SUNEER_PAGEMODEL_UNDO=1 \
	timeout 900 xvfb-run -a -s "-screen 0 1600x1000x24" "$SCRIBUS" -ns -pr "$work/prefs" "$work/$name.sla" > "$work/scribus.log" 2>&1
rc=$?
[ -f "$work/result.txt" ] && sed 's/^/   /' "$work/result.txt"
echo "saved: $work/out.sla  log: $work/scribus.log"
grep -q "^RESULT PASS" "$work/result.txt" 2>/dev/null && [ $rc -eq 0 ] && { echo PASSED; exit 0; }
echo "FAILED (exit $rc)"; exit 1
