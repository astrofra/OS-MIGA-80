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
division_build_dir=$4
assembler=$5
objcopy=$6
report=$7
expected_report=$8
assembly_o0="$division_build_dir/generated-o0.s"
object_o0="$division_build_dir/generated-o0.o"
image_o0="$division_build_dir/generated-o0.bin"
assembly_o1="$division_build_dir/generated-o1.s"
object_o1="$division_build_dir/generated-o1.o"
image_o1="$division_build_dir/generated-o1.bin"
evaluation_error="$division_build_dir/evaluation-error.txt"

mkdir -p "$division_build_dir" "$(dirname -- "$report")"
"$compiler" "$source" -O0 -S -o "$assembly_o0"
"$assembler" -m68020 "$assembly_o0" -o "$object_o0"
"$objcopy" -O binary -j .text "$object_o0" "$image_o0"
"$compiler" "$source" -O1 -S -o "$assembly_o1"
"$assembler" -m68020 "$assembly_o1" -o "$object_o1"
"$objcopy" -O binary -j .text "$object_o1" "$image_o1"

grep -Fq 'divul.l %d1,%d2:%d0' "$assembly_o0"
grep -Fq 'divu.l  %d1,%d2:%d0' "$assembly_o0"
grep -Eq 'divul\.l %d6,%d7:%d[0-5]' "$assembly_o1"
grep -Eq 'divu\.l  %d6,%d7:%d[0-5]' "$assembly_o1"
grep -Eq 'movem\.l .*%d6/%d7,-\(%a7\)' "$assembly_o1"
grep -Eq 'movem\.l \(%a7\)\+,.*%d6/%d7' "$assembly_o1"
if grep -Eq '[[:space:]](jsr|bsr)' "$assembly_o0" "$assembly_o1"; then
    printf 'fixed-point division unexpectedly calls a runtime helper\n' >&2
    exit 1
fi

: >"$report"
run_value_case()
{
    image=$1
    level=$2
    name=$3
    a_text=$4
    a_raw=$5
    b_text=$6
    b_raw=$7
    c_text=$8
    c_raw=$9
    expected=$("$compiler" "$source" --eval "$a_text" "$b_text" "$c_text")

    "$runner" --case "$image" "$level/$name" "$a_raw" "$b_raw" \
        "$c_raw" "$expected" >>"$report"
}

run_fault_case()
{
    image=$1
    level=$2
    name=$3
    a_text=$4
    a_raw=$5
    b_text=$6
    b_raw=$7
    c_text=$8
    c_raw=$9
    line=${10}
    column=${11}

    if "$compiler" "$source" --eval "$a_text" "$b_text" "$c_text" \
            >/dev/null 2>"$evaluation_error"; then
        printf 'typed-IR oracle accepted fixed division by zero for %s/%s\n' \
            "$level" "$name" >&2
        exit 1
    fi
    if ! grep -Fqx "$source:$line:$column: error: division by zero" \
            "$evaluation_error"; then
        printf 'typed-IR oracle reported the wrong fixed-division fault for %s/%s\n' \
            "$level" "$name" >&2
        sed -n '1,4p' "$evaluation_error" >&2
        exit 1
    fi
    "$runner" --fault-case "$image" "$level/$name" "$a_raw" "$b_raw" \
        "$c_raw" 1 "$line" "$column" >>"$report"
}

run_value_suite()
{
    image=$1
    level=$2

    run_value_case "$image" "$level" exact 6.0 0x00060000 2.0 0x00020000 1.5 0x00018000
    run_value_case "$image" "$level" positive_trunc 1.0 0x00010000 3.0 0x00030000 1.0 0x00010000
    run_value_case "$image" "$level" negative_dividend -1.0 0xffff0000 3.0 0x00030000 1.0 0x00010000
    run_value_case "$image" "$level" negative_divisor 1.0 0x00010000 -3.0 0xfffd0000 1.0 0x00010000
    run_value_case "$image" "$level" both_negative -1.0 0xffff0000 -3.0 0xfffd0000 1.0 0x00010000
    run_value_case "$image" "$level" fractional 7.0 0x00070000 2.0 0x00020000 0.5 0x00008000
    run_value_case "$image" "$level" wrap 1.5 0x00018000 0.000030518 0x00000002 1.0 0x00010000
    run_value_case "$image" "$level" min_wrap -32768.0 0x80000000 -1.0 0xffff0000 1.0 0x00010000
}

run_value_suite "$image_o0" generated-o0
run_fault_case "$image_o0" generated-o0 zero_expression 1.0 0x00010000 0.0 0x00000000 1.0 0x00010000 3 29
run_fault_case "$image_o0" generated-o0 zero_update 1.0 0x00010000 1.0 0x00010000 0.0 0x00000000 4 14
run_value_suite "$image_o1" generated-o1
run_fault_case "$image_o1" generated-o1 zero_expression 1.0 0x00010000 0.0 0x00000000 1.0 0x00010000 3 29
run_fault_case "$image_o1" generated-o1 zero_update 1.0 0x00010000 1.0 0x00010000 0.0 0x00000000 4 14

o0_size=$(wc -c <"$image_o0" | tr -d '[:space:]')
o1_size=$(wc -c <"$image_o1" | tr -d '[:space:]')
if [ "$o1_size" -ge "$o0_size" ]; then
    printf 'O1 fixed-division code-size regression: O0=%s bytes, O1=%s bytes\n' \
        "$o0_size" "$o1_size" >&2
    exit 1
fi
printf 'PASS  compiler Q16.16 division matches typed-IR oracle (16 values; 4 controlled faults)\n' \
    >>"$report"
printf 'PASS  compiler O1 fixed-division code size reduced from %s to %s bytes\n' \
    "$o0_size" "$o1_size" >>"$report"

diff -u "$expected_report" "$report"
cat "$report"
