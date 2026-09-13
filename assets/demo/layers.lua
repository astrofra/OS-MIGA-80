function main(): void
  local y: i32 = 12
  layer(PLANAR)
  while y < 240 do
    line(8, y, 247, y, 3)
    y = y + 12
  end
  line(0, 0, 255, 255, 5)
  line(255, 0, 0, 255, 5)
  pset(128, 128, 15)
  layer(PIXEL)
  line(32, 32, 223, 32, 9)
  line(223, 32, 223, 191, 9)
  line(223, 191, 32, 191, 9)
  line(32, 191, 32, 32, 9)
  line(32, 32, 223, 191, 12)
  line(223, 32, 32, 191, 14)
  pset(128, 128, 0)
end
