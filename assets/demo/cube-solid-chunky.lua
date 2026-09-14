function main(): void
 local t:fix=0.0;local s:fix=0.0;local c:fix=0.0
 local u:fix=0.0;local w:fix=0.0;local g:fix=0.0
 local x:fix=0.0;local y:fix=0.0;local z:fix=0.0
 local r:fix=0.0;local d:fix=0.0;local f:i32=0
 local v:i32=0;local a:i32=0;local b:i32=0
 local ax:i32=0;local ay:i32=0;local bx:i32=0
 local by:i32=0;local k:u8=0;layer(PIXEL)
 music_play("SYS:mods/93_10_12_A_SYNTH_1.mod")
 while t<10.0 do
  cls(0);s=sin(t*0.8);c=cos(t*0.8)
  u=sin(t*0.55+0.65);w=cos(t*0.55+0.65);f=0
  while f<6 do g=fix(f-f/2*2)*2.0-1.0
   x=fix(f/2-f/4*2)*g;y=fix(f/4)*g;z=g-x-y
   r=x*c+z*s;z=z*c-x*s;x=r
   r=y*w-z*u;z=y*u+z*w;y=r
   if z< -0.167 then d=0.2-z*0.7+y*0.2-x*0.2;k=2
    while d>0.125 do k=k+1;d=d-0.125 end
    v=0;while v<4 do
     a=v+v/2;x=fix(v/2*2-1);y=fix((a-a/2*2)*2-1);z=g
     if f>=2 then
      if f<4 then r=x;x=z;z=r else r=y;y=z;z=r end else end
     r=x*c+z*s;z=z*c-x*s;x=r
     r=y*w-z*u;z=y*u+z*w;y=r
     a=128+i32(x*410.0/(z+6.0));b=128-i32(y*410.0/(z+6.0))
     if v==0 then ax=a;ay=b else
      if v>=2 then tri(ax,ay,bx,by,a,b,k) else end end
     bx=a;by=b;v=v+1 end
   else end f=f+1 end
  flip();t=time() end end
