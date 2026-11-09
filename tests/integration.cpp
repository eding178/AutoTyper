#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// Run as a console application on an interactive Windows desktop (or Wine/Xvfb).
// This harness uses window messages only to configure the source application.
// Text received by the target edit controls must arrive through actual SendInput.
namespace {
HWND source{}, target1{}, edit1{}, edit2{}, target2{}, edit3{};
ULONGLONG firstChange = 0, lastChange = 0;
int failures = 0, assertions = 0;

ULONGLONG now() { return GetTickCount64(); }

void pump(DWORD milliseconds) {
    const auto deadline = now() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        const auto current = now();
        if (current >= deadline) break;
        MsgWaitForMultipleObjects(0, nullptr, FALSE,
            static_cast<DWORD>(std::min<ULONGLONG>(deadline - current, 10)), QS_ALLINPUT);
    } while (true);
}

bool until(const std::function<bool()>& condition, DWORD timeout) {
    const auto deadline = now() + timeout;
    do {
        pump(5);
        if (condition()) return true;
    } while (now() < deadline);
    return condition();
}

void check(bool result, const char* name) {
    ++assertions;
    if (!result) ++failures;
    std::printf("%s %s\n", result ? "PASS" : "FAIL", name);
    std::fflush(stdout);
}

std::wstring textOf(HWND handle) {
    const auto length = static_cast<std::size_t>(SendMessageW(handle, WM_GETTEXTLENGTH, 0, 0));
    std::wstring text(length + 1, L'\0');
    const auto copied = SendMessageW(handle, WM_GETTEXT,
        static_cast<WPARAM>(text.size()), reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<std::size_t>(copied));
    return text;
}

void set(HWND handle, const wchar_t* text) {
    SendMessageW(handle, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text));
}

bool activate(HWND window, HWND focus) {
    const DWORD current = GetCurrentThreadId();
    const DWORD destination = GetWindowThreadProcessId(window, nullptr);
    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const bool attachedForeground = foregroundThread && foregroundThread != current &&
        AttachThreadInput(current, foregroundThread, TRUE);
    const bool attachedDestination = destination != current && destination != foregroundThread &&
        AttachThreadInput(current, destination, TRUE);
    ShowWindow(window, SW_SHOW);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    SetFocus(focus);
    if (attachedDestination) AttachThreadInput(current, destination, FALSE);
    if (attachedForeground) AttachThreadInput(current, foregroundThread, FALSE);
    pump(30);
    return GetForegroundWindow() == window;
}

LRESULT CALLBACK targetProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND && HIWORD(wParam) == EN_CHANGE &&
        reinterpret_cast<HWND>(lParam) == edit1) {
        if (!firstChange) firstChange = now();
        lastChange = now();
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool createTargets() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = targetProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TyperIntegrationTarget";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc)) return false;
    target1 = CreateWindowW(wc.lpszClassName, L"Integration target 1",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60, 60, 650, 380, nullptr, nullptr, wc.hInstance, nullptr);
    target2 = CreateWindowW(wc.lpszClassName, L"Integration target 2",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 760, 80, 400, 300, nullptr, nullptr, wc.hInstance, nullptr);
    const auto makeEdit = [&](HWND parent, int y, int id) {
        return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE |
            ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL,
            15, y, parent == target1 ? 600 : 350, 130, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), wc.hInstance, nullptr);
    };
    edit1 = makeEdit(target1, 15, 201);
    edit2 = makeEdit(target1, 170, 202);
    edit3 = makeEdit(target2, 15, 203);
    return target1 && target2 && edit1 && edit2 && edit3;
}

bool idle() { return IsWindowEnabled(GetDlgItem(source, 104)) != FALSE; }

void stop() {
    SendMessageW(GetDlgItem(source, 105), BM_CLICK, 0, 0);
    pump(30);
}

void prepare(const wchar_t* text, const wchar_t* delay = L"1", const wchar_t* speed = L"35") {
    if (!idle()) stop();
    set(GetDlgItem(source, 101), text);
    set(GetDlgItem(source, 102), delay);
    set(GetDlgItem(source, 103), speed);
    set(edit1, L"");
    set(edit2, L"");
    set(edit3, L"");
    pump(10);
    firstChange = lastChange = 0;
}

ULONGLONG start() {
    const auto began = now();
    SendMessageW(GetDlgItem(source, 104), BM_CLICK, 0, 0);
    return began;
}

bool key(WORD virtualKey, bool up) {
    INPUT event{};
    event.type = INPUT_KEYBOARD;
    event.ki.wVk = virtualKey;
    event.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    return SendInput(1, &event, sizeof(event)) == 1;
}

void escape() {
    check(key(VK_ESCAPE, false), "Escape key-down injected");
    pump(80); // Longer than the app's cancellation polling interval.
    check(key(VK_ESCAPE, true), "Escape key-up injected");
    pump(30);
}

std::wstring utf8(const char* value) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, -1, wide.data(), size);
    wide.resize(static_cast<std::size_t>(size - 1));
    return wide;
}
} // namespace

int main(int argc, char** argv) {
    SetProcessDPIAware();
    PROCESS_INFORMATION process{};
    bool launched = false;
    if (argc > 1) {
        std::wstring command = L"\"" + utf8(argv[1]) + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
            nullptr, nullptr, &startup, &process) != FALSE;
        if (!launched) {
            std::printf("FAIL launch app, error %lu\n", GetLastError());
            return 2;
        }
        AllowSetForegroundWindow(process.dwProcessId);
    }
    if (!until([] { source = FindWindowW(L"TecladoAutomaticoWindow", nullptr);
            return source && GetDlgItem(source, 104); }, 10000)) {
        std::printf("FAIL app window not found\n");
        return 2;
    }
    if (!createTargets()) {
        std::printf("FAIL test target setup\n");
        return 2;
    }
    check(activate(target1, edit1), "target window can receive focus");

    // Exact Unicode and multiline input, delay, approximate character pacing.
    const wchar_t* sample = L"\u00e1\u00f1\u20ac\u4e2d\u6587\U0001f600\r\nFin";
    prepare(sample);
    const auto began = start();
    check(!idle(), "Start begins countdown");
    check(activate(target1, edit1), "focus target during countdown");
    pump(650);
    check(textOf(edit1).empty(), "no input before one-second countdown");
    check(until([] { return idle(); }, 5000), "Unicode run completes");
    pump(100);
    check(textOf(edit1) == sample, "Unicode, surrogate pair and CRLF received exactly");
    check(firstChange >= began + 900, "first input respects configured delay");
    check(lastChange >= firstChange + 200 && lastChange <= firstChange + 2500,
        "35 ms interval is paced rather than pasted");
    check(GetForegroundWindow() == target1 && GetFocus() == edit1,
        "completion preserves destination focus");

    // Countdown cancellation sends no text later.
    prepare(L"must not appear");
    start();
    activate(target1, edit1);
    pump(120);
    escape();
    check(idle(), "Escape cancels countdown");
    pump(1100);
    check(textOf(edit1).empty(), "cancelled countdown sends no text");

    // Mid-stream cancellation does not emit a trailing queued portion.
    prepare(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", L"1", L"35");
    start();
    activate(target1, edit1);
    check(until([] { return textOf(edit1).size() >= 3; }, 3500), "typing starts before Escape test");
    escape();
    const auto escaped = textOf(edit1);
    check(idle() && escaped.size() >= 3 && escaped.size() < 52,
        "Escape interrupts an in-progress run");
    pump(300);
    check(textOf(edit1) == escaped, "no input after Escape cancellation");

    // Foreground changes stop delivery to either target.
    prepare(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
    start();
    activate(target1, edit1);
    check(until([] { return textOf(edit1).size() >= 3; }, 3500), "typing starts before window-switch test");
    check(activate(target2, edit3), "switch to second target window");
    check(until([] { return idle(); }, 700), "window change stops typing");
    const auto switched = textOf(edit1);
    pump(250);
    check(textOf(edit1) == switched && textOf(edit3).empty(), "window change causes no further delivery");

    // A different native control within the same foreground window also stops.
    prepare(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
    start();
    activate(target1, edit1);
    check(until([] { return textOf(edit1).size() >= 3; }, 3500), "typing starts before control-switch test");
    SetFocus(edit2);
    check(until([] { return idle(); }, 700), "same-window focus change stops typing");
    pump(100);
    check(textOf(edit2).empty(), "same-window second edit receives no text");

    // Forgetting to select another application does not type into the source.
    prepare(L"source stays intact");
    start();
    check(activate(source, GetDlgItem(source, 101)), "focus source for no-target test");
    check(until([] { return idle(); }, 2500), "source foreground cancels at deadline");
    check(textOf(GetDlgItem(source, 101)) == L"source stays intact" && textOf(edit1).empty(),
        "no-target cancellation preserves source and destination");

    // Invalid controls must reject before starting a countdown.
    for (const auto* delay : {L"0", L"3601", L"abc", L""}) {
        prepare(L"invalid", delay, L"35");
        start();
        check(idle(), "invalid delay rejected without countdown");
    }
    for (const auto* speed : {L"0", L"1001", L"abc", L""}) {
        prepare(L"invalid", L"1", speed);
        start();
        check(idle(), "invalid interval rejected without countdown");
    }

    // The declared fast range must still send every character exactly once.
    prepare(L"01234567890123456789", L"1", L"1");
    start();
    activate(target1, edit1);
    check(until([] { return idle(); }, 3500), "one-millisecond run completes");
    pump(100);
    check(textOf(edit1) == L"01234567890123456789", "fast run sends every character exactly once");

    if (!idle()) stop();
    DestroyWindow(target1);
    DestroyWindow(target2);
    if (launched) {
        PostMessageW(source, WM_CLOSE, 0, 0);
        WaitForSingleObject(process.hProcess, 2000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    std::printf("\n%d assertions, %d failures\n", assertions, failures);
    return failures ? 1 : 0;
}
