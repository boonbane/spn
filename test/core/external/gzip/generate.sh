#!/bin/sh
set -eu

out=$(cd "$(dirname "$0")" && pwd)

printf A | gzip -n > "$out/member.gz"
{ printf A | gzip -n; printf B | gzip -n; } > "$out/members.gz"
