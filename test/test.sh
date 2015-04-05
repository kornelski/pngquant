#!/bin/bash
set -eu
set -o pipefail

TESTDIR=$1
IMGSRC=$TESTDIR/img
TMPDIR=$(mktemp -d -t pngquantXXXXXX)
BIN=$2
PATH=.:$PATH # Required, since BIN may be just 'pngquant'

$BIN --version 2>&1 | fgrep 2.
$BIN --help | fgrep -q "usage:"

$BIN 2>/dev/null && { echo "should fail without args"; exit 1; } || true

cp "$IMGSRC/test.png" "$TMPDIR/"

$BIN "$TMPDIR/test.png"
test -f "$TMPDIR/test-fs8.png"

$BIN --floyd=0.5 --force "$TMPDIR/test.png"
test -f "$TMPDIR/test-fs8.png"
test '!' -e "$TMPDIR/test-or8.png"

$BIN --nofs "$TMPDIR/test.png"
test -f "$TMPDIR/test-or8.png"

rm "$TMPDIR/test-or8.png"
$BIN 2>&1 -nofs "$TMPDIR/test.png" | fgrep -q warning:
test -f "$TMPDIR/test-or8.png"

$BIN 2>/dev/null --ordered "$TMPDIR/test.png" && { echo "should refuse to overwrite"; exit 1; } || true

{ $BIN 2>&1 --ordered "$TMPDIR/test.png" && { echo "should refuse to overwrite"; exit 1; } || true; } | fgrep -q 'not overwriting'
