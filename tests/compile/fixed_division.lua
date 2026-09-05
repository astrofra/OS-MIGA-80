-- Signed Q16.16 division, statement-only /=, wrapping, and fault sites.
function fixed_division(a: fix, b: fix, c: fix): fix
    local quotient: fix = a / b
    quotient /= c
    return quotient / 1.0
end
