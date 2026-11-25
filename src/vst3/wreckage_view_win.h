#pragma once
#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include "public.sdk/source/common/pluginview.h"
#include "wreckage_controller.h"
#include <vector>
#include <string>

namespace NoiseMu6ix {

class WreckageWinView : public Steinberg::CPluginView {
public:
    explicit WreckageWinView(WreckageController* controller);
    ~WreckageWinView() override;

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canResize() SMTG_OVERRIDE { return Steinberg::kResultTrue; }

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void createDeviceResources();
    void discardDeviceResources();
    void paint();

    void drawHeader(const ThemePalette& th);
    void drawTabs(const ThemePalette& th);
    void drawSourceOrKitTab(const ThemePalette& th);
    void drawStageAndMixerTab(const ThemePalette& th);
    void drawMasterFxTab(const ThemePalette& th);
    void drawPunishAndKeyboard(const ThemePalette& th);
    void drawLibraryBrowserOverlay(const ThemePalette& th);

    void drawFader(float x, float y, float w, float h, float val01, const wchar_t* label, const ThemePalette& th, bool accentFill = false);
    void drawText(const wchar_t* text, float x, float y, float w, float h, uint32_t rgb, float sizePt, bool bold = false, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);
    D2D1_COLOR_F toD2D(uint32_t rgb, float alpha = 1.0f) const;

    void onMouseDown(float lx, float ly);
    void onMouseMove(float lx, float ly);
    void onMouseUp();
    void applyZoomIndex(int idx);
    void openRootFolderDialog();
    void openLoadWavDialog();
    void openExportNm6Dialog();
    void rescanRootDirectory();

    WreckageController* controller_ = nullptr;
    HWND hwnd_ = nullptr;
    ID2D1Factory* d2dFactory_ = nullptr;
    IDWriteFactory* dwFactory_ = nullptr;
    ID2D1HwndRenderTarget* rt_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;

    int activeTab_ = 1; // 0=SOURCE/KIT, 1=STAGE & MIXER, 2=MASTER FX
    int activeBank_ = 0;
    int selectedSlot_ = 0;
    int keyboardOctaveOffset_ = 0;
    bool browserOpen_ = false;
    int activePressedNote_ = -1;
    int dragTarget_ = 0; // 0=none, 1=stageNode, 2=punish, 10..14=5-bus, 20..24=slotFaders, 30..37=masterFx

    std::vector<std::wstring> browserCategories_;
    std::vector<std::wstring> browserItems_;
    int selectedCategory_ = 0;

    float uiScale_ = 1.0f;
};

} // namespace NoiseMu6ix
#endif
