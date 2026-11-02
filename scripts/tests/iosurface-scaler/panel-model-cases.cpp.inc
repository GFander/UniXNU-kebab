static void put(uint8_t*p,unsigned off,uint32_t value){memcpy(p+off,&value,4);}
static void original_cases(){
 for(unsigned mode=0;mode<12;mode++){
  Memory memory={56};surface=KebabSurface{};surface.memory=&memory;surface.data.resize(80,0xa5);uint8_t*g=surface.data.data();
  for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)for(unsigned ch=0;ch<4;ch++)kpf_write(g+8+y*24+x*8,KPF_W40A,ch,ch==3?65536:((x+y+ch)%2?65536:0));
  std::vector<uint32_t>panel(12,0x69696969);AppleCLCD fb;fb.fPixels=(uintptr_t)(panel.data()+1);uint8_t q[0x110]={};put(q,0x104,1);put(q,0x54,2);put(q,0xcc,2);put(q,0xd0,2);cancel=kebab_console_muted=0;panic_now=false;
  if(mode==1){surface.bytesPerElement=4;surface.pixelFormat=KPF_BGRA;for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)for(unsigned ch=0;ch<4;ch++)g[8+y*24+x*4+ch]=ch==3?255:((x+y+ch)%2?255:0);}
  if(mode==2)kpf_write(g+8,KPF_W40A,3,32768);
  if(mode==3)surface.allocSize=47;
  if(mode==4)memory.n=47;
  if(mode==5)surface.bytesPerRow=15;
  if(mode==6)surface.planeCount=1;
  if(mode==7)surface.pixelFormat=0x26773461;
  if(mode==8)panic_now=true;
  if(mode==9)surface.elementWidth=2;if(mode==10){g[14]=0xc0;g[15]=0xff;}if(mode==11){g[14]=0;g[15]=0;}
  fb.present(q,1);bool good=mode<3||mode==10||mode==11;
  assert(!locks&&surface.refs==1);
  if(good){assert(cancel==1&&kebab_console_muted&&fb.fConsolePaused&&fb.fTraceLast.bytes==16&&fb.fTraceLast.rejected==0);for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++){uint32_t want=0xff000000;for(unsigned ch=0;ch<3;ch++)if((x+y+ch)%2)want|=255u<<(ch*8);assert(panel[1+y*3+x]==want);}}
  else{assert(cancel==0&&!fb.fConsolePaused);for(auto v:panel)assert(v==0x69696969);if(mode!=8)assert(fb.fTraceLast.rejected==1);}
  assert(panel[0]==0x69696969&&panel[3]==0x69696969&&panel[6]==0x69696969&&panel[11]==0x69696969);
 }
 puts("PASS12 actual present/handoff cases, byte and padding checks");return;
}

static void mapped_cases(){
 for(unsigned mode=0;mode<9;mode++){
  unsigned lw=mode==0?1125:8,lh=mode==0?2436:6,pw=mode==0?1080:6,ph=mode==0?2400:4;
  Memory mem={uint64_t(lw*4+16)*lh+8};surface=KebabSurface{};surface.width=lw;surface.height=lh;surface.bytesPerRow=lw*4+16;surface.bytesPerElement=4;surface.pixelFormat=KPF_BGRA;surface.offset=8;surface.allocSize=mem.n;surface.memory=&mem;surface.data.resize(mem.n,0x65);
  for(unsigned y=0;y<lh;y++)for(unsigned x=0;x<lw;x++){uint32_t v=0xff000000|x|(y<<12);memcpy(surface.data.data()+8+y*surface.bytesPerRow+x*4,&v,4);}
  AppleCLCD fb;fb.fLogicalW=lw;fb.fLogicalH=lh;fb.fWidth=pw;fb.fHeight=ph;fb.fRowBytes=pw*4+12;
  std::vector<uint32_t>panel((pw+3)*ph+2,0x69696969);fb.fPixels=(uintptr_t)(panel.data()+1);uint8_t q[0x110]={};put(q,0x104,1);put(q,0x54,2);
  int sx=0,sy=0,sw=lw,sh=lh,dx=0,dy=0,dw=lw,dh=lh;
  if(mode==1){sx=2;sy=1;sw=4;sh=3;dx=1;dy=1;dw=6;dh=4;}
  if(mode==2){dx=-2;dy=-1;}if(mode==3){dx=4;dy=3;dw=4;dh=3;}
  if(mode==4)mem.n--;if(mode==5){dx=INT32_MIN;}if(mode==6)surface.planeCount=1;if(mode==7){sw=lw+1;}if(mode==8){dw=-1;}
  put(q,0x64,sx);put(q,0x68,sy);put(q,0x6c,sw);put(q,0x70,sh);put(q,0xc4,dx);put(q,0xc8,dy);put(q,0xcc,dw);put(q,0xd0,dh);panic_now=false;cancel=0;fb.present(q,1);
  bool good=mode<4||mode==4; // One trailing padding byte is outside visible source.
  uint64_t bytes=0;
  for(unsigned y=0;y<ph;y++)for(unsigned x=0;x<pw;x++){
   // Independent rational pixel-centre reference, not the production helpers.
   double lx=(x+.5)*lw/pw,ly=(y+.5)*lh/ph;
   bool inside=good&&lx>=dx&&lx<dx+dw&&ly>=dy&&ly<dy+dh;
   uint32_t want=0x69696969;
   if(inside){unsigned ix=sx+(unsigned)((lx-dx)*sw/dw),iy=sy+(unsigned)((ly-dy)*sh/dh);want=0xff000000|ix|(iy<<12);bytes+=4;}
   assert(panel[1+y*(pw+3)+x]==want);
  }
  assert(surface.refs==1&&fb.fTraceLast.bytes==bytes);
  if(mode==0){assert(bytes==uint64_t(pw)*ph*4&&cancel==1);assert((panel[1]&4095)==0);assert((panel[1+pw-1]&4095)==lw-1);}
  for(unsigned y=0;y<ph;y++)for(unsigned x=pw;x<pw+3;x++)assert(panel[1+y*(pw+3)+x]==0x69696969);
  assert(panel.front()==0x69696969&&panel.back()==0x69696969);
 }
 for(int edge=-50;edge<=50;edge++)for(unsigned l=1;l<17;l++)for(unsigned p=1;p<17;p++){
  int want=0;while(want<(int)p&&(want+.5)*l/p<edge)want++;
  assert(kebab_fb_panel_edge(edge,p,l)==want);
 }
 puts("PASS9 real mapped present cases, 25856 rational edges, full1125-to1080 canvas, partial/negative rectangles and padding");
}
int main(){original_cases();mapped_cases();return 0;}
