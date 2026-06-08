#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h> // Winsock 1.1 header for Win95

#define WM_VNC_SOCKET_EVENT (WM_USER + 1)
#define ID_CONNECT_BUTTON   101

// Global Variables
HINSTANCE hInst;
HWND hWndMain;
SOCKET vncSocket = INVALID_SOCKET;
char* serverIP = "192.168.1.50"; // Change to your VNC server IP
int serverPort = 5900;           // Default VNC Port

// Forward Declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
BOOL InitWinsock(void);
void CleanUpWinsock(void);
void ConnectToVNC(HWND hwnd);
void HandleVNCNetwork(HWND hwnd, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc;
    MSG msg;

    hInst = hInstance;

    if (!InitWinsock()) {
        MessageBox(NULL, "Winsock 1.1 Initialization Failed!", "Error", MB_ICONERROR);
        return 0;
    }

    // Register Window Class
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

    // Create Main Window
    hWndMain = CreateWindow("Win95VNCClass", "Win95 Single-Threaded VNC",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            640, 480, NULL, NULL, hInstance, NULL);

    if (!hWndMain) return 0;

    ShowWindow(hWndMain, nCmdShow);
    UpdateWindow(hWndMain);

    // Standard Single-Threaded Message Loop
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CleanUpWinsock();
    return msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            // Create a simple button to trigger connection
            CreateWindow("BUTTON", "Connect", WS_VISIBLE | WS_CHILD,
                         10, 10, 100, 30, hwnd, (HMENU)ID_CONNECT_BUTTON, hInst, NULL);
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_CONNECT_BUTTON) {
                ConnectToVNC(hwnd);
            }
            break;

        case WM_VNC_SOCKET_EVENT:
            // This is where the single-threaded magic happens!
            HandleVNCNetwork(hwnd, wParam, lParam);
            break;

        case WM_DESTROY:
            if (vncSocket != INVALID_SOCKET) {
                closesocket(vncSocket);
            }
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

BOOL InitWinsock(void) {
    WSADATA wsaData;
    // Request Winsock 1.1 specifically for pure Win95 compatibility
    WORD wVersionRequested = MAKEWORD(1, 1); 
    return (WSAStartup(wVersionRequested, &wsaData) == 0);
}

void CleanUpWinsock(void) {
    WSACleanup();
}

void ConnectToVNC(HWND hwnd) {
    struct sockaddr_in serverAddr;

    vncSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (vncSocket == INVALID_SOCKET) {
        MessageBox(hwnd, "Could not create socket.", "Error", MB_OK);
        return;
    }

    // Configure WSAAsyncSelect BEFORE connecting to make the connect call non-blocking
    // We want Windows to notify us on READ, WRITE (Connected), and CLOSE events.
    WSAAsyncSelect(vncSocket, hwnd, WM_VNC_SOCKET_EVENT, FD_READ | FD_WRITE | FD_CLOSE);

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    serverAddr.sin_addr.s_addr = inet_addr(serverIP);

    // Because of WSAAsyncSelect, connect() will immediately return WSAEWOULDBLOCK.
    // This is expected! The actual connection success will arrive via FD_WRITE.
    connect(vncSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
}

void HandleVNCNetwork(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    int socketError = WSAGETSELECTERROR(lParam);
    int socketEvent = WSAGETSELECTEVENT(lParam);

    if (socketError) {
        MessageBox(hwnd, "Socket error encountered.", "Error", MB_OK);
        closesocket(vncSocket);
        vncSocket = INVALID_SOCKET;
        return;
    }

    switch (socketEvent) {
        case FD_WRITE:
            // FD_WRITE triggers once when the non-blocking connection successfully establishes
            MessageBox(hwnd, "Connected to VNC Server! Starting Handshake...", "Success", MB_OK);
            
            // TODO: Step 1 of RFB Protocol: Server sends ProtocolVersion string.
            // In a full client, you'd prepare your state machine here to process incoming bytes.
            break;

        case FD_READ: {
            char buffer[1024];
            int bytesRead = recv(vncSocket, buffer, sizeof(buffer) - 1, 0);

            if (bytesRead > 0) {
                // TODO: Feed 'buffer' into your RFB Protocol State Machine.
                // Because you are single-threaded, parse what you can, and if you need 
                // more bytes, simply exit. Windows will fire FD_READ again when more data lands.
                
                // For a graphical app: Parse pixels -> write to DIB -> InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case FD_CLOSE:
            MessageBox(hwnd, "VNC Server closed the connection.", "Info", MB_OK);
            closesocket(vncSocket);
            vncSocket = INVALID_SOCKET;
            break;
    }
}