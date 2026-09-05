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
conversion_build_dir=$4
assembler=$5
objcopy=$6
report=$7
expected_report=$8
assembly_o0="$conversion_build_dir/generated-o0.s"
object_o0="$conversion_build_dir/generated-o0.o"
image_o0="$conversion_build_dir/generated-o0.bin"
assembly_o1="$conversion_build_dir/generated-o1.s"
object_o1="$conversion_build_dir/generated-o1.o"
image_o1="$conversion_build_dir/generated-o1.bin"
evaluation_error="$conversion_build_dir/evaluation-error.txt"

mkdir -p "$conversion_build_dir" "$(dirname -- "$report")"
"$compiler" "$source" -O0 -S -o "$assembly_o0"
"$assembler" -m68020 "$assembly_o0" -o "$object_o0"
"$objcopy" -O binary -j .text "$object_o0" "$image_o0"
"$compiler" "$source" -O1 -S -o "$assembly_o1"
"$assembler" -m68020 "$assembly_o1" -o "$object_o1"
"$objcopy" -O binary -j .text "$object_o1" "$image_o1"

grep -Fq 'cmp.l   #0x00010000,%d0' "$assembly_o0"
grep -Fq 'swap    %d0' "$assembly_o0"
grep -Fq 'clr.w   %d0' "$assembly_o0"
grep -Eq 'cmp\.l +#0x00010000,%d[0-7]' "$assembly_o1"
grep -Eq 'ext\.l +%d[0-7]' "$assembly_o1"
grep -Fq '.L_conversions_fault_conversion:' "$assembly_o0"
grep -Fq '.L_conversions_fault_conversion:' "$assembly_o1"
if grep -Eq '[[:space:]](jsr|bsr)' "$assembly_o0" "$assembly_o1"; then
    printf 'numeric conversion unexpectedly calls a runtime helper\n' >&2
    exit 1
fi

: >"$report"
run_value_case()
{
    image=$1
    level=$2
    name=$3
    value_text=$4
    value_raw=$5
    fraction_text=$6
    fraction_raw=$7
    bool_text=$8
    bool_raw=$9
    expected=$("$compiler" "$source" --eval "$value_text" \
        "$fraction_text" "$bool_text")

    "$runner" --case "$image" "$level/$name" "$value_raw" \
        "$fraction_raw" "$bool_raw" "$expected" >>"$report"
}

run_fault_case()
{
    image=$1
    level=$2
    name=$3
    value_text=$4
    value_raw=$5
    line=$6
    column=$7

    if "$compiler" "$source" --eval "$value_text" 0.5 false \
            >/dev/null 2>"$evaluation_error"; then
        printf 'typed-IR oracle accepted out-of-range conversion for %s/%s\n' \
            "$level" "$name" >&2
        exit 1
    fi
    if ! grep -Fqx "$source:$line:$column: error: conversion out of range" \
            "$evaluation_error"; then
        printf 'typed-IR oracle reported the wrong conversion fault for %s/%s\n' \
            "$level" "$name" >&2
        sed -n '1,4p' "$evaluation_error" >&2
        exit 1
    fi
    "$runner" --fault-case "$image" "$level/$name" "$value_raw" \
        0x00008000 0 2 "$line" "$column" >>"$report"
}

run_value_suite()
{
    image=$1
    level=$2

    run_value_case "$image" "$level" fraction 0 0 1.5 0x00018000 false 0
    run_value_case "$image" "$level" positive_trunc 2 2 1.75 0x0001c000 true 1
    run_value_case "$image" "$level" negative_trunc -2 0xfffffffe -1.75 0xfffe4000 true 1
    run_value_case "$image" "$level" negative_fraction -2 0xfffffffe -1.75 0xfffe4000 false 0
    run_value_case "$image" "$level" positive_limit 32767 32767 0.5 0x00008000 false 0
    run_value_case "$image" "$level" negative_limit -32768 0xffff8000 0.5 0x00008000 false 0
}

run_value_suite "$image_o0" generated-o0
run_fault_case "$image_o0" generated-o0 positive_range 32768 32768 3 28
run_fault_case "$image_o0" generated-o0 negative_range -32769 0xffff7fff 3 28
run_value_suite "$image_o1" generated-o1
run_fault_case "$image_o1" generated-o1 positive_range 32768 32768 3 28
run_fault_case "$image_o1" generated-o1 negative_range -32769 0xffff7fff 3 28

o0_size=$(wc -c <"$image_o0" | tr -d '[:space:]')
o1_size=$(wc -c <"$image_o1" | tr -d '[:space:]')
if [ "$o1_size" -ge "$o0_size" ]; then
    printf 'O1 conversion code-size regression: O0=%s bytes, O1=%s bytes\n' \
        "$o0_size" "$o1_size" >&2
    exit 1
fi
printf 'PASS  compiler explicit i32/fix conversions match typed-IR oracle (12 values; 4 controlled faults)\n' \
    >>"$report"
printf 'PASS  compiler O1 conversion code size reduced from %s to %s bytes\n' \
    "$o0_size" "$o1_size" >>"$report"

diff -u "$expected_report" "$report"
cat "$report"
