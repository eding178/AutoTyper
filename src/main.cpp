#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>
#include "input_plan.h"
#include "text_compare.h"

namespace {
constexpr wchar_t kClass[] = L"TecladoAutomaticoWindow";
constexpr int kHotkey = 77;
constexpr int kText = 101, kDelay = 102, kSpeed = 103, kStart = 104, kStop = 105;
constexpr int kLinePause = 106, kCompare = 107;
constexpr wchar_t kCompareClass[] = L"AutoTyperCompareWindow";
constexpr int kMaxText = 1000000;
constexpr COLORREF kBackground = RGB(245, 247, 251);
constexpr COLORREF kInk = RGB(27, 39, 61);
constexpr COLORREF kMuted = RGB(83, 99, 120);

enum class State { Idle, Countdown, Typing };
struct App {
    HWND window{}, title{}, author{}, subtitle{}, textLabel{}, count{}, text{}, delayLabel{}, delay{},
        speedLabel{}, speed{}, linePauseLabel{}, linePauseInput{}, start{}, stop{}, compare{}, status{}, progress{}, help{};
    HFONT normal{}, heading{}, small{};
    HBRUSH background{}, white{};
    HANDLE timer{};
    State state = State::Idle;
    std::vector<typer::Stroke> plan;
    std::size_t index = 0;
    ULONGLONG deadline = 0, nextStroke = 0, lastStatus = 0;
    unsigned interval = 50, linePause = 250;
    int dpi = 96;
    HWND target{}, targetFocus{};
    bool hotkeyRegistered = false;
    std::wstring targetName;
    std::u16string lastRunText;
    bool hasLastRun = false;
} app;

ULONGLONG clockMs() {
    static const LONGLONG frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<ULONGLONG>(value.QuadPart / frequency) * 1000 +
        static_cast<ULONGLONG>(value.QuadPart % frequency) * 1000 / frequency;
}

bool armTimer() {
    if (app.state == State::Idle) {
        CancelWaitableTimer(app.timer);
        return true;
    }
    const auto now = clockMs();
    const auto next = app.state == State::Countdown ? app.deadline : app.nextStroke;
    // At most 10 ms between cancellation checks. A short character interval
    // schedules its own earlier wake-up; there is never a busy loop or catch-up.
    const ULONGLONG delay = next > now ? std::min<ULONGLONG>(10, next - now) : 1;
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(delay * 10000);
    return SetWaitableTimer(app.timer, &due, 0, nullptr, nullptr, FALSE) != FALSE;
}

int px(int value) { return MulDiv(value, app.dpi, 96); }
void setText(HWND control, const std::wstring& value) { SetWindowTextW(control, value.c_str()); }
std::wstring readText(HWND control) {
    const int n = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(n) + 1, L'\0');
    const int copied = GetWindowTextW(control, value.data(), n + 1);
    value.resize(copied);
    return value;
}
void status(const std::wstring& value) { setText(app.status, value); }
HWND makeControl(const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0,
                 DWORD exStyle = 0) {
    return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 0, 0, app.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}
void layout() {
    RECT r{};
    GetClientRect(app.window, &r);
    const int gap = px(24), width = r.right - gap * 2;
    const int controlsY = r.bottom - px(220);
    const int textY = px(127), textHeight = std::max(px(120), controlsY - textY - px(20));
    auto move = [](HWND h, int x, int y, int w, int height) {
        MoveWindow(h, x, y, std::max(w, 1), std::max(height, 1), TRUE);
    };
    move(app.title, gap, px(21), width - px(220), px(34));
    move(app.author, gap + width - px(220), px(30), px(220), px(20));
    move(app.subtitle, gap, px(62), width, px(37));
    move(app.textLabel, gap, px(104), width / 2, px(23));
    move(app.count, gap + width / 2, px(105), width / 2, px(21));
    move(app.text, gap, textY, width, textHeight);
    move(app.delayLabel, gap, controlsY, px(165), px(22));
    move(app.delay, gap, controlsY + px(25), px(145), px(32));
    move(app.speedLabel, gap + px(160), controlsY, px(200), px(22));
    move(app.speed, gap + px(160), controlsY + px(25), px(145), px(32));
    move(app.linePauseLabel, gap + px(370), controlsY, px(150), px(22));
    move(app.linePauseInput, gap + px(370), controlsY + px(25), px(130), px(32));
    move(app.start, r.right - gap - px(225), controlsY + px(21), px(108), px(39));
    move(app.stop, r.right - gap - px(108), controlsY + px(21), px(108), px(39));
    move(app.compare, gap, controlsY + px(72), px(170), px(36));
    move(app.status, gap + px(190), controlsY + px(70), width - px(190), px(45));
    move(app.progress, gap, controlsY + px(125), width, px(8));
    move(app.help, gap, controlsY + px(145), width, px(60));
}
void createFonts() {
    if (app.normal) DeleteObject(app.normal);
    if (app.heading) DeleteObject(app.heading);
    if (app.small) DeleteObject(app.small);
    app.normal = CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    app.heading = CreateFontW(-px(25), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    app.small = CreateFontW(-px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    const HWND controls[] = {app.subtitle, app.textLabel, app.text, app.delayLabel,
        app.delay, app.speedLabel, app.speed, app.linePauseLabel, app.linePauseInput,
        app.start, app.stop, app.compare, app.status};
    for (HWND h : controls) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(app.normal), TRUE);
    SendMessageW(app.title, WM_SETFONT, reinterpret_cast<WPARAM>(app.heading), TRUE);
    SendMessageW(app.author, WM_SETFONT, reinterpret_cast<WPARAM>(app.small), TRUE);
    SendMessageW(app.count, WM_SETFONT, reinterpret_cast<WPARAM>(app.small), TRUE);
    SendMessageW(app.help, WM_SETFONT, reinterpret_cast<WPARAM>(app.small), TRUE);
    SendMessageW(app.text, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(px(10), px(10)));
}
void updateCount() {
    const int n = GetWindowTextLengthW(app.text);
    setText(app.count, std::to_wstring(n) + L" characters");
}
void setBusy(bool busy) {
    SendMessageW(app.text, EM_SETREADONLY, busy, 0);
    EnableWindow(app.delay, !busy);
    EnableWindow(app.speed, !busy);
    EnableWindow(app.linePauseInput, !busy);
    EnableWindow(app.compare, !busy);
    EnableWindow(app.start, !busy);
    EnableWindow(app.stop, busy);
}
void finish(const std::wstring& message) {
    app.state = State::Idle;
    CancelWaitableTimer(app.timer);
    if (app.hotkeyRegistered) UnregisterHotKey(app.window, kHotkey);
    app.hotkeyRegistered = false;
    app.target = nullptr;
    app.targetFocus = nullptr;
    app.plan.clear();
    setBusy(false);
    status(message);
    setText(app.window, L"Auto Typer");
    // Do not activate the app or move keyboard focus after typing finishes.
}
bool isDown(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }
bool modifiersHeld() {
    return isDown(VK_CONTROL) || isDown(VK_MENU) || isDown(VK_SHIFT) ||
           isDown(VK_LWIN) || isDown(VK_RWIN);
}
HWND focusOf(HWND foreground) {
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    const DWORD thread = GetWindowThreadProcessId(foreground, nullptr);
    return thread && GetGUIThreadInfo(thread, &info) ? info.hwndFocus : nullptr;
}
bool ownsWindow(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
}
bool parseNumber(HWND input, unsigned min, unsigned max, unsigned& out) {
    const auto text = readText(input);
    if (text.empty() || text.size() > 6) return false;
    unsigned value = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9') return false;
        value = value * 10 + static_cast<unsigned>(c - L'0');
    }
    if (value < min || value > max) return false;
    out = value;
    return true;
}
void begin() {
    if (app.state != State::Idle) return;
    unsigned seconds = 0, interval = 0, linePause = 0;
    if (!parseNumber(app.delay, 1, 3600, seconds)) {
        status(L"Enter a delay between 1 and 3600 seconds.");
        SetFocus(app.delay);
        return;
    }
    if (!parseNumber(app.speed, 1, 1000, interval)) {
        status(L"Enter an interval between 1 and 1000 ms per character.");
        SetFocus(app.speed);
        return;
    }
    if (!parseNumber(app.linePauseInput, 0, 5000, linePause)) {
        status(L"Enter a line pause between 0 and 5000 ms.");
        SetFocus(app.linePauseInput);
        return;
    }
    const auto text = readText(app.text);
    if (text.empty()) {
        status(L"Paste or type the text you want to send first.");
        SetFocus(app.text);
        return;
    }
    std::u16string utf16;
    utf16.reserve(text.size());
    for (wchar_t c : text) utf16.push_back(static_cast<char16_t>(c));
    auto plan = typer::makePlan(utf16);
    if (!plan) {
        const auto detail = plan.error == "control" ? L"an unsupported control character" :
                                                     L"an invalid Unicode sequence";
        status(L"The text contains " + std::wstring(detail) + L" at position " +
               std::to_wstring(plan.errorOffset + 1) + L".");
        SetFocus(app.text);
        return;
    }
    app.lastRunText = utf16;
    app.hasLastRun = true;
    app.plan = std::move(plan.strokes);
    app.index = 0;
    app.interval = interval;
    app.linePause = linePause;
    app.deadline = clockMs() + static_cast<ULONGLONG>(seconds) * 1000;
    app.lastStatus = 0;
    app.state = State::Countdown;
    app.hotkeyRegistered = RegisterHotKey(app.window, kHotkey, MOD_NOREPEAT, VK_F8) != FALSE;
    setBusy(true);
    SendMessageW(app.progress, PBM_SETPOS, 0, 0);
    if (!armTimer()) {
        finish(L"Could not start the timer. Please try again.");
        return;
    }
    status(L"Starting in " + std::to_wstring(seconds) +
        L" s. Click the target text field. Press Esc to stop.");
}

bool captureTarget() {
    const HWND target = GetForegroundWindow();
    if (!target || ownsWindow(target)) {
        finish(L"Cancelled: click a text field in another application during the countdown.");
        return false;
    }
    if (modifiersHeld() || isDown(VK_LBUTTON) || isDown(VK_RBUTTON)) {
        finish(L"Cancelled: release Ctrl, Alt, Shift, Windows and all mouse buttons before typing starts.");
        return false;
    }
    const HWND focused = focusOf(target);
    if (!focused || !IsWindow(focused)) {
        finish(L"Cancelled: no target control was found. Click inside the target text field.");
        return false;
    }
    app.target = target;
    app.targetFocus = focused;
    app.targetName = readText(target);
    if (app.targetName.size() > 75) app.targetName = app.targetName.substr(0, 72) + L"...";
    app.state = State::Typing;
    app.nextStroke = clockMs();
    app.lastStatus = 0;
    return true;
}

bool sendStroke(const typer::Stroke& stroke) {
    INPUT inputs[4]{};
    UINT count = 0;
    auto add = [&](WORD value, bool unicode, bool up) {
        auto& input = inputs[count++];
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = unicode ? 0 : value;
        input.ki.wScan = unicode ? value : 0;
        input.ki.dwFlags = (unicode ? KEYEVENTF_UNICODE : 0) | (up ? KEYEVENTF_KEYUP : 0);
    };
    if (stroke.kind == typer::Kind::Unicode) {
        add(stroke.first, true, false);
        add(stroke.first, true, true);
        if (stroke.units == 2) {
            add(stroke.second, true, false);
            add(stroke.second, true, true);
        }
    } else {
        const WORD key = stroke.kind == typer::Kind::Enter ? VK_RETURN : VK_TAB;
        add(key, false, false);
        add(key, false, true);
    }
    const UINT sent = SendInput(count, inputs, sizeof(INPUT));
    if (sent == count) return true;
    // If a partial batch ended on a key-down, release just that key. Never retry
    // text, as some of it may already have reached the destination.
    if (sent > 0 && sent < count && !(inputs[sent - 1].ki.dwFlags & KEYEVENTF_KEYUP)) {
        INPUT release = inputs[sent - 1];
        release.ki.dwFlags |= KEYEVENTF_KEYUP;
        SendInput(1, &release, sizeof(INPUT));
    }
    finish(L"Windows blocked the input. Make sure the target does not run with higher privileges than this app.");
    return false;
}

void tick() {
    if (app.state == State::Idle) return;
    if (isDown(VK_ESCAPE)) {
        finish(L"Stopped with Esc.");
        return;
    }
    const ULONGLONG now = clockMs();
    if (app.state == State::Countdown) {
        if (now < app.deadline) {
            const auto seconds = (app.deadline - now + 999) / 1000;
            status(L"Starting in " + std::to_wstring(seconds) +
                L" s. Click the target text field. Press Esc to stop.");
            setText(app.window, L"(" + std::to_wstring(seconds) + L") Auto Typer");
            return;
        }
        if (!captureTarget()) return;
    }
    if (app.state != State::Typing) return;
    if (!IsWindow(app.target) || GetForegroundWindow() != app.target ||
        focusOf(app.target) != app.targetFocus) {
        finish(L"Stopped: the target window or control changed.");
        return;
    }
    if (modifiersHeld() || isDown(VK_LBUTTON) || isDown(VK_RBUTTON)) {
        finish(L"Stopped: a modifier key or mouse button was pressed.");
        return;
    }
    if (now < app.nextStroke) return;
    if (app.index >= app.plan.size()) {
        finish(L"Sent " + std::to_wstring(app.index) +
            L" keystrokes. Delivery is not verified; use Compare output.");
        return;
    }
    const bool endedLine = app.plan[app.index].kind == typer::Kind::Enter;
    if (!sendStroke(app.plan[app.index])) return;
    ++app.index;
    // Start every interval after the successful SendInput call. No catch-up.
    app.nextStroke = clockMs() + app.interval + (endedLine ? app.linePause : 0);
    const auto total = app.plan.size();
    SendMessageW(app.progress, PBM_SETPOS, static_cast<WPARAM>(app.index * 1000 / total), 0);
    if (now - app.lastStatus >= 100 || app.index == total) {
        status(L"Typing: " + std::to_wstring(app.index) + L" / " + std::to_wstring(total) +
            L" · Esc" + (app.hotkeyRegistered ? std::wstring(L" or F8") : std::wstring()) +
            L" to stop.\r\n" + app.targetName);
        app.lastStatus = now;
    }
    if (app.index == total) {
        finish(L"Sent " + std::to_wstring(total) +
            L" keystrokes. Delivery is not verified; use Compare output.");
    }
}

struct CompareDialog {
    HWND window{}, label{}, actual{}, compare{}, close{}, result{};
    HFONT font{};
    int dpi = 96;
    std::u16string expected;
    bool usesLastRun = false;
};

int comparePx(const CompareDialog& dialog, int value) { return MulDiv(value, dialog.dpi, 96); }
std::u16string toUtf16(const std::wstring& text) {
    std::u16string result;
    result.reserve(text.size());
    for (wchar_t c : text) result.push_back(static_cast<char16_t>(c));
    return result;
}
void layoutCompare(CompareDialog& dialog) {
    RECT r{};
    GetClientRect(dialog.window, &r);
    const int margin = comparePx(dialog, 24), width = r.right - margin * 2;
    auto move = [](HWND control, int x, int y, int w, int h) {
        MoveWindow(control, x, y, std::max(1, w), std::max(1, h), TRUE);
    };
    move(dialog.label, margin, comparePx(dialog, 20), width, comparePx(dialog, 48));
    move(dialog.actual, margin, comparePx(dialog, 78), width, r.bottom - comparePx(dialog, 238));
    move(dialog.compare, margin, r.bottom - comparePx(dialog, 142), comparePx(dialog, 130), comparePx(dialog, 36));
    move(dialog.close, r.right - margin - comparePx(dialog, 108), r.bottom - comparePx(dialog, 142),
        comparePx(dialog, 108), comparePx(dialog, 36));
    move(dialog.result, margin, r.bottom - comparePx(dialog, 94), width, comparePx(dialog, 73));
}
void setCompareFont(CompareDialog& dialog) {
    if (dialog.font) DeleteObject(dialog.font);
    dialog.font = CreateFontW(-comparePx(dialog, 15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    for (HWND control : {dialog.label, dialog.actual, dialog.compare, dialog.close, dialog.result})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(dialog.font), TRUE);
    SendMessageW(dialog.actual, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
        MAKELPARAM(comparePx(dialog, 10), comparePx(dialog, 10)));
}
void comparePastedOutput(CompareDialog& dialog) {
    const auto result = typer::compareText(dialog.expected, toUtf16(readText(dialog.actual)));
    if (result.equal) {
        setText(dialog.result, L"Match: the pasted output equals the reference text.\r\n"
            L"Only line endings were normalized; spaces, tabs and the final newline match exactly.\r\n"
            L"Length: " + std::to_wstring(result.expectedLength) + L" UTF-16 units after newline normalization.");
    } else {
        setText(dialog.result, L"Mismatch at line " + std::to_wstring(result.line) +
            L", column " + std::to_wstring(result.column) + L".\r\nExpected length: " +
            std::to_wstring(result.expectedLength) + L"; pasted length: " +
            std::to_wstring(result.actualLength) + L" UTF-16 units (line endings normalized).\r\n"
            L"Spaces, tabs and the final newline are compared exactly.");
    }
}
LRESULT CALLBACK compareWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* dialog = reinterpret_cast<CompareDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        dialog = static_cast<CompareDialog*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        dialog->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
    }
    if (!dialog) return DefWindowProcW(hwnd, message, wParam, lParam);
    switch (message) {
    case WM_CREATE: {
        HDC dc = GetDC(hwnd);
        dialog->dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(hwnd, dc);
        auto make = [hwnd](const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0) {
            return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        };
        dialog->label = make(L"STATIC", dialog->usesLastRun ?
            L"Reference: text captured by the last Start, even if the main text was edited later.\r\n"
            L"Copy the entire output from your target application, then paste it below." :
            L"Reference: the current main text, captured when this comparison opened.\r\n"
            L"Copy the entire output from your target application, then paste it below.", SS_LEFT, 0);
        dialog->actual = make(L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_AUTOHSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL, 201, WS_EX_CLIENTEDGE);
        // A transformed output may be much longer than the input. Detect any
        // edit-control limit instead of silently treating truncated text as valid.
        SendMessageW(dialog->actual, EM_SETLIMITTEXT, kMaxText * 4, 0);
        dialog->compare = make(L"BUTTON", L"Compare", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
        dialog->close = make(L"BUTTON", L"Close", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL);
        dialog->result = make(L"STATIC",
            L"Only line endings are normalized. Spaces, tabs and the final newline must match exactly.", SS_LEFT, 0);
        setCompareFont(*dialog);
        layoutCompare(*dialog);
        return 0;
    }
    case WM_SIZE:
        layoutCompare(*dialog);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = comparePx(*dialog, 780);
        info->ptMinTrackSize.y = comparePx(*dialog, 540);
        return 0;
    }
    case WM_DPICHANGED: {
        dialog->dpi = HIWORD(wParam);
        setCompareFont(*dialog);
        const auto* rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
            rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layoutCompare(*dialog);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK && HIWORD(wParam) == BN_CLICKED && IsWindowEnabled(dialog->compare))
            comparePastedOutput(*dialog);
        if (LOWORD(wParam) == IDCANCEL) DestroyWindow(hwnd);
        if (LOWORD(wParam) == 201 && HIWORD(wParam) == EN_CHANGE) {
            if (IsWindowEnabled(dialog->compare))
                setText(dialog->result, L"Output changed. Click Compare to check it against the reference text.");
            else if (GetWindowTextLengthW(dialog->actual) == 0) {
                EnableWindow(dialog->compare, TRUE);
                setText(dialog->result, L"Paste the complete output, then click Compare.");
            }
        }
        if (LOWORD(wParam) == 201 && (HIWORD(wParam) == EN_MAXTEXT || HIWORD(wParam) == EN_ERRSPACE)) {
            EnableWindow(dialog->compare, FALSE);
            setText(dialog->result, L"The pasted output could not be kept in full. Comparison is disabled.\r\n"
                L"Clear this field and paste again (limit: 4,000,000 UTF-16 units).");
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), kInk);
        SetBkColor(reinterpret_cast<HDC>(wParam), kBackground);
        return reinterpret_cast<LRESULT>(app.background);
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wParam), kInk);
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(app.white);
    case WM_ERASEBKGND: {
        RECT r{};
        GetClientRect(hwnd, &r);
        FillRect(reinterpret_cast<HDC>(wParam), &r, app.background);
        return 1;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        DeleteObject(dialog->font);
        dialog->font = nullptr;
        dialog->window = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
void showCompare() {
    if (app.state != State::Idle) return;
    CompareDialog dialog;
    dialog.expected = app.hasLastRun ? app.lastRunText : toUtf16(readText(app.text));
    dialog.usesLastRun = app.hasLastRun;
    dialog.dpi = app.dpi;
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kCompareClass,
        L"Compare output - Auto Typer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, px(900), px(650), app.window, nullptr,
        GetModuleHandleW(nullptr), &dialog);
    if (!window) { status(L"Could not open Compare output."); return; }
    EnableWindow(app.window, FALSE);
    ShowWindow(window, SW_SHOW);
    SetFocus(dialog.actual);
    MSG message{};
    int result = 1;
    while (dialog.window && (result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (dialog.window) DestroyWindow(dialog.window);
    if (IsWindow(app.window)) {
        EnableWindow(app.window, TRUE);
        SetActiveWindow(app.window);
    }
    if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        app.window = hwnd;
        HDC dc = GetDC(hwnd);
        app.dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(hwnd, dc);
        app.background = CreateSolidBrush(kBackground);
        app.white = CreateSolidBrush(RGB(255, 255, 255));
        app.title = makeControl(L"STATIC", L"Auto Typer", SS_LEFT);
        app.author = makeControl(L"STATIC", L"By Eduard Serra Tuneu", SS_RIGHT);
        app.subtitle = makeControl(L"STATIC",
            L"Paste your text, press Start, then click where you want to type during the countdown.", SS_LEFT);
        app.textLabel = makeControl(L"STATIC", L"Text to type", SS_LEFT);
        app.count = makeControl(L"STATIC", L"0 characters", SS_RIGHT);
        app.text = makeControl(L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_WANTRETURN | WS_VSCROLL, kText, WS_EX_CLIENTEDGE);
        SendMessageW(app.text, EM_SETLIMITTEXT, kMaxText, 0);
        app.delayLabel = makeControl(L"STATIC", L"Delay (seconds)", SS_LEFT);
        app.delay = makeControl(L"EDIT", L"5", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
            kDelay, WS_EX_CLIENTEDGE);
        SendMessageW(app.delay, EM_SETLIMITTEXT, 4, 0);
        app.speedLabel = makeControl(L"STATIC", L"Interval (ms / character)", SS_LEFT);
        app.speed = makeControl(L"EDIT", L"50", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
            kSpeed, WS_EX_CLIENTEDGE);
        SendMessageW(app.speed, EM_SETLIMITTEXT, 4, 0);
        app.linePauseLabel = makeControl(L"STATIC", L"Line pause (ms)", SS_LEFT);
        app.linePauseInput = makeControl(L"EDIT", L"250", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
            kLinePause, WS_EX_CLIENTEDGE);
        SendMessageW(app.linePauseInput, EM_SETLIMITTEXT, 4, 0);
        app.compare = makeControl(L"BUTTON", L"Compare output", WS_TABSTOP | BS_PUSHBUTTON, kCompare);
        app.start = makeControl(L"BUTTON", L"Start", WS_TABSTOP | BS_DEFPUSHBUTTON, kStart);
        app.stop = makeControl(L"BUTTON", L"Stop", WS_TABSTOP | BS_PUSHBUTTON, kStop);
        EnableWindow(app.stop, FALSE);
        app.status = makeControl(L"STATIC", L"Ready. Your text will be typed one character at a time.", SS_LEFT);
        app.progress = makeControl(PROGRESS_CLASSW, L"", PBS_SMOOTH);
        SendMessageW(app.progress, PBM_SETRANGE32, 0, 1000);
        SendMessageW(app.progress, PBM_SETBARCOLOR, 0, RGB(31, 104, 216));
        app.help = makeControl(L"STATIC",
            L"Code editors: disable auto-indent and autocomplete before typing.\r\n"
            L"Esc stops typing. Enter and Tab may trigger actions. Avoid using the keyboard or mouse while typing.\r\n"
            L"Offline. Text stays in memory only. Compare output checks the text you paste back.", SS_LEFT);
        createFonts();
        layout();
        return 0;
    }
    case WM_SIZE:
        layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = px(820);
        info->ptMinTrackSize.y = px(620);
        return 0;
    }
    case WM_DPICHANGED: {
        app.dpi = HIWORD(wParam);
        createFonts();
        const auto* rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
            rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kText && HIWORD(wParam) == EN_CHANGE) updateCount();
        if (LOWORD(wParam) == kStart && HIWORD(wParam) == BN_CLICKED) begin();
        if (LOWORD(wParam) == kStop && HIWORD(wParam) == BN_CLICKED) finish(L"Stopped.");
        if (LOWORD(wParam) == kCompare && HIWORD(wParam) == BN_CLICKED) showCompare();
        return 0;
    case WM_HOTKEY:
        if (wParam == kHotkey && app.state != State::Idle) finish(L"Stopped with F8.");
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND control = reinterpret_cast<HWND>(lParam);
        SetTextColor(dc, control == app.help || control == app.count || control == app.subtitle || control == app.author ? kMuted : kInk);
        SetBkColor(dc, control == app.text ? RGB(255, 255, 255) : kBackground);
        return reinterpret_cast<LRESULT>(control == app.text ? app.white : app.background);
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kInk);
        SetBkColor(dc, RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(app.white);
    }
    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect, app.background);
        return 1;
    }
    case WM_CLOSE:
        if (app.state != State::Idle) finish(L"Stopped.");
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        CancelWaitableTimer(app.timer);
        if (app.hotkeyRegistered) UnregisterHotKey(hwnd, kHotkey);
        DeleteObject(app.normal);
        DeleteObject(app.heading);
        DeleteObject(app.small);
        DeleteObject(app.background);
        DeleteObject(app.white);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    // Resolve newer DPI support dynamically to retain compatibility.
    using DpiFunction = BOOL(WINAPI*)(HANDLE);
    auto dpiFunction = reinterpret_cast<DpiFunction>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (dpiFunction) dpiFunction(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
    else SetProcessDPIAware();
    using CreateTimerFunction = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
    auto createTimer = reinterpret_cast<CreateTimerFunction>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateWaitableTimerExW"));
    // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Windows 10 1803+) is resolved
    // dynamically and falls back to a normal auto-reset waitable timer.
    if (createTimer) app.timer = createTimer(nullptr, nullptr, 0x00000002, TIMER_ALL_ACCESS);
    if (!app.timer) app.timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!app.timer) {
        MessageBoxW(nullptr, L"Could not create the typing timer.",
            L"Auto Typer", MB_OK | MB_ICONERROR);
        return 1;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc)) { CloseHandle(app.timer); return 1; }
    wc.lpfnWndProc = compareWindowProc;
    wc.lpszClassName = kCompareClass;
    if (!RegisterClassExW(&wc)) { CloseHandle(app.timer); return 1; }
    HDC dc = GetDC(nullptr);
    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    app.dpi = dpi;
    HWND window = CreateWindowExW(0, kClass, L"Auto Typer", WS_OVERLAPPEDWINDOW |
        WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, px(900), px(740), nullptr, nullptr,
        instance, nullptr);
    if (!window) { CloseHandle(app.timer); return 1; }
    ShowWindow(window, show);
    UpdateWindow(window);
    SetFocus(app.text);
    int exitCode = 0;
    bool running = true;
    while (running) {
        const DWORD wait = MsgWaitForMultipleObjectsEx(1, &app.timer, INFINITE,
            QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_FAILED) {
            finish(L"A timer error occurred.");
            DestroyWindow(window);
            exitCode = 1;
            break;
        }
        // Drain input first so Stop/F8/closing are acted on before another stroke.
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                exitCode = static_cast<int>(message.wParam);
                running = false;
                break;
            }
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (running && wait == WAIT_OBJECT_0 && app.state != State::Idle) {
            tick();
            if (!armTimer()) finish(L"Could not continue the typing timer.");
        }
    }
    CloseHandle(app.timer);
    return exitCode;
}
