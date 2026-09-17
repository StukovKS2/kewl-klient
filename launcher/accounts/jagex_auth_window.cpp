#include "jagex_auth_window.hpp"

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <filesystem>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

struct JagexAuthWindow::Impl : std::enable_shared_from_this<JagexAuthWindow::Impl> {
    HWND hwnd = nullptr;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    std::wstring profile;
    std::string pendingUrl;
    RedirectCallback redirect;
    ClosedCallback closed;
    JagexAuthWindow* owner = nullptr;

    static LRESULT CALLBACK WindowProc(HWND h, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = reinterpret_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd = h;
        }
        if (!self) return DefWindowProcW(h, message, w, l);
        if (message == WM_SIZE && self->controller) { RECT r{}; GetClientRect(h, &r); self->controller->put_Bounds(r); return 0; }
        if (message == WM_CLOSE) { if (self->closed) self->closed(); DestroyWindow(h); return 0; }
        if (message == WM_DESTROY) {
            self->hwnd = nullptr;
            if (self->owner) self->owner->hwnd_ = nullptr;
            return 0;
        }
        return DefWindowProcW(h, message, w, l);
    }

    bool createWindow(std::string& error) {
        static const wchar_t klass[] = L"KewlKlientJagexAuth";
        static bool registered = false;
        if (!registered) {
            WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = klass; wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(IDC_ARROW));
            if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { error = "cannot register Jagex authentication window"; return false; }
            registered = true;
        }
        hwnd = CreateWindowExW(0, klass, L"Sign in with Jagex", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               CW_USEDEFAULT, CW_USEDEFAULT, 980, 760, nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd) { error = "cannot create Jagex authentication window"; return false; }
        return true;
    }
    bool initializeWebView(std::string& error) {
        const std::weak_ptr<Impl> weak = shared_from_this();
        auto envHandler = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [weak](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                auto self = weak.lock();
                if (!self) return S_OK;
                if (FAILED(result) || !env) return result;
                self->environment = env;
                self->environment->CreateCoreWebView2Controller(self->hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [weak](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
                            auto self = weak.lock();
                            if (!self) return S_OK;
                            if (FAILED(hr) || !controller) return hr;
                            self->controller = controller;
                            controller->get_CoreWebView2(&self->webview);
                            RECT r{}; GetClientRect(self->hwnd, &r); controller->put_Bounds(r);
                            self->webview->add_NavigationStarting(
                                Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                        auto self = weak.lock();
                                        if (!self) return S_OK;
                                        LPWSTR uri = nullptr;
                                        if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                                            std::wstring value(uri); CoTaskMemFree(uri);
                                            if (self->redirect && self->redirect(value)) args->put_Cancel(TRUE);
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);
                            if (!self->pendingUrl.empty()) {
                                self->webview->Navigate(std::wstring(self->pendingUrl.begin(), self->pendingUrl.end()).c_str());
                                self->pendingUrl.clear();
                            }
                            return S_OK;
                        }).Get());
                return S_OK;
            });
        const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, profile.c_str(), nullptr, envHandler.Get());
        if (FAILED(hr)) {
            error = "WebView2 environment creation failed (HRESULT " + std::to_string(static_cast<unsigned long>(hr)) + ")";
            return false;
        }
        return true;
    }
};

JagexAuthWindow::JagexAuthWindow() : impl_(std::make_shared<Impl>()) { impl_->owner = this; }
JagexAuthWindow::~JagexAuthWindow() { Close(); }
bool JagexAuthWindow::Open(const std::wstring& profile, RedirectCallback callback, ClosedCallback closed, std::string& error) {
    if (IsOpen()) { error = "Jagex authentication is already open"; return false; }
    impl_->profile = profile; impl_->redirect = std::move(callback); impl_->closed = std::move(closed);
    if (!impl_->createWindow(error)) return false;
    hwnd_ = impl_->hwnd;
    if (!impl_->initializeWebView(error)) {
        Close();
        return false;
    }
    return true;
}
bool JagexAuthWindow::Navigate(const std::string& url, std::string& error) {
    if (!IsOpen()) { error = "Jagex authentication window is not open"; return false; }
    impl_->pendingUrl = url;
    if (impl_->webview) { std::wstring wide(url.begin(), url.end()); HRESULT hr = impl_->webview->Navigate(wide.c_str()); if (FAILED(hr)) { error = "WebView2 navigation failed"; return false; } impl_->pendingUrl.clear(); }
    return true;
}
void JagexAuthWindow::Close() {
    if (!impl_) return;
    if (impl_->controller) impl_->controller->Close();
    if (impl_->hwnd) DestroyWindow(impl_->hwnd);
    impl_->webview.Reset(); impl_->controller.Reset(); impl_->environment.Reset();
    std::error_code ec; if (!impl_->profile.empty()) std::filesystem::remove_all(impl_->profile, ec);
    impl_->profile.clear(); hwnd_ = nullptr;
}
