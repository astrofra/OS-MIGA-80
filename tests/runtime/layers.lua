function main(): void
  local x: i32 = 7
  local y: i32 = 13
  while x < 30 do
    layer(PLANAR)
    line(x, y, x + 11, y + 5, 9)
    pset(x + 1, y, 3)
    layer(PIXEL)
    line(y, x, y + 5, x + 11, 6)
    pset(y, x + 1, 0)
    x = x + 3
    y = y + 2
  end
  layer(PLANAR)
  line(128, 128, 150, 139, 1)
  line(128, 128, 139, 150, 2)
  line(128, 128, 117, 150, 3)
  line(128, 128, 106, 139, 4)
  line(128, 128, 106, 117, 5)
  line(128, 128, 117, 106, 6)
  line(128, 128, 139, 106, 7)
  line(128, 128, 150, 117, 8)
  line(-2147483647, 200, 2147483647, 200, 4)
  line(200, -2147483647, 200, 2147483647, 11)
  line(128, 128, 128, 128, 0)
  line(-100, -100, -1, -1, 15)
end
