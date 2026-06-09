#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>
#include <stdlib.h>
#include <stdio.h>

/* --- VNC Protocol States (from original DOS client) --- */
#include "vnc.h"

/* --- Default configuration --- */
#define DEF_PORT  5900
#define DEF_HOST  "192.168.1.50"
#define WM_VNC_SOCKET_EVENT (WM_USER + 1)

/* --- External VNC Library Functions (Assumed from your repo) --- */
#include "vnc.h"

extern char server_name[256];
/* VNC.C screen size from VNC server */
extern unsigned short fb_width;
extern unsigned short fb_height;

/* Custom struct to hold the header AND the 256 color palette table */
typedef struct {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[256];
} BITMAPINFO_8BPP;


/* --- Global Variables --- */
HINSTANCE        g_hInst;
HWND             hWndMain           = NULL;
struct VncSocket g_VncSock;
int              g_VncState         = ST_IDLE;
char*            g_BufIn            = NULL;    /* Replaces DOS input buffer buffer */

FILE* fout = (FILE*)stderr;

/* GDI Screen Buffering */
BYTE* g_pPixels          = NULL;
BITMAPINFO_8BPP g_Bmi;             /* Persistent layout and palette configuration */
int              g_ScreenWidth      = 800;   /* Will update dynamically if known */
int              g_ScreenHeight     = 600;

/* --- Win32/GDI Implementation of Missing video.c Functions --- */
int video_init(int width, int height) {
    int i;

    g_ScreenWidth = width;
    g_ScreenHeight = height;

    /* 1. Allocate the VNC network stream buffer */
    g_BufIn = (char*)malloc(width * height);
    if (!g_BufIn) return -1;

    /* 2. Allocate our local persistent screen surface memory */
    g_pPixels = (char*)malloc(width * height);
    if (!g_pPixels) {
        free(g_BufIn);
        return -1;
    }
    memset(g_pPixels, 0, width * height);

    /* 3. Setup the DIB Header (CRITICAL: biHeight MUST be positive for Win32s) */
    memset(&g_Bmi, 0, sizeof(BITMAPINFO_8BPP));
    g_Bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    g_Bmi.bmiHeader.biWidth       = width;
    g_Bmi.bmiHeader.biHeight      = height; /* Positive = Bottom-Up layout */
    g_Bmi.bmiHeader.biPlanes      = 1;
    g_Bmi.bmiHeader.biBitCount    = 8;       
    g_Bmi.bmiHeader.biCompression = BI_RGB;

    /* 4. Generate the BGR233 Palette */
    for (i = 0; i < 256; i++) {
        int r = (i & 0x07);
        int g = ((i >> 3) & 0x07);
        int b = ((i >> 6) & 0x03);

        g_Bmi.bmiColors[i].rgbRed   = (BYTE)(r * 255 / 7);
        g_Bmi.bmiColors[i].rgbGreen = (BYTE)(g * 255 / 7);
        g_Bmi.bmiColors[i].rgbBlue  = (BYTE)(b * 255 / 3);
        g_Bmi.bmiColors[i].rgbReserved = 0;
    }

    return 0;
}

void video_blk(int x, int y, int w, int h, long p, int s, char* buf_in) {
    int row, src_y_start;
    HDC hdc;

    /* 1. Invert rows while copying from Top-Down network stream to Bottom-Up memory */
    for (row = 0; row < h; row++) {
        int current_network_y = y + row;
        
        /* Convert standard top-down Y to Win32s bottom-up memory row index */
        int win32s_dib_row = g_ScreenHeight - 1 - current_network_y;
        
        /* Map row directly into our persistent surface */
        memcpy(&g_pPixels[win32s_dib_row * g_ScreenWidth + x], &buf_in[row * w], w);
    }

    /* 2. Blit the dirty rectangle straight to the window DC immediately */
    hdc = GetDC(hWndMain);
    
    /* Calculate where the top-left of our visual rect maps to the source DIB origin */
    src_y_start = g_ScreenHeight - y - h;

    SetDIBitsToDevice(
        hdc, 
        x, y,             /* Destination top-left coordinates on screen window */
        w, h,             /* Width and height of the updated dirty box */
        x, src_y_start,   /* Source coordinates relative to the bottom-left of DIB */
        0,                /* First scan line to start reading in lpBits */
        g_ScreenHeight,   /* Total number of scan lines available in lpBits array */
        g_pPixels,        /* Pointer to our raw memory array */
        (BITMAPINFO*)&g_Bmi, 
        DIB_RGB_COLORS
    );

    ReleaseDC(hWndMain, hdc);
}

void video_blt(int dest_x, int dest_y, int w, int h, int src_x, int src_y) {
    int row;
    RECT rect;
    if (dest_y < src_y) {
        for (row = 0; row < h; row++) {
            memmove(&g_pPixels[(dest_y + row) * g_ScreenWidth + dest_x],
                    &g_pPixels[(src_y + row) * g_ScreenWidth + src_x], w);
        }
    } else {
        for (row = h - 1; row >= 0; row--) {
            memmove(&g_pPixels[(dest_y + row) * g_ScreenWidth + dest_x],
                    &g_pPixels[(src_y + row) * g_ScreenWidth + src_x], w);
        }
    }
    SetRect(&rect, dest_x, dest_y, dest_x + w, dest_y + h);
    InvalidateRect(hWndMain, &rect, FALSE);
}

void drawbar(int x, int y, int w, int h, char color) {
    int row;
    RECT rect;
    for (row = 0; row < h; row++) {
        memset(&g_pPixels[(y + row) * g_ScreenWidth + x], color, w);
    }
    SetRect(&rect, x, y, x + w, y + h);
    InvalidateRect(hWndMain, &rect, FALSE);
}

/* --- Main Windows Message Loop Window Handler --- */
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    /* Keep parsing state variables safe across windows messaging cycles */
    static int x, y, w, h, s;
    static long p;
    static int srcx, srcy;

    switch (message) {

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            /* Guard check: Only paint if our video buffer has been initialized */
            if (g_pPixels) {
                /* Draw the entire frame buffer directly from raw RAM to the window */
                SetDIBitsToDevice(
                    hdc,
                    0, 0,                           /* Destination top-left inside the window */
                    g_ScreenWidth, g_ScreenHeight,   /* Width and height of the window area to paint */
                    0, 0,                           /* Source X, Y (Start at the very beginning of our DIB memory) */
                    0,                              /* First scan line to start reading */
                    g_ScreenHeight,                 /* Total number of scan lines in the array */
                    g_pPixels,                      /* Pointer to our raw memory frame buffer */
                    (BITMAPINFO*)&g_Bmi,            /* Our global header containing the VNC palette */
                    DIB_RGB_COLORS
                );
            }
            EndPaint(hwnd, &ps);
            break;
        }

        /* --- Input Mappings Replacing DOS kbhit / Getch / Mouse Emulation --- */
        case WM_KEYDOWN: {
            int key = (int)wParam;
            /* Note: Basic virtual key translation. Expand to match specific RFB keysyms if needed */
            send_vnc_key(&g_VncSock, key);
            request_vnc_refresh(&g_VncSock);
            break;
        }

        case WM_MOUSEMOVE: {
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);
            int buttons = 0;
            if (wParam & MK_LBUTTON) buttons |= 1;
            if (wParam & MK_RBUTTON) buttons |= 4;

            send_vnc_pointer(&g_VncSock, mx, my, buttons);
            break;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP: {
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);
            int buttons = 0;
            if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) buttons |= 1;
            if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) buttons |= 4;

            send_vnc_pointer(&g_VncSock, mx, my, buttons);
            request_vnc_refresh(&g_VncSock);
            break;
        }

        case WM_DESTROY:
            sock_close(&g_VncSock);
            
            if (g_BufIn) {
                free(g_BufIn);
                g_BufIn = NULL;
            }
            if (g_pPixels) {
                free(g_pPixels);
                g_pPixels = NULL;
            }
            
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

/* --- WinMain Application Entry Point --- */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc;
    MSG msg;
    BOOL bRunning = TRUE;
    int x, y, w, h, s, srcx, srcy;
    long p;

    fout = fopen("vncsclnt.log", "w");

    /* ... Standard Window Registration & Creation Here ... */
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = "Win95VNCClass";

    if (!RegisterClass(&wc)) return 0;

    hWndMain = CreateWindow("Win95VNCClass", "Windows 95 Single-Threaded VNC",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            650, 510, NULL, NULL, hInstance, NULL);

    if (!hWndMain) return 0;
    ShowWindow(hWndMain, nCmdShow);
    UpdateWindow(hWndMain);

    /* 1. Run the blocking VNC Handshake Sequentially, exactly like DOS main() */
    sock_init();
    if (!socket_connect(&g_VncSock, DEF_HOST, DEF_PORT)) return 0;
    if (!auth_vnc(&g_VncSock, "password")) return 0;
    if (!init_vnc_client(&g_VncSock)) return 0;
	if (!setup_vnc_pixelformat(&g_VncSock)) return 0;
	if (!setup_vnc_encodings(&g_VncSock)) return 0;

    video_init(fb_width, fb_height);
    request_vnc_refresh(&g_VncSock);

    /* 2. Cooperative PeekMessage Game-Style Loop Setup */
    while (bRunning) {
        /* Process all pending Windows messages first to keep the Win95 UI responsive */
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                bRunning = FALSE;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!bRunning) break;

        /* --- THE ORIGINAL DOS MAIN LOOP LOGIC RUNS HERE --- */
        if (tcp_tick(&g_VncSock)) {
            if (sock_dataready(&g_VncSock)) {
	fprintf(fout, "tick, sock_dataready, g_VncState=%d\n",g_VncState),fflush(fout);
                switch(g_VncState) {
                    case ST_IDLE:
	fprintf(fout, " ST_IDLE\n"),fflush(fout);
                        g_VncState = parse_vnc_msg(&g_VncSock);
                        break;
                    case ST_RECT:
	fprintf(fout, " ST_RECT\n"),fflush(fout);
                        g_VncState = parse_vnc_rect(&g_VncSock);
                        break;
                    case ST_RAW:
	fprintf(fout, " ST_RAW\n"),fflush(fout);
                        g_VncState = parse_vnc_raw(&g_VncSock, &x, &y, &w, &h, &p, &s, g_BufIn);
                        video_blk(x, y, w, h, p, s, g_BufIn);
                        break;
                    case ST_COPY:
	fprintf(fout, " ST_COPY\n"),fflush(fout);
                        g_VncState = parse_vnc_copy(&g_VncSock, &x, &y, &w, &h, &srcx, &srcy);
                        video_blt(x, y, w, h, srcx, srcy);
                        break;
                    case ST_RRE:  
	fprintf(fout, " ST_RRE\n"),fflush(fout);
                        g_VncState = parse_vnc_rre(&g_VncSock, &x, &y, &w, &h, g_BufIn);
                        drawbar(x, y, w, h, *g_BufIn);
                        break;
                    case ST_CRRE:  
	fprintf(fout, " ST_CRRE\n"),fflush(fout);
                        g_VncState = parse_vnc_crre(&g_VncSock, &x, &y, &w, &h, g_BufIn);
                        drawbar(x, y, w, h, *g_BufIn);
                        break;
                }
	fprintf(fout, " new g_VncState=%d\n",g_VncState),fflush(fout);
            } else {
                /* Periodic refresh check fallback replacing countdown() */
                static DWORD lastRefresh = 0;
                DWORD now = GetTickCount(); /* Win32 millisecond counter */
                if (!lastRefresh) lastRefresh = now;
                if (now - lastRefresh > 220) {
	fprintf(fout, "tick, !sock_dataready, timer=%d\n",now - lastRefresh),fflush(fout);
                    request_vnc_refresh(&g_VncSock);
                    lastRefresh = now;
                }
            }

            if (g_VncState == ST_ERROR) bRunning = FALSE;
        } else {
	fprintf(fout, "no tick\n"),fflush(fout);
            bRunning = FALSE; /* Network socket disconnected */
        }
    }

    sock_close(&g_VncSock);
    WSACleanup();
    return msg.wParam;
}
