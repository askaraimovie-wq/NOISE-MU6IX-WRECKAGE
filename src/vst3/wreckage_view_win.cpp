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

static const wchar_t* kWndClassName = L"NoiseMu6ixWreckageD2DViewClass_v102";

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
    int zIdx = 1;
    if (controller_) {
        zIdx = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamUiZoom) * 2.0)), 0, 2);
    }
    uiScale_ = (zIdx == 0) ? 0.50f : ((zIdx == 1) ? 0.75f : 1.00f);
    int px = static_cast<int>(std::round(kBaseUiSize * uiScale_));
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    if (screenH > 0 && px > screenH - 120) {
        uiScale_ = 0.75f;
        px = static_cast<int>(std::round(kBaseUiSize * uiScale_));
    }
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
        if (pw >= 200 && ph >= 200) {
            px = std::min(pw, ph);
        }
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
                                const ThemePalette& th, bool accentFill, bool enabled) {
    val01 = std::clamp(val01, 0.0f, 1.0f);
    drawText(label, x, y - 16.0f, w, 15.0f, enabled ? th.textPrimary : th.textMuted, 10.5f, true);
    brush_->SetColor(toD2D(th.faderTrack));
    rt_->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush_);
    brush_->SetColor(toD2D(enabled ? (accentFill ? th.accent : th.faderFill) : th.textMuted));
    rt_->FillRectangle(D2D1::RectF(x, y, x + w * val01, y + h), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(x, y, x + w, y + h), brush_, 1.4f);

    float tx = x + w * val01 - 5.0f;
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(tx, y - 3.0f, tx + 10.0f, y + h + 3.0f), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(tx, y - 3.0f, tx + 10.0f, y + h + 3.0f), brush_, 1.6f);
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

    if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) {
        discardDeviceResources();
    }
}

void WreckageWinView::drawHeader(const ThemePalette& th) {
    brush_->SetColor(toD2D(th.headerBg));
    rt_->FillRectangle(D2D1::RectF(0, 0, 840, 68), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawLine(D2D1::Point2F(0, 68), D2D1::Point2F(840, 68), brush_, 1.6f);

    // Tight brand lockup: NOISE MU6IX directly above WRECKAGE with v1.0.2 bottom-right
    drawText(L"NOISE MU6IX", 16, 10, 200, 14, th.textMuted, 10.0f, true);
    drawText(L"WRECKAGE", 15, 22, 175, 36, th.textPrimary, 28.0f, true);
    drawText(L"v" WRECKAGE_VERSION_STR, 186, 42, 46, 16, th.accent, 10.0f, true);

    // Center Preset Box + Direct SAVE Button
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(238, 12, 520, 56), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(238, 12, 520, 56), brush_, 1.5f);
    std::wstring rootLabel = controller_->rootLibraryDir().empty()
        ? L"PRESET / KIT  [CLICK TO BROWSE]"
        : (L"LIB: " + std::filesystem::path(controller_->rootLibraryDir()).filename().wstring());
    drawText(rootLabel.c_str(), 246, 14, 268, 16, th.textMuted, 9.5f, true);
    std::wstring pName = toWide(controller_->activePresetName());
    drawText(pName.c_str(), 246, 30, 268, 22, th.textPrimary, 12.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    bool savedFlash = controller_->saveFlashTicks() > 0;
    brush_->SetColor(toD2D(savedFlash ? 0x2E7D32 : th.accent));
    rt_->FillRectangle(D2D1::RectF(526, 12, 594, 56), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(526, 12, 594, 56), brush_, 1.5f);
    drawText(savedFlash ? L"SAVED!" : L"SAVE", 526, 12, 68, 44, 0xFFFFFF, 11.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    // Kontakt-style Telemetry Readout (Voices, RAM, CPU) + Panic (!) + Settings Gear
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(602, 12, 744, 56), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(602, 12, 744, 56), brush_, 1.4f);

    int vox = controller_->liveVoices();
    double ramMb = static_cast<double>(controller_->liveRamBytes()) / (1024.0 * 1024.0);
    wchar_t line1[64], line2[64];
    swprintf(line1, 64, L"VOX: %d   RAM: %.1fM", vox, ramMb);
    swprintf(line2, 64, L"ENGINE: %s", vox > 0 ? L"ACTIVE" : L"IDLE");
    drawText(line1, 608, 14, 132, 18, th.textPrimary, 9.5f, true);
    drawText(line2, 608, 34, 132, 18, vox > 0 ? th.accent : th.textMuted, 9.5f, true);

    // Panic (!) Button
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(750, 12, 784, 56), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(750, 12, 784, 56), brush_, 1.4f);
    drawText(L"!", 750, 12, 34, 44, th.accent, 16.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    // Settings Gear Button
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
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(16, 120, 412, 624), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(16, 120, 412, 624), brush_, 1.6f);

    drawText(L"3-OCTAVE SOURCE BANK (C3 - B5  |  DBL-CLICK RENAME)", 26, 128, 380, 22, th.textPrimary, 10.5f, true);
    const wchar_t* banks[3] = { L"BANK 1 (C3-B3)", L"BANK 2 (C4-B4)", L"BANK 3 (C5-B5)" };
    for (int b = 0; b < 3; ++b) {
        float bx = 26.0f + b * 126.0f;
        brush_->SetColor(toD2D(b == activeBank_ ? th.border : th.headerBg));
        rt_->FillRectangle(D2D1::RectF(bx, 156, bx + 120, 184), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(bx, 156, bx + 120, 184), brush_, 1.2f);
        drawText(banks[b], bx, 156, 120, 28, b == activeBank_ ? th.panelBg : th.textPrimary, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    for (int i = 0; i < 12; ++i) {
        int sIdx = activeBank_ * 12 + i;
        const auto& sl = controller_->slotMirror(sIdx);
        int col = i % 3, row = i / 3;
        float px = 26.0f + col * 126.0f;
        float py = 196.0f + row * 90.0f;
        bool sel = (sIdx == selectedSlot_);
        brush_->SetColor(toD2D(sel ? th.border : th.bg));
        rt_->FillRectangle(D2D1::RectF(px, py, px + 120, py + 80), brush_);
        if (sl.hasCustomColor) {
            brush_->SetColor(toD2D(sl.keyColor));
            rt_->FillRectangle(D2D1::RectF(px, py, px + 120, py + 8), brush_);
        }
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(px, py, px + 120, py + 80), brush_, sel ? 2.2f : 1.2f);
        drawText(toWide(sl.name).c_str(), px + 4, py + 14, 112, 34, sel ? th.panelBg : th.textPrimary, 11.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        std::wstring info = sl.purged
            ? L"PURGED"
            : (midiNoteName(sl.midiNote) + L"  V:" + std::to_wstring(sl.velCount) + L" RR:" + std::to_wstring(sl.rrCount));
        drawText(info.c_str(), px + 4, py + 50, 112, 22, sel ? th.headerBg : th.textMuted, 9.5f, false, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    brush_->SetColor(toD2D(th.border));
    rt_->FillRectangle(D2D1::RectF(26, 568, 152, 608), brush_);
    drawText(L"+ LOAD WAV", 26, 568, 126, 40, th.panelBg, 11.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    brush_->SetColor(toD2D(th.accent));
    rt_->FillRectangle(D2D1::RectF(162, 568, 288, 608), brush_);
    drawText(L"SAVE AS .nm6", 162, 568, 126, 40, 0xFFFFFF, 10.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    bool selPurged = controller_->slotMirror(selectedSlot_).purged;
    brush_->SetColor(toD2D(selPurged ? th.accent : th.headerBg));
    rt_->FillRectangle(D2D1::RectF(298, 568, 402, 608), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(298, 568, 402, 608), brush_, 1.2f);
    drawText(selPurged ? L"UNPURGE" : L"MUTE/PURGE", 298, 568, 104, 40, selPurged ? 0xFFFFFF : th.textPrimary, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    const auto& sl = controller_->slotMirror(selectedSlot_);
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(428, 120, 824, 624), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(428, 120, 824, 624), brush_, 1.6f);

    std::wstring hdr = L"INSPECTOR (" + midiNoteName(sl.midiNote) + L"): " + toWide(sl.name);
    drawText(hdr.c_str(), 440, 128, 370, 24, th.textPrimary, 12.0f, true);

    drawText(L"KEY COLOR:", 440, 158, 86, 20, th.textMuted, 10.0f, true);
    for (int c = 0; c < kNumKeyColors; ++c) {
        float sx = 528.0f + c * 23.0f;
        brush_->SetColor(toD2D(kStudioKeyColors[c]));
        rt_->FillRectangle(D2D1::RectF(sx, 158, sx + 19, 178), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(sx, 158, sx + 19, 178), brush_, (sl.hasCustomColor && sl.keyColor == kStudioKeyColors[c]) ? 2.4f : 1.0f);
    }

    const wchar_t* modeTxt[3] = { L"AUTO", L"ON", L"OFF" };
    std::wstring pills[3] = {
        std::wstring(L"RR: ")  + modeTxt[sl.rrMode % 3],
        std::wstring(L"VEL: ") + modeTxt[sl.velMode % 3],
        std::wstring(L"MIC: ") + modeTxt[sl.micMode % 3]
    };
    int modes[3] = { sl.rrMode, sl.velMode, sl.micMode };
    for (int p = 0; p < 3; ++p) {
        float px = 440.0f + p * 126.0f;
        uint32_t bg = (modes[p] == 0) ? th.accent : ((modes[p] == 1) ? th.border : th.headerBg);
        uint32_t fg = (modes[p] == 2) ? th.textPrimary : 0xFFFFFF;
        brush_->SetColor(toD2D(bg));
        rt_->FillRectangle(D2D1::RectF(px, 194, px + 118, 226), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(px, 194, px + 118, 226), brush_, 1.2f);
        drawText(pills[p].c_str(), px, 194, 118, 32, fg, 11.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    std::wstring m1 = sl.oneShot ? L"MODE: ONE-SHOT" : L"MODE: GATE";
    std::wstring m2 = sl.chokeGroup == 0 ? L"CHOKE: OFF" : (L"CHOKE: " + std::to_wstring(sl.chokeGroup));
    std::wstring m3 = sl.reverse ? L"REV: ON" : L"REV: OFF";
    const wchar_t* dModes[3] = { m1.c_str(), m2.c_str(), m3.c_str() };
    for (int d = 0; d < 3; ++d) {
        float dx = 440.0f + d * 126.0f;
        brush_->SetColor(toD2D(th.headerBg));
        rt_->FillRectangle(D2D1::RectF(dx, 240, dx + 118, 270), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(dx, 240, dx + 118, 270), brush_, 1.2f);
        drawText(dModes[d], dx, 240, 118, 30, th.textPrimary, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    drawFader(440, 310, 368, 16, sl.gain,    L"GAIN",    th);
    drawFader(440, 368, 368, 16, sl.pitch,   L"PITCH (-24 / +24 ST)", th);
    drawFader(440, 426, 368, 16, sl.attack,  L"ATTACK",  th);
    drawFader(440, 484, 368, 16, sl.release, L"RELEASE", th);
    drawFader(440, 542, 368, 16, sl.xfade,   L"XFADE",   th);
}

void WreckageWinView::drawStageTab(const ThemePalette& th) {
    // Full-Width Damage 2 Inspired 3D Perspective Stage + DRUM / MIX Switchable Lower Rack
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(16, 116, 824, 626), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(16, 116, 824, 626), brush_, 1.6f);

    const auto& selSl = controller_->slotMirror(selectedSlot_);
    std::wstring stageTitle = midiNoteName(selSl.midiNote) + L" : " + toWide(selSl.name);
    drawText(stageTitle.c_str(), 28, 122, 380, 26, th.textPrimary, 13.0f, true);

    // Stage Top Toolbar: GRID SNAP, FOLLOW MIDI, RESET SPREAD
    auto drawMiniBtn = [&](float x, float w, const wchar_t* txt, bool on) {
        brush_->SetColor(toD2D(on ? th.accent : th.headerBg));
        rt_->FillRectangle(D2D1::RectF(x, 122, x + w, 148), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(x, 122, x + w, 148), brush_, 1.2f);
        drawText(txt, x, 122, w, 26, on ? 0xFFFFFF : th.textPrimary, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    drawMiniBtn(440, 126, stageGridSnap_ ? L"GRID SNAP: ON" : L"GRID SNAP: OFF", stageGridSnap_);
    drawMiniBtn(574, 132, followMidi_ ? L"FOLLOW MIDI: ON" : L"FOLLOW MIDI: OFF", followMidi_);
    drawMiniBtn(714, 96,  L"RESET STAGE", false);

    // 3D Perspective Dotted Stage Box (x: 32..808, y: 156..420)
    brush_->SetColor(toD2D(th.bg));
    rt_->FillRectangle(D2D1::RectF(32, 156, 808, 420), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(32, 156, 808, 420), brush_, 1.4f);

    // Perspective trapezoid grid lines & dots
    // Top (Back/Far) width = 460 (x: 190..650 at y=172), Bottom (Front/Close) width = 720 (x: 60..780 at y=406)
    auto stageToScreen = [](float sx, float sy, float& outX, float& outY) {
        float ny = std::clamp(sy, 0.0f, 1.0f);
        float nx = std::clamp(sx, -1.0f, 1.0f);
        float halfW = 360.0f - ny * 130.0f; // 360 at front, 230 at back
        outX = 420.0f + nx * halfW;
        outY = 406.0f - ny * 234.0f;
    };

    brush_->SetColor(toD2D(th.textMuted, 0.35f));
    for (int gy = 0; gy <= 4; ++gy) {
        float yNorm = gy / 4.0f;
        float xL, yL, xR, yR;
        stageToScreen(-1.0f, yNorm, xL, yL);
        stageToScreen( 1.0f, yNorm, xR, yR);
        rt_->DrawLine(D2D1::Point2F(xL, yL), D2D1::Point2F(xR, yR), brush_, 1.0f);
        // Dotted grid nodes
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

    drawText(L"BACK  (HALL / FAR)", 32, 156, 776, 16, th.textMuted, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    drawText(L"FRONT  (CLOSE / DRY)", 32, 404, 776, 16, th.textMuted, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    // Draw all 11 non-selected slots first, then draw SELECTED slot LAST on top with selection bracket!
    auto drawOneStageDot = [&](int sIdx, int displayNum, bool isSelected) {
        const auto& sl = controller_->slotMirror(sIdx);
        float nx, ny;
        stageToScreen(sl.stageX, sl.stageY, nx, ny);
        float rad = isSelected ? 16.0f : 11.5f;
        brush_->SetColor(toD2D(sl.hasCustomColor ? sl.keyColor : th.accent));
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(nx, ny), rad, rad), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(nx, ny), rad, rad), brush_, isSelected ? 2.4f : 1.2f);
        if (isSelected) {
            // Damage 2 style corner selection ring
            brush_->SetColor(toD2D(th.accent));
            rt_->DrawRectangle(D2D1::RectF(nx - 21.0f, ny - 21.0f, nx + 21.0f, ny + 21.0f), brush_, 1.8f);
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

    // Lower Rack: Left = DRUM button, Right = MIX button, Center = 6 Per-Drum Faders OR 4 Mic Faders
    brush_->SetColor(toD2D(stageSubMode_ == 0 ? th.border : th.headerBg));
    rt_->FillRectangle(D2D1::RectF(32, 434, 114, 610), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(32, 434, 114, 610), brush_, 1.5f);
    drawText(L"DRUM", 32, 434, 82, 176, stageSubMode_ == 0 ? th.panelBg : th.textPrimary, 13.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    brush_->SetColor(toD2D(stageSubMode_ == 1 ? th.border : th.headerBg));
    rt_->FillRectangle(D2D1::RectF(726, 434, 808, 610), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(726, 434, 808, 610), brush_, 1.5f);
    drawText(L"MIX", 726, 434, 82, 176, stageSubMode_ == 1 ? th.panelBg : th.textPrimary, 13.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    if (stageSubMode_ == 0) {
        // Per-Drum Horizontal Faders (VOL, TUNE, TONE LOW, TONE HI, ATTACK, DECAY)
        drawFader(132, 456, 276, 16, selSl.gain,     L"VOL (PER-DRUM)",   th, true);
        drawFader(132, 514, 276, 16, selSl.pitch,    L"TUNE (-24/+24 ST)", th);
        drawFader(132, 572, 276, 16, selSl.toneLow,  L"TONE LOW",          th);

        drawFader(432, 456, 276, 16, selSl.toneHigh, L"TONE HI",           th);
        drawFader(432, 514, 276, 16, selSl.attack,   L"ATTACK",            th);
        drawFader(432, 572, 276, 16, selSl.release,  L"DECAY / RELEASE",   th);
    } else {
        // 4-Mic Mixer (CLOSE, ROOM, HALL, SUB) + Individual On/Off Buttons
        const wchar_t* micNames[4] = {
            L"CLOSE MIC (DIRECT PUNCH)",
            L"ROOM MIC (EARLY REFLECTIONS)",
            L"HALL MIC (SCORING SPACE)",
            L"SUB MIC (LOW IMPACT)"
        };
        for (int b = 0; b < 4; ++b) {
            float y = 452.0f + b * 42.0f;
            float val = static_cast<float>(controller_->getParamNormalized(kParamBusCloseGain + b));
            bool byp = controller_->getParamNormalized(kParamBusCloseBypass + b) >= 0.5;
            drawFader(134, y, 514, 14, val, micNames[b], th, !byp && b == 2, !byp);

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
    rt_->FillRectangle(D2D1::RectF(16, 120, 824, 624), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(16, 120, 824, 624), brush_, 1.6f);
    drawText(L"MASTER FX RACK  |  INDIVIDUAL BYPASS SWITCHES FOR LOW CPU", 28, 128, 780, 24, th.textPrimary, 12.0f, true);

    bool filtOn  = controller_->getParamNormalized(kParamFilterOn) >= 0.5;
    bool eqOn    = controller_->getParamNormalized(kParamEqOn) >= 0.5;
    bool compOn  = controller_->getParamNormalized(kParamCompOn) >= 0.5;
    bool transOn = controller_->getParamNormalized(kParamTransientOn) >= 0.5;

    struct FxDef { ParamID id; const wchar_t* name; bool accent; bool enabled; };
    const FxDef leftFx[4] = {
        { kParamMasterHPF, L"HIGH-PASS FILTER (20 - 400 Hz)", false, filtOn },
        { kParamMasterLPF, L"LOW-PASS FILTER (2 - 20 kHz)", false, filtOn },
        { kParamEqLow,     L"3-BAND EQ: LOW (90 Hz)", false, eqOn },
        { kParamEqMid,     L"3-BAND EQ: MID (1.1 kHz)", false, eqOn }
    };
    const FxDef rightFx[4] = {
        { kParamEqHigh,         L"3-BAND EQ: HIGH (7.5 kHz)", false, eqOn },
        { kParamCompAmount,     L"BUS COMPRESSOR", false, compOn },
        { kParamTransientPunch, L"TRANSIENT PUNCH SHAPER", true, transOn },
        { kParamStereoWidth,    L"STEREO WIDTH", false, true }
    };

    for (int i = 0; i < 4; ++i) {
        float y = 190.0f + i * 84.0f;
        drawFader(36,  y, 356, 18, static_cast<float>(controller_->getParamNormalized(leftFx[i].id)),  leftFx[i].name,  th, leftFx[i].accent, leftFx[i].enabled);
        drawFader(442, y, 356, 18, static_cast<float>(controller_->getParamNormalized(rightFx[i].id)), rightFx[i].name, th, rightFx[i].accent, rightFx[i].enabled);
    }

    // Bottom Effect Module On/Off Switches (FILTER, EQ, COMP, TRANSIENT, MONO BASS, LIMITER)
    bool mb  = controller_->getParamNormalized(kParamMonoBass) >= 0.5;
    bool lim = controller_->getParamNormalized(kParamLimiterOn) >= 0.5;

    struct ModBtn { const wchar_t* label; bool on; };
    ModBtn mods[6] = {
        { filtOn  ? L"FILTER: ON" : L"FILTER: OFF", filtOn },
        { eqOn    ? L"EQ: ON"     : L"EQ: OFF",     eqOn },
        { compOn  ? L"COMP: ON"   : L"COMP: OFF",   compOn },
        { transOn ? L"PUNCH: ON"  : L"PUNCH: OFF",  transOn },
        { mb      ? L"MONO SUB: ON" : L"MONO SUB: OFF", mb },
        { lim     ? L"LIMITER: ON"  : L"LIMITER: OFF",  lim }
    };
    for (int m = 0; m < 6; ++m) {
        float bx = 36.0f + m * 128.0f;
        brush_->SetColor(toD2D(mods[m].on ? th.border : th.headerBg));
        rt_->FillRectangle(D2D1::RectF(bx, 548, bx + 120, 594), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(bx, 548, bx + 120, 594), brush_, 1.4f);
        drawText(mods[m].label, bx, 548, 120, 46, mods[m].on ? th.panelBg : th.textPrimary, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
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

    // 3-Octave Keyboard (Default C3 - B5) with Live Mouse + MIDI Press Animation
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
            if (sl.midiNote == note && sl.hasCustomColor && !sl.purged) {
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
    drawText(L"LIBRARY & .nm6 PRESET BROWSER   (CLICK ITEM TO LOAD)", 76, 120, 600, 28, th.textPrimary, 12.0f, true);

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

    // 1. Themes Grid (2 columns x 5 rows)
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

    // 2. UI Zoom (50%, 75%, 100%)
    drawText(L"2. WINDOW ZOOM SIZE", 160, 396, 520, 20, th.textMuted, 10.0f, true);
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

    // 3. Velocity Curve & Set Root Folder
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
    int py = static_cast<int>(std::round((196.0f + row * 90.0f + 16.0f) * uiScale_));
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
    if (!u8.empty() && renamingSlot_ >= 0) {
        controller_->uiRenameSlot(renamingSlot_, u8);
    }
    DestroyWindow(renameEdit_);
    renameEdit_ = nullptr;
    renamingSlot_ = -1;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WreckageWinView::cancelInlineRename() {
    if (renameEdit_) {
        DestroyWindow(renameEdit_);
        renameEdit_ = nullptr;
        renamingSlot_ = -1;
    }
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

void WreckageWinView::openLoadWavDialog() {
    ComApartmentGuard guard;
    IFileOpenDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg)))) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM);
        COMDLG_FILTERSPEC spec[] = { { L"Audio WAV Files", L"*.wav" } };
        dlg->SetFileTypes(1, spec);
        if (SUCCEEDED(dlg->Show(hwnd_))) {
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(dlg->GetResults(&items))) {
                DWORD count = 0;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* it = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &it))) {
                        PWSTR psz = nullptr;
                        if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &psz))) {
                            controller_->uiLoadWavFile(selectedSlot_, psz);
                            CoTaskMemFree(psz);
                        }
                        it->Release();
                    }
                }
                items->Release();
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
        COMDLG_FILTERSPEC spec[] = { { L"Noise Mu6ix Pack (.nm6)", L"*.nm6" } };
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
    if (activeTab_ == 0 && !browserOpen_ && !settingsOpen_) {
        if (ly >= 196 && ly <= 556 && lx >= 26 && lx <= 400) {
            int col = std::clamp(static_cast<int>((lx - 26.0f) / 126.0f), 0, 2);
            int row = std::clamp(static_cast<int>((ly - 196.0f) / 90.0f), 0, 3);
            selectedSlot_ = activeBank_ * 12 + row * 3 + col;
            startInlineRename();
        }
    }
}

void WreckageWinView::onMouseDown(float lx, float ly) {
    if (renameEdit_) commitInlineRename();

    // 1. Settings Modal Overlay
    if (settingsOpen_) {
        if (lx < 140 || lx > 700 || ly < 120 || ly > 610 || (lx > 635 && ly < 162)) {
            settingsOpen_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (ly >= 194 && ly <= 384 && lx >= 160 && lx <= 680) {
            int col = std::clamp(static_cast<int>((lx - 160.0f) / 264.0f), 0, 1);
            int row = std::clamp(static_cast<int>((ly - 194.0f) / 38.0f), 0, 4);
            int t = row * 2 + col;
            controller_->uiSetParam(kParamThemeIndex, t / 9.0);
        } else if (ly >= 422 && ly <= 456 && lx >= 160 && lx <= 680) {
            int z = std::clamp(static_cast<int>((lx - 160.0f) / 176.0f), 0, 2);
            applyZoomIndex(z);
        } else if (ly >= 500 && ly <= 534 && lx >= 160 && lx <= 680) {
            int v = std::clamp(static_cast<int>((lx - 160.0f) / 176.0f), 0, 2);
            controller_->uiSetParam(kParamVelCurve, v * 0.5);
        } else if (ly >= 550 && ly <= 590 && lx >= 160 && lx <= 680) {
            openRootFolderDialog();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // 2. Preset & Sample Browser Overlay
    if (browserOpen_) {
        if (lx < 60 || lx > 780 || ly < 110 || ly > 660 || (lx > 710 && ly < 155)) {
            browserOpen_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (lx >= 610 && lx <= 710 && ly >= 120 && ly <= 148) {
            openRootFolderDialog();
            return;
        }
        if (lx >= 76 && lx <= 360 && ly >= 162) {
            int idx = static_cast<int>((ly - 162.0f) / 32.0f);
            if (idx >= 0 && idx < static_cast<int>(browserCategories_.size())) {
                selectedCategory_ = idx;
                rescanRootDirectory();
            }
        } else if (lx >= 380 && lx <= 760 && ly >= 162) {
            int idx = static_cast<int>((ly - 162.0f) / 32.0f);
            if (idx >= 0 && idx < static_cast<int>(browserItems_.size()) && !controller_->rootLibraryDir().empty()) {
                std::filesystem::path base(controller_->rootLibraryDir());
                if (selectedCategory_ >= 0 && selectedCategory_ < static_cast<int>(browserCategories_.size())) {
                    base /= browserCategories_[selectedCategory_];
                }
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

    // 3. Header Bar Actions (Dialog buttons do NOT call SetCapture before opening modal dialogs!)
    if (ly < 68) {
        if (lx >= 238 && lx <= 520) {
            rescanRootDirectory();
            browserOpen_ = true;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (lx >= 526 && lx <= 594) {
            openExportNm6Dialog(false);
            return;
        }
        if (lx >= 750 && lx <= 784) {
            controller_->uiPanic();
            activePressedNote_ = -1;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (lx >= 790 && lx <= 826) {
            settingsOpen_ = !settingsOpen_;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
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
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (lx >= 138 && lx <= 488) {
            SetCapture(hwnd_);
            dragTarget_ = 2;
            controller_->uiSetParam(kParamPunishAmount, std::clamp((lx - 138.0f) / 350.0f, 0.0f, 1.0f));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (lx >= 506 && lx <= 830) {
            int m = std::clamp(static_cast<int>((lx - 506.0f) / 108.0f), 0, 2);
            controller_->uiSetParam(kParamPunishMode, m * 0.5);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }

    // 6. 3-Octave Keyboard (C3 - B5) + Follow MIDI
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
            int oct = wIdx / 7, k = wIdx % 7;
            note = kBaseMidiNote + (oct + keyboardOctaveOffset_) * 12 + kWhiteOffsets[k];
        }
        SetCapture(hwnd_);
        activePressedNote_ = note;
        if (followMidi_) {
            for (int s = 0; s < kTotalSlots; ++s) {
                if (controller_->slotMirror(s).midiNote == note) {
                    selectedSlot_ = s;
                    activeBank_ = s / 12;
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
        if (ly >= 156 && ly <= 184 && lx >= 26 && lx <= 400) {
            activeBank_ = std::clamp(static_cast<int>((lx - 26.0f) / 126.0f), 0, 2);
            selectedSlot_ = activeBank_ * 12 + (selectedSlot_ % 12);
        } else if (ly >= 196 && ly <= 556 && lx >= 26 && lx <= 400) {
            int col = std::clamp(static_cast<int>((lx - 26.0f) / 126.0f), 0, 2);
            int row = std::clamp(static_cast<int>((ly - 196.0f) / 90.0f), 0, 3);
            selectedSlot_ = activeBank_ * 12 + row * 3 + col;
            controller_->uiTriggerNote(controller_->slotMirror(selectedSlot_).midiNote, 0.88f, true);
        } else if (ly >= 568 && ly <= 608) {
            if (lx >= 26 && lx <= 152) { openLoadWavDialog(); return; }
            else if (lx >= 162 && lx <= 288) { openExportNm6Dialog(true); return; }
            else if (lx >= 298 && lx <= 402) {
                auto& sl = controller_->slotMirror(selectedSlot_);
                controller_->uiSetSlotProp(selectedSlot_, 8, sl.purged ? 0 : 1, 0.0);
            }
        } else if (ly >= 156 && ly <= 182 && lx >= 528 && lx <= 804) {
            int c = std::clamp(static_cast<int>((lx - 528.0f) / 23.0f), 0, kNumKeyColors - 1);
            controller_->uiSetKeyColor(selectedSlot_, kStudioKeyColors[c]);
        } else if (ly >= 194 && ly <= 226 && lx >= 440 && lx <= 812) {
            int p = std::clamp(static_cast<int>((lx - 440.0f) / 126.0f), 0, 2);
            controller_->uiCycleSmartMode(selectedSlot_, p);
        } else if (ly >= 240 && ly <= 270 && lx >= 440 && lx <= 812) {
            int d = std::clamp(static_cast<int>((lx - 440.0f) / 126.0f), 0, 2);
            auto& sl = controller_->slotMirror(selectedSlot_);
            if (d == 0) controller_->uiSetSlotProp(selectedSlot_, 5, sl.oneShot ? 0 : 1, 0.0);
            else if (d == 1) controller_->uiSetSlotProp(selectedSlot_, 6, (sl.chokeGroup + 1) % 5, 0.0);
            else controller_->uiSetSlotProp(selectedSlot_, 7, sl.reverse ? 0 : 1, 0.0);
        } else if (lx >= 440 && lx <= 808 && ly >= 296 && ly <= 575) {
            SetCapture(hwnd_);
            int fIdx = std::clamp(static_cast<int>((ly - 296.0f) / 58.0f), 0, 4);
            dragTarget_ = 20 + fIdx;
            controller_->uiSetSlotProp(selectedSlot_, fIdx, 0, std::clamp((lx - 440.0f) / 368.0f, 0.0f, 1.0f));
        }
    }
    // 8. Tab 1: STAGE & MIX (Damage 2 Perspective Stage + DRUM / MIX Lower Rack)
    else if (activeTab_ == 1) {
        if (ly >= 122 && ly <= 148) {
            if (lx >= 440 && lx <= 566) stageGridSnap_ = !stageGridSnap_;
            else if (lx >= 574 && lx <= 706) followMidi_ = !followMidi_;
            else if (lx >= 714 && lx <= 810) controller_->uiResetStage();
        } else if (lx >= 32 && lx <= 808 && ly >= 156 && ly <= 420) {
            auto screenToStage = [&](float px, float py, float& sx, float& sy) {
                float ny = std::clamp((406.0f - py) / 234.0f, 0.0f, 1.0f);
                float halfW = 360.0f - ny * 130.0f;
                float nx = std::clamp((px - 420.0f) / halfW, -1.0f, 1.0f);
                if (stageGridSnap_) {
                    nx = std::round(nx * 4.0f) / 4.0f;
                    ny = std::round(ny * 4.0f) / 4.0f;
                }
                sx = nx; sy = ny;
            };
            auto stageToScreen = [](float sx, float sy, float& outX, float& outY) {
                float halfW = 360.0f - sy * 130.0f;
                outX = 420.0f + sx * halfW;
                outY = 406.0f - sy * 234.0f;
            };

            int hitSlot = -1;
            float bestDist2 = 24.0f * 24.0f;
            for (int i = 0; i < 12; ++i) {
                int sIdx = activeBank_ * 12 + i;
                const auto& sl = controller_->slotMirror(sIdx);
                float px, py;
                stageToScreen(sl.stageX, sl.stageY, px, py);
                float d2 = (px - lx) * (px - lx) + (py - ly) * (py - ly);
                if (d2 <= bestDist2) { bestDist2 = d2; hitSlot = sIdx; }
            }
            if (hitSlot >= 0) {
                selectedSlot_ = hitSlot;
                controller_->uiTriggerNote(controller_->slotMirror(selectedSlot_).midiNote, 0.85f, true);
            }
            SetCapture(hwnd_);
            dragTarget_ = 1;
            float nx, ny;
            screenToStage(lx, ly, nx, ny);
            controller_->uiSetStageXY(selectedSlot_, nx, ny);
        } else if (ly >= 434 && ly <= 610) {
            if (lx >= 32 && lx <= 114) { stageSubMode_ = 0; }
            else if (lx >= 726 && lx <= 808) { stageSubMode_ = 1; }
            else if (stageSubMode_ == 0) {
                // Per-Drum 6 faders (2 cols x 3 rows)
                if (lx >= 132 && lx <= 408 && ly >= 440 && ly <= 596) {
                    SetCapture(hwnd_);
                    int row = std::clamp(static_cast<int>((ly - 440.0f) / 58.0f), 0, 2);
                    static const int props[3] = { 0, 1, 10 }; // Gain, Pitch, ToneLow
                    dragTarget_ = 40 + row;
                    controller_->uiSetSlotProp(selectedSlot_, props[row], 0, std::clamp((lx - 132.0f) / 276.0f, 0.0f, 1.0f));
                } else if (lx >= 432 && lx <= 708 && ly >= 440 && ly <= 596) {
                    SetCapture(hwnd_);
                    int row = std::clamp(static_cast<int>((ly - 440.0f) / 58.0f), 0, 2);
                    static const int props[3] = { 11, 2, 3 }; // ToneHigh, Attack, Release
                    dragTarget_ = 43 + row;
                    controller_->uiSetSlotProp(selectedSlot_, props[row], 0, std::clamp((lx - 432.0f) / 276.0f, 0.0f, 1.0f));
                }
            } else {
                // 4-Mic Mixer (CLOSE, ROOM, HALL, SUB)
                if (lx >= 134 && lx <= 648 && ly >= 438 && ly <= 606) {
                    SetCapture(hwnd_);
                    int b = std::clamp(static_cast<int>((ly - 438.0f) / 42.0f), 0, 3);
                    dragTarget_ = 10 + b;
                    controller_->uiSetParam(kParamBusCloseGain + b, std::clamp((lx - 134.0f) / 514.0f, 0.0f, 1.0f));
                } else if (lx >= 660 && lx <= 710 && ly >= 438 && ly <= 606) {
                    int b = std::clamp(static_cast<int>((ly - 438.0f) / 42.0f), 0, 3);
                    bool byp = controller_->getParamNormalized(kParamBusCloseBypass + b) >= 0.5;
                    controller_->uiSetParam(kParamBusCloseBypass + b, byp ? 0.0 : 1.0);
                }
            }
        }
    }
    // 9. Tab 2: MASTER FX
    else if (activeTab_ == 2) {
        if (ly >= 548 && ly <= 594 && lx >= 36 && lx <= 804) {
            int m = std::clamp(static_cast<int>((lx - 36.0f) / 128.0f), 0, 5);
            static const ParamID mIds[6] = {
                kParamFilterOn, kParamEqOn, kParamCompOn, kParamTransientOn, kParamMonoBass, kParamLimiterOn
            };
            bool cur = controller_->getParamNormalized(mIds[m]) >= 0.5;
            controller_->uiSetParam(mIds[m], cur ? 0.0 : 1.0);
        } else if (ly >= 175 && ly <= 515) {
            int row = std::clamp(static_cast<int>((ly - 175.0f) / 84.0f), 0, 3);
            if (lx >= 36 && lx <= 392) {
                SetCapture(hwnd_);
                dragTarget_ = 30 + row;
                static const ParamID lIds[4] = { kParamMasterHPF, kParamMasterLPF, kParamEqLow, kParamEqMid };
                controller_->uiSetParam(lIds[row], std::clamp((lx - 36.0f) / 356.0f, 0.0f, 1.0f));
            } else if (lx >= 442 && lx <= 798) {
                SetCapture(hwnd_);
                dragTarget_ = 34 + row;
                static const ParamID rIds[4] = { kParamEqHigh, kParamCompAmount, kParamTransientPunch, kParamStereoWidth };
                controller_->uiSetParam(rIds[row], std::clamp((lx - 442.0f) / 356.0f, 0.0f, 1.0f));
            }
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WreckageWinView::onMouseMove(float lx, float ly) {
    if (dragTarget_ == 0) return;
    if (dragTarget_ == 1) {
        float ny = std::clamp((406.0f - ly) / 234.0f, 0.0f, 1.0f);
        float halfW = 360.0f - ny * 130.0f;
        float nx = std::clamp((lx - 420.0f) / halfW, -1.0f, 1.0f);
        if (stageGridSnap_) {
            nx = std::round(nx * 4.0f) / 4.0f;
            ny = std::round(ny * 4.0f) / 4.0f;
        }
        controller_->uiSetStageXY(selectedSlot_, nx, ny);
    } else if (dragTarget_ == 2) {
        controller_->uiSetParam(kParamPunishAmount, std::clamp((lx - 138.0f) / 350.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 10 && dragTarget_ <= 13) {
        controller_->uiSetParam(kParamBusCloseGain + (dragTarget_ - 10), std::clamp((lx - 134.0f) / 514.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 20 && dragTarget_ <= 24) {
        controller_->uiSetSlotProp(selectedSlot_, dragTarget_ - 20, 0, std::clamp((lx - 440.0f) / 368.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 30 && dragTarget_ <= 33) {
        static const ParamID lIds[4] = { kParamMasterHPF, kParamMasterLPF, kParamEqLow, kParamEqMid };
        controller_->uiSetParam(lIds[dragTarget_ - 30], std::clamp((lx - 36.0f) / 356.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 34 && dragTarget_ <= 37) {
        static const ParamID rIds[4] = { kParamEqHigh, kParamCompAmount, kParamTransientPunch, kParamStereoWidth };
        controller_->uiSetParam(rIds[dragTarget_ - 34], std::clamp((lx - 442.0f) / 356.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 40 && dragTarget_ <= 42) {
        static const int props[3] = { 0, 1, 10 };
        controller_->uiSetSlotProp(selectedSlot_, props[dragTarget_ - 40], 0, std::clamp((lx - 132.0f) / 276.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 43 && dragTarget_ <= 45) {
        static const int props[3] = { 11, 2, 3 };
        controller_->uiSetSlotProp(selectedSlot_, props[dragTarget_ - 43], 0, std::clamp((lx - 432.0f) / 276.0f, 0.0f, 1.0f));
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
        if (self) {
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    }
    return self ? self->handleMessage(hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT WreckageWinView::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
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
            if (LOWORD(wParam) == 9001 && HIWORD(wParam) == EN_KILLFOCUS) {
                commitInlineRename();
            }
            return 0;
        case WM_KEYDOWN:
            if (renameEdit_ && wParam == VK_RETURN) { commitInlineRename(); return 0; }
            if (renameEdit_ && wParam == VK_ESCAPE) { cancelInlineRename(); return 0; }
            if (HWND p = GetParent(hwnd)) PostMessageW(p, msg, wParam, lParam);
            return 0;
        case WM_DROPFILES: {
            HDROP hDrop = reinterpret_cast<HDROP>(wParam);
            UINT n = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; ++i) {
                wchar_t buf[MAX_PATH] = {0};
                if (DragQueryFileW(hDrop, i, buf, MAX_PATH) > 0) {
                    controller_->uiLoadWavFile(selectedSlot_, buf);
                }
            }
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
