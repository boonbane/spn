#!/bin/sh
set -eu

out=$(cd "$(dirname "$0")" && pwd)

printf A | xz -T1 > "$out/block.xz"
printf AB | xz -T2 --block-size=1 > "$out/blocks.xz"
{ printf A | xz -T1; printf B | xz -T1; } > "$out/streams.xz"
