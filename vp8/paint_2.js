function _paint($luma, $cb, $cr, h, stride) {
  for (var y1,y2,u,v,ruv,guv,buv,j,surface=SDL.surfaces[SDL.screen],w=surface.width,w_2=w>>1,W=w*4, d=surface.image.data, r=0; h-=2;) {
    for (j=w_2; j--;) {
      u = HEAPU8[$cr++];
      v = HEAPU8[$cb++];
      ruv = 409*u-56992;
      guv = 34784-208*u-100*v;
      buv = 516*v-70688;

      y2 = HEAPU8[$luma+stride]*298;
      y1 = HEAPU8[$luma++]*298;
      d[r+W] = y2+ruv>>8;
      d[r++] = y1+ruv>>8;
      d[r+W] = y2+guv>>8;
      d[r++] = y1+guv>>8;
      d[r+W] = y2+buv>>8;
      d[r++] = y1+buv>>8;
      r++;

      y2 = HEAPU8[$luma+stride]*298;
      y1 = HEAPU8[$luma++]*298;
      d[r+W] = y2+ruv>>8;
      d[r++] = y1+ruv>>8;
      d[r+W] = y2+guv>>8;
      d[r++] = y1+guv>>8;
      d[r+W] = y2+buv>>8;
      d[r++] = y1+buv>>8;
      r++;
    }
    r+=W;
    $luma+=2*stride-w;
    $cr+=stride-w>>1;
    $cb+=stride-w>>1;
  }
  surface.ctx.putImageData(surface.image, 0, 0 );
}

