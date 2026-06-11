#ifndef _DOSVNC_PROTO_H_
#define _DOSVNC_PROTO_H_

#define MAXPWLEN	8
#define CHALLENGESIZE	16

#define ST_ERROR	(-1)
#define ST_IDLE		0
#define ST_RECT		2
#define ST_RAW		3
#define ST_COPY		4
#define ST_RRE		5
#define ST_CRRE		7
#define ST_HEXTILE	8

#define VNC_KEY_SHIFT	0xFFE1
#define VNC_KEY_CONTROL	0xFFE3
#define VNC_KEY_ALT	0xFFE9
#define VNC_KEY_META	0xFFE7

/* Define the VncSocket structure to wrap a Win32 SOCKET handle */
struct VncSocket {
    SOCKET sock;
};

extern void sock_init(void);
extern int socket_connect(struct VncSocket* s, char* host, int port);
extern int tcp_tick(struct VncSocket* s);
extern int sock_dataready(struct VncSocket* s);
extern void sock_close(struct VncSocket* s);
extern int sock_read(struct VncSocket* s, char* buffer, int len);
extern int sock_write(struct VncSocket* s, char* buffer, int len);
extern int sock_getc(struct VncSocket* s);
extern int sock_putc(struct VncSocket* s, char ch);

extern int auth_vnc(struct VncSocket *fd, char *passwd);
extern int init_vnc_client(struct VncSocket *fd);
extern int setup_vnc_pixelformat(struct VncSocket *fd);
extern int setup_vnc_encodings(struct VncSocket *fd);
extern int request_vnc_refresh(struct VncSocket *fd);
extern int parse_vnc_msg(struct VncSocket *fd);
extern int parse_vnc_rect(struct VncSocket *fd);
extern int parse_vnc_raw(struct VncSocket *fd, int *x, int *y, int *w, int *h,
			unsigned char* buf);
extern int parse_vnc_copy(struct VncSocket *fd, int *x, int *y, int *w, int *h,
			int *srcx, int *srcy);
extern int parse_vnc_rre(struct VncSocket *fd, int *x, int *y, int *w, int *h,
			unsigned char *buf);
extern int parse_vnc_crre(struct VncSocket *fd, int *x, int *y, int *w, int *h,
			unsigned char *buf);
extern int parse_vnc_hextile(struct VncSocket *fd, int *x, int *y, int *w, int *h,
			unsigned char* buf);
extern int send_vnc_key(struct VncSocket *fd, int kbd);
extern int send_vnc_shift(struct VncSocket *fd, int vnc_key, int down);
extern int send_vnc_pointer(struct VncSocket *fd, int x, int y, int b);


#endif
