function main(): void
 local t:fix=0.0; local s:fix=0.0; local c:fix=0.0
 local u:fix=0.0; local w:fix=0.0; local e:i32=0
 local v:i32=0; local a:i32=0; local x:fix=0.0
 local y:fix=0.0; local z:fix=0.0; local r:fix=0.0
 local d:fix=0.0; local px:i32=0; local py:i32=0
 local k:u8=0; layer(PIXEL)
 while t<10.0 do
  cls(0); s=sin(t*0.8); c=cos(t*0.8)
  u=sin(t*0.55+0.65); w=cos(t*0.55+0.65); e=0
  while e<12 do
   v=0
   while v<2 do
    a=e-e/4*4; x=fix(a/2*2-1)
    y=fix((a-a/2*2)*2-1); z=fix(v*2-1)
    if e>=4 then
     if e<8 then r=x;x=z;z=r else r=y;y=z;z=r end
    else end
    r=x*c+z*s; z=z*c-x*s; x=r
    r=y*w-z*u; z=y*u+z*w; y=r
    a=128+i32(x*410.0/(z+6.0))
    if v==0 then px=a;py=128-i32(y*410.0/(z+6.0));d=z else
     k=8; if z+d> -1.0 then k=6 else end
     if z+d>0.0 then k=4 else end
     if z+d>1.0 then k=2 else end
     line(px,py,a,128-i32(y*410.0/(z+6.0)),k) end
    v=v+1 end
   e=e+1 end
  flip(); t=time() end
end
