# Win32s & Windows 95 VNC Client

A lightweight, pure Win32 C-based VNC (RFB protocol) client optimized for vintage 32-bit Windows environments. This client is specially architected to run not only on Windows 95/98/NT but also within the **Win32s extensibility subsystem on Windows 3.1x**.

## Features

* **Win32s Compatible:** Bypasses `CreateDIBSection` (which is unsupported on Win32s) in favor of standard memory arrays blitted directly via `SetDIBitsToDevice`.
* **Dynamic Scrollbars:** Automatically handles remote desktop dimensions larger than the local window client area using custom `WM_HSCROLL` and `WM_VSCROLL` logic.
* **Scroll-Aware Input:** Automatically translates local window mouse coordinates to real remote VNC server coordinates based on the current scroll position.
* **Vintage Connection Dialog:** Features an old-school, standalone connection configuration prompt utilizing standard `EDIT` boxes, bypassing modern common controls (`SysIPAddress32`) for maximum backward compatibility.
* **Strict Packing Alignment:** Employs `#pragma pack(1)` network structure definitions to prevent 32-bit compiler padding from desynchronizing the RFB data stream.

## Repository Structure

* `vncsclnt.c` - Core application entry point (`WinMain`), window subclass procedure (`WndProc`), mouse/scrollbar routing, Framebuffer allocation, BGR233 palette generation, and bottom-up memory row-inversion blitting.
* `VNC.C` / `VNC.H` / `RFBPROTO.H` - RFB protocol parser, network state machine (`ST_IDLE`, `ST_RECT`), and byte-swapping utilities.
* `D3DES.C` / `D3DES.H` - VNC 3DES cryptor.
* `resource.h` / `resource.rc` - Dialog template definitions for the connection prompt.

## Technical Architecture Notes

### 1. Coordinate Inversion (Top-Down to Bottom-Up)

The VNC protocol transmits framebuffer updates assuming a top-down coordinate space (row 0 is the top of the screen). Because Win32s GDI handles device-independent bitmaps (DIBs) reliably only in a bottom-up format (positive `biHeight`), the rendering pipeline manually inverts the target rows during copy operations inside `video_blk`:

DIB\_Row=ScreenHeight−1−VNC\_Y

### 2. High-Performance Repainting

To keep performance high on vintage 486 and Pentium processors, `video_blk` does not issue immediate blits for the entire screen. Instead, it marks the exact updated rectangle as "dirty" via `InvalidateRect`. `WM_PAINT` then handles the clipped redrawing by slicing only the modified coordinate frame out of raw memory:

```
SetDIBitsToDevice(hdc, dest_x, dest_y, dest_w, dest_h, vnc_x, src_y, ...);

```

## Compilation Requirements

This codebase relies strictly on standard, legacy Win32 SDK APIs and can be compiled using vintage or modern tools targeting 32-bit x86 architectures:

* **Recommended Historical Compilers:** Visual C++ 6.0, Borland C++ 5.0, Watcom C/C++, LCC-Win32.
* **Modern Cross-Compilers:** MinGW (`i686-w64-mingw32-gcc`) with appropriate deployment flags.
* **Dependencies:** `WINSOCK.H` / `WSOCK32.LIB` (or the Win32s Winsock implementation).

---

