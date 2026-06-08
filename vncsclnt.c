#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>
#include <stdlib.h>
#include <stdio.h>

/* --- VNC Protocol States (from original DOS client) --- */
#define ST_IDLE   0
#define ST_RECT   1
#define ST_RAW    2
#define ST_COPY   3
#define ST_RRE    4
#define ST_CRRE   5
#define ST_ERROR  -1

/* --- Default configuration --- */
#define DEF_PORT  5900
#define DEF_HOST  "192.168.1.50"
#define WM_VNC_SOCKET_EVENT (WM_USER + 1)

/* --- External VNC Library Functions (Assumed from your repo) --- */
/* You may need to change 'struct VncSocket' to match your actual structure name */

#include "vnc.h"

/* --- Global Variables --- */
HINSTANCE        g_hInst;
HWND             hWndMain           = NULL;
struct VncSocket g_VncSock;
int              g_VncState         = ST_IDLE;
char             g_BufIn[65536];    /* Replaces DOS input buffer buffer */

/* GDI Screen Buffering */
HBITMAP          g_hDIB             = NULL;
BYTE* g_pPixels          = NULL;
int              g_ScreenWidth      = 800;   /* Will update dynamically if known */
int              g_ScreenHeight     = 600;

/* --- Win32/GDI Implementation of Missing video.c Functions --- */
int video_init(int width, int height) {
    HDC hdc;
    BITMAPINFO bmi;

    g_ScreenWidth = width;
    g_ScreenHeight = height;

    memset(&bmi, 0, sizeof(BITMAPINFO));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = width;
    bmi.bmiHeader.biHeight      = -height; /* Negative indicates Top-Down rendering */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 8;       /* 8-bit color mode mapped from DOS VGA */
    bmi.bmiHeader.biCompression = BI_RGB;

    hdc = GetDC(NULL);
    g_hDIB = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void**)&g_pPixels, NULL, 0);
    ReleaseDC(NULL, hdc);

    return (g_hDIB != NULL) ? 0 : -1;
}

void video_blk(int x, int y, int w, int h, long p, int s, char* buf_in) {
    int row;
    RECT rect;
    for (row = 0; row < h; row++) {
        memcpy(&g_pPixels[(y + row) * g_ScreenWidth + x], &buf_in[row * w], w);
    }
    SetRect(&rect, x, y, x + w, y + h);
    InvalidateRect(hWndMain, &rect, FALSE);
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

/* --- VNC Setup Sequence (Synchronous Handshake) --- */
BOOL StartVNCClient(HWND hwnd, char* host, int port, char* password) {
    sock_init();

    if (!socket_connect(&g_VncSock, host, port)) {
        MessageBox(hwnd, "Failed to connect to VNC server.", "Network Error", MB_OK | MB_ICONERROR);
        return FALSE;
    }

    if (!auth_vnc(&g_VncSock, password)) {
        MessageBox(hwnd, "VNC Authentication failed.", "Auth Error", MB_OK | MB_ICONERROR);
        sock_close(&g_VncSock);
        return FALSE;
    }

    if (!init_vnc_client(&g_VncSock)) return FALSE;
    if (!setup_vnc_pixelformat(&g_VncSock)) return FALSE;
    if (!setup_vnc_encodings(&g_VncSock)) return FALSE;

    /* Hardcoded resolution fallback for old architecture sizing */
    if (video_init(640, 480) == -1) {
        sock_close(&g_VncSock);
        return FALSE;
    }

    /* Request initial full screen load */
    if (!request_vnc_refresh(&g_VncSock)) {
        sock_close(&g_VncSock);
        return FALSE;
    }

    /* Handshake done! Turn socket into an asynchronous Windows Event driver */
    WSAAsyncSelect(g_VncSock.sock, hwnd, WM_VNC_SOCKET_EVENT, FD_READ | FD_CLOSE);
    
    /* Replaces old 220ms countdown logic loop with Windows Timer */
    SetTimer(hwnd, 1, 220, NULL);

    g_VncState = ST_IDLE;
    return TRUE;
}

/* --- Main Windows Message Loop Window Handler --- */
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    /* Keep parsing state variables safe across windows messaging cycles */
    static int x, y, w, h, s;
    static long p;
    static int srcx, srcy;

    switch (message) {
        case WM_CREATE:
            /* Automatically start network handshake on startup */
            /* In production, change arguments to parsed argv entries or input forms */
            StartVNCClient(hwnd, DEF_HOST, DEF_PORT, "secret");
            break;

        case WM_VNC_SOCKET_EVENT:
            if (WSAGETSELECTERROR(lParam)) {
                MessageBox(hwnd, "VNC Socket Error encountered.", "Error", MB_OK);
                DestroyWindow(hwnd);
                break;
            }

            if (WSAGETSELECTEVENT(lParam) == FD_READ) {
                /* Run the original DOS parsed loop structure context directly on network trigger */
                tcp_tick(&g_VncSock);
                
                switch(g_VncState) {
                    case ST_IDLE: 
                        g_VncState = parse_vnc_msg(&g_VncSock); 
                        break;
                    case ST_RECT: 
                        g_VncState = parse_vnc_rect(&g_VncSock); 
                        break;
                    case ST_RAW:  
                        g_VncState = parse_vnc_raw(&g_VncSock, &x, &y, &w, &h, &p, &s, g_BufIn);
                        video_blk(x, y, w, h, p, s, g_BufIn);
                        break;
                    case ST_COPY:
                        g_VncState = parse_vnc_copy(&g_VncSock, &x, &y, &w, &h, &srcx, &srcy);
                        video_blt(x, y, w, h, srcx, srcy);
                        break;
                    case ST_RRE:
                        g_VncState = parse_vnc_rre(&g_VncSock, &x, &y, &w, &h, g_BufIn);
                        drawbar(x, y, w, h, g_BufIn);
                        break;
                    case ST_CRRE:
                        g_VncState = parse_vnc_crre(&g_VncSock, &x, &y, &w, &h, g_BufIn);
                        drawbar(x, y, w, h, g_BufIn);
                        break;
                }

                if (g_VncState == ST_ERROR) {
                    MessageBox(hwnd, "RFB protocol parsing error.", "Error", MB_OK);
                    DestroyWindow(hwnd);
                }
            }
            else if (WSAGETSELECTEVENT(lParam) == FD_CLOSE) {
                MessageBox(hwnd, "Server disconnected connection.", "Notice", MB_OK);
                DestroyWindow(hwnd);
            }
            break;

        case WM_TIMER:
            if (wParam == 1) {
                request_vnc_refresh(&g_VncSock);
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (g_hDIB) {
                HDC hdcMem = CreateCompatibleDC(hdc);
                SelectObject(hdcMem, g_hDIB);
                BitBlt(hdc, 0, 0, g_ScreenWidth, g_ScreenHeight, hdcMem, 0, 0, SRCCOPY);
                DeleteDC(hdcMem);
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
            KillTimer(hwnd, 1);
            sock_close(&g_VncSock);
            if (g_hDIB) DeleteObject(g_hDIB);
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
    
    video_init(640, 480);
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
                switch(g_VncState) {
                    case ST_IDLE: g_VncState = parse_vnc_msg(&g_VncSock); break;
                    case ST_RECT: g_VncState = parse_vnc_rect(&g_VncSock); break;
                    case ST_RAW:  
                        g_VncState = parse_vnc_raw(&g_VncSock, &x, &y, &w, &h, &p, &s, g_BufIn);
                        video_blk(x, y, w, h, p, s, g_BufIn);
                        break;
                    case ST_COPY:
                        g_VncState = parse_vnc_copy(&g_VncSock, &x, &y, &w, &h, &srcx, &srcy);
                        video_blt(x, y, w, h, srcx, srcy);
                        break;
                    case ST_RRE:  
                        g_VncState = parse_vnc_rre(&g_VncSock, &x, &y, &w, &h, g_BufIn);
                        drawbar(x, y, w, h, g_BufIn);
                        break;
                }
            } else {
                /* Periodic refresh check fallback replacing countdown() */
                static DWORD lastRefresh = 0;
                DWORD now = GetTickCount(); /* Win32 millisecond counter */
                if (now - lastRefresh > 220) {
                    request_vnc_refresh(&g_VncSock);
                    lastRefresh = now;
                }
            }

            if (g_VncState == ST_ERROR) bRunning = FALSE;
        } else {
            bRunning = FALSE; /* Network socket disconnected */
        }
    }

    sock_close(&g_VncSock);
    WSACleanup();
    return msg.wParam;
}
