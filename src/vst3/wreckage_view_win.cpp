#ifdef _WIN32
#include "wreckage_view_win.h"
#include "wreckage_uids.h"
#include "version.h"
#include <windowsx.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace Steinberg;
using Steinberg::Vst::ParamID;

namespace NoiseMu6ix {

static const wchar_t* kWndClassName = L"NoiseMu6ixWreckageD2DViewClass_v103";

static HINSTANCE getPluginModuleHandle() {
    HINSTANCE hInst = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&getPluginModuleHandle),
            &hInst)) {
        hInst = GetModuleHandleW(nullptr);
    }
    return hInst;
}

struct ComApartmentGuard {
    HRESULT hr;
    ComApartmentGuard() { hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ComApartmentGuard() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(std::max(0, n - 1)), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], n);
    return out;
}

static std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(0, n - 1)), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], n, nullptr, nullptr);
    return out;
}

static std::wstring midiNoteName(int note) {
    static const wchar_t* kNames[12] = { L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B" };
    int n = std::clamp(note, 0, 127);
    int oct = (n / 12) - 1;
    return std::wstring(kNames[n % 12]) + std::to_wstring(oct);
}

WreckageWinView::WreckageWinView(WreckageController* controller)
    : CPluginView(nullptr), controller_(controller) {
    int zIdx = 2; // Default 100%
    if (controller_) {
        zIdx = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamUiZoom) * 2.0)), 0, 2);
    }
    uiScale_ = (zIdx == 0) ? 0.50f : ((zIdx == 1) ? 0.75f : 1.00f);
    int px = static_cast<int>(std::round(kBaseUiSize * uiScale_));
    ViewRect r(0, 0, px, px);
    setRect(r);
}

WreckageWinView::~WreckageWinView() {
    discardDeviceResources();
    if (dwFactory_) { dwFactory_->Release(); dwFactory_ = nullptr; }
    if (d2dFactory_) { d2dFactory_->Release(); d2dFactory_ = nullptr; }
}

tresult PLUGIN_API WreckageWinView::isPlatformTypeSupported(FIDString type) {
    return (type && strcmp(type, kPlatformTypeHWND) == 0) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API WreckageWinView::attached(void* parent, FIDString type) {
    if (!parent || isPlatformTypeSupported(type) != kResultTrue) return kResultFalse;

    HINSTANCE hInst = getPluginModuleHandle();
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &WreckageWinView::wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWndClassName;
    RegisterClassExW(&wc);

    int px = std::max(200, getRect().getWidth());
    RECT parentRc{};
    if (GetClientRect(static_cast<HWND>(parent), &parentRc)) {
        int pw = parentRc.right - parentRc.left;
        int ph = parentRc.bottom - parentRc.top;
        if (pw >= 200 && ph >= 200) px = std::min(pw, ph);
    }
    uiScale_ = static_cast<float>(px) / static_cast<float>(kBaseUiSize);
    ViewRect r(0, 0, px, px);
    setRect(r);

    hwnd_ = CreateWindowExW(
        0, kWndClassName, L"WRECKAGE",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, px, px,
        static_cast<HWND>(parent), nullptr, hInst, this);

    if (!hwnd_) return kResultFalse;
    DragAcceptFiles(hwnd_, TRUE);

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dwFactory_));
    createDeviceResources();

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    SetTimer(hwnd_, 1, 33, nullptr);
    return CPluginView::attached(parent, type);
}

tresult PLUGIN_API WreckageWinView::removed() {
    cancelInlineRename();
    if (hwnd_) {
        KillTimer(hwnd_, 1);
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        discardDeviceResources();
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    return CPluginView::removed();
}

tresult PLUGIN_API WreckageWinView::onSize(ViewRect* newSize) {
    if (!newSize) return kInvalidArgument;
    CPluginView::onSize(newSize);
    int w = std::max(200, newSize->getWidth());
    int h = std::max(200, newSize->getHeight());
    uiScale_ = static_cast<float>(std::min(w, h)) / static_cast<float>(kBaseUiSize);
    if (hwnd_) {
        SetWindowPos(hwnd_, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
        if (rt_) rt_->Resize(D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h)));
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return kResultOk;
}

void WreckageWinView::createDeviceResources() {
    if (!rt_ && d2dFactory_ && hwnd_) {
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        UINT32 w = static_cast<UINT32>(std::max<LONG>(1, rc.right - rc.left));
        UINT32 h = static_cast<UINT32>(std::max<LONG>(1, rc.bottom - rc.top));
        D2D1_SIZE_U sz = D2D1::SizeU(w, h);
        D2D1_PIXEL_FORMAT pxFmt = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(hwnd_, sz, D2D1_PRESENT_OPTIONS_NONE);

        HRESULT hr = d2dFactory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, pxFmt),
            hwndProps, &rt_);
        if (FAILED(hr) || !rt_) {
            hr = d2dFactory_->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, pxFmt),
                hwndProps, &rt_);
        }
        if (SUCCEEDED(hr) && rt_) {
            rt_->SetDpi(96.0f, 96.0f);
            rt_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &brush_);
        }
    }
}

void WreckageWinView::discardDeviceResources() {
    if (brush_) { brush_->Release(); brush_ = nullptr; }
    if (rt_) { rt_->Release(); rt_ = nullptr; }
}

D2D1_COLOR_F WreckageWinView::toD2D(uint32_t rgb, float alpha) const {
    return D2D1::ColorF(
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8)  & 0xFF) / 255.0f,
        (rgb & 0xFF)         / 255.0f,
        alpha);
}

void WreckageWinView::drawText(const wchar_t* text, float x, float y, float w, float h,
                               uint32_t rgb, float sizePt, bool bold, DWRITE_TEXT_ALIGNMENT align) {
    if (!rt_ || !dwFactory_ || !brush_ || !text) return;
    IDWriteTextFormat* fmt = nullptr;
    if (SUCCEEDED(dwFactory_->CreateTextFormat(
            L"Segoe UI", nullptr,
            bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            sizePt, L"en-us", &fmt))) {
        fmt->SetTextAlignment(align);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        brush_->SetColor(toD2D(rgb));
        rt_->DrawTextW(text, static_cast<UINT32>(wcslen(text)), fmt, D2D1::RectF(x, y, x + w, y + h), brush_);
        fmt->Release();
    }
}

void WreckageWinView::drawFader(float x, float y, float w, float h, float val01, const wchar_t* label,
                                const wchar_t* valStr, const ThemePalette& th, bool accentFill, bool enabled) {
    val01 = std::clamp(val01, 0.0f, 1.0f);
    drawText(label, x, y - 16.0f, w * 0.72f, 15.0f, enabled ? th.textPrimary : th.textMuted, 10.0f, true);
    if (valStr && valStr[0] != 0) {
        drawText(valStr, x + w * 0.65f, y - 16.0f, w * 0.35f, 15.0f, enabled ? th.accent : th.textMuted, 10.0f, true, DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    brush_->SetColor(toD2D(th.faderTrack));
    rt_->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush_);
    brush_->SetColor(toD2D(enabled ? (accentFill ? th.accent : th.faderFill) : th.textMuted));
    rt_->FillRectangle(D2D1::RectF(x, y, x + w * val01, y + h), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(x, y, x + w, y + h), brush_, 1.3f);

    float tx = x + w * val01 - 4.5f;
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(tx, y - 2.5f, tx + 9.0f, y + h + 2.5f), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(tx, y - 2.5f, tx + 9.0f, y + h + 2.5f), brush_, 1.5f);
}

void WreckageWinView::paint() {
    createDeviceResources();
    if (!rt_ || !brush_ || !controller_) return;

    int thIdx = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamThemeIndex) * 9.0)), 0, kNumThemes - 1);
    const ThemePalette& th = kThemes[thIdx];

    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Scale(uiScale_, uiScale_));
    rt_->Clear(toD2D(th.bg));

    drawHeader(th);
    drawTabs(th);

    if (activeTab_ == 0) drawSourceTab(th);
    else if (activeTab_ == 1) drawStageTab(th);
    else drawMasterFxTab(th);

    drawPunishAndKeyboard(th);
    if (browserOpen_) drawLibraryBrowserOverlay(th);
    if (settingsOpen_) drawSettingsOverlay(th);

    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(1.0f, 1.0f, kBaseUiSize - 1.0f, kBaseUiSize - 1.0f), brush_, 2.0f);

    if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) discardDeviceResources();
}

void WreckageWinView::drawHeader(const ThemePalette& th) {
    brush_->SetColor(toD2D(th.headerBg));
    rt_->FillRectangle(D2D1::RectF(0, 0, 840, 68), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawLine(D2D1::Point2F(0, 68), D2D1::Point2F(840, 68), brush_, 1.6f);

    // Exact requested lockup: NOISE MU6IX directly above W..C of WRECKAGE, and v1.0.3 tucked against bottom-right of 'E'
    drawText(L"NOISE MU6IX", 16, 8, 95, 13, th.textMuted, 9.5f, true);
    drawText(L"WRECKAGE", 14, 17, 178, 38, th.textPrimary, 30.0f, true);
    drawText(L"v" WRECKAGE_VERSION_STR, 184, 39, 44, 15, th.accent, 9.5f, true);

    // Center Preset Box (pulled slightly left to balance) + Direct SAVE Button
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(230, 12, 518, 56), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(230, 12, 518, 56), brush_, 1.5f);
    std::wstring rootLabel = controller_->rootLibraryDir().empty()
        ? L"PRESET / KIT  [CLICK TO BROWSE]"
        : (L"LIB: " + std::filesystem::path(controller_->rootLibraryDir()).filename().wstring());
    drawText(rootLabel.c_str(), 238, 14, 274, 16, th.textMuted, 9.5f, true);
    drawText(toWide(controller_->activePresetName()).c_str(), 238, 30, 274, 22, th.textPrimary, 12.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    bool savedFlash = controller_->saveFlashTicks() > 0;
    brush_->SetColor(toD2D(savedFlash ? 0x2E7D32 : th.accent));
    rt_->FillRectangle(D2D1::RectF(524, 12, 592, 56), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(524, 12, 592, 56), brush_, 1.5f);
    drawText(savedFlash ? L"SAVED!" : L"SAVE", 524, 12, 68, 44, 0xFFFFFF, 11.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    // Telemetry Readout (Voices, RAM) + Panic (!) + SET
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(600, 12, 744, 56), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(600, 12, 744, 56), brush_, 1.4f);

    int vox = controller_->liveVoices();
    double ramMb = static_cast<double>(controller_->liveRamBytes()) / (1024.0 * 1024.0);
    wchar_t line1[64], line2[64];
    swprintf(line1, 64, L"VOX: %d   RAM: %.1fM", vox, ramMb);
    swprintf(line2, 64, L"ENGINE: %s", vox > 0 ? L"ACTIVE" : L"IDLE");
    drawText(line1, 606, 14, 134, 18, th.textPrimary, 9.5f, true);
    drawText(line2, 606, 34, 134, 18, vox > 0 ? th.accent : th.textMuted, 9.5f, true);

    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(750, 12, 784, 56), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(750, 12, 784, 56), brush_, 1.4f);
    drawText(L"!", 750, 12, 34, 44, th.accent, 16.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    brush_->SetColor(toD2D(settingsOpen_ ? th.border : th.panelBg));
    rt_->FillRectangle(D2D1::RectF(790, 12, 826, 56), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(790, 12, 826, 56), brush_, 1.4f);
    drawText(L"SET", 790, 12, 36, 44, settingsOpen_ ? th.panelBg : th.textPrimary, 10.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void WreckageWinView::drawTabs(const ThemePalette& th) {
    const wchar_t* tabNames[3] = { L"SOURCE", L"STAGE & MIX", L"MASTER FX" };
    for (int i = 0; i < 3; ++i) {
        float x = i * 280.0f;
        bool active = (i == activeTab_);
        brush_->SetColor(toD2D(active ? th.border : th.headerBg));
        rt_->FillRectangle(D2D1::RectF(x, 68, x + 280, 106), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(x, 68, x + 280, 106), brush_, 1.5f);
        drawText(tabNames[i], x, 68, 280, 38, active ? th.panelBg : th.textPrimary, 13.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void WreckageWinView::drawSourceTab(const ThemePalette& th) {
    // LEFT: 3-Octave Drag & Drop Key Mapping Board (C3-B5) + SAVE AS / CLEAR / RESET
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(16, 118, 412, 626), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(16, 118, 412, 626), brush_, 1.6f);

    drawText(L"DRAG & DROP KEY MAP (DBL-CLICK RENAME)", 26, 124, 380, 22, th.textPrimary, 10.5f, true);
    const wchar_t* banks[3] = { L"BANK 1 (C3-B3)", L"BANK 2 (C4-B4)", L"BANK 3 (C5-B5)" };
    for (int b = 0; b < 3; ++b) {
        float bx = 26.0f + b * 126.0f;
        brush_->SetColor(toD2D(b == activeBank_ ? th.border : th.headerBg));
        rt_->FillRectangle(D2D1::RectF(bx, 152, bx + 120, 180), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(bx, 152, bx + 120, 180), brush_, 1.2f);
        drawText(banks[b], bx, 152, 120, 28, b == activeBank_ ? th.panelBg : th.textPrimary, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    for (int i = 0; i < 12; ++i) {
        int sIdx = activeBank_ * 12 + i;
        const auto& sl = controller_->slotMirror(sIdx);
        int col = i % 3, row = i / 3;
        float px = 26.0f + col * 126.0f;
        float py = 192.0f + row * 90.0f;
        bool sel = (sIdx == selectedSlot_);
        brush_->SetColor(toD2D(sel ? th.border : th.bg));
        rt_->FillRectangle(D2D1::RectF(px, py, px + 120, py + 80), brush_);
        if (sl.hasCustomColor && sl.itemCount > 0) {
            brush_->SetColor(toD2D(sl.keyColor));
            rt_->FillRectangle(D2D1::RectF(px, py, px + 120, py + 8), brush_);
        }
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(px, py, px + 120, py + 80), brush_, sel ? 2.2f : 1.2f);
        drawText(toWide(sl.name).c_str(), px + 4, py + 14, 112, 34, sel ? th.panelBg : th.textPrimary, 11.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        std::wstring info = (sl.itemCount == 0)
            ? (midiNoteName(sl.midiNote) + L"  [DROP WAV]")
            : (midiNoteName(sl.midiNote) + L"  RR:" + std::to_wstring(sl.itemCount));
        drawText(info.c_str(), px + 4, py + 50, 112, 22, sel ? th.headerBg : th.textMuted, 9.5f, false, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Bottom Left Buttons: SAVE AS | CLEAR SLOT | RESET KIT
    brush_->SetColor(toD2D(th.accent));
    rt_->FillRectangle(D2D1::RectF(26, 568, 152, 610), brush_);
    drawText(L"SAVE AS", 26, 568, 126, 42, 0xFFFFFF, 11.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    brush_->SetColor(toD2D(th.headerBg));
    rt_->FillRectangle(D2D1::RectF(162, 568, 282, 610), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(162, 568, 282, 610), brush_, 1.2f);
    drawText(L"CLEAR SLOT", 162, 568, 120, 42, th.textPrimary, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    brush_->SetColor(toD2D(th.headerBg));
    rt_->FillRectangle(D2D1::RectF(290, 568, 402, 610), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(290, 568, 402, 610), brush_, 1.2f);
    drawText(L"RESET KIT", 290, 568, 112, 42, th.textPrimary, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    // RIGHT: Wave Editor (1, 2, 3... RR Selector + Direct Mouse Start/End/FadeIn/FadeOut) + Key Mapping Vel Grid + Global Fake Engines + Controls
    auto& sl = controller_->slotMirror(selectedSlot_);
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(428, 118, 824, 626), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(428, 118, 824, 626), brush_, 1.6f);

    // Sample/RR Tabs 1, 2, 3... at top of Wave Editor
    drawText(L"WAVE & VELOCITY:", 440, 124, 120, 24, th.textPrimary, 10.0f, true);
    int nItems = sl.itemCount;
    if (selectedWaveItem_ >= nItems) selectedWaveItem_ = std::max(0, nItems - 1);
    for (int k = 0; k < std::min(nItems, 8); ++k) {
        float bx = 562.0f + k * 31.0f;
        bool activeW = (k == selectedWaveItem_);
        brush_->SetColor(toD2D(activeW ? th.accent : th.headerBg));
        rt_->FillRectangle(D2D1::RectF(bx, 124, bx + 27, 148), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(bx, 124, bx + 27, 148), brush_, 1.2f);
        std::wstring num = std::to_wstring(k + 1);
        drawText(num.c_str(), bx, 124, 27, 24, activeW ? 0xFFFFFF : th.textPrimary, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Wave Editor Box (x: 440..726, y: 154..310) + Right Velocity Mapping Grid (x: 734..812, y: 154..310)
    float wx = 440.0f, wy = 154.0f, ww = 286.0f, wh = 156.0f;
    brush_->SetColor(toD2D(th.bg));
    rt_->FillRectangle(D2D1::RectF(wx, wy, wx + ww, wy + wh), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(wx, wy, wx + ww, wy + wh), brush_, 1.3f);

    float gx = 734.0f, gy = 154.0f, gw = 78.0f, gh = 156.0f;
    brush_->SetColor(toD2D(th.bg));
    rt_->FillRectangle(D2D1::RectF(gx, gy, gx + gw, gy + gh), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(gx, gy, gx + gw, gy + gh), brush_, 1.3f);
    drawText(L"VEL MAP", gx, gy + 2, gw, 16, th.textMuted, 8.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    // Draw horizontal velocity grid lines (25%, 50%, 75%)
    brush_->SetColor(toD2D(th.textMuted, 0.35f));
    for (int g = 1; g <= 3; ++g) {
        float ly = gy + gh * (g / 4.0f);
        rt_->DrawLine(D2D1::Point2F(gx, ly), D2D1::Point2F(gx + gw, ly), brush_, 1.0f);
    }

    if (nItems > 0 && selectedWaveItem_ < static_cast<int>(sl.waveItems.size())) {
        const auto& wi = sl.waveItems[static_cast<size_t>(selectedWaveItem_)];
        float midY = wy + wh * 0.5f;
        brush_->SetColor(toD2D(th.textMuted, 0.3f));
        rt_->DrawLine(D2D1::Point2F(wx, midY), D2D1::Point2F(wx + ww, midY), brush_, 1.0f);

        // Waveform bars
        float barW = ww / static_cast<float>(kWavePeakBins);
        for (int b = 0; b < kWavePeakBins; ++b) {
            float posN = static_cast<float>(b) / static_cast<float>(kWavePeakBins);
            bool inRegion = (posN >= wi.startNorm && posN <= wi.endNorm);
            float pk = std::clamp(wi.peaks[static_cast<size_t>(b)], 0.0f, 1.0f);
            float hHalf = std::max(1.0f, pk * (wh * 0.44f));
            brush_->SetColor(toD2D(inRegion ? th.textPrimary : th.textMuted, inRegion ? 0.90f : 0.30f));
            rt_->FillRectangle(D2D1::RectF(wx + b * barW, midY - hHalf, wx + (b + 1) * barW, midY + hHalf), brush_);
        }

        // Start & End Draggable Vertical Markers + Fade-In / Fade-Out Diagonal Slopes
        float xStart = wx + wi.startNorm * ww;
        float xEnd   = wx + wi.endNorm * ww;
        float regW   = std::max(4.0f, xEnd - xStart);
        float xFadeIn  = xStart + wi.fadeInNorm * regW;
        float xFadeOut = xEnd - wi.fadeOutNorm * regW;

        brush_->SetColor(toD2D(th.accent));
        rt_->DrawLine(D2D1::Point2F(xStart, wy), D2D1::Point2F(xStart, wy + wh), brush_, 2.2f);
        rt_->DrawLine(D2D1::Point2F(xEnd, wy),   D2D1::Point2F(xEnd, wy + wh),   brush_, 2.2f);
        // Fade slopes
        rt_->DrawLine(D2D1::Point2F(xStart, wy + wh), D2D1::Point2F(xFadeIn, wy + 8.0f), brush_, 1.8f);
        rt_->DrawLine(D2D1::Point2F(xFadeOut, wy + 8.0f), D2D1::Point2F(xEnd, wy + wh), brush_, 1.8f);

        // Top Handle Squares for Fade In / Fade Out
        rt_->FillRectangle(D2D1::RectF(xFadeIn - 4.0f, wy + 4.0f, xFadeIn + 4.0f, wy + 12.0f), brush_);
        rt_->FillRectangle(D2D1::RectF(xFadeOut - 4.0f, wy + 4.0f, xFadeOut + 4.0f, wy + 12.0f), brush_);

        // Velocity Range Block in Right Grid (Drag Top/Bottom edge to map velocity)
        float vTop = gy + (1.0f - wi.velMax) * gh;
        float vBot = gy + (1.0f - wi.velMin) * gh;
        brush_->SetColor(toD2D(th.accent, 0.35f));
        rt_->FillRectangle(D2D1::RectF(gx + 6, vTop, gx + gw - 6, vBot), brush_);
        brush_->SetColor(toD2D(th.accent));
        rt_->DrawRectangle(D2D1::RectF(gx + 6, vTop, gx + gw - 6, vBot), brush_, 1.8f);
        wchar_t vTxt[32];
        swprintf(vTxt, 32, L"%d-%d%%", static_cast<int>(std::round(wi.velMin * 100.0f)), static_cast<int>(std::round(wi.velMax * 100.0f)));
        drawText(vTxt, gx, (vTop + vBot) * 0.5f - 8.0f, gw, 16.0f, th.textPrimary, 9.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        drawText(L"DROP .WAV ON ANY KEY SLOT TO EDIT WAVE", wx, wy, ww, wh, th.textMuted, 10.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Key Color Swatches
    drawText(L"COLOR:", 440, 320, 52, 20, th.textMuted, 9.5f, true);
    for (int c = 0; c < kNumKeyColors; ++c) {
        float sx = 496.0f + c * 26.0f;
        brush_->SetColor(toD2D(kStudioKeyColors[c]));
        rt_->FillRectangle(D2D1::RectF(sx, 320, sx + 21, 338), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(sx, 320, sx + 21, 338), brush_, (sl.hasCustomColor && sl.keyColor == kStudioKeyColors[c]) ? 2.2f : 1.0f);
    }

    // Global Fake Engines (ON/OFF across whole kit)
    bool fRR  = controller_->getParamNormalized(kParamGlobalFakeRR) >= 0.5;
    bool fVel = controller_->getParamNormalized(kParamGlobalFakeVel) >= 0.5;
    bool fMic = controller_->getParamNormalized(kParamGlobalFakeMic) >= 0.5;
    const wchar_t* gLabels[3] = {
        fRR  ? L"GLOBAL FAKE RR: ON"  : L"GLOBAL FAKE RR: OFF",
        fVel ? L"GLOBAL FAKE VEL: ON" : L"GLOBAL FAKE VEL: OFF",
        fMic ? L"GLOBAL FAKE MIC: ON" : L"GLOBAL FAKE MIC: OFF"
    };
    bool gStates[3] = { fRR, fVel, fMic };
    for (int g = 0; g < 3; ++g) {
        float px = 440.0f + g * 126.0f;
        brush_->SetColor(toD2D(gStates[g] ? th.accent : th.headerBg));
        rt_->FillRectangle(D2D1::RectF(px, 348, px + 118, 376), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(px, 348, px + 118, 376), brush_, 1.2f);
        drawText(gLabels[g], px, 348, 118, 28, gStates[g] ? 0xFFFFFF : th.textPrimary, 9.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Trigger Mode Row (ONE-SHOT / GATE | CHOKE | REVERSE)
    std::wstring m1 = sl.oneShot ? L"MODE: ONE-SHOT" : L"MODE: GATE";
    std::wstring m2 = sl.chokeGroup == 0 ? L"CHOKE: OFF" : (L"CHOKE: " + std::to_wstring(sl.chokeGroup));
    std::wstring m3 = sl.reverse ? L"REV: ON" : L"REV: OFF";
    const wchar_t* dModes[3] = { m1.c_str(), m2.c_str(), m3.c_str() };
    for (int d = 0; d < 3; ++d) {
        float dx = 440.0f + d * 126.0f;
        brush_->SetColor(toD2D(th.headerBg));
        rt_->FillRectangle(D2D1::RectF(dx, 384, dx + 118, 412), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(dx, 384, dx + 118, 412), brush_, 1.2f);
        drawText(dModes[d], dx, 384, 118, 28, th.textPrimary, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // 4 Faders with Live Value Readout + Double-Click Reset (Gain 80%, Pitch 0 st, Attack ms, Release ms)
    wchar_t vGain[32], vPitch[32], vAtt[32], vRel[32];
    swprintf(vGain,  32, L"%d%%", static_cast<int>(std::round(sl.gain * 100.0f)));
    float stVal = (sl.pitch - 0.5f) * 48.0f;
    swprintf(vPitch, 32, L"%+.1f st", stVal);
    swprintf(vAtt,   32, L"%.0f ms", sl.attack * 200.0f);
    swprintf(vRel,   32, L"%.0f ms", 20.0f + sl.release * 3980.0f);

    drawFader(440, 442, 368, 15, sl.gain,    L"GAIN (DBL-CLICK = 80%)",   vGain,  th);
    drawFader(440, 490, 368, 15, sl.pitch,   L"PITCH (DBL-CLICK = 0 st)", vPitch, th);
    drawFader(440, 538, 368, 15, sl.attack,  L"ATTACK",                   vAtt,   th);
    drawFader(440, 586, 368, 15, sl.release, L"RELEASE",                  vRel,   th);
}

void WreckageWinView::drawStageTab(const ThemePalette& th) {
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(16, 116, 824, 626), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(16, 116, 824, 626), brush_, 1.6f);

    const auto& selSl = controller_->slotMirror(selectedSlot_);
    std::wstring stageTitle = L"BANK " + std::to_wstring(activeBank_ + 1) + L"  |  " + midiNoteName(selSl.midiNote) + L" : " + toWide(selSl.name);
    drawText(stageTitle.c_str(), 28, 122, 390, 26, th.textPrimary, 12.5f, true);

    auto drawMiniBtn = [&](float x, float w, const wchar_t* txt, bool on) {
        brush_->SetColor(toD2D(on ? th.accent : th.headerBg));
        rt_->FillRectangle(D2D1::RectF(x, 122, x + w, 148), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(x, 122, x + w, 148), brush_, 1.2f);
        drawText(txt, x, 122, w, 26, on ? 0xFFFFFF : th.textPrimary, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    drawMiniBtn(440, 126, stageGridSnap_ ? L"GRID SNAP: ON" : L"GRID SNAP: OFF", stageGridSnap_);
    drawMiniBtn(574, 132, followMidi_ ? L"FOLLOW MIDI: ON" : L"FOLLOW MIDI: OFF", followMidi_);
    drawMiniBtn(714, 96,  L"CENTER ALL", false);

    // 3D Perspective Stage (x: 32..808, y: 156..374)
    brush_->SetColor(toD2D(th.bg));
    rt_->FillRectangle(D2D1::RectF(32, 156, 808, 374), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(32, 156, 808, 374), brush_, 1.4f);

    auto stageToScreen = [](float sx, float sy, float& outX, float& outY) {
        float ny = std::clamp(sy, 0.0f, 1.0f);
        float nx = std::clamp(sx, -1.0f, 1.0f);
        float halfW = 360.0f - ny * 130.0f;
        outX = 420.0f + nx * halfW;
        outY = 360.0f - ny * 188.0f;
    };

    brush_->SetColor(toD2D(th.textMuted, 0.35f));
    for (int gy = 0; gy <= 4; ++gy) {
        float yNorm = gy / 4.0f;
        float xL, yL, xR, yR;
        stageToScreen(-1.0f, yNorm, xL, yL);
        stageToScreen( 1.0f, yNorm, xR, yR);
        rt_->DrawLine(D2D1::Point2F(xL, yL), D2D1::Point2F(xR, yR), brush_, 1.0f);
        for (int gx = -4; gx <= 4; ++gx) {
            float px, py;
            stageToScreen(gx / 4.0f, yNorm, px, py);
            rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px, py), 2.2f, 2.2f), brush_);
        }
    }
    for (int gx = -4; gx <= 4; gx += 2) {
        float xF, yF, xB, yB;
        stageToScreen(gx / 4.0f, 0.0f, xF, yF);
        stageToScreen(gx / 4.0f, 1.0f, xB, yB);
        rt_->DrawLine(D2D1::Point2F(xF, yF), D2D1::Point2F(xB, yB), brush_, 1.0f);
    }

    drawText(L"BACK  (HALL / DEPTH Y=100%)", 32, 156, 776, 16, th.textMuted, 9.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    drawText(L"FRONT CENTER (X=0, Y=0  |  DBL-CLICK DOT TO RESET)", 32, 358, 776, 16, th.textMuted, 9.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    // Draw ONLY loaded slots in active bank (empty slots stay hidden until sample is loaded!)
    int loadedInBank = 0;
    for (int i = 0; i < 12; ++i) {
        if (controller_->slotMirror(activeBank_ * 12 + i).itemCount > 0) ++loadedInBank;
    }
    if (loadedInBank == 0) {
        drawText(L"STAGE IS EMPTY  —  DROP .WAV SAMPLES IN 'SOURCE' TO PLACE INSTRUMENTS", 32, 240, 776, 30, th.textMuted, 11.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    auto drawOneStageDot = [&](int sIdx, int displayNum, bool isSelected) {
        const auto& sl = controller_->slotMirror(sIdx);
        if (sl.itemCount <= 0) return;
        float nx, ny;
        stageToScreen(sl.stageX, sl.stageY, nx, ny);
        float rad = isSelected ? 15.5f : 11.5f;
        brush_->SetColor(toD2D(sl.hasCustomColor ? sl.keyColor : th.accent));
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(nx, ny), rad, rad), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(nx, ny), rad, rad), brush_, isSelected ? 2.3f : 1.2f);
        if (isSelected) {
            brush_->SetColor(toD2D(th.accent));
            rt_->DrawRectangle(D2D1::RectF(nx - 20.0f, ny - 20.0f, nx + 20.0f, ny + 20.0f), brush_, 1.8f);
        }
        std::wstring num = std::to_wstring(displayNum);
        drawText(num.c_str(), nx - 14.0f, ny - 8.0f, 28.0f, 16.0f, 0xFFFFFF, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    };

    for (int i = 0; i < 12; ++i) {
        int sIdx = activeBank_ * 12 + i;
        if (sIdx != selectedSlot_) drawOneStageDot(sIdx, i + 1, false);
    }
    if (selectedSlot_ >= activeBank_ * 12 && selectedSlot_ < (activeBank_ + 1) * 12) {
        drawOneStageDot(selectedSlot_, (selectedSlot_ % 12) + 1, true);
    }

    // Dedicated STAGE X (Pan) & STAGE Y (Depth) Faders with Double-Click Reset to 0
    wchar_t vX[32], vY[32];
    swprintf(vX, 32, L"%+d%%", static_cast<int>(std::round(selSl.stageX * 100.0f)));
    swprintf(vY, 32, L"%d%%",  static_cast<int>(std::round(selSl.stageY * 100.0f)));
    drawFader(32,  398, 376, 13, (selSl.stageX + 1.0f) * 0.5f, L"STAGE X (PAN L/R  |  DBL-CLICK = 0)", vX, th);
    drawFader(432, 398, 376, 13, selSl.stageY,                 L"STAGE Y (DEPTH    |  DBL-CLICK = 0)", vY, th);

    // Lower Rack: Left = DRUM, Right = MIX
    brush_->SetColor(toD2D(stageSubMode_ == 0 ? th.border : th.headerBg));
    rt_->FillRectangle(D2D1::RectF(32, 424, 114, 612), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(32, 424, 114, 612), brush_, 1.5f);
    drawText(L"DRUM", 32, 424, 82, 188, stageSubMode_ == 0 ? th.panelBg : th.textPrimary, 13.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    brush_->SetColor(toD2D(stageSubMode_ == 1 ? th.border : th.headerBg));
    rt_->FillRectangle(D2D1::RectF(726, 424, 808, 612), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(726, 424, 808, 612), brush_, 1.5f);
    drawText(L"MIX", 726, 424, 82, 188, stageSubMode_ == 1 ? th.panelBg : th.textPrimary, 13.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    if (stageSubMode_ == 0) {
        wchar_t vVol[32], vTune[32], vTL[32], vTH[32], vAt[32], vRl[32];
        swprintf(vVol,  32, L"%d%%", static_cast<int>(std::round(selSl.gain * 100.0f)));
        swprintf(vTune, 32, L"%+.1f st", (selSl.pitch - 0.5f) * 48.0f);
        swprintf(vTL,   32, L"%+.1f dB", (selSl.toneLow - 0.5f) * 24.0f);
        swprintf(vTH,   32, L"%+.1f dB", (selSl.toneHigh - 0.5f) * 24.0f);
        swprintf(vAt,   32, L"%.0f ms", selSl.attack * 200.0f);
        swprintf(vRl,   32, L"%.0f ms", 20.0f + selSl.release * 3980.0f);

        drawFader(132, 448, 276, 15, selSl.gain,     L"VOL (DBL-CLK 80%)", vVol,  th);
        drawFader(132, 508, 276, 15, selSl.pitch,    L"TUNE (DBL-CLK 0)",  vTune, th);
        drawFader(132, 568, 276, 15, selSl.toneLow,  L"TONE LOW (DBL 0)",  vTL,   th);

        drawFader(432, 448, 276, 15, selSl.toneHigh, L"TONE HI (DBL 0)",   vTH,   th);
        drawFader(432, 508, 276, 15, selSl.attack,   L"ATTACK",            vAt,   th);
        drawFader(432, 568, 276, 15, selSl.release,  L"DECAY / RELEASE",   vRl,   th);
    } else {
        // Uniform clean color for all 4 mics + individual ON/OFF buttons
        const wchar_t* micNames[4] = {
            L"CLOSE MIC (DIRECT PUNCH)",
            L"ROOM MIC (EARLY REFLECTIONS)",
            L"HALL MIC (SCORING SPACE)",
            L"SUB MIC (LOW IMPACT)"
        };
        for (int b = 0; b < 4; ++b) {
            float y = 446.0f + b * 44.0f;
            float val = static_cast<float>(controller_->getParamNormalized(kParamBusCloseGain + b));
            bool byp = controller_->getParamNormalized(kParamBusCloseBypass + b) >= 0.5;
            wchar_t vMic[32];
            swprintf(vMic, 32, L"%d%%", static_cast<int>(std::round(val * 125.0f)));
            drawFader(134, y, 514, 14, val, micNames[b], vMic, th, false, !byp);

            brush_->SetColor(toD2D(byp ? th.headerBg : th.accent));
            rt_->FillRectangle(D2D1::RectF(660, y - 4, 710, y + 18), brush_);
            brush_->SetColor(toD2D(th.border));
            rt_->DrawRectangle(D2D1::RectF(660, y - 4, 710, y + 18), brush_, 1.2f);
            drawText(byp ? L"OFF" : L"ON", 660, y - 4, 50, 22, byp ? th.textMuted : 0xFFFFFF, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
}

void WreckageWinView::drawMasterFxTab(const ThemePalette& th) {
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(16, 116, 824, 626), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(16, 116, 824, 626), brush_, 1.6f);

    // Top 6 Effect Module Boxes (FILTER, EQ, COMP, SAT, DELAY, REVERB) with Custom Vector Icon + LED On/Off Dot
    static const wchar_t* modNames[6] = { L"FILTER", L"EQ", L"COMP", L"SAT", L"DELAY", L"REVERB" };
    static const ParamID modOnIds[6] = { kParamFilterOn, kParamEqOn, kParamCompOn, kParamSatOn, kParamDelayOn, kParamRevOn };

    for (int m = 0; m < 6; ++m) {
        float bx = 28.0f + m * 131.0f;
        float by = 128.0f;
        float bw = 123.0f, bh = 74.0f;
        bool selected = (m == activeFxModule_);
        bool isOn = controller_->getParamNormalized(modOnIds[m]) >= 0.5;

        brush_->SetColor(toD2D(selected ? th.border : th.bg));
        rt_->FillRectangle(D2D1::RectF(bx, by, bx + bw, by + bh), brush_);
        brush_->SetColor(toD2D(selected ? th.accent : th.border));
        rt_->DrawRectangle(D2D1::RectF(bx, by, bx + bw, by + bh), brush_, selected ? 2.2f : 1.3f);

        // Top-right LED On/Off Toggle Circle (Click LED to toggle ON/OFF without changing selection)
        float ledX = bx + bw - 16.0f, ledY = by + 14.0f;
        brush_->SetColor(toD2D(isOn ? th.accent : th.headerBg));
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ledX, ledY), 7.5f, 7.5f), brush_);
        brush_->SetColor(toD2D(selected ? th.panelBg : th.border));
        rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(ledX, ledY), 7.5f, 7.5f), brush_, 1.4f);

        // Custom Monochrome Vector Icon per FX Module
        uint32_t fg = selected ? th.panelBg : th.textPrimary;
        brush_->SetColor(toD2D(fg));
        float cx = bx + 42.0f, cy = by + 28.0f;
        if (m == 0) { // Filter slope icon
            rt_->DrawLine(D2D1::Point2F(cx - 18, cy - 6), D2D1::Point2F(cx + 2, cy - 6), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(cx + 2, cy - 6), D2D1::Point2F(cx + 18, cy + 10), brush_, 2.0f);
        } else if (m == 1) { // EQ bell curve icon
            rt_->DrawLine(D2D1::Point2F(cx - 20, cy + 4), D2D1::Point2F(cx - 6, cy - 8), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(cx - 6, cy - 8), D2D1::Point2F(cx + 6, cy + 6), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(cx + 6, cy + 6), D2D1::Point2F(cx + 20, cy - 2), brush_, 2.0f);
        } else if (m == 2) { // Compressor knee icon
            rt_->DrawLine(D2D1::Point2F(cx - 16, cy + 10), D2D1::Point2F(cx, cy - 2), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(cx, cy - 2), D2D1::Point2F(cx + 18, cy - 6), brush_, 2.0f);
        } else if (m == 3) { // Saturator S-wave icon
            rt_->DrawLine(D2D1::Point2F(cx - 16, cy + 8), D2D1::Point2F(cx - 4, cy + 6), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(cx - 4, cy + 6), D2D1::Point2F(cx + 4, cy - 6), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(cx + 4, cy - 6), D2D1::Point2F(cx + 16, cy - 8), brush_, 2.0f);
        } else if (m == 4) { // Delay echoes icon
            rt_->DrawLine(D2D1::Point2F(cx - 12, cy - 8), D2D1::Point2F(cx - 12, cy + 8), brush_, 2.2f);
            rt_->DrawLine(D2D1::Point2F(cx, cy - 4), D2D1::Point2F(cx, cy + 8), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(cx + 12, cy), D2D1::Point2F(cx + 12, cy + 8), brush_, 1.8f);
        } else { // 3D Reverb cube icon
            rt_->DrawRectangle(D2D1::RectF(cx - 14, cy - 6, cx + 6, cy + 10), brush_, 1.6f);
            rt_->DrawRectangle(D2D1::RectF(cx - 6, cy - 11, cx + 14, cy + 5), brush_, 1.6f);
        }

        drawText(modNames[m], bx, by + 44, bw, 24, fg, 11.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Lower Expanded Editor Area (x: 28..812, y: 214..612)
    brush_->SetColor(toD2D(th.bg));
    rt_->FillRectangle(D2D1::RectF(28, 214, 812, 612), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(28, 214, 812, 612), brush_, 1.4f);

    bool modEnabled = controller_->getParamNormalized(modOnIds[activeFxModule_]) >= 0.5;
    wchar_t v1[32], v2[32], v3[32], v4[32], v5[32], v6[32];

    if (activeFxModule_ == 0) {
        // FILTER (FabFilter Volcano/Simplon style: LP24, LP12, HP24, HP12, BP12, BP24, PEAK, NOTCH)
        drawText(L"FILTER MODULE  (SELECT SLOPE / MODE)", 44, 224, 420, 24, th.textPrimary, 12.0f, true);
        static const wchar_t* fTypes[8] = { L"LP 24dB", L"LP 12dB", L"HP 24dB", L"HP 12dB", L"BP 12dB", L"BP 24dB", L"PEAK", L"NOTCH" };
        int ft = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamFilterType) * 7.0)), 0, 7);
        for (int t = 0; t < 8; ++t) {
            float tx = 44.0f + t * 94.0f;
            brush_->SetColor(toD2D(t == ft ? th.accent : th.headerBg));
            rt_->FillRectangle(D2D1::RectF(tx, 260, tx + 88, 296), brush_);
            brush_->SetColor(toD2D(th.border));
            rt_->DrawRectangle(D2D1::RectF(tx, 260, tx + 88, 296), brush_, 1.2f);
            drawText(fTypes[t], tx, 260, 88, 36, t == ft ? 0xFFFFFF : th.textPrimary, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        float cNorm = static_cast<float>(controller_->getParamNormalized(kParamFilterCutoff));
        float rNorm = static_cast<float>(controller_->getParamNormalized(kParamFilterRes));
        float hz = 20.0f * std::pow(1000.0f, cNorm);
        if (hz >= 1000.0f) swprintf(v1, 32, L"%.2f kHz", hz / 1000.0f);
        else swprintf(v1, 32, L"%.0f Hz", hz);
        swprintf(v2, 32, L"%d%%", static_cast<int>(std::round(rNorm * 100.0f)));

        drawFader(44, 344, 748, 20, cNorm, L"CUTOFF FREQUENCY", v1, th, true, modEnabled);
        drawFader(44, 424, 748, 20, rNorm, L"RESONANCE",        v2, th, false, modEnabled);
    }
    else if (activeFxModule_ == 1) {
        // EQ (FabFilter Pro-Q style interactive Curve + Low/Mid/High Freq & Gain)
        drawText(L"3-BAND PARAMETRIC EQ  (DRAG NODES ON CURVE OR FADERS BELOW)", 44, 222, 600, 22, th.textPrimary, 11.5f, true);
        float cx = 44.0f, cy = 250.0f, cw = 748.0f, ch = 136.0f;
        brush_->SetColor(toD2D(th.panelBg));
        rt_->FillRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), brush_, 1.2f);

        float midY = cy + ch * 0.5f;
        brush_->SetColor(toD2D(th.textMuted, 0.4f));
        rt_->DrawLine(D2D1::Point2F(cx, midY), D2D1::Point2F(cx + cw, midY), brush_, 1.0f);

        float lf = static_cast<float>(controller_->getParamNormalized(kParamEqLowFreq));
        float lg = static_cast<float>(controller_->getParamNormalized(kParamEqLowGain));
        float mf = static_cast<float>(controller_->getParamNormalized(kParamEqMidFreq));
        float mg = static_cast<float>(controller_->getParamNormalized(kParamEqMidGain));
        float hf = static_cast<float>(controller_->getParamNormalized(kParamEqHighFreq));
        float hg = static_cast<float>(controller_->getParamNormalized(kParamEqHighGain));

        float nx[3] = { cx + (0.06f + lf * 0.24f) * cw, cx + (0.34f + mf * 0.32f) * cw, cx + (0.70f + hf * 0.24f) * cw };
        float ny[3] = { cy + (1.0f - lg) * ch, cy + (1.0f - mg) * ch, cy + (1.0f - hg) * ch };

        // Draw EQ response curve
        brush_->SetColor(toD2D(modEnabled ? th.accent : th.textMuted));
        float prevX = cx, prevY = midY;
        for (int s = 1; s <= 64; ++s) {
            float px = cx + (s / 64.0f) * cw;
            float dy = 0.0f;
            for (int b = 0; b < 3; ++b) {
                float d = (px - nx[b]) / 68.0f;
                dy += (ny[b] - midY) * std::exp(-d * d);
            }
            float py = std::clamp(midY + dy, cy + 4.0f, cy + ch - 4.0f);
            rt_->DrawLine(D2D1::Point2F(prevX, prevY), D2D1::Point2F(px, py), brush_, 2.2f);
            prevX = px; prevY = py;
        }
        for (int b = 0; b < 3; ++b) {
            brush_->SetColor(toD2D(modEnabled ? th.accent : th.textMuted));
            rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(nx[b], ny[b]), 7.5f, 7.5f), brush_);
            brush_->SetColor(toD2D(th.border));
            rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(nx[b], ny[b]), 7.5f, 7.5f), brush_, 1.5f);
        }

        swprintf(v1, 32, L"%.0f Hz", 30.0f + lf * 270.0f);
        swprintf(v2, 32, L"%+.1f dB", (lg - 0.5f) * 24.0f);
        swprintf(v3, 32, L"%.0f Hz", 250.0f + mf * 3750.0f);
        swprintf(v4, 32, L"%+.1f dB", (mg - 0.5f) * 24.0f);
        swprintf(v5, 32, L"%.1f kHz", (2500.0f + hf * 13500.0f) / 1000.0f);
        swprintf(v6, 32, L"%+.1f dB", (hg - 0.5f) * 24.0f);

        drawFader(44,  416, 236, 13, lf, L"LOW FREQ",  v1, th, false, modEnabled);
        drawFader(300, 416, 236, 13, mf, L"MID FREQ",  v3, th, false, modEnabled);
        drawFader(556, 416, 236, 13, hf, L"HIGH FREQ", v5, th, false, modEnabled);

        drawFader(44,  474, 236, 13, lg, L"LOW GAIN",  v2, th, true, modEnabled);
        drawFader(300, 474, 236, 13, mg, L"MID GAIN",  v4, th, true, modEnabled);
        drawFader(556, 474, 236, 13, hg, L"HIGH GAIN", v6, th, true, modEnabled);
    }
    else if (activeFxModule_ == 2) {
        // COMP (FabFilter Pro-C style: Threshold, Ratio, Attack, Release, Knee/Shoulder, Makeup)
        drawText(L"DYNAMICS COMPRESSOR  (PRO-C STYLE)", 44, 224, 500, 24, th.textPrimary, 12.0f, true);
        float cTh = static_cast<float>(controller_->getParamNormalized(kParamCompThresh));
        float cRt = static_cast<float>(controller_->getParamNormalized(kParamCompRatio));
        float cAt = static_cast<float>(controller_->getParamNormalized(kParamCompAttack));
        float cRl = static_cast<float>(controller_->getParamNormalized(kParamCompRelease));
        float cKn = static_cast<float>(controller_->getParamNormalized(kParamCompKnee));
        float cMk = static_cast<float>(controller_->getParamNormalized(kParamCompMakeup));

        swprintf(v1, 32, L"%.1f dB", -36.0f + cTh * 36.0f);
        swprintf(v2, 32, L"%.1f : 1", 1.0f + cRt * 11.0f);
        swprintf(v3, 32, L"%.1f ms", 0.5f + cAt * 79.5f);
        swprintf(v4, 32, L"%.0f ms", 15.0f + cRl * 385.0f);
        swprintf(v5, 32, L"%.1f dB", cKn * 12.0f);
        swprintf(v6, 32, L"+%.1f dB", cMk * 15.0f);

        drawFader(44,  284, 356, 16, cTh, L"THRESHOLD",       v1, th, true,  modEnabled);
        drawFader(436, 284, 356, 16, cRt, L"RATIO",           v2, th, false, modEnabled);
        drawFader(44,  354, 356, 16, cAt, L"ATTACK",          v3, th, false, modEnabled);
        drawFader(436, 354, 356, 16, cRl, L"RELEASE",         v4, th, false, modEnabled);
        drawFader(44,  424, 356, 16, cKn, L"KNEE / SHOULDER", v5, th, false, modEnabled);
        drawFader(436, 424, 356, 16, cMk, L"MAKEUP GAIN",     v6, th, true,  modEnabled);
    }
    else if (activeFxModule_ == 3) {
        // SAT (FabFilter Saturn style: Drive, Warmth, Mix)
        drawText(L"HARMONIC SATURATOR  (SATURN STYLE)", 44, 224, 500, 24, th.textPrimary, 12.0f, true);
        float sDr = static_cast<float>(controller_->getParamNormalized(kParamSatDrive));
        float sWm = static_cast<float>(controller_->getParamNormalized(kParamSatWarmth));
        float sMx = static_cast<float>(controller_->getParamNormalized(kParamSatMix));
        swprintf(v1, 32, L"%d%%", static_cast<int>(std::round(sDr * 100.0f)));
        swprintf(v2, 32, L"%d%%", static_cast<int>(std::round(sWm * 100.0f)));
        swprintf(v3, 32, L"%d%%", static_cast<int>(std::round(sMx * 100.0f)));

        drawFader(44, 294, 748, 20, sDr, L"DRIVE",         v1, th, true,  modEnabled);
        drawFader(44, 368, 748, 20, sWm, L"WARMTH / TONE", v2, th, false, modEnabled);
        drawFader(44, 442, 748, 20, sMx, L"DRY / WET MIX", v3, th, false, modEnabled);
    }
    else if (activeFxModule_ == 4) {
        // DELAY (ValhallaDelay style: Modern / Vintage + Time, Feedback, Tone, Width, Mix)
        drawText(L"STEREO DELAY  (VALHALLA DELAY STYLE)", 44, 224, 420, 24, th.textPrimary, 12.0f, true);
        int dMode = (controller_->getParamNormalized(kParamDelayMode) >= 0.5) ? 1 : 0;
        const wchar_t* dModes[2] = { L"MODE: MODERN", L"MODE: VINTAGE" };
        for (int dm = 0; dm < 2; ++dm) {
            float dx = 520.0f + dm * 140.0f;
            brush_->SetColor(toD2D(dm == dMode ? th.accent : th.headerBg));
            rt_->FillRectangle(D2D1::RectF(dx, 224, dx + 132, 254), brush_);
            brush_->SetColor(toD2D(th.border));
            rt_->DrawRectangle(D2D1::RectF(dx, 224, dx + 132, 254), brush_, 1.2f);
            drawText(dModes[dm], dx, 224, 132, 30, dm == dMode ? 0xFFFFFF : th.textPrimary, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        float dTm = static_cast<float>(controller_->getParamNormalized(kParamDelayTime));
        float dFb = static_cast<float>(controller_->getParamNormalized(kParamDelayFeedback));
        float dTn = static_cast<float>(controller_->getParamNormalized(kParamDelayTone));
        float dWd = static_cast<float>(controller_->getParamNormalized(kParamDelayWidth));
        float dMx = static_cast<float>(controller_->getParamNormalized(kParamDelayMix));

        swprintf(v1, 32, L"%.0f ms", 20.0f + dTm * 780.0f);
        swprintf(v2, 32, L"%d%%", static_cast<int>(std::round(dFb * 88.0f)));
        swprintf(v3, 32, L"%d%%", static_cast<int>(std::round(dTn * 100.0f)));
        swprintf(v4, 32, L"%d%%", static_cast<int>(std::round(dWd * 100.0f)));
        swprintf(v5, 32, L"%d%%", static_cast<int>(std::round(dMx * 100.0f)));

        drawFader(44,  288, 356, 16, dTm, L"DELAY TIME",   v1, th, true,  modEnabled);
        drawFader(436, 288, 356, 16, dFb, L"FEEDBACK",     v2, th, false, modEnabled);
        drawFader(44,  358, 356, 16, dTn, L"TONE",         v3, th, false, modEnabled);
        drawFader(436, 358, 356, 16, dWd, L"3D WIDTH",     v4, th, false, modEnabled);
        drawFader(44,  428, 748, 16, dMx, L"DELAY MIX",    v5, th, true,  modEnabled);
    }
    else {
        // REVERB (ValhallaVintageVerb 3D Space style: Room / Plate / Hall + PreDelay, Decay, Size, LowCut, HighCut, Mix)
        drawText(L"3D SPACE REVERB  (VINTAGE VERB STYLE)", 44, 224, 400, 24, th.textPrimary, 12.0f, true);
        int rMode = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamRevMode) * 2.0)), 0, 2);
        const wchar_t* rModes[3] = { L"ROOM", L"PLATE", L"HALL" };
        for (int rm = 0; rm < 3; ++rm) {
            float rx = 500.0f + rm * 98.0f;
            brush_->SetColor(toD2D(rm == rMode ? th.accent : th.headerBg));
            rt_->FillRectangle(D2D1::RectF(rx, 224, rx + 92, 254), brush_);
            brush_->SetColor(toD2D(th.border));
            rt_->DrawRectangle(D2D1::RectF(rx, 224, rx + 92, 254), brush_, 1.2f);
            drawText(rModes[rm], rx, 224, 92, 30, rm == rMode ? 0xFFFFFF : th.textPrimary, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        float rPd = static_cast<float>(controller_->getParamNormalized(kParamRevPreDelay));
        float rDc = static_cast<float>(controller_->getParamNormalized(kParamRevDecay));
        float rSz = static_cast<float>(controller_->getParamNormalized(kParamRevSize));
        float rLc = static_cast<float>(controller_->getParamNormalized(kParamRevLowCut));
        float rHc = static_cast<float>(controller_->getParamNormalized(kParamRevHighCut));
        float rMx = static_cast<float>(controller_->getParamNormalized(kParamRevMix));

        swprintf(v1, 32, L"%.0f ms", rPd * 120.0f);
        swprintf(v2, 32, L"%.2f s",  0.2f + rDc * 5.8f);
        swprintf(v3, 32, L"%d%%",    static_cast<int>(std::round(rSz * 100.0f)));
        swprintf(v4, 32, L"%.0f Hz", 20.0f + rLc * 480.0f);
        swprintf(v5, 32, L"%.1f kHz",(1500.0f + rHc * 16500.0f) / 1000.0f);
        swprintf(v6, 32, L"%d%%",    static_cast<int>(std::round(rMx * 100.0f)));

        drawFader(44,  288, 356, 16, rPd, L"PRE-DELAY", v1, th, false, modEnabled);
        drawFader(436, 288, 356, 16, rDc, L"DECAY",     v2, th, true,  modEnabled);
        drawFader(44,  358, 356, 16, rSz, L"3D SIZE",   v3, th, false, modEnabled);
        drawFader(436, 358, 356, 16, rLc, L"LOW CUT",   v4, th, false, modEnabled);
        drawFader(44,  428, 356, 16, rHc, L"HIGH CUT",  v5, th, false, modEnabled);
        drawFader(436, 428, 356, 16, rMx, L"REVERB MIX",v6, th, true,  modEnabled);
    }

    // Bottom Global Utility Toggles inside Master FX (MONO BASS & OUTPUT LIMITER)
    bool mb  = controller_->getParamNormalized(kParamMonoBass) >= 0.5;
    bool lim = controller_->getParamNormalized(kParamLimiterOn) >= 0.5;
    brush_->SetColor(toD2D(mb ? th.border : th.headerBg));
    rt_->FillRectangle(D2D1::RectF(44, 548, 400, 594), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(44, 548, 400, 594), brush_, 1.3f);
    drawText(mb ? L"MONO SUB BASS (<120 Hz): ON" : L"MONO SUB BASS (<120 Hz): OFF", 44, 548, 356, 46, mb ? th.panelBg : th.textPrimary, 10.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    brush_->SetColor(toD2D(lim ? th.border : th.headerBg));
    rt_->FillRectangle(D2D1::RectF(436, 548, 792, 594), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(436, 548, 792, 594), brush_, 1.3f);
    drawText(lim ? L"MASTER SAFETY LIMITER: ON" : L"MASTER SAFETY LIMITER: OFF", 436, 548, 356, 46, lim ? th.panelBg : th.textPrimary, 10.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void WreckageWinView::drawPunishAndKeyboard(const ThemePalette& th) {
    brush_->SetColor(toD2D(th.punishBg));
    rt_->FillRectangle(D2D1::RectF(0, 636, 840, 724), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawLine(D2D1::Point2F(0, 636), D2D1::Point2F(840, 636), brush_, 1.8f);

    bool punOn = controller_->getParamNormalized(kParamPunishOn) >= 0.5;
    brush_->SetColor(toD2D(punOn ? th.accent : 0x363D47));
    rt_->FillRectangle(D2D1::RectF(18, 656, 126, 704), brush_);
    brush_->SetColor(toD2D(0xFFFFFF));
    rt_->DrawRectangle(D2D1::RectF(18, 656, 126, 704), brush_, 1.2f);
    drawText(punOn ? L"PUNISH" : L"PUNISH OFF", 18, 656, 108, 48, 0xFFFFFF, punOn ? 18.0f : 12.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    float punVal = static_cast<float>(controller_->getParamNormalized(kParamPunishAmount));
    brush_->SetColor(toD2D(0x363D47));
    rt_->FillRectangle(D2D1::RectF(138, 668, 488, 692), brush_);
    brush_->SetColor(toD2D(punOn ? th.accent : 0x7A818A));
    rt_->FillRectangle(D2D1::RectF(138, 668, 138 + 350 * punVal, 692), brush_);
    brush_->SetColor(toD2D(0xFFFFFF));
    rt_->DrawRectangle(D2D1::RectF(138, 668, 488, 692), brush_, 1.2f);

    int pm = static_cast<int>(std::round(controller_->getParamNormalized(kParamPunishMode) * 2.0));
    const wchar_t* pModes[3] = { L"GENTLY NOW", L"HURT ME PLENTY", L"NIGHTMARE" };
    for (int i = 0; i < 3; ++i) {
        float bx = 506.0f + i * 108.0f;
        brush_->SetColor(toD2D((i == pm && punOn) ? th.accent : 0x2E353F));
        rt_->FillRectangle(D2D1::RectF(bx, 654, bx + 102, 706), brush_);
        brush_->SetColor(toD2D(i == pm ? 0xFFFFFF : 0x7A818A));
        rt_->DrawRectangle(D2D1::RectF(bx, 654, bx + 102, 706), brush_, 1.3f);
        drawText(pModes[i], bx, 654, 102, 52, 0xFFFFFF, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // 3-Octave Keyboard (C3 - B5) — Clean white/black keys when empty, color stripe ONLY when sample is loaded!
    brush_->SetColor(toD2D(th.headerBg));
    rt_->FillRectangle(D2D1::RectF(0, 724, 840, 840), brush_);

    static const int kWhiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const bool kHasBlackAfter[7] = { true, true, false, true, true, true, false };
    float kbX = 42.0f, whiteW = 36.0f, kbY = 732.0f, kbH = 98.0f;

    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(8, kbY, 36, kbY + kbH), brush_);
    rt_->FillRectangle(D2D1::RectF(804, kbY, 832, kbY + kbH), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(8, kbY, 36, kbY + kbH), brush_, 1.2f);
    rt_->DrawRectangle(D2D1::RectF(804, kbY, 832, kbY + kbH), brush_, 1.2f);
    drawText(L"<", 8, kbY, 28, kbH, th.textPrimary, 14.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    drawText(L">", 804, kbY, 28, kbH, th.textPrimary, 14.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    auto collectNoteColors = [&](int note, uint32_t& c1, uint32_t& c2) -> int {
        int count = 0;
        for (int i = 0; i < kTotalSlots; ++i) {
            const auto& sl = controller_->slotMirror(i);
            if (sl.midiNote == note && sl.itemCount > 0 && sl.hasCustomColor && !sl.purged) {
                if (count == 0) c1 = sl.keyColor;
                else if (count == 1 && sl.keyColor != c1) c2 = sl.keyColor;
                ++count;
            }
        }
        return count;
    };

    for (int oct = 0; oct < 3; ++oct) {
        for (int k = 0; k < 7; ++k) {
            int note = kBaseMidiNote + (oct + keyboardOctaveOffset_) * 12 + kWhiteOffsets[k];
            bool pressed = (note == activePressedNote_) || controller_->isMidiNoteLive(note);
            float kx = kbX + (oct * 7 + k) * whiteW;
            float pressShift = pressed ? 3.0f : 0.0f;

            brush_->SetColor(toD2D(pressed ? 0xFFD9C2 : 0xFFFFFF));
            rt_->FillRectangle(D2D1::RectF(kx, kbY + pressShift, kx + whiteW, kbY + kbH), brush_);
            if (pressed) {
                brush_->SetColor(toD2D(th.accent, 0.35f));
                rt_->FillRectangle(D2D1::RectF(kx + 1, kbY + pressShift, kx + whiteW - 1, kbY + kbH - 14), brush_);
            }

            uint32_t c1 = 0, c2 = 0;
            int cnt = collectNoteColors(note, c1, c2);
            if (cnt >= 2 && c2 != 0) {
                brush_->SetColor(toD2D(c1));
                rt_->FillRectangle(D2D1::RectF(kx, kbY + kbH - 14, kx + whiteW * 0.5f, kbY + kbH), brush_);
                brush_->SetColor(toD2D(c2));
                rt_->FillRectangle(D2D1::RectF(kx + whiteW * 0.5f, kbY + kbH - 14, kx + whiteW, kbY + kbH), brush_);
            } else if (cnt >= 1) {
                brush_->SetColor(toD2D(c1));
                rt_->FillRectangle(D2D1::RectF(kx, kbY + kbH - 14, kx + whiteW, kbY + kbH), brush_);
            }
            brush_->SetColor(toD2D(0x181C22));
            rt_->DrawRectangle(D2D1::RectF(kx, kbY + pressShift, kx + whiteW, kbY + kbH), brush_, pressed ? 1.8f : 1.1f);

            if (k == 0) {
                std::wstring cLbl = L"C" + std::to_wstring((note / 12) - 1);
                drawText(cLbl.c_str(), kx, kbY + kbH - 30, whiteW, 14, 0x333333, 8.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
            }
        }
    }

    for (int oct = 0; oct < 3; ++oct) {
        for (int k = 0; k < 7; ++k) {
            if (!kHasBlackAfter[k]) continue;
            int note = kBaseMidiNote + (oct + keyboardOctaveOffset_) * 12 + kWhiteOffsets[k] + 1;
            bool pressed = (note == activePressedNote_) || controller_->isMidiNoteLive(note);
            float kx = kbX + (oct * 7 + k + 1) * whiteW - 11.0f;
            float pressShift = pressed ? 3.0f : 0.0f;

            brush_->SetColor(toD2D(pressed ? th.accent : 0x181C22));
            rt_->FillRectangle(D2D1::RectF(kx, kbY + pressShift, kx + 22.0f, kbY + 58.0f), brush_);
            brush_->SetColor(toD2D(pressed ? 0xFFFFFF : 0x000000));
            rt_->DrawRectangle(D2D1::RectF(kx, kbY + pressShift, kx + 22.0f, kbY + 58.0f), brush_, pressed ? 1.6f : 1.0f);

            uint32_t c1 = 0, c2 = 0;
            if (collectNoteColors(note, c1, c2) >= 1) {
                brush_->SetColor(toD2D(c1));
                rt_->FillRectangle(D2D1::RectF(kx + 1.0f, kbY + 48.0f, kx + 21.0f, kbY + 57.0f), brush_);
            }
        }
    }
}

void WreckageWinView::drawLibraryBrowserOverlay(const ThemePalette& th) {
    brush_->SetColor(toD2D(0x000000, 0.45f));
    rt_->FillRectangle(D2D1::RectF(0, 68, 840, 840), brush_);
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(60, 110, 780, 660), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(60, 110, 780, 660), brush_, 2.0f);
    drawText(L"LIBRARY & PRESET BROWSER   (CLICK ITEM TO LOAD)", 76, 120, 600, 28, th.textPrimary, 12.0f, true);

    brush_->SetColor(toD2D(th.accent));
    rt_->FillRectangle(D2D1::RectF(610, 120, 710, 148), brush_);
    drawText(L"SET ROOT", 610, 120, 100, 28, 0xFFFFFF, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    drawText(L"[ X ]", 720, 120, 48, 28, th.accent, 13.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    for (size_t i = 0; i < std::min<size_t>(browserCategories_.size(), 14); ++i) {
        float y = 162.0f + i * 32.0f;
        bool sel = (static_cast<int>(i) == selectedCategory_);
        brush_->SetColor(toD2D(sel ? th.border : th.bg));
        rt_->FillRectangle(D2D1::RectF(76, y, 360, y + 28), brush_);
        drawText(browserCategories_[i].c_str(), 84, y, 270, 28, sel ? th.panelBg : th.textPrimary, 11.0f, sel);
    }
    for (size_t i = 0; i < std::min<size_t>(browserItems_.size(), 14); ++i) {
        float y = 162.0f + i * 32.0f;
        brush_->SetColor(toD2D(th.bg));
        rt_->FillRectangle(D2D1::RectF(380, y, 760, y + 28), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(380, y, 760, y + 28), brush_, 1.0f);
        drawText(browserItems_[i].c_str(), 388, y, 364, 28, th.textPrimary, 11.0f, false);
    }
}

void WreckageWinView::drawSettingsOverlay(const ThemePalette& th) {
    brush_->SetColor(toD2D(0x000000, 0.45f));
    rt_->FillRectangle(D2D1::RectF(0, 68, 840, 840), brush_);
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(140, 120, 700, 610), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(140, 120, 700, 610), brush_, 2.0f);

    drawText(L"WRECKAGE SETTINGS  (THEMES  |  ZOOM  |  CURVE  |  ROOT)", 160, 130, 480, 28, th.textPrimary, 12.0f, true);
    drawText(L"[ X ]", 644, 130, 44, 28, th.accent, 13.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    drawText(L"1. UI THEME (DEFAULT: PAPER SKETCH)", 160, 168, 520, 20, th.textMuted, 10.0f, true);
    int curTh = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamThemeIndex) * 9.0)), 0, 9);
    for (int t = 0; t < kNumThemes; ++t) {
        int col = t % 2, row = t / 2;
        float bx = 160.0f + col * 264.0f;
        float by = 194.0f + row * 38.0f;
        bool active = (t == curTh);
        brush_->SetColor(toD2D(active ? th.border : th.bg));
        rt_->FillRectangle(D2D1::RectF(bx, by, bx + 252, by + 32), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(bx, by, bx + 252, by + 32), brush_, 1.2f);
        drawText(toWide(kThemes[t].name).c_str(), bx + 10, by, 232, 32, active ? th.panelBg : th.textPrimary, 10.5f, true);
    }

    drawText(L"2. WINDOW ZOOM SIZE (DEFAULT: 100%)", 160, 396, 520, 20, th.textMuted, 10.0f, true);
    int zIdx = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamUiZoom) * 2.0)), 0, 2);
    const wchar_t* zLabels[3] = { L"50% (COMPACT)", L"75% (LAPTOP)", L"100% (FULL)" };
    for (int z = 0; z < 3; ++z) {
        float bx = 160.0f + z * 176.0f;
        brush_->SetColor(toD2D(z == zIdx ? th.border : th.bg));
        rt_->FillRectangle(D2D1::RectF(bx, 422, bx + 168, 456), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(bx, 422, bx + 168, 456), brush_, 1.2f);
        drawText(zLabels[z], bx, 422, 168, 34, z == zIdx ? th.panelBg : th.textPrimary, 10.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    drawText(L"3. VELOCITY RESPONSE & SAMPLE LIBRARY FOLDER", 160, 474, 520, 20, th.textMuted, 10.0f, true);
    int vc = static_cast<int>(std::round(controller_->getParamNormalized(kParamVelCurve) * 2.0));
    const wchar_t* vcLabels[3] = { L"SOFT CURVE", L"LINEAR CURVE", L"HARD CURVE" };
    for (int v = 0; v < 3; ++v) {
        float bx = 160.0f + v * 176.0f;
        brush_->SetColor(toD2D(v == vc ? th.accent : th.bg));
        rt_->FillRectangle(D2D1::RectF(bx, 500, bx + 168, 534), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(bx, 500, bx + 168, 534), brush_, 1.2f);
        drawText(vcLabels[v], bx, 500, 168, 34, v == vc ? 0xFFFFFF : th.textPrimary, 10.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    brush_->SetColor(toD2D(th.border));
    rt_->FillRectangle(D2D1::RectF(160, 550, 680, 590), brush_);
    std::wstring rootBtn = controller_->rootLibraryDir().empty()
        ? L"SET ROOT SAMPLE LIBRARY FOLDER..."
        : (L"ROOT FOLDER: " + controller_->rootLibraryDir());
    drawText(rootBtn.c_str(), 168, 550, 504, 40, th.panelBg, 10.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void WreckageWinView::startInlineRename() {
    if (!hwnd_ || renameEdit_) return;
    renamingSlot_ = selectedSlot_;
    int i = selectedSlot_ % 12;
    int col = i % 3, row = i / 3;
    int px = static_cast<int>(std::round((26.0f + col * 126.0f + 4.0f) * uiScale_));
    int py = static_cast<int>(std::round((192.0f + row * 90.0f + 16.0f) * uiScale_));
    int pw = static_cast<int>(std::round(112.0f * uiScale_));
    int ph = static_cast<int>(std::round(28.0f * uiScale_));

    std::wstring cur = toWide(controller_->slotMirror(renamingSlot_).name);
    renameEdit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", cur.c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_CENTER,
        px, py, pw, ph, hwnd_, reinterpret_cast<HMENU>(9001), getPluginModuleHandle(), nullptr);
    if (renameEdit_) {
        SendMessageW(renameEdit_, EM_SETLIMITTEXT, 20, 0);
        SendMessageW(renameEdit_, EM_SETSEL, 0, -1);
        SetFocus(renameEdit_);
    }
}

void WreckageWinView::commitInlineRename() {
    if (!renameEdit_) return;
    wchar_t buf[64] = {0};
    GetWindowTextW(renameEdit_, buf, 64);
    std::string u8 = toUtf8(buf);
    if (!u8.empty() && renamingSlot_ >= 0) controller_->uiRenameSlot(renamingSlot_, u8);
    DestroyWindow(renameEdit_);
    renameEdit_ = nullptr;
    renamingSlot_ = -1;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WreckageWinView::cancelInlineRename() {
    if (renameEdit_) { DestroyWindow(renameEdit_); renameEdit_ = nullptr; renamingSlot_ = -1; }
}

void WreckageWinView::applyZoomIndex(int idx) {
    idx = std::clamp(idx, 0, 2);
    controller_->uiSetParam(kParamUiZoom, idx * 0.5);
    uiScale_ = (idx == 0) ? 0.50f : ((idx == 1) ? 0.75f : 1.00f);
    int px = static_cast<int>(std::round(kBaseUiSize * uiScale_));
    ViewRect newRect(0, 0, px, px);
    if (plugFrame) plugFrame->resizeView(this, &newRect);
    else onSize(&newRect);
}

void WreckageWinView::openRootFolderDialog() {
    ComApartmentGuard guard;
    IFileOpenDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg)))) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (SUCCEEDED(dlg->Show(hwnd_))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR psz = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz))) {
                    controller_->rootLibraryDir() = psz;
                    CoTaskMemFree(psz);
                    rescanRootDirectory();
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WreckageWinView::openExportNm6Dialog(bool forceSaveAs) {
    if (!forceSaveAs && !controller_->lastSavedNm6Path().empty()) {
        controller_->uiExportNm6File(controller_->lastSavedNm6Path());
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    ComApartmentGuard guard;
    IFileSaveDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg)))) {
        COMDLG_FILTERSPEC spec[] = { { L"Noise Mu6ix Pack", L"*.nm6" } };
        dlg->SetFileTypes(1, spec);
        dlg->SetDefaultExtension(L"nm6");
        if (SUCCEEDED(dlg->Show(hwnd_))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR psz = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz))) {
                    controller_->uiExportNm6File(psz);
                    controller_->activePresetName() = toUtf8(std::filesystem::path(psz).stem().wstring());
                    CoTaskMemFree(psz);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WreckageWinView::rescanRootDirectory() {
    browserCategories_.clear();
    browserItems_.clear();
    if (controller_->rootLibraryDir().empty()) return;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(controller_->rootLibraryDir(), ec)) {
        if (entry.is_directory()) browserCategories_.push_back(entry.path().filename().wstring());
        else if (entry.path().extension() == L".nm6" || entry.path().extension() == L".wav")
            browserItems_.push_back(entry.path().filename().wstring());
    }
    if (selectedCategory_ >= 0 && selectedCategory_ < static_cast<int>(browserCategories_.size())) {
        browserItems_.clear();
        auto sub = std::filesystem::path(controller_->rootLibraryDir()) / browserCategories_[selectedCategory_];
        for (const auto& entry : std::filesystem::directory_iterator(sub, ec)) {
            if (entry.path().extension() == L".nm6" || entry.path().extension() == L".wav")
                browserItems_.push_back(entry.path().filename().wstring());
        }
    }
}

void WreckageWinView::onMouseDoubleClick(float lx, float ly) {
    if (browserOpen_ || settingsOpen_) return;

    if (activeTab_ == 0) {
        // Double-click slot box -> Inline Rename
        if (ly >= 192 && ly <= 552 && lx >= 26 && lx <= 400) {
            int col = std::clamp(static_cast<int>((lx - 26.0f) / 126.0f), 0, 2);
            int row = std::clamp(static_cast<int>((ly - 192.0f) / 90.0f), 0, 3);
            selectedSlot_ = activeBank_ * 12 + row * 3 + col;
            startInlineRename();
            return;
        }
        // Double-click Gain (reset to 80%) or Pitch (reset to 0 st)
        if (lx >= 440 && lx <= 808) {
            if (ly >= 426 && ly <= 466) controller_->uiSetSlotProp(selectedSlot_, 0, 0, 0.80);
            else if (ly >= 474 && ly <= 514) controller_->uiSetSlotProp(selectedSlot_, 1, 0, 0.50);
        }
    } else if (activeTab_ == 1) {
        // Double-click Stage Dot -> Reset to Center Front (0, 0)
        if (lx >= 32 && lx <= 808 && ly >= 156 && ly <= 374) {
            controller_->uiSetStageXY(selectedSlot_, 0.0f, 0.0f);
        }
        // Double-click STAGE X -> 0, STAGE Y -> 0
        else if (ly >= 384 && ly <= 418) {
            if (lx >= 32 && lx <= 408) controller_->uiSetStageXY(selectedSlot_, 0.0f, controller_->slotMirror(selectedSlot_).stageY);
            else if (lx >= 432 && lx <= 808) controller_->uiSetStageXY(selectedSlot_, controller_->slotMirror(selectedSlot_).stageX, 0.0f);
        }
        // Double-click DRUM faders: VOL -> 80%, TUNE -> 0, TONE LOW -> 0, TONE HI -> 0
        else if (stageSubMode_ == 0 && ly >= 432 && ly <= 592) {
            if (lx >= 132 && lx <= 408) {
                int row = std::clamp(static_cast<int>((ly - 432.0f) / 60.0f), 0, 2);
                if (row == 0) controller_->uiSetSlotProp(selectedSlot_, 0, 0, 0.80);
                else if (row == 1) controller_->uiSetSlotProp(selectedSlot_, 1, 0, 0.50);
                else controller_->uiSetSlotProp(selectedSlot_, 10, 0, 0.50);
            } else if (lx >= 432 && lx <= 708) {
                int row = std::clamp(static_cast<int>((ly - 432.0f) / 60.0f), 0, 2);
                if (row == 0) controller_->uiSetSlotProp(selectedSlot_, 11, 0, 0.50);
            }
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WreckageWinView::onMouseDown(float lx, float ly) {
    if (renameEdit_) commitInlineRename();

    // 1. Settings Modal
    if (settingsOpen_) {
        if (lx < 140 || lx > 700 || ly < 120 || ly > 610 || (lx > 635 && ly < 162)) {
            settingsOpen_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (ly >= 194 && ly <= 384 && lx >= 160 && lx <= 680) {
            int col = std::clamp(static_cast<int>((lx - 160.0f) / 264.0f), 0, 1);
            int row = std::clamp(static_cast<int>((ly - 194.0f) / 38.0f), 0, 4);
            controller_->uiSetParam(kParamThemeIndex, (row * 2 + col) / 9.0);
        } else if (ly >= 422 && ly <= 456 && lx >= 160 && lx <= 680) {
            applyZoomIndex(std::clamp(static_cast<int>((lx - 160.0f) / 176.0f), 0, 2));
        } else if (ly >= 500 && ly <= 534 && lx >= 160 && lx <= 680) {
            controller_->uiSetParam(kParamVelCurve, std::clamp(static_cast<int>((lx - 160.0f) / 176.0f), 0, 2) * 0.5);
        } else if (ly >= 550 && ly <= 590 && lx >= 160 && lx <= 680) {
            openRootFolderDialog();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // 2. Browser Modal
    if (browserOpen_) {
        if (lx < 60 || lx > 780 || ly < 110 || ly > 660 || (lx > 710 && ly < 155)) {
            browserOpen_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (lx >= 610 && lx <= 710 && ly >= 120 && ly <= 148) { openRootFolderDialog(); return; }
        if (lx >= 76 && lx <= 360 && ly >= 162) {
            int idx = static_cast<int>((ly - 162.0f) / 32.0f);
            if (idx >= 0 && idx < static_cast<int>(browserCategories_.size())) { selectedCategory_ = idx; rescanRootDirectory(); }
        } else if (lx >= 380 && lx <= 760 && ly >= 162) {
            int idx = static_cast<int>((ly - 162.0f) / 32.0f);
            if (idx >= 0 && idx < static_cast<int>(browserItems_.size()) && !controller_->rootLibraryDir().empty()) {
                std::filesystem::path base(controller_->rootLibraryDir());
                if (selectedCategory_ >= 0 && selectedCategory_ < static_cast<int>(browserCategories_.size()))
                    base /= browserCategories_[selectedCategory_];
                base /= browserItems_[idx];
                if (base.extension() == L".nm6") {
                    controller_->uiLoadNm6File(base.wstring());
                    controller_->activePresetName() = toUtf8(base.stem().wstring());
                    browserOpen_ = false;
                } else if (base.extension() == L".wav") {
                    controller_->uiLoadWavFile(selectedSlot_, base.wstring());
                    controller_->uiTriggerNote(controller_->slotMirror(selectedSlot_).midiNote, 0.9f, true);
                }
            }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // 3. Header Bar
    if (ly < 68) {
        if (lx >= 230 && lx <= 518) { rescanRootDirectory(); browserOpen_ = true; InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (lx >= 524 && lx <= 592) { openExportNm6Dialog(false); return; }
        if (lx >= 750 && lx <= 784) { controller_->uiPanic(); activePressedNote_ = -1; InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (lx >= 790 && lx <= 826) { settingsOpen_ = !settingsOpen_; InvalidateRect(hwnd_, nullptr, FALSE); return; }
        return;
    }

    // 4. Tabs
    if (ly >= 68 && ly < 106) {
        activeTab_ = std::clamp(static_cast<int>(lx / 280.0f), 0, 2);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // 5. PUNISH Bar
    if (ly >= 636 && ly < 724) {
        if (lx >= 18 && lx <= 126) {
            bool on = controller_->getParamNormalized(kParamPunishOn) >= 0.5;
            controller_->uiSetParam(kParamPunishOn, on ? 0.0 : 1.0);
        } else if (lx >= 138 && lx <= 488) {
            SetCapture(hwnd_);
            dragTarget_ = 2;
            controller_->uiSetParam(kParamPunishAmount, std::clamp((lx - 138.0f) / 350.0f, 0.0f, 1.0f));
        } else if (lx >= 506 && lx <= 830) {
            int m = std::clamp(static_cast<int>((lx - 506.0f) / 108.0f), 0, 2);
            controller_->uiSetParam(kParamPunishMode, m * 0.5);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // 6. 3-Octave Keyboard (C3 - B5)
    if (ly >= 724) {
        if (lx < 38) { keyboardOctaveOffset_ = std::max(-2, keyboardOctaveOffset_ - 1); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (lx > 802) { keyboardOctaveOffset_ = std::min(3, keyboardOctaveOffset_ + 1); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        static const int kWhiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const bool kHasBlackAfter[7] = { true, true, false, true, true, true, false };
        float kbX = 42.0f, whiteW = 36.0f, kbY = 732.0f;
        int note = -1;
        if (ly >= kbY && ly <= kbY + 58.0f) {
            for (int oct = 0; oct < 3 && note < 0; ++oct) {
                for (int k = 0; k < 7; ++k) {
                    if (!kHasBlackAfter[k]) continue;
                    float kx = kbX + (oct * 7 + k + 1) * whiteW - 11.0f;
                    if (lx >= kx && lx <= kx + 22.0f) {
                        note = kBaseMidiNote + (oct + keyboardOctaveOffset_) * 12 + kWhiteOffsets[k] + 1;
                        break;
                    }
                }
            }
        }
        if (note < 0) {
            int wIdx = std::clamp(static_cast<int>((lx - kbX) / whiteW), 0, 20);
            note = kBaseMidiNote + ((wIdx / 7) + keyboardOctaveOffset_) * 12 + kWhiteOffsets[wIdx % 7];
        }
        SetCapture(hwnd_);
        activePressedNote_ = note;
        if (followMidi_) {
            for (int s = 0; s < kTotalSlots; ++s) {
                if (controller_->slotMirror(s).midiNote == note) {
                    selectedSlot_ = s;
                    activeBank_ = s / 12;
                    selectedWaveItem_ = 0;
                    break;
                }
            }
        }
        controller_->uiTriggerNote(note, 0.90f, true);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // 7. Tab 0: SOURCE
    if (activeTab_ == 0) {
        if (ly >= 152 && ly <= 180 && lx >= 26 && lx <= 400) {
            activeBank_ = std::clamp(static_cast<int>((lx - 26.0f) / 126.0f), 0, 2);
            selectedSlot_ = activeBank_ * 12 + (selectedSlot_ % 12);
            selectedWaveItem_ = 0;
        } else if (ly >= 192 && ly <= 552 && lx >= 26 && lx <= 400) {
            int col = std::clamp(static_cast<int>((lx - 26.0f) / 126.0f), 0, 2);
            int row = std::clamp(static_cast<int>((ly - 192.0f) / 90.0f), 0, 3);
            selectedSlot_ = activeBank_ * 12 + row * 3 + col;
            selectedWaveItem_ = 0;
            controller_->uiTriggerNote(controller_->slotMirror(selectedSlot_).midiNote, 0.88f, true);
        } else if (ly >= 568 && ly <= 610) {
            if (lx >= 26 && lx <= 152) { openExportNm6Dialog(true); return; }
            else if (lx >= 162 && lx <= 282) { controller_->uiClearSlot(selectedSlot_); selectedWaveItem_ = 0; }
            else if (lx >= 290 && lx <= 402) { controller_->uiResetKit(); selectedWaveItem_ = 0; }
        } else if (ly >= 124 && ly <= 148 && lx >= 562 && lx <= 810) {
            // Wave Sample/RR Tabs 1, 2, 3...
            int k = std::clamp(static_cast<int>((lx - 562.0f) / 31.0f), 0, 7);
            auto& sl = controller_->slotMirror(selectedSlot_);
            if (k < sl.itemCount) {
                selectedWaveItem_ = k;
                controller_->uiRequestWave(selectedSlot_, selectedWaveItem_);
            }
        } else if (lx >= 440 && lx <= 726 && ly >= 154 && ly <= 310) {
            // Direct Mouse Drag inside Wave Editor (Start / End / Fade In / Fade Out)
            auto& sl = controller_->slotMirror(selectedSlot_);
            if (sl.itemCount > 0 && selectedWaveItem_ < static_cast<int>(sl.waveItems.size())) {
                const auto& wi = sl.waveItems[static_cast<size_t>(selectedWaveItem_)];
                float wx = 440.0f, ww = 286.0f;
                float xStart = wx + wi.startNorm * ww;
                float xEnd   = wx + wi.endNorm * ww;
                float regW   = std::max(4.0f, xEnd - xStart);
                float xFI    = xStart + wi.fadeInNorm * regW;
                float xFO    = xEnd - wi.fadeOutNorm * regW;

                SetCapture(hwnd_);
                if (ly <= 176.0f && std::fabs(lx - xFI) <= 12.0f) dragTarget_ = 52;      // Fade In handle
                else if (ly <= 176.0f && std::fabs(lx - xFO) <= 12.0f) dragTarget_ = 53; // Fade Out handle
                else if (std::fabs(lx - xStart) <= std::fabs(lx - xEnd)) dragTarget_ = 50; // Start line
                else dragTarget_ = 51; // End line
                onMouseMove(lx, ly);
            }
        } else if (lx >= 734 && lx <= 812 && ly >= 154 && ly <= 310) {
            // Velocity Mapping Grid Drag (Top = VelMax, Bottom = VelMin)
            auto& sl = controller_->slotMirror(selectedSlot_);
            if (sl.itemCount > 0 && selectedWaveItem_ < static_cast<int>(sl.waveItems.size())) {
                const auto& wi = sl.waveItems[static_cast<size_t>(selectedWaveItem_)];
                float gy = 154.0f, gh = 156.0f;
                float vTop = gy + (1.0f - wi.velMax) * gh;
                float vBot = gy + (1.0f - wi.velMin) * gh;
                SetCapture(hwnd_);
                dragTarget_ = (std::fabs(ly - vTop) <= std::fabs(ly - vBot)) ? 54 : 55;
                onMouseMove(lx, ly);
            }
        } else if (ly >= 320 && ly <= 340 && lx >= 496 && lx <= 808) {
            int c = std::clamp(static_cast<int>((lx - 496.0f) / 26.0f), 0, kNumKeyColors - 1);
            controller_->uiSetKeyColor(selectedSlot_, kStudioKeyColors[c]);
        } else if (ly >= 348 && ly <= 376 && lx >= 440 && lx <= 812) {
            int g = std::clamp(static_cast<int>((lx - 440.0f) / 126.0f), 0, 2);
            ParamID gId = (g == 0) ? kParamGlobalFakeRR : ((g == 1) ? kParamGlobalFakeVel : kParamGlobalFakeMic);
            bool cur = controller_->getParamNormalized(gId) >= 0.5;
            controller_->uiSetParam(gId, cur ? 0.0 : 1.0);
        } else if (ly >= 384 && ly <= 412 && lx >= 440 && lx <= 812) {
            int d = std::clamp(static_cast<int>((lx - 440.0f) / 126.0f), 0, 2);
            auto& sl = controller_->slotMirror(selectedSlot_);
            if (d == 0) controller_->uiSetSlotProp(selectedSlot_, 5, sl.oneShot ? 0 : 1, 0.0);
            else if (d == 1) controller_->uiSetSlotProp(selectedSlot_, 6, (sl.chokeGroup + 1) % 5, 0.0);
            else controller_->uiSetSlotProp(selectedSlot_, 7, sl.reverse ? 0 : 1, 0.0);
        } else if (lx >= 440 && lx <= 808 && ly >= 426 && ly <= 612) {
            SetCapture(hwnd_);
            int fIdx = std::clamp(static_cast<int>((ly - 426.0f) / 48.0f), 0, 3);
            dragTarget_ = 20 + fIdx;
            controller_->uiSetSlotProp(selectedSlot_, fIdx, 0, std::clamp((lx - 440.0f) / 368.0f, 0.0f, 1.0f));
        }
    }
    // 8. Tab 1: STAGE & MIX
    else if (activeTab_ == 1) {
        if (ly >= 122 && ly <= 148) {
            if (lx >= 440 && lx <= 566) stageGridSnap_ = !stageGridSnap_;
            else if (lx >= 574 && lx <= 706) followMidi_ = !followMidi_;
            else if (lx >= 714 && lx <= 810) controller_->uiResetStage();
        } else if (lx >= 32 && lx <= 808 && ly >= 156 && ly <= 374) {
            auto screenToStage = [&](float px, float py, float& sx, float& sy) {
                float ny = std::clamp((360.0f - py) / 188.0f, 0.0f, 1.0f);
                float halfW = 360.0f - ny * 130.0f;
                float nx = std::clamp((px - 420.0f) / halfW, -1.0f, 1.0f);
                if (stageGridSnap_) { nx = std::round(nx * 4.0f) / 4.0f; ny = std::round(ny * 4.0f) / 4.0f; }
                sx = nx; sy = ny;
            };
            auto stageToScreen = [](float sx, float sy, float& outX, float& outY) {
                float halfW = 360.0f - sy * 130.0f;
                outX = 420.0f + sx * halfW;
                outY = 360.0f - sy * 188.0f;
            };

            int hitSlot = -1;
            float bestDist2 = 24.0f * 24.0f;
            for (int i = 0; i < 12; ++i) {
                int sIdx = activeBank_ * 12 + i;
                const auto& sl = controller_->slotMirror(sIdx);
                if (sl.itemCount <= 0) continue;
                float px, py;
                stageToScreen(sl.stageX, sl.stageY, px, py);
                float d2 = (px - lx) * (px - lx) + (py - ly) * (py - ly);
                if (d2 <= bestDist2) { bestDist2 = d2; hitSlot = sIdx; }
            }
            if (hitSlot >= 0) {
                selectedSlot_ = hitSlot;
                controller_->uiTriggerNote(controller_->slotMirror(selectedSlot_).midiNote, 0.85f, true);
                SetCapture(hwnd_);
                dragTarget_ = 1;
                float nx, ny;
                screenToStage(lx, ly, nx, ny);
                controller_->uiSetStageXY(selectedSlot_, nx, ny);
            } else if (controller_->slotMirror(selectedSlot_).itemCount > 0) {
                SetCapture(hwnd_);
                dragTarget_ = 1;
                float nx, ny;
                screenToStage(lx, ly, nx, ny);
                controller_->uiSetStageXY(selectedSlot_, nx, ny);
            }
        } else if (ly >= 384 && ly <= 418) {
            if (lx >= 32 && lx <= 408) {
                SetCapture(hwnd_); dragTarget_ = 60;
                float nx = std::clamp((lx - 32.0f) / 376.0f, 0.0f, 1.0f) * 2.0f - 1.0f;
                controller_->uiSetStageXY(selectedSlot_, nx, controller_->slotMirror(selectedSlot_).stageY);
            } else if (lx >= 432 && lx <= 808) {
                SetCapture(hwnd_); dragTarget_ = 61;
                float ny = std::clamp((lx - 432.0f) / 376.0f, 0.0f, 1.0f);
                controller_->uiSetStageXY(selectedSlot_, controller_->slotMirror(selectedSlot_).stageX, ny);
            }
        } else if (ly >= 424 && ly <= 612) {
            if (lx >= 32 && lx <= 114) stageSubMode_ = 0;
            else if (lx >= 726 && lx <= 808) stageSubMode_ = 1;
            else if (stageSubMode_ == 0) {
                if (lx >= 132 && lx <= 408 && ly >= 432 && ly <= 596) {
                    SetCapture(hwnd_);
                    int row = std::clamp(static_cast<int>((ly - 432.0f) / 60.0f), 0, 2);
                    static const int props[3] = { 0, 1, 10 };
                    dragTarget_ = 40 + row;
                    controller_->uiSetSlotProp(selectedSlot_, props[row], 0, std::clamp((lx - 132.0f) / 276.0f, 0.0f, 1.0f));
                } else if (lx >= 432 && lx <= 708 && ly >= 432 && ly <= 596) {
                    SetCapture(hwnd_);
                    int row = std::clamp(static_cast<int>((ly - 432.0f) / 60.0f), 0, 2);
                    static const int props[3] = { 11, 2, 3 };
                    dragTarget_ = 43 + row;
                    controller_->uiSetSlotProp(selectedSlot_, props[row], 0, std::clamp((lx - 432.0f) / 276.0f, 0.0f, 1.0f));
                }
            } else {
                if (lx >= 134 && lx <= 648 && ly >= 432 && ly <= 608) {
                    SetCapture(hwnd_);
                    int b = std::clamp(static_cast<int>((ly - 432.0f) / 44.0f), 0, 3);
                    dragTarget_ = 10 + b;
                    controller_->uiSetParam(kParamBusCloseGain + b, std::clamp((lx - 134.0f) / 514.0f, 0.0f, 1.0f));
                } else if (lx >= 660 && lx <= 710 && ly >= 432 && ly <= 608) {
                    int b = std::clamp(static_cast<int>((ly - 432.0f) / 44.0f), 0, 3);
                    bool byp = controller_->getParamNormalized(kParamBusCloseBypass + b) >= 0.5;
                    controller_->uiSetParam(kParamBusCloseBypass + b, byp ? 0.0 : 1.0);
                }
            }
        }
    }
    // 9. Tab 2: MASTER FX (6 Modules + LED On/Off + Expanded Editor + Global Mono Sub & Limiter)
    else if (activeTab_ == 2) {
        static const ParamID modOnIds[6] = { kParamFilterOn, kParamEqOn, kParamCompOn, kParamSatOn, kParamDelayOn, kParamRevOn };
        if (ly >= 128 && ly <= 202 && lx >= 28 && lx <= 812) {
            int m = std::clamp(static_cast<int>((lx - 28.0f) / 131.0f), 0, 5);
            float bx = 28.0f + m * 131.0f;
            float ledX = bx + 123.0f - 16.0f, ledY = 128.0f + 14.0f;
            if ((lx - ledX) * (lx - ledX) + (ly - ledY) * (ly - ledY) <= 13.0f * 13.0f) {
                bool cur = controller_->getParamNormalized(modOnIds[m]) >= 0.5;
                controller_->uiSetParam(modOnIds[m], cur ? 0.0 : 1.0);
            } else {
                activeFxModule_ = m;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }

        if (ly >= 548 && ly <= 594) {
            if (lx >= 44 && lx <= 400) {
                bool mb = controller_->getParamNormalized(kParamMonoBass) >= 0.5;
                controller_->uiSetParam(kParamMonoBass, mb ? 0.0 : 1.0);
            } else if (lx >= 436 && lx <= 792) {
                bool lim = controller_->getParamNormalized(kParamLimiterOn) >= 0.5;
                controller_->uiSetParam(kParamLimiterOn, lim ? 0.0 : 1.0);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }

        if (activeFxModule_ == 0) {
            if (ly >= 260 && ly <= 296 && lx >= 44 && lx <= 796) {
                int t = std::clamp(static_cast<int>((lx - 44.0f) / 94.0f), 0, 7);
                controller_->uiSetParam(kParamFilterType, t / 7.0);
            } else if (ly >= 328 && ly <= 370 && lx >= 44 && lx <= 792) {
                SetCapture(hwnd_); dragTarget_ = 70;
                controller_->uiSetParam(kParamFilterCutoff, std::clamp((lx - 44.0f) / 748.0f, 0.0f, 1.0f));
            } else if (ly >= 408 && ly <= 450 && lx >= 44 && lx <= 792) {
                SetCapture(hwnd_); dragTarget_ = 71;
                controller_->uiSetParam(kParamFilterRes, std::clamp((lx - 44.0f) / 748.0f, 0.0f, 1.0f));
            }
        } else if (activeFxModule_ == 1) {
            if (ly >= 250 && ly <= 386 && lx >= 44 && lx <= 792) {
                SetCapture(hwnd_);
                int band = (lx < 293.0f) ? 0 : ((lx < 542.0f) ? 1 : 2);
                dragTarget_ = 80 + band;
                onMouseMove(lx, ly);
            } else if (ly >= 400 && ly <= 440) {
                SetCapture(hwnd_);
                int col = std::clamp(static_cast<int>((lx - 44.0f) / 256.0f), 0, 2);
                dragTarget_ = 83 + col;
                onMouseMove(lx, ly);
            } else if (ly >= 458 && ly <= 498) {
                SetCapture(hwnd_);
                int col = std::clamp(static_cast<int>((lx - 44.0f) / 256.0f), 0, 2);
                dragTarget_ = 86 + col;
                onMouseMove(lx, ly);
            }
        } else if (activeFxModule_ == 2) {
            if (ly >= 268 && ly <= 452) {
                int row = std::clamp(static_cast<int>((ly - 268.0f) / 70.0f), 0, 2);
                int col = (lx < 420.0f) ? 0 : 1;
                SetCapture(hwnd_);
                dragTarget_ = 90 + row * 2 + col;
                onMouseMove(lx, ly);
            }
        } else if (activeFxModule_ == 3) {
            if (ly >= 278 && ly <= 470 && lx >= 44 && lx <= 792) {
                int row = std::clamp(static_cast<int>((ly - 278.0f) / 74.0f), 0, 2);
                SetCapture(hwnd_);
                dragTarget_ = 100 + row;
                onMouseMove(lx, ly);
            }
        } else if (activeFxModule_ == 4) {
            if (ly >= 224 && ly <= 254 && lx >= 520 && lx <= 792) {
                int dm = (lx < 660.0f) ? 0 : 1;
                controller_->uiSetParam(kParamDelayMode, dm ? 1.0 : 0.0);
            } else if (ly >= 272 && ly <= 454) {
                int row = std::clamp(static_cast<int>((ly - 272.0f) / 70.0f), 0, 2);
                int col = (lx < 420.0f) ? 0 : 1;
                SetCapture(hwnd_);
                dragTarget_ = (row == 2) ? 114 : (110 + row * 2 + col);
                onMouseMove(lx, ly);
            }
        } else if (activeFxModule_ == 5) {
            if (ly >= 224 && ly <= 254 && lx >= 500 && lx <= 792) {
                int rm = std::clamp(static_cast<int>((lx - 500.0f) / 98.0f), 0, 2);
                controller_->uiSetParam(kParamRevMode, rm * 0.5);
            } else if (ly >= 272 && ly <= 454) {
                int row = std::clamp(static_cast<int>((ly - 272.0f) / 70.0f), 0, 2);
                int col = (lx < 420.0f) ? 0 : 1;
                SetCapture(hwnd_);
                dragTarget_ = 120 + row * 2 + col;
                onMouseMove(lx, ly);
            }
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WreckageWinView::onMouseMove(float lx, float ly) {
    if (dragTarget_ == 0) return;

    if (dragTarget_ == 1) {
        float ny = std::clamp((360.0f - ly) / 188.0f, 0.0f, 1.0f);
        float halfW = 360.0f - ny * 130.0f;
        float nx = std::clamp((lx - 420.0f) / halfW, -1.0f, 1.0f);
        if (stageGridSnap_) { nx = std::round(nx * 4.0f) / 4.0f; ny = std::round(ny * 4.0f) / 4.0f; }
        controller_->uiSetStageXY(selectedSlot_, nx, ny);
    } else if (dragTarget_ == 2) {
        controller_->uiSetParam(kParamPunishAmount, std::clamp((lx - 138.0f) / 350.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 10 && dragTarget_ <= 13) {
        controller_->uiSetParam(kParamBusCloseGain + (dragTarget_ - 10), std::clamp((lx - 134.0f) / 514.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 20 && dragTarget_ <= 23) {
        controller_->uiSetSlotProp(selectedSlot_, dragTarget_ - 20, 0, std::clamp((lx - 440.0f) / 368.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 40 && dragTarget_ <= 42) {
        static const int props[3] = { 0, 1, 10 };
        controller_->uiSetSlotProp(selectedSlot_, props[dragTarget_ - 40], 0, std::clamp((lx - 132.0f) / 276.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 43 && dragTarget_ <= 45) {
        static const int props[3] = { 11, 2, 3 };
        controller_->uiSetSlotProp(selectedSlot_, props[dragTarget_ - 43], 0, std::clamp((lx - 432.0f) / 276.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 50 && dragTarget_ <= 55) {
        // Wave Editor & Velocity Grid direct mouse editing
        auto& sl = controller_->slotMirror(selectedSlot_);
        if (sl.itemCount > 0 && selectedWaveItem_ < static_cast<int>(sl.waveItems.size())) {
            auto& wi = sl.waveItems[static_cast<size_t>(selectedWaveItem_)];
            float wx = 440.0f, ww = 286.0f;
            float gy = 154.0f, gh = 156.0f;
            if (dragTarget_ == 50) {
                wi.startNorm = std::clamp((lx - wx) / ww, 0.0f, wi.endNorm - 0.02f);
            } else if (dragTarget_ == 51) {
                wi.endNorm = std::clamp((lx - wx) / ww, wi.startNorm + 0.02f, 1.0f);
            } else if (dragTarget_ == 52) {
                float regW = std::max(0.02f, wi.endNorm - wi.startNorm);
                float posN = std::clamp((lx - wx) / ww, wi.startNorm, wi.endNorm);
                wi.fadeInNorm = std::clamp((posN - wi.startNorm) / regW, 0.0f, 0.5f);
            } else if (dragTarget_ == 53) {
                float regW = std::max(0.02f, wi.endNorm - wi.startNorm);
                float posN = std::clamp((lx - wx) / ww, wi.startNorm, wi.endNorm);
                wi.fadeOutNorm = std::clamp((wi.endNorm - posN) / regW, 0.0f, 0.5f);
            } else if (dragTarget_ == 54) {
                float v = 1.0f - std::clamp((ly - gy) / gh, 0.0f, 1.0f);
                wi.velMax = std::clamp(std::round(v * 20.0f) / 20.0f, wi.velMin + 0.05f, 1.0f);
            } else if (dragTarget_ == 55) {
                float v = 1.0f - std::clamp((ly - gy) / gh, 0.0f, 1.0f);
                wi.velMin = std::clamp(std::round(v * 20.0f) / 20.0f, 0.0f, wi.velMax - 0.05f);
            }
            controller_->uiUpdateWaveEdit(selectedSlot_, selectedWaveItem_);
        }
    } else if (dragTarget_ == 60) {
        float nx = std::clamp((lx - 32.0f) / 376.0f, 0.0f, 1.0f) * 2.0f - 1.0f;
        controller_->uiSetStageXY(selectedSlot_, nx, controller_->slotMirror(selectedSlot_).stageY);
    } else if (dragTarget_ == 61) {
        float ny = std::clamp((lx - 432.0f) / 376.0f, 0.0f, 1.0f);
        controller_->uiSetStageXY(selectedSlot_, controller_->slotMirror(selectedSlot_).stageX, ny);
    } else if (dragTarget_ == 70) {
        controller_->uiSetParam(kParamFilterCutoff, std::clamp((lx - 44.0f) / 748.0f, 0.0f, 1.0f));
    } else if (dragTarget_ == 71) {
        controller_->uiSetParam(kParamFilterRes, std::clamp((lx - 44.0f) / 748.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 80 && dragTarget_ <= 82) {
        int b = dragTarget_ - 80;
        float gNorm = 1.0f - std::clamp((ly - 250.0f) / 136.0f, 0.0f, 1.0f);
        static const ParamID gIds[3] = { kParamEqLowGain, kParamEqMidGain, kParamEqHighGain };
        controller_->uiSetParam(gIds[b], gNorm);
    } else if (dragTarget_ >= 83 && dragTarget_ <= 88) {
        int idx = dragTarget_ - 83;
        int col = idx % 3;
        float x0 = 44.0f + col * 256.0f;
        float norm = std::clamp((lx - x0) / 236.0f, 0.0f, 1.0f);
        static const ParamID ids[6] = { kParamEqLowFreq, kParamEqMidFreq, kParamEqHighFreq, kParamEqLowGain, kParamEqMidGain, kParamEqHighGain };
        controller_->uiSetParam(ids[idx], norm);
    } else if (dragTarget_ >= 90 && dragTarget_ <= 95) {
        int idx = dragTarget_ - 90;
        float x0 = (idx % 2 == 0) ? 44.0f : 436.0f;
        static const ParamID ids[6] = { kParamCompThresh, kParamCompRatio, kParamCompAttack, kParamCompRelease, kParamCompKnee, kParamCompMakeup };
        controller_->uiSetParam(ids[idx], std::clamp((lx - x0) / 356.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 100 && dragTarget_ <= 102) {
        static const ParamID ids[3] = { kParamSatDrive, kParamSatWarmth, kParamSatMix };
        controller_->uiSetParam(ids[dragTarget_ - 100], std::clamp((lx - 44.0f) / 748.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 110 && dragTarget_ <= 114) {
        int idx = dragTarget_ - 110;
        if (idx == 4) controller_->uiSetParam(kParamDelayMix, std::clamp((lx - 44.0f) / 748.0f, 0.0f, 1.0f));
        else {
            float x0 = (idx % 2 == 0) ? 44.0f : 436.0f;
            static const ParamID ids[4] = { kParamDelayTime, kParamDelayFeedback, kParamDelayTone, kParamDelayWidth };
            controller_->uiSetParam(ids[idx], std::clamp((lx - x0) / 356.0f, 0.0f, 1.0f));
        }
    } else if (dragTarget_ >= 120 && dragTarget_ <= 125) {
        int idx = dragTarget_ - 120;
        float x0 = (idx % 2 == 0) ? 44.0f : 436.0f;
        static const ParamID ids[6] = { kParamRevPreDelay, kParamRevDecay, kParamRevSize, kParamRevLowCut, kParamRevHighCut, kParamRevMix };
        controller_->uiSetParam(ids[idx], std::clamp((lx - x0) / 356.0f, 0.0f, 1.0f));
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WreckageWinView::onMouseUp() {
    if (GetCapture() == hwnd_) ReleaseCapture();
    dragTarget_ = 0;
    if (activePressedNote_ >= 0) {
        controller_->uiTriggerNote(activePressedNote_, 0.0f, false);
        activePressedNote_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

LRESULT CALLBACK WreckageWinView::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WreckageWinView* self = reinterpret_cast<WreckageWinView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<WreckageWinView*>(cs->lpCreateParams);
        if (self) { self->hwnd_ = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
    }
    return self ? self->handleMessage(hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT WreckageWinView::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: {
            UINT w = std::max<UINT>(1, LOWORD(lParam));
            UINT h = std::max<UINT>(1, HIWORD(lParam));
            uiScale_ = static_cast<float>(std::min(w, h)) / static_cast<float>(kBaseUiSize);
            if (rt_) rt_->Resize(D2D1::SizeU(w, h));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            paint();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            if (controller_) controller_->decSaveFlash();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDBLCLK:
            onMouseDoubleClick(GET_X_LPARAM(lParam) / uiScale_, GET_Y_LPARAM(lParam) / uiScale_);
            return 0;
        case WM_LBUTTONDOWN:
            onMouseDown(GET_X_LPARAM(lParam) / uiScale_, GET_Y_LPARAM(lParam) / uiScale_);
            return 0;
        case WM_MOUSEMOVE:
            if (dragTarget_ != 0) onMouseMove(GET_X_LPARAM(lParam) / uiScale_, GET_Y_LPARAM(lParam) / uiScale_);
            return 0;
        case WM_LBUTTONUP:
            onMouseUp();
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == 9001 && HIWORD(wParam) == EN_KILLFOCUS) commitInlineRename();
            return 0;
        case WM_KEYDOWN:
            if (renameEdit_ && wParam == VK_RETURN) { commitInlineRename(); return 0; }
            if (renameEdit_ && wParam == VK_ESCAPE) { cancelInlineRename(); return 0; }
            if (HWND p = GetParent(hwnd)) PostMessageW(p, msg, wParam, lParam);
            return 0;
        case WM_DROPFILES: {
            HDROP hDrop = reinterpret_cast<HDROP>(wParam);
            POINT pt{};
            int targetSlot = selectedSlot_;
            if (DragQueryPoint(hDrop, &pt)) {
                float lx = pt.x / uiScale_, ly = pt.y / uiScale_;
                if (activeTab_ == 0 && ly >= 192 && ly <= 552 && lx >= 26 && lx <= 400) {
                    int col = std::clamp(static_cast<int>((lx - 26.0f) / 126.0f), 0, 2);
                    int row = std::clamp(static_cast<int>((ly - 192.0f) / 90.0f), 0, 3);
                    targetSlot = activeBank_ * 12 + row * 3 + col;
                    selectedSlot_ = targetSlot;
                }
            }
            UINT n = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; ++i) {
                wchar_t buf[MAX_PATH] = {0};
                if (DragQueryFileW(hDrop, i, buf, MAX_PATH) > 0) {
                    controller_->uiLoadWavFile(targetSlot, buf);
                }
            }
            selectedWaveItem_ = std::max(0, controller_->slotMirror(targetSlot).itemCount);
            DragFinish(hDrop);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace NoiseMu6ix
#endif
