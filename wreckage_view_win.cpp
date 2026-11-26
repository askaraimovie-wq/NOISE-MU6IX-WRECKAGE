#ifdef _WIN32
#include "wreckage_view_win.h"
#include "wreckage_uids.h"
#include "version.h"
#include "wreckage_dsp.h"
#include <windowsx.h>
#include <shobjidl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace NoiseMu6ix {

static const wchar_t* kWndClassName = L"NoiseMu6ixWreckageD2DViewClass_v108";

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

static void formatDbLabel(wchar_t* dst, size_t cap, float db) {
    if (db <= -59.0f) swprintf(dst, cap, L"-inf dB");
    else if (std::fabs(db) < 0.05f) swprintf(dst, cap, L"0.0 dB");
    else swprintf(dst, cap, L"%+.1f dB", db);
}

WreckageWinView::WreckageWinView(WreckageController* controller)
    : CPluginView(nullptr), controller_(controller) {
    int zIdx = 2;
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

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dwFactory_));
    createDeviceResources();

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    SetTimer(hwnd_, 1, 25, nullptr);
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

bool WreckageWinView::isNotePhysicallyDown(int midiNote) const {
    if (midiNote < 0 || midiNote >= 128) return false;
    if (activePressedNote_ >= 0 && midiNote == activePressedNote_) return true;
    return controller_ && (controller_->isMidiNoteLive(midiNote) || controller_->isNotePulsing(midiNote));
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
    drawText(label, x, y - 16.0f, w * 0.70f, 15.0f, enabled ? th.textPrimary : th.textMuted, 9.6f, true);
    if (valStr && valStr[0] != 0) {
        drawText(valStr, x + w * 0.62f, y - 16.0f, w * 0.38f, 15.0f, enabled ? th.accent : th.textMuted, 9.8f, true, DWRITE_TEXT_ALIGNMENT_TRAILING);
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

    // N6 Logo Box + Large WRECKAGE + Black Version right next to 'E'
    brush_->SetColor(toD2D(th.border));
    rt_->FillRectangle(D2D1::RectF(14, 14, 52, 54), brush_);
    brush_->SetColor(toD2D(th.accent));
    rt_->DrawRectangle(D2D1::RectF(14, 14, 52, 54), brush_, 1.5f);
    drawText(L"N6", 14, 14, 38, 40, th.panelBg, 16.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    drawText(L"WRECKAGE", 58, 12, 174, 44, th.textPrimary, 30.0f, true);
    drawText(L"v" WRECKAGE_VERSION_STR, 236, 34, 42, 18, th.textPrimary, 9.5f, true);

    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(282, 12, 530, 56), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(282, 12, 530, 56), brush_, 1.5f);
    std::wstring rootLabel = controller_->rootLibraryDir().empty()
        ? L"PRESET / KIT  [CLICK TO BROWSE]"
        : (L"LIB: " + std::filesystem::path(controller_->rootLibraryDir()).filename().wstring());
    drawText(rootLabel.c_str(), 288, 14, 236, 16, th.textMuted, 8.8f, true);
    drawText(toWide(controller_->activePresetName()).c_str(), 288, 30, 236, 22, th.textPrimary, 12.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    // Clean Save Icon Button (flashes green when saved)
    bool savedFlash = controller_->saveFlashTicks() > 0;
    brush_->SetColor(toD2D(savedFlash ? 0x28CD41 : th.panelBg));
    rt_->FillRectangle(D2D1::RectF(536, 12, 582, 56), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(536, 12, 582, 56), brush_, 1.5f);
    uint32_t icoCol = savedFlash ? 0xFFFFFF : th.textPrimary;
    brush_->SetColor(toD2D(icoCol));
    // Floppy / Save Icon geometry centered at (559, 34)
    rt_->DrawRectangle(D2D1::RectF(548, 22, 570, 46), brush_, 1.8f);
    rt_->FillRectangle(D2D1::RectF(552, 22, 564, 30), brush_);
    rt_->DrawRectangle(D2D1::RectF(552, 35, 566, 46), brush_, 1.4f);

    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(588, 12, 746, 56), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(588, 12, 746, 56), brush_, 1.4f);

    int vox = controller_->liveVoices();
    double ramMb = static_cast<double>(controller_->liveRamBytes()) / (1024.0 * 1024.0);
    float cpu = controller_->liveCpuPct();
    wchar_t line1[64], line2[64];
    swprintf(line1, 64, L"VOX:%d  RAM:%.1fM", vox, ramMb);
    swprintf(line2, 64, L"CPU:%.1f%% | %s", cpu, vox > 0 ? L"ACTIVE" : L"IDLE");
    drawText(line1, 594, 14, 148, 18, th.textPrimary, 9.2f, true);
    drawText(line2, 594, 34, 148, 18, vox > 0 ? th.accent : th.textMuted, 9.2f, true);

    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(752, 12, 784, 56), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(752, 12, 784, 56), brush_, 1.4f);
    drawText(L"!", 752, 12, 32, 44, th.accent, 16.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

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
    // LEFT: Piano Key Map (Black keys stay cleanly black when empty; selected key shown with accent border/badge)
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(16, 118, 412, 626), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(16, 118, 412, 626), brush_, 1.6f);

    drawText(L"PIANO KEY MAP  (+ LOAD WAV | R-CLICK RENAME)", 24, 124, 382, 22, th.textPrimary, 9.8f, true);

    const wchar_t* banks[3] = { L"BANK 1 (C3-B3)", L"BANK 2 (C4-B4)", L"BANK 3 (C5-B5)" };
    for (int b = 0; b < 3; ++b) {
        float bx = 26.0f + b * 126.0f;
        brush_->SetColor(toD2D(b == activeBank_ ? th.border : th.headerBg));
        rt_->FillRectangle(D2D1::RectF(bx, 150, bx + 120, 176), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(bx, 150, bx + 120, 176), brush_, 1.2f);
        drawText(banks[b], bx, 150, 120, 26, b == activeBank_ ? th.panelBg : th.textPrimary, 9.8f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    float pmX = 26.0f, pmY = 184.0f, pmW = 376.0f, pmH = 372.0f;
    float wKeyW = pmW / 7.0f;
    static const int kWhiteSemis[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int kBlackSemis[5] = { 1, 3, 6, 8, 10 };
    static const int kBlackAfterWhiteIdx[5] = { 0, 1, 3, 4, 5 };

    // 7 White keys
    for (int w = 0; w < 7; ++w) {
        int semi = kWhiteSemis[w];
        int sIdx = activeBank_ * 12 + semi;
        const auto& sl = controller_->slotMirror(sIdx);
        bool sel = (sIdx == selectedSlot_);
        bool hasSamp = sl.hasAnySample();
        float kx = pmX + w * wKeyW;

        uint32_t fillCol = (hasSamp && sl.hasCustomColor) ? sl.keyColor : (sel ? 0xF3E6DC : 0xFFFFFF);
        brush_->SetColor(toD2D(fillCol));
        rt_->FillRectangle(D2D1::RectF(kx, pmY, kx + wKeyW, pmY + pmH), brush_);
        brush_->SetColor(toD2D(sel ? th.accent : 0x181C22));
        rt_->DrawRectangle(D2D1::RectF(kx, pmY, kx + wKeyW, pmY + pmH), brush_, sel ? 2.6f : 1.2f);

        uint32_t txtCol = (hasSamp && sl.hasCustomColor) ? 0xFFFFFF : 0x181C22;
        std::wstring lbl = midiNoteName(sl.midiNote) + L"\n" + (hasSamp ? toWide(sl.name) : L"EMPTY");
        drawText(lbl.c_str(), kx + 2, pmY + pmH * 0.56f, wKeyW - 4, pmH * 0.40f,
                 txtCol, 9.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // 5 Black keys: always dark (#181C22) unless a sample is loaded on that black key; selected shown with border/dot
    float bKeyW = wKeyW * 0.64f;
    float bKeyH = pmH * 0.50f;
    for (int bk = 0; bk < 5; ++bk) {
        int semi = kBlackSemis[bk];
        int sIdx = activeBank_ * 12 + semi;
        const auto& sl = controller_->slotMirror(sIdx);
        bool sel = (sIdx == selectedSlot_);
        bool hasSamp = sl.hasAnySample();
        float kx = pmX + (kBlackAfterWhiteIdx[bk] + 1) * wKeyW - bKeyW * 0.5f;

        uint32_t fillCol = (hasSamp && sl.hasCustomColor) ? sl.keyColor : 0x181C22;
        brush_->SetColor(toD2D(fillCol));
        rt_->FillRectangle(D2D1::RectF(kx, pmY, kx + bKeyW, pmY + bKeyH), brush_);
        brush_->SetColor(toD2D(sel ? th.accent : 0x000000));
        rt_->DrawRectangle(D2D1::RectF(kx, pmY, kx + bKeyW, pmY + bKeyH), brush_, sel ? 2.2f : 1.0f);
        if (sel) {
            brush_->SetColor(toD2D(th.accent));
            rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx + bKeyW * 0.5f, pmY + bKeyH - 10.0f), 4.5f, 4.5f), brush_);
        }

        std::wstring bLbl = midiNoteName(sl.midiNote) + (hasSamp ? (L"\n" + toWide(sl.name)) : L"");
        drawText(bLbl.c_str(), kx + 1, pmY + 6.0f, bKeyW - 2, bKeyH - 18.0f, 0xFFFFFF, 7.8f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    auto drawBottomBtn = [&](float x, float w, const wchar_t* label, bool primary) {
        brush_->SetColor(toD2D(primary ? th.accent : th.headerBg));
        rt_->FillRectangle(D2D1::RectF(x, 568, x + w, 610), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(x, 568, x + w, 610), brush_, 1.2f);
        drawText(label, x, 568, w, 42, primary ? 0xFFFFFF : th.textPrimary, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    drawBottomBtn(26,  90, L"+ LOAD WAV", true);
    drawBottomBtn(122, 90, L"SAVE AS",    false);
    drawBottomBtn(218, 90, L"CLEAR KEY",  false);
    drawBottomBtn(314, 88, L"RESET KIT",  false);

    // RIGHT: Velocity Layers (Max 4: V1..V4) + Round-Robins (Max 9: 1..9 + DEL) + Wide Wave Editor
    auto& sl = controller_->slotMirror(selectedSlot_);
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(428, 118, 824, 626), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(428, 118, 824, 626), brush_, 1.6f);

    // Active RR & Velocity Layer sync is handled cleanly on trigger events in WM_TIMER

    selectedVelLayer_ = std::clamp(selectedVelLayer_, 0, kMaxVelLayers - 1);
    int nRRs = sl.rrCountInLayer(selectedVelLayer_);
    if (selectedRR_ >= nRRs) selectedRR_ = std::max(0, nRRs - 1);

    // Row 1 (y: 123..145): VEL LAYERS (V1..V4, max 4) on left + RR SLOTS (1..9, max 9) & DEL on right
    drawText(L"VEL:", 438, 123, 30, 22, th.textPrimary, 9.2f, true);
    for (int v = 0; v < kMaxVelLayers; ++v) {
        float vx = 468.0f + v * 25.0f;
        bool hasV = sl.rrCounts[v] > 0;
        bool selV = (v == selectedVelLayer_);
        brush_->SetColor(toD2D(selV ? th.accent : (hasV ? th.panelBg : th.headerBg)));
        rt_->FillRectangle(D2D1::RectF(vx, 123, vx + 22, 145), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(vx, 123, vx + 22, 145), brush_, 1.1f);
        wchar_t vLbl[8];
        swprintf(vLbl, 8, L"V%d", v + 1);
        drawText(vLbl, vx, 123, 22, 22, selV ? 0xFFFFFF : th.textPrimary, 8.8f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    drawText(nRRs >= kMaxRoundRobins ? L"9/9" : L"RR:", 570, 123, 24, 22, nRRs >= kMaxRoundRobins ? th.accent : th.textPrimary, 8.8f, true);
    for (int r = 0; r < kMaxRoundRobins; ++r) {
        float rx = 594.0f + r * 19.0f;
        bool exists = (r < nRRs);
        bool selR = (exists && r == selectedRR_);
        brush_->SetColor(toD2D(selR ? th.accent : (exists ? th.panelBg : th.headerBg)));
        rt_->FillRectangle(D2D1::RectF(rx, 123, rx + 17, 145), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(rx, 123, rx + 17, 145), brush_, 1.0f);
        std::wstring rLbl = std::to_wstring(r + 1);
        drawText(rLbl.c_str(), rx, 123, 17, 22, selR ? 0xFFFFFF : (exists ? th.textPrimary : th.textMuted), 8.2f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    if (nRRs > 0) {
        brush_->SetColor(toD2D(th.headerBg));
        rt_->FillRectangle(D2D1::RectF(768, 123, 812, 145), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(768, 123, 812, 145), brush_, 1.1f);
        drawText(L"DEL", 768, 123, 44, 22, th.accent, 8.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Full-width Wave Editor Box (x: 440..812, y: 151..310)
    float wx = 440.0f, wy = 151.0f, ww = 372.0f, wh = 159.0f;
    brush_->SetColor(toD2D(th.bg));
    rt_->FillRectangle(D2D1::RectF(wx, wy, wx + ww, wy + wh), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(wx, wy, wx + ww, wy + wh), brush_, 1.3f);

    if (nRRs > 0 && selectedRR_ < static_cast<int>(sl.velLayers[selectedVelLayer_].rrs.size())) {
        const auto& wi = sl.velLayers[selectedVelLayer_].rrs[static_cast<size_t>(selectedRR_)];
        float midY = wy + wh * 0.5f;
        brush_->SetColor(toD2D(th.textMuted, 0.3f));
        rt_->DrawLine(D2D1::Point2F(wx, midY), D2D1::Point2F(wx + ww, midY), brush_, 1.0f);

        float xStart = wx + wi.startNorm * ww;
        float xEnd   = wx + wi.endNorm * ww;
        float regW   = std::max(4.0f, xEnd - xStart);
        float xFadeIn  = xStart + wi.fadeInNorm * regW;
        float xFadeOut = xEnd - wi.fadeOutNorm * regW;

        float barW = ww / static_cast<float>(kWavePeakBins);
        for (int b = 0; b < kWavePeakBins; ++b) {
            int srcBin = sl.reverse ? (kWavePeakBins - 1 - b) : b;
            float bxPos = wx + b * barW;
            float posN = static_cast<float>(b) / static_cast<float>(kWavePeakBins);
            bool inRegion = (posN >= wi.startNorm && posN <= wi.endNorm);

            float fadeGain = 1.0f;
            if (inRegion) {
                if (bxPos < xFadeIn && (xFadeIn - xStart) > 1.0f) {
                    fadeGain = std::clamp((bxPos - xStart) / (xFadeIn - xStart), 0.05f, 1.0f);
                } else if (bxPos > xFadeOut && (xEnd - xFadeOut) > 1.0f) {
                    fadeGain = std::clamp((xEnd - bxPos) / (xEnd - xFadeOut), 0.05f, 1.0f);
                }
            }

            float pk = std::clamp(wi.peaks[static_cast<size_t>(srcBin)], 0.0f, 1.0f) * (inRegion ? fadeGain : 1.0f);
            float hHalf = std::max(1.0f, pk * (wh * 0.44f));
            brush_->SetColor(toD2D(inRegion ? th.textPrimary : th.textMuted, inRegion ? 0.90f : 0.25f));
            rt_->FillRectangle(D2D1::RectF(bxPos, midY - hHalf, bxPos + barW, midY + hHalf), brush_);
        }

        if (xFadeIn > xStart + 2.0f) {
            brush_->SetColor(toD2D(th.accent, 0.16f));
            rt_->FillRectangle(D2D1::RectF(xStart, wy, xFadeIn, wy + wh), brush_);
        }
        if (xEnd - xFadeOut > 2.0f) {
            brush_->SetColor(toD2D(th.accent, 0.16f));
            rt_->FillRectangle(D2D1::RectF(xFadeOut, wy, xEnd, wy + wh), brush_);
        }

        brush_->SetColor(toD2D(th.accent));
        rt_->DrawLine(D2D1::Point2F(xStart, wy), D2D1::Point2F(xStart, wy + wh), brush_, 2.2f);
        rt_->DrawLine(D2D1::Point2F(xEnd, wy),   D2D1::Point2F(xEnd, wy + wh),   brush_, 2.2f);
        rt_->DrawLine(D2D1::Point2F(xStart, wy + wh), D2D1::Point2F(xFadeIn, wy + 8.0f), brush_, 1.8f);
        rt_->DrawLine(D2D1::Point2F(xFadeOut, wy + 8.0f), D2D1::Point2F(xEnd, wy + wh), brush_, 1.8f);
        rt_->FillRectangle(D2D1::RectF(xFadeIn - 4.5f, wy + 4.0f, xFadeIn + 4.5f, wy + 13.0f), brush_);
        rt_->FillRectangle(D2D1::RectF(xFadeOut - 4.5f, wy + 4.0f, xFadeOut + 4.5f, wy + 13.0f), brush_);

        float ph = controller_->livePlayheadNorm();
        if (ph >= 0.0f && ph <= 1.0f) {
            float dispPh = sl.reverse ? (1.0f - ph) : ph;
            float phX = wx + dispPh * ww;
            brush_->SetColor(toD2D(0x28CD41));
            rt_->DrawLine(D2D1::Point2F(phX, wy), D2D1::Point2F(phX, wy + wh), brush_, 2.4f);
        }
    } else {
        drawText(L"CLICK '+ LOAD WAV' TO LOAD SAMPLES (MAX 4 VEL x 9 RR)", wx, wy, ww, wh, th.textMuted, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    drawText(L"COLOR:", 440, 320, 52, 20, th.textMuted, 9.5f, true);
    for (int c = 0; c < kNumKeyColors; ++c) {
        float sx = 496.0f + c * 26.0f;
        brush_->SetColor(toD2D(kStudioKeyColors[c]));
        rt_->FillRectangle(D2D1::RectF(sx, 320, sx + 21, 338), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(sx, 320, sx + 21, 338), brush_, (sl.hasCustomColor && sl.keyColor == kStudioKeyColors[c]) ? 2.4f : 1.0f);
    }

    bool fMic = controller_->getParamNormalized(kParamGlobalFakeMic) >= 0.5;
    const wchar_t* gLabels[3] = {
        sl.fakeRR  ? L"KEY FAKE RR: ON"  : L"KEY FAKE RR: OFF",
        sl.fakeVel ? L"KEY FAKE VEL: ON" : L"KEY FAKE VEL: OFF",
        fMic       ? L"GLOBAL MIC: ON"   : L"GLOBAL MIC: OFF"
    };
    bool gStates[3] = { sl.fakeRR, sl.fakeVel, fMic };
    for (int g = 0; g < 3; ++g) {
        float px = 440.0f + g * 126.0f;
        brush_->SetColor(toD2D(gStates[g] ? th.accent : th.headerBg));
        rt_->FillRectangle(D2D1::RectF(px, 348, px + 118, 376), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(px, 348, px + 118, 376), brush_, 1.2f);
        drawText(gLabels[g], px, 348, 118, 28, gStates[g] ? 0xFFFFFF : th.textPrimary, 9.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    std::wstring m1 = sl.oneShot ? L"MODE: ONE-SHOT" : L"MODE: GATE";
    std::wstring m2 = sl.chokeGroup == 0 ? L"CHOKE: OFF" : (L"CHOKE: " + std::to_wstring(sl.chokeGroup));
    std::wstring m3 = sl.reverse ? L"REVERSE: ON" : L"REVERSE: OFF";
    const wchar_t* dModes[3] = { m1.c_str(), m2.c_str(), m3.c_str() };
    bool dStates[3] = { false, sl.chokeGroup > 0, sl.reverse };
    for (int d = 0; d < 3; ++d) {
        float dx = 440.0f + d * 126.0f;
        brush_->SetColor(toD2D(dStates[d] ? th.border : th.headerBg));
        rt_->FillRectangle(D2D1::RectF(dx, 384, dx + 118, 412), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(dx, 384, dx + 118, 412), brush_, 1.2f);
        drawText(dModes[d], dx, 384, 118, 28, dStates[d] ? th.panelBg : th.textPrimary, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // 4 Faders: GAIN in dB (0.0 dB default), PITCH in st (0.0 st default), ATTACK ms, RELEASE ms
    wchar_t vGain[32], vPitch[32], vAtt[32], vRel[32];
    formatDbLabel(vGain, 32, gainNormToDb(sl.gainNorm));
    swprintf(vPitch, 32, L"%+.1f st", (sl.pitch - 0.5f) * 48.0f);
    swprintf(vAtt,   32, L"%.0f ms", sl.attack * 200.0f);
    swprintf(vRel,   32, L"%.0f ms", releaseNormToMs(sl.release));

    drawFader(440, 442, 368, 15, sl.gainNorm, L"GAIN (DBL-CLICK = 0.0 dB)", vGain,  th);
    drawFader(440, 490, 368, 15, sl.pitch,    L"PITCH (DBL-CLICK = 0 st)",  vPitch, th);
    drawFader(440, 538, 368, 15, sl.attack,   L"ATTACK",                    vAtt,   th);
    drawFader(440, 586, 368, 15, sl.release,  L"RELEASE / DECAY",           vRel,   th);
}

void WreckageWinView::drawStageTab(const ThemePalette& th) {
    // Clean STAGE & MIX tab: NO DRUM/MIX side buttons; Stage X/Y on top + 4 Mic dB Faders below
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(16, 116, 824, 626), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(16, 116, 824, 626), brush_, 1.6f);

    const auto& selSl = controller_->slotMirror(selectedSlot_);
    std::wstring stageTitle = midiNoteName(selSl.midiNote) + L" : " + toWide(selSl.name);
    drawText(stageTitle.c_str(), 28, 122, 264, 26, th.textPrimary, 12.0f, true);
    const wchar_t* sBanks[3] = { L"B1", L"B2", L"B3" };
    for (int b = 0; b < 3; ++b) {
        float sbx = 300.0f + b * 44.0f;
        bool bAct = (b == activeBank_);
        brush_->SetColor(toD2D(bAct ? th.border : th.headerBg));
        rt_->FillRectangle(D2D1::RectF(sbx, 122, sbx + 40, 148), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(sbx, 122, sbx + 40, 148), brush_, 1.2f);
        drawText(sBanks[b], sbx, 122, 40, 26, bAct ? th.panelBg : th.textPrimary, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

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

    brush_->SetColor(toD2D(th.bg));
    rt_->FillRectangle(D2D1::RectF(32, 156, 808, 366), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(32, 156, 808, 366), brush_, 1.4f);

    auto stageToScreen = [](float sx, float sy, float& outX, float& outY) {
        float ny = std::clamp(sy, 0.0f, 1.0f);
        float nx = std::clamp(sx, -1.0f, 1.0f);
        float halfW = 360.0f - ny * 130.0f;
        outX = 420.0f + nx * halfW;
        outY = 352.0f - ny * 180.0f;
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
    drawText(L"FRONT CENTER (X=0, Y=0  |  DBL-CLICK DOT TO RESET)", 32, 350, 776, 16, th.textMuted, 9.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    int loadedInBank = 0;
    for (int i = 0; i < 12; ++i) {
        if (controller_->slotMirror(activeBank_ * 12 + i).hasAnySample()) ++loadedInBank;
    }
    if (loadedInBank == 0) {
        drawText(L"STAGE IS EMPTY  —  LOAD .WAV SAMPLES IN 'SOURCE' TO PLACE INSTRUMENTS", 32, 236, 776, 30, th.textMuted, 11.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    auto drawOneStageDot = [&](int sIdx, int displayNum, bool isSelected) {
        const auto& sl = controller_->slotMirror(sIdx);
        if (!sl.hasAnySample()) return;
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

    wchar_t vX[32], vY[32];
    swprintf(vX, 32, L"%+d%%", static_cast<int>(std::round(selSl.stageX * 100.0f)));
    swprintf(vY, 32, L"%d%%",  static_cast<int>(std::round(selSl.stageY * 100.0f)));
    drawFader(32,  392, 376, 13, (selSl.stageX + 1.0f) * 0.5f, L"STAGE X (PAN L/R  |  DBL-CLICK = 0)", vX, th);
    drawFader(432, 392, 376, 13, selSl.stageY,                 L"STAGE Y (DEPTH    |  DBL-CLICK = 0)", vY, th);

    // 4 Wide Mic Faders in dB (0.0 dB default & double-click reset) across full width
    const wchar_t* micNames[4] = {
        L"CLOSE MIC (DBL-CLICK = 0.0 dB)",
        L"ROOM MIC (DBL-CLICK = 0.0 dB)",
        L"HALL MIC (DBL-CLICK = 0.0 dB)",
        L"SUB MIC (DBL-CLICK = 0.0 dB)"
    };
    for (int b = 0; b < 4; ++b) {
        float y = 442.0f + b * 44.0f;
        float val = static_cast<float>(controller_->getParamNormalized(kParamBusCloseGain + b));
        bool byp = controller_->getParamNormalized(kParamBusCloseBypass + b) >= 0.5;
        wchar_t vMic[32];
        formatDbLabel(vMic, 32, gainNormToDb(val));
        drawFader(36, y, 686, 14, val, micNames[b], vMic, th, false, !byp);

        brush_->SetColor(toD2D(byp ? th.headerBg : th.accent));
        rt_->FillRectangle(D2D1::RectF(736, y - 4, 804, y + 18), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(736, y - 4, 804, y + 18), brush_, 1.2f);
        drawText(byp ? L"OFF" : L"ON", 736, y - 4, 68, 22, byp ? th.textMuted : 0xFFFFFF, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void WreckageWinView::drawMasterFxTab(const ThemePalette& th) {
    brush_->SetColor(toD2D(th.panelBg));
    rt_->FillRectangle(D2D1::RectF(16, 116, 824, 626), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(16, 116, 824, 626), brush_, 1.6f);

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

        float ledX = bx + bw - 16.0f, ledY = by + 14.0f;
        brush_->SetColor(toD2D(isOn ? th.accent : th.headerBg));
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ledX, ledY), 7.5f, 7.5f), brush_);
        brush_->SetColor(toD2D(selected ? th.panelBg : th.border));
        rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(ledX, ledY), 7.5f, 7.5f), brush_, 1.4f);

        uint32_t fg = selected ? th.panelBg : th.textPrimary;
        brush_->SetColor(toD2D(fg));
        float cx = bx + 42.0f, cy = by + 28.0f;
        if (m == 0) {
            rt_->DrawLine(D2D1::Point2F(cx - 18, cy - 6), D2D1::Point2F(cx + 2, cy - 6), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(cx + 2, cy - 6), D2D1::Point2F(cx + 18, cy + 10), brush_, 2.0f);
        } else if (m == 1) {
            rt_->DrawLine(D2D1::Point2F(cx - 20, cy + 4), D2D1::Point2F(cx - 6, cy - 8), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(cx - 6, cy - 8), D2D1::Point2F(cx + 6, cy + 6), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(cx + 6, cy + 6), D2D1::Point2F(cx + 20, cy - 2), brush_, 2.0f);
        } else if (m == 2) {
            rt_->DrawLine(D2D1::Point2F(cx - 16, cy + 10), D2D1::Point2F(cx, cy - 2), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(cx, cy - 2), D2D1::Point2F(cx + 18, cy - 6), brush_, 2.0f);
        } else if (m == 3) {
            rt_->DrawLine(D2D1::Point2F(cx - 16, cy + 8), D2D1::Point2F(cx - 4, cy + 6), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(cx - 4, cy + 6), D2D1::Point2F(cx + 4, cy - 6), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(cx + 4, cy - 6), D2D1::Point2F(cx + 16, cy - 8), brush_, 2.0f);
        } else if (m == 4) {
            rt_->DrawLine(D2D1::Point2F(cx - 12, cy - 8), D2D1::Point2F(cx - 12, cy + 8), brush_, 2.2f);
            rt_->DrawLine(D2D1::Point2F(cx, cy - 4), D2D1::Point2F(cx, cy + 8), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(cx + 12, cy), D2D1::Point2F(cx + 12, cy + 8), brush_, 1.8f);
        } else {
            rt_->DrawRectangle(D2D1::RectF(cx - 14, cy - 6, cx + 6, cy + 10), brush_, 1.6f);
            rt_->DrawRectangle(D2D1::RectF(cx - 6, cy - 11, cx + 14, cy + 5), brush_, 1.6f);
        }

        drawText(modNames[m], bx, by + 44, bw, 24, fg, 11.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    brush_->SetColor(toD2D(th.bg));
    rt_->FillRectangle(D2D1::RectF(28, 214, 812, 612), brush_);
    brush_->SetColor(toD2D(th.border));
    rt_->DrawRectangle(D2D1::RectF(28, 214, 812, 612), brush_, 1.4f);

    bool modEnabled = controller_->getParamNormalized(modOnIds[activeFxModule_]) >= 0.5;
    float outLvl = controller_->liveOutLevel();
    wchar_t v1[32], v2[32], v3[32], v4[32], v5[32], v6[32];

    if (activeFxModule_ == 0) {
        drawText(L"FILTER MODULE  (LIVE CUTOFF & RESONANCE RESPONSE)", 44, 220, 520, 20, th.textPrimary, 11.0f, true);
        static const wchar_t* fTypes[8] = { L"LP 24dB", L"LP 12dB", L"HP 24dB", L"HP 12dB", L"BP 12dB", L"BP 24dB", L"PEAK", L"NOTCH" };
        int ft = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamFilterType) * 7.0)), 0, 7);
        for (int t = 0; t < 8; ++t) {
            float tx = 44.0f + t * 94.0f;
            brush_->SetColor(toD2D(t == ft ? th.accent : th.headerBg));
            rt_->FillRectangle(D2D1::RectF(tx, 244, tx + 88, 272), brush_);
            brush_->SetColor(toD2D(th.border));
            rt_->DrawRectangle(D2D1::RectF(tx, 244, tx + 88, 272), brush_, 1.2f);
            drawText(fTypes[t], tx, 244, 88, 28, t == ft ? 0xFFFFFF : th.textPrimary, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        float cNorm = static_cast<float>(controller_->getParamNormalized(kParamFilterCutoff));
        float rNorm = static_cast<float>(controller_->getParamNormalized(kParamFilterRes));

        float cx = 44.0f, cy = 280.0f, cw = 748.0f, ch = 98.0f;
        brush_->SetColor(toD2D(th.panelBg));
        rt_->FillRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), brush_, 1.2f);

        float midY = cy + ch * 0.45f;
        brush_->SetColor(toD2D(th.textMuted, 0.35f));
        rt_->DrawLine(D2D1::Point2F(cx, midY), D2D1::Point2F(cx + cw, midY), brush_, 1.0f);

        float cutX = cx + cNorm * cw;
        float resBump = rNorm * (ch * 0.34f);
        brush_->SetColor(toD2D(modEnabled ? th.accent : th.textMuted));
        float px0 = cx, py0 = midY;
        for (int s = 1; s <= 72; ++s) {
            float u = s / 72.0f;
            float px = cx + u * cw;
            float diff = u - cNorm;
            float resTerm = resBump * std::exp(-(diff * diff) / 0.0025f);
            float py = midY;
            if (ft == 0 || ft == 1) {
                float slope = (ft == 0) ? 18.0f : 10.0f;
                float drop = (diff > 0.0f) ? std::min(ch * 0.48f, diff * slope * ch * 0.14f) : 0.0f;
                py = midY - resTerm + drop;
            } else if (ft == 2 || ft == 3) {
                float slope = (ft == 2) ? 18.0f : 10.0f;
                float drop = (diff < 0.0f) ? std::min(ch * 0.48f, (-diff) * slope * ch * 0.14f) : 0.0f;
                py = midY - resTerm + drop;
            } else if (ft == 4 || ft == 5) {
                float dist = std::fabs(diff);
                py = (cy + ch - 10.0f) - (ch * 0.50f + resTerm * 0.6f) * std::exp(-(dist * dist) / 0.015f);
            } else if (ft == 6) {
                py = midY - (ch * 0.25f + resTerm) * std::exp(-(diff * diff) / 0.006f);
            } else {
                py = midY + (ch * 0.38f) * std::exp(-(diff * diff) / (0.003f + (1.0f - rNorm) * 0.01f));
            }
            py = std::clamp(py, cy + 6.0f, cy + ch - 6.0f);
            if (s > 1) rt_->DrawLine(D2D1::Point2F(px0, py0), D2D1::Point2F(px, py), brush_, 2.2f);
            px0 = px; py0 = py;
        }
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cutX, std::clamp(midY - resBump, cy + 8.0f, cy + ch - 8.0f)), 5.5f, 5.5f), brush_);

        float hz = 20.0f * std::pow(1000.0f, cNorm);
        if (hz >= 1000.0f) swprintf(v1, 32, L"%.2f kHz", hz / 1000.0f);
        else swprintf(v1, 32, L"%.0f Hz", hz);
        swprintf(v2, 32, L"%d%%", static_cast<int>(std::round(rNorm * 100.0f)));

        drawFader(44, 412, 748, 16, cNorm, L"CUTOFF FREQUENCY", v1, th, true, modEnabled);
        drawFader(44, 468, 748, 16, rNorm, L"RESONANCE",        v2, th, false, modEnabled);
    }
    else if (activeFxModule_ == 1) {
        drawText(L"3-BAND PARAMETRIC EQ  (LIVE SPECTRUM + DBL-CLICK GAIN = 0.0 dB)", 44, 220, 680, 20, th.textPrimary, 11.0f, true);
        float cx = 44.0f, cy = 244.0f, cw = 748.0f, ch = 122.0f;
        brush_->SetColor(toD2D(th.panelBg));
        rt_->FillRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), brush_, 1.2f);

        float lf = static_cast<float>(controller_->getParamNormalized(kParamEqLowFreq));
        float lg = static_cast<float>(controller_->getParamNormalized(kParamEqLowGain));
        float mf = static_cast<float>(controller_->getParamNormalized(kParamEqMidFreq));
        float mg = static_cast<float>(controller_->getParamNormalized(kParamEqMidGain));
        float hf = static_cast<float>(controller_->getParamNormalized(kParamEqHighFreq));
        float hg = static_cast<float>(controller_->getParamNormalized(kParamEqHighGain));

        float nx[3] = { cx + (0.06f + lf * 0.24f) * cw, cx + (0.34f + mf * 0.32f) * cw, cx + (0.70f + hf * 0.24f) * cw };
        float ny[3] = { cy + (1.0f - lg) * ch, cy + (1.0f - mg) * ch, cy + (1.0f - hg) * ch };
        float midY = cy + ch * 0.5f;

        if (outLvl > 0.002f) {
            brush_->SetColor(toD2D(th.accent, 0.22f));
            int nBars = 48;
            float bw = cw / static_cast<float>(nBars);
            for (int sb = 0; sb < nBars; ++sb) {
                float u = (sb + 0.5f) / static_cast<float>(nBars);
                float px = cx + u * cw;
                float eqBoost = 1.0f;
                for (int b = 0; b < 3; ++b) {
                    float d = (px - nx[b]) / 68.0f;
                    eqBoost += ((midY - ny[b]) / (ch * 0.5f)) * 0.65f * std::exp(-d * d);
                }
                float specShape = (1.0f - u * 0.65f) * (0.65f + 0.35f * std::sin(u * 23.0f + outLvl * 12.0f));
                float barH = std::clamp(outLvl * specShape * std::max(0.2f, eqBoost) * ch * 1.15f, 2.0f, ch - 6.0f);
                rt_->FillRectangle(D2D1::RectF(cx + sb * bw + 1.0f, cy + ch - barH, cx + (sb + 1) * bw - 1.0f, cy + ch), brush_);
            }
        }

        brush_->SetColor(toD2D(th.textMuted, 0.4f));
        rt_->DrawLine(D2D1::Point2F(cx, midY), D2D1::Point2F(cx + cw, midY), brush_, 1.0f);

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

        drawFader(44,  404, 236, 12, lf, L"LOW FREQ",          v1, th, false, modEnabled);
        drawFader(300, 404, 236, 12, mf, L"MID FREQ",          v3, th, false, modEnabled);
        drawFader(556, 404, 236, 12, hf, L"HIGH FREQ",         v5, th, false, modEnabled);

        drawFader(44,  462, 236, 12, lg, L"LOW GAIN (DBL=0)",  v2, th, true,  modEnabled);
        drawFader(300, 462, 236, 12, mg, L"MID GAIN (DBL=0)",  v4, th, true,  modEnabled);
        drawFader(556, 462, 236, 12, hg, L"HIGH GAIN (DBL=0)", v6, th, true,  modEnabled);
    }
    else if (activeFxModule_ == 2) {
        float cTh = static_cast<float>(controller_->getParamNormalized(kParamCompThresh));
        float cRt = static_cast<float>(controller_->getParamNormalized(kParamCompRatio));
        float cAt = static_cast<float>(controller_->getParamNormalized(kParamCompAttack));
        float cRl = static_cast<float>(controller_->getParamNormalized(kParamCompRelease));
        float cKn = static_cast<float>(controller_->getParamNormalized(kParamCompKnee));
        float cMk = static_cast<float>(controller_->getParamNormalized(kParamCompMakeup));

        float gr = controller_->liveGrDb();
        wchar_t hdrGr[80];
        swprintf(hdrGr, 80, L"COMPRESSOR  |  GR: -%.1f dB  |  DBL-CLICK MAKEUP = 0.0 dB", gr);
        drawText(hdrGr, 44, 220, 748, 20, th.textPrimary, 11.0f, true);

        float cx = 44.0f, cy = 242.0f, cw = 748.0f, ch = 84.0f;
        brush_->SetColor(toD2D(th.panelBg));
        rt_->FillRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), brush_, 1.2f);

        float ratio = 1.0f + cRt * 11.0f;
        float threshX = cx + cTh * cw;
        brush_->SetColor(toD2D(th.textMuted, 0.35f));
        rt_->DrawLine(D2D1::Point2F(threshX, cy), D2D1::Point2F(threshX, cy + ch), brush_, 1.0f);

        brush_->SetColor(toD2D(modEnabled ? th.accent : th.textMuted));
        float p0x = cx, p0y = cy + ch - 8.0f;
        float p1x = threshX, p1y = p0y - cTh * (ch - 16.0f);
        float remW = std::max(10.0f, (cx + cw) - p1x);
        float p2y = std::max(cy + 8.0f, p1y - (remW / ratio));
        rt_->DrawLine(D2D1::Point2F(p0x, p0y), D2D1::Point2F(p1x, p1y), brush_, 2.0f);
        rt_->DrawLine(D2D1::Point2F(p1x, p1y), D2D1::Point2F(cx + cw, p2y), brush_, 2.2f);

        float grNorm = std::clamp(gr / 18.0f, 0.0f, 1.0f);
        float outNorm = std::clamp(outLvl, 0.0f, 1.0f);
        brush_->SetColor(toD2D(th.faderTrack));
        rt_->FillRectangle(D2D1::RectF(cx + cw - 200.0f, cy + 12.0f, cx + cw - 16.0f, cy + 26.0f), brush_);
        rt_->FillRectangle(D2D1::RectF(cx + cw - 200.0f, cy + 44.0f, cx + cw - 16.0f, cy + 58.0f), brush_);
        brush_->SetColor(toD2D(th.accent));
        rt_->FillRectangle(D2D1::RectF(cx + cw - 200.0f, cy + 12.0f, cx + cw - 200.0f + 184.0f * grNorm, cy + 26.0f), brush_);
        brush_->SetColor(toD2D(0x28CD41));
        rt_->FillRectangle(D2D1::RectF(cx + cw - 200.0f, cy + 44.0f, cx + cw - 200.0f + 184.0f * outNorm, cy + 58.0f), brush_);
        drawText(L"GAIN REDUCTION", cx + cw - 200.0f, cy + 27.0f, 184.0f, 14.0f, th.textMuted, 8.0f, true);
        drawText(L"OUTPUT LEVEL",   cx + cw - 200.0f, cy + 59.0f, 184.0f, 14.0f, th.textMuted, 8.0f, true);

        swprintf(v1, 32, L"%.1f dB", -36.0f + cTh * 36.0f);
        swprintf(v2, 32, L"%.1f : 1", ratio);
        swprintf(v3, 32, L"%.1f ms", 0.5f + cAt * 79.5f);
        swprintf(v4, 32, L"%.0f ms", 15.0f + cRl * 385.0f);
        swprintf(v5, 32, L"%.1f dB", cKn * 12.0f);
        swprintf(v6, 32, L"%+.1f dB", (cMk - 0.5f) * 24.0f);

        drawFader(44,  362, 356, 13, cTh, L"THRESHOLD",       v1, th, true,  modEnabled);
        drawFader(436, 362, 356, 13, cRt, L"RATIO",           v2, th, false, modEnabled);
        drawFader(44,  416, 356, 13, cAt, L"ATTACK",          v3, th, false, modEnabled);
        drawFader(436, 416, 356, 13, cRl, L"RELEASE",         v4, th, false, modEnabled);
        drawFader(44,  470, 356, 13, cKn, L"KNEE / SHOULDER", v5, th, false, modEnabled);
        drawFader(436, 470, 356, 13, cMk, L"MAKEUP (DBL=0)",  v6, th, true,  modEnabled);
    }
    else if (activeFxModule_ == 3) {
        float sDr = static_cast<float>(controller_->getParamNormalized(kParamSatDrive));
        float sWm = static_cast<float>(controller_->getParamNormalized(kParamSatWarmth));
        float sMx = static_cast<float>(controller_->getParamNormalized(kParamSatMix));

        drawText(L"SATURATOR  |  0% = FULL DRY, 100% = FULL WET", 44, 222, 620, 22, th.textPrimary, 11.5f, true);
        float cx = 44.0f, cy = 248.0f, cw = 748.0f, ch = 74.0f;
        brush_->SetColor(toD2D(th.panelBg));
        rt_->FillRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), brush_, 1.2f);

        float midY = cy + ch * 0.5f;
        brush_->SetColor(toD2D(th.textMuted, 0.45f));
        float dx0 = cx + 16.0f, dy0 = midY;
        for (int i = 1; i <= 80; ++i) {
            float t = i / 80.0f;
            float px = cx + 16.0f + t * (cw - 32.0f);
            float py = midY - std::sin(t * 6.0f * kPi) * (ch * 0.34f);
            rt_->DrawLine(D2D1::Point2F(dx0, dy0), D2D1::Point2F(px, py), brush_, 1.2f);
            dx0 = px; dy0 = py;
        }

        brush_->SetColor(toD2D(modEnabled ? th.accent : th.textMuted));
        float drvGain = 1.0f + sDr * 4.5f;
        float px0 = cx + 16.0f, py0 = midY;
        for (int i = 1; i <= 80; ++i) {
            float t = i / 80.0f;
            float px = cx + 16.0f + t * (cw - 32.0f);
            float baseW = std::sin(t * 6.0f * kPi);
            float satW = std::tanh(baseW * drvGain);
            float blended = baseW * (1.0f - sMx) + satW * sMx;
            float py = midY - std::clamp(blended, -1.1f, 1.1f) * (ch * 0.34f);
            rt_->DrawLine(D2D1::Point2F(px0, py0), D2D1::Point2F(px, py), brush_, 2.2f);
            px0 = px; py0 = py;
        }

        swprintf(v1, 32, L"%d%%", static_cast<int>(std::round(sDr * 100.0f)));
        swprintf(v2, 32, L"%d%%", static_cast<int>(std::round(sWm * 100.0f)));
        swprintf(v3, 32, L"%d%%", static_cast<int>(std::round(sMx * 100.0f)));

        drawFader(44, 356, 748, 16, sDr, L"DRIVE",         v1, th, true,  modEnabled);
        drawFader(44, 412, 748, 16, sWm, L"WARMTH / TONE", v2, th, false, modEnabled);
        drawFader(44, 468, 748, 16, sMx, L"WET / DRY MIX", v3, th, true,  modEnabled);
    }
    else if (activeFxModule_ == 4) {
        int dMode = (controller_->getParamNormalized(kParamDelayMode) >= 0.5) ? 1 : 0;
        float dTm = static_cast<float>(controller_->getParamNormalized(kParamDelayTime));
        float dFb = static_cast<float>(controller_->getParamNormalized(kParamDelayFeedback));
        float dTn = static_cast<float>(controller_->getParamNormalized(kParamDelayTone));
        float dWd = static_cast<float>(controller_->getParamNormalized(kParamDelayWidth));
        float dMx = static_cast<float>(controller_->getParamNormalized(kParamDelayMix));

        float timeMs = 20.0f + dTm * 780.0f;
        int fbPct = static_cast<int>(std::round(dFb * 88.0f));
        int wdPct = static_cast<int>(std::round(dWd * 100.0f));
        wchar_t digInfo[128];
        swprintf(digInfo, 128, L"DELAY  |  TIME: %.0f ms  |  FB: %d%%  |  WIDTH: %d%%", timeMs, fbPct, wdPct);
        drawText(digInfo, 44, 220, 460, 22, th.textPrimary, 10.5f, true);

        const wchar_t* dModes[2] = { L"MODERN", L"VINTAGE" };
        for (int dm = 0; dm < 2; ++dm) {
            float dx = 580.0f + dm * 106.0f;
            brush_->SetColor(toD2D(dm == dMode ? th.accent : th.headerBg));
            rt_->FillRectangle(D2D1::RectF(dx, 220, dx + 100, 244), brush_);
            brush_->SetColor(toD2D(th.border));
            rt_->DrawRectangle(D2D1::RectF(dx, 220, dx + 100, 244), brush_, 1.2f);
            drawText(dModes[dm], dx, 220, 100, 24, dm == dMode ? 0xFFFFFF : th.textPrimary, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        float cx = 44.0f, cy = 248.0f, cw = 748.0f, ch = 76.0f;
        brush_->SetColor(toD2D(th.panelBg));
        rt_->FillRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), brush_, 1.2f);

        float baseY = cy + ch - 8.0f;
        float tapSpacing = 28.0f + dTm * 95.0f;
        float amp = 1.0f;
        for (int tap = 0; tap < 8; ++tap) {
            float tx = cx + 26.0f + tap * tapSpacing;
            if (tx > cx + cw - 14.0f) break;
            float thgt = std::max(4.0f, amp * (ch - 18.0f));
            brush_->SetColor(toD2D(modEnabled ? (tap == 0 ? th.textPrimary : th.accent) : th.textMuted));
            rt_->FillRectangle(D2D1::RectF(tx - 4.0f, baseY - thgt, tx + 4.0f, baseY), brush_);
            amp *= std::clamp(dFb + 0.15f, 0.05f, 0.92f);
        }

        swprintf(v1, 32, L"%.0f ms", timeMs);
        swprintf(v2, 32, L"%d%%", fbPct);
        swprintf(v3, 32, L"%d%%", static_cast<int>(std::round(dTn * 100.0f)));
        swprintf(v4, 32, L"%d%%", wdPct);
        swprintf(v5, 32, L"%d%%", static_cast<int>(std::round(dMx * 100.0f)));

        drawFader(44,  358, 356, 13, dTm, L"DELAY TIME", v1, th, true,  modEnabled);
        drawFader(436, 358, 356, 13, dFb, L"FEEDBACK",   v2, th, false, modEnabled);
        drawFader(44,  412, 356, 13, dTn, L"TONE",       v3, th, false, modEnabled);
        drawFader(436, 412, 356, 13, dWd, L"WIDTH",      v4, th, false, modEnabled);
        drawFader(44,  468, 748, 13, dMx, L"DELAY MIX",  v5, th, true,  modEnabled);
    }
    else {
        int rMode = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamRevMode) * 2.0)), 0, 2);
        float rPd = static_cast<float>(controller_->getParamNormalized(kParamRevPreDelay));
        float rDc = static_cast<float>(controller_->getParamNormalized(kParamRevDecay));
        float rSz = static_cast<float>(controller_->getParamNormalized(kParamRevSize));
        float rLc = static_cast<float>(controller_->getParamNormalized(kParamRevLowCut));
        float rHc = static_cast<float>(controller_->getParamNormalized(kParamRevHighCut));
        float rMx = static_cast<float>(controller_->getParamNormalized(kParamRevMix));

        float pdMs = rPd * 120.0f;
        float decSec = 0.2f + rDc * 5.8f;
        wchar_t revDig[128];
        swprintf(revDig, 128, L"REVERB  |  PRE: %.0f ms  |  DECAY: %.2f s  |  SIZE: %d%%",
                 pdMs, decSec, static_cast<int>(std::round(rSz * 100.0f)));
        drawText(revDig, 44, 220, 470, 22, th.textPrimary, 10.5f, true);

        const wchar_t* rModes[3] = { L"ROOM", L"PLATE", L"HALL" };
        for (int rm = 0; rm < 3; ++rm) {
            float rx = 520.0f + rm * 90.0f;
            brush_->SetColor(toD2D(rm == rMode ? th.accent : th.headerBg));
            rt_->FillRectangle(D2D1::RectF(rx, 220, rx + 84, 244), brush_);
            brush_->SetColor(toD2D(th.border));
            rt_->DrawRectangle(D2D1::RectF(rx, 220, rx + 84, 244), brush_, 1.2f);
            drawText(rModes[rm], rx, 220, 84, 24, rm == rMode ? 0xFFFFFF : th.textPrimary, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        float cx = 44.0f, cy = 248.0f, cw = 748.0f, ch = 76.0f;
        brush_->SetColor(toD2D(th.panelBg));
        rt_->FillRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), brush_, 1.2f);

        float boxW = 24.0f + rSz * 52.0f;
        float boxH = 22.0f + rSz * 38.0f;
        brush_->SetColor(toD2D(modEnabled ? th.accent : th.textMuted, 0.25f));
        rt_->FillRectangle(D2D1::RectF(cx + 14.0f, cy + (ch - boxH) * 0.5f, cx + 14.0f + boxW, cy + (ch + boxH) * 0.5f), brush_);
        brush_->SetColor(toD2D(th.border));
        rt_->DrawRectangle(D2D1::RectF(cx + 14.0f, cy + (ch - boxH) * 0.5f, cx + 14.0f + boxW, cy + (ch + boxH) * 0.5f), brush_, 1.4f);

        float waveStartX = cx + 104.0f;
        float midY = cy + ch * 0.5f;
        float prePx = waveStartX + rPd * 110.0f;
        float tailSpan = std::max(80.0f, (0.25f + rDc * 0.75f) * (cw - (prePx - cx) - 24.0f));
        brush_->SetColor(toD2D(modEnabled ? th.accent : th.textMuted));
        rt_->DrawLine(D2D1::Point2F(waveStartX, midY), D2D1::Point2F(prePx, midY), brush_, 1.5f);

        float prevX = prePx, prevTop = midY - (ch * 0.38f * rMx + 4.0f), prevBot = midY + (ch * 0.38f * rMx + 4.0f);
        for (int s = 1; s <= 48; ++s) {
            float t = s / 48.0f;
            float px = prePx + t * tailSpan;
            float env = std::exp(-t * (2.2f + (1.0f - rDc) * 3.5f)) * (0.35f + 0.65f * rMx);
            float hHalf = env * (ch * 0.42f);
            float topY = midY - hHalf, botY = midY + hHalf;
            rt_->DrawLine(D2D1::Point2F(prevX, prevTop), D2D1::Point2F(px, topY), brush_, 2.0f);
            rt_->DrawLine(D2D1::Point2F(prevX, prevBot), D2D1::Point2F(px, botY), brush_, 2.0f);
            prevX = px; prevTop = topY; prevBot = botY;
        }

        swprintf(v1, 32, L"%.0f ms", pdMs);
        swprintf(v2, 32, L"%.2f s",  decSec);
        swprintf(v3, 32, L"%d%%",    static_cast<int>(std::round(rSz * 100.0f)));
        swprintf(v4, 32, L"%.0f Hz", 20.0f + rLc * 480.0f);
        swprintf(v5, 32, L"%.1f kHz",(1500.0f + rHc * 16500.0f) / 1000.0f);
        swprintf(v6, 32, L"%d%%",    static_cast<int>(std::round(rMx * 100.0f)));

        drawFader(44,  354, 356, 12, rPd, L"PRE-DELAY",  v1, th, false, modEnabled);
        drawFader(436, 354, 356, 12, rDc, L"DECAY TIME", v2, th, true,  modEnabled);
        drawFader(44,  408, 356, 12, rSz, L"SIZE",       v3, th, false, modEnabled);
        drawFader(436, 408, 356, 12, rLc, L"LOW CUT",    v4, th, false, modEnabled);
        drawFader(44,  462, 356, 12, rHc, L"HIGH CUT",   v5, th, false, modEnabled);
        drawFader(436, 462, 356, 12, rMx, L"REVERB MIX", v6, th, true,  modEnabled);
    }

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

    // Bottom 3-Octave Keyboard (C3 - B5): Single unified pressed check (Mouse + MIDI) with clear press shift & highlight
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

    auto getNoteColor = [&](int note, uint32_t& outCol) -> bool {
        for (int i = 0; i < kTotalSlots; ++i) {
            const auto& sl = controller_->slotMirror(i);
            if (sl.midiNote == note && sl.hasAnySample() && sl.hasCustomColor && !sl.purged) {
                outCol = sl.keyColor;
                return true;
            }
        }
        return false;
    };

    for (int oct = 0; oct < 3; ++oct) {
        for (int k = 0; k < 7; ++k) {
            int note = kBaseMidiNote + (oct + keyboardOctaveOffset_) * 12 + kWhiteOffsets[k];
            bool pressed = isNotePhysicallyDown(note);
            float kx = kbX + (oct * 7 + k) * whiteW;
            float pressShift = pressed ? 4.0f : 0.0f;

            uint32_t keyCol = 0;
            bool hasCol = getNoteColor(note, keyCol);

            uint32_t fillRgb = 0xFFFFFF;
            if (pressed) fillRgb = hasCol ? 0xFFFFFF : 0xFFE2CF;
            else if (hasCol) fillRgb = keyCol;

            brush_->SetColor(toD2D(fillRgb));
            rt_->FillRectangle(D2D1::RectF(kx, kbY + pressShift, kx + whiteW, kbY + kbH), brush_);
            if (pressed) {
                brush_->SetColor(toD2D(th.accent, 0.35f));
                rt_->FillRectangle(D2D1::RectF(kx + 1, kbY + pressShift, kx + whiteW - 1, kbY + kbH - 1), brush_);
            }
            brush_->SetColor(toD2D(pressed ? th.accent : 0x181C22));
            rt_->DrawRectangle(D2D1::RectF(kx, kbY + pressShift, kx + whiteW, kbY + kbH), brush_, pressed ? 2.4f : 1.1f);

            if (k == 0) {
                std::wstring cLbl = L"C" + std::to_wstring((note / 12) - 1);
                uint32_t lblCol = (!pressed && hasCol) ? 0xFFFFFF : 0x222222;
                drawText(cLbl.c_str(), kx, kbY + kbH - 22, whiteW, 16, lblCol, 8.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
            }
        }
    }

    for (int oct = 0; oct < 3; ++oct) {
        for (int k = 0; k < 7; ++k) {
            if (!kHasBlackAfter[k]) continue;
            int note = kBaseMidiNote + (oct + keyboardOctaveOffset_) * 12 + kWhiteOffsets[k] + 1;
            bool pressed = isNotePhysicallyDown(note);
            float kx = kbX + (oct * 7 + k + 1) * whiteW - 11.0f;
            float pressShift = pressed ? 4.0f : 0.0f;

            uint32_t keyCol = 0;
            bool hasCol = getNoteColor(note, keyCol);
            uint32_t fillRgb = 0x181C22;
            if (pressed) fillRgb = 0xFFFFFF;
            else if (hasCol) fillRgb = keyCol;

            brush_->SetColor(toD2D(fillRgb));
            rt_->FillRectangle(D2D1::RectF(kx, kbY + pressShift, kx + 22.0f, kbY + 58.0f), brush_);
            brush_->SetColor(toD2D(pressed ? th.accent : 0x000000));
            rt_->DrawRectangle(D2D1::RectF(kx, kbY + pressShift, kx + 22.0f, kbY + 58.0f), brush_, pressed ? 2.2f : 1.0f);
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
    drawText(L"LIBRARY & PRESET BROWSER   (CLICK PRESET .nm6 OR SAMPLE TO LOAD)", 76, 120, 520, 28, th.textPrimary, 11.5f, true);

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

int WreckageWinView::hitTestPianoKeyMapNote(float lx, float ly) const {
    float pmX = 26.0f, pmY = 184.0f, pmW = 376.0f, pmH = 372.0f;
    if (lx < pmX || lx > pmX + pmW || ly < pmY || ly > pmY + pmH) return -1;

    float wKeyW = pmW / 7.0f;
    float bKeyW = wKeyW * 0.64f;
    float bKeyH = pmH * 0.50f;
    static const int kBlackSemis[5] = { 1, 3, 6, 8, 10 };
    static const int kBlackAfterWhiteIdx[5] = { 0, 1, 3, 4, 5 };
    static const int kWhiteSemis[7] = { 0, 2, 4, 5, 7, 9, 11 };

    if (ly <= pmY + bKeyH) {
        for (int bk = 0; bk < 5; ++bk) {
            float kx = pmX + (kBlackAfterWhiteIdx[bk] + 1) * wKeyW - bKeyW * 0.5f;
            if (lx >= kx && lx <= kx + bKeyW) {
                return activeBank_ * 12 + kBlackSemis[bk];
            }
        }
    }
    int wIdx = std::clamp(static_cast<int>((lx - pmX) / wKeyW), 0, 6);
    return activeBank_ * 12 + kWhiteSemis[wIdx];
}

LRESULT CALLBACK WreckageWinView::editSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WreckageWinView* self = reinterpret_cast<WreckageWinView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    WNDPROC orig = self->origEditProc_;
    if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) { self->commitInlineRename(); return 0; }
        if (wParam == VK_ESCAPE) { self->cancelInlineRename(); return 0; }
    }
    if (msg == WM_CHAR && (wParam == L'\r' || wParam == 27)) return 0;
    return orig ? CallWindowProcW(orig, hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

void WreckageWinView::startInlineRename() {
    if (!hwnd_ || renameEdit_) return;
    renamingSlot_ = selectedSlot_;
    int px = static_cast<int>(std::round(36.0f * uiScale_));
    int py = static_cast<int>(std::round(440.0f * uiScale_));
    int pw = static_cast<int>(std::round(356.0f * uiScale_));
    int ph = static_cast<int>(std::round(30.0f * uiScale_));

    std::wstring cur = toWide(controller_->slotMirror(renamingSlot_).name);
    renameEdit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", cur.c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_CENTER,
        px, py, pw, ph, hwnd_, reinterpret_cast<HMENU>(9001), getPluginModuleHandle(), nullptr);
    if (renameEdit_) {
        SetWindowLongPtrW(renameEdit_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        origEditProc_ = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(renameEdit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WreckageWinView::editSubclassProc)));
        SendMessageW(renameEdit_, EM_SETLIMITTEXT, 20, 0);
        SendMessageW(renameEdit_, EM_SETSEL, 0, -1);
        SetFocus(renameEdit_);
    }
}

void WreckageWinView::commitInlineRename() {
    if (!renameEdit_) return;
    HWND ed = renameEdit_;
    renameEdit_ = nullptr;
    WNDPROC orig = origEditProc_;
    origEditProc_ = nullptr;
    if (orig) SetWindowLongPtrW(ed, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
    SetWindowLongPtrW(ed, GWLP_USERDATA, 0);

    wchar_t buf[64] = {0};
    GetWindowTextW(ed, buf, 64);
    std::string u8 = toUtf8(buf);
    int sl = renamingSlot_;
    renamingSlot_ = -1;
    if (!u8.empty() && sl >= 0) controller_->uiRenameSlot(sl, u8);
    DestroyWindow(ed);
    if (hwnd_) SetFocus(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WreckageWinView::cancelInlineRename() {
    if (!renameEdit_) return;
    HWND ed = renameEdit_;
    renameEdit_ = nullptr;
    WNDPROC orig = origEditProc_;
    origEditProc_ = nullptr;
    if (orig) SetWindowLongPtrW(ed, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
    SetWindowLongPtrW(ed, GWLP_USERDATA, 0);
    renamingSlot_ = -1;
    DestroyWindow(ed);
    if (hwnd_) SetFocus(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
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
        COMDLG_FILTERSPEC spec[] = { { L"WAV Audio", L"*.wav" }, { L"All Files", L"*.*" } };
        dlg->SetFileTypes(2, spec);
        if (SUCCEEDED(dlg->Show(hwnd_))) {
            IShellItemArray* arr = nullptr;
            if (SUCCEEDED(dlg->GetResults(&arr))) {
                DWORD count = 0;
                arr->GetCount(&count);
                int prevRRCount = controller_->slotMirror(selectedSlot_).rrCountInLayer(selectedVelLayer_);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(arr->GetItemAt(i, &item))) {
                        PWSTR psz = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz))) {
                            controller_->uiLoadWavFile(selectedSlot_, psz, selectedVelLayer_);
                            CoTaskMemFree(psz);
                        }
                        item->Release();
                    }
                }
                if (count > 0) {
                    selectedRR_ = std::clamp(prevRRCount, 0, kMaxRoundRobins - 1);
                    controller_->uiSelectSlot(selectedSlot_, selectedVelLayer_);
                }
                arr->Release();
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
        else if (entry.path().extension() == L".nm6")
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

    if (ly >= 636 && ly < 724 && lx >= 138 && lx <= 488) {
        controller_->uiSetParam(kParamPunishAmount, 0.0);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (activeTab_ == 0) {
        if (lx >= 440 && lx <= 808) {
            if (ly >= 424 && ly <= 468) controller_->uiSetSlotProp(selectedSlot_, 0, 0, 0.75);      // 0.0 dB
            else if (ly >= 472 && ly <= 516) controller_->uiSetSlotProp(selectedSlot_, 1, 0, 0.50); // 0.0 st
            else if (ly >= 520 && ly <= 564) controller_->uiSetSlotProp(selectedSlot_, 2, 0, 0.00); // 0 ms
            else if (ly >= 566 && ly <= 622) controller_->uiSetSlotProp(selectedSlot_, 3, 0, 1.00); // Full
        }
    } else if (activeTab_ == 1) {
        if (lx >= 32 && lx <= 808 && ly >= 156 && ly <= 366) {
            controller_->uiSetStageXY(selectedSlot_, 0.0f, 0.0f);
        } else if (ly >= 378 && ly <= 414) {
            if (lx >= 32 && lx <= 408) controller_->uiSetStageXY(selectedSlot_, 0.0f, controller_->slotMirror(selectedSlot_).stageY);
            else if (lx >= 432 && lx <= 808) controller_->uiSetStageXY(selectedSlot_, controller_->slotMirror(selectedSlot_).stageX, 0.0f);
        } else if (lx >= 36 && lx <= 722 && ly >= 426 && ly <= 612) {
            // Double-click any of the 4 Mic faders -> reset to 0.0 dB (norm = 0.75)
            int b = std::clamp(static_cast<int>((ly - 426.0f) / 44.0f), 0, 3);
            controller_->uiSetParam(kParamBusCloseGain + b, 0.75);
        }
    } else if (activeTab_ == 2) {
        if (activeFxModule_ == 1) {
            if (ly >= 446 && ly <= 486) {
                int col = std::clamp(static_cast<int>((lx - 44.0f) / 256.0f), 0, 2);
                static const ParamID gIds[3] = { kParamEqLowGain, kParamEqMidGain, kParamEqHighGain };
                controller_->uiSetParam(gIds[col], 0.50);
            }
        } else if (activeFxModule_ == 2) {
            if (ly >= 454 && ly <= 494 && lx >= 436 && lx <= 792) {
                controller_->uiSetParam(kParamCompMakeup, 0.50);
            }
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WreckageWinView::onMouseRightDown(float lx, float ly) {
    if (browserOpen_ || settingsOpen_) return;
    if (activeTab_ == 0) {
        int hitKey = hitTestPianoKeyMapNote(lx, ly);
        if (hitKey >= 0) {
            selectedSlot_ = hitKey;
            startInlineRename();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
}

void WreckageWinView::onMouseDown(float lx, float ly) {
    if (renameEdit_) commitInlineRename();

    if (settingsOpen_) {
        if (lx < 140 || lx > 700 || ly < 120 || ly > 610 || (lx > 635 && ly < 162)) {
            settingsOpen_ = false; InvalidateRect(hwnd_, nullptr, FALSE); return;
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

    if (browserOpen_) {
        if (lx < 60 || lx > 780 || ly < 110 || ly > 660 || (lx > 710 && ly < 155)) {
            browserOpen_ = false; InvalidateRect(hwnd_, nullptr, FALSE); return;
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
                    controller_->uiLoadWavFile(selectedSlot_, base.wstring(), selectedVelLayer_);
                    controller_->uiSelectSlot(selectedSlot_, selectedVelLayer_);
                    controller_->uiTriggerNote(controller_->slotMirror(selectedSlot_).midiNote, 0.9f, true);
                }
            }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (ly < 68) {
        if (lx >= 282 && lx <= 530) { rescanRootDirectory(); browserOpen_ = true; InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (lx >= 536 && lx <= 582) { openExportNm6Dialog(false); return; }
        if (lx >= 752 && lx <= 784) { controller_->uiPanic(); activePressedNote_ = -1; InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (lx >= 790 && lx <= 826) { settingsOpen_ = !settingsOpen_; InvalidateRect(hwnd_, nullptr, FALSE); return; }
        return;
    }

    if (ly >= 68 && ly < 106) {
        activeTab_ = std::clamp(static_cast<int>(lx / 280.0f), 0, 2);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (ly >= 636 && ly < 724) {
        if (lx >= 18 && lx <= 126) {
            bool on = controller_->getParamNormalized(kParamPunishOn) >= 0.5;
            controller_->uiSetParam(kParamPunishOn, on ? 0.0 : 1.0);
        } else if (lx >= 138 && lx <= 488) {
            SetCapture(hwnd_); dragTarget_ = 2;
            controller_->uiSetParam(kParamPunishAmount, std::clamp((lx - 138.0f) / 350.0f, 0.0f, 1.0f));
        } else if (lx >= 506 && lx <= 830) {
            int m = std::clamp(static_cast<int>((lx - 506.0f) / 108.0f), 0, 2);
            controller_->uiSetParam(kParamPunishMode, m * 0.5);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

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
        for (int s = 0; s < kTotalSlots; ++s) {
            if (controller_->slotMirror(s).midiNote == note) {
                selectedSlot_ = s;
                activeBank_ = s / 12;
                selectedRR_ = 0;
                controller_->uiSelectSlot(selectedSlot_, selectedVelLayer_);
                break;
            }
        }
        controller_->uiTriggerNote(note, 0.90f, true);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (activeTab_ == 0) {
        if (ly >= 150 && ly <= 176 && lx >= 26 && lx <= 400) {
            activeBank_ = std::clamp(static_cast<int>((lx - 26.0f) / 126.0f), 0, 2);
            selectedSlot_ = activeBank_ * 12 + (selectedSlot_ % 12);
            selectedRR_ = 0;
            controller_->uiSelectSlot(selectedSlot_, selectedVelLayer_);
        } else if (int hitKey = hitTestPianoKeyMapNote(lx, ly); hitKey >= 0) {
            selectedSlot_ = hitKey;
            selectedRR_ = 0;
            int note = controller_->slotMirror(selectedSlot_).midiNote;
            SetCapture(hwnd_);
            activePressedNote_ = note;
            controller_->uiSelectSlot(selectedSlot_, selectedVelLayer_);
            controller_->uiTriggerNote(note, 0.88f, true);
        } else if (ly >= 568 && ly <= 610) {
            if (lx >= 26 && lx <= 116) { openLoadWavDialog(); return; }
            else if (lx >= 122 && lx <= 212) { openExportNm6Dialog(true); return; }
            else if (lx >= 218 && lx <= 308) { controller_->uiClearSlot(selectedSlot_); selectedRR_ = 0; }
            else if (lx >= 314 && lx <= 402) { controller_->uiResetKit(); selectedRR_ = 0; }
        } else if (ly >= 123 && ly <= 145) {
            auto& sl = controller_->slotMirror(selectedSlot_);
            if (lx >= 468 && lx <= 568) {
                int v = std::clamp(static_cast<int>((lx - 468.0f) / 25.0f), 0, kMaxVelLayers - 1);
                selectedVelLayer_ = v;
                selectedRR_ = 0;
                controller_->uiSelectSlot(selectedSlot_, selectedVelLayer_);
            } else if (lx >= 594 && lx <= 765) {
                int r = std::clamp(static_cast<int>((lx - 594.0f) / 19.0f), 0, kMaxRoundRobins - 1);
                if (r < sl.rrCountInLayer(selectedVelLayer_)) {
                    selectedRR_ = r;
                    controller_->uiRequestWave(selectedSlot_, selectedVelLayer_, selectedRR_);
                }
            } else if (lx >= 768 && lx <= 812 && sl.rrCountInLayer(selectedVelLayer_) > 0) {
                controller_->uiDeleteWaveItem(selectedSlot_, selectedVelLayer_, selectedRR_);
                selectedRR_ = std::max(0, selectedRR_ - 1);
            }
        } else if (lx >= 440 && lx <= 812 && ly >= 151 && ly <= 310) {
            auto& sl = controller_->slotMirror(selectedSlot_);
            auto& rrs = sl.velLayers[selectedVelLayer_].rrs;
            if (selectedRR_ >= 0 && selectedRR_ < static_cast<int>(rrs.size())) {
                const auto& wi = rrs[static_cast<size_t>(selectedRR_)];
                float wx = 440.0f, ww = 372.0f;
                float xStart = wx + wi.startNorm * ww;
                float xEnd   = wx + wi.endNorm * ww;
                float regW   = std::max(4.0f, xEnd - xStart);
                float xFI    = xStart + wi.fadeInNorm * regW;
                float xFO    = xEnd - wi.fadeOutNorm * regW;

                SetCapture(hwnd_);
                if (ly <= 175.0f && std::fabs(lx - xFI) <= 12.0f) dragTarget_ = 52;
                else if (ly <= 175.0f && std::fabs(lx - xFO) <= 12.0f) dragTarget_ = 53;
                else if (std::fabs(lx - xStart) <= std::fabs(lx - xEnd)) dragTarget_ = 50;
                else dragTarget_ = 51;
                onMouseMove(lx, ly);
            }
        } else if (ly >= 320 && ly <= 340 && lx >= 496 && lx <= 808) {
            int c = std::clamp(static_cast<int>((lx - 496.0f) / 26.0f), 0, kNumKeyColors - 1);
            controller_->uiSetKeyColor(selectedSlot_, kStudioKeyColors[c]);
        } else if (ly >= 348 && ly <= 376 && lx >= 440 && lx <= 812) {
            int g = std::clamp(static_cast<int>((lx - 440.0f) / 126.0f), 0, 2);
            auto& sl = controller_->slotMirror(selectedSlot_);
            if (g == 0) controller_->uiSetSlotProp(selectedSlot_, 12, sl.fakeRR ? 0 : 1, 0.0);
            else if (g == 1) controller_->uiSetSlotProp(selectedSlot_, 13, sl.fakeVel ? 0 : 1, 0.0);
            else {
                bool cur = controller_->getParamNormalized(kParamGlobalFakeMic) >= 0.5;
                controller_->uiSetParam(kParamGlobalFakeMic, cur ? 0.0 : 1.0);
            }
        } else if (ly >= 384 && ly <= 412 && lx >= 440 && lx <= 812) {
            int d = std::clamp(static_cast<int>((lx - 440.0f) / 126.0f), 0, 2);
            auto& sl = controller_->slotMirror(selectedSlot_);
            if (d == 0) controller_->uiSetSlotProp(selectedSlot_, 5, sl.oneShot ? 0 : 1, 0.0);
            else if (d == 1) controller_->uiSetSlotProp(selectedSlot_, 6, (sl.chokeGroup + 1) % 5, 0.0);
            else controller_->uiSetSlotProp(selectedSlot_, 7, sl.reverse ? 0 : 1, 0.0);
        } else if (lx >= 436 && lx <= 814 && ly >= 424 && ly <= 622) {
            SetCapture(hwnd_);
            int fIdx = 0;
            if (ly < 470.0f) fIdx = 0;
            else if (ly < 518.0f) fIdx = 1;
            else if (ly < 566.0f) fIdx = 2;
            else fIdx = 3;
            dragTarget_ = 20 + fIdx;
            controller_->uiSetSlotProp(selectedSlot_, fIdx, 0, std::clamp((lx - 440.0f) / 368.0f, 0.0f, 1.0f));
        }
    } else if (activeTab_ == 1) {
        if (ly >= 122 && ly <= 148) {
            if (lx >= 300 && lx <= 428) {
                activeBank_ = std::clamp(static_cast<int>((lx - 300.0f) / 44.0f), 0, 2);
                selectedSlot_ = activeBank_ * 12 + (selectedSlot_ % 12);
                selectedRR_ = 0;
                controller_->uiSelectSlot(selectedSlot_, selectedVelLayer_);
            } else if (lx >= 440 && lx <= 566) stageGridSnap_ = !stageGridSnap_;
            else if (lx >= 574 && lx <= 706) followMidi_ = !followMidi_;
            else if (lx >= 714 && lx <= 810) controller_->uiResetStage();
        } else if (lx >= 32 && lx <= 808 && ly >= 156 && ly <= 366) {
            auto screenToStage = [&](float px, float py, float& sx, float& sy) {
                float ny = std::clamp((352.0f - py) / 180.0f, 0.0f, 1.0f);
                float halfW = 360.0f - ny * 130.0f;
                float nx = std::clamp((px - 420.0f) / halfW, -1.0f, 1.0f);
                if (stageGridSnap_) { nx = std::round(nx * 4.0f) / 4.0f; ny = std::round(ny * 4.0f) / 4.0f; }
                sx = nx; sy = ny;
            };
            auto stageToScreen = [](float sx, float sy, float& outX, float& outY) {
                float halfW = 360.0f - sy * 130.0f;
                outX = 420.0f + sx * halfW;
                outY = 352.0f - sy * 180.0f;
            };

            int hitSlot = -1;
            float bestDist2 = 24.0f * 24.0f;
            for (int i = 0; i < 12; ++i) {
                int sIdx = activeBank_ * 12 + i;
                const auto& sl = controller_->slotMirror(sIdx);
                if (!sl.hasAnySample()) continue;
                float px, py;
                stageToScreen(sl.stageX, sl.stageY, px, py);
                float d2 = (px - lx) * (px - lx) + (py - ly) * (py - ly);
                if (d2 <= bestDist2) { bestDist2 = d2; hitSlot = sIdx; }
            }
            if (hitSlot >= 0) {
                selectedSlot_ = hitSlot;
                int note = controller_->slotMirror(selectedSlot_).midiNote;
                activePressedNote_ = note;
                controller_->uiSelectSlot(selectedSlot_, selectedVelLayer_);
                controller_->uiTriggerNote(note, 0.85f, true);
                SetCapture(hwnd_); dragTarget_ = 1;
                float nx, ny; screenToStage(lx, ly, nx, ny);
                controller_->uiSetStageXY(selectedSlot_, nx, ny);
            } else if (controller_->slotMirror(selectedSlot_).hasAnySample()) {
                SetCapture(hwnd_); dragTarget_ = 1;
                float nx, ny; screenToStage(lx, ly, nx, ny);
                controller_->uiSetStageXY(selectedSlot_, nx, ny);
            }
        } else if (ly >= 378 && ly <= 414) {
            if (lx >= 32 && lx <= 408) {
                SetCapture(hwnd_); dragTarget_ = 60;
                float nx = std::clamp((lx - 32.0f) / 376.0f, 0.0f, 1.0f) * 2.0f - 1.0f;
                controller_->uiSetStageXY(selectedSlot_, nx, controller_->slotMirror(selectedSlot_).stageY);
            } else if (lx >= 432 && lx <= 808) {
                SetCapture(hwnd_); dragTarget_ = 61;
                float ny = std::clamp((lx - 432.0f) / 376.0f, 0.0f, 1.0f);
                controller_->uiSetStageXY(selectedSlot_, controller_->slotMirror(selectedSlot_).stageX, ny);
            }
        } else if (ly >= 426 && ly <= 612) {
            if (lx >= 36 && lx <= 722) {
                SetCapture(hwnd_);
                int b = std::clamp(static_cast<int>((ly - 426.0f) / 44.0f), 0, 3);
                dragTarget_ = 10 + b;
                controller_->uiSetParam(kParamBusCloseGain + b, std::clamp((lx - 36.0f) / 686.0f, 0.0f, 1.0f));
            } else if (lx >= 736 && lx <= 804) {
                int b = std::clamp(static_cast<int>((ly - 426.0f) / 44.0f), 0, 3);
                bool byp = controller_->getParamNormalized(kParamBusCloseBypass + b) >= 0.5;
                controller_->uiSetParam(kParamBusCloseBypass + b, byp ? 0.0 : 1.0);
            }
        }
    } else if (activeTab_ == 2) {
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
            if (ly >= 244 && ly <= 272 && lx >= 44 && lx <= 796) {
                int t = std::clamp(static_cast<int>((lx - 44.0f) / 94.0f), 0, 7);
                controller_->uiSetParam(kParamFilterType, t / 7.0);
            } else if (ly >= 394 && ly <= 436 && lx >= 44 && lx <= 792) {
                SetCapture(hwnd_); dragTarget_ = 70;
                controller_->uiSetParam(kParamFilterCutoff, std::clamp((lx - 44.0f) / 748.0f, 0.0f, 1.0f));
            } else if (ly >= 450 && ly <= 492 && lx >= 44 && lx <= 792) {
                SetCapture(hwnd_); dragTarget_ = 71;
                controller_->uiSetParam(kParamFilterRes, std::clamp((lx - 44.0f) / 748.0f, 0.0f, 1.0f));
            }
        } else if (activeFxModule_ == 1) {
            if (ly >= 244 && ly <= 366 && lx >= 44 && lx <= 792) {
                float lf = static_cast<float>(controller_->getParamNormalized(kParamEqLowFreq));
                float lg = static_cast<float>(controller_->getParamNormalized(kParamEqLowGain));
                float mf = static_cast<float>(controller_->getParamNormalized(kParamEqMidFreq));
                float mg = static_cast<float>(controller_->getParamNormalized(kParamEqMidGain));
                float hf = static_cast<float>(controller_->getParamNormalized(kParamEqHighFreq));
                float hg = static_cast<float>(controller_->getParamNormalized(kParamEqHighGain));
                float nx[3] = { 44.0f + (0.06f + lf * 0.24f) * 748.0f, 44.0f + (0.34f + mf * 0.32f) * 748.0f, 44.0f + (0.70f + hf * 0.24f) * 748.0f };
                float ny[3] = { 244.0f + (1.0f - lg) * 122.0f, 244.0f + (1.0f - mg) * 122.0f, 244.0f + (1.0f - hg) * 122.0f };
                int bestB = 0; float bestD2 = 1e18f;
                for (int b = 0; b < 3; ++b) {
                    float d2 = (lx - nx[b]) * (lx - nx[b]) + (ly - ny[b]) * (ly - ny[b]);
                    if (d2 < bestD2) { bestD2 = d2; bestB = b; }
                }
                SetCapture(hwnd_);
                dragTarget_ = 80 + bestB;
                onMouseMove(lx, ly);
            } else if (ly >= 388 && ly <= 424) {
                SetCapture(hwnd_);
                int col = std::clamp(static_cast<int>((lx - 44.0f) / 256.0f), 0, 2);
                dragTarget_ = 83 + col;
                onMouseMove(lx, ly);
            } else if (ly >= 446 && ly <= 482) {
                SetCapture(hwnd_);
                int col = std::clamp(static_cast<int>((lx - 44.0f) / 256.0f), 0, 2);
                dragTarget_ = 86 + col;
                onMouseMove(lx, ly);
            }
        } else if (activeFxModule_ == 2) {
            if (ly >= 346 && ly <= 492) {
                int row = std::clamp(static_cast<int>((ly - 346.0f) / 54.0f), 0, 2);
                int col = (lx < 420.0f) ? 0 : 1;
                SetCapture(hwnd_);
                dragTarget_ = 90 + row * 2 + col;
                onMouseMove(lx, ly);
            }
        } else if (activeFxModule_ == 3) {
            if (ly >= 340 && ly <= 492 && lx >= 44 && lx <= 792) {
                int row = std::clamp(static_cast<int>((ly - 340.0f) / 56.0f), 0, 2);
                SetCapture(hwnd_);
                dragTarget_ = 100 + row;
                onMouseMove(lx, ly);
            }
        } else if (activeFxModule_ == 4) {
            if (ly >= 220 && ly <= 244 && lx >= 580 && lx <= 792) {
                int dm = (lx < 686.0f) ? 0 : 1;
                controller_->uiSetParam(kParamDelayMode, dm ? 1.0 : 0.0);
            } else if (ly >= 342 && ly <= 492) {
                int row = std::clamp(static_cast<int>((ly - 342.0f) / 54.0f), 0, 2);
                int col = (lx < 420.0f) ? 0 : 1;
                SetCapture(hwnd_);
                dragTarget_ = (row == 2) ? 114 : (110 + row * 2 + col);
                onMouseMove(lx, ly);
            }
        } else if (activeFxModule_ == 5) {
            if (ly >= 220 && ly <= 244 && lx >= 520 && lx <= 792) {
                int rm = std::clamp(static_cast<int>((lx - 520.0f) / 90.0f), 0, 2);
                controller_->uiSetParam(kParamRevMode, rm * 0.5);
            } else if (ly >= 338 && ly <= 488) {
                int row = std::clamp(static_cast<int>((ly - 338.0f) / 54.0f), 0, 2);
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
        float ny = std::clamp((352.0f - ly) / 180.0f, 0.0f, 1.0f);
        float halfW = 360.0f - ny * 130.0f;
        float nx = std::clamp((lx - 420.0f) / halfW, -1.0f, 1.0f);
        if (stageGridSnap_) { nx = std::round(nx * 4.0f) / 4.0f; ny = std::round(ny * 4.0f) / 4.0f; }
        controller_->uiSetStageXY(selectedSlot_, nx, ny);
    } else if (dragTarget_ == 2) {
        controller_->uiSetParam(kParamPunishAmount, std::clamp((lx - 138.0f) / 350.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 10 && dragTarget_ <= 13) {
        controller_->uiSetParam(kParamBusCloseGain + (dragTarget_ - 10), std::clamp((lx - 36.0f) / 686.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 20 && dragTarget_ <= 23) {
        controller_->uiSetSlotProp(selectedSlot_, dragTarget_ - 20, 0, std::clamp((lx - 440.0f) / 368.0f, 0.0f, 1.0f));
    } else if (dragTarget_ >= 50 && dragTarget_ <= 53) {
        auto& sl = controller_->slotMirror(selectedSlot_);
        auto& rrs = sl.velLayers[selectedVelLayer_].rrs;
        if (selectedRR_ >= 0 && selectedRR_ < static_cast<int>(rrs.size())) {
            auto& wi = rrs[static_cast<size_t>(selectedRR_)];
            float wx = 440.0f, ww = 372.0f;
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
            }
            controller_->uiUpdateWaveEdit(selectedSlot_, selectedVelLayer_, selectedRR_);
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
        float gNorm = 1.0f - std::clamp((ly - 244.0f) / 122.0f, 0.0f, 1.0f);
        float xNorm = std::clamp((lx - 44.0f) / 748.0f, 0.0f, 1.0f);
        float fNorm = 0.5f;
        if (b == 0) fNorm = std::clamp((xNorm - 0.06f) / 0.24f, 0.0f, 1.0f);
        else if (b == 1) fNorm = std::clamp((xNorm - 0.34f) / 0.32f, 0.0f, 1.0f);
        else fNorm = std::clamp((xNorm - 0.70f) / 0.24f, 0.0f, 1.0f);
        static const ParamID fIds[3] = { kParamEqLowFreq, kParamEqMidFreq, kParamEqHighFreq };
        static const ParamID gIds[3] = { kParamEqLowGain, kParamEqMidGain, kParamEqHighGain };
        controller_->uiSetParam(fIds[b], fNorm);
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
            if (controller_) {
                controller_->decSaveFlash();
                controller_->tickNotePulses();

                uint32_t vSeq = controller_->liveVoiceSeq();
                if (vSeq != lastSeenVoiceSeq_) {
                    lastSeenVoiceSeq_ = vSeq;
                    int lvSlot = controller_->liveVoiceSlot();
                    int lvVel  = controller_->liveVoiceVel();
                    int lvRR   = controller_->liveVoiceRR();
                    if (followMidi_ && lvSlot >= 0 && lvSlot < kTotalSlots) {
                        if (selectedSlot_ != lvSlot) {
                            selectedSlot_ = lvSlot;
                            activeBank_ = lvSlot / 12;
                            controller_->uiSelectSlot(selectedSlot_, std::max(0, lvVel));
                        }
                        if (lvVel >= 0 && lvVel < kMaxVelLayers) selectedVelLayer_ = lvVel;
                        if (lvRR >= 0 && lvRR < kMaxRoundRobins) selectedRR_ = lvRR;
                    } else if (lvSlot == selectedSlot_) {
                        if (lvVel >= 0 && lvVel < kMaxVelLayers) selectedVelLayer_ = lvVel;
                        if (lvRR >= 0 && lvRR < kMaxRoundRobins) selectedRR_ = lvRR;
                    }
                } else if (followMidi_) {
                    uint32_t mSeq = controller_->liveMidiNoteSeq();
                    if (mSeq != lastSeenMidiSeq_) {
                        lastSeenMidiSeq_ = mSeq;
                        int mNote = controller_->liveLastMidiNote();
                        for (int s = 0; s < kTotalSlots; ++s) {
                            if (controller_->slotMirror(s).midiNote == mNote) {
                                if (selectedSlot_ != s) {
                                    selectedSlot_ = s;
                                    activeBank_ = s / 12;
                                    controller_->uiSelectSlot(selectedSlot_, selectedVelLayer_);
                                }
                                break;
                            }
                        }
                    }
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_RBUTTONDOWN:
            onMouseRightDown(GET_X_LPARAM(lParam) / uiScale_, GET_Y_LPARAM(lParam) / uiScale_);
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
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace NoiseMu6ix
#endif
