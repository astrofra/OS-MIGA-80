function main(): void
  local x:i32=0;local y:i32=0
  local color:u8=1;local dither:u8=2
  local response:u8=RESPONSE_NEUTRAL;local shown:u8=RESPONSE_NEUTRAL
  local now:fix=0.0;local next_change:fix=0.0

  layer(PLANAR);cls(0)
  while color<15 do
    line(x,16,x,255,color);x=x+1;line(x,16,x,255,dither);x=x+1
    line(x,16,x,255,color);x=x+1;line(x,16,x,255,dither);x=x+1
    line(x,16,x,255,color);x=x+1;line(x,16,x,255,dither);x=x+1
    line(x,16,x,255,color);x=x+1;line(x,16,x,255,dither);x=x+1
    line(x,16,x,255,color);x=x+1;color=color+1;dither=dither+1
  end
  line(126,16,126,255,15);line(127,16,127,255,15)

  layer(PIXEL);cls(0);x=128;color=14;dither=13
  while color>0 do
    line(x,16,x,255,color);x=x+1;line(x,16,x,255,dither);x=x+1
    line(x,16,x,255,color);x=x+1;line(x,16,x,255,dither);x=x+1
    line(x,16,x,255,color);x=x+1;line(x,16,x,255,dither);x=x+1
    line(x,16,x,255,color);x=x+1;line(x,16,x,255,dither);x=x+1
    line(x,16,x,255,color);x=x+1;color=color-1;dither=dither-1
  end
  line(254,16,254,255,1);line(255,16,255,255,1)

  while true do
    now=time()
    if now>=next_change then
      shown=response;color_response(shown);layer(PIXEL);y=0
      while y<12 do line(0,y,255,y,0);y=y+1 end
      if shown==RESPONSE_NEUTRAL then print("NEUTRAL RGB12",4,4,9);response=RESPONSE_WARM_NEGATIVE else end
      if shown==RESPONSE_WARM_NEGATIVE then print("WARM NEGATIVE",4,4,9);response=RESPONSE_COOL_REVERSAL else end
      if shown==RESPONSE_COOL_REVERSAL then print("COOL REVERSAL",4,4,9);response=RESPONSE_INSTANT_600 else end
      if shown==RESPONSE_INSTANT_600 then print("INSTANT 600",4,4,9);response=RESPONSE_MUTED_METROPOLIS else end
      if shown==RESPONSE_MUTED_METROPOLIS then print("MUTED METROPOLIS",4,4,9);response=RESPONSE_PANCHRO_MONO else end
      if shown==RESPONSE_PANCHRO_MONO then print("PANCHRO MONO",4,4,9);response=RESPONSE_NTSC_1953 else end
      if shown==RESPONSE_NTSC_1953 then print("NTSC 1953",4,4,9);response=RESPONSE_PAL_SECAM_625 else end
      if shown==RESPONSE_PAL_SECAM_625 then print("PAL SECAM 625",4,4,9);response=RESPONSE_OSKM_1960 else end
      if shown==RESPONSE_OSKM_1960 then print("OSKM 1960",4,4,9);response=RESPONSE_DEUTAN_2009 else end
      if shown==RESPONSE_DEUTAN_2009 then print("DEUTAN 2009",4,4,9);response=RESPONSE_PROTAN_2009 else end
      if shown==RESPONSE_PROTAN_2009 then print("PROTAN 2009",4,4,9);response=RESPONSE_VIOLET_DRIVE else end
      if shown==RESPONSE_VIOLET_DRIVE then print("VIOLET DRIVE",4,4,9);response=RESPONSE_NEUTRAL else end
      next_change=next_change+3.0
    else end
    flip()
  end
end
