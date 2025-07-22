#pragma once
#include <windows.h>
#include <commctrl.h>
#include <atlstr.h>
#include <comutil.h>
#include <chrono>
#include <assert.h>
#include <Gdiplus.h>
#include <shlobj.h>

HINSTANCE g_hInst = NULL;
UINT const WMAPP_NOTIFYCALLBACK = WM_APP + 1;
wchar_t const szWindowClass[] = L"Screenshot Saver";
// Use a guid to uniquely identify our icon
class __declspec(uuid("09e54236-9a13-4b7f-b61d-096fc3400e10")) NotifIcon;
HWND main_hwnd = NULL;

enum EVENTMODE : UINT
{
    PADDING = 0,
    IDLE,
    SCREENSHOT,
    INVALID
};

void                RegisterWindowClass(PCWSTR pszClassName, WNDPROC lpfnWndProc);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
void                ShowContextMenu(HWND hwnd);
BOOL                AddIcon(HWND hwnd);
void                UpdateIcon(EVENTMODE channel);
BOOL                DeleteIcon();
void                MainLoop();
void                check_clipboard();
void                check_icon();
int                 ConvertBitmapToJpeg(const BITMAPINFOHEADER* bmiHeader, BITMAPFILEHEADER BitmapFileHeader, const WCHAR* filePath);
std::wstring        get_formatted_filename();

BOOL OpenClipboard_ButTryABitHarder(HWND hWnd)
{
    for (int i = 0; i < 10; ++i)
    {
        // This can fail if the clipboard is currently being accessed by another application.
        if (OpenClipboard(hWnd)) return true;
        Sleep(50);
    }
    return false;
}

// Returns the offset, in bytes, from the start of the BITMAPINFO, to the start of the pixel data array, for a packed DIB.
INT GetPixelDataOffsetForPackedDIB(const BITMAPINFOHEADER* BitmapInfoHeader)
{
    INT OffsetExtra = 0;

    if (BitmapInfoHeader->biSize == sizeof(BITMAPINFOHEADER) /* 40 */)
    {
        // This is the common BITMAPINFOHEADER type. In this case, there may be bit masks following the BITMAPINFOHEADER
        // and before the actual pixel bits (does not apply if bitmap has <= 8 bpp)
        if (BitmapInfoHeader->biBitCount > 8)
        {
            if (BitmapInfoHeader->biCompression == BI_BITFIELDS)
                OffsetExtra += 3 * sizeof(RGBQUAD);
            else if (BitmapInfoHeader->biCompression == 6 /* BI_ALPHABITFIELDS */)
                // Not widely supported, but valid.
                OffsetExtra += 4 * sizeof(RGBQUAD);
        }
    }

    if (BitmapInfoHeader->biClrUsed > 0)
        // We have no choice but to trust this value.
        OffsetExtra += BitmapInfoHeader->biClrUsed * sizeof(RGBQUAD);
    else
    {
        // In this case, the color table contains the maximum number for the current bit count (0 if > 8bpp)
        if (BitmapInfoHeader->biBitCount <= 8)
            // 1bpp: 2
            // 4bpp: 16
            // 8bpp: 256
            OffsetExtra += sizeof(RGBQUAD) << BitmapInfoHeader->biBitCount;
    }

    return BitmapInfoHeader->biSize + OffsetExtra;
}
