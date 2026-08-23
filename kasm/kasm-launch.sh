#!/usr/bin/env bash
# ============================================================================
#  Auto-launch Scribus inside the Kasm XFCE session.
#
#  Installed as $STARTUPDIR/custom_startup.sh — the hook Kasm runs once the
#  container's VNC/desktop stack is coming up. A second, redundant trigger is
#  ~/.config/autostart/scribus.desktop, which fires when the XFCE session
#  itself starts. The pgrep guard below makes running twice harmless.
#
#  NOTE: /usr/local/bin/scribus is a thin wrapper that sets the fcitx env and
#  then `exec`s scribus.bin, so the *running* process is scribus.bin — that is
#  what must be pgrep'd. Guarding on "scribus" would never match and the retry
#  loop would spawn duplicate windows.
# ============================================================================
set -u

SCRIBUS_BIN=/usr/local/bin/scribus    # wrapper (applies fcitx env, keeps prefs dir)
PROC_NAME=scribus.bin                 # actual process after exec
SAM_SYSTEM_DIR=${SAM_SYSTEM_DIR:-/opt/scribus/sam}

# Already running (other trigger won the race)? Nothing to do.
if pgrep -x "${PROC_NAME}" >/dev/null 2>&1; then
    exit 0
fi

# ---------------------------------------------------------------------------
# Wait for the desktop. desktop_ready is Kasm's own gate (it blocks on
# `pidof $START_DE`, defaulting to xfce4-session) and tracks whatever DE the
# base image uses, so it is correct across base-image changes in a way that
# hand-rolling `until pgrep -x xfce4-session` is not.
# ---------------------------------------------------------------------------
if [ -x /usr/bin/desktop_ready ]; then
    /usr/bin/desktop_ready
else
    until pgrep -x "${START_DE:-xfce4-session}" >/dev/null 2>&1; do sleep 0.5; done
fi

# ---------------------------------------------------------------------------
# SAM models: SamSegmenter::modelDir() is hardcoded to
# $HOME/.config/scribus/sam/. The models are kept as a single system-wide copy
# so they are not duplicated into every user profile, so link them in here.
# Skipped if the user already has a real directory there (or an admin mounted
# one), and harmless when no models are installed — Smart Select just reports
# the absence in the status bar.
# ---------------------------------------------------------------------------
if [ -d "${SAM_SYSTEM_DIR}" ] && [ ! -e "${HOME}/.config/scribus/sam" ]; then
    mkdir -p "${HOME}/.config/scribus"
    ln -sfn "${SAM_SYSTEM_DIR}" "${HOME}/.config/scribus/sam"
fi

# ---------------------------------------------------------------------------
# Malayalam input method. Scribus reads QT_IM_MODULE at startup, so fcitx5 must
# be up first or the frame will not accept preedit until a restart.
# ---------------------------------------------------------------------------
export QT_IM_MODULE=fcitx
export GTK_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx

if command -v fcitx5 >/dev/null 2>&1 && ! pgrep -x fcitx5 >/dev/null 2>&1; then
    fcitx5 -d >/dev/null 2>&1 || true
    sleep 1
fi

# ---------------------------------------------------------------------------
# Launch, with a small retry for the case where the desktop reports ready a
# moment before it can actually host a window.
# ---------------------------------------------------------------------------
for _ in 1 2 3; do
    "${SCRIBUS_BIN}" >/dev/null 2>&1 &
    sleep 5
    if pgrep -x "${PROC_NAME}" >/dev/null 2>&1; then
        break
    fi
done
