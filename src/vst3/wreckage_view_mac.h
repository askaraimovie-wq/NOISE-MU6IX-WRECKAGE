#pragma once
#ifdef __APPLE__

#include "public.sdk/source/common/pluginview.h"
#include "wreckage_controller.h"
#include <string>
#include <vector>

namespace NoiseMu6ix {

class WreckageMacView : public Steinberg::CPluginView {
public:
    explicit WreckageMacView(WreckageController* controller);
    ~WreckageMacView() override;

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canResize() SMTG_OVERRIDE { return Steinberg::kResultTrue; }

    // Cocoa view callbacks
    void paintWithContext(void* cgCtx);
    void onMouseDown(float lx, float ly);
    void onMouseRightDown(float lx, float ly);
    void onMouseDoubleClick(float lx, float ly);
    void onMouseMove(float lx, float ly);
    void onMouseUp();
    void onMouseWheel(float lx, float deltaY);
    void onTimerTick();
    void commitInlineRename();
    void cancelInlineRename();
    float uiScale() const { return uiScale_; }
    void syncScaleFromBounds(float w, float h);

private:
    void requestRepaint();
    void drawHeader(void* ctx, const ThemePalette& th);
    void drawTabs(void* ctx, const ThemePalette& th);
    void drawSourceTab(void* ctx, const ThemePalette& th);
    void drawStageTab(void* ctx, const ThemePalette& th);
    void drawMasterFxTab(void* ctx, const ThemePalette& th);
    void drawPunishAndKeyboard(void* ctx, const ThemePalette& th);
    void drawLibraryBrowserOverlay(void* ctx, const ThemePalette& th);
    void drawSettingsOverlay(void* ctx, const ThemePalette& th);

    void drawFader(void* ctx, float x, float y, float w, float h, float val01,
                   const char* label, const char* valStr, const ThemePalette& th,
                   bool accentFill = false, bool enabled = true);
    void drawText(const char* text, float x, float y, float w, float h,
                  uint32_t rgb, float sizePt, bool bold = false, int align = 0);

    bool isNotePhysicallyDown(int midiNote) const;
    int hitTestPianoKeyMapNote(float lx, float ly) const;
    void startInlineRename();
    void applyZoomIndex(int idx);
    void openRootFolderDialog();
    void openLoadWavDialog();
    void openExportNm6Dialog(bool forceSaveAs);
    void rescanRootDirectory();

    WreckageController* controller_ = nullptr;
    void* nsViewRef_ = nullptr;
    void* timerRef_ = nullptr;
    void* renameFieldRef_ = nullptr;
    void* renameDelegateRef_ = nullptr;
    void* fontCacheRef_ = nullptr;
    int renamingSlot_ = -1;

    int activeTab_ = 1;          // Default 1 = STAGE & MIX
    int activeFxModule_ = 0;     // 0:FILTER, 1:EQ, 2:COMP, 3:SAT, 4:DELAY, 5:REVERB
    bool stageGridSnap_ = true;
    bool followMidi_ = true;
    int activeBank_ = 0;         // 0: C3-B3, 1: C4-B4, 2: C5-B5
    int selectedSlot_ = 0;
    int selectedVelLayer_ = 0;   // 0..3
    int selectedRR_ = 0;         // 0..8
    int keyboardOctaveOffset_ = 0;
    bool browserOpen_ = false;
    bool settingsOpen_ = false;
    int activePressedNote_ = -1;
    uint32_t lastSeenMidiSeq_ = 0;
    uint32_t lastSeenVoiceSeq_ = 0;
    uint32_t lastSeenWaveFocusSeq_ = 0;
    uint32_t lastSeenDirtySeq_ = 0;
    float lastDrawnOutLvl_ = -1.0f;
    float lastDrawnPlayhead_ = -2.0f;
    int lastDrawnVoices_ = -1;
    bool hadActivePulses_ = false;
    int dragTarget_ = 0;

    std::vector<std::string> browserCategories_;
    std::vector<std::string> browserItems_;
    int selectedCategory_ = 0;
    int browserCatScroll_ = 0;
    int browserItemScroll_ = 0;

    float uiScale_ = 1.0f;
};

} // namespace NoiseMu6ix
#endif
