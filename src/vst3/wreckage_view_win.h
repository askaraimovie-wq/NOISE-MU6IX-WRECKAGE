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
    static LRESULT CALLBACK editSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void createDeviceResources();
    void discardDeviceResources();
    void paint();

    void drawHeader(const ThemePalette& th);
    void drawTabs(const ThemePalette& th);
    void drawSourceTab(const ThemePalette& th);
    void drawStageTab(const ThemePalette& th);
    void drawMasterFxTab(const ThemePalette& th);
    void drawPunishAndKeyboard(const ThemePalette& th);
    void drawLibraryBrowserOverlay(const ThemePalette& th);
    void drawSettingsOverlay(const ThemePalette& th);

    void drawFader(float x, float y, float w, float h, float val01, const wchar_t* label, const wchar_t* valStr, const ThemePalette& th, bool accentFill = false, bool enabled = true);
    void drawText(const wchar_t* text, float x, float y, float w, float h, uint32_t rgb, float sizePt, bool bold = false, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);
    D2D1_COLOR_F toD2D(uint32_t rgb, float alpha = 1.0f) const;

    bool isNotePhysicallyDown(int midiNote) const;
    int hitTestPianoKeyMapNote(float lx, float ly) const;
    void onMouseDown(float lx, float ly);
    void onMouseRightDown(float lx, float ly);
    void onMouseDoubleClick(float lx, float ly);
    void onMouseMove(float lx, float ly);
    void onMouseUp();
    void startInlineRename();
    void commitInlineRename();
    void cancelInlineRename();

    void applyZoomIndex(int idx);
    void openRootFolderDialog();
    void openLoadWavDialog();
    void openExportNm6Dialog(bool forceSaveAs);
    void rescanRootDirectory();

    WreckageController* controller_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND renameEdit_ = nullptr;
    WNDPROC origEditProc_ = nullptr;
    int renamingSlot_ = -1;

    ID2D1Factory* d2dFactory_ = nullptr;
    IDWriteFactory* dwFactory_ = nullptr;
    ID2D1HwndRenderTarget* rt_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;

    int activeTab_ = 1;          // Default 1 = STAGE & MIX
    int activeFxModule_ = 0;     // 0:FILTER, 1:EQ, 2:COMP, 3:SAT, 4:DELAY, 5:REVERB
    bool stageGridSnap_ = true;
    bool followMidi_ = true;
    int activeBank_ = 0;         // 0: C3-B3, 1: C4-B4, 2: C5-B5
    int selectedSlot_ = 0;
    int selectedVelLayer_ = 0;   // 0..3 (Max 4 Velocity Layers)
    int selectedRR_ = 0;         // 0..8 (Max 9 Round-Robins per velocity layer)
    int keyboardOctaveOffset_ = 0;
    bool browserOpen_ = false;
    bool settingsOpen_ = false;
    int activePressedNote_ = -1;
    uint32_t lastSeenMidiSeq_ = 0;
    uint32_t lastSeenVoiceSeq_ = 0;
    int dragTarget_ = 0;

    std::vector<std::wstring> browserCategories_;
    std::vector<std::wstring> browserItems_;
    int selectedCategory_ = -1;
    int browserCatScroll_ = 0;
    int browserItemScroll_ = 0;

    float uiScale_ = 1.0f;
};

} // namespace NoiseMu6ix
#endif
