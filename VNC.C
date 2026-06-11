#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <windows.h>
#include <winsock.h>

#include "d3des.h"

#include "rfbproto.h"
#include "vnc.h"

extern FILE* fout;

unsigned short fb_width = 0;
unsigned short fb_height = 0;
rfbServerToClientMsg rfb_msg;
rfbFramebufferUpdateRectHeader rfb_uprect;
rfbRREHeader rfb_rrehead;
unsigned short rfb_rect;
unsigned long rfb_pos,rfb_total;
char server_name[256];
BOOL isWin32s = 0;

/* --- WATTCP Core Lifecycle Functions Translation --- */

void sock_init(void) {
    WSADATA wsaData;
    DWORD dwWinVer;
    /* Initialize Winsock 1.1 for Windows 95 */
    WSAStartup(MAKEWORD(1, 1), &wsaData);
    dwWinVer = GetVersion();
    isWin32s = (dwWinVer >= 0x80000000) && ((dwWinVer & 0xFF) == 3);
#ifdef DEBUG
		fprintf(fout,"sock_init: dwWinVer=%08x, isWin32s=%d\n",dwWinVer,isWin32s),fflush(fout);
#endif
}

int socket_connect(struct VncSocket* s, char* host, int port) {
    struct sockaddr_in serverAddr;
    unsigned long ip;
    struct hostent* hp;

    s->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s->sock == INVALID_SOCKET) return 0;

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    /* Resolve host IP or hostname */
    ip = inet_addr(host);
    if (ip != INADDR_NONE) {
        memcpy(&serverAddr.sin_addr, &ip, sizeof(ip));
    } else {
        hp = gethostbyname(host);
        if (!hp) {
            closesocket(s->sock);
            return 0;
        }
        memcpy(&serverAddr.sin_addr, hp->h_addr, hp->h_length);
    }

    /* Connect in standard blocking mode (just like DOS) */
    if (connect(s->sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(s->sock);
        return 0;
    }

    return 1;
}

int tcp_tick(struct VncSocket* s) {
    /* WATTCP requires manual processing ticks. Windows does this automatically.
       We use Sleep(1) here to yield CPU time to Windows 95, preventing 
       our infinite loop from pinning a 90s CPU to 100%. */
    Sleep(1);
    return (s->sock != INVALID_SOCKET);
}

int sock_dataready(struct VncSocket* s) {
    unsigned long bytesAvailable = 0;
    /* Use ioctlsocket to check if data is waiting in the Windows network buffer */
    if (ioctlsocket(s->sock, FIONREAD, &bytesAvailable) == SOCKET_ERROR) {
        return 0;
    }
    return (bytesAvailable > 0);
}

void sock_close(struct VncSocket* s) {
    if (s->sock != INVALID_SOCKET) {
        closesocket(s->sock);
        s->sock = INVALID_SOCKET;
    }
}

/* Replaces WATTCP block reads */
/* Upgraded sock_read: Guarantees ALL requested bytes are read before returning */
int sock_read(struct VncSocket* s, char* buffer, int len) {
    int total_read = 0;
    int bytes_left = len;
    int n;
    int read_bytes;

    while (total_read < len) {
        read_bytes = bytes_left > 32767 ? (isWin32s ? 32767 : bytes_left) : bytes_left;
        /* Ask recv to fill whatever is remaining in our requested length */
        n = recv(s->sock, buffer + total_read, read_bytes, 0);

        if (n == 0) {
            /* Connection was gracefully closed by the server */
            return (total_read > 0) ? total_read : 0;
        } 
        else if (n == SOCKET_ERROR) {
            /* A network error occurred */
            return (total_read > 0) ? total_read : -1;
        }

        /* Shift our tracking variables forward by the amount we just received */
        total_read += n;
        bytes_left -= n;
    }

    /* We successfully grabbed every single byte the parser asked for! */
    return total_read;
}

/* Replaces WATTCP block writes */
int sock_write(struct VncSocket* s, char* buffer, int len) {
    return send(s->sock, buffer, len, 0);
}

/* Replaces WATTCP single character getters */
int sock_getc(struct VncSocket* s) {
    char ch;
    if (recv(s->sock, &ch, 1, 0) <= 0) return -1;
    return (int)ch;
}

/* Replaces WATTCP single character putters */
int sock_putc(struct VncSocket* s, char ch) {
    return send(s->sock, &ch, 1, 0);
}

int auth_vnc(struct VncSocket *fd, char *passwd)
{
	CARD8 ch[CHALLENGESIZE];
	CARD8 key[MAXPWLEN];
	int i, pwlen;
	rfbProtocolVersionMsg vmsg;
	CARD32 i32;
	CARD8 x;

	sprintf(vmsg, rfbProtocolVersionFormat, rfbProtocolMajorVersion, rfbProtocolMinorVersion);
	sock_write(fd, vmsg, sz_rfbProtocolVersionMsg);
	sock_read(fd, vmsg, sz_rfbProtocolVersionMsg);

	pwlen = strlen(passwd);
	i32 = rfbConnFailed;
	sock_read(fd, (byte*)&i32, sizeof(i32));
	i32 = ntohl(i32);

	switch(i32) {
	default:
#ifdef DEBUG
		fprintf(fout, "Unknown Authentification scheme %ld\n",i32),fflush(fout);
#endif
	auth_out:
		sock_close(fd);
		return 0;

	case rfbConnFailed: /* conn failed */
#ifdef DEBUG
		fprintf(fout,"Connection failed:"),fflush(fout);
#endif
		i32 = 0;
		sock_read(fd, (byte*)&i32, sizeof(i32));
		i32=ntohl(i32);
		while (i32-- && (sock_read(fd, &x, sizeof(x))==sizeof(x))) {
#ifdef DEBUG
			fprintf(fout,"%c", x),fflush(fout);
#endif
		}
#ifdef DEBUG
		fprintf(fout,"\n"),fflush(fout);
#endif
		goto auth_out;

	case rfbNoAuth:
#ifdef DEBUG
		fprintf(fout,"rfbNoAuth\n"),fflush(fout);
#endif
		return 1;

	case rfbVncAuth:
#ifdef DEBUG
		fprintf(fout,"rfbVncAuth\n"),fflush(fout);
#endif
		if (sock_read(fd, (byte*)ch, CHALLENGESIZE)!=CHALLENGESIZE) {
#ifdef DEBUG
			fprintf(fout,"VncAuth: challenge read error\n"),fflush(fout);
#endif
			goto auth_out;
		}

		/* pad password with nulls */
		for(i=0;i<MAXPWLEN;i++)
		  if (i < pwlen) key[i]=passwd[i];
		  		else 	  key[i]=0;

		/* set des key with padded vnc password */
		deskey(key,EN0);

		/* clean up vnc password and key which no longer necessary */
		for(i=0;i<MAXPWLEN;i++) {
			if (i < pwlen)
				key[i]=passwd[i]=0;
			else
				key[i]=0;
		}
		
		/* encrypt challenge bytes */
		for(i = 0; i < CHALLENGESIZE; i+=8)
			des(ch+i, ch+i);

		/* send back encrypted challenge bytes to server for auth */
		if (sock_write(fd, (byte*)ch, CHALLENGESIZE)!=CHALLENGESIZE) {
#ifdef DEBUG
			fprintf(fout,"VncAuth: response write error\n"),fflush(fout);
#endif
			goto auth_out;
		}
	}

	i32 = rfbVncAuthFailed;
	sock_read(fd, (byte*)&i32, sizeof(i32));
	i32 = ntohl(i32);

	switch(i32) {
	default:
#ifdef DEBUG
		fprintf(fout, "VncAuth: Strange\n"),fflush(fout);
#endif
		goto auth_out;

	case rfbVncAuthFailed:
#ifdef DEBUG
		fprintf(fout, "VncAuth: Failed\n"),fflush(fout);
#endif
		goto auth_out;

	case rfbVncAuthTooMany:
#ifdef DEBUG
		fprintf(fout, "VncAuth: Too Many Failures\n"),fflush(fout);
#endif
		goto auth_out;

	case rfbVncAuthOK:
#ifdef DEBUG
		fprintf(fout, "VncAuth: OK\n"),fflush(fout);
#endif
		return 1;
	}
}

int init_vnc_client(struct VncSocket *fd)
{
	rfbClientInitMsg clientinit;
	rfbServerInitMsg serverinit;
	CARD8  x;
	CARD32 i32;
	int i;

	/* ClientInitialisation */
	clientinit.shared = 1; // share
	sock_write(fd, (byte*)&clientinit, sz_rfbClientInitMsg);

	sock_read(fd, (byte*)&serverinit, sz_rfbServerInitMsg);

	fb_width = ntohs(serverinit.framebufferWidth);
	fb_height = ntohs(serverinit.framebufferHeight);

	i32 = ntohl(serverinit.nameLength);
	for (i=0; i<i32; i++)
		sock_read(fd, i < 256 ? server_name+i : (byte*)&x, 1);

	return 1;
}

int setup_vnc_pixelformat(struct VncSocket *fd)
{

	rfbSetPixelFormatMsg pixformmsg;

	pixformmsg.type = rfbSetPixelFormat;
	pixformmsg.format.bitsPerPixel = 8;
	pixformmsg.format.depth = 8;
	pixformmsg.format.bigEndian = 0; // don't care
	pixformmsg.format.trueColour = 1;

	pixformmsg.format.redMax = htons(7);
	pixformmsg.format.greenMax = htons(7);
	pixformmsg.format.blueMax = htons(3);

	pixformmsg.format.redShift = 0;
	pixformmsg.format.greenShift = 3;
	pixformmsg.format.blueShift = 6;

	sock_write(fd, (byte*)&pixformmsg, sz_rfbSetPixelFormatMsg);

	return 1;
}

int setup_vnc_encodings(struct VncSocket *fd)
{
#define ENCODINGS 5
	rfbSetEncodingsMsg *encodingsmsgp;
	size_t sz_enc;
	CARD32 *enc;

	sz_enc=sz_rfbSetEncodingsMsg+ENCODINGS*sizeof(CARD32);
	encodingsmsgp = (rfbSetEncodingsMsg *)malloc(sz_enc);
	if (encodingsmsgp == NULL) {
		perror("malloc: Cannot initiate communication");
		return 0;
	}
	enc=(CARD32*)(sz_rfbSetEncodingsMsg+(CARD8*)encodingsmsgp);

	encodingsmsgp->type = rfbSetEncodings;
	encodingsmsgp->nEncodings = htons(ENCODINGS);
	enc[0] = htonl(rfbEncodingHextile);
	enc[1] = htonl(rfbEncodingCopyRect);
	enc[2] = htonl(rfbEncodingRRE);
	enc[3] = htonl(rfbEncodingRaw);
	enc[4] = htonl(rfbEncodingCoRRE);
	sock_write(fd, (byte*)encodingsmsgp, sz_enc);

	free(encodingsmsgp);
	return 1;
}

int request_vnc_refresh(struct VncSocket *fd)
{
	rfbFramebufferUpdateRequestMsg updreq;
	static int incremental = 0;

	if(fd->sock == INVALID_SOCKET) return 0;

	updreq.type = rfbFramebufferUpdateRequest;
	updreq.incremental = incremental;
	incremental=1;
	updreq.x = htons(0);
	updreq.y = htons(0);
	updreq.w = htons(fb_width);
	updreq.h = htons(fb_height);

	if (sock_write(fd, (byte*)&updreq, sz_rfbFramebufferUpdateRequestMsg)!=
	    sz_rfbFramebufferUpdateRequestMsg)
		return 0;
	else
		return 1;
}

int parse_vnc_msg(struct VncSocket *fd)
{
	int i;
	CARD32 i32;
	CARD8 x;
#ifdef DEBUG
	fprintf(fout, "  enter parse_vnc_msg\n"),fflush(fout);
#endif

	i = sock_read (fd, (byte*)&rfb_msg, sizeof(CARD8));
	if (i != sizeof(CARD8)) return ST_ERROR;

#ifdef DEBUG
	fprintf(fout, "    rfb_msg.type=%d\n",rfb_msg.type),fflush(fout);
#endif
	switch (rfb_msg.type) {
	case rfbFramebufferUpdate:
		i = sock_read(fd, ((byte*)&rfb_msg)+sizeof(CARD8), sz_rfbFramebufferUpdateMsg - sizeof(CARD8));
		rfb_msg.fu.nRects = ntohs(rfb_msg.fu.nRects);
		rfb_rect=0;
#ifdef DEBUG
		fprintf(fout, "msg: fbu: %d rectangles\n",rfb_msg.fu.nRects),fflush(fout);
#endif
		return ST_RECT;

	case rfbBell:
		i = sock_read(fd, ((byte*)&rfb_msg)+sizeof(CARD8), sz_rfbBellMsg - sizeof(CARD8));
		MessageBeep(0);
#ifdef DEBUG
		fprintf(fout, "msg: bell\n"),fflush(fout);
#endif
		return ST_IDLE;

	case rfbServerCutText:
		i = sock_read(fd, ((byte*)&rfb_msg)+sizeof(CARD8), sz_rfbServerCutTextMsg - sizeof(CARD8));
		i32 = rfb_msg.sct.length = ntohl(rfb_msg.sct.length);
		for (i=0; i<i32; i++)
			sock_read(fd, (byte*)&x, 1);
#ifdef DEBUG
		fprintf(fout, "msg: srv.cuttext: %ld\n",i32),fflush(fout);
#endif
		return ST_IDLE;

	default:
#ifdef DEBUG
		fprintf(fout, "msg: unknown: %d\n",rfb_msg.type),fflush(fout);
#endif
		return ST_IDLE; // ignore it
	}
}


int parse_vnc_rect(struct VncSocket *fd)
{
	int i;
#ifdef DEBUG
	fprintf(fout, "  enter parse_vnc_raw, rfb_rect=%d, rfb_msg.fu.nRects=%d\n",rfb_rect,rfb_msg.fu.nRects),fflush(fout);
#endif
	if (rfb_rect >= rfb_msg.fu.nRects) return ST_IDLE;

	i = sock_read(fd, (byte*)&rfb_uprect, sz_rfbFramebufferUpdateRectHeader);
	if (i != sz_rfbFramebufferUpdateRectHeader) return ST_ERROR;
	
	rfb_uprect.r.x = ntohs(rfb_uprect.r.x);
	rfb_uprect.r.y = ntohs(rfb_uprect.r.y);
	rfb_uprect.r.w = ntohs(rfb_uprect.r.w);
	rfb_uprect.r.h = ntohs(rfb_uprect.r.h);
	rfb_uprect.encoding = ntohl(rfb_uprect.encoding);
#ifdef DEBUG
	fprintf(fout, "  rect: #%d (%d,%d) %dx%d, enc:%ld\n",
		rfb_rect, rfb_uprect.r.x, rfb_uprect.r.y,
		rfb_uprect.r.w, rfb_uprect.r.h,	rfb_uprect.encoding),fflush(fout);
#endif
			
	if (    rfb_uprect.r.x >= fb_width || 
		rfb_uprect.r.x + rfb_uprect.r.w > fb_width ||
		rfb_uprect.r.y >= fb_height || 
		rfb_uprect.r.y + rfb_uprect.r.h > fb_height )
			return ST_ERROR;

	rfb_pos=0;

#ifdef DEBUG
	fprintf(fout, "  rfb_uprect.encoding=%d\n",rfb_uprect.encoding),fflush(fout);
#endif
	switch(rfb_uprect.encoding) {
	case rfbEncodingRaw:
		rfb_total=(CARD32)rfb_uprect.r.w * (CARD32)rfb_uprect.r.h;
		return ST_RAW;

	case rfbEncodingCopyRect:
		return ST_COPY;

	case rfbEncodingRRE:
	case rfbEncodingCoRRE:
		i = sock_read(fd, (byte*)&rfb_rrehead,sz_rfbRREHeader);
		if (i != sz_rfbRREHeader) return ST_ERROR;

		rfb_total = rfb_rrehead.nSubrects = ntohl(rfb_rrehead.nSubrects);

#ifdef DEBUG
		fprintf(fout, "    enc_%s: %ld subrectangles\n", rfb_uprect.encoding==rfbEncodingCoRRE ? "crre" : "rre", rfb_total),fflush(fout);
#endif
				
		rfb_pos=-1;
		if (rfb_uprect.encoding==rfbEncodingCoRRE)
			return ST_CRRE;
		return ST_RRE;
	case rfbEncodingHextile:
		return ST_HEXTILE;
	}
	return ST_ERROR; // unknown encoding scheme
}

int parse_vnc_raw(struct VncSocket *fd, int *x, int *y, int *w, int *h, unsigned char* buf)
{
	int i;
#ifdef DEBUG
	fprintf(fout, "  enter parse_vnc_raw, rfb_total=%d, rfb_pos=%d\n",rfb_total,rfb_pos),fflush(fout);
#endif

	i = sock_read(fd, buf, rfb_total-rfb_pos);
#ifdef DEBUG
	fprintf(fout, "    enc_raw: sock_read returns %d\n", i),fflush(fout);
#endif
	//if (i != *s) return ST_ERROR;

#ifdef DEBUG
	fprintf(fout, "    enc_raw: read %d, %ld of %ld bytes\n",rfb_total-rfb_pos,rfb_pos,rfb_total),fflush(fout);
#endif

	if (rfb_pos==0) {
		*x = rfb_uprect.r.x;
		*y = rfb_uprect.r.y;
		*w = rfb_uprect.r.w;
		*h = rfb_uprect.r.h;
	}

	rfb_pos+=i;
	if (rfb_pos>=rfb_total) {
		rfb_rect++;
		return ST_RECT;
	} else 
		return ST_RAW;
}

int parse_vnc_copy(struct VncSocket *fd, int *x, int *y, int *w, int *h,
			int *srcx, int *srcy)
{
	int i;
	rfbCopyRect copyrect;
#ifdef DEBUG
	fprintf(fout, "  enter parse_vnc_copy\n"),fflush(fout);
#endif

	i = sock_read(fd, (byte*)&copyrect, sz_rfbCopyRect);
#ifdef DEBUG
	fprintf(fout, " parse_vnc_copy: sock_read returns %d\n",1),fflush(fout);
#endif
	if (i != sz_rfbCopyRect) return ST_ERROR;

	copyrect.srcX = ntohs(copyrect.srcX);
	copyrect.srcY = ntohs(copyrect.srcY);
	*x = rfb_uprect.r.x; *y = rfb_uprect.r.y;
	*w = rfb_uprect.r.w; *h = rfb_uprect.r.h;
	*srcx = copyrect.srcX;
	*srcy = copyrect.srcY;

#ifdef DEBUG
	fprintf(fout, "    enc_copytrct: from (%d,%d)\n",*srcx,*srcy),fflush(fout);
#endif

	rfb_rect++;
	return ST_RECT;
}

/* encodings below needs to call drawbar and video_blk directly */
extern void video_blk_mem(int x, int y, int w, int h, char* buf_in);
extern void video_blk_upd(int x, int y, int w, int h);
extern void drawbar(int x, int y, int w, int h, char color);

int parse_vnc_rre(struct VncSocket *fd, int *x, int *y, int *w, int *h,
			unsigned char *buf)
{
	int i;
	rfbRectangle subRect;
#ifdef DEBUG
	fprintf(fout, "  enter parse_vnc_rre, rfb_total=%d, rfb_pos=%d\n",rfb_total,rfb_pos),fflush(fout);
#endif

	/* read background color */
	i = sock_read(fd, (byte*)buf,sizeof(CARD8));
	if (i != sizeof(CARD8)) return ST_ERROR;
	/* draw background color */
	drawbar(rfb_uprect.r.x, rfb_uprect.r.y, rfb_uprect.r.w, rfb_uprect.r.h, *buf);

	/* 4. Process all subrectangles */
	for (rfb_pos = 0; rfb_pos < rfb_total; rfb_pos++) {
		unsigned char sub_color;

		/* Read the color */
		if (sock_read(fd, (char*)&sub_color, sizeof(CARD8)) < 0) return ST_ERROR;

		/* Read the geometry (4x 16-bit integers = 8 bytes) */
		i = sock_read(fd, (byte*)&subRect,sz_rfbRectangle);
		if (i != sz_rfbRectangle) return ST_ERROR;

		/* Convert Endianness */
		subRect.x = ntohs(subRect.x);
		subRect.y = ntohs(subRect.y);
		subRect.w = ntohs(subRect.w);
		subRect.h = ntohs(subRect.h);

        /* * Draw the subrectangle.
         * CRITICAL: sx and sy are relative to rect_x and rect_y! 
         */
        drawbar(rfb_uprect.r.x + subRect.x, rfb_uprect.r.y + subRect.y, subRect.w, subRect.h, sub_color);
    }

    /* tell surface to update */
	*x = rfb_uprect.r.x;
	*y = rfb_uprect.r.y;
	*w = rfb_uprect.r.w;
	*h = rfb_uprect.r.h;
    video_blk_upd(*x, *y, *w, *h);

	if (rfb_pos>=rfb_total) {
		rfb_rect++;
		return ST_RECT;
	} else
		return ST_RRE;
}

int parse_vnc_crre(struct VncSocket *fd, int *x, int *y, int *w, int *h,
			unsigned char *buf)
{
	int i;
	rfbCoRRERectangle coRect;
#ifdef DEBUG
	fprintf(fout, "  enter parse_vnc_crre, rfb_total=%d, rfb_pos=%d\n",rfb_total,rfb_pos),fflush(fout);
#endif

	/* read background color */
	i = sock_read(fd, (byte*)buf,sizeof(CARD8));
	if (i != sizeof(CARD8)) return ST_ERROR;
	/* draw background color */
	drawbar(rfb_uprect.r.x, rfb_uprect.r.y, rfb_uprect.r.w, rfb_uprect.r.h, *buf);

	/* 4. Process all subrectangles */
	for (rfb_pos = 0; rfb_pos < rfb_total; rfb_pos++) {
		unsigned char sub_color;

		/* Read the color */
		if (sock_read(fd, (char*)&sub_color, sizeof(CARD8)) < 0) return ST_ERROR;

		/* Read the geometry (4x 8-bit integers = 4 bytes) */
		i = sock_read(fd, (byte*)&coRect,sz_rfbCoRRERectangle);
		if (i != sz_rfbCoRRERectangle) return ST_ERROR;

        /* * Draw the subrectangle.
         * CRITICAL: sx and sy are relative to rect_x and rect_y! 
         */
        drawbar(rfb_uprect.r.x + coRect.x, rfb_uprect.r.y + coRect.y, coRect.w, coRect.h, sub_color);
    }

    /* tell surface to update */
	*x = rfb_uprect.r.x;
	*y = rfb_uprect.r.y;
	*w = rfb_uprect.r.w;
	*h = rfb_uprect.r.h;
    video_blk_upd(*x, *y, *w, *h);

	if (rfb_pos>=rfb_total) {
		rfb_rect++;
		return ST_RECT;
	} else
		return ST_CRRE;
}

/* Hextile Bitmask Constants */
#define HEXTILE_RAW                0x01
#define HEXTILE_BG_SPECIFIED       0x02
#define HEXTILE_FG_SPECIFIED       0x04
#define HEXTILE_ANY_SUBRECTS       0x08
#define HEXTILE_SUBRECTS_COLORED   0x10

int parse_vnc_hextile(struct VncSocket *fd, int *x, int *y, int *w, int *h, unsigned char* buf)
{
	int tx, ty, tw, th;
	int o;
	CARD8 bg_color = 0;
	CARD8 fg_color = 0;

	/* Buffer to hold one fully reconstructed tile (max 16x16 = 256 bytes) */
	CARD8 tile_buf[256];
#ifdef DEBUG
	fprintf(fout, "  enter parse_vnc_hextile, rfb_total=%d, rfb_pos=%d\n",rfb_total,rfb_pos),fflush(fout);
#endif

	*x = rfb_uprect.r.x; *y = rfb_uprect.r.y;
	*w = rfb_uprect.r.w; *h = rfb_uprect.r.h;

	/* Process the rectangle in 16x16 chunks, left-to-right, top-to-bottom */
    for (ty = *y; ty < *y + *h; ty += 16) {
        for (tx = *x; tx < *x + *w; tx += 16) {

            CARD8 subencoding;
            int i, r;

            /* Calculate actual tile dimensions (edges might be smaller than 16x16) */
            tw = 16;
            th = 16;
            if (tx + 16 > *x + *w) tw = *x + *w - tx;
            if (ty + 16 > *y + *h) th = *y + *h - ty;

            /* 1. Read the subencoding flag for this specific tile */
            if (o = sock_read(fd, (char*)&subencoding, sizeof(CARD8)) < 0) return ST_ERROR;
            rfb_pos+=o;

            /* 2. Is it just raw uncompressed pixels? */
            if (subencoding & HEXTILE_RAW) {
                if (o = sock_read(fd, tile_buf, tw * th) < 0) return ST_ERROR;
                rfb_pos+=o;
                video_blk_mem(tx, ty, tw, th, tile_buf);
                continue; /* Move to the next tile */
            }

            /* 3. Update stateful background/foreground colors if provided */
            if (subencoding & HEXTILE_BG_SPECIFIED) {
                if (o = sock_read(fd, (char*)&bg_color, sizeof(CARD8)) < 0) return ST_ERROR;
                rfb_pos+=o;
            }
            if (subencoding & HEXTILE_FG_SPECIFIED) {
                if (o = sock_read(fd, (char*)&fg_color, sizeof(CARD8)) < 0) return ST_ERROR;
                rfb_pos+=o;
            }

            /* 4. Fill the entire tile buffer with the background color */
            memset(tile_buf, bg_color, tw * th);

            /* 5. Draw any colored sub-rectangles on top of the background */
            if (subencoding & HEXTILE_ANY_SUBRECTS) {
                CARD8 num_subrects;
                if (o = sock_read(fd, (char*)&num_subrects, sizeof(CARD8)) < 0) return ST_ERROR;
                rfb_pos+=o;

                for (i = 0; i < num_subrects; i++) {
                    CARD8 sub_color = fg_color;
                    CARD8 xy, wh;
                    int sx, sy, sw, sh;

                    /* If SUBRECTS_COLORED is set, this subrect has a unique color */
                    if (subencoding & HEXTILE_SUBRECTS_COLORED) {
                        if (o = sock_read(fd, (char*)&sub_color, sizeof(CARD8)) < 0) return ST_ERROR;
                        rfb_pos+=o;
                    }

                    /* Read geometry bytes */
                    if (o = sock_read(fd, (char*)&xy, sizeof(CARD8)) < 0) return ST_ERROR;
                    rfb_pos+=o;
                    if (o = sock_read(fd, (char*)&wh, sizeof(CARD8)) < 0) return ST_ERROR;
                    rfb_pos+=o;

                    /* Extract X/Y (high/low nibbles) and W/H (high/low nibbles + 1) */
                    sx = (xy >> 4);
                    sy = (xy & 0x0F);
                    sw = (wh >> 4) + 1;
                    sh = (wh & 0x0F) + 1;

                    /* Draw the subrect directly into our 16x16 tile buffer */
                    for (r = 0; r < sh; r++) {
                        memset(&tile_buf[(sy + r) * tw + sx], sub_color, sw);
                    }
                }
            }

            /* 6. Push the fully assembled tile to our Win32s graphics pipeline */
            video_blk_mem(tx, ty, tw, th, tile_buf);
        }
    }

    /* tell surface to update */
    video_blk_upd(*x, *y, *w, *h);

	if (rfb_pos>=rfb_total) {
		rfb_rect++;
		return ST_RECT;
	} else 
		return ST_HEXTILE;
}

int send_vnc_key(struct VncSocket *fd, int kbd)
{
	rfbKeyEventMsg ke = { rfbKeyEvent, 0, 0, 0};
	CARD16 k = 0;

	switch(kbd) {
		case VK_BACK  : k = 0xFF08; break; // bksp
		case VK_TAB   : k = 0xFF09; break; // tab
		case VK_ESCAPE: k = 0xFF1b; break; // esc
		case VK_LEFT  : k = 0xFF51; break; // left
		case VK_UP    : k = 0xFF52; break; // up
		case VK_RIGHT : k = 0xFF53; break; // right
		case VK_DOWN  : k = 0xFF54; break; // down
		case VK_INSERT: k = 0xFF63; break; // ins
		case VK_DELETE: k = 0xFFFF; break; // del
		case VK_HOME  : k = 0xFF50; break; // home
		case VK_END   : k = 0xFF57; break; // end
		case VK_PRIOR : k = 0xFF55; break; // pg-up
		case VK_NEXT  : k = 0xFF56; break; // pg-down
		default:
			if (kbd >= VK_F1 && kbd <=VK_F12) { // F1..F12
				k = 0xFFBE + (kbd-VK_F1);
			} else if (kbd < 0x100) k = kbd;
				else k=0;
			break;
	}

	if (k>0) {
		ke.key = htonl(k);
		ke.down = 1;
		sock_write(fd, (byte*)&ke, sz_rfbKeyEventMsg);
		ke.down = 0;
		sock_write(fd, (byte*)&ke, sz_rfbKeyEventMsg);
	}
	
	return 1;
}

int send_vnc_shift(struct VncSocket *fd, int vnc_key, int down)
{
	rfbKeyEventMsg ke = { rfbKeyEvent, 0, 0, 0};

	ke.down = down;
	ke.key = vnc_key;
	sock_write(fd, (byte*)&ke, sz_rfbKeyEventMsg);

	return 1;
}

int send_vnc_pointer(struct VncSocket *fd, int x, int y, int b)
{
	rfbPointerEventMsg pe = { rfbPointerEvent, 0, 0, 0};

	pe.x = htons(x);
	pe.y = htons(y);
	pe.buttonMask = 0;
	if (b&1) pe.buttonMask |= rfbButton1Mask;
	if (b&2) pe.buttonMask |= rfbButton2Mask;
	if (b&4) pe.buttonMask |= rfbButton3Mask;
	sock_write(fd, (byte*)&pe, sz_rfbPointerEventMsg);

	return 1;
}
