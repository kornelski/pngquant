#!/bin/bash
set -eu
set -o pipefail

TESTDIR=$1
IMGSRC=$TESTDIR/img
TMPDIR=$(mktemp -d -t pngquantXXXXXX)
BIN=$2
TESTBIN=$3
PATH=.:$PATH # Required, since BIN may be just 'pngquant'

$BIN --version 2>&1 | fgrep 2.
$BIN --help | fgrep -q "usage:"

$BIN 2>/dev/null && { echo "should fail without args"; exit 1; } || true

function test_overwrite() {
    cp "$IMGSRC/test.png" "$TMPDIR/overwritetest.png"
    rm -rf "$TMPDIR/overwritetest-fs8.png" "$TMPDIR/overwritetest-or8.png"

    $BIN "$TMPDIR/overwritetest.png"
    test -f "$TMPDIR/overwritetest-fs8.png"

    $BIN --floyd=0.5 --force "$TMPDIR/overwritetest.png"
    test -f "$TMPDIR/overwritetest-fs8.png"
    test '!' -e "$TMPDIR/overwritetest-or8.png"

    $BIN --nofs "$TMPDIR/overwritetest.png"
    test -f "$TMPDIR/overwritetest-or8.png"

    rm "$TMPDIR/overwritetest-or8.png"
    $BIN 2>&1 -nofs "$TMPDIR/overwritetest.png" | fgrep -q warning:
    test -f "$TMPDIR/overwritetest-or8.png"

    $BIN 2>/dev/null --ordered "$TMPDIR/overwritetest.png" && { echo "should refuse to overwrite"; exit 1; } || true

    { $BIN 2>&1 --ordered "$TMPDIR/overwritetest.png" && { echo "should refuse to overwrite"; exit 1; } || true; } | fgrep -q 'not overwriting'

    $BIN "$TMPDIR/overwritetest.png" -o "$TMPDIR/overwritedest.png"
    test -f "$TMPDIR/overwritedest.png"

    $BIN 2>/dev/null "$TMPDIR/overwritetest.png" -o "$TMPDIR/overwritedest.png" && { echo "should refuse to overwrite"; exit 1; } || true
}

function test_skip() {
    cp "$IMGSRC/test.png" "$TMPDIR/skiptest.png"
    rm -rf "$TMPDIR/shouldskip.png"

    $BIN 2>/dev/null "$TMPDIR/skiptest.png" -Q 100-100 -o "$TMPDIR/shouldskip.png" && { echo "should skip due to quality"; exit 1; } || RET=$?
    test "$RET" -eq 99 || { echo "should return 99, not $RET"; exit 1; }
    test '!' -e "$TMPDIR/shouldskip.png"

    $BIN "$TMPDIR/skiptest.png" -Q 0-50 -o "$TMPDIR/q50output.png"
    test -f "$TMPDIR/q50output.png"

    $BIN "$TMPDIR/q50output.png" --skip-if-larger -Q 0-49 -o "$TMPDIR/q49output.png" && { echo "should skip due to filesize"; exit 1; } || RET=$?
    test "$RET" -eq 98 || { echo "should return 98, not $RET"; exit 1; }
    test '!' -e "$TMPDIR/q49output.png"
}

function test_metadata() {
    cp "$IMGSRC/metadata.png" "$TMPDIR/metadatatest.png"
    $BIN 2>/dev/null "$TMPDIR/metadatatest.png"

    # This test will fail if compiled with old libpng
    fgrep -q '<rdf:RDF xmlns:rdf' "$TMPDIR/metadatatest-fs8.png" || { echo "embedded RDF not found. This is expected if configured with Cocoa reader"; exit 1; }

    # This test will fail if compiled without liblcms or cocoa
    fgrep -q 'sRGB' "$TMPDIR/metadatatest-fs8.png" || { echo "sRGB chunk not found. This test requires lcms2"; exit 1; }
}

test_overwrite &
test_skip &
test_metadata &

for job in `jobs -p`
do
    wait $job
done

$TESTBIN
