#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>
#include <stdlib.h>
#include <stdio.h>

#include "resource.h"

/* --- VNC Protocol States (from original DOS client) --- */
#include "vnc.h"

/* --- Default configuration --- */
char g_VncIP[64]   = "192.168.1.100"; /* Provide default values */
int  g_VncPort     = 5900;
char g_VncPass[10] = "";

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

int g_ScrollX = 0; /* Current horizontal scroll position */
int g_ScrollY = 0; /* Current vertical scroll position */

#ifdef DEBUG
FILE* fout = (FILE*)stderr;
#endif

/* GDI Screen Buffering */
BYTE* g_pPixels          = NULL;
BITMAPINFO_8BPP g_Bmi;             /* Persistent layout and palette configuration */
HPALETTE g_hPalette = NULL;        /* Global GDI Palette handle */
int              g_ScreenWidth      = 800;   /* Will update dynamically if known */
int              g_ScreenHeight     = 600;

/* --- Win32/GDI Implementation of Missing video.c Functions --- */
int video_init(int width, int height) {
    int i;
    LOGPALETTE* pLogPal;

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

    pLogPal = (LOGPALETTE*)malloc(sizeof(LOGPALETTE) + (256 * sizeof(PALETTEENTRY)));
    if (!pLogPal) {
        free(g_pPixels);
        free(g_BufIn);
        return -1;
    }

    /* 3. Setup the DIB Header (CRITICAL: biHeight MUST be positive for Win32s) */
    memset(&g_Bmi, 0, sizeof(BITMAPINFO_8BPP));
    g_Bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    g_Bmi.bmiHeader.biWidth       = width;
    g_Bmi.bmiHeader.biHeight      = height; /* Positive = Bottom-Up layout */
    g_Bmi.bmiHeader.biPlanes      = 1;
    g_Bmi.bmiHeader.biBitCount    = 8;       
    g_Bmi.bmiHeader.biCompression = BI_RGB;

    /* 4. Generate the BGR332 Palette */
    for (i = 0; i < 256; i++) {
        int r = (i & 0x03);
        int g = ((i >> 2) & 0x07);
        int b = ((i >> 5) & 0x07);

        pLogPal->palPalEntry[i].peRed   = g_Bmi.bmiColors[i].rgbRed   = (BYTE)(r * 255 / 3);
        pLogPal->palPalEntry[i].peGreen = g_Bmi.bmiColors[i].rgbGreen = (BYTE)(g * 255 / 7);
        pLogPal->palPalEntry[i].peBlue  = g_Bmi.bmiColors[i].rgbBlue  = (BYTE)(b * 255 / 7);
        pLogPal->palPalEntry[i].peFlags = g_Bmi.bmiColors[i].rgbReserved = 0;
    }

    g_hPalette = CreatePalette(pLogPal);
    free(pLogPal);

    return 0;
}

void video_blk(int x, int y, int w, int h, long p, int s, char* buf_in) {
    int row;
    RECT r;

    /* 1. Update our persistent memory buffer (Bottom-Up layout transformation) */
    for (row = 0; row < h; row++) {
        int current_network_y = y + row;
        int win32s_dib_row = g_ScreenHeight - 1 - current_network_y;
        memcpy(&g_pPixels[win32s_dib_row * g_ScreenWidth + x], &buf_in[row * w], w);
    }

    /* 2. Convert raw VNC screen coordinates to current scrolled Client space */
    r.left   = x - g_ScrollX;
    r.top    = y - g_ScrollY;
    r.right  = r.left + w;
    r.bottom = r.top + h;

    /* 3. Tell Windows this exact box needs a repaint pass */
    InvalidateRect(hWndMain, &r, FALSE);
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

        case WM_QUERYNEWPALETTE:
            if (g_hPalette) {
                HDC hdc = GetDC(hwnd);
                HPALETTE hOldPal = SelectPalette(hdc, g_hPalette, FALSE);

                /* RealizePalette returns the number of colors changed in system mapping */
                UINT realized = RealizePalette(hdc);

                SelectPalette(hdc, hOldPal, FALSE);
                ReleaseDC(hwnd, hdc);

                if (realized > 0) {
                    /* If colors shifted, redraw the entire client window immediately */
                    InvalidateRect(hwnd, NULL, FALSE);
                    return TRUE;
                }
            }
            return FALSE;

        case WM_PALETTECHANGED:
            /* Only respond if another application caused the palette shift */
            if (g_hPalette && (HWND)wParam != hwnd) {
                HDC hdc = GetDC(hwnd);

                /* Select as a background palette (TRUE) so we don't fight foreground apps */
                HPALETTE hOldPal = SelectPalette(hdc, g_hPalette, TRUE);
                UINT realized = RealizePalette(hdc);

                SelectPalette(hdc, hOldPal, FALSE);
                ReleaseDC(hwnd, hdc);

                if (realized > 0) {
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            /* Guard check: Only paint if our video buffer has been initialized */
            if (g_pPixels) {
                HPALETTE hOldPal = NULL;

                /* Get coordinates of the dirty window window area that needs refreshing */
                int dest_x = ps.rcPaint.left;
                int dest_y = ps.rcPaint.top;
                int dest_w = ps.rcPaint.right - ps.rcPaint.left;
                int dest_h = ps.rcPaint.bottom - ps.rcPaint.top;

                /* If running on a 256-color display, force our palette choices */
                if (g_hPalette) {
                    hOldPal = SelectPalette(hdc, g_hPalette, FALSE);
                    RealizePalette(hdc);
                }

                if (dest_w > 0 && dest_h > 0) {
                    /* Map client window coordinates back into top-down VNC image coordinates */
                    int vnc_x = dest_x + g_ScrollX;
                    int vnc_y_bottom = dest_y + dest_h + g_ScrollY;

                    /* Translate top-down VNC Y coordinate into our Bottom-Up DIB row array */
                    int src_y = g_ScreenHeight - vnc_y_bottom;

                    SetDIBitsToDevice(
                        hdc,
                        dest_x, dest_y,       /* Destination inside your window */
                        dest_w, dest_h,       /* Update dimensions */
                        vnc_x, src_y,         /* Source position inside g_pPixels */
                        0,
                        g_ScreenHeight,       /* Total available source scanlines */
                        g_pPixels,
                        (BITMAPINFO*)&g_Bmi,
                        DIB_RGB_COLORS
                    );
                }

                /* Restore old system palette state */
                if (g_hPalette) {
                    SelectPalette(hdc, hOldPal, FALSE);
                }
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
            int mx = LOWORD(lParam) + g_ScrollX;
            int my = HIWORD(lParam) + g_ScrollY;
            int buttons = 0;
            if (wParam & MK_LBUTTON) buttons |= 1;
            if (wParam & MK_MBUTTON) buttons |= 2;
            if (wParam & MK_RBUTTON) buttons |= 4;

            /* Clamp positions */
            if (mx < 0) mx = 0;
            if (mx >= g_ScreenWidth) mx = g_ScreenWidth - 1;
            if (my < 0) my = 0;
            if (my >= g_ScreenHeight) my = g_ScreenHeight - 1;

            send_vnc_pointer(&g_VncSock, mx, my, buttons);
            break;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP: {
            int mx = LOWORD(lParam) + g_ScrollX;
            int my = HIWORD(lParam) + g_ScrollY;
            int buttons = 0;
            if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) buttons |= 1;
            if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) buttons |= 2;
            if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) buttons |= 4;

            /* Clamp positions */
            if (mx < 0) mx = 0;
            if (mx >= g_ScreenWidth) mx = g_ScreenWidth - 1;
            if (my < 0) my = 0;
            if (my >= g_ScreenHeight) my = g_ScreenHeight - 1;

            send_vnc_pointer(&g_VncSock, mx, my, buttons);
            request_vnc_refresh(&g_VncSock);
            break;
        }

        case WM_DESTROY:
            sock_close(&g_VncSock);
            
            if (g_hPalette) {
                DeleteObject(g_hPalette);
                g_hPalette = NULL;
            }
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

        case WM_SIZE:
        {
            int cx = LOWORD(lParam); /* Current window client width */
            int cy = HIWORD(lParam); /* Current window client height */

            /* Max scroll limit = Server Dimension - Window Dimension */
            int max_x = (g_ScreenWidth > cx) ? (g_ScreenWidth - cx) : 0;
            int max_y = (g_ScreenHeight > cy) ? (g_ScreenHeight - cy) : 0;

            /* Configure scrollbar ranges (100% Win32s Safe) */
            SetScrollRange(hwnd, SB_HORZ, 0, max_x, TRUE);
            SetScrollRange(hwnd, SB_VERT, 0, max_y, TRUE);

            /* Ensure current scroll tracking doesn't leave the new boundaries */
            if (g_ScrollX > max_x) g_ScrollX = max_x;
            if (g_ScrollY > max_y) g_ScrollY = max_y;

            SetScrollPos(hwnd, SB_HORZ, g_ScrollX, TRUE);
            SetScrollPos(hwnd, SB_VERT, g_ScrollY, TRUE);
            break;
        }

        case WM_HSCROLL:
        {
            int action = LOWORD(wParam);
            int pos    = HIWORD(wParam);
            int cur_x  = g_ScrollX;
            int max_x;

            RECT r;
            GetClientRect(hwnd, &r);
            max_x = (g_ScreenWidth > r.right) ? (g_ScreenWidth - r.right) : 0;

            switch (action) {
                case SB_LINELEFT:      cur_x -= 10;  break;
                case SB_LINERIGHT:     cur_x += 10;  break;
                case SB_PAGELEFT:      cur_x -= 100; break;
                case SB_PAGERIGHT:     cur_x += 100; break;
                case SB_THUMBPOSITION:
                case SB_THUMBTRACK:    cur_x = pos;  break;
            }

            if (cur_x < 0) cur_x = 0;
            if (cur_x > max_x) cur_x = max_x;

            if (cur_x != g_ScrollX) {
                g_ScrollX = cur_x;
                SetScrollPos(hwnd, SB_HORZ, g_ScrollX, TRUE);
                InvalidateRect(hwnd, NULL, FALSE); /* Redraw screen with new offset */
            }
            break;
        }

        case WM_VSCROLL:
        {
            int action = LOWORD(wParam);
            int pos    = HIWORD(wParam);
            int cur_y  = g_ScrollY;
            int max_y;

            RECT r;
            GetClientRect(hwnd, &r);
            max_y = (g_ScreenHeight > r.bottom) ? (g_ScreenHeight - r.bottom) : 0;

            switch (action) {
                case SB_LINEUP:        cur_y -= 10;  break;
                case SB_LINEDOWN:      cur_y += 10;  break;
                case SB_PAGEUP:        cur_y -= 100; break;
                case SB_PAGEDOWN:      cur_y += 100; break;
                case SB_THUMBPOSITION:
                case SB_THUMBTRACK:    cur_y = pos;  break;
            }

            if (cur_y < 0) cur_y = 0;
            if (cur_y > max_y) cur_y = max_y;

            if (cur_y != g_ScrollY) {
                g_ScrollY = cur_y;
                SetScrollPos(hwnd, SB_VERT, g_ScrollY, TRUE);
                InvalidateRect(hwnd, NULL, FALSE); /* Redraw screen with new offset */
            }
            break;
        }

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

/* Dialog Procedure for handling user input */
BOOL CALLBACK ConnectDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG:
            /* Pre-populate fields with our default globals */
            SetDlgItemText(hDlg, IDC_IP_EDIT, g_VncIP);
            SetDlgItemInt(hDlg, IDC_PORT_EDIT, g_VncPort, FALSE);
            SetDlgItemText(hDlg, IDC_PASS_EDIT, g_VncPass);
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                    /* Extract user inputs directly into globals when OK is pressed */
                    GetDlgItemText(hDlg, IDC_IP_EDIT, g_VncIP, sizeof(g_VncIP));
                    g_VncPort = GetDlgItemInt(hDlg, IDC_PORT_EDIT, NULL, FALSE);
                    GetDlgItemText(hDlg, IDC_PASS_EDIT, g_VncPass, sizeof(g_VncPass));
                    
                    EndDialog(hDlg, IDOK);
                    return TRUE;

                case IDCANCEL:
                    EndDialog(hDlg, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

/* --- WinMain Application Entry Point --- */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc;
    MSG msg;
    BOOL bRunning = TRUE;
    DWORD result;
    int x, y, w, h, s, srcx, srcy;
    long p;

#ifdef DEBUG
    fout = fopen("vncsclnt.log", "w");
#endif

    /* 1. Launch connection window first */
    result = DialogBox(hInstance, MAKEINTRESOURCE(IDD_CONNECT_DLG), NULL, (DLGPROC)ConnectDlgProc);
    
    /* If user pressed cancel or closed the dialog window, stop app execution */
    if (result != IDOK) {
        return 0; 
    }

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
    wc.lpszClassName = "W32sVNCClass";

    if (!RegisterClass(&wc)) return 0;

    hWndMain = CreateWindow("W32sVNCClass", "Win32s VNC Client",
        WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL, /* Enforce scrollbars */
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
        NULL, NULL, hInstance, NULL);

    /* 1. Run the blocking VNC Handshake Sequentially, exactly like DOS main() */
    sock_init();

#ifdef DEBUG
	fprintf(fout,"WinMain: socket_connect\n"),fflush(fout);
#endif
    /* 2. User clicked OK! Initialize networking using our new dynamic variables */
    if (!socket_connect(&g_VncSock, g_VncIP, g_VncPort)) {
        MessageBox(NULL, "Failed to resolve host or connect to socket!", "Network Error", MB_ICONSTOP);
        return 0;
    }

#ifdef DEBUG
	fprintf(fout,"WinMain: auth_vnc\n"),fflush(fout);
#endif
    if (!auth_vnc(&g_VncSock, g_VncPass)) {
        MessageBox(NULL, "Failed to authenticate!", "Authentication Error", MB_ICONSTOP);
        return 0;
    }
#ifdef DEBUG
	fprintf(fout,"WinMain: init_vnc_client\n"),fflush(fout);
#endif
    if (!init_vnc_client(&g_VncSock)) {
        MessageBox(NULL, "Failed to init VNC client!", "Error", MB_ICONSTOP);
        return 0;
    }
#ifdef DEBUG
	fprintf(fout,"WinMain: setup_vnc_pixelformat\n"),fflush(fout);
#endif
    if (!setup_vnc_pixelformat(&g_VncSock)) {
        MessageBox(NULL, "Failed to setup VNC pixel format!", "Error", MB_ICONSTOP);
        return 0;
    }
#ifdef DEBUG
	fprintf(fout,"WinMain: setup_vnc_encodings\n"),fflush(fout);
#endif
    if (!setup_vnc_encodings(&g_VncSock)) {
        MessageBox(NULL, "Failed to setup VNC encodings!", "Error", MB_ICONSTOP);
        return 0;
    }

    if (!hWndMain) return 0;
    ShowWindow(hWndMain, nCmdShow);
    UpdateWindow(hWndMain);

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
#ifdef DEBUG
	fprintf(fout, "tick, sock_dataready, g_VncState=%d\n",g_VncState),fflush(fout);
#endif
                switch(g_VncState) {
                    case ST_IDLE:
#ifdef DEBUG
	fprintf(fout, " ST_IDLE\n"),fflush(fout);
#endif
                        g_VncState = parse_vnc_msg(&g_VncSock);
                        break;
                    case ST_RECT:
#ifdef DEBUG
	fprintf(fout, " ST_RECT\n"),fflush(fout);
#endif
                        g_VncState = parse_vnc_rect(&g_VncSock);
                        break;
                    case ST_RAW:
#ifdef DEBUG
	fprintf(fout, " ST_RAW\n"),fflush(fout);
#endif
                        g_VncState = parse_vnc_raw(&g_VncSock, &x, &y, &w, &h, &p, &s, g_BufIn);
                        video_blk(x, y, w, h, p, s, g_BufIn);
                        break;
                    case ST_COPY:
#ifdef DEBUG
	fprintf(fout, " ST_COPY\n"),fflush(fout);
#endif
                        g_VncState = parse_vnc_copy(&g_VncSock, &x, &y, &w, &h, &srcx, &srcy);
                        video_blt(x, y, w, h, srcx, srcy);
                        break;
                    case ST_RRE:  
#ifdef DEBUG
	fprintf(fout, " ST_RRE\n"),fflush(fout);
#endif
                        g_VncState = parse_vnc_rre(&g_VncSock, &x, &y, &w, &h, g_BufIn);
                        drawbar(x, y, w, h, *g_BufIn);
                        break;
                    case ST_CRRE:  
#ifdef DEBUG
	fprintf(fout, " ST_CRRE\n"),fflush(fout);
#endif
                        g_VncState = parse_vnc_crre(&g_VncSock, &x, &y, &w, &h, g_BufIn);
                        drawbar(x, y, w, h, *g_BufIn);
                        break;
                }
#ifdef DEBUG
	fprintf(fout, " new g_VncState=%d\n",g_VncState),fflush(fout);
#endif
            } else {
                /* Periodic refresh check fallback replacing countdown() */
                static DWORD lastRefresh = 0;
                DWORD now = GetTickCount(); /* Win32 millisecond counter */
                if (!lastRefresh) lastRefresh = now;
                if (now - lastRefresh > 220) {
#ifdef DEBUG
	fprintf(fout, "tick, !sock_dataready, timer=%d\n",now - lastRefresh),fflush(fout);
#endif
                    request_vnc_refresh(&g_VncSock);
                    lastRefresh = now;
                }
            }

            if (g_VncState == ST_ERROR) bRunning = FALSE;
        } else {
#ifdef DEBUG
	fprintf(fout, "no tick\n"),fflush(fout);
#endif
            bRunning = FALSE; /* Network socket disconnected */
        }
    }

    sock_close(&g_VncSock);
    WSACleanup();
    return msg.wParam;
}
