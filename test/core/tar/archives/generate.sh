#!/bin/sh
set -eu

out=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"

long=$(printf 'G%.0s' $(seq 101))
pack() {
  format=$1
  archive=$2
  shift 2
  options=
  if [ "$format" = pax ]; then
    options='--pax-option=exthdr.name=%d/PaxHeaders/%f,delete=atime,delete=ctime'
  fi
  tar --format="$format" $options -b1 --no-recursion --owner=0 --group=0 --numeric-owner \
    --mtime='2000-01-01 00:00:00Z' -cf "$out/$archive.tar" "$@"
}

mkdir kinds && cd kinds
mkdir A
printf B > A/B
printf C > A/C
chmod 755 A A/C
chmod 644 A/B
ln -s B A/D
ln A/B A/E
pack ustar kinds A A/B A/C A/D A/E
cd ..

mkdir long && cd long
mkdir "$long"
printf F > "$long/F"
chmod 644 "$long/F"
ln -s "$long/F" D
pack ustar ustar "$long/F"
pack gnu gnu "$long/F" D
pack pax pax "$long/F" D
cd ..

mkdir fifo && cd fifo
mkfifo A
pack ustar fifo A
cd ..

mkdir git && cd git
git init -q
mkdir A
printf B > A/B
git add A/B
GIT_AUTHOR_DATE='2000-01-01T00:00:00Z' GIT_COMMITTER_DATE='2000-01-01T00:00:00Z' \
  git -c user.name=A -c user.email=A@A -c commit.gpgsign=false commit -q -m A
git -c tar.umask=0022 archive --format=tar -o "$out/git.tar" HEAD
cd ..
