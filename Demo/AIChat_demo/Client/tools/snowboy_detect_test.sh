#!/usr/bin/env bash
# 编译并运行唤醒词离线验证
#   ./snowboy_detect_test.sh [model.pmdl] [wav ...]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SB="$ROOT/third_party/snowboy"
OUT="${TMPDIR:-/tmp}/snowboy_detect_test.$$"

MODEL="${1:-$ROOT/../../DeskBot_demo/bin/third_party/snowboy/resources/models/xiaonong.pmdl}"
shift || true
WAVS=("$@")
if [ ${#WAVS[@]} -eq 0 ]; then
    for i in 1 2 3; do [ -f "$HOME/snowboy/record$i.wav" ] && WAVS+=("$HOME/snowboy/record$i.wav"); done
fi
[ -f "$MODEL" ] || { echo "找不到模型: $MODEL"; exit 1; }
[ ${#WAVS[@]} -gt 0 ] || { echo "没有 wav 可测, 用法: $0 <pmdl> <wav1> ..."; exit 1; }

mkdir -p "$OUT.d"
c++ -std=c++17 -O1 -D_GLIBCXX_USE_CXX11_ABI=0 \
   "$ROOT/tools/snowboy_detect_test.cc" "$SB/snowboy-detect-c-wrapper.cc" \
   "$SB/lib/ubuntu64/libsnowboy-detect.a" \
   -lblas -llapack -lgfortran -lm -lpthread -o "$OUT" 2> "$OUT.log" || { cat "$OUT.log"; exit 1; }

# common.res 用相对路径, 所以必须从 Client 目录跑
cd "$ROOT"
"$OUT" "$MODEL" "${WAVS[@]}"
rc=$?
rm -rf "$OUT" "$OUT.d" "$OUT.log"
exit $rc
