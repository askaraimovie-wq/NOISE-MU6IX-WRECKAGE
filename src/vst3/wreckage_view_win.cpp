#ifdef _WIN32
#include "wreckage_view_win.h"
#include "wreckage_uids.h"
#include <windowsx.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

#include <cstring>
using namespace Steinberg;
using Steinberg::Vst::ParamID;

namespace NoiseMu6ix {

static const wchar_t* kWndClassName = L"NoiseMu6ixWreckageD2DViewClass";

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(std::max(0, n - 1)), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], n);
    return out;
}

WreckageWinView::WreckageWinView(WreckageController* controller)
    : CPluginView(nullptr), controller_(controller) {
    ViewRect r(0, 0, kBaseUiSize, kBaseUiSize);
    setRect(r);
#if !WRECKAGE_IS_PLAYER
    activeTab_ = 0; // Full WRECKAGE opens on SOURCE
#else
    activeTab_ = 1; // WRECKAGE Player opens on STAGE & MIXER (tab 0 is Read-Only KIT)
#endif
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

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &WreckageWinView::wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWndClassName;
    RegisterClassExW(&wc);

    int zIdx = static_cast<int>(std::round(controller_->getParamNormalized(kParamUiZoom) * 2.0));
    uiScale_ = (zIdx == 0) ? 0.50f : ((zIdx == 1) ? 0.75f : 1.00f);
    int px = static_cast<int>(std::round(kBaseUiSize * uiScale_));
    ViewRect r(0, 0, px, px);
    setRect(r);

    hwnd_ = CreateWindowExW(
        0, kWndClassName, L"WRECKAGE",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, px, px,
        static_cast<HWND>(parent), nullptr, hInst, this);

    if (!hwnd_) return kResultFalse;
#if !WRECKAGE_IS_PLAYER
    DragAcceptFiles(hwnd_, TRUE);
#endif
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dwFactory_));
    createDeviceResources();
    SetTimer(hwnd_, 1, 33, nullptr);
    return CPluginView::attached(parent, type);
}

tresult PLUGIN_API WreckageWinView::removed() {
    if (hwnd_) {
        KillTimer(hwnd_, 1);
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
        D2D1_SIZE_U sz = D2D1::SizeU(static_cast<UINT32>(std::max<LONG>(1, rc.right - rc.left)),
                                     static_cast<UINT32>(std::max<LONG>(1, rc.bottom - rc.top)));
        if (SUCCEEDED(d2dFactory_->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(hwnd_, sz), &rt_))) {
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
                                const ThemePalette& th, bool accentFill) {
    val01 = std::clamp(val01, 0.0f, 1.0f);
    drawText(label, x, y - 16.0f, w, 15.0f, th.textPrimary, 11.0f, true);
    brush_->SetColor(toD2D(th.faderTrack));
    rt_->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush_);
    brush_->SetColor(toD2D(accentFill ? th.accent : th.faderFill));
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
    if (!rt_ || !brush_) return;

    int thIdx = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamThemeIndex) * 9.0)), 0, kNumThemes - 1);
    const ThemePalette& th = kThemes[thIdx];

    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Scale(uiScale_, uiScale_));
    rt_->Clear(toD2D(th.bg));

    drawHeader(th);
    drawTabs(th);

    if (activeTab_ == 0) drawSourceOrKitTab(th);
    else if (activeTab_ == 1) drawStageAndMixerTab(th);
    else drawMasterFxTab(th);

    drawPunishAndKeyboard(th);
    if (browserOpen_) drawLibraryBrowserOverlay(th);

    // Outer frame border
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

    // Small Brand on Top + BIG Plugin Title Below
    drawText(L"NOISE MU6IX", 16, 6, 200, 16, th.textMuted, 10.5f, true);
    drawText(L"WRECKAGE", 16, 20, 220, 42, th.textPrimary, 29.0f, true);

    // Preset + SET ROOT Box
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(230, 10, 560, 58), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(230, 10, 560, 58), brush_, 1.5f);
    std::wstring rootLabel = controller_->rootLibraryDir().empty()
        ? L"ROOT: [CLICK SET ROOT]" : (L"ROOT: " + std::filesystem::path(controller_->rootLibraryDir()).filename().wstring());
    drawText(rootLabel.c_str(), 238, 12, 230, 18, th.textMuted, 10.0f, true);
    drawText(L"[SET ROOT]", 478, 12, 76, 18, th.accent, 10.0f, true, DWRITE_TEXT_ALIGNMENT_TRAILING);
    std::wstring pName = toWide(controller_->activePresetName());
    drawText(pName.c_str(), 238, 30, 314, 24, th.textPrimary, 13.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    // Theme 1/10 Switcher
    int thIdx = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamThemeIndex) * 9.0)), 0, 9);
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(572, 10, 704, 58), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(572, 10, 704, 58), brush_, 1.5f);
    std::wstring thNum = L"THEME " + std::to_wstring(thIdx + 1) + L"/10";
    drawText(thNum.c_str(), 572, 13, 132, 16, th.textMuted, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    drawText(toWide(th.name).c_str(), 572, 30, 132, 24, th.textPrimary, 10.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    // Zoom Buttons: 50% / 75% / 100%
    int zIdx = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamUiZoom) * 2.0)), 0, 2);
    const wchar_t* zLabels[3] = { L"50%", L"75%", L"100%" };
    for (int i = 0; i < 3; ++i) {
        float bx = 714.0f + i * 38.0f;
        brush_->SetColor(toD2D(i == zIdx ? th.border : th.panelBg));
        rt_->FillRectangle(D2D1::RectF(bx, 18, bx + 34, 50), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(bx, 18, bx + 34, 50), brush_, 1.3f);
        drawText(zLabels[i], bx, 18, 34, 32, i == zIdx ? th.panelBg : th.textPrimary, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void WreckageWinView::drawTabs(const ThemePalette& th) {
#if WRECKAGE_IS_PLAYER
    const wchar_t* tabNames[3] = { L"KIT (MAP)", L"STAGE & MIXER", L"MASTER FX" };
#else
    const wchar_t* tabNames[3] = { L"SOURCE", L"STAGE & MIXER", L"MASTER FX" };
#endif
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

void WreckageWinView::drawSourceOrKitTab(const ThemePalette& th) {
    // Left Panel: 3-Octave 3x4 Pad Bank
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(16, 120, 412, 624), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(16, 120, 412, 624), brush_, 1.6f);

    drawText(L"3-OCTAVE SOURCE BANK (36 SLOTS)", 26, 128, 376, 22, th.textPrimary, 12.0f, true);
    const wchar_t* banks[3] = { L"BANK 1 (C1-B1)", L"BANK 2 (C2-B2)", L"BANK 3 (C3-B3)" };
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
        drawText(toWide(sl.name).c_str(), px + 4, py + 16, 112, 32, sel ? th.panelBg : th.textPrimary, 11.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        std::wstring info = sl.purged ? L"PURGED" : (L"V:" + std::to_wstring(sl.velCount) + L" RR:" + std::to_wstring(sl.rrCount));
        drawText(info.c_str(), px + 4, py + 50, 112, 22, sel ? th.headerBg : th.textMuted, 9.5f, false, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

#if !WRECKAGE_IS_PLAYER
    // Editor-only Load WAV + Export .nm6 buttons
    brush_->SetColor(toD2D(th.border));
    rt_->FillRectangle(D2D1::RectF(26, 568, 152, 608), brush_);
    drawText(L"+ LOAD WAV", 26, 568, 126, 40, th.panelBg, 11.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    brush_->SetColor(toD2D(th.accent));
    rt_->FillRectangle(D2D1::RectF(162, 568, 288, 608), brush_);
    drawText(L"EXPORT .nm6", 162, 568, 126, 40, 0xFFFFFF, 11.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
#endif
    brush_->SetColor(toD2D(th.headerBg));
    rt_->FillRectangle(D2D1::RectF(298, 568, 402, 608), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(298, 568, 402, 608), brush_, 1.2f);
    drawText(L"PURGE", 298, 568, 104, 40, th.textPrimary, 11.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    // Right Panel: Sample Inspector (Read-only summary in Player, Full Edit in WRECKAGE)
    const auto& sl = controller_->slotMirror(selectedSlot_);
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(428, 120, 824, 624), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(428, 120, 824, 624), brush_, 1.6f);

    std::wstring hdr = L"INSPECTOR: " + toWide(sl.name);
    drawText(hdr.c_str(), 440, 128, 370, 24, th.textPrimary, 12.5f, true);

    // Custom Key Color Swatches (12 studio colors)
    drawText(L"KEY COLOR:", 440, 158, 86, 20, th.textMuted, 10.0f, true);
    for (int c = 0; c < kNumKeyColors; ++c) {
        float sx = 528.0f + c * 23.0f;
        brush_->SetColor(toD2D(kStudioKeyColors[c]));
        rt_->FillRectangle(D2D1::RectF(sx, 158, sx + 19, 178), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(sx, 158, sx + 19, 178), brush_, (sl.hasCustomColor && sl.keyColor == kStudioKeyColors[c]) ? 2.4f : 1.0f);
    }

    // 3-State Smart Hybrid Pills (AUTO / ON / OFF)
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

    // Drum Modes: ONE-SHOT/GATE, CHOKE, REV
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

    // 5 Horizontal Slot Faders
    drawFader(440, 310, 368, 16, sl.gain,    L"GAIN",    th);
    drawFader(440, 368, 368, 16, sl.pitch,   L"PITCH (-24 / +24 ST)", th);
    drawFader(440, 426, 368, 16, sl.attack,  L"ATTACK",  th);
    drawFader(440, 484, 368, 16, sl.release, L"RELEASE", th);
    drawFader(440, 542, 368, 16, sl.xfade,   L"XFADE",   th);
}

void WreckageWinView::drawStageAndMixerTab(const ThemePalette& th) {
    // Left: THE STAGE
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(16, 120, 412, 624), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(16, 120, 412, 624), brush_, 1.6f);
    drawText(L"THE STAGE  (X: STEREO PAN • Y: DEPTH)", 26, 128, 376, 24, th.textPrimary, 12.0f, true);

    D2D1_RECT_F stageBox = D2D1::RectF(32, 162, 396, 598);
    brush_->SetColor(toD2D(th.bg));
    rt_->FillRectangle(stageBox, brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(stageBox, brush_, 1.5f);

    // Stage Grid lines
    brush_->SetColor(toD2D(th.textMuted, 0.35f));
    for (int g = 1; g < 4; ++g) {
        float gx = 32.0f + g * (364.0f / 4.0f);
        float gy = 162.0f + g * (436.0f / 4.0f);
        rt_->DrawLine(D2D1::Point2F(gx, 162), D2D1::Point2F(gx, 598), brush_, 1.0f);
        rt_->DrawLine(D2D1::Point2F(32, gy), D2D1::Point2F(396, gy), brush_, 1.0f);
    }
    drawText(L"BACK (HALL / FAR)", 32, 166, 364, 18, th.textMuted, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    drawText(L"FRONT (CLOSE / DRY)", 32, 576, 364, 18, th.textMuted, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    // Draw all loaded/active stage nodes
    for (int i = 0; i < 12; ++i) {
        int sIdx = activeBank_ * 12 + i;
        const auto& sl = controller_->slotMirror(sIdx);
        float nx = 32.0f + ((sl.stageX + 1.0f) * 0.5f) * 364.0f;
        float ny = 598.0f - sl.stageY * 436.0f;
        float rad = (sIdx == selectedSlot_) ? 16.0f : 11.0f;
        brush_->SetColor(toD2D(sl.hasCustomColor ? sl.keyColor : th.accent));
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(nx, ny), rad, rad), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(nx, ny), rad, rad), brush_, (sIdx == selectedSlot_) ? 2.4f : 1.2f);
    }

    // Right: 5-BUS MIXER
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(428, 120, 824, 624), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(428, 120, 824, 624), brush_, 1.6f);
    drawText(L"5-BUS MIXER  (HORIZONTAL FADERS + BYPASS)", 440, 128, 376, 24, th.textPrimary, 12.0f, true);

    const wchar_t* busNames[5] = { L"CLOSE (DIRECT)", L"ROOM (EARLY REFLECTIONS)", L"HALL (SCORING SPACE)", L"LFE (MONO SUB IMPACT)", L"CRUSH (PARALLEL SMASH)" };
    for (int b = 0; b < 5; ++b) {
        float y = 190.0f + b * 84.0f;
        float val = static_cast<float>(controller_->getParamNormalized(kParamBusCloseGain + b));
        bool byp = controller_->getParamNormalized(kParamBusCloseBypass + b) >= 0.5;
        drawFader(442, y, 310, 20, val, busNames[b], th, !byp && b == 4);

        // Bypass Circle LED
        brush_->SetColor(toD2D(byp ? th.headerBg : th.accent));
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(786, y + 10), 12, 12), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(786, y + 10), 12, 12), brush_, 1.5f);
        drawText(byp ? L"OFF" : L"ON", 768, y + 24, 36, 14, th.textMuted, 9.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void WreckageWinView::drawMasterFxTab(const ThemePalette& th) {
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(16, 120, 824, 624), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(16, 120, 824, 624), brush_, 1.6f);
    drawText(L"MASTER FX RACK  •  TRANSIENT PUNCH  •  STEREO WIDTH  •  LIMITER", 28, 128, 780, 24, th.textPrimary, 12.5f, true);

    struct FxDef { ParamID id; const wchar_t* name; bool accent; };
    const FxDef leftFx[4] = {
        { kParamMasterHPF, L"HIGH-PASS FILTER (20 - 400 Hz)", false },
        { kParamMasterLPF, L"LOW-PASS FILTER (2 - 20 kHz)", false },
        { kParamEqLow,     L"3-BAND EQ: LOW (90 Hz)", false },
        { kParamEqMid,     L"3-BAND EQ: MID (1.1 kHz)", false }
    };
    const FxDef rightFx[4] = {
        { kParamEqHigh,         L"3-BAND EQ: HIGH (7.5 kHz)", false },
        { kParamCompAmount,     L"BUS COMPRESSOR", false },
        { kParamTransientPunch, L"TRANSIENT PUNCH SHAPER", true },
        { kParamStereoWidth,    L"STEREO WIDTH", false }
    };

    for (int i = 0; i < 4; ++i) {
        float y = 195.0f + i * 88.0f;
        drawFader(36,  y, 356, 20, static_cast<float>(controller_->getParamNormalized(leftFx[i].id)),  leftFx[i].name,  th, leftFx[i].accent);
        drawFader(442, y, 356, 20, static_cast<float>(controller_->getParamNormalized(rightFx[i].id)), rightFx[i].name, th, rightFx[i].accent);
    }

    // Bottom Row Toggles: Velocity Curve, Mono Bass, Output Limiter
    int vc = static_cast<int>(std::round(controller_->getParamNormalized(kParamVelCurve) * 2.0));
    const wchar_t* vcLabel = (vc == 0) ? L"VEL CURVE: SOFT" : ((vc == 1) ? L"VEL CURVE: LINEAR" : L"VEL CURVE: HARD");
    bool mb  = controller_->getParamNormalized(kParamMonoBass) >= 0.5;
    bool lim = controller_->getParamNormalized(kParamLimiterOn) >= 0.5;

    auto drawToggle = [&](float x, const wchar_t* txt, bool on) {
        brush_->SetColor(toD2D(on ? th.border : th.headerBg));
        rt_->FillRectangle(D2D1::RectF(x, 548, x + 236, 594), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(x, 548, x + 236, 594), brush_, 1.4f);
        drawText(txt, x, 548, 236, 46, on ? th.panelBg : th.textPrimary, 11.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    drawToggle(36,  vcLabel, true);
    drawToggle(302, mb  ? L"MONO BASS (<120Hz): ON" : L"MONO BASS: OFF", mb);
    drawToggle(568, lim ? L"OUTPUT LIMITER (-0.3dB): ON" : L"OUTPUT LIMITER: OFF", lim);
}

void WreckageWinView::drawPunishAndKeyboard(const ThemePalette& th) {
    // Signature PUNISH Bar (y: 636..724)
    brush_->SetColor(toD2D(th.punishBg));
    rt_->FillRectangle(D2D1::RectF(0, 636, 840, 724), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawLine(D2D1::Point2F(0, 636), D2D1::Point2F(840, 636), brush_, 1.8f);

    drawText(L"PUNISH", 20, 656, 110, 48, 0xFFFFFF, 21.0f, true);
    float punVal = static_cast<float>(controller_->getParamNormalized(kParamPunishAmount));
    brush_->SetColor(toD2D(0x363D47));
    rt_->FillRectangle(D2D1::RectF(138, 668, 488, 692), brush_);
    brush_->SetColor(toD2D(th.accent));
    rt_->FillRectangle(D2D1::RectF(138, 668, 138 + 350 * punVal, 692), brush_);
    brush_->SetColor(toD2D(0xFFFFFF));
    rt_->DrawRectangle(D2D1::RectF(138, 668, 488, 692), brush_, 1.2f);

    int pm = static_cast<int>(std::round(controller_->getParamNormalized(kParamPunishMode) * 2.0));
    const wchar_t* pModes[3] = { L"GENTLY NOW", L"HURT ME PLENTY", L"NIGHTMARE" };
    for (int i = 0; i < 3; ++i) {
        float bx = 506.0f + i * 108.0f;
        brush_->SetColor(toD2D(i == pm ? th.accent : 0x2E353F));
        rt_->FillRectangle(D2D1::RectF(bx, 654, bx + 102, 706), brush_);
        brush_->SetColor(toD2D(i == pm ? 0xFFFFFF : 0x7A818A));
        rt_->DrawRectangle(D2D1::RectF(bx, 654, bx + 102, 706), brush_, 1.3f);
        drawText(pModes[i], bx, 654, 102, 52, 0xFFFFFF, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // 3-Octave Custom Color-Coded MIDI Keyboard (y: 724..840)
    brush_->SetColor(toD2D(th.headerBg));
    rt_->FillRectangle(D2D1::RectF(0, 724, 840, 840), brush_);

    static const int kWhiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const bool kHasBlackAfter[7] = { true, true, false, true, true, true, false };
    float kbX = 42.0f, whiteW = 36.0f, kbY = 732.0f, kbH = 98.0f;

    // Octave Shift buttons
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

    // White keys
    for (int oct = 0; oct < 3; ++oct) {
        for (int k = 0; k < 7; ++k) {
            int note = kBaseMidiNote + (oct + keyboardOctaveOffset_) * 12 + kWhiteOffsets[k];
            float kx = kbX + (oct * 7 + k) * whiteW;
            brush_->SetColor(toD2D(note == activePressedNote_ ? 0xFFE6D5 : 0xFFFFFF));
            rt_->FillRectangle(D2D1::RectF(kx, kbY, kx + whiteW, kbY + kbH), brush_);
            uint32_t c1 = 0, c2 = 0;
            int cnt = collectNoteColors(note, c1, c2);
            if (cnt >= 2 && c2 != 0) {
                // Dual-Color Split for Layered Pads
                brush_->SetColor(toD2D(c1));
                rt_->FillRectangle(D2D1::RectF(kx, kbY + kbH - 14, kx + whiteW * 0.5f, kbY + kbH), brush_);
                brush_->SetColor(toD2D(c2));
                rt_->FillRectangle(D2D1::RectF(kx + whiteW * 0.5f, kbY + kbH - 14, kx + whiteW, kbY + kbH), brush_);
            } else if (cnt >= 1) {
                brush_->SetColor(toD2D(c1));
                rt_->FillRectangle(D2D1::RectF(kx, kbY + kbH - 14, kx + whiteW, kbY + kbH), brush_);
            }
            brush_->SetColor(toD2D(0x181C22));
            rt_->DrawRectangle(D2D1::RectF(kx, kbY, kx + whiteW, kbY + kbH), brush_, 1.1f);
        }
    }

    // Black keys
    for (int oct = 0; oct < 3; ++oct) {
        for (int k = 0; k < 7; ++k) {
            if (!kHasBlackAfter[k]) continue;
            int note = kBaseMidiNote + (oct + keyboardOctaveOffset_) * 12 + kWhiteOffsets[k] + 1;
            float kx = kbX + (oct * 7 + k + 1) * whiteW - 11.0f;
            brush_->SetColor(toD2D(note == activePressedNote_ ? th.accent : 0x181C22));
            rt_->FillRectangle(D2D1::RectF(kx, kbY, kx + 22.0f, kbY + 58.0f), brush_);
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
    drawText(L"LIBRARY & .nm6 PRESET BROWSER   [CLICK OUTSIDE OR X TO CLOSE]", 76, 120, 640, 28, th.textPrimary, 12.5f, true);
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
        drawText(browserItems_[i].c_str(), 388, y, 364, 28, th.textPrimary, 11.0f, false);
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
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
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
}

void WreckageWinView::openLoadWavDialog() {
#if !WRECKAGE_IS_PLAYER
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
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
#endif
}

void WreckageWinView::openExportNm6Dialog() {
#if !WRECKAGE_IS_PLAYER
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
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
                    CoTaskMemFree(psz);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
#endif
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
}

void WreckageWinView::onMouseDown(float lx, float ly) {
    SetCapture(hwnd_);
    if (browserOpen_) {
        if (lx < 60 || lx > 780 || ly < 110 || ly > 660 || (lx > 710 && ly < 155)) {
            browserOpen_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    // Header clicks
    if (ly < 68) {
        if (lx >= 475 && lx <= 558 && ly <= 34) { openRootFolderDialog(); return; }
        if (lx >= 230 && lx <= 560) { rescanRootDirectory(); browserOpen_ = true; InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (lx >= 572 && lx <= 704) {
            int cur = static_cast<int>(std::round(controller_->getParamNormalized(kParamThemeIndex) * 9.0));
            controller_->uiSetParam(kParamThemeIndex, ((cur + 1) % kNumThemes) / 9.0);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (lx >= 714 && lx <= 828) {
            int z = std::clamp(static_cast<int>((lx - 714.0f) / 38.0f), 0, 2);
            applyZoomIndex(z);
            return;
        }
    }

    // Tabs
    if (ly >= 68 && ly < 106) {
        activeTab_ = std::clamp(static_cast<int>(lx / 280.0f), 0, 2);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // PUNISH bar
    if (ly >= 636 && ly < 724) {
        if (lx >= 138 && lx <= 488) {
            dragTarget_ = 2;
            controller_->uiSetParam(kParamPunishAmount, (lx - 138.0f) / 350.0f);
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

    // Virtual 3-Octave Color Keyboard
    if (ly >= 724) {
        if (lx < 38) { keyboardOctaveOffset_ = std::max(-2, keyboardOctaveOffset_ - 1); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (lx > 802) { keyboardOctaveOffset_ = std::min(3, keyboardOctaveOffset_ + 1); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        static const int kWhiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
        int wIdx = std::clamp(static_cast<int>((lx - 42.0f) / 36.0f), 0, 20);
        int oct = wIdx / 7, k = wIdx % 7;
        int note = kBaseMidiNote + (oct + keyboardOctaveOffset_) * 12 + kWhiteOffsets[k];
        activePressedNote_ = note;
        controller_->uiTriggerNote(note, 0.90f, true);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // Tab-specific interactions
    if (activeTab_ == 0) {
        if (ly >= 156 && ly <= 184 && lx >= 26 && lx <= 400) {
            activeBank_ = std::clamp(static_cast<int>((lx - 26.0f) / 126.0f), 0, 2);
        } else if (ly >= 196 && ly <= 556 && lx >= 26 && lx <= 400) {
            int col = std::clamp(static_cast<int>((lx - 26.0f) / 126.0f), 0, 2);
            int row = std::clamp(static_cast<int>((ly - 196.0f) / 90.0f), 0, 3);
            selectedSlot_ = activeBank_ * 12 + row * 3 + col;
            controller_->uiTriggerNote(controller_->slotMirror(selectedSlot_).midiNote, 0.88f, true);
        } else if (ly >= 568 && ly <= 608) {
            if (lx >= 26 && lx <= 152) openLoadWavDialog();
            else if (lx >= 162 && lx <= 288) openExportNm6Dialog();
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
            int fIdx = std::clamp(static_cast<int>((ly - 296.0f) / 58.0f), 0, 4);
            dragTarget_ = 20 + fIdx;
            controller_->uiSetSlotProp(selectedSlot_, fIdx, 0, std::clamp((lx - 440.0f) / 368.0f, 0.0f, 1.0f));
        }
    } else if (activeTab_ == 1) {
        if (lx >= 32 && lx <= 396 && ly >= 162 && ly <= 598) {
            dragTarget_ = 1;
            float nx = ((lx - 32.0f) / 364.0f) * 2.0f - 1.0f;
            float ny = (598.0f - ly) / 436.0f;
            controller_->uiSetStageXY(selectedSlot_, nx, ny);
        } else if (lx >= 442 && lx <= 752 && ly >= 175 && ly <= 590) {
            int b = std::clamp(static_cast<int>((ly - 175.0f) / 84.0f), 0, 4);
            dragTarget_ = 10 + b;
            controller_->uiSetParam(kParamBusCloseGain + b, (lx - 442.0f) / 310.0f);
        } else if (lx >= 766 && lx <= 808 && ly >= 175 && ly <= 590) {
            int b = std::clamp(static_cast<int>((ly - 175.0f) / 84.0f), 0, 4);
            bool byp = controller_->getParamNormalized(kParamBusCloseBypass + b) >= 0.5;
            controller_->uiSetParam(kParamBusCloseBypass + b, byp ? 0.0 : 1.0);
        }
    } else if (activeTab_ == 2) {
        if (ly >= 548 && ly <= 594) {
            if (lx >= 36 && lx <= 272) {
                int vc = static_cast<int>(std::round(controller_->getParamNormalized(kParamVelCurve) * 2.0));
                controller_->uiSetParam(kParamVelCurve, ((vc + 1) % 3) * 0.5);
            } else if (lx >= 302 && lx <= 538) {
                bool mb = controller_->getParamNormalized(kParamMonoBass) >= 0.5;
                controller_->uiSetParam(kParamMonoBass, mb ? 0.0 : 1.0);
            } else if (lx >= 568 && lx <= 804) {
                bool lim = controller_->getParamNormalized(kParamLimiterOn) >= 0.5;
                controller_->uiSetParam(kParamLimiterOn, lim ? 0.0 : 1.0);
            }
        } else if (ly >= 180 && ly <= 520) {
            int row = std::clamp(static_cast<int>((ly - 180.0f) / 88.0f), 0, 3);
            if (lx >= 36 && lx <= 392) {
                dragTarget_ = 30 + row;
                static const ParamID lIds[4] = { kParamMasterHPF, kParamMasterLPF, kParamEqLow, kParamEqMid };
                controller_->uiSetParam(lIds[row], (lx - 36.0f) / 356.0f);
            } else if (lx >= 442 && lx <= 798) {
                dragTarget_ = 34 + row;
                static const ParamID rIds[4] = { kParamEqHigh, kParamCompAmount, kParamTransientPunch, kParamStereoWidth };
                controller_->uiSetParam(rIds[row], (lx - 442.0f) / 356.0f);
            }
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WreckageWinView::onMouseMove(float lx, float ly) {
    if (dragTarget_ == 0) return;
    if (dragTarget_ == 1) {
        float nx = ((lx - 32.0f) / 364.0f) * 2.0f - 1.0f;
        float ny = (598.0f - ly) / 436.0f;
        controller_->uiSetStageXY(selectedSlot_, nx, ny);
    } else if (dragTarget_ == 2) {
        controller_->uiSetParam(kParamPunishAmount, (lx - 138.0f) / 350.0f);
    } else if (dragTarget_ >= 10 && dragTarget_ <= 14) {
        controller_->uiSetParam(kParamBusCloseGain + (dragTarget_ - 10), (lx - 442.0f) / 310.0f);
    } else if (dragTarget_ >= 20 && dragTarget_ <= 24) {
        controller_->uiSetSlotProp(selectedSlot_, dragTarget_ - 20, 0, std::clamp((lx - 440.0f) / 368.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 30 && dragTarget_ <= 33) {
        static const ParamID lIds[4] = { kParamMasterHPF, kParamMasterLPF, kParamEqLow, kParamEqMid };
        controller_->uiSetParam(lIds[dragTarget_ - 30], (lx - 36.0f) / 356.0f);
    } else if (dragTarget_ >= 34 && dragTarget_ <= 37) {
        static const ParamID rIds[4] = { kParamEqHigh, kParamCompAmount, kParamTransientPunch, kParamStereoWidth };
        controller_->uiSetParam(rIds[dragTarget_ - 34], (lx - 442.0f) / 356.0f);
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
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handleMessage(msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT WreckageWinView::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd_, &ps);
            paint();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_TIMER:
            InvalidateRect(hwnd_, nullptr, FALSE);
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
        case WM_KEYDOWN:
            // Pass-Through Focus so FL Studio Spacebar / Ctrl+S always work!
            if (HWND p = GetParent(hwnd_)) PostMessageW(p, msg, wParam, lParam);
            return 0;
#if !WRECKAGE_IS_PLAYER
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
#endif
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

} // namespace NoiseMu6ix
#endif
