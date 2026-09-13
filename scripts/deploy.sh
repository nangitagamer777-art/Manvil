#!/usr/bin/env bash
set -euo pipefail

SRC="${1:-/root/Manvil/build}"
DST="/sdcard/manvil_out"

mkdir -p "${DST}"

if [ ! -d "${SRC}" ]; then
    echo "Build directory not found: ${SRC}"
    exit 1
fi

cp -rv "${SRC}"/*.so "${DST}/" 2>/dev/null || true
cp -rv "${SRC}"/tools/* "${DST}/" 2>/dev/null || true

echo "Deployed to ${DST}"
ls -la "${DST}"
