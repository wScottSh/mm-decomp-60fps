#!/usr/bin/env bash
# Run a ROM in ares (N64) without a physical display.
#
# usage: tools/ares/ares-headless.sh [--seconds N] [--screenshot out.png] [ares args...] <rom>
#
# - Uses $DISPLAY if set; otherwise starts a private Xvfb server.
# - Audio and input are disabled, so emulation runs uncapped (faster than real time).
# - With --seconds, ares is killed after N seconds (after taking --screenshot, if given).
#   Without it, ares runs until interrupted.
# - Extra ares args are passed through, e.g. --setting General/ForceInterpreter=true
#   or the GDB stub: --setting DebugServer/Enabled=true --setting DebugServer/Port=9123
#   --setting DebugServer/UseIPv4=true
#
# Note: the ROM's directory must be writable, or ares blocks on a modal dialog.

set -euo pipefail

ARES="${ARES:-ares}"
SECONDS_TO_RUN=""
SCREENSHOT=""
ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --seconds) SECONDS_TO_RUN="$2"; shift 2 ;;
        --screenshot) SCREENSHOT="$2"; shift 2 ;;
        *) ARGS+=("$1"); shift ;;
    esac
done

if [[ ${#ARGS[@]} -eq 0 ]]; then
    sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi

XVFB_PID=""
cleanup() {
    [[ -n "${ARES_PID:-}" ]] && kill "$ARES_PID" 2>/dev/null || true
    [[ -n "$XVFB_PID" ]] && kill "$XVFB_PID" 2>/dev/null || true
}
trap cleanup EXIT

if [[ -z "${DISPLAY:-}" ]]; then
    # Let Xvfb pick a free display number itself, so parallel instances don't collide
    DISPLAY_FILE=$(mktemp)
    Xvfb -displayfd 3 -screen 0 1280x1024x24 -nolisten tcp 3>"$DISPLAY_FILE" >/dev/null 2>&1 &
    XVFB_PID=$!
    for _ in $(seq 100); do
        [[ -s "$DISPLAY_FILE" ]] && break
        sleep 0.05
    done
    DISPLAY_NUM=$(tr -d '[:space:]' < "$DISPLAY_FILE")
    rm -f "$DISPLAY_FILE"
    if [[ -z "$DISPLAY_NUM" ]]; then
        echo "error: Xvfb failed to start" >&2
        exit 1
    fi
    export DISPLAY=":$DISPLAY_NUM"
fi

"$ARES" --system "Nintendo 64" --no-file-prompt \
    --setting Audio/Driver=None \
    --setting Input/Driver=None \
    --setting Video/Driver="OpenGL 3.2" \
    --setting Boot/Fast=true \
    "${ARGS[@]}" &
ARES_PID=$!

if [[ -n "$SECONDS_TO_RUN" ]]; then
    sleep "$SECONDS_TO_RUN"
    if [[ -n "$SCREENSHOT" ]]; then
        import -window root "$SCREENSHOT"
    fi
else
    wait "$ARES_PID"
fi
