#ifndef _DOSVNC_PROTO_H_
#define _DOSVNC_PROTO_H_

#define BUF_IN_MAX	512

#define MAXPWLEN	8
#define CHALLENGESIZE	16

#define ST_ERROR	(-1)
#define ST_IDLE		0
#define ST_RECT		2
#define ST_RAW		3
#define ST_COPY		4
#define ST_RRE		5
#define ST_CRRE		7

#define DOS_XMAX	640
#define DOS_YMAX	480
#define DOS_ALT_ESC	0x101
#define DOS_ALT_TILDE	0x129

#define VNC_KEY_SHIFT	0xFFE1
#define VNC_KEY_CONTROL	0xFFE3
#define VNC_KEY_ALT	0xFFE9
#define VNC_KEY_META	0xFFE7

extern int auth_vnc(tcp_Socket *fd, char *passwd);
extern int init_vnc_client(tcp_Socket *fd);
extern int setup_vnc_pixelformat(tcp_Socket *fd);
extern int setup_vnc_encodings(tcp_Socket *fd);
extern int request_vnc_refresh(tcp_Socket *fd);
extern int parse_vnc_msg(tcp_Socket *fd);
extern int parse_vnc_rect(tcp_Socket *fd);
extern int parse_vnc_raw(tcp_Socket *fd, int *x, int *y, int *w, int *h,
			long *p, int *s, unsigned char* buf);
extern int parse_vnc_copy(tcp_Socket *fd, int *x, int *y, int *w, int *h,
			int *srcx, int *srcy);
extern int parse_vnc_rre(tcp_Socket *fd, int *x, int *y, int *w, int *h,
			unsigned char *buf);
extern int parse_vnc_crre(tcp_Socket *fd, int *x, int *y, int *w, int *h,
			unsigned char *buf);
extern int send_vnc_key(tcp_Socket *fd, int kbd);
extern int send_vnc_shift(tcp_Socket *fd, int vnc_key, int down);
extern int send_vnc_pointer(tcp_Socket *fd, int x, int y, int b);


#endif
