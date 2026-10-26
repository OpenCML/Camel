#!/bin/sh
# Encodes rendered frames into ../renders/<scene>/<scene>.mp4 (1080p60, H.264).
# Usage: ./encode.sh shot06 [shot07 ...]   (run in promo/motion after render.cjs)
set -e
for n in "$@"; do
  mkdir -p "../renders/$n"
  ffmpeg -y -loglevel error -framerate 60 -i "output/camel-promo/$n/%06d.png" \
    -c:v libx264 -preset slow -crf 16 -pix_fmt yuv420p -movflags +faststart "../renders/$n/$n.mp4"
  echo "../renders/$n/$n.mp4"
done
