/**
 * input_dialog.cpp — Floating "Say..." input window for Insert-key player chat.
 *
 * Lives in its own thread with its own message loop.
 * Show() / submit callback are thread-safe.
 */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "input_dialog.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

static constexpr UINT WM_IDIALOG_SHOW = WM_APP + 1;
static constexpr int  IDC_EDIT        = 101;
static constexpr int  IDC_BTN         = 102;

static std::atomic<HWND> g_wnd{ nullptr };
static HWND              g_edit     = nullptr;
static WNDPROC           g_origEdit = nullptr;

static std::mutex                       g_cbMutex;
static std::function<void(std::string)> g_cb;

// Subclass: catch Enter (submit) and Escape (cancel) inside the Edit control.
static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            SendMessageA(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDC_BTN, BN_CLICKED), 0);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            ShowWindow(GetParent(hwnd), SW_HIDE);
            return 0;
        }
    }
    return CallWindowProcA(g_origEdit, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_edit = CreateWindowExA(
            WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            4, 6, 368, 22,
            hwnd, (HMENU)(UINT_PTR)IDC_EDIT, nullptr, nullptr);
        CreateWindowExA(0, "BUTTON", "Say",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            376, 4, 64, 26,
            hwnd, (HMENU)(UINT_PTR)IDC_BTN, nullptr, nullptr);
        g_origEdit = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(g_edit, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(EditSubclassProc)));
        return 0;
    }

    case WM_IDIALOG_SHOW:
        SetWindowTextA(g_edit, "");
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetFocus(g_edit);
        return 0;

    case WM_COMMAND:
        if (LOWORD(wp) == IDC_BTN) {
            char buf[512] = {};
            GetWindowTextA(g_edit, buf, sizeof(buf));
            SetWindowTextA(g_edit, "");
            ShowWindow(hwnd, SW_HIDE);
            if (buf[0]) {
                std::function<void(std::string)> cb;
                { std::lock_guard<std::mutex> lk(g_cbMutex); cb = g_cb; }
                if (cb) cb(buf);
            }
            return 0;
        }
        break;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void DialogThread()
{
    WNDCLASSEXA wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.hCursor       = LoadCursorA(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "KenshiAI_Input";
    RegisterClassExA(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = 460, wh = 56;

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "KenshiAI_Input", "Say (Enter to send, Esc to cancel)",
        WS_CAPTION | WS_SYSMENU,
        (sw - ww) / 2, sh * 3 / 4, ww, wh,
        nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);

    g_wnd.store(hwnd, std::memory_order_release);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

namespace InputDialog
{
    void Init()
    {
        std::thread(DialogThread).detach();
    }

    void Show(std::function<void(std::string)> onSubmit)
    {
        { std::lock_guard<std::mutex> lk(g_cbMutex); g_cb = std::move(onSubmit); }
        HWND wnd = g_wnd.load(std::memory_order_acquire);
        if (wnd) PostMessageA(wnd, WM_IDIALOG_SHOW, 0, 0);
    }
}
