#ifndef UNICODE
#define UNICODE
#endif

#include "resource.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <commdlg.h>

#include <format>
#include <string>
#include <vector>
#include <fstream>

// Linking with libraries
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")
#pragma comment(lib, "comdlg32")

const wchar_t *CLASS_NAME = L"Wallclock_Window_Class";
const wchar_t *WINDOW_NAME = L"Wallclock";

const wchar_t *WEEK_DAY_NAMES[] =
    {
        L"Sunday",
        L"Monday",
        L"Tuesday",
        L"Wednesday",
        L"Thursday",
        L"Friday",
        L"Saturday"};

const wchar_t *MONTH_NAMES[] = {
    L"January",
    L"February",
    L"March",
    L"April",
    L"May",
    L"June",
    L"July",
    L"August",
    L"September",
    L"October",
    L"November",
    L"December"};

const wchar_t *GetDateSuffix(int date)
{
    switch (date)
    {
    case 1:
        return L"st";
    case 2:
        return L"nd";
    case 3:
        return L"rd";
    default:
        return L"th";
    }
}

struct Wallpaper
{
    int width;
    int height;
    uint8_t *pixels;
    ID2D1Bitmap *bitmap;
};

struct AppState
{
    ID2D1Factory *d2dFactory;
    ID2D1HwndRenderTarget *renderTarget;
    IDWriteFactory *dwriteFactory;
    IDWriteTextFormat *titleTextFormat;
    IDWriteTextFormat *subttitleTextFormat;
    IDWriteTextFormat *bodyTextFormat;
    ID2D1SolidColorBrush *textBrush;
    ID2D1SolidColorBrush *backgroundBrush;
    bool resourcesInitialised;
    Wallpaper wallpaper;
    HWND hwnd;
    DWORD processId;
    std::wstring todoFile;
    std::vector<std::wstring> todos;
};

inline AppState *GetAppState(HWND hwnd)
{
    LONG_PTR ptr = GetWindowLongPtr(hwnd, GWLP_USERDATA);
    AppState *state = (AppState *)(ptr);
    return state;
}

template <class T>
void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

bool CreateDeviceIndependentResources(AppState *state);
void ReleaseDeviceIndependentResources(AppState *state);
void ResizeWindow(AppState *state);
bool CreateDeviceResources(AppState *state);
void ReleaseDeviceResources(AppState *state);
void PaintWindow(AppState *state);
void UpdateSleepBehaviour(AppState *state);
void SelectTodoFile(AppState *state);
void LoadTodos(AppState *state);

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nShowCmd)
{
    WNDCLASSEX wc = {0};

    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));

    RegisterClassEx(&wc);

    AppState *state = (AppState *)VirtualAlloc(
        nullptr,
        sizeof(state),
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    state->processId = GetCurrentProcessId();

    SelectTodoFile(state);
    LoadTodos(state);

    state->hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        WINDOW_NAME,

        WS_POPUP | WS_BORDER,
        0, 0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),

        nullptr,
        nullptr,
        hInstance,
        state);

    if (state->hwnd == nullptr)
    {
        OutputDebugString(L"Failed to created window\n");
        return 1;
    }

    ShowCursor(false);
    ShowWindow(state->hwnd, nShowCmd);

    MSG msg = {0};
    while (GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    AppState *state;
    if (uMsg == WM_CREATE)
    {
        CREATESTRUCT *pCreate = (CREATESTRUCT *)(lParam);
        state = (AppState *)(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)state);

        bool failed = CreateDeviceIndependentResources(state);
        if (failed)
        {
            return -1;
        }

        SetTimer(hwnd, 1, 1000, nullptr);
    }
    else
    {
        state = GetAppState(hwnd);
    }

    switch (uMsg)
    {
    case WM_SIZE:
    {
        ResizeWindow(state);
    }
        return 0;

    case WM_TIMER:
    {
        UpdateSleepBehaviour(state);
        InvalidateRect(state->hwnd, nullptr, FALSE);
    }
        return 0;

    case WM_ACTIVATE:
    {
        UpdateSleepBehaviour(state);
    }

    case WM_KEYDOWN:
        switch (wParam)
        {
        case VK_ESCAPE:
        {
            if (MessageBox(state->hwnd,
                           L"Are you sure you want to quit?",
                           L"Quit Confirmation", MB_YESNO | MB_ICONQUESTION) == IDYES)
            {
                DestroyWindow(state->hwnd);
            }
        }
        break;

        case VK_F5:
        {
            LoadTodos(state);
            InvalidateRect(state->hwnd, nullptr, FALSE);
        }
        break;

        default:
            break;
        }
        return 0;

    case WM_DESTROY:
    {
        ReleaseDeviceResources(state);
        ReleaseDeviceIndependentResources(state);

        PostQuitMessage(0);
    }
        return 0;

    case WM_PAINT:
    {
        PaintWindow(state);
    }
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void UpdateSleepBehaviour(AppState *state)
{
    HWND hWnd = GetForegroundWindow();
    if (hWnd == nullptr)
    {
        return;
    }

    DWORD processId;
    GetWindowThreadProcessId(hWnd, &processId);

    if (processId == state->processId)
    {
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
    }
    else
    {
        SetThreadExecutionState(ES_CONTINUOUS);
    }
}

bool CreateDeviceIndependentResources(AppState *state)
{
    HRESULT hr;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &state->d2dFactory);
    if (FAILED(hr))
    {
        OutputDebugString(L"Failed to create D2D Factory\n");
        return true;
    }

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             __uuidof(state->dwriteFactory),
                             (IUnknown **)&state->dwriteFactory);

    if (FAILED(hr))
    {
        OutputDebugString(L"Failed to create DWrite Factory\n");
        return true;
    }

    static const WCHAR msc_fontName[] = L"Consolas";

    hr = state->dwriteFactory->CreateTextFormat(
        msc_fontName,
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        128,
        L"", // Locale
        &state->titleTextFormat);
    if (FAILED(hr))
    {
        OutputDebugString(L"Failed to create DWrite Title Text Format\n");
        return true;
    }
    state->titleTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

    hr = state->dwriteFactory->CreateTextFormat(
        msc_fontName,
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        70,
        L"", // Locale
        &state->subttitleTextFormat);

    if (FAILED(hr))
    {
        OutputDebugString(L"Failed to create DWrite SubTitle Text Format\n");
        return true;
    }
    state->subttitleTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

    hr = state->dwriteFactory->CreateTextFormat(
        msc_fontName,
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        50,
        L"", // Locale
        &state->bodyTextFormat);

    if (FAILED(hr))
    {
        OutputDebugString(L"Failed to create DWrite Body Text Format");
        return true;
    }
    return false;
}

void ReleaseDeviceIndependentResources(AppState *state)
{
    SafeRelease(&state->titleTextFormat);
    SafeRelease(&state->subttitleTextFormat);
    SafeRelease(&state->bodyTextFormat);
    SafeRelease(&state->dwriteFactory);
    SafeRelease(&state->d2dFactory);
}

void ResizeWindow(AppState *state)
{
    if (state->renderTarget != nullptr)
    {
        RECT rc;
        GetClientRect(state->hwnd, &rc);

        D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);

        state->renderTarget->Resize(size);
        InvalidateRect(state->hwnd, nullptr, FALSE);
    }
}

bool CreateDeviceResources(AppState *state)
{
    HRESULT hr;

    if (state->renderTarget == nullptr || !state->resourcesInitialised)
    {
        state->resourcesInitialised = false;

        RECT rc;
        GetClientRect(state->hwnd, &rc);

        D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);

        hr = state->d2dFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(state->hwnd, size),
            &state->renderTarget);

        if (FAILED(hr))
        {
            OutputDebugString(L"Cannot create d2d render target\n");
            return true;
        }

        hr = state->renderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &state->textBrush);
        if (FAILED(hr))
        {
            OutputDebugString(L"Cannot text brush\n");
            return true;
        }

        hr = state->renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.5f), &state->backgroundBrush);
        if (FAILED(hr))
        {
            OutputDebugString(L"Cannot background brush\n");
            return true;
        }

        HINSTANCE hInstance = GetModuleHandle(NULL);
        HBITMAP hBitmap = (HBITMAP)LoadImage(
            hInstance,
            MAKEINTRESOURCE(IDB_BACKGROUND_IMAGE),
            IMAGE_BITMAP,
            0, 0,
            LR_LOADTRANSPARENT);
        if (hBitmap == nullptr)
        {
            OutputDebugString(L"Cannot load wallpaper image\n");
            return true;
        }

        BITMAP bitmapInfo;
        if (GetObject(hBitmap, sizeof(BITMAP), &bitmapInfo) == 0)
        {
            OutputDebugString(L"Cannot load wallpaper image info\n");
            return true;
        }
        state->wallpaper.width = bitmapInfo.bmWidth;
        state->wallpaper.height = bitmapInfo.bmHeight;

        BITMAPINFO bitmapInfoHeader = {};
        bitmapInfoHeader.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfoHeader.bmiHeader.biWidth = bitmapInfo.bmWidth;
        bitmapInfoHeader.bmiHeader.biHeight = bitmapInfo.bmHeight;
        bitmapInfoHeader.bmiHeader.biPlanes = 1;
        bitmapInfoHeader.bmiHeader.biBitCount = 32;
        bitmapInfoHeader.bmiHeader.biCompression = BI_RGB;

        state->wallpaper.pixels = new uint8_t[bitmapInfo.bmWidthBytes * bitmapInfo.bmHeight];
        if (state->wallpaper.pixels == nullptr)
        {
            OutputDebugString(L"Cannot allocate memory for bitmap pixels\n");
            DeleteObject(hBitmap);
            return true;
        }

        GetDIBits(GetDC(NULL), hBitmap, 0, bitmapInfo.bmHeight, state->wallpaper.pixels, &bitmapInfoHeader, DIB_RGB_COLORS);
        DeleteObject(hBitmap);

        D2D1_BITMAP_PROPERTIES bitmapProperties = D2D1::BitmapProperties(
            D2D1::PixelFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED));

        hr = state->renderTarget->CreateBitmap(
            D2D1::SizeU(bitmapInfo.bmWidth, bitmapInfo.bmHeight),
            state->wallpaper.pixels,
            bitmapInfo.bmWidthBytes,
            bitmapProperties,
            &state->wallpaper.bitmap);
        if (FAILED(hr))
        {
            OutputDebugString(L"Unable to create bitmap from image\n");
            return true;
        }

        state->resourcesInitialised = true;
    }

    return false;
}

void ReleaseDeviceResources(AppState *state)
{
    OutputDebugString(L"Resources device released\n");

    SafeRelease(&state->renderTarget);
    SafeRelease(&state->textBrush);

    SafeRelease(&state->wallpaper.bitmap);
    delete[] state->wallpaper.pixels;
}

void PaintWindow(AppState *state)
{
    bool failed = CreateDeviceResources(state);
    if (failed)
    {
        MessageBox(state->hwnd, L"Failed to create device resources", L"Error", MB_ICONERROR);
        ExitProcess(1);
    }

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(state->hwnd, &ps);
    {
        state->renderTarget->BeginDraw();
        state->renderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        D2D1_SIZE_F renderTargetSize = state->renderTarget->GetSize();

        SYSTEMTIME lt;
        GetLocalTime(&lt);

        // Drawing Wallpaper
        {
            float sourceTop;

            int hour = lt.wHour;
            if (hour >= 6 && hour < 12)
            {
                sourceTop = 0;
            }
            else if (hour >= 12 && hour < 18)
            {
                sourceTop = state->wallpaper.height / 4;
            }
            else if (hour >= 18 && hour < 24)
            {
                sourceTop = state->wallpaper.height / 4 * 2;
            }
            else
            {
                sourceTop = state->wallpaper.height / 4 * 3;
            }

            D2D1_RECT_F destinationRect = D2D1::RectF(0.0f, 0.0f, renderTargetSize.width, renderTargetSize.height);
            D2D1_RECT_F sourceRect = D2D1::RectF(
                0.0f,
                sourceTop,
                state->wallpaper.width,
                sourceTop + state->wallpaper.height/4);

            state->renderTarget->DrawBitmap(
                state->wallpaper.bitmap,
                destinationRect,
                1.0f,
                D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                sourceRect);

            state->renderTarget->FillRectangle(
                destinationRect,
                state->backgroundBrush);
        }

        // Drawing Date & Time
        {
            static int padding = 50;
            std::wstring time = std::format(L"{:02}:{:02}", lt.wHour, lt.wMinute);

            IDWriteTextLayout *textLayout;
            state->dwriteFactory->CreateTextLayout(
                time.c_str(),
                time.size(),
                state->titleTextFormat,
                renderTargetSize.width - padding,
                renderTargetSize.height,
                &textLayout);

            state->renderTarget->DrawTextLayout(
                D2D1::Point2F(0, 0),
                textLayout,
                state->textBrush);

            std::wstring date = std::format(L"{} {}{} {}",
                                            WEEK_DAY_NAMES[lt.wDayOfWeek],
                                            lt.wDay, GetDateSuffix(lt.wDay),
                                            MONTH_NAMES[lt.wMonth]);

            DWRITE_TEXT_METRICS textMetrics;
            textLayout->GetMetrics(&textMetrics);

            float textHeight = textMetrics.height;
            float textWidth = textMetrics.width;

            state->renderTarget->DrawText(
                date.c_str(),
                date.size(),
                state->subttitleTextFormat,
                D2D1::RectF(0, textHeight,
                            renderTargetSize.width - padding,
                            renderTargetSize.height),
                state->textBrush);

            SafeRelease(&textLayout);
        }

        // Drawing TODOs
        {
            float y = 250;
            float x = 50;
            static float spacing = 10;
            for (const auto &todo : state->todos)
            {
                IDWriteTextLayout *textLayout;
                state->dwriteFactory->CreateTextLayout(
                    todo.c_str(),
                    todo.size(),
                    state->bodyTextFormat,
                    renderTargetSize.width,
                    renderTargetSize.height,
                    &textLayout);

                DWRITE_TEXT_METRICS textMetrics;
                textLayout->GetMetrics(&textMetrics);

                float textHeight = textMetrics.height;
                float textWidth = textMetrics.width;

                state->renderTarget->DrawTextLayout(
                    D2D1::Point2F(x, y),
                    textLayout,
                    state->textBrush);

                y += textHeight + spacing;
                SafeRelease(&textLayout);
            }
        }

        HRESULT hr = state->renderTarget->EndDraw();
        if (FAILED(hr) || hr == D2DERR_RECREATE_TARGET)
        {
            ReleaseDeviceResources(state);
        }
    }
    EndPaint(state->hwnd, &ps);
}

void SelectTodoFile(AppState *state)
{
    OutputDebugString(L"Selecting file\n");
    OPENFILENAME ofn;
    WCHAR szFile[260];
    ZeroMemory(&ofn, sizeof(ofn));

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = L"All Files\0*.*\0Text Files\0*.TXT\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn))
    {
        state->todoFile = ofn.lpstrFile;
    }
    else
    {
        MessageBox(state->hwnd, L"Failed to open file", L"Error", MB_ICONERROR);
        ExitProcess(1);
    }
}

void LoadTodos(AppState *state)
{
    if (state->todoFile.empty())
    {
        return;
    }

    state->todos.clear();

    std::ifstream file(state->todoFile);
    if (file.is_open())
    {
        std::string line;
        while (std::getline(file, line))
        {
            state->todos.push_back(std::wstring(line.begin(), line.end()));
        }
    }
    else
    {
        OutputDebugString(L"Failed to open todos.txt\n");
    }
}
