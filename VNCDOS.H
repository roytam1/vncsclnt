#ifndef _DOs2VNC_H
#define _DOs2VNC_H

#define DEF_PORT 5901
#define DEF_HOST "192.168.76.76"

#define MSA_NOOP  0  /* some internal processes */
#define MSA_1     1  /* single mouse action */
#define MSA_2     2  /* doubled mouse action */
#define MSA_LEAVE 3  /* found emulation left key */

#define CNT_NORMAL 0
#define CNT_AGRESS 1

#define CNTN_1SEC  18
#define CNTN_500MS 9
#define CNTN_330MS 6
#define CNTN_220MS 4
#define CNTN_165MS 3
#define CNTN_110MS 2
#define CNTN_55MS  1
#define CNTN_1TCK  1

extern int socket_connect(tcp_Socket *fd, char *host, int port);
extern int parsed_moused_key(int k, int *x, int *y, int *speed,
				int *b1, int *b2);
extern int countdown(int h, int agressive);

#endif
