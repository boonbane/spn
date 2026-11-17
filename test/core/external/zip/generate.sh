#!/bin/sh
set -eu

out=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"

name=$(printf 'F%.0s' $(seq 17))
pack() {
  archive=$1
  shift
  TZ=UTC zip -q -X -y "$archive.zip" "$@"
  cp "$archive.zip" "$out/$archive.zip"
}

mkdir A
printf B > B
printf C > C
printf E > E
printf F > "$name"
printf '%64s' '' | tr ' ' B > Z
ln -s B D
chmod 755 A C
chmod 644 B E Z "$name"
touch -h -d '2000-01-01 00:00:00Z' A B C D E Z "$name"

pack stored -0 B
pack deflated Z
pack bzip2 -Z bzip2 Z
pack dir A
pack executable C
pack symlink D
pack entries B E
pack name "$name"
