#!/bin/sh
cat progress >/dev/fd/"$ZIG_PROGRESS"
cat err >&2
exit "$1"
