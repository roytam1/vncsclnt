#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <sys/types.h>
#include <string.h>

#include <tcp.h>

#include "vncdos.h"
#include "vnc.h"
#include "video.h"

tcp_Socket vncsock;
unsigned char buf_in[BUF_IN_MAX];

int main(int argc, char * argv[])
{
	int	state,emu_mouse_on;
	int	x,y,w,h,k,s,srcx,srcy;
	int	mx,my,mb,mb2,speed;
	long	p;
	char *passwd;
	int port = DEF_PORT;
	char *host = DEF_HOST;

	if (argc>=2) host = argv[1];
	if (argc>=3) port = atoi(argv[2]);

//	dbug_init();
	sock_init();

	if (!socket_connect(&vncsock, host, port)) return 1;

	passwd=getpass("VncAuth: password:");

	if (!auth_vnc(&vncsock,passwd)) return 1;
	if (!init_vnc_client(&vncsock)) return 1;
	if (!setup_vnc_pixelformat(&vncsock)) return 1;
	if (!setup_vnc_encodings(&vncsock)) return 1;

	if (video_init() == -1) {
		perror("video_init():");
		sock_close(&vncsock);
		return 1;
	}

	if (!request_vnc_refresh(&vncsock)) {
		fprintf(stderr,"firs screen load error\n");
		return 1;
	}

	countdown(1,CNT_AGRESS);
	state = ST_IDLE;
	emu_mouse_on = 0;
	mx = DOS_XMAX/2;
	my = DOS_YMAX/2;
	mb = mb2 = 0;
	speed = 4;

	dbug_printf("while(1)\n");
	
	while(tcp_tick(&vncsock)) {
		if(sock_dataready(&vncsock))
			switch(state) {
			case ST_IDLE:
				state=parse_vnc_msg(&vncsock);
				break;

			case ST_RECT: state=parse_vnc_rect(&vncsock);
				break;

			case ST_RAW: state=parse_vnc_raw(&vncsock,&x,&y,&w,&h,&p,&s,buf_in);
				video_blk(x,y,w,h,p,s,buf_in);
				break;

			case ST_COPY:
				state=parse_vnc_copy(&vncsock,&x,&y,&w,&h,&srcx,&srcy);
				video_blt(x,y,w,h,srcx,srcy);
				break;

			case ST_RRE: 
				state=parse_vnc_rre(&vncsock,&x,&y,&w,&h,buf_in);
				goto draw;

			case ST_CRRE: 
				state=parse_vnc_crre(&vncsock,&x,&y,&w,&h,buf_in);
			draw:
				drawbar(x,y,w,h,video_pixcolor(buf_in));
				break;
			}
		else
			if (countdown(CNTN_220MS,CNT_NORMAL))
				request_vnc_refresh(&vncsock);
		if (kbhit()) {
			if ((k=getch()) == 0) k = 256 + getch();
			if (k == DOS_ALT_ESC)   break;
			if (emu_mouse_on)
				switch (parsed_moused_key(k,&mx,&my,&speed,&mb,&mb2)) {
				case MSA_2:	
					send_vnc_pointer(&vncsock,mx,my,mb2);
				case MSA_1:
					send_vnc_pointer(&vncsock,mx,my,mb);
					countdown(CNTN_110MS,CNT_AGRESS);
					break;
				case MSA_LEAVE:
					emu_mouse_on=0;
				case MSA_NOOP:
					break;
				}
			else if (k == DOS_ALT_TILDE)
				emu_mouse_on=!emu_mouse_on;
			     else {
			     	send_vnc_key(&vncsock,k);
				countdown(CNTN_110MS,CNT_AGRESS);
			     }
		}
		if (state == ST_ERROR) break;
	}

	sock_close(&vncsock);

	return 0;
}

int socket_connect(tcp_Socket *fd, char *host, int port)
{
	longword addr;
	int status,i,s;

	addr=resolve(host);
	if (addr==0L) {
		fprintf(stderr, "Cannot resolve hostname\n");
		return 0;
	}

	if (!tcp_open(fd, 0, addr, port, NULL)) {
		fprintf(stderr,"Cannot create socket\n");
		return 0;
	}

	sock_wait_established(fd, sock_delay, NULL, &status);
	
sock_err:
	switch (status) {
		case 1:
			fprintf(stderr,"remote host closed connection\n");
			return 0;
		case -1:
			fprintf(stderr,"server timeout: %s\n",sockerr(fd));
			return 0;
	}
	return 1;
}

int parsed_moused_key(int k, int *x, int *y, int *speed, int *b1, int *b2)
{

	switch(k) {
	case 0x14b: // left
		*x -= *speed;
		if ( *x < 0) *x = 0;
		return MSA_1;

	case 0x148: // up
		*y -= *speed;
		if ( *y < 0) *y = 0;
		return MSA_1;

	case 0x14d: // right
		*x += *speed;
		if ( *x >= DOS_XMAX) *x = DOS_XMAX-1;
		return MSA_1;

	case 0x150: // down
		*y += *speed;
		if ( *y >= DOS_YMAX) *y = DOS_YMAX-1;
		return MSA_1;

	case '1':
		*b2=1; *b1=0;
		return MSA_2;
		
	case '2':
		*b2=2; *b1=0;
		return MSA_2;

	case '3':
		*b2=4; *b1=0;
		return MSA_2;

	case '!':
		*b1 ^= 1;
		return MSA_1; 

	case '@':
		*b1 ^= 2;
		return MSA_1; 

	case '#':
		*b1 ^= 4;
		return MSA_1;

	case '9': case '(': // slower
		(*speed)--; if (*speed <= 0) *speed=1;
		return MSA_NOOP;

	case '0': case ')': // faster
		(*speed)++; if (*speed > 16) *speed=16;
		return MSA_NOOP;

	default:
		return MSA_LEAVE;
	}
}

int	countdown(int h, int agressive)
{
	unsigned long far *x=(unsigned long far*)0x0000046c;
	static unsigned long d;

	if(*x > d) {
		if (h!=0) d = h + *x;
		return 1;
	} else {
		if(agressive && (d-*x) > (unsigned long)h) d = h + *x;
		return 0;
	}
}

