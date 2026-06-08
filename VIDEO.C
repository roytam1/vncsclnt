#include <conio.h>
#include <dos.h>
#include <stdlib.h>
#include "video.h"

#define RED(x) ((x)&3)
#define GREEN(x) (((x)>>2)&7)
#define BLUE(x) (((x)>>5)&7)
#define FASTGREEN(x) ((x)&4)

int video_pixcolor(unsigned char *buf)
{
	if (FASTGREEN(*buf)) return 15;
	 		else return 0;
}

int video_init()
{
	struct REGPACK r1;
	r1.r_ax=0x12;
	intr(0x10,&r1);
	
	
	return atexit(video_stop);
}

void video_stop()
{
	struct REGPACK r1;
	r1.r_ax=0x03;
	intr(0x10,&r1);
}

unsigned char far *vram=(unsigned char far*)0xa0000000L;

void drawpixel(int x, int y, int c)
{
	unsigned short addr;
	register unsigned char t;
	addr = (unsigned short)y;
	addr <<=4;
	addr += addr <<2;
	addr += (x>>3);
	
	outport(0x3ce,0x0005); // writemode 0
	outportb(0x3ce,0x08);
	outportb(0x3cf,0x80 >> (x & 7));
	t=vram[addr];
	t=t;
	outport(0x3c4,0x0f02);
	vram[addr]=0x00;
	outportb(0x3c5,c);
	vram[addr]=0xff;
	outport(0x3ce,0xff08);
}

void drawrect(int x, int y, int w, int h, int c)
{
	int i;
	for(i=h; i--;) drawpixel(x,y+i,c), drawpixel(x+w-1,y+i,c);
	for(i=w; i--;) drawpixel(x+i,y,c), drawpixel(x+i,y+h-1,c);
}

void drawbar(int x, int y, int w, int h, int c)
{
	unsigned char lbm,rbm;
	unsigned char far *la;
	register unsigned char t;

	outport(0x3c4,0x0f02); // layers to use
	outport(0x3ce,0x0205); // writemode 2
	outportb(0x3ce,0x08);

	w+=x-1;
	lbm = 0xff >> (x & 7);
	rbm = 0xff << (w & 7);
	x >>= 3; w >>= 3;
	if (x==w) lbm &= rbm;
	w -= x;
	for(;h--;y++) {
		la = vram + (y << 4) + (y << 6) + x; // line address
		t=*la; t=t;
		outportb(0x3cf, lbm);
		*la++=c;
		if (w > 1) {
			outportb(0x3cf, 0xff);
			for(t=1; t < (unsigned char)w; t++)
				*la++=c;
			}
		t=*la; t=t;
		outportb(0x3cf, rbm);
		*la=c;
	}
	outportb(0x3cf, 0xff);
	outport(0x3ce,0x0005); // writemode 0
}

#define outout(m,o) if (oldmask ^ m) { \
			 outportb(0x3cf, oldmask=m); \
			} \
			if (m!=0xff) { mask=*va; } *va++ = o

void video_blt(int x, int y, int w, int h,
		int fromx, int fromy)
{
	int t,t80,dir,i;
	unsigned char far  *sa;
	unsigned char far  *da;
	
	if (x==fromx && y==fromy) return;

	outport(0x3ce,0x0105); // writemode 1
	t=(w+7)>>3;
	t80=80-t;

	if (y < fromy)
		dir=1;
	else {
		if (y > fromy)
			dir=-1;
		else {
			if (x < fromx)
				dir=1;
			else
				dir=-1;
		}
	}

	if (dir < 0) {
		x+=w-1; y+=h-1;
		fromx+=w-1; fromy+=h-1;
	}
	sa = vram + (fromy << 4) + (fromy << 6) + (fromx >> 3);
	da = vram + (y << 4) + (y << 6) + (x >> 3); // dest

	if (dir > 0) {
		for(;h--;) {
			for(i=t;i--;) *da++=*sa++;
			sa+=t80;da+=t80;
		}
	} else {
		for(;h--;) {
			for(i=t;i--;) *da--=*sa--;
			sa-=t80;da-=t80;
		}
	}

	outport(0x3ce,0x0005); // writemode 0
	outport(0x3ce,0xff08); // bitmask 0xff
}


void video_blk(int x, int y, int w, int h,
		long pos, int bytes, unsigned char *buf)
{
	unsigned short	lx, ly;
	unsigned char far  *va;
	unsigned char far  *la;
	register unsigned char out,mask,runbit,bitshift;
	unsigned char oldmask;
	
	outport(0x3c4,0x0f02); // layers to use
	outport(0x3ce,0x0005); // writemode 0
	outportb(0x3ce,0x08);
	outportb(0x3cf, oldmask=0xff);

	ly = pos / (long)w;
	lx = pos % (long)w;
	lx += x; ly += y;
	w += x - 1; h += y - 1;
	la = vram + (ly << 4) + (ly << 6); // line address
	
	va = la + (lx >> 3);
	out=0;
	bitshift = (lx & 7);
	mask = 0xff >> bitshift;
	runbit = 0x80 >> bitshift;

	while(bytes && ly <= h) {

	    if(FASTGREEN(*buf++)) out|=runbit;

	    if (!(--bytes) || lx >= w) { // exactly: equal, but...
		if (runbit!=1)
			mask &= 0xff << (7-(bitshift));
		outout(mask,out);
		lx = x;
		la+=80; ly++;
		va = la + (lx >> 3);
		bitshift = (lx & 7);	
		runbit = 0x80 >> bitshift;
		mask = 0xff >> bitshift;
		out=0;		
	    } else {
		lx++;
		bitshift++;
		runbit >>=1;
		if (runbit==0) {
			outout(mask,out);
			mask=0xff; out=0;
			bitshift=0; runbit=0x80;
		}
	    }
	}
	outport(0x3ce,0x0005); // writemode 0
	outport(0x3ce,0xff08); // bitmask 0xff
}

