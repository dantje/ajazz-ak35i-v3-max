#!/usr/bin/env bash
# Argument-parsing test suite for ajazz.
#
# Tests every example from the README plus edge cases.
# Does NOT require the keyboard to be connected.
#
# "Parsing OK" means: the binary accepted the arguments and only failed
# because no device was found — not because of a parse error.
#
# Usage: tests/test_args.sh [path/to/ajazz]
#
set -euo pipefail

AJAZZ="${1:-build/ajazz}"

if [[ ! -x "$AJAZZ" ]]; then
    echo "Binary not found: $AJAZZ  (run 'make' first)" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

PASS=0
FAIL=0

pass() { echo "  PASS  $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL  $1"; FAIL=$((FAIL + 1)); }

# Expect exit 0.
expect_ok() {
    local desc="$1"; shift
    if "$AJAZZ" "$@" >/dev/null 2>&1; then
        pass "$desc"
    else
        fail "$desc  [expected exit 0, got $?]"
    fi
}

# Expect exit 0 and stdout contains a string.
expect_ok_match() {
    local desc="$1"; local pattern="$2"; shift 2
    local out
    out=$("$AJAZZ" "$@" 2>&1) && rc=0 || rc=$?
    if [[ $rc -eq 0 && "$out" == *"$pattern"* ]]; then
        pass "$desc"
    else
        fail "$desc  [rc=$rc, pattern='$pattern' not found in: $out]"
    fi
}

# Expect argument parsing to succeed — either the command runs (exit 0 when
# device is present) or it fails cleanly because no device was found.
expect_no_device() {
    local desc="$1"; shift
    local out rc=0
    out=$("$AJAZZ" "$@" 2>&1) || rc=$?
    if [[ $rc -eq 0 || "$out" == *"not found"* || "$out" == *"not plugged"* ]]; then
        pass "$desc"
    else
        fail "$desc  [unexpected output: $out]"
    fi
}

# Expect exit non-zero with an error message NOT containing "not found".
# Used to verify that invalid args are rejected cleanly.
expect_parse_error() {
    local desc="$1"; shift
    local out rc
    out=$("$AJAZZ" "$@" 2>&1) || rc=$?
    if [[ $rc -ne 0 && "$out" == *"Error:"* && "$out" != *"not found"* ]]; then
        pass "$desc"
    else
        fail "$desc  [expected parse error, got: rc=${rc:-0} '$out']"
    fi
}

# Create minimal valid dummy files for commands that require a file argument.
DUMMY_PNG=$(mktemp /tmp/ajazz_test_XXXX.png)
DUMMY_GIF=$(mktemp /tmp/ajazz_test_XXXX.gif)
DUMMY_RAW=$(mktemp /tmp/ajazz_test_XXXX.raw)
# Make .raw large enough to pass the 256-byte minimum check, with frame count 1
python3 -c "
import sys
buf = bytearray(256 + 240*135*2)
buf[0] = 1   # 1 frame
buf[1] = 1   # delay
sys.stdout.buffer.write(buf)
" > "$DUMMY_RAW"
# Minimal valid 1x1 white PNG
python3 -c "
import struct, zlib, sys
def chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t+d) & 0xffffffff)
sig  = b'\x89PNG\r\n\x1a\n'
ihdr = chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 2, 0, 0, 0))
idat = chunk(b'IDAT', zlib.compress(b'\x00\xff\xff\xff'))
iend = chunk(b'IEND', b'')
sys.stdout.buffer.write(sig + ihdr + idat + iend)
" > "$DUMMY_PNG"
# Minimal valid 1x1 white GIF87a
python3 -c "
import sys
sys.stdout.buffer.write(
    b'GIF87a\x01\x00\x01\x00\x80\x00\x00\xff\xff\xff\x00\x00\x00'
    b',\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02D\x01\x00;')
" > "$DUMMY_GIF"
trap 'rm -f "$DUMMY_PNG" "$DUMMY_GIF" "$DUMMY_RAW"' EXIT

# ---------------------------------------------------------------------------
# Global help
# ---------------------------------------------------------------------------
echo "--- global help"
expect_ok            "--help"              --help
expect_ok            "-h"                 -h
expect_ok_match      "help lists commands" "Commands:" --help

# ---------------------------------------------------------------------------
# No subcommand → prints help, exits non-zero
# ---------------------------------------------------------------------------
echo "--- no subcommand"
if ! "$AJAZZ" >/dev/null 2>&1; then
    pass "no args exits non-zero"
else
    fail "no args should exit non-zero"
fi

# ---------------------------------------------------------------------------
# Unknown command / option
# ---------------------------------------------------------------------------
echo "--- unknown command/option"
expect_parse_error   "unknown command"            frobnicator
expect_parse_error   "unknown global option"      --frobnicator
expect_parse_error   "unknown short global option" -Z

# ---------------------------------------------------------------------------
# list
# ---------------------------------------------------------------------------
echo "--- list"
# list just scans sysfs; succeeds or fails cleanly regardless of device
if "$AJAZZ" list >/dev/null 2>&1; then
    pass "list: device found"
else
    pass "list: no device (clean exit)"
fi
expect_ok            "list --help"                list --help

# ---------------------------------------------------------------------------
# time — README examples
# ---------------------------------------------------------------------------
echo "--- time"
expect_ok            "time --help"                time --help
expect_no_device     "time (now)"                 time
expect_no_device     "time --at HH:MM:SS"         time --at 14:30:00
expect_no_device     "time --at ISO8601"           time --at 2026-06-01T09:00:00
expect_parse_error   "time --at bad value"         time --at notadate
expect_parse_error   "time unknown option"         time --unknown

# ---------------------------------------------------------------------------
# light — README examples
# ---------------------------------------------------------------------------
echo "--- light"
expect_ok            "light --help"               light --help
expect_no_device     "light (defaults)"           light
expect_no_device     "light -m static -r -g -b"  light -m static -r 255 -g 0 -b 0
expect_no_device     "light -m breath RGB"        light -m breath -r 0 -g 128 -b 255
expect_no_device     "light -m breath --brightness" light -m breath --brightness 3
expect_no_device     "light -m spectrum --rainbow" light -m spectrum --rainbow
expect_no_device     "light -m falling --speed --direction" light -m falling --speed 5 --direction 1
expect_no_device     "light -m off"               light -m off
expect_no_device     "light --mode long form"     light --mode breath --brightness 3
expect_no_device     "light -q flag"              -q light -m off
expect_no_device     "light -v flag"              -v light -m off

# Invalid values
expect_parse_error   "light unknown mode"         light -m unknownmode
expect_parse_error   "light -r out of range"      light -r 256
expect_parse_error   "light -g out of range"      light -g -1
expect_parse_error   "light --brightness out of range" light --brightness 6
expect_parse_error   "light --speed out of range" light --speed 6
expect_parse_error   "light --direction out of range" light --direction 4
expect_parse_error   "light unknown option"       light --unknown

# ---------------------------------------------------------------------------
# solid — README examples
# ---------------------------------------------------------------------------
echo "--- solid"
expect_ok            "solid --help"               solid --help
expect_no_device     "solid (defaults)"           solid
expect_no_device     "solid -r -g -b"             solid -r 0 -g 255 -b 0
expect_no_device     "solid black"                solid -r 0 -g 0 -b 0
expect_no_device     "solid long forms"           solid --red 255 --green 0 --blue 0

# Invalid
expect_parse_error   "solid -r out of range"      solid -r 256
expect_parse_error   "solid unknown option"       solid --unknown

# ---------------------------------------------------------------------------
# image — README examples
# ---------------------------------------------------------------------------
echo "--- image"
expect_ok            "image --help"               image --help
expect_parse_error   "image missing file"         image
expect_no_device     "image file (png)"           image "$DUMMY_PNG"
expect_no_device     "image --fill"               image "$DUMMY_PNG" --fill
expect_no_device     "image raw file"             image "$DUMMY_RAW"

# Invalid
expect_parse_error   "image unknown option"       image "$DUMMY_PNG" --unknown

# ---------------------------------------------------------------------------
# animation — README examples
# ---------------------------------------------------------------------------
echo "--- animation"
expect_ok            "animation --help"           animation --help
expect_parse_error   "animation missing file"     animation
expect_no_device     "animation gif"              animation "$DUMMY_GIF"
expect_no_device     "animation --fill"           animation "$DUMMY_GIF" --fill
expect_no_device     "animation raw file"         animation "$DUMMY_RAW"

# Invalid
expect_parse_error   "animation unknown option"   animation "$DUMMY_GIF" --unknown

# ---------------------------------------------------------------------------
# Global flags can appear before subcommand
# ---------------------------------------------------------------------------
echo "--- global flag placement"
expect_no_device     "-v before subcommand"       -v time
expect_no_device     "-q before subcommand"       -q time
expect_no_device     "--verbose before subcommand" --verbose time
expect_no_device     "--quiet before subcommand"  --quiet time

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "Results: $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
