#ifndef _DOS2VNC_VIDEO_H_
#define _DOS2VNC_VIDEO_H_

extern int video_init();
extern void video_stop();

extern void video_blk(int x, int y, int w, int h,
		long pos, int bytes, unsigned char *buf);
extern void video_blt(int x, int y, int w, int h,
		int fromx, int fromy);
extern int video_pixcolor(unsigned char *buf);

extern void drawpixel(int x, int y, int c);
extern void drawrect(int x, int y, int w, int h, int c);
extern void drawbar(int x, int y, int w, int h, int c);

#endif
