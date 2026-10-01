#!/bin/sh
# Downloads a few well-known test photos into samples/ (not committed).
set -e
cd "$(dirname "$0")/.."
mkdir -p samples
SKIMAGE=https://raw.githubusercontent.com/scikit-image/scikit-image/v0.22.0/skimage/data
OPENCV=https://raw.githubusercontent.com/opencv/opencv/4.x/samples/data
for f in astronaut.png chelsea.png coffee.png rocket.jpg; do
  curl -fsSL -o "samples/$f" "$SKIMAGE/$f"
done
for f in starry_night.jpg fruits.jpg baboon.jpg; do
  curl -fsSL -o "samples/$f" "$OPENCV/$f"
done
ls samples
