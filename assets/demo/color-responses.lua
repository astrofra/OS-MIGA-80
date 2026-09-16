function main(): void
  local x:i32=20;local y:i32=20;local row:i32=0
  local phase:i32=0;local band:i32=15
  local color:u8=1;local dither:u8=2
  local paint:u8=1
  local response:u8=RESPONSE_NEUTRAL;local shown:u8=RESPONSE_NEUTRAL
  local now:fix=0.0;local next_change:fix=0.0

  layer(PLANAR);cls(0)
  while color<15 do
    line(x,20,x,235,color);line(x+1,20,x+1,235,dither)
    line(x+2,20,x+2,235,color);line(x+3,20,x+3,235,dither)
    line(x+4,20,x+4,235,color);line(x+5,20,x+5,235,dither)
    line(x+6,20,x+6,235,color);line(x+7,20,x+7,235,dither)
    line(x+8,20,x+8,235,color);line(x+9,20,x+9,235,dither)
    line(x+10,20,x+10,235,color);line(x+11,20,x+11,235,dither)
    line(x+12,20,x+12,235,color);line(x+13,20,x+13,235,dither)
    line(x+14,20,x+14,235,color)
    x=x+15;color=color+1;dither=dither+1
  end
  line(230,20,230,235,15);line(231,20,231,235,15)
  line(232,20,232,235,15);line(233,20,233,235,15)
  line(234,20,234,235,15);line(235,20,235,235,15)

  layer(PIXEL);cls(0);y=20;color=14;dither=15;paint=color;phase=0
  while color>0 do
    band=15;if color>8 then band=16 else end;row=0
    while row<band do
      x=20+phase
      while x<236 do pset(x,y,paint);x=x+2 end
      phase=1-phase;paint=color+dither-paint;y=y+1;row=row+1
    end
    color=color-1;dither=dither-1;paint=color
  end

  while true do
    now=time()
    if now>=next_change then
      shown=response;color_response(shown);layer(PIXEL)
      tri(0,0,255,0,255,11,0);tri(0,0,255,11,0,11,0)
      if shown==RESPONSE_NEUTRAL then print("NEUTRAL RGB12",4,4,15);response=RESPONSE_WARM_NEGATIVE else end
      if shown==RESPONSE_WARM_NEGATIVE then print("WARM NEGATIVE",4,4,15);response=RESPONSE_COOL_REVERSAL else end
      if shown==RESPONSE_COOL_REVERSAL then print("COOL REVERSAL",4,4,15);response=RESPONSE_INSTANT_600 else end
      if shown==RESPONSE_INSTANT_600 then print("INSTANT 600",4,4,15);response=RESPONSE_MUTED_METROPOLIS else end
      if shown==RESPONSE_MUTED_METROPOLIS then print("MUTED METROPOLIS",4,4,15);response=RESPONSE_PANCHRO_MONO else end
      if shown==RESPONSE_PANCHRO_MONO then print("PANCHRO MONO",4,4,15);response=RESPONSE_NTSC_1953 else end
      if shown==RESPONSE_NTSC_1953 then print("NTSC 1953",4,4,15);response=RESPONSE_PAL_SECAM_625 else end
      if shown==RESPONSE_PAL_SECAM_625 then print("PAL SECAM 625",4,4,15);response=RESPONSE_OSKM_1960 else end
      if shown==RESPONSE_OSKM_1960 then print("OSKM 1960",4,4,15);response=RESPONSE_DEUTAN_2009 else end
      if shown==RESPONSE_DEUTAN_2009 then print("DEUTAN 2009",4,4,15);response=RESPONSE_PROTAN_2009 else end
      if shown==RESPONSE_PROTAN_2009 then print("PROTAN 2009",4,4,15);response=RESPONSE_VIOLET_DRIVE else end
      if shown==RESPONSE_VIOLET_DRIVE then print("VIOLET DRIVE",4,4,15);response=RESPONSE_NEUTRAL else end
      next_change=next_change+3.0
    else end
    flip()
  end
end
