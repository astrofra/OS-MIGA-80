function main(): void
  local x: i32 = 48; local y: i32 = 64
  local i: u8 = 0
  local cr: fix = -2.0; local ci: fix = -1.2
  local zr: fix = 0.0; local zi: fix = 0.0
  local next_zr: fix = 0.0
  while y < 192 do
    x = 48
    cr = -2.0
    while x < 208 do
      zr = 0.0; zi = 0.0
      i = 0
      while i < 15 do
        if zr * zr + zi * zi > 4.0 then
          break
        else
          next_zr = zr * zr - zi * zi + cr
          zi = 2.0 * zr * zi + ci
          zr = next_zr
          i = i + 1
        end
      end
      pset(x, y, i)
      cr = cr + 0.01875
      x = x + 1
    end
    ci = ci + 0.01875
    y = y + 1
  end
end
