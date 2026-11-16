#!/bin/sh
set -eu

out=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"

tar_with() {
  mode=$1
  archive=$2
  shift 2
  tar --format=ustar -b1 --no-recursion --owner=0 --group=0 --numeric-owner \
    --mtime='2000-01-01 00:00:00Z' "$mode" "$archive.tar" "$@"
}

pack() {
  tar_with -cf "$@"
}

append() {
  tar_with -rf "$@"
}

seal() {
  xz -T1 -c "$1.tar" > "$out/$1.tar.xz"
}

mkdir -p A/C A/X
printf B > A/B
printf B > A/C/B
printf C > A/X/C
printf B > B
chmod 755 A A/C A/X
chmod 644 A/B A/C/B A/X/C B
cp A/B A/Y
chmod 755 A/Y
ln -s B A/D
ln -s C A/L
ln -s .. A/U
ln -s ../B A/O
ln A/B A/E
mkfifo A/F

pack file A A/B
seal file
gzip -n -c file.tar > "$out/file.tar.gz"
head -c -8 "$out/file.tar.gz" > "$out/trailer.tar.gz"

head -c 1024 file.tar > cut.tar
seal cut

pack dir A A/C
seal dir

pack orphan A/C/B
seal orphan

pack executable --transform='s,^A/Y$,A/B,' A/Y
seal executable

pack symlink A/B A/D
seal symlink

pack dirlink A/C A/C/B A/L
seal dirlink

pack hardlink A/B A/E
seal hardlink

pack dot ./A/B
seal dot

pack absolute -P --transform='s,^,/,' A/B
seal absolute

pack drive --transform='s,^A/B$,A/C:/B,' A/B
seal drive

pack dotdot --transform='s,^A/B$,A/../B,' A/B
seal dotdot

pack through A/U
append through --transform='s,^A/C/B$,A/U/B,' A/C/B
seal through

pack nested A/U
append nested --transform='s,^A/U$,A/U/U,' A/U
seal nested

pack onto A/O
append onto --transform='s,^A/B$,A/O,' A/B
seal onto

pack nul --format=pax --pax-option='exthdr.name=%d/PaxHeaders/%f,delete=atime,ctime:=0' A/O
append nul --format=pax --pax-option='exthdr.name=%d/PaxHeaders/%f,delete=atime,ctime:=0,path:=A/OX' A/B
printf '\0' | dd of=nul.tar bs=1 seek=2059 conv=notrunc status=none
seal nul

pack outlink -P --transform='s,^A/B$,A/../B,RS' A/B A/E
seal outlink

pack under A/B
append under --transform='s,^A/X/C$,A/B/C,' A/X/C
seal under

pack loose B
seal loose

pack fifo A/F
seal fifo

printf A > "$out/bytes"
printf A | xz -T1 > "$out/bytes.xz"

zipped() {
  archive=$1
  shift
  rm -f "$out/$archive.zip"
  TZ=UTC zip -q -X -y "$out/$archive.zip" "$@"
}

printf '%64s' '' | tr ' ' B > A/Z
touch -h -d '2000-01-01 00:00:00Z' A A/B A/C A/D A/Y A/Z

zipped file A/Z
head -c 40 "$out/file.zip" > "$out/cut.zip"

zipped dir A A/C

zipped executable A/Y

zipped symlink A/B A/D

zipped sum -0 A/B
printf C | dd of="$out/sum.zip" bs=1 seek=33 conv=notrunc status=none

zipped bzip2 -Z bzip2 A/Z
