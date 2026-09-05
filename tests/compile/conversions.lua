-- Explicit i32/fix conversions, truncation toward zero, and range faults.
function conversions(value: i32, fraction: fix, use_integer: bool): fix
    local converted: fix = fix(value)
    local truncated: i32 = i32(fraction)
    if use_integer then
        converted = converted + fix(truncated)
    else
        converted = converted + fraction
    end
    return converted
end
