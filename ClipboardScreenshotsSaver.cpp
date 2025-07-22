// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "ClipboardScreenshotsSaver.h"
#include "resource.h"

// we need commctrl v6 for LoadIconMetric()
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comsuppw.lib")
#pragma comment (lib,"Gdiplus.lib")

std::chrono::steady_clock::time_point saved_time{};
std::wstring directory;
int JPEG_QUALITY = 95;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(pCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);
    
    // Get class id as string
    LPOLESTR className;
    HRESULT hr = StringFromCLSID(__uuidof(NotifIcon), &className);
    if (hr != S_OK)
        return -1;

    // convert to CString
    CString c = (char*)(_bstr_t)className;
    // then release the memory used by the class name
    CoTaskMemFree(className);

    CreateMutex(0, FALSE, c); // try to create a named mutex
    if (GetLastError() == ERROR_ALREADY_EXISTS) // did the mutex already exist?
        return -1; // quit; mutex is released automatically

    SetProcessDPIAware();
    g_hInst = hInstance;
    RegisterWindowClass(szWindowClass, WndProc);

    // Create the main window. This could be a hidden window if you don't need
    // any UI other than the notification icon.
    main_hwnd = CreateWindow(szWindowClass, szWindowClass, 0, 0, 0, 0, 0, 0, 0, g_hInst, 0);
    if (main_hwnd)
    {
        if (AddClipboardFormatListener(main_hwnd) == TRUE) //-V676
        {
            PWSTR picturesFolderPath = nullptr;
            HRESULT result = SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &picturesFolderPath);

            if (result == S_OK && picturesFolderPath != nullptr) {
                directory = picturesFolderPath;
                directory += L"\\Screenshots\\";
                CoTaskMemFree(picturesFolderPath);

                MainLoop();
            }

			RemoveClipboardFormatListener(main_hwnd);
        }
    }

    return 0;
}

void MainLoop()
{
    bool CanRun = true;
    while (CanRun)
    {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                CanRun = false;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        check_icon();
        Sleep(250);
    }
}

std::wstring get_formatted_filename(const wchar_t* ext)
{
    struct tm newtime;
    time_t now = time(0);
    localtime_s(&newtime, &now);

    std::wostringstream oss;
    oss << std::put_time(&newtime, L"%Y-%m-%d %H.%M.%S");
    std::wstring filename = L"Screenshot " + oss.str() + ext;

    return directory + filename;
}

void check_clipboard()
{
	static DWORD SeqUD_old = 0;

    if (!OpenClipboard_ButTryABitHarder(NULL))
        // Could not open clipboard. This usually indicates that another application is permanently blocking it.
        return;

    DWORD SeqUD = GetClipboardSequenceNumber();
    HGLOBAL ClipboardDataHandle = (HGLOBAL)GetClipboardData(CF_DIB);

	if (SeqUD == SeqUD_old || !ClipboardDataHandle)
	{
        // Clipboard object is not a DIB, and is not auto-convertible to DIB
        CloseClipboard();
        return;
	}

    BITMAPINFOHEADER* BitmapInfoHeader = (BITMAPINFOHEADER*)GlobalLock(ClipboardDataHandle);
    assert(BitmapInfoHeader); // This can theoretically fail if mapping the HGLOBAL into local address space fails. Very pathological, just act as if it wasn't a bitmap in the clipboard.

	if (BitmapInfoHeader->biSizeImage == 0 || BitmapInfoHeader->biCompression != BI_BITFIELDS)
	{
		//clipboard was not a screenshot but a copied image
		GlobalUnlock(ClipboardDataHandle);
		CloseClipboard();
		return;
	}
    
    SIZE_T ClipboardDataSize = GlobalSize(ClipboardDataHandle);
    assert(ClipboardDataSize >= sizeof(BITMAPINFOHEADER)); // Malformed data. While older DIB formats exist (e.g. BITMAPCOREHEADER), they are not valid data for CF_DIB; it mandates a BITMAPINFO struct. If this fails, just act as if it wasn't a bitmap in the clipboard.

    INT PixelDataOffset = GetPixelDataOffsetForPackedDIB(BitmapInfoHeader);

    // The clipboard contains a packed DIB, whose start address coincides with BitmapInfoHeader, and whose total size is ClipboardDataSize.
    // By definition, we can jam the whole DIB memory into a BMP file as-is, except that we need to prepend a BITMAPFILEHEADER struct.
    // The tricky part is that for BITMAPFILEHEADER.bfOffBits, which must be calculated using the information in BITMAPINFOHEADER.

    // The BMP file layout:
    // @offset 0:                              BITMAPFILEHEADER
    // @offset 14 (sizeof(BITMAPFILEHEADER)):  BITMAPINFOHEADER
    // @offset 14 + BitmapInfoHeader->biSize:  Optional bit masks and color table
    // @offset 14 + DIBPixelDataOffset:        pixel bits
    // @offset 14 + ClipboardDataSize:         EOF
    size_t TotalBitmapFileSize = sizeof(BITMAPFILEHEADER) + ClipboardDataSize;

    BITMAPFILEHEADER BitmapFileHeader = {};
    BitmapFileHeader.bfType = 0x4D42;
    BitmapFileHeader.bfSize = (DWORD)TotalBitmapFileSize; // Will fail if bitmap size is nonstandard >4GB
    BitmapFileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + PixelDataOffset;

    //HANDLE FileHandle = CreateFileW((get_formatted_filename(L".bmp")).c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    //if (FileHandle != INVALID_HANDLE_VALUE)
    //{
    //    DWORD dummy = 0;
    //    BOOL Success = true;
    //    Success &= WriteFile(FileHandle, &BitmapFileHeader, sizeof(BITMAPFILEHEADER), &dummy, NULL);
    //    Success &= WriteFile(FileHandle, BitmapInfoHeader, (DWORD)ClipboardDataSize, &dummy, NULL);
    //    Success &= CloseHandle(FileHandle);
    //}

    int ret = ConvertBitmapToJpeg(BitmapInfoHeader, BitmapFileHeader, get_formatted_filename(L".jpg").c_str());
    if (ret == 0)
    {
        saved_time = std::chrono::high_resolution_clock::now();
        SeqUD_old = SeqUD;
    }

    GlobalUnlock(ClipboardDataHandle);
    CloseClipboard();
}

void check_icon()
{
    auto curr_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float, std::milli> delta_time_ms = curr_time - saved_time;

    //show icon for 2.5s
    UpdateIcon(delta_time_ms.count() >= 0 && delta_time_ms.count() < 2500 ? SCREENSHOT : IDLE);
}

void RegisterWindowClass(PCWSTR pszClassName, WNDPROC lpfnWndProc)
{
    WNDCLASSEX wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = lpfnWndProc;
    wcex.hInstance = g_hInst;
    wcex.lpszClassName = pszClassName;
    RegisterClassEx(&wcex);
}

BOOL AddIcon(HWND hwnd)
{
    NOTIFYICONDATA nid = { sizeof(nid) };
    nid.hWnd = hwnd;
    // add the icon, setting the icon, tooltip, and callback message.
    // the icon will be identified with the GUID
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_GUID;
    nid.guidItem = __uuidof(NotifIcon);
    nid.uCallbackMessage = WMAPP_NOTIFYCALLBACK;
    LoadIconMetric(g_hInst, MAKEINTRESOURCE(IDI_NOTIFICATIONICONIDX + (int)IDLE), LIM_SMALL, &nid.hIcon);
    BOOL ret = Shell_NotifyIcon(NIM_ADD, &nid);
    if (ret != TRUE)
        return FALSE;

    // NOTIFYICON_VERSION_4 is prefered
    nid.uVersion = NOTIFYICON_VERSION_4;
    return Shell_NotifyIcon(NIM_SETVERSION, &nid);
}

void UpdateIcon(EVENTMODE mode)
{
    static EVENTMODE old_mode = PADDING;
    if (mode == old_mode)
        return;

    int IDX = (int)mode + IDI_NOTIFICATIONICONIDX;
    NOTIFYICONDATA nid = { sizeof(nid) };
    nid.uFlags = NIF_ICON | NIF_GUID;
    nid.guidItem = __uuidof(NotifIcon);

    HRESULT hr = LoadIconMetric(g_hInst, MAKEINTRESOURCE(IDX), LIM_SMALL, &nid.hIcon);
    if (hr != S_OK)
        return;
    BOOL ret = Shell_NotifyIcon(NIM_MODIFY, &nid);
    if (ret != TRUE)
    {
        DeleteIcon();
        AddIcon(main_hwnd);
        old_mode = PADDING;
        return;
    }

    old_mode = mode;

}

BOOL DeleteIcon()
{
    NOTIFYICONDATA nid = { sizeof(nid) };
    nid.uFlags = NIF_GUID;
    nid.guidItem = __uuidof(NotifIcon);
    return Shell_NotifyIcon(NIM_DELETE, &nid);
}

void ShowContextMenu(HWND hwnd)
{
    HMENU hMenu = LoadMenu(g_hInst, MAKEINTRESOURCE(IDC_CONTEXTMENU));
    if (hMenu)
    {
        HMENU hSubMenu = GetSubMenu(hMenu, 0);
        if (hSubMenu)
        {
            // our window must be foreground before calling TrackPopupMenu or the menu will not disappear when the user clicks away
            SetForegroundWindow(hwnd);

            // respect menu drop alignment
            UINT uFlags = TPM_RIGHTBUTTON;
            if (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0)
                uFlags |= TPM_RIGHTALIGN;
            else
                uFlags |= TPM_LEFTALIGN;
            POINT pt;
            GetCursorPos(&pt);
            TrackPopupMenuEx(hSubMenu, uFlags, pt.x, pt.y, hwnd, NULL);
        }
        DestroyMenu(hMenu);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CLIPBOARDUPDATE:
        check_clipboard();
        break;
    case WM_CREATE:
        // add the notification icon
        if (!AddIcon(hwnd))
            return -1;
        break;
    case WM_COMMAND:
    {
        // Parse the menu selections:
        switch (LOWORD(wParam))
        {
        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
        }
    }
    break;

    case WMAPP_NOTIFYCALLBACK:
        switch (LOWORD(lParam))
        {
        case WM_CONTEXTMENU:
            ShowContextMenu(hwnd);
            break;
        }
        break;

    case WM_DESTROY:
        DeleteIcon();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
    UINT numEncoders, size;
    Gdiplus::ImageCodecInfo* encoders;
    Gdiplus::GetImageEncodersSize(&numEncoders, &size);
    encoders = (Gdiplus::ImageCodecInfo*)(malloc(size));
    if (encoders == NULL)
        return -1;
    Gdiplus::GetImageEncoders(numEncoders, size, encoders);

    for (UINT j = 0; j < numEncoders; ++j) {
        if (wcscmp(encoders[j].MimeType, format) == 0) {
            *pClsid = encoders[j].Clsid;
            free(encoders);
            return j;
        }
    }
    free(encoders);
    return 0;
}

int ConvertBitmapToJpeg(const BITMAPINFOHEADER* bmiHeader, BITMAPFILEHEADER BitmapFileHeader, const WCHAR* filePath)
{
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    
    int width = bmiHeader->biWidth;
    int height = abs(bmiHeader->biHeight);
    int bpp = bmiHeader->biBitCount;

    auto format = bpp == 32 ? PixelFormat32bppRGB : PixelFormat24bppRGB;
    Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(width, height, format);
    Gdiplus::BitmapData bitmapData;
    Gdiplus::Rect rect(0, 0, width, height);

    bitmap->LockBits(&rect, Gdiplus::ImageLockModeWrite, format, &bitmapData);
    BYTE* destPixels = (BYTE*)bitmapData.Scan0;
    int bytesPerRow = bitmapData.Stride;

    bool k{};
    for (int y = 0; y < height; ++y) {
        BYTE* destRow = destPixels + (height - y - 1) * bytesPerRow;
        const BYTE* sourceRow = (BYTE*)bmiHeader + BitmapFileHeader.bfOffBits - 14 + y * (width * (bpp / 8));
        memcpy(destRow, sourceRow, width * (bpp / 8));
    }
    bitmap->UnlockBits(&bitmapData);

    CLSID encoderClsid;
    GetEncoderClsid(L"image/jpeg", &encoderClsid);
    Gdiplus::EncoderParameters params;
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &JPEG_QUALITY;

    Gdiplus::Status status = bitmap->Save(filePath, &encoderClsid, &params);

    delete bitmap;
    Gdiplus::GdiplusShutdown(gdiplusToken);

    return (status == Gdiplus::Ok) ? 0 : -1;
}
