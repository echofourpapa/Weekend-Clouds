#!/usr/bin/env bash
# Compile-check all cloud shaders with DXC (Linux or Windows dxc on PATH, or tools/dxc/bin/dxc).
# Exit 0 with a LOUD warning if dxc is unavailable — the Windows/MSBuild compile remains
# authoritative (spec: docs/PLAN.md section 8).
set -u
cd "$(dirname "$0")/.."

DXC=""
if command -v dxc >/dev/null 2>&1; then DXC=dxc;
elif [ -x tools/dxc/bin/dxc ]; then DXC=tools/dxc/bin/dxc; fi

SHADER_DIR=src/renderer/shaders
mapfile -t FILES < <(ls "$SHADER_DIR"/Cloud*-c.hlsl "$SHADER_DIR"/Sky*-c.hlsl 2>/dev/null)

if [ ${#FILES[@]} -eq 0 ]; then
    echo "check_shaders: no cloud shaders exist yet — nothing to check."
    exit 0
fi

if [ -z "$DXC" ]; then
    echo "############################################################"
    echo "# WARNING: dxc not found — cloud shaders NOT compile-checked"
    echo "# Install DXC (or drop it in tools/dxc/bin/) before trusting"
    echo "# this commit. Windows build remains authoritative."
    echo "############################################################"
    exit 0
fi

FAIL=0
check() { # file [extra defines...]
    local f="$1"; shift
    local out
    if ! out=$("$DXC" -T cs_6_8 -E main -I "$SHADER_DIR" "$@" "$f" -Fo /dev/null 2>&1); then
        echo "FAIL: $f $*"; echo "$out" | head -30; FAIL=1
    else
        echo "  ok: $(basename "$f") $*"
    fi
}

for f in "${FILES[@]}"; do
    base=$(basename "$f")
    case "$base" in
        CloudTrace-c.hlsl)
            check "$f"
            check "$f" -D TRACE_BRUTE=1
            check "$f" -D TRACE_RQ=1
            ;;
        *)
            check "$f"
            ;;
    esac
done

if [ $FAIL -ne 0 ]; then echo "check_shaders: FAILURES above."; exit 1; fi
echo "check_shaders: all cloud shaders compile."
