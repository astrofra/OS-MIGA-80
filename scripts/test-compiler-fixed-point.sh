#!/bin/sh

set -eu

if [ "$#" -ne 8 ]; then
    printf 'Usage: %s miga80c miga68k-test source build-dir as objcopy report expected-report\n' \
        "$0" >&2
    exit 2
fi

compiler=$1
runner=$2
source=$3
fix_build_dir=$4
assembler=$5
objcopy=$6
report=$7
expected_report=$8
assembly_o0="$fix_build_dir/generated-o0.s"
object_o0="$fix_build_dir/generated-o0.o"
image_o0="$fix_build_dir/generated-o0.bin"
assembly_o1="$fix_build_dir/generated-o1.s"
object_o1="$fix_build_dir/generated-o1.o"
image_o1="$fix_build_dir/generated-o1.bin"
assembly_default="$fix_build_dir/generated.s"

mkdir -p "$fix_build_dir" "$(dirname -- "$report")"
"$compiler" "$source" -O0 -S -o "$assembly_o0"
"$assembler" -m68020 "$assembly_o0" -o "$object_o0"
"$objcopy" -O binary -j .text "$object_o0" "$image_o0"
"$compiler" "$source" -O1 -S -o "$assembly_o1"
"$compiler" "$source" -S -o "$assembly_default"
cmp "$assembly_o1" "$assembly_default"
"$assembler" -m68020 "$assembly_o1" -o "$object_o1"
"$objcopy" -O binary -j .text "$object_o1" "$image_o1"

grep -Fq 'muls.l  %d1,%d2:%d0' "$assembly_o0"
grep -Fq 'move.w  %d2,%d0' "$assembly_o0"
grep -Eq 'muls\.l  .*%d7:%d[0-6]' "$assembly_o1"
grep -Eq 'muls\.l  #0x00008000,%d7:%d[0-6]' "$assembly_o1"
grep -Eq 'move\.w  %d7,%d[0-6]' "$assembly_o1"
grep -Eq 'movem\.l .*%d7,-\(%a7\)' "$assembly_o1"
grep -Eq 'movem\.l \(%a7\)\+,.*%d7' "$assembly_o1"
grep -Fq 'slt' "$assembly_o0"
grep -Fq 'slt' "$assembly_o1"
if grep -Eq '[[:space:]](jsr|bsr)' "$assembly_o0" "$assembly_o1"; then
    printf 'fixed-point code unexpectedly calls a runtime helper\n' >&2
    exit 1
fi

: >"$report"
run_case()
{
    image=$1
    level=$2
    name=$3
    a_text=$4
    a_raw=$5
    b_text=$6
    b_raw=$7
    choose=$8
    expected=$("$compiler" "$source" --eval "$a_text" "$b_text" "$choose")

    "$runner" --case "$image" "$level/$name" "$a_raw" "$b_raw" \
        "$choose" "$expected" >>"$report"
}

for level in generated-o0 generated-o1; do
    if [ "$level" = generated-o0 ]; then
        image=$image_o0
    else
        image=$image_o1
    fi
    run_case "$image" "$level" exact 2.0 0x00020000 3.0 0x00030000 1
    run_case "$image" "$level" negative_round -0.1 0xffffe666 0.1 0x0000199a 1
    run_case "$image" "$level" mixed_sign 1.5 0x00018000 -2.25 0xfffdc000 1
    run_case "$image" "$level" wrap 30000.0 0x75300000 2.0 0x00020000 0
done

o0_size=$(wc -c <"$image_o0" | tr -d '[:space:]')
o1_size=$(wc -c <"$image_o1" | tr -d '[:space:]')
if [ "$o1_size" -ge "$o0_size" ]; then
    printf 'O1 fixed-point code-size regression: O0=%s bytes, O1=%s bytes\n' \
        "$o0_size" "$o1_size" >&2
    exit 1
fi
printf 'PASS  compiler Q16.16 fixed point matches typed-IR oracle (8 cases)\n' \
    >>"$report"
printf 'PASS  compiler O1 fixed-point code size reduced from %s to %s bytes\n' \
    "$o0_size" "$o1_size" >>"$report"

diff -u "$expected_report" "$report"
cat "$report"
