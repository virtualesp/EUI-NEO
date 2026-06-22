#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>

#include <d2d1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

constexpr int kInitialWidth = 760;
constexpr int kInitialHeight = 460;
constexpr UINT kPresentSyncInterval = 0;

template <typename T>
void safeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

struct RectF {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

RECT toWinRect(const RectF& rect, int width, int height) {
    RECT result{};
    result.left = std::clamp(static_cast<LONG>(std::floor(rect.x)), 0L, static_cast<LONG>(width));
    result.top = std::clamp(static_cast<LONG>(std::floor(rect.y)), 0L, static_cast<LONG>(height));
    result.right = std::clamp(static_cast<LONG>(std::ceil(rect.x + rect.w)), result.left, static_cast<LONG>(width));
    result.bottom = std::clamp(static_cast<LONG>(std::ceil(rect.y + rect.h)), result.top, static_cast<LONG>(height));
    return result;
}

RectF inflate(const RectF& rect, float amount) {
    return {rect.x - amount, rect.y - amount, rect.w + amount * 2.0f, rect.h + amount * 2.0f};
}

bool contains(const RectF& rect, float x, float y) {
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

bool intersects(const RectF& a, const RectF& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

D2D1_RECT_F toD2DRect(const RectF& rect) {
    return D2D1::RectF(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h);
}

struct Button {
    RectF bounds;
    RectF dirtyBounds;
    const wchar_t* label = L"";
    bool shadow = false;
    bool hover = false;
    bool pressed = false;
};

class DxgiDirtyPresentProbe {
public:
    int run(int argc, char** argv) {
        parseArgs(argc, argv);
        if (!createWindow()) {
            return 1;
        }
        if (!createDeviceResources()) {
            return 1;
        }
        layout();
        invalidate({0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)});
        render();
        if (maxFrames_ >= 0 && presentCount_ >= maxFrames_) {
            destroyDeviceResources();
            return 0;
        }

        MSG msg{};
        while (running_) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running_ = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!running_) {
                break;
            }
            if (!manual_) {
                animateProbe();
            }
            if (dirtyValid_) {
                render();
                if (maxFrames_ >= 0 && presentCount_ >= maxFrames_) {
                    break;
                }
            } else {
                Sleep(1);
            }
        }
        destroyDeviceResources();
        return 0;
    }

private:
    void parseArgs(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--manual") == 0) {
                manual_ = true;
            } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
                maxFrames_ = std::max(1, std::atoi(argv[++i]));
            }
        }
    }

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<DxgiDirtyPresentProbe*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = reinterpret_cast<DxgiDirtyPresentProbe*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
        if (self) {
            return self->handleMessage(message, wParam, lParam);
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_DESTROY:
            running_ = false;
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            width_ = std::max(1, static_cast<int>(LOWORD(lParam)));
            height_ = std::max(1, static_cast<int>(HIWORD(lParam)));
            resizeSwapchain();
            layout();
            invalidate({0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)});
            return 0;
        case WM_MOUSEMOVE:
            updatePointer(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)), false);
            return 0;
        case WM_LBUTTONDOWN:
            SetCapture(hwnd_);
            pointerDown_ = true;
            updatePointer(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)), true);
            return 0;
        case WM_LBUTTONUP:
            pointerDown_ = false;
            ReleaseCapture();
            updatePointer(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)), true);
            return 0;
        case WM_KEYDOWN:
            if (wParam == 'D') {
                dirtyPresent_ = true;
                invalidate({0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)});
            } else if (wParam == 'F') {
                dirtyPresent_ = false;
                invalidate({0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)});
            } else if (wParam == VK_ESCAPE) {
                DestroyWindow(hwnd_);
            }
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    bool createWindow() {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW wc{};
        wc.lpfnWndProc = windowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"EuiDxgiDirtyPresentProbe";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        RECT rect{0, 0, kInitialWidth, kInitialHeight};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd_ = CreateWindowExW(0,
                                wc.lpszClassName,
                                L"DXGI Dirty Present Probe",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                rect.right - rect.left,
                                rect.bottom - rect.top,
                                nullptr,
                                nullptr,
                                instance,
                                this);
        return hwnd_ != nullptr;
    }

    bool createDeviceResources() {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL actualLevel{};
        HRESULT hr = D3D11CreateDevice(nullptr,
                                       D3D_DRIVER_TYPE_HARDWARE,
                                       nullptr,
                                       flags,
                                       levels,
                                       ARRAYSIZE(levels),
                                       D3D11_SDK_VERSION,
                                       &device_,
                                       &actualLevel,
                                       &context_);
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(device_.As(&dxgiDevice)) ||
            FAILED(dxgiDevice->GetAdapter(&adapter)) ||
            FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = static_cast<UINT>(width_);
        desc.Height = static_cast<UINT>(height_);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        desc.Scaling = DXGI_SCALING_STRETCH;

        if (FAILED(factory->CreateSwapChainForHwnd(device_.Get(), hwnd_, &desc, nullptr, nullptr, &swapchain_))) {
            return false;
        }
        factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);

        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_)) ||
            FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &writeFactory_))) {
            return false;
        }
        if (FAILED(writeFactory_->CreateTextFormat(L"Segoe UI",
                                                   nullptr,
                                                   DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                                   DWRITE_FONT_STYLE_NORMAL,
                                                   DWRITE_FONT_STRETCH_NORMAL,
                                                   22.0f,
                                                   L"zh-CN",
                                                   &buttonTextFormat_)) ||
            FAILED(writeFactory_->CreateTextFormat(L"Consolas",
                                                   nullptr,
                                                   DWRITE_FONT_WEIGHT_NORMAL,
                                                   DWRITE_FONT_STYLE_NORMAL,
                                                   DWRITE_FONT_STRETCH_NORMAL,
                                                   15.0f,
                                                   L"en-US",
                                                   &debugTextFormat_))) {
            return false;
        }
        buttonTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        buttonTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        debugTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        debugTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        return createD2DTarget();
    }

    bool createD2DTarget() {
        d2dTarget_.Reset();
        ComPtr<IDXGISurface> surface;
        if (FAILED(swapchain_->GetBuffer(0, IID_PPV_ARGS(&surface)))) {
            return false;
        }
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f,
            96.0f);
        if (FAILED(d2dFactory_->CreateDxgiSurfaceRenderTarget(surface.Get(), &props, &d2dTarget_))) {
            return false;
        }
        return SUCCEEDED(d2dTarget_->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.10f, 0.12f), &backgroundBrush_)) &&
               SUCCEEDED(d2dTarget_->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.42f, 0.88f), &buttonBrush_)) &&
               SUCCEEDED(d2dTarget_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f), &whiteBrush_)) &&
               SUCCEEDED(d2dTarget_->CreateSolidColorBrush(D2D1::ColorF(0.63f, 0.68f, 0.76f), &mutedBrush_)) &&
               SUCCEEDED(d2dTarget_->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f), &shadowBrush_));
    }

    void destroyDeviceResources() {
        shadowBrush_.Reset();
        mutedBrush_.Reset();
        whiteBrush_.Reset();
        buttonBrush_.Reset();
        backgroundBrush_.Reset();
        debugTextFormat_.Reset();
        buttonTextFormat_.Reset();
        d2dTarget_.Reset();
        writeFactory_.Reset();
        safeRelease(d2dFactory_);
        swapchain_.Reset();
        context_.Reset();
        device_.Reset();
    }

    void resizeSwapchain() {
        if (!swapchain_) {
            return;
        }
        d2dTarget_.Reset();
        swapchain_->ResizeBuffers(0, static_cast<UINT>(width_), static_cast<UINT>(height_), DXGI_FORMAT_UNKNOWN, 0);
        createD2DTarget();
    }

    void layout() {
        const float contentW = 640.0f;
        const float gap = 32.0f;
        const float buttonW = (contentW - gap) * 0.5f;
        const float x = (static_cast<float>(width_) - contentW) * 0.5f;
        const float y = std::max(126.0f, static_cast<float>(height_) * 0.34f);
        plain_.bounds = {x, y, buttonW, 112.0f};
        plain_.dirtyBounds = inflate(plain_.bounds, 8.0f);
        plain_.label = L"Plain";
        plain_.shadow = false;
        shadow_.bounds = {x + buttonW + gap, y, buttonW, 112.0f};
        shadow_.dirtyBounds = inflate(shadow_.bounds, 38.0f);
        shadow_.label = L"Shadow";
        shadow_.shadow = true;
    }

    void updatePointer(float x, float y, bool forceDirty) {
        updateButtonState(plain_, x, y, forceDirty);
        updateButtonState(shadow_, x, y, forceDirty);
    }

    void updateButtonState(Button& button, float x, float y, bool forceDirty) {
        const bool nextHover = contains(button.bounds, x, y);
        const bool nextPressed = nextHover && pointerDown_;
        if (button.hover != nextHover || button.pressed != nextPressed || forceDirty) {
            button.hover = nextHover;
            button.pressed = nextPressed;
            invalidate(button.dirtyBounds);
        }
    }

    void animateProbe() {
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - startTime_).count();
        const float cx = static_cast<float>(width_) * 0.5f + static_cast<float>(std::sin(seconds * 2.0) * 168.0);
        const float cy = plain_.bounds.y + plain_.bounds.h * 0.5f;
        pointerDown_ = std::fmod(seconds, 1.1) < 0.55;
        updatePointer(cx, cy, false);
    }

    void invalidate(const RectF& rect) {
        if (!dirtyValid_) {
            dirty_ = rect;
            dirtyValid_ = true;
            return;
        }
        const float left = std::min(dirty_.x, rect.x);
        const float top = std::min(dirty_.y, rect.y);
        const float right = std::max(dirty_.x + dirty_.w, rect.x + rect.w);
        const float bottom = std::max(dirty_.y + dirty_.h, rect.y + rect.h);
        dirty_ = {left, top, right - left, bottom - top};
    }

    void render() {
        if (!d2dTarget_) {
            return;
        }
        const RectF renderRect = dirtyPresent_ ? dirty_ : RectF{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)};
        d2dTarget_->BeginDraw();
        d2dTarget_->PushAxisAlignedClip(toD2DRect(renderRect), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        d2dTarget_->FillRectangle(toD2DRect(renderRect), backgroundBrush_.Get());
        drawScene(renderRect);
        d2dTarget_->PopAxisAlignedClip();
        const HRESULT drawResult = d2dTarget_->EndDraw();
        if (drawResult == D2DERR_RECREATE_TARGET) {
            createD2DTarget();
            invalidate({0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)});
            return;
        }

        DXGI_PRESENT_PARAMETERS params{};
        RECT dirtyRect = toWinRect(renderRect, width_, height_);
        if (dirtyPresent_) {
            params.DirtyRectsCount = 1;
            params.pDirtyRects = &dirtyRect;
        }

        const auto before = std::chrono::steady_clock::now();
        const HRESULT presentResult = swapchain_->Present1(kPresentSyncInterval, 0, &params);
        const auto after = std::chrono::steady_clock::now();
        presentMsAccum_ += std::chrono::duration<double, std::milli>(after - before).count();
        ++presentCount_;
        const int dirtyPixels = std::max(0L, dirtyRect.right - dirtyRect.left) * std::max(0L, dirtyRect.bottom - dirtyRect.top);
        lastDirtyPercent_ = static_cast<double>(dirtyPixels) * 100.0 / static_cast<double>(std::max(1, width_ * height_));
        dirtyValid_ = false;
        updateDebugTitle();

        if (presentResult == DXGI_ERROR_DEVICE_REMOVED || presentResult == DXGI_ERROR_DEVICE_RESET) {
            destroyDeviceResources();
            createDeviceResources();
            invalidate({0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)});
        }
    }

    void drawScene(const RectF& clip) {
        const std::wstring title = L"DXGI Present1 dirty rect probe";
        d2dTarget_->DrawText(title.c_str(),
                             static_cast<UINT32>(title.size()),
                             buttonTextFormat_.Get(),
                             D2D1::RectF(58.0f, 30.0f, static_cast<float>(width_) - 58.0f, 66.0f),
                             whiteBrush_.Get());
        const std::wstring debug = debugLine();
        d2dTarget_->DrawText(debug.c_str(),
                             static_cast<UINT32>(debug.size()),
                             debugTextFormat_.Get(),
                             D2D1::RectF(58.0f, 75.0f, static_cast<float>(width_) - 58.0f, 116.0f),
                             mutedBrush_.Get());
        drawButton(plain_, clip);
        drawButton(shadow_, clip);
    }

    void drawButton(const Button& button, const RectF& clip) {
        if (!intersects(button.dirtyBounds, clip)) {
            return;
        }
        if (button.shadow) {
            for (int i = 5; i >= 1; --i) {
                const float offset = static_cast<float>(i) * 2.6f;
                const float expand = static_cast<float>(i) * 4.0f;
                const RectF shadowRect{
                    button.bounds.x - expand,
                    button.bounds.y + offset - expand,
                    button.bounds.w + expand * 2.0f,
                    button.bounds.h + expand * 2.0f
                };
                d2dTarget_->FillRoundedRectangle(D2D1::RoundedRect(toD2DRect(shadowRect), 26.0f + expand, 26.0f + expand),
                                                 shadowBrush_.Get());
            }
        }
        const D2D1_COLOR_F normal = button.pressed
            ? D2D1::ColorF(0.16f, 0.34f, 0.76f)
            : button.hover ? D2D1::ColorF(0.24f, 0.48f, 0.96f) : D2D1::ColorF(0.20f, 0.42f, 0.88f);
        buttonBrush_->SetColor(normal);
        const RectF body = button.pressed ? RectF{button.bounds.x + 2.0f, button.bounds.y + 2.0f, button.bounds.w - 4.0f, button.bounds.h - 4.0f}
                                          : button.bounds;
        d2dTarget_->FillRoundedRectangle(D2D1::RoundedRect(toD2DRect(body), 22.0f, 22.0f), buttonBrush_.Get());
        d2dTarget_->DrawText(button.label,
                             static_cast<UINT32>(std::wcslen(button.label)),
                             buttonTextFormat_.Get(),
                             toD2DRect(body),
                             whiteBrush_.Get());
    }

    std::wstring debugLine() const {
        wchar_t buffer[256]{};
        const double avgPresentMs = presentCount_ > 0 ? presentMsAccum_ / static_cast<double>(presentCount_) : 0.0;
        std::swprintf(buffer,
                      ARRAYSIZE(buffer),
                      L"Mode %ls | Dirty %.1f%% | Present %.3f ms avg | Count %d | D=dirty F=full",
                      dirtyPresent_ ? L"DIRTY" : L"FULL",
                      lastDirtyPercent_,
                      avgPresentMs,
                      presentCount_);
        return buffer;
    }

    void updateDebugTitle() {
        std::wstring title = L"DXGI Dirty Present Probe - " + debugLine();
        SetWindowTextW(hwnd_, title.c_str());
    }

    HWND hwnd_ = nullptr;
    bool running_ = true;
    bool manual_ = false;
    int maxFrames_ = -1;
    bool dirtyPresent_ = true;
    bool pointerDown_ = false;
    int width_ = kInitialWidth;
    int height_ = kInitialHeight;
    Button plain_;
    Button shadow_;
    RectF dirty_;
    bool dirtyValid_ = false;
    int presentCount_ = 0;
    double presentMsAccum_ = 0.0;
    double lastDirtyPercent_ = 100.0;
    std::chrono::steady_clock::time_point startTime_ = std::chrono::steady_clock::now();

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain1> swapchain_;
    ID2D1Factory* d2dFactory_ = nullptr;
    ComPtr<IDWriteFactory> writeFactory_;
    ComPtr<ID2D1RenderTarget> d2dTarget_;
    ComPtr<IDWriteTextFormat> buttonTextFormat_;
    ComPtr<IDWriteTextFormat> debugTextFormat_;
    ComPtr<ID2D1SolidColorBrush> backgroundBrush_;
    ComPtr<ID2D1SolidColorBrush> buttonBrush_;
    ComPtr<ID2D1SolidColorBrush> whiteBrush_;
    ComPtr<ID2D1SolidColorBrush> mutedBrush_;
    ComPtr<ID2D1SolidColorBrush> shadowBrush_;
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return DxgiDirtyPresentProbe{}.run(__argc, __argv);
}

int main(int argc, char** argv) {
    return DxgiDirtyPresentProbe{}.run(argc, argv);
}
#else
#include <iostream>

int main() {
    std::cout << "dxgi_dirty_present_probe is only available on Windows.\n";
    return 0;
}
#endif
