-- Signed Q16.16 arithmetic, comparisons, locals, and CFG joins.
function fixed_point(a: fix, b: fix, positive: bool): fix
    local product: fix = a * b
    product = product * 0.5
    if (a < b) == positive then
        product = product + 1.5
    else
        product = product - 0.25
    end
    return -product
end
