#include "pch.h"
#include "Surtr.h"

#include "SurtrArgument.h"

#ifndef HID_USAGE_PAGE_GENERIC
#define HID_USAGE_PAGE_GENERIC         ((USHORT) 0x01)
#endif
#ifndef HID_USAGE_GENERIC_MOUSE
#define HID_USAGE_GENERIC_MOUSE        ((USHORT) 0x02)
#endif

using namespace DirectX;

UINT                        g_dwSize = sizeof(RAWINPUT);
BYTE                        g_lpb[sizeof(RAWINPUT)];

LPCWSTR                     g_szAppName = L"Surtr";
std::unique_ptr<Surtr>      g_surtr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Windows procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static bool s_in_sizemove = false;
    static bool s_in_suspend = false;
    static bool s_minimized = false;

    // imgui procedure handler.
    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;

    switch (message)
    {
    case WM_CREATE:
        break;

    case WM_PAINT:
        if (s_in_sizemove && g_surtr)
        {
            g_surtr->Tick();
        }
        else
        {
            PAINTSTRUCT ps;
            std::ignore = BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
        }
        break;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
        {
            if (!s_minimized)
            {
                s_minimized = true;
                if (!s_in_suspend && g_surtr)
                    g_surtr->OnSuspending();
                s_in_suspend = true;
            }
        }
        else if (s_minimized)
        {
            s_minimized = false;
            if (s_in_suspend && g_surtr)
                g_surtr->OnResuming();
            s_in_suspend = false;
        }
        else if (!s_in_sizemove && g_surtr)
        {
            g_surtr->OnWindowSizeChanged(LOWORD(lParam), HIWORD(lParam));
        }
        break;

    case WM_KEYDOWN:
        g_surtr->OnKeyDown(static_cast<UINT8>(wParam));
        break;

    case WM_KEYUP:
		g_surtr->OnKeyUp(static_cast<UINT8>(wParam));
		break;

    case WM_MOUSEWHEEL:
        g_surtr->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
		break;

    case WM_INPUT:
    {
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, g_lpb, &g_dwSize, sizeof(RAWINPUTHEADER));

        const auto* raw = reinterpret_cast<RAWINPUT*>(g_lpb);
        if (raw->header.dwType == RIM_TYPEMOUSE)
            g_surtr->OnMouseMove(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
        break;
    }

    case WM_ENTERSIZEMOVE:
        s_in_sizemove = true;
        break;

    case WM_EXITSIZEMOVE:
        s_in_sizemove = false;
        if (g_surtr)
        {
            RECT rc;
            GetClientRect(hWnd, &rc);

            g_surtr->OnWindowSizeChanged(rc.right - rc.left, rc.bottom - rc.top);
        }
        break;

    case WM_GETMINMAXINFO:
        if (lParam)
        {
	        const auto info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 320;
            info->ptMinTrackSize.y = 200;
        }
        break;

    case WM_ACTIVATEAPP:
        if (g_surtr)
        {
            if (wParam)
            {
                g_surtr->OnActivated();
            }
            else
            {
                g_surtr->OnDeactivated();
            }
        }
        break;

    case WM_POWERBROADCAST:
        switch (wParam)
        {
        case PBT_APMQUERYSUSPEND:
            if (!s_in_suspend && g_surtr)
                g_surtr->OnSuspending();
            s_in_suspend = true;
            return TRUE;

        case PBT_APMRESUMESUSPEND:
            if (!s_minimized)
            {
                if (s_in_suspend && g_surtr)
                    g_surtr->OnResuming();
                s_in_suspend = false;
            }
            return TRUE;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_MENUCHAR:
        return MAKELRESULT(0, MNC_CLOSE);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

// Entry point
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    if (!XMVerifyCPUSupport())
        return 1;

    Microsoft::WRL::Wrappers::RoInitializeWrapper initialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialize))
        return 1;

    g_surtr = std::make_unique<Surtr>();

    // Register class and create window
    {
        // Register class
        WNDCLASSEXW wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WndProc;
        wcex.hInstance = hInstance;
        wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
        wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_ACTIVECAPTION);
        wcex.lpszClassName = L"SurtrWinClass";
        wcex.hIconSm = LoadIcon(hInstance, IDI_APPLICATION);
        if (!RegisterClassExW(&wcex))
            return 1;

        // Collect arguments
        const SurtrArgument arguments = CollectSurtrArgument();

        RECT rc = { 0, 0, static_cast<LONG>(arguments.Width), static_cast<LONG>(arguments.Height) };
        const DWORD dwStyle = arguments.FullScreenMode ? WS_POPUP : WS_OVERLAPPEDWINDOW;

    	AdjustWindowRect(&rc, dwStyle, FALSE);

        HWND hwnd = CreateWindowExW(0, L"SurtrWinClass", g_szAppName, dwStyle,
            CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
            nullptr, nullptr, hInstance,
            g_surtr.get());

        if (!hwnd)
            return 1;

        ShowWindow(hwnd, nCmdShow);
    	GetClientRect(hwnd, &rc);

        POINT pt = { arguments.Width / 2, arguments.Height / 2 };
        ClientToScreen(hwnd, &pt);
        SetCursorPos(pt.x, pt.y);
        ShowCursor(FALSE);

        // Register raw input handler.
        RAWINPUTDEVICE Rid[1];
        Rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
        Rid[0].usUsage = HID_USAGE_GENERIC_MOUSE;
        Rid[0].dwFlags = RIDEV_INPUTSINK;
        Rid[0].hwndTarget = hwnd;
        RegisterRawInputDevices(Rid, 1, sizeof(Rid[0]));

        g_surtr->InitializeD3DResources(
            hwnd, rc.right - rc.left, rc.bottom - rc.top, 
            arguments.SubDivideCount, 
            arguments.ShadowMapSize, 
            arguments.FullScreenMode);
    }

    // Main message loop
    MSG msg = {};
    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            g_surtr->Tick();
        }
    }

    g_surtr.reset();

    return static_cast<int>(msg.wParam);
}

// Exit helper
void ExitGame() noexcept
{
    PostQuitMessage(0);
}