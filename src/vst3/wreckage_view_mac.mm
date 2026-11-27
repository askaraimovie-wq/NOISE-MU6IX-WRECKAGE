#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "wreckage_view_mac.h"
#include "wreckage_uids.h"
#include "version.h"
#include "wreckage_dsp.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace Steinberg;
using namespace Steinberg::Vst;

@interface NoiseMu6ixWreckageCocoaView_v111 : NSView {
    NoiseMu6ix::WreckageMacView* owner_;
}
- (instancetype)initWithFrame:(NSRect)frame owner:(NoiseMu6ix::WreckageMacView*)owner;
- (void)detachCppOwner;
- (void)onTimerFired:(NSTimer*)timer;
@end

@implementation NoiseMu6ixWreckageCocoaView_v111

- (instancetype)initWithFrame:(NSRect)frame owner:(NoiseMu6ix::WreckageMacView*)owner {
    self = [super initWithFrame:frame];
    if (self) {
        owner_ = owner;
    }
    return self;
}

- (void)detachCppOwner {
    owner_ = nullptr;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    if (owner_) {
        owner_->syncScaleFromBounds(static_cast<float>(newSize.width), static_cast<float>(newSize.height));
    }
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (!owner_) return;
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    if (!ctx) return;
    CGContextSaveGState(ctx);
    CGContextScaleCTM(ctx, owner_->uiScale(), owner_->uiScale());
    owner_->paintWithContext(ctx);
    CGContextRestoreGState(ctx);
}

- (void)onTimerFired:(NSTimer*)timer {
    (void)timer;
    if (owner_) owner_->onTimerTick();
}

- (void)mouseDown:(NSEvent*)event {
    if (!owner_) return;
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    float sc = std::max(0.1f, owner_->uiScale());
    float lx = static_cast<float>(pt.x) / sc;
    float ly = static_cast<float>(pt.y) / sc;
    if (event.clickCount >= 2) {
        owner_->onMouseDoubleClick(lx, ly);
    } else {
        owner_->onMouseDown(lx, ly);
    }
}

- (void)mouseDragged:(NSEvent*)event {
    if (!owner_) return;
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    float sc = std::max(0.1f, owner_->uiScale());
    owner_->onMouseMove(static_cast<float>(pt.x) / sc, static_cast<float>(pt.y) / sc);
}

- (void)mouseUp:(NSEvent*)event {
    (void)event;
    if (owner_) owner_->onMouseUp();
}

- (void)rightMouseDown:(NSEvent*)event {
    if (!owner_) return;
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    float sc = std::max(0.1f, owner_->uiScale());
    owner_->onMouseRightDown(static_cast<float>(pt.x) / sc, static_cast<float>(pt.y) / sc);
}

- (void)scrollWheel:(NSEvent*)event {
    if (!owner_) return;
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    float sc = std::max(0.1f, owner_->uiScale());
    owner_->onMouseWheel(static_cast<float>(pt.x) / sc, static_cast<float>(event.scrollingDeltaY));
}

@end

@interface NoiseMu6ixWreckageRenameDelegate_v111 : NSObject <NSTextFieldDelegate> {
    NoiseMu6ix::WreckageMacView* owner_;
}
- (instancetype)initWithOwner:(NoiseMu6ix::WreckageMacView*)owner;
- (void)detachCppOwner;
@end

@implementation NoiseMu6ixWreckageRenameDelegate_v111

- (instancetype)initWithOwner:(NoiseMu6ix::WreckageMacView*)owner {
    self = [super init];
    if (self) owner_ = owner;
    return self;
}

- (void)detachCppOwner { owner_ = nullptr; }

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)commandSelector {
    (void)control; (void)textView;
    if (!owner_) return NO;
    if (commandSelector == @selector(insertNewline:)) {
        owner_->commitInlineRename();
        return YES;
    }
    if (commandSelector == @selector(cancelOperation:)) {
        owner_->cancelInlineRename();
        return YES;
    }
    return NO;
}

- (void)controlTextDidEndEditing:(NSNotification*)obj {
    (void)obj;
    if (owner_) owner_->commitInlineRename();
}

@end

namespace NoiseMu6ix {

static std::wstring utf8ToWideMac(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        if (c < 0x80) { cp = c; i += 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < n) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < n) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < n) {
            cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                 ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
            i += 4;
        } else { cp = c; i += 1; }
        out.push_back(static_cast<wchar_t>(cp));
    }
    return out;
}

static std::string wideToUtf8Mac(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (wchar_t wc : w) {
        uint32_t cp = static_cast<uint32_t>(wc);
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }
    return out;
}

static std::string midiNoteNameMac(int note) {
    static const char* kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int n = std::clamp(note, 0, 127);
    int oct = (n / 12) - 1;
    return std::string(kNames[n % 12]) + std::to_string(oct);
}

static void formatDbLabelMac(char* dst, size_t cap, float db) {
    if (db <= -59.0f) std::snprintf(dst, cap, "-inf dB");
    else if (std::fabs(db) < 0.05f) std::snprintf(dst, cap, "0.0 dB");
    else std::snprintf(dst, cap, "%+.1f dB", db);
}

static void formatSignedValMac(char* dst, size_t cap, float val, const char* unit) {
    if (std::fabs(val) < 0.05f) std::snprintf(dst, cap, "0.0 %s", unit);
    else std::snprintf(dst, cap, "%+.1f %s", val, unit);
}

static void formatSignedPctMac(char* dst, size_t cap, int pct) {
    if (pct == 0) std::snprintf(dst, cap, "0%%");
    else std::snprintf(dst, cap, "%+d%%", pct);
}

static bool hasExtCIMac(const std::filesystem::path& p, const char* targetLower) {
    std::string ext = p.extension().u8string();
    for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return ext == targetLower;
}

static uint32_t contrastTextMac(uint32_t bgRgb) {
    int r = (bgRgb >> 16) & 0xFF;
    int g = (bgRgb >> 8)  & 0xFF;
    int b = bgRgb & 0xFF;
    int lum = (r * 299 + g * 587 + b * 114) / 1000;
    return (lum > 140) ? 0x181C22u : 0xFFFFFFu;
}

static inline void cgFillRect(CGContextRef ctx, float x, float y, float w, float h, uint32_t rgb, float alpha = 1.0f) {
    CGContextSetRGBFillColor(ctx, ((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0, alpha);
    CGContextFillRect(ctx, CGRectMake(x, y, w, h));
}

static inline void cgStrokeRect(CGContextRef ctx, float x, float y, float w, float h, uint32_t rgb, float lw = 1.2f, float alpha = 1.0f) {
    CGContextSetRGBStrokeColor(ctx, ((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0, alpha);
    CGContextStrokeRectWithWidth(ctx, CGRectMake(x, y, w, h), lw);
}

static inline void cgDrawLine(CGContextRef ctx, float x1, float y1, float x2, float y2, uint32_t rgb, float lw = 1.5f, float alpha = 1.0f) {
    CGContextSetRGBStrokeColor(ctx, ((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0, alpha);
    CGContextSetLineWidth(ctx, lw);
    CGContextBeginPath(ctx);
    CGContextMoveToPoint(ctx, x1, y1);
    CGContextAddLineToPoint(ctx, x2, y2);
    CGContextStrokePath(ctx);
}

static inline void cgFillEllipse(CGContextRef ctx, float cx, float cy, float rx, float ry, uint32_t rgb, float alpha = 1.0f) {
    CGContextSetRGBFillColor(ctx, ((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0, alpha);
    CGContextFillEllipseInRect(ctx, CGRectMake(cx - rx, cy - ry, rx * 2.0f, ry * 2.0f));
}

static inline void cgStrokeEllipse(CGContextRef ctx, float cx, float cy, float rx, float ry, uint32_t rgb, float lw = 1.5f, float alpha = 1.0f) {
    CGContextSetRGBStrokeColor(ctx, ((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0, alpha);
    CGContextSetLineWidth(ctx, lw);
    CGContextStrokeEllipseInRect(ctx, CGRectMake(cx - rx, cy - ry, rx * 2.0f, ry * 2.0f));
}

WreckageMacView::WreckageMacView(WreckageController* controller)
    : CPluginView(nullptr), controller_(controller) {
    int zIdx = 2;
    if (controller_) {
        zIdx = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamUiZoom) * 2.0)), 0, 2);
    }
    uiScale_ = (zIdx == 0) ? 0.50f : ((zIdx == 1) ? 0.75f : 1.00f);
    int px = static_cast<int>(std::round(kBaseUiSize * uiScale_));
    ViewRect r(0, 0, px, px);
    setRect(r);
    NSMutableDictionary* fc = [NSMutableDictionary dictionary];
    fontCacheRef_ = (__bridge_retained void*)fc;
}

WreckageMacView::~WreckageMacView() {
    removed();
    if (fontCacheRef_) {
        NSMutableDictionary* fc = (__bridge_transfer NSMutableDictionary*)fontCacheRef_;
        fontCacheRef_ = nullptr;
        [fc removeAllObjects];
    }
}

tresult PLUGIN_API WreckageMacView::isPlatformTypeSupported(FIDString type) {
    return (type && strcmp(type, kPlatformTypeNSView) == 0) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API WreckageMacView::attached(void* parent, FIDString type) {
    if (!parent || isPlatformTypeSupported(type) != kResultTrue) return kResultFalse;

    NSView* parentView = (__bridge NSView*)parent;
    int px = std::max(200, getRect().getWidth());
    NSRect parentBounds = [parentView bounds];
    if (parentBounds.size.width >= 200.0 && parentBounds.size.height >= 200.0) {
        px = static_cast<int>(std::round(std::min(parentBounds.size.width, parentBounds.size.height)));
    }
    uiScale_ = static_cast<float>(px) / static_cast<float>(kBaseUiSize);
    ViewRect r(0, 0, px, px);
    setRect(r);

    NoiseMu6ixWreckageCocoaView_v111* view =
        [[NoiseMu6ixWreckageCocoaView_v111 alloc] initWithFrame:NSMakeRect(0, 0, px, px) owner:this];
    [view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [parentView addSubview:view];
    nsViewRef_ = (__bridge_retained void*)view;

    NSTimer* timer = [NSTimer timerWithTimeInterval:0.030
                                             target:view
                                           selector:@selector(onTimerFired:)
                                           userInfo:nil
                                            repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
    timerRef_ = (__bridge_retained void*)timer;

    if (controller_) controller_->uiRequestFullSync();
    [view setNeedsDisplay:YES];
    return CPluginView::attached(parent, type);
}

tresult PLUGIN_API WreckageMacView::removed() {
    cancelInlineRename();
    if (timerRef_) {
        NSTimer* timer = (__bridge_transfer NSTimer*)timerRef_;
        timerRef_ = nullptr;
        [timer invalidate];
    }
    if (nsViewRef_) {
        NoiseMu6ixWreckageCocoaView_v111* view = (__bridge_transfer NoiseMu6ixWreckageCocoaView_v111*)nsViewRef_;
        nsViewRef_ = nullptr;
        [view detachCppOwner];
        [view removeFromSuperview];
    }
    return CPluginView::removed();
}

void WreckageMacView::syncScaleFromBounds(float w, float h) {
    float minDim = std::max(200.0f, std::min(w, h));
    uiScale_ = minDim / static_cast<float>(kBaseUiSize);
}

tresult PLUGIN_API WreckageMacView::onSize(ViewRect* newSize) {
    if (!newSize) return kInvalidArgument;
    CPluginView::onSize(newSize);
    int w = std::max(200, newSize->getWidth());
    int h = std::max(200, newSize->getHeight());
    uiScale_ = static_cast<float>(std::min(w, h)) / static_cast<float>(kBaseUiSize);
    if (nsViewRef_) {
        NoiseMu6ixWreckageCocoaView_v111* view = (__bridge NoiseMu6ixWreckageCocoaView_v111*)nsViewRef_;
        [view setFrame:NSMakeRect(0, 0, w, h)];
        [view setNeedsDisplay:YES];
    }
    return kResultOk;
}

void WreckageMacView::requestRepaint() {
    if (nsViewRef_) {
        NoiseMu6ixWreckageCocoaView_v111* view = (__bridge NoiseMu6ixWreckageCocoaView_v111*)nsViewRef_;
        [view setNeedsDisplay:YES];
    }
}

bool WreckageMacView::isNotePhysicallyDown(int midiNote) const {
    if (midiNote < 0 || midiNote >= 128) return false;
    if (activePressedNote_ >= 0 && midiNote == activePressedNote_) return true;
    return controller_ && (controller_->isMidiNoteLive(midiNote) || controller_->isNotePulsing(midiNote));
}

void WreckageMacView::drawText(const char* text, float x, float y, float w, float h,
                               uint32_t rgb, float sizePt, bool bold, int align) {
    if (!text || text[0] == '\0' || !fontCacheRef_) return;
    NSString* nsStr = [NSString stringWithUTF8String:text];
    if (!nsStr) return;

    NSMutableDictionary* cache = (__bridge NSMutableDictionary*)fontCacheRef_;
    float macPt = std::max(6.5f, sizePt * 0.88f);
    uint32_t szKey = static_cast<uint32_t>(std::round(macPt * 10.0f));
    NSNumber* fKey = @((szKey << 1) | (bold ? 1u : 0u));
    NSFont* font = cache[fKey];
    if (!font) {
        font = [NSFont systemFontOfSize:macPt weight:(bold ? NSFontWeightBold : NSFontWeightRegular)];
        cache[fKey] = font;
    }

    static NSParagraphStyle* sAlignStyles[3] = { nil, nil, nil };
    int aIdx = std::clamp(align, 0, 2);
    if (!sAlignStyles[aIdx]) {
        NSMutableParagraphStyle* ps = [[NSMutableParagraphStyle alloc] init];
        ps.alignment = (aIdx == 1) ? NSTextAlignmentCenter : ((aIdx == 2) ? NSTextAlignmentRight : NSTextAlignmentLeft);
        ps.lineBreakMode = NSLineBreakByTruncatingTail;
        sAlignStyles[aIdx] = [ps copy];
    }

    NSColor* color = [NSColor colorWithSRGBRed:((rgb >> 16) & 0xFF) / 255.0
                                         green:((rgb >> 8)  & 0xFF) / 255.0
                                          blue:(rgb & 0xFF)         / 255.0
                                         alpha:1.0];
    NSDictionary* attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: color,
        NSParagraphStyleAttributeName: sAlignStyles[aIdx]
    };

    NSRect bounds = [nsStr boundingRectWithSize:NSMakeSize(w, h)
                                        options:NSStringDrawingUsesLineFragmentOrigin
                                     attributes:attrs];
    float drawY = y + std::max(0.0f, (h - static_cast<float>(bounds.size.height)) * 0.5f);
    [nsStr drawInRect:NSMakeRect(x, drawY, w, std::min(h, static_cast<float>(bounds.size.height) + 2.0f))
       withAttributes:attrs];
}

void WreckageMacView::drawFader(void* ctxV, float x, float y, float w, float h, float val01,
                                const char* label, const char* valStr, const ThemePalette& th,
                                bool accentFill, bool enabled) {
    CGContextRef ctx = static_cast<CGContextRef>(ctxV);
    val01 = std::clamp(val01, 0.0f, 1.0f);
    drawText(label, x, y - 16.0f, w * 0.70f, 15.0f, enabled ? th.textPrimary : th.textMuted, 9.6f, true, 0);
    if (valStr && valStr[0] != 0) {
        drawText(valStr, x + w * 0.62f, y - 16.0f, w * 0.38f, 15.0f, enabled ? th.accent : th.textMuted, 9.8f, true, 2);
    }
    cgFillRect(ctx, x, y, w, h, th.faderTrack);
    cgFillRect(ctx, x, y, w * val01, h, enabled ? (accentFill ? th.accent : th.faderFill) : th.textMuted);
    cgStrokeRect(ctx, x, y, w, h, th.border, 1.3f);

    float tx = x + (w - 9.0f) * val01;
    cgFillRect(ctx, tx, y - 2.5f, 9.0f, h + 5.0f, th.panelBg);
    cgStrokeRect(ctx, tx, y - 2.5f, 9.0f, h + 5.0f, th.border, 1.5f);
}

void WreckageMacView::paintWithContext(void* cgCtx) {
    if (!cgCtx || !controller_) return;
    CGContextRef ctx = static_cast<CGContextRef>(cgCtx);

    int thIdx = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamThemeIndex) * 9.0)), 0, kNumThemes - 1);
    const ThemePalette& th = kThemes[thIdx];

    cgFillRect(ctx, 0, 0, kBaseUiSize, kBaseUiSize, th.bg);

    drawHeader(ctx, th);
    drawTabs(ctx, th);

    if (activeTab_ == 0) drawSourceTab(ctx, th);
    else if (activeTab_ == 1) drawStageTab(ctx, th);
    else drawMasterFxTab(ctx, th);

    drawPunishAndKeyboard(ctx, th);
    if (browserOpen_) drawLibraryBrowserOverlay(ctx, th);
    if (settingsOpen_) drawSettingsOverlay(ctx, th);

    cgStrokeRect(ctx, 1.0f, 1.0f, kBaseUiSize - 2.0f, kBaseUiSize - 2.0f, th.border, 2.0f);
}

void WreckageMacView::drawHeader(void* ctxV, const ThemePalette& th) {
    CGContextRef ctx = static_cast<CGContextRef>(ctxV);
    cgFillRect(ctx, 0, 0, 840, 68, th.headerBg);
    cgDrawLine(ctx, 0, 68, 840, 68, th.border, 1.6f);

    cgFillRect(ctx, 14, 14, 38, 40, th.border);
    cgStrokeRect(ctx, 14, 14, 38, 40, th.accent, 1.5f);
    drawText("N6", 14, 14, 38, 40, contrastTextMac(th.border), 16.0f, true, 1);

    drawText("WRECKAGE", 58, 12, 174, 44, th.textPrimary, 29.0f, true, 0);
    drawText("v" WRECKAGE_VERSION_STR, 222, 34, 46, 18, th.textPrimary, 9.5f, true, 0);

    cgFillRect(ctx, 272, 12, 258, 44, th.panelBg);
    cgStrokeRect(ctx, 272, 12, 258, 44, th.border, 1.5f);
    drawText(controller_->activePresetName().c_str(), 278, 12, 246, 44, th.textPrimary, 13.0f, true, 1);

    bool savedFlash = controller_->saveFlashTicks() > 0;
    cgFillRect(ctx, 536, 12, 46, 44, savedFlash ? 0x28CD41u : th.panelBg);
    cgStrokeRect(ctx, 536, 12, 46, 44, th.border, 1.5f);
    uint32_t icoCol = savedFlash ? 0xFFFFFFu : th.textPrimary;
    cgStrokeRect(ctx, 548, 22, 22, 24, icoCol, 1.8f);
    cgFillRect(ctx, 552, 22, 12, 8, icoCol);
    cgStrokeRect(ctx, 552, 35, 14, 11, icoCol, 1.4f);

    cgFillRect(ctx, 588, 12, 158, 44, th.panelBg);
    cgStrokeRect(ctx, 588, 12, 158, 44, th.border, 1.4f);

    int vox = controller_->liveVoices();
    double ramMb = static_cast<double>(controller_->liveRamBytes()) / (1024.0 * 1024.0);
    float cpu = controller_->liveCpuPct();
    char line1[64], line2[64];
    std::snprintf(line1, 64, "VOX:%d  RAM:%.1fM", vox, ramMb);
    std::snprintf(line2, 64, "CPU:%.1f%% | %s", cpu, vox > 0 ? "ACTIVE" : "IDLE");
    drawText(line1, 594, 13, 148, 18, th.textPrimary, 9.2f, true, 0);
    drawText(line2, 594, 31, 148, 18, vox > 0 ? th.accent : th.textMuted, 9.2f, true, 0);

    float pk = std::clamp(controller_->liveOutLevel(), 0.0f, 1.0f);
    cgFillRect(ctx, 590, 51, 154, 3.5f, th.faderTrack);
    if (pk > 0.002f) {
        uint32_t pkCol = (pk > 0.94f) ? 0xFF3B30u : ((pk > 0.72f) ? th.accent : 0x28CD41u);
        cgFillRect(ctx, 590, 51, 154.0f * pk, 3.5f, pkCol);
    }

    cgFillRect(ctx, 752, 12, 32, 44, th.panelBg);
    cgStrokeRect(ctx, 752, 12, 32, 44, th.border, 1.4f);
    drawText("!", 752, 12, 32, 44, th.accent, 16.0f, true, 1);

    cgFillRect(ctx, 790, 12, 36, 44, settingsOpen_ ? th.border : th.panelBg);
    cgStrokeRect(ctx, 790, 12, 36, 44, th.border, 1.4f);
    drawText("SET", 790, 12, 36, 44, settingsOpen_ ? contrastTextMac(th.border) : th.textPrimary, 10.5f, true, 1);
}

void WreckageMacView::drawTabs(void* ctxV, const ThemePalette& th) {
    CGContextRef ctx = static_cast<CGContextRef>(ctxV);
    const char* tabNames[3] = { "SOURCE", "STAGE & MIX", "MASTER FX" };
    for (int i = 0; i < 3; ++i) {
        float x = i * 280.0f;
        bool active = (i == activeTab_);
        cgFillRect(ctx, x, 68, 280, 38, active ? th.border : th.headerBg);
        cgStrokeRect(ctx, x, 68, 280, 38, th.border, 1.5f);
        drawText(tabNames[i], x, 68, 280, 38, active ? contrastTextMac(th.border) : th.textPrimary, 13.0f, true, 1);
    }
}

void WreckageMacView::drawSourceTab(void* ctxV, const ThemePalette& th) {
    CGContextRef ctx = static_cast<CGContextRef>(ctxV);
    cgFillRect(ctx, 16, 118, 396, 508, th.panelBg);
    cgStrokeRect(ctx, 16, 118, 396, 508, th.border, 1.6f);

    drawText("PIANO KEY MAP", 24, 124, 382, 22, th.textPrimary, 9.8f, true, 0);

    const char* banks[3] = { "BANK 1 (C3-B3)", "BANK 2 (C4-B4)", "BANK 3 (C5-B5)" };
    for (int b = 0; b < 3; ++b) {
        float bx = 26.0f + b * 126.0f;
        cgFillRect(ctx, bx, 150, 120, 26, b == activeBank_ ? th.border : th.headerBg);
        cgStrokeRect(ctx, bx, 150, 120, 26, th.border, 1.2f);
        drawText(banks[b], bx, 150, 120, 26, b == activeBank_ ? contrastTextMac(th.border) : th.textPrimary, 9.8f, true, 1);
    }

    float pmX = 26.0f, pmY = 184.0f, pmW = 376.0f, pmH = 372.0f;
    float wKeyW = pmW / 7.0f;
    static const int kWhiteSemis[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int kBlackSemis[5] = { 1, 3, 6, 8, 10 };
    static const int kBlackAfterWhiteIdx[5] = { 0, 1, 3, 4, 5 };

    for (int w = 0; w < 7; ++w) {
        int semi = kWhiteSemis[w];
        int sIdx = activeBank_ * 12 + semi;
        const auto& sl = controller_->slotMirror(sIdx);
        bool sel = (sIdx == selectedSlot_);
        bool hasSamp = sl.hasAnySample();
        float kx = pmX + w * wKeyW;

        uint32_t fillCol = (hasSamp && sl.hasCustomColor) ? sl.keyColor : (sel ? 0xF3E6DCu : 0xFFFFFFu);
        cgFillRect(ctx, kx, pmY, wKeyW, pmH, fillCol);
        cgStrokeRect(ctx, kx, pmY, wKeyW, pmH, sel ? th.accent : 0x181C22u, sel ? 2.6f : 1.2f);

        uint32_t txtCol = (hasSamp && sl.hasCustomColor) ? 0xFFFFFFu : 0x181C22u;
        std::string lbl = midiNoteNameMac(sl.midiNote) + "\n" + (hasSamp ? sl.name : "EMPTY");
        drawText(lbl.c_str(), kx + 2, pmY + pmH * 0.56f, wKeyW - 4, pmH * 0.40f, txtCol, 9.0f, true, 1);
    }

    float bKeyW = wKeyW * 0.64f;
    float bKeyH = pmH * 0.50f;
    for (int bk = 0; bk < 5; ++bk) {
        int semi = kBlackSemis[bk];
        int sIdx = activeBank_ * 12 + semi;
        const auto& sl = controller_->slotMirror(sIdx);
        bool sel = (sIdx == selectedSlot_);
        bool hasSamp = sl.hasAnySample();
        float kx = pmX + (kBlackAfterWhiteIdx[bk] + 1) * wKeyW - bKeyW * 0.5f;

        uint32_t fillCol = (hasSamp && sl.hasCustomColor) ? sl.keyColor : 0x181C22u;
        cgFillRect(ctx, kx, pmY, bKeyW, bKeyH, fillCol);
        cgStrokeRect(ctx, kx, pmY, bKeyW, bKeyH, sel ? th.accent : 0x000000u, sel ? 2.2f : 1.0f);
        if (sel) {
            cgFillEllipse(ctx, kx + bKeyW * 0.5f, pmY + bKeyH - 10.0f, 4.5f, 4.5f, th.accent);
        }
        std::string bLbl = midiNoteNameMac(sl.midiNote) + (hasSamp ? ("\n" + sl.name) : "");
        drawText(bLbl.c_str(), kx + 1, pmY + 6.0f, bKeyW - 2, bKeyH - 18.0f, 0xFFFFFFu, 7.8f, true, 1);
    }

    auto drawBottomBtn = [&](float x, float w, const char* label, bool primary) {
        cgFillRect(ctx, x, 568, w, 42, primary ? th.accent : th.headerBg);
        cgStrokeRect(ctx, x, 568, w, 42, th.border, 1.2f);
        drawText(label, x, 568, w, 42, primary ? contrastTextMac(th.accent) : th.textPrimary, 9.5f, true, 1);
    };
    drawBottomBtn(26,  90, "+ LOAD WAV", true);
    drawBottomBtn(122, 90, "SAVE AS",    false);
    drawBottomBtn(218, 90, "CLEAR KEY",  false);
    drawBottomBtn(314, 88, "RESET KIT",  false);

    auto& sl = controller_->slotMirror(selectedSlot_);
    cgFillRect(ctx, 428, 118, 396, 508, th.panelBg);
    cgStrokeRect(ctx, 428, 118, 396, 508, th.border, 1.6f);

    selectedVelLayer_ = std::clamp(selectedVelLayer_, 0, kMaxVelLayers - 1);
    int nRRs = sl.rrCountInLayer(selectedVelLayer_);
    if (selectedRR_ >= nRRs) selectedRR_ = std::max(0, nRRs - 1);

    drawText("VEL:", 438, 123, 30, 22, th.textPrimary, 9.2f, true, 0);
    for (int v = 0; v < kMaxVelLayers; ++v) {
        float vx = 468.0f + v * 25.0f;
        bool hasV = sl.rrCounts[v] > 0;
        bool selV = (v == selectedVelLayer_);
        cgFillRect(ctx, vx, 123, 22, 22, selV ? th.accent : (hasV ? th.panelBg : th.headerBg));
        cgStrokeRect(ctx, vx, 123, 22, 22, th.border, 1.1f);
        char vLbl[8];
        std::snprintf(vLbl, 8, "V%d", v + 1);
        drawText(vLbl, vx, 123, 22, 22, selV ? 0xFFFFFFu : th.textPrimary, 8.8f, true, 1);
    }

    drawText(nRRs >= kMaxRoundRobins ? "9/9" : "RR:", 570, 123, 24, 22, nRRs >= kMaxRoundRobins ? th.accent : th.textPrimary, 8.8f, true, 0);
    for (int r = 0; r < kMaxRoundRobins; ++r) {
        float rx = 594.0f + r * 19.0f;
        bool exists = (r < nRRs);
        bool selR = (exists && r == selectedRR_);
        cgFillRect(ctx, rx, 123, 17, 22, selR ? th.accent : (exists ? th.panelBg : th.headerBg));
        cgStrokeRect(ctx, rx, 123, 17, 22, th.border, 1.0f);
        std::string rLbl = std::to_string(r + 1);
        drawText(rLbl.c_str(), rx, 123, 17, 22, selR ? 0xFFFFFFu : (exists ? th.textPrimary : th.textMuted), 8.2f, true, 1);
    }

    if (nRRs > 0) {
        cgFillRect(ctx, 768, 123, 44, 22, th.headerBg);
        cgStrokeRect(ctx, 768, 123, 44, 22, th.border, 1.1f);
        drawText("DEL", 768, 123, 44, 22, th.accent, 8.5f, true, 1);
    }

    float wx = 440.0f, wy = 151.0f, ww = 372.0f, wh = 159.0f;
    cgFillRect(ctx, wx, wy, ww, wh, th.bg);
    cgStrokeRect(ctx, wx, wy, ww, wh, th.border, 1.3f);

    if (nRRs > 0 && selectedRR_ < static_cast<int>(sl.velLayers[selectedVelLayer_].rrs.size())) {
        const auto& wi = sl.velLayers[selectedVelLayer_].rrs[static_cast<size_t>(selectedRR_)];
        float midY = wy + wh * 0.5f;
        cgDrawLine(ctx, wx, midY, wx + ww, midY, th.textMuted, 1.0f, 0.3f);

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
            cgFillRect(ctx, bxPos, midY - hHalf, barW, hHalf * 2.0f,
                       inRegion ? th.textPrimary : th.textMuted, inRegion ? 0.90f : 0.25f);
        }

        if (xFadeIn > xStart + 2.0f) cgFillRect(ctx, xStart, wy, xFadeIn - xStart, wh, th.accent, 0.16f);
        if (xEnd - xFadeOut > 2.0f)  cgFillRect(ctx, xFadeOut, wy, xEnd - xFadeOut, wh, th.accent, 0.16f);

        cgDrawLine(ctx, xStart, wy, xStart, wy + wh, th.accent, 2.2f);
        cgDrawLine(ctx, xEnd, wy, xEnd, wy + wh, th.accent, 2.2f);
        cgDrawLine(ctx, xStart, wy + wh, xFadeIn, wy + 8.0f, th.accent, 1.8f);
        cgDrawLine(ctx, xFadeOut, wy + 8.0f, xEnd, wy + wh, th.accent, 1.8f);
        float fiBoxX = std::clamp(xFadeIn,  wx + 5.5f, wx + ww - 5.5f);
        float foBoxX = std::clamp(xFadeOut, wx + 5.5f, wx + ww - 5.5f);
        cgFillRect(ctx, fiBoxX - 4.5f, wy + 4.0f, 9.0f, 9.0f, th.accent);
        cgFillRect(ctx, foBoxX - 4.5f, wy + 4.0f, 9.0f, 9.0f, th.accent);

        float ph = controller_->livePlayheadNorm();
        if (ph >= 0.0f && ph <= 1.0f) {
            float phX = wx + ph * ww;
            cgDrawLine(ctx, phX, wy, phX, wy + wh, 0x28CD41u, 2.4f);
        }
    }

    drawText("COLOR:", 440, 320, 52, 20, th.textMuted, 9.5f, true, 0);
    for (int c = 0; c < kNumKeyColors; ++c) {
        float sx = 496.0f + c * 26.0f;
        cgFillRect(ctx, sx, 320, 21, 18, kStudioKeyColors[c]);
        cgStrokeRect(ctx, sx, 320, 21, 18, th.border, (sl.hasCustomColor && sl.keyColor == kStudioKeyColors[c]) ? 2.4f : 1.0f);
    }

    bool fMic = controller_->getParamNormalized(kParamGlobalFakeMic) >= 0.5;
    const char* gLabels[3] = {
        sl.fakeRR  ? "KEY FAKE RR: ON"  : "KEY FAKE RR: OFF",
        sl.fakeVel ? "KEY FAKE VEL: ON" : "KEY FAKE VEL: OFF",
        fMic       ? "GLOBAL MIC: ON"   : "GLOBAL MIC: OFF"
    };
    bool gStates[3] = { sl.fakeRR, sl.fakeVel, fMic };
    for (int g = 0; g < 3; ++g) {
        float px = 440.0f + g * 126.0f;
        cgFillRect(ctx, px, 348, 118, 28, gStates[g] ? th.accent : th.headerBg);
        cgStrokeRect(ctx, px, 348, 118, 28, th.border, 1.2f);
        drawText(gLabels[g], px, 348, 118, 28, gStates[g] ? 0xFFFFFFu : th.textPrimary, 9.0f, true, 1);
    }

    std::string m1 = sl.oneShot ? "MODE: ONE-SHOT" : "MODE: GATE";
    std::string m2 = sl.chokeGroup == 0 ? "CHOKE: OFF" : ("CHOKE: " + std::to_string(sl.chokeGroup));
    std::string m3 = sl.reverse ? "REVERSE: ON" : "REVERSE: OFF";
    const char* dModes[3] = { m1.c_str(), m2.c_str(), m3.c_str() };
    bool dStates[3] = { false, sl.chokeGroup > 0, sl.reverse };
    for (int d = 0; d < 3; ++d) {
        float dx = 440.0f + d * 126.0f;
        cgFillRect(ctx, dx, 384, 118, 28, dStates[d] ? th.border : th.headerBg);
        cgStrokeRect(ctx, dx, 384, 118, 28, th.border, 1.2f);
        drawText(dModes[d], dx, 384, 118, 28, dStates[d] ? contrastTextMac(th.border) : th.textPrimary, 9.5f, true, 1);
    }

    char vGain[32], vPitch[32], vAtt[32], vRel[32];
    formatDbLabelMac(vGain, 32, gainNormToDb(sl.gainNorm));
    formatSignedValMac(vPitch, 32, (sl.pitch - 0.5f) * 48.0f, "st");
    std::snprintf(vAtt, 32, "%.0f ms", sl.attack * 200.0f);
    std::snprintf(vRel, 32, "%.0f ms", releaseNormToMs(sl.release));

    drawFader(ctx, 440, 442, 368, 15, sl.gainNorm, "GAIN",            vGain,  th);
    drawFader(ctx, 440, 490, 368, 15, sl.pitch,    "PITCH",           vPitch, th);
    drawFader(ctx, 440, 538, 368, 15, sl.attack,   "ATTACK",          vAtt,   th);
    drawFader(ctx, 440, 586, 368, 15, sl.release,  "RELEASE / DECAY", vRel,   th);
}

void WreckageMacView::drawStageTab(void* ctxV, const ThemePalette& th) {
    CGContextRef ctx = static_cast<CGContextRef>(ctxV);
    cgFillRect(ctx, 16, 116, 808, 510, th.panelBg);
    cgStrokeRect(ctx, 16, 116, 808, 510, th.border, 1.6f);

    const auto& selSl = controller_->slotMirror(selectedSlot_);
    std::string stageTitle = midiNoteNameMac(selSl.midiNote) + " : " + selSl.name;
    drawText(stageTitle.c_str(), 28, 122, 264, 26, th.textPrimary, 12.0f, true, 0);

    const char* sBanks[3] = { "B1", "B2", "B3" };
    for (int b = 0; b < 3; ++b) {
        float sbx = 300.0f + b * 44.0f;
        bool bAct = (b == activeBank_);
        cgFillRect(ctx, sbx, 122, 40, 26, bAct ? th.border : th.headerBg);
        cgStrokeRect(ctx, sbx, 122, 40, 26, th.border, 1.2f);
        drawText(sBanks[b], sbx, 122, 40, 26, bAct ? contrastTextMac(th.border) : th.textPrimary, 9.5f, true, 1);
    }

    auto drawMiniBtn = [&](float x, float w, const char* txt, bool on) {
        cgFillRect(ctx, x, 122, w, 26, on ? th.accent : th.headerBg);
        cgStrokeRect(ctx, x, 122, w, 26, th.border, 1.2f);
        drawText(txt, x, 122, w, 26, on ? 0xFFFFFFu : th.textPrimary, 9.5f, true, 1);
    };
    drawMiniBtn(440, 126, stageGridSnap_ ? "GRID SNAP: ON" : "GRID SNAP: OFF", stageGridSnap_);
    drawMiniBtn(574, 132, followMidi_ ? "FOLLOW MIDI: ON" : "FOLLOW MIDI: OFF", followMidi_);
    drawMiniBtn(714, 96,  "CENTER ALL", false);

    cgFillRect(ctx, 32, 156, 776, 210, th.bg);
    cgStrokeRect(ctx, 32, 156, 776, 210, th.border, 1.4f);

    auto stageToScreen = [](float sx, float sy, float& outX, float& outY) {
        float ny = std::clamp(sy, 0.0f, 1.0f);
        float nx = std::clamp(sx, -1.0f, 1.0f);
        float halfW = 356.0f - ny * 124.0f;
        outX = 420.0f + nx * halfW;
        outY = 336.0f - ny * 156.0f;
    };

    for (int gy = 0; gy <= 4; ++gy) {
        float yNorm = gy / 4.0f;
        float xL, yL, xR, yR;
        stageToScreen(-1.0f, yNorm, xL, yL);
        stageToScreen( 1.0f, yNorm, xR, yR);
        cgDrawLine(ctx, xL, yL, xR, yR, th.textMuted, 1.15f, 0.62f);
        for (int gx = -4; gx <= 4; ++gx) {
            float px, py;
            stageToScreen(gx / 4.0f, yNorm, px, py);
            cgFillEllipse(ctx, px, py, 2.5f, 2.5f, th.textMuted, 0.62f);
        }
    }
    for (int gx = -4; gx <= 4; gx += 2) {
        float xF, yF, xB, yB;
        stageToScreen(gx / 4.0f, 0.0f, xF, yF);
        stageToScreen(gx / 4.0f, 1.0f, xB, yB);
        cgDrawLine(ctx, xF, yF, xB, yB, th.textMuted, 1.15f, 0.62f);
    }

    drawText("BACK",  32, 158, 776, 15, th.textMuted, 9.0f, true, 1);
    drawText("FRONT", 32, 349, 776, 15, th.textMuted, 9.0f, true, 1);

    auto drawOneStageDot = [&](int sIdx, int displayNum, bool isSelected) {
        const auto& sl = controller_->slotMirror(sIdx);
        if (!sl.hasAnySample()) return;
        float nx, ny;
        stageToScreen(sl.stageX, sl.stageY, nx, ny);
        float rad = isSelected ? 15.5f : 11.5f;
        cgFillEllipse(ctx, nx, ny, rad, rad, sl.hasCustomColor ? sl.keyColor : th.accent);
        cgStrokeEllipse(ctx, nx, ny, rad, rad, th.border, isSelected ? 2.3f : 1.2f);
        if (isSelected) {
            cgStrokeRect(ctx, nx - 20.0f, ny - 20.0f, 40.0f, 40.0f, th.accent, 1.8f);
        }
        std::string num = std::to_string(displayNum);
        drawText(num.c_str(), nx - 14.0f, ny - 8.0f, 28.0f, 16.0f, 0xFFFFFFu, 9.5f, true, 1);
    };

    for (int i = 0; i < 12; ++i) {
        int sIdx = activeBank_ * 12 + i;
        if (sIdx != selectedSlot_) drawOneStageDot(sIdx, i + 1, false);
    }
    if (selectedSlot_ >= activeBank_ * 12 && selectedSlot_ < (activeBank_ + 1) * 12) {
        drawOneStageDot(selectedSlot_, (selectedSlot_ % 12) + 1, true);
    }

    char vX[32], vY[32];
    formatSignedPctMac(vX, 32, static_cast<int>(std::round(selSl.stageX * 100.0f)));
    std::snprintf(vY, 32, "%d%%", static_cast<int>(std::round(selSl.stageY * 100.0f)));
    drawFader(ctx, 32,  392, 376, 13, (selSl.stageX + 1.0f) * 0.5f, "STAGE X (PAN L/R)", vX, th);
    drawFader(ctx, 432, 392, 376, 13, selSl.stageY,                 "STAGE Y (DEPTH)",   vY, th);

    const char* micNames[4] = { "CLOSE MIC", "ROOM MIC", "HALL MIC", "SUB MIC" };
    for (int b = 0; b < 4; ++b) {
        float y = 442.0f + b * 44.0f;
        float val = static_cast<float>(controller_->getParamNormalized(kParamBusCloseGain + b));
        bool byp = controller_->getParamNormalized(kParamBusCloseBypass + b) >= 0.5;
        char vMic[32];
        formatDbLabelMac(vMic, 32, gainNormToDb(val));
        drawFader(ctx, 32, y, 696, 14, val, micNames[b], vMic, th, false, !byp);

        cgFillRect(ctx, 738, y - 4, 70, 22, byp ? th.headerBg : th.accent);
        cgStrokeRect(ctx, 738, y - 4, 70, 22, th.border, 1.2f);
        drawText(byp ? "OFF" : "ON", 738, y - 4, 70, 22, byp ? th.textMuted : 0xFFFFFFu, 9.5f, true, 1);
    }
}

void WreckageMacView::drawMasterFxTab(void* ctxV, const ThemePalette& th) {
    CGContextRef ctx = static_cast<CGContextRef>(ctxV);
    cgFillRect(ctx, 16, 116, 808, 510, th.panelBg);
    cgStrokeRect(ctx, 16, 116, 808, 510, th.border, 1.6f);

    static const char* modNames[6] = { "FILTER", "EQ", "COMP", "SAT", "DELAY", "REVERB" };
    static const ParamID modOnIds[6] = { kParamFilterOn, kParamEqOn, kParamCompOn, kParamSatOn, kParamDelayOn, kParamRevOn };

    for (int m = 0; m < 6; ++m) {
        float bx = 28.0f + m * 131.0f;
        float by = 128.0f, bw = 123.0f, bh = 74.0f;
        bool selected = (m == activeFxModule_);
        bool isOn = controller_->getParamNormalized(modOnIds[m]) >= 0.5;

        cgFillRect(ctx, bx, by, bw, bh, selected ? th.border : th.bg);
        cgStrokeRect(ctx, bx, by, bw, bh, selected ? th.accent : th.border, selected ? 2.2f : 1.3f);

        float ledX = bx + bw - 16.0f, ledY = by + 14.0f;
        if (isOn) cgFillEllipse(ctx, ledX, ledY, 7.5f, 7.5f, th.accent);
        cgStrokeEllipse(ctx, ledX, ledY, 7.5f, 7.5f, isOn ? (selected ? th.panelBg : th.border) : th.textMuted, 1.6f);

        uint32_t fg = selected ? contrastTextMac(th.border) : th.textPrimary;
        float cx = bx + 42.0f, cy = by + 28.0f;
        if (m == 0) {
            cgDrawLine(ctx, cx - 18, cy - 6, cx + 2, cy - 6, fg, 2.0f);
            cgDrawLine(ctx, cx + 2, cy - 6, cx + 18, cy + 10, fg, 2.0f);
        } else if (m == 1) {
            cgDrawLine(ctx, cx - 20, cy + 4, cx - 6, cy - 8, fg, 2.0f);
            cgDrawLine(ctx, cx - 6, cy - 8, cx + 6, cy + 6, fg, 2.0f);
            cgDrawLine(ctx, cx + 6, cy + 6, cx + 20, cy - 2, fg, 2.0f);
        } else if (m == 2) {
            cgDrawLine(ctx, cx - 16, cy + 10, cx, cy - 2, fg, 2.0f);
            cgDrawLine(ctx, cx, cy - 2, cx + 18, cy - 6, fg, 2.0f);
        } else if (m == 3) {
            cgDrawLine(ctx, cx - 16, cy + 8, cx - 4, cy + 6, fg, 2.0f);
            cgDrawLine(ctx, cx - 4, cy + 6, cx + 4, cy - 6, fg, 2.0f);
            cgDrawLine(ctx, cx + 4, cy - 6, cx + 16, cy - 8, fg, 2.0f);
        } else if (m == 4) {
            cgDrawLine(ctx, cx - 12, cy - 8, cx - 12, cy + 8, fg, 2.2f);
            cgDrawLine(ctx, cx, cy - 4, cx, cy + 8, fg, 2.0f);
            cgDrawLine(ctx, cx + 12, cy, cx + 12, cy + 8, fg, 1.8f);
        } else {
            cgStrokeRect(ctx, cx - 14, cy - 6, 20, 16, fg, 1.6f);
            cgStrokeRect(ctx, cx - 6, cy - 11, 20, 16, fg, 1.6f);
        }
        drawText(modNames[m], bx, by + 44, bw, 24, fg, 11.0f, true, 1);
    }

    cgFillRect(ctx, 28, 214, 784, 398, th.bg);
    cgStrokeRect(ctx, 28, 214, 784, 398, th.border, 1.4f);

    bool modEnabled = controller_->getParamNormalized(modOnIds[activeFxModule_]) >= 0.5;
    float outLvl = controller_->liveOutLevel();
    char v1[32], v2[32], v3[32], v4[32], v5[32], v6[32];

    if (activeFxModule_ == 0) {
        drawText("FILTER", 44, 220, 320, 20, th.textPrimary, 11.5f, true, 0);
        static const char* fTypes[8] = { "LP 24dB", "LP 12dB", "HP 24dB", "HP 12dB", "BP 12dB", "BP 24dB", "PEAK", "NOTCH" };
        int ft = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamFilterType) * 7.0)), 0, 7);
        for (int t = 0; t < 8; ++t) {
            float tx = 44.0f + t * 94.0f;
            cgFillRect(ctx, tx, 244, 88, 28, t == ft ? th.accent : th.headerBg);
            cgStrokeRect(ctx, tx, 244, 88, 28, th.border, 1.2f);
            drawText(fTypes[t], tx, 244, 88, 28, t == ft ? 0xFFFFFFu : th.textPrimary, 9.5f, true, 1);
        }

        float cNorm = static_cast<float>(controller_->getParamNormalized(kParamFilterCutoff));
        float rNorm = static_cast<float>(controller_->getParamNormalized(kParamFilterRes));
        float cx = 44.0f, cy = 280.0f, cw = 748.0f, ch = 98.0f;
        cgFillRect(ctx, cx, cy, cw, ch, th.panelBg);
        cgStrokeRect(ctx, cx, cy, cw, ch, th.border, 1.2f);

        float midY = cy + ch * 0.45f;
        cgDrawLine(ctx, cx, midY, cx + cw, midY, th.textMuted, 1.0f, 0.35f);

        float cutX = cx + cNorm * cw;
        float resBump = rNorm * (ch * 0.34f);
        uint32_t lineCol = modEnabled ? th.accent : th.textMuted;
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
            if (s > 1) cgDrawLine(ctx, px0, py0, px, py, lineCol, 2.2f);
            px0 = px; py0 = py;
        }
        cgFillEllipse(ctx, cutX, std::clamp(midY - resBump, cy + 8.0f, cy + ch - 8.0f), 5.5f, 5.5f, lineCol);

        float hz = 20.0f * std::pow(1000.0f, cNorm);
        if (hz >= 1000.0f) std::snprintf(v1, 32, "%.2f kHz", hz / 1000.0f);
        else std::snprintf(v1, 32, "%.0f Hz", hz);
        std::snprintf(v2, 32, "%d%%", static_cast<int>(std::round(rNorm * 100.0f)));

        drawFader(ctx, 44, 412, 748, 16, cNorm, "CUTOFF FREQUENCY", v1, th, true, modEnabled);
        drawFader(ctx, 44, 468, 748, 16, rNorm, "RESONANCE",        v2, th, false, modEnabled);
    }
    else if (activeFxModule_ == 1) {
        drawText("3-BAND EQ", 44, 220, 320, 20, th.textPrimary, 11.5f, true, 0);
        float cx = 44.0f, cy = 244.0f, cw = 748.0f, ch = 122.0f;
        cgFillRect(ctx, cx, cy, cw, ch, th.panelBg);
        cgStrokeRect(ctx, cx, cy, cw, ch, th.border, 1.2f);

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
                cgFillRect(ctx, cx + sb * bw + 1.0f, cy + ch - barH, bw - 2.0f, barH, th.accent, 0.22f);
            }
        }

        cgDrawLine(ctx, cx, midY, cx + cw, midY, th.textMuted, 1.0f, 0.4f);

        uint32_t lineCol = modEnabled ? th.accent : th.textMuted;
        float prevX = cx, prevY = midY;
        for (int s = 1; s <= 64; ++s) {
            float px = cx + (s / 64.0f) * cw;
            float dy = 0.0f;
            for (int b = 0; b < 3; ++b) {
                float d = (px - nx[b]) / 68.0f;
                dy += (ny[b] - midY) * std::exp(-d * d);
            }
            float py = std::clamp(midY + dy, cy + 4.0f, cy + ch - 4.0f);
            cgDrawLine(ctx, prevX, prevY, px, py, lineCol, 2.2f);
            prevX = px; prevY = py;
        }
        for (int b = 0; b < 3; ++b) {
            cgFillEllipse(ctx, nx[b], ny[b], 7.5f, 7.5f, lineCol);
            cgStrokeEllipse(ctx, nx[b], ny[b], 7.5f, 7.5f, th.border, 1.5f);
        }

        std::snprintf(v1, 32, "%.0f Hz", 30.0f + lf * 270.0f);
        formatSignedValMac(v2, 32, (lg - 0.5f) * 24.0f, "dB");
        std::snprintf(v3, 32, "%.0f Hz", 250.0f + mf * 3750.0f);
        formatSignedValMac(v4, 32, (mg - 0.5f) * 24.0f, "dB");
        std::snprintf(v5, 32, "%.1f kHz", (2500.0f + hf * 13500.0f) / 1000.0f);
        formatSignedValMac(v6, 32, (hg - 0.5f) * 24.0f, "dB");

        drawFader(ctx, 44,  404, 236, 12, lf, "LOW FREQ",  v1, th, false, modEnabled);
        drawFader(ctx, 300, 404, 236, 12, mf, "MID FREQ",  v3, th, false, modEnabled);
        drawFader(ctx, 556, 404, 236, 12, hf, "HIGH FREQ", v5, th, false, modEnabled);

        drawFader(ctx, 44,  462, 236, 12, lg, "LOW GAIN",  v2, th, true,  modEnabled);
        drawFader(ctx, 300, 462, 236, 12, mg, "MID GAIN",  v4, th, true,  modEnabled);
        drawFader(ctx, 556, 462, 236, 12, hg, "HIGH GAIN", v6, th, true,  modEnabled);
    }
    else if (activeFxModule_ == 2) {
        float cTh = static_cast<float>(controller_->getParamNormalized(kParamCompThresh));
        float cRt = static_cast<float>(controller_->getParamNormalized(kParamCompRatio));
        float cAt = static_cast<float>(controller_->getParamNormalized(kParamCompAttack));
        float cRl = static_cast<float>(controller_->getParamNormalized(kParamCompRelease));
        float cKn = static_cast<float>(controller_->getParamNormalized(kParamCompKnee));
        float cMk = static_cast<float>(controller_->getParamNormalized(kParamCompMakeup));

        float gr = controller_->liveGrDb();
        char hdrGr[80];
        std::snprintf(hdrGr, 80, "COMPRESSOR    GR: -%.1f dB", gr);
        drawText(hdrGr, 44, 220, 748, 20, th.textPrimary, 11.0f, true, 0);

        float cx = 44.0f, cy = 242.0f, cw = 748.0f, ch = 84.0f;
        cgFillRect(ctx, cx, cy, cw, ch, th.panelBg);
        cgStrokeRect(ctx, cx, cy, cw, ch, th.border, 1.2f);

        float ratio = 1.0f + cRt * 11.0f;
        float threshX = cx + cTh * cw;
        cgDrawLine(ctx, threshX, cy, threshX, cy + ch, th.textMuted, 1.0f, 0.35f);

        uint32_t lineCol = modEnabled ? th.accent : th.textMuted;
        float p0x = cx, p0y = cy + ch - 8.0f;
        float p1x = threshX, p1y = p0y - cTh * (ch - 16.0f);
        float remW = std::max(10.0f, (cx + cw) - p1x);
        float p2y = std::max(cy + 8.0f, p1y - (remW / ratio));
        cgDrawLine(ctx, p0x, p0y, p1x, p1y, lineCol, 2.0f);
        cgDrawLine(ctx, p1x, p1y, cx + cw, p2y, lineCol, 2.2f);

        float grNorm = std::clamp(gr / 18.0f, 0.0f, 1.0f);
        float outNorm = std::clamp(outLvl, 0.0f, 1.0f);
        cgFillRect(ctx, cx + cw - 200.0f, cy + 12.0f, 184.0f, 14.0f, th.faderTrack);
        cgFillRect(ctx, cx + cw - 200.0f, cy + 44.0f, 184.0f, 14.0f, th.faderTrack);
        cgFillRect(ctx, cx + cw - 200.0f, cy + 12.0f, 184.0f * grNorm, 14.0f, th.accent);
        cgFillRect(ctx, cx + cw - 200.0f, cy + 44.0f, 184.0f * outNorm, 14.0f, 0x28CD41u);
        drawText("GAIN REDUCTION", cx + cw - 200.0f, cy + 27.0f, 184.0f, 14.0f, th.textMuted, 8.0f, true, 0);
        drawText("OUTPUT LEVEL",   cx + cw - 200.0f, cy + 59.0f, 184.0f, 14.0f, th.textMuted, 8.0f, true, 0);

        std::snprintf(v1, 32, "%.1f dB", -36.0f + cTh * 36.0f);
        std::snprintf(v2, 32, "%.1f : 1", ratio);
        std::snprintf(v3, 32, "%.1f ms", 0.5f + cAt * 79.5f);
        std::snprintf(v4, 32, "%.0f ms", 15.0f + cRl * 385.0f);
        std::snprintf(v5, 32, "%.1f dB", cKn * 12.0f);
        formatSignedValMac(v6, 32, (cMk - 0.5f) * 24.0f, "dB");

        drawFader(ctx, 44,  362, 356, 13, cTh, "THRESHOLD",       v1, th, true,  modEnabled);
        drawFader(ctx, 436, 362, 356, 13, cRt, "RATIO",           v2, th, false, modEnabled);
        drawFader(ctx, 44,  416, 356, 13, cAt, "ATTACK",          v3, th, false, modEnabled);
        drawFader(ctx, 436, 416, 356, 13, cRl, "RELEASE",         v4, th, false, modEnabled);
        drawFader(ctx, 44,  470, 356, 13, cKn, "KNEE / SHOULDER", v5, th, false, modEnabled);
        drawFader(ctx, 436, 470, 356, 13, cMk, "MAKEUP",          v6, th, true,  modEnabled);
    }
    else if (activeFxModule_ == 3) {
        int sMode = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamSatMode) * 3.0)), 0, 3);
        float sDr = static_cast<float>(controller_->getParamNormalized(kParamSatDrive));
        float sWm = static_cast<float>(controller_->getParamNormalized(kParamSatWarmth));
        float sMx = static_cast<float>(controller_->getParamNormalized(kParamSatMix));

        drawText("SATURATOR", 44, 220, 240, 24, th.textPrimary, 11.5f, true, 0);
        const char* sModes[4] = { "TAPE", "TUBE", "CRUSH", "CLIP" };
        for (int sm = 0; sm < 4; ++sm) {
            float sx = 456.0f + sm * 85.0f;
            cgFillRect(ctx, sx, 220, 79, 24, sm == sMode ? th.accent : th.headerBg);
            cgStrokeRect(ctx, sx, 220, 79, 24, th.border, 1.2f);
            drawText(sModes[sm], sx, 220, 79, 24, sm == sMode ? 0xFFFFFFu : th.textPrimary, 9.2f, true, 1);
        }
        float cx = 44.0f, cy = 248.0f, cw = 748.0f, ch = 74.0f;
        cgFillRect(ctx, cx, cy, cw, ch, th.panelBg);
        cgStrokeRect(ctx, cx, cy, cw, ch, th.border, 1.2f);

        float midY = cy + ch * 0.5f;
        float dx0 = cx + 16.0f, dy0 = midY;
        for (int i = 1; i <= 80; ++i) {
            float t = i / 80.0f;
            float px = cx + 16.0f + t * (cw - 32.0f);
            float py = midY - std::sin(t * 6.0f * kPi) * (ch * 0.34f);
            cgDrawLine(ctx, dx0, dy0, px, py, th.textMuted, 1.2f, 0.45f);
            dx0 = px; dy0 = py;
        }

        uint32_t lineCol = modEnabled ? th.accent : th.textMuted;
        float px0 = cx + 16.0f, py0 = midY;
        for (int i = 1; i <= 80; ++i) {
            float t = i / 80.0f;
            float px = cx + 16.0f + t * (cw - 32.0f);
            float baseW = std::sin(t * 6.0f * kPi);
            float satW = baseW;
            if (sMode == 0) {
                satW = std::tanh(baseW * (1.0f + sDr * 4.0f));
            } else if (sMode == 1) {
                float bias = 0.25f + sDr * 0.35f;
                satW = std::tanh(baseW * (1.0f + sDr * 5.5f) + bias) - std::tanh(bias) * 0.5f;
            } else if (sMode == 2) {
                float a = baseW * (1.0f + sDr * 10.0f);
                satW = (a / (1.0f + std::fabs(a) * 0.65f)) * 1.15f;
            } else {
                float a = std::clamp(baseW * (1.0f + sDr * 4.5f), -1.25f, 1.25f);
                satW = a - (a * a * a) / 6.75f;
            }
            float blended = baseW * (1.0f - sMx) + satW * sMx;
            float py = midY - std::clamp(blended, -1.08f, 1.08f) * (ch * 0.34f);
            cgDrawLine(ctx, px0, py0, px, py, lineCol, 2.2f);
            px0 = px; py0 = py;
        }

        std::snprintf(v1, 32, "%d%%", static_cast<int>(std::round(sDr * 100.0f)));
        formatSignedPctMac(v2, 32, static_cast<int>(std::round((sWm - 0.5f) * 200.0f)));
        std::snprintf(v3, 32, "%d%%", static_cast<int>(std::round(sMx * 100.0f)));

        drawFader(ctx, 44, 356, 748, 16, sDr, "DRIVE",       v1, th, true,  modEnabled);
        drawFader(ctx, 44, 412, 748, 16, sWm, "TONE (TILT)", v2, th, false, modEnabled);
        drawFader(ctx, 44, 468, 748, 16, sMx, "MIX",         v3, th, true,  modEnabled);
    }
    else if (activeFxModule_ == 4) {
        int dMode = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamDelayMode) * 2.0)), 0, 2);
        float dTm = static_cast<float>(controller_->getParamNormalized(kParamDelayTime));
        float dFb = static_cast<float>(controller_->getParamNormalized(kParamDelayFeedback));
        float dTn = static_cast<float>(controller_->getParamNormalized(kParamDelayTone));
        float dWd = static_cast<float>(controller_->getParamNormalized(kParamDelayWidth));
        float dMx = static_cast<float>(controller_->getParamNormalized(kParamDelayMix));

        float timeMs = 20.0f + dTm * 780.0f;
        int fbPct = static_cast<int>(std::round(dFb * 88.0f));
        int wdPct = static_cast<int>(std::round(dWd * 100.0f));
        drawText("DELAY", 44, 220, 220, 24, th.textPrimary, 11.5f, true, 0);

        const char* dModes[3] = { "STEREO", "PING-PONG", "TAPE" };
        for (int dm = 0; dm < 3; ++dm) {
            float dx = 496.0f + dm * 100.0f;
            cgFillRect(ctx, dx, 220, 94, 24, dm == dMode ? th.accent : th.headerBg);
            cgStrokeRect(ctx, dx, 220, 94, 24, th.border, 1.2f);
            drawText(dModes[dm], dx, 220, 94, 24, dm == dMode ? 0xFFFFFFu : th.textPrimary, 9.0f, true, 1);
        }

        float cx = 44.0f, cy = 248.0f, cw = 748.0f, ch = 76.0f;
        cgFillRect(ctx, cx, cy, cw, ch, th.panelBg);
        cgStrokeRect(ctx, cx, cy, cw, ch, th.border, 1.2f);

        float tapSpacing = 28.0f + dTm * 92.0f;
        float amp = 1.0f;
        if (dMode == 1) {
            float midY = cy + ch * 0.5f;
            cgDrawLine(ctx, cx + 8.0f, midY, cx + cw - 8.0f, midY, th.textMuted, 1.0f, 0.40f);
            drawText("L", cx + 6, cy + 4, 16, 16, th.textMuted, 8.5f, true, 0);
            drawText("R", cx + 6, cy + ch - 20, 16, 16, th.textMuted, 8.5f, true, 0);
            for (int tap = 0; tap < 10; ++tap) {
                float tx = cx + 28.0f + tap * tapSpacing;
                if (tx > cx + cw - 14.0f) break;
                float thgt = std::max(4.0f, amp * (ch * 0.42f));
                uint32_t col = modEnabled ? (tap == 0 ? th.textPrimary : th.accent) : th.textMuted;
                if ((tap & 1) == 0) cgFillRect(ctx, tx - 4.5f, midY - thgt, 9.0f, thgt, col);
                else cgFillRect(ctx, tx - 4.5f, midY, 9.0f, thgt, col);
                amp *= std::clamp(dFb + 0.14f, 0.05f, 0.92f);
            }
        } else {
            float baseY = cy + ch - 8.0f;
            for (int tap = 0; tap < 10; ++tap) {
                float tx = cx + 26.0f + tap * tapSpacing;
                if (tx > cx + cw - 14.0f) break;
                float thgt = std::max(4.0f, amp * (ch - 18.0f));
                if (dMode == 2 && tap > 0) {
                    cgFillRect(ctx, tx - 8.0f, baseY - thgt, 16.0f, thgt, modEnabled ? th.accent : th.textMuted, 0.25f);
                }
                cgFillRect(ctx, tx - 4.0f, baseY - thgt, 8.0f, thgt, modEnabled ? (tap == 0 ? th.textPrimary : th.accent) : th.textMuted);
                amp *= std::clamp(dFb + 0.14f, 0.05f, 0.92f);
            }
        }

        std::snprintf(v1, 32, "%.0f ms", timeMs);
        std::snprintf(v2, 32, "%d%%", fbPct);
        std::snprintf(v3, 32, "%d%%", static_cast<int>(std::round(dTn * 100.0f)));
        std::snprintf(v4, 32, "%d%%", wdPct);
        std::snprintf(v5, 32, "%d%%", static_cast<int>(std::round(dMx * 100.0f)));

        drawFader(ctx, 44,  358, 356, 13, dTm, "DELAY TIME", v1, th, true,  modEnabled);
        drawFader(ctx, 436, 358, 356, 13, dFb, "FEEDBACK",   v2, th, false, modEnabled);
        drawFader(ctx, 44,  412, 356, 13, dTn, "TONE",       v3, th, false, modEnabled);
        drawFader(ctx, 436, 412, 356, 13, dWd, "WIDTH",      v4, th, false, modEnabled);
        drawFader(ctx, 44,  468, 748, 13, dMx, "DELAY MIX",  v5, th, true,  modEnabled);
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
        drawText("REVERB", 44, 220, 220, 24, th.textPrimary, 11.5f, true, 0);

        const char* rModes[3] = { "ROOM", "PLATE", "HALL" };
        for (int rm = 0; rm < 3; ++rm) {
            float rx = 520.0f + rm * 90.0f;
            cgFillRect(ctx, rx, 220, 84, 24, rm == rMode ? th.accent : th.headerBg);
            cgStrokeRect(ctx, rx, 220, 84, 24, th.border, 1.2f);
            drawText(rModes[rm], rx, 220, 84, 24, rm == rMode ? 0xFFFFFFu : th.textPrimary, 9.5f, true, 1);
        }

        float cx = 44.0f, cy = 248.0f, cw = 748.0f, ch = 76.0f;
        cgFillRect(ctx, cx, cy, cw, ch, th.panelBg);
        cgStrokeRect(ctx, cx, cy, cw, ch, th.border, 1.2f);

        float boxW = 24.0f + rSz * 52.0f;
        float boxH = 22.0f + rSz * 38.0f;
        cgFillRect(ctx, cx + 14.0f, cy + (ch - boxH) * 0.5f, boxW, boxH, modEnabled ? th.accent : th.textMuted, 0.25f);
        cgStrokeRect(ctx, cx + 14.0f, cy + (ch - boxH) * 0.5f, boxW, boxH, th.border, 1.4f);

        float waveStartX = cx + 104.0f;
        float midY = cy + ch * 0.5f;
        float prePx = waveStartX + rPd * 110.0f;
        float tailSpan = std::max(80.0f, (0.25f + rDc * 0.75f) * (cw - (prePx - cx) - 24.0f));
        uint32_t lineCol = modEnabled ? th.accent : th.textMuted;
        cgDrawLine(ctx, waveStartX, midY, prePx, midY, lineCol, 1.5f);

        float prevX = prePx, prevTop = midY - (ch * 0.38f * rMx + 4.0f), prevBot = midY + (ch * 0.38f * rMx + 4.0f);
        for (int s = 1; s <= 48; ++s) {
            float t = s / 48.0f;
            float px = prePx + t * tailSpan;
            float env = std::exp(-t * (2.2f + (1.0f - rDc) * 3.5f)) * (0.35f + 0.65f * rMx);
            float hHalf = env * (ch * 0.42f);
            float topY = midY - hHalf, botY = midY + hHalf;
            cgDrawLine(ctx, prevX, prevTop, px, topY, lineCol, 2.0f);
            cgDrawLine(ctx, prevX, prevBot, px, botY, lineCol, 2.0f);
            prevX = px; prevTop = topY; prevBot = botY;
        }

        std::snprintf(v1, 32, "%.0f ms", pdMs);
        std::snprintf(v2, 32, "%.2f s",  decSec);
        std::snprintf(v3, 32, "%d%%",    static_cast<int>(std::round(rSz * 100.0f)));
        std::snprintf(v4, 32, "%.0f Hz", 20.0f + rLc * 480.0f);
        std::snprintf(v5, 32, "%.1f kHz",(1500.0f + rHc * 16500.0f) / 1000.0f);
        std::snprintf(v6, 32, "%d%%",    static_cast<int>(std::round(rMx * 100.0f)));

        drawFader(ctx, 44,  354, 356, 12, rPd, "PRE-DELAY",  v1, th, false, modEnabled);
        drawFader(ctx, 436, 354, 356, 12, rDc, "DECAY TIME", v2, th, true,  modEnabled);
        drawFader(ctx, 44,  408, 356, 12, rSz, "SIZE",       v3, th, false, modEnabled);
        drawFader(ctx, 436, 408, 356, 12, rLc, "LOW CUT",    v4, th, false, modEnabled);
        drawFader(ctx, 44,  462, 356, 12, rHc, "HIGH CUT",   v5, th, false, modEnabled);
        drawFader(ctx, 436, 462, 356, 12, rMx, "REVERB MIX", v6, th, true,  modEnabled);
    }

    bool mb  = controller_->getParamNormalized(kParamMonoBass) >= 0.5;
    bool lim = controller_->getParamNormalized(kParamLimiterOn) >= 0.5;
    cgFillRect(ctx, 44, 548, 356, 46, mb ? th.border : th.headerBg);
    cgStrokeRect(ctx, 44, 548, 356, 46, th.border, 1.3f);
    drawText(mb ? "MONO SUB BASS (<120 Hz): ON" : "MONO SUB BASS (<120 Hz): OFF",
             44, 548, 356, 46, mb ? contrastTextMac(th.border) : th.textPrimary, 10.5f, true, 1);

    cgFillRect(ctx, 436, 548, 356, 46, lim ? th.border : th.headerBg);
    cgStrokeRect(ctx, 436, 548, 356, 46, th.border, 1.3f);
    drawText(lim ? "MASTER SAFETY LIMITER: ON" : "MASTER SAFETY LIMITER: OFF",
             436, 548, 356, 46, lim ? contrastTextMac(th.border) : th.textPrimary, 10.5f, true, 1);
}

void WreckageMacView::drawPunishAndKeyboard(void* ctxV, const ThemePalette& th) {
    CGContextRef ctx = static_cast<CGContextRef>(ctxV);
    cgFillRect(ctx, 0, 636, 840, 88, th.punishBg);
    cgDrawLine(ctx, 0, 636, 840, 636, th.border, 1.8f);

    bool punOn = controller_->getParamNormalized(kParamPunishOn) >= 0.5;
    cgFillRect(ctx, 18, 656, 108, 48, punOn ? th.accent : 0x363D47u);
    cgStrokeRect(ctx, 18, 656, 108, 48, 0xFFFFFFu, 1.2f);
    drawText(punOn ? "PUNISH" : "PUNISH OFF", 18, 656, 108, 48, 0xFFFFFFu, punOn ? 18.0f : 12.0f, true, 1);

    float punVal = static_cast<float>(controller_->getParamNormalized(kParamPunishAmount));
    cgFillRect(ctx, 138, 668, 350, 24, 0x363D47u);
    cgFillRect(ctx, 138, 668, 350.0f * punVal, 24, punOn ? th.accent : 0x7A818Au);
    cgStrokeRect(ctx, 138, 668, 350, 24, 0xFFFFFFu, 1.2f);

    int pm = static_cast<int>(std::round(controller_->getParamNormalized(kParamPunishMode) * 2.0));
    const char* pModes[3] = { "GENTLY NOW", "HURT ME PLENTY", "NIGHTMARE" };
    for (int i = 0; i < 3; ++i) {
        float bx = 506.0f + i * 108.0f;
        cgFillRect(ctx, bx, 654, 102, 52, (i == pm && punOn) ? th.accent : 0x2E353Fu);
        cgStrokeRect(ctx, bx, 654, 102, 52, i == pm ? 0xFFFFFFu : 0x7A818Au, 1.3f);
        drawText(pModes[i], bx, 654, 102, 52, 0xFFFFFFu, 10.0f, true, 1);
    }

    cgFillRect(ctx, 0, 724, 840, 116, th.headerBg);

    static const int kWhiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const bool kHasBlackAfter[7] = { true, true, false, true, true, true, false };
    float kbX = 42.0f, whiteW = 36.0f, kbY = 732.0f, kbH = 98.0f;

    cgFillRect(ctx, 8, kbY, 28, kbH, th.panelBg);
    cgFillRect(ctx, 804, kbY, 28, kbH, th.panelBg);
    cgStrokeRect(ctx, 8, kbY, 28, kbH, th.border, 1.2f);
    cgStrokeRect(ctx, 804, kbY, 28, kbH, th.border, 1.2f);
    drawText("<", 8, kbY, 28, kbH, th.textPrimary, 14.0f, true, 1);
    drawText(">", 804, kbY, 28, kbH, th.textPrimary, 14.0f, true, 1);

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
            uint32_t fillRgb = 0xFFFFFFu;
            if (pressed) fillRgb = hasCol ? 0xFFFFFFu : 0xFFE2CFu;
            else if (hasCol) fillRgb = keyCol;

            cgFillRect(ctx, kx, kbY + pressShift, whiteW, kbH - pressShift, fillRgb);
            if (pressed) {
                cgFillRect(ctx, kx + 1, kbY + pressShift, whiteW - 2, kbH - pressShift - 1, th.accent, 0.35f);
            }
            cgStrokeRect(ctx, kx, kbY + pressShift, whiteW, kbH - pressShift, pressed ? th.accent : 0x181C22u, pressed ? 2.4f : 1.1f);

            if (k == 0) {
                std::string cLbl = "C" + std::to_string((note / 12) - 1);
                uint32_t lblCol = (!pressed && hasCol) ? 0xFFFFFFu : 0x222222u;
                drawText(cLbl.c_str(), kx, kbY + kbH - 22, whiteW, 16, lblCol, 8.5f, true, 1);
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
            uint32_t fillRgb = 0x181C22u;
            if (pressed) fillRgb = 0xFFFFFFu;
            else if (hasCol) fillRgb = keyCol;

            cgFillRect(ctx, kx, kbY + pressShift, 22.0f, 58.0f - pressShift, fillRgb);
            cgStrokeRect(ctx, kx, kbY + pressShift, 22.0f, 58.0f - pressShift, pressed ? th.accent : 0x000000u, pressed ? 2.2f : 1.0f);
        }
    }
}

void WreckageMacView::drawLibraryBrowserOverlay(void* ctxV, const ThemePalette& th) {
    CGContextRef ctx = static_cast<CGContextRef>(ctxV);
    cgFillRect(ctx, 0, 68, 840, 772, 0x000000u, 0.45f);
    cgFillRect(ctx, 60, 110, 720, 550, th.panelBg);
    cgStrokeRect(ctx, 60, 110, 720, 550, th.border, 2.0f);
    drawText("LIBRARY & PRESET BROWSER", 76, 120, 520, 28, th.textPrimary, 12.0f, true, 0);

    cgFillRect(ctx, 610, 120, 100, 28, th.accent);
    drawText("SET ROOT", 610, 120, 100, 28, 0xFFFFFFu, 10.0f, true, 1);
    drawText("[ X ]", 720, 120, 48, 28, th.accent, 13.0f, true, 1);

    int maxCatOff = std::max(0, static_cast<int>(browserCategories_.size()) - 14);
    browserCatScroll_ = std::clamp(browserCatScroll_, 0, maxCatOff);
    for (int row = 0; row < 14; ++row) {
        int i = browserCatScroll_ + row;
        if (i >= static_cast<int>(browserCategories_.size())) break;
        float y = 162.0f + row * 32.0f;
        bool sel = (i == selectedCategory_);
        cgFillRect(ctx, 76, y, 284, 28, sel ? th.border : th.bg);
        drawText(browserCategories_[static_cast<size_t>(i)].c_str(), 84, y, 270, 28,
                 sel ? contrastTextMac(th.border) : th.textPrimary, 11.0f, sel, 0);
    }
    int maxItemOff = std::max(0, static_cast<int>(browserItems_.size()) - 14);
    browserItemScroll_ = std::clamp(browserItemScroll_, 0, maxItemOff);
    for (int row = 0; row < 14; ++row) {
        int i = browserItemScroll_ + row;
        if (i >= static_cast<int>(browserItems_.size())) break;
        float y = 162.0f + row * 32.0f;
        cgFillRect(ctx, 380, y, 380, 28, th.bg);
        cgStrokeRect(ctx, 380, y, 380, 28, th.border, 1.0f);
        drawText(browserItems_[static_cast<size_t>(i)].c_str(), 388, y, 364, 28, th.textPrimary, 11.0f, false, 0);
    }
}

void WreckageMacView::drawSettingsOverlay(void* ctxV, const ThemePalette& th) {
    CGContextRef ctx = static_cast<CGContextRef>(ctxV);
    cgFillRect(ctx, 0, 68, 840, 772, 0x000000u, 0.45f);
    cgFillRect(ctx, 140, 120, 560, 490, th.panelBg);
    cgStrokeRect(ctx, 140, 120, 560, 490, th.border, 2.0f);

    drawText("WRECKAGE SETTINGS", 160, 130, 480, 28, th.textPrimary, 12.5f, true, 0);
    drawText("[ X ]", 644, 130, 44, 28, th.accent, 13.0f, true, 1);

    drawText("1. UI THEME", 160, 168, 520, 20, th.textMuted, 10.0f, true, 0);
    int curTh = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamThemeIndex) * 9.0)), 0, 9);
    for (int t = 0; t < kNumThemes; ++t) {
        int col = t % 2, row = t / 2;
        float bx = 160.0f + col * 264.0f;
        float by = 194.0f + row * 38.0f;
        bool active = (t == curTh);
        cgFillRect(ctx, bx, by, 252, 32, active ? th.border : th.bg);
        cgStrokeRect(ctx, bx, by, 252, 32, th.border, 1.2f);
        drawText(kThemes[t].name, bx + 10, by, 232, 32, active ? contrastTextMac(th.border) : th.textPrimary, 10.5f, true, 0);
    }

    drawText("2. WINDOW ZOOM", 160, 396, 520, 20, th.textMuted, 10.0f, true, 0);
    int zIdx = std::clamp(static_cast<int>(std::round(controller_->getParamNormalized(kParamUiZoom) * 2.0)), 0, 2);
    const char* zLabels[3] = { "50% (COMPACT)", "75% (LAPTOP)", "100% (FULL)" };
    for (int z = 0; z < 3; ++z) {
        float bx = 160.0f + z * 176.0f;
        cgFillRect(ctx, bx, 422, 168, 34, z == zIdx ? th.border : th.bg);
        cgStrokeRect(ctx, bx, 422, 168, 34, th.border, 1.2f);
        drawText(zLabels[z], bx, 422, 168, 34, z == zIdx ? contrastTextMac(th.border) : th.textPrimary, 10.5f, true, 1);
    }

    drawText("3. VELOCITY CURVE & LIBRARY", 160, 474, 520, 20, th.textMuted, 10.0f, true, 0);
    int vc = static_cast<int>(std::round(controller_->getParamNormalized(kParamVelCurve) * 2.0));
    const char* vcLabels[3] = { "SOFT CURVE", "LINEAR CURVE", "HARD CURVE" };
    for (int v = 0; v < 3; ++v) {
        float bx = 160.0f + v * 176.0f;
        cgFillRect(ctx, bx, 500, 168, 34, v == vc ? th.accent : th.bg);
        cgStrokeRect(ctx, bx, 500, 168, 34, th.border, 1.2f);
        drawText(vcLabels[v], bx, 500, 168, 34, v == vc ? 0xFFFFFFu : th.textPrimary, 10.5f, true, 1);
    }

    cgFillRect(ctx, 160, 550, 520, 40, th.border);
    std::string rootU8 = wideToUtf8Mac(controller_->rootLibraryDir());
    std::string rootBtn = rootU8.empty() ? "SET ROOT SAMPLE LIBRARY FOLDER..." : ("ROOT FOLDER: " + rootU8);
    drawText(rootBtn.c_str(), 168, 550, 504, 40, contrastTextMac(th.border), 10.5f, true, 1);
}

int WreckageMacView::hitTestPianoKeyMapNote(float lx, float ly) const {
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
            if (lx >= kx && lx <= kx + bKeyW) return activeBank_ * 12 + kBlackSemis[bk];
        }
    }
    int wIdx = std::clamp(static_cast<int>((lx - pmX) / wKeyW), 0, 6);
    return activeBank_ * 12 + kWhiteSemis[wIdx];
}

void WreckageMacView::startInlineRename() {
    if (!nsViewRef_ || renameFieldRef_) return;
    renamingSlot_ = selectedSlot_;
    NoiseMu6ixWreckageCocoaView_v111* view = (__bridge NoiseMu6ixWreckageCocoaView_v111*)nsViewRef_;

    NSRect frame = NSMakeRect(36.0 * uiScale_, 440.0 * uiScale_, 356.0 * uiScale_, 30.0 * uiScale_);
    NSTextField* tf = [[NSTextField alloc] initWithFrame:frame];
    tf.alignment = NSTextAlignmentCenter;
    tf.bezeled = YES;
    tf.editable = YES;
    tf.selectable = YES;
    NSString* cur = [NSString stringWithUTF8String:controller_->slotMirror(renamingSlot_).name.c_str()];
    if (cur) tf.stringValue = cur;

    NoiseMu6ixWreckageRenameDelegate_v111* del = [[NoiseMu6ixWreckageRenameDelegate_v111 alloc] initWithOwner:this];
    tf.delegate = del;
    [view addSubview:tf];
    renameFieldRef_ = (__bridge_retained void*)tf;
    renameDelegateRef_ = (__bridge_retained void*)del;

    if (view.window) {
        [view.window makeFirstResponder:tf];
    }
}

void WreckageMacView::commitInlineRename() {
    if (!renameFieldRef_) return;
    NSTextField* tf = (__bridge_transfer NSTextField*)renameFieldRef_;
    renameFieldRef_ = nullptr;
    if (renameDelegateRef_) {
        NoiseMu6ixWreckageRenameDelegate_v111* del = (__bridge_transfer NoiseMu6ixWreckageRenameDelegate_v111*)renameDelegateRef_;
        renameDelegateRef_ = nullptr;
        [del detachCppOwner];
        tf.delegate = nil;
    }

    NSString* txt = tf.stringValue;
    int sl = renamingSlot_;
    renamingSlot_ = -1;
    [tf removeFromSuperview];

    if (txt && txt.length > 0 && sl >= 0) {
        std::string u8 = txt.UTF8String;
        if (u8.size() > 20) u8.resize(20);
        if (!u8.empty()) controller_->uiRenameSlot(sl, u8);
    }
    requestRepaint();
}

void WreckageMacView::cancelInlineRename() {
    if (!renameFieldRef_) return;
    NSTextField* tf = (__bridge_transfer NSTextField*)renameFieldRef_;
    renameFieldRef_ = nullptr;
    if (renameDelegateRef_) {
        NoiseMu6ixWreckageRenameDelegate_v111* del = (__bridge_transfer NoiseMu6ixWreckageRenameDelegate_v111*)renameDelegateRef_;
        renameDelegateRef_ = nullptr;
        [del detachCppOwner];
        tf.delegate = nil;
    }
    renamingSlot_ = -1;
    [tf removeFromSuperview];
    requestRepaint();
}

void WreckageMacView::applyZoomIndex(int idx) {
    idx = std::clamp(idx, 0, 2);
    controller_->uiSetParam(kParamUiZoom, idx * 0.5);
    uiScale_ = (idx == 0) ? 0.50f : ((idx == 1) ? 0.75f : 1.00f);
    int px = static_cast<int>(std::round(kBaseUiSize * uiScale_));
    ViewRect newRect(0, 0, px, px);
    if (plugFrame) plugFrame->resizeView(this, &newRect);
    else onSize(&newRect);
}

void WreckageMacView::openRootFolderDialog() {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = NO;
    panel.allowsMultipleSelection = NO;
    if ([panel runModal] == NSModalResponseOK && panel.URL) {
        const char* p = panel.URL.path.UTF8String;
        if (p && p[0]) {
            controller_->rootLibraryDir() = utf8ToWideMac(p);
            controller_->saveGlobalConfig();
            selectedCategory_ = 0;
            browserCatScroll_ = 0;
            browserItemScroll_ = 0;
            rescanRootDirectory();
        }
    }
    requestRepaint();
}

void WreckageMacView::openLoadWavDialog() {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = YES;
    UTType* wavType = [UTType typeWithFilenameExtension:@"wav"];
    if (wavType) panel.allowedContentTypes = @[wavType];

    if ([panel runModal] == NSModalResponseOK && panel.URLs.count > 0) {
        int prevRRCount = controller_->slotMirror(selectedSlot_).rrCountInLayer(selectedVelLayer_);
        for (NSURL* url in panel.URLs) {
            const char* p = url.path.UTF8String;
            if (p && p[0]) {
                controller_->uiLoadWavFile(selectedSlot_, utf8ToWideMac(p), selectedVelLayer_);
            }
        }
        selectedRR_ = std::clamp(prevRRCount, 0, kMaxRoundRobins - 1);
        controller_->uiSelectSlot(selectedSlot_, selectedVelLayer_);
    }
    requestRepaint();
}

void WreckageMacView::openExportNm6Dialog(bool forceSaveAs) {
    if (!forceSaveAs && !controller_->lastSavedNm6Path().empty()) {
        controller_->uiExportNm6File(controller_->lastSavedNm6Path());
        requestRepaint();
        return;
    }
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = @"MyKit.nm6";
    panel.allowsOtherFileTypes = YES;

    if ([panel runModal] == NSModalResponseOK && panel.URL) {
        const char* p = panel.URL.path.UTF8String;
        if (p && p[0]) {
            std::string u8(p);
            std::filesystem::path fp = std::filesystem::u8path(u8);
            if (!hasExtCIMac(fp, ".nm6")) {
                u8 += ".nm6";
                fp = std::filesystem::u8path(u8);
            }
            controller_->uiExportNm6File(utf8ToWideMac(u8));
            std::string stem = fp.stem().u8string();
            if (!stem.empty()) controller_->activePresetName() = stem;
        }
    }
    requestRepaint();
}

void WreckageMacView::rescanRootDirectory() {
    browserCategories_.clear();
    browserItems_.clear();
    std::string rootU8 = wideToUtf8Mac(controller_->rootLibraryDir());
    if (rootU8.empty()) return;

    std::error_code ec;
    std::filesystem::path rootPath = std::filesystem::u8path(rootU8);
    std::vector<std::string> subDirs;
    for (const auto& entry : std::filesystem::directory_iterator(rootPath, ec)) {
        if (entry.is_directory()) subDirs.push_back(entry.path().filename().u8string());
    }
    std::sort(subDirs.begin(), subDirs.end());
    browserCategories_.push_back("[ ROOT ]");
    for (auto& d : subDirs) browserCategories_.push_back(std::move(d));

    if (selectedCategory_ < 0 || selectedCategory_ >= static_cast<int>(browserCategories_.size())) {
        selectedCategory_ = 0;
    }

    std::filesystem::path scanDir = rootPath;
    if (selectedCategory_ > 0) {
        scanDir /= std::filesystem::u8path(browserCategories_[static_cast<size_t>(selectedCategory_)]);
    }
    for (const auto& entry : std::filesystem::directory_iterator(scanDir, ec)) {
        if (!entry.is_directory() && (hasExtCIMac(entry.path(), ".nm6") || hasExtCIMac(entry.path(), ".wav"))) {
            browserItems_.push_back(entry.path().filename().u8string());
        }
    }
    std::sort(browserItems_.begin(), browserItems_.end());
}

void WreckageMacView::onMouseDoubleClick(float lx, float ly) {
    if (browserOpen_ || settingsOpen_) return;
    dragTarget_ = 0;

    if (ly >= 636 && ly < 724 && lx >= 138 && lx <= 488) {
        controller_->uiSetParam(kParamPunishAmount, 0.0);
        controller_->uiSetParam(kParamPunishOn, 0.0);
        requestRepaint();
        return;
    }

    if (activeTab_ == 0) {
        if (lx >= 440 && lx <= 808) {
            if (ly >= 424 && ly <= 468) controller_->uiSetSlotProp(selectedSlot_, 0, 0, 0.75);
            else if (ly >= 472 && ly <= 516) controller_->uiSetSlotProp(selectedSlot_, 1, 0, 0.50);
            else if (ly >= 520 && ly <= 564) controller_->uiSetSlotProp(selectedSlot_, 2, 0, 0.00);
            else if (ly >= 566 && ly <= 622) controller_->uiSetSlotProp(selectedSlot_, 3, 0, 1.00);
        }
    } else if (activeTab_ == 1) {
        if (lx >= 32 && lx <= 808 && ly >= 156 && ly <= 366) {
            controller_->uiSetStageXY(selectedSlot_, 0.0f, 0.0f);
        } else if (ly >= 378 && ly <= 414) {
            if (lx >= 32 && lx <= 408) controller_->uiSetStageXY(selectedSlot_, 0.0f, controller_->slotMirror(selectedSlot_).stageY);
            else if (lx >= 432 && lx <= 808) controller_->uiSetStageXY(selectedSlot_, controller_->slotMirror(selectedSlot_).stageX, 0.0f);
        } else if (lx >= 32 && lx <= 728 && ly >= 426 && ly <= 612) {
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
        } else if (activeFxModule_ == 0) {
            if (ly >= 394 && ly <= 436) controller_->uiSetParam(kParamFilterCutoff, 0.92);
            else if (ly >= 450 && ly <= 492) controller_->uiSetParam(kParamFilterRes, 0.15);
        } else if (activeFxModule_ == 2) {
            if (ly >= 454 && ly <= 494 && lx >= 436 && lx <= 792) controller_->uiSetParam(kParamCompMakeup, 0.50);
        } else if (activeFxModule_ == 3) {
            if (ly >= 396 && ly <= 436) controller_->uiSetParam(kParamSatWarmth, 0.50);
            else if (ly >= 452 && ly <= 492) controller_->uiSetParam(kParamSatMix, 0.00);
        } else if (activeFxModule_ == 4) {
            if (ly >= 452 && ly <= 492) controller_->uiSetParam(kParamDelayMix, 0.22);
        } else if (activeFxModule_ == 5) {
            if (ly >= 446 && ly <= 488 && lx >= 436) controller_->uiSetParam(kParamRevMix, 0.30);
        }
    }
    requestRepaint();
}

void WreckageMacView::onMouseRightDown(float lx, float ly) {
    if (browserOpen_ || settingsOpen_) return;
    if (activeTab_ == 0) {
        int hitKey = hitTestPianoKeyMapNote(lx, ly);
        if (hitKey >= 0) {
            selectedSlot_ = hitKey;
            startInlineRename();
            requestRepaint();
        }
    }
}

void WreckageMacView::onMouseDown(float lx, float ly) {
    if (renameFieldRef_) commitInlineRename();

    if (settingsOpen_) {
        if (lx < 140 || lx > 700 || ly < 120 || ly > 610 || (lx > 635 && ly < 162)) {
            settingsOpen_ = false; requestRepaint(); return;
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
        requestRepaint();
        return;
    }

    if (browserOpen_) {
        if (lx < 60 || lx > 780 || ly < 110 || ly > 660 || (lx > 710 && ly < 155)) {
            browserOpen_ = false; requestRepaint(); return;
        }
        if (lx >= 610 && lx <= 710 && ly >= 120 && ly <= 148) { openRootFolderDialog(); return; }
        if (lx >= 76 && lx <= 360 && ly >= 162 && ly <= 610) {
            int idx = browserCatScroll_ + static_cast<int>((ly - 162.0f) / 32.0f);
            if (idx >= 0 && idx < static_cast<int>(browserCategories_.size())) {
                selectedCategory_ = idx;
                browserItemScroll_ = 0;
                rescanRootDirectory();
            }
        } else if (lx >= 380 && lx <= 760 && ly >= 162 && ly <= 610) {
            int idx = browserItemScroll_ + static_cast<int>((ly - 162.0f) / 32.0f);
            std::string rootU8 = wideToUtf8Mac(controller_->rootLibraryDir());
            if (idx >= 0 && idx < static_cast<int>(browserItems_.size()) && !rootU8.empty()) {
                std::filesystem::path base = std::filesystem::u8path(rootU8);
                if (selectedCategory_ > 0 && selectedCategory_ < static_cast<int>(browserCategories_.size()))
                    base /= std::filesystem::u8path(browserCategories_[static_cast<size_t>(selectedCategory_)]);
                base /= std::filesystem::u8path(browserItems_[static_cast<size_t>(idx)]);
                if (hasExtCIMac(base, ".nm6")) {
                    controller_->uiLoadNm6File(utf8ToWideMac(base.u8string()));
                    controller_->activePresetName() = base.stem().u8string();
                    browserOpen_ = false;
                } else if (hasExtCIMac(base, ".wav")) {
                    controller_->uiLoadWavFile(selectedSlot_, utf8ToWideMac(base.u8string()), selectedVelLayer_);
                    controller_->uiSelectSlot(selectedSlot_, selectedVelLayer_);
                    int note = controller_->slotMirror(selectedSlot_).midiNote;
                    activePressedNote_ = note;
                    controller_->uiTriggerNote(note, 0.9f, true);
                }
            }
        }
        requestRepaint();
        return;
    }

    if (ly < 68) {
        if (lx >= 272 && lx <= 530) { rescanRootDirectory(); browserOpen_ = true; requestRepaint(); return; }
        if (lx >= 536 && lx <= 582) { openExportNm6Dialog(false); return; }
        if (lx >= 752 && lx <= 784) { controller_->uiPanic(); activePressedNote_ = -1; requestRepaint(); return; }
        if (lx >= 790 && lx <= 826) { settingsOpen_ = !settingsOpen_; requestRepaint(); return; }
        return;
    }

    if (ly >= 68 && ly < 106) {
        activeTab_ = std::clamp(static_cast<int>(lx / 280.0f), 0, 2);
        requestRepaint();
        return;
    }

    if (ly >= 636 && ly < 724) {
        if (lx >= 18 && lx <= 126) {
            bool on = controller_->getParamNormalized(kParamPunishOn) >= 0.5;
            controller_->uiSetParam(kParamPunishOn, on ? 0.0 : 1.0);
        } else if (lx >= 138 && lx <= 488) {
            dragTarget_ = 2;
            float pv = std::clamp((lx - 138.0f) / 350.0f, 0.0f, 1.0f);
            controller_->uiSetParam(kParamPunishAmount, pv);
            controller_->uiSetParam(kParamPunishOn, pv > 0.005f ? 1.0 : 0.0);
        } else if (lx >= 506 && lx <= 830) {
            int m = std::clamp(static_cast<int>((lx - 506.0f) / 108.0f), 0, 2);
            controller_->uiSetParam(kParamPunishMode, m * 0.5);
        }
        requestRepaint();
        return;
    }

    if (ly >= 724) {
        if (lx < 38) { keyboardOctaveOffset_ = std::max(-2, keyboardOctaveOffset_ - 1); requestRepaint(); return; }
        if (lx > 802) { keyboardOctaveOffset_ = std::min(3, keyboardOctaveOffset_ + 1); requestRepaint(); return; }
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
        requestRepaint();
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
            int fIdx = (ly < 470.0f) ? 0 : ((ly < 518.0f) ? 1 : ((ly < 566.0f) ? 2 : 3));
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
                float ny = std::clamp((336.0f - py) / 156.0f, 0.0f, 1.0f);
                float halfW = 356.0f - ny * 124.0f;
                float nx = std::clamp((px - 420.0f) / halfW, -1.0f, 1.0f);
                if (stageGridSnap_) { nx = std::round(nx * 4.0f) / 4.0f; ny = std::round(ny * 4.0f) / 4.0f; }
                sx = nx; sy = ny;
            };
            auto stageToScreen = [](float sx, float sy, float& outX, float& outY) {
                float halfW = 356.0f - sy * 124.0f;
                outX = 420.0f + sx * halfW;
                outY = 336.0f - sy * 156.0f;
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
                dragTarget_ = 1;
                float nx, ny; screenToStage(lx, ly, nx, ny);
                controller_->uiSetStageXY(selectedSlot_, nx, ny);
            } else if (controller_->slotMirror(selectedSlot_).hasAnySample()) {
                dragTarget_ = 1;
                float nx, ny; screenToStage(lx, ly, nx, ny);
                controller_->uiSetStageXY(selectedSlot_, nx, ny);
            }
        } else if (ly >= 378 && ly <= 414) {
            if (lx >= 32 && lx <= 408) {
                dragTarget_ = 60;
                float nx = std::clamp((lx - 32.0f) / 376.0f, 0.0f, 1.0f) * 2.0f - 1.0f;
                controller_->uiSetStageXY(selectedSlot_, nx, controller_->slotMirror(selectedSlot_).stageY);
            } else if (lx >= 432 && lx <= 808) {
                dragTarget_ = 61;
                float ny = std::clamp((lx - 432.0f) / 376.0f, 0.0f, 1.0f);
                controller_->uiSetStageXY(selectedSlot_, controller_->slotMirror(selectedSlot_).stageX, ny);
            }
        } else if (ly >= 426 && ly <= 612) {
            if (lx >= 32 && lx <= 728) {
                int b = std::clamp(static_cast<int>((ly - 426.0f) / 44.0f), 0, 3);
                dragTarget_ = 10 + b;
                controller_->uiSetParam(kParamBusCloseGain + b, std::clamp((lx - 32.0f) / 696.0f, 0.0f, 1.0f));
            } else if (lx >= 738 && lx <= 808) {
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
            requestRepaint();
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
            requestRepaint();
            return;
        }

        if (activeFxModule_ == 0) {
            if (ly >= 244 && ly <= 272 && lx >= 44 && lx <= 796) {
                int t = std::clamp(static_cast<int>((lx - 44.0f) / 94.0f), 0, 7);
                controller_->uiSetParam(kParamFilterType, t / 7.0);
            } else if (ly >= 394 && ly <= 436 && lx >= 44 && lx <= 792) {
                dragTarget_ = 70;
                controller_->uiSetParam(kParamFilterCutoff, std::clamp((lx - 44.0f) / 748.0f, 0.0f, 1.0f));
            } else if (ly >= 450 && ly <= 492 && lx >= 44 && lx <= 792) {
                dragTarget_ = 71;
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
                dragTarget_ = 80 + bestB;
                onMouseMove(lx, ly);
            } else if (ly >= 388 && ly <= 424) {
                int col = std::clamp(static_cast<int>((lx - 44.0f) / 256.0f), 0, 2);
                dragTarget_ = 83 + col;
                onMouseMove(lx, ly);
            } else if (ly >= 446 && ly <= 482) {
                int col = std::clamp(static_cast<int>((lx - 44.0f) / 256.0f), 0, 2);
                dragTarget_ = 86 + col;
                onMouseMove(lx, ly);
            }
        } else if (activeFxModule_ == 2) {
            if (ly >= 346 && ly <= 492) {
                int row = std::clamp(static_cast<int>((ly - 346.0f) / 54.0f), 0, 2);
                int col = (lx < 420.0f) ? 0 : 1;
                dragTarget_ = 90 + row * 2 + col;
                onMouseMove(lx, ly);
            }
        } else if (activeFxModule_ == 3) {
            if (ly >= 220 && ly <= 244 && lx >= 456 && lx <= 796) {
                int sm = std::clamp(static_cast<int>((lx - 456.0f) / 85.0f), 0, 3);
                controller_->uiSetParam(kParamSatMode, sm / 3.0);
            } else if (ly >= 340 && ly <= 492 && lx >= 44 && lx <= 792) {
                int row = std::clamp(static_cast<int>((ly - 340.0f) / 56.0f), 0, 2);
                dragTarget_ = 100 + row;
                onMouseMove(lx, ly);
            }
        } else if (activeFxModule_ == 4) {
            if (ly >= 220 && ly <= 244 && lx >= 496 && lx <= 796) {
                int dm = std::clamp(static_cast<int>((lx - 496.0f) / 100.0f), 0, 2);
                controller_->uiSetParam(kParamDelayMode, dm * 0.5);
            } else if (ly >= 342 && ly <= 492) {
                int row = std::clamp(static_cast<int>((ly - 342.0f) / 54.0f), 0, 2);
                int col = (lx < 420.0f) ? 0 : 1;
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
                dragTarget_ = 120 + row * 2 + col;
                onMouseMove(lx, ly);
            }
        }
    }
    requestRepaint();
}

void WreckageMacView::onMouseMove(float lx, float ly) {
    if (dragTarget_ == 0) return;

    if (dragTarget_ == 1) {
        float ny = std::clamp((336.0f - ly) / 156.0f, 0.0f, 1.0f);
        float halfW = 356.0f - ny * 124.0f;
        float nx = std::clamp((lx - 420.0f) / halfW, -1.0f, 1.0f);
        if (stageGridSnap_) { nx = std::round(nx * 4.0f) / 4.0f; ny = std::round(ny * 4.0f) / 4.0f; }
        controller_->uiSetStageXY(selectedSlot_, nx, ny);
    } else if (dragTarget_ == 2) {
        float pv = std::clamp((lx - 138.0f) / 350.0f, 0.0f, 1.0f);
        controller_->uiSetParam(kParamPunishAmount, pv);
        controller_->uiSetParam(kParamPunishOn, pv > 0.005f ? 1.0 : 0.0);
    } else if (dragTarget_ >= 10 && dragTarget_ <= 13) {
        controller_->uiSetParam(kParamBusCloseGain + (dragTarget_ - 10), std::clamp((lx - 32.0f) / 696.0f, 0.0f, 1.0f));
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
    requestRepaint();
}

void WreckageMacView::onMouseUp() {
    dragTarget_ = 0;
    if (activePressedNote_ >= 0) {
        controller_->uiTriggerNote(activePressedNote_, 0.0f, false);
        activePressedNote_ = -1;
        requestRepaint();
    }
}

void WreckageMacView::onMouseWheel(float lx, float deltaY) {
    if (!browserOpen_) return;
    int step = (deltaY > 0.0f) ? -2 : 2;
    if (lx < 370.0f) browserCatScroll_ = std::max(0, browserCatScroll_ + step);
    else browserItemScroll_ = std::max(0, browserItemScroll_ + step);
    requestRepaint();
}

void WreckageMacView::onTimerTick() {
    if (!controller_) return;
    bool needRepaint = false;
    if (controller_->saveFlashTicks() > 0) {
        controller_->decSaveFlash();
        needRepaint = true;
    }
    bool anyPulseNow = false;
    for (int n = 0; n < 128; ++n) {
        if (controller_->isNotePulsing(n)) { anyPulseNow = true; break; }
    }
    if (anyPulseNow || hadActivePulses_) {
        controller_->tickNotePulses();
        hadActivePulses_ = anyPulseNow;
        needRepaint = true;
    }

    uint32_t fSeq = controller_->waveFocusSeq();
    if (fSeq != lastSeenWaveFocusSeq_) {
        lastSeenWaveFocusSeq_ = fSeq;
        if (controller_->waveFocusSlot() == selectedSlot_) {
            selectedVelLayer_ = std::clamp(controller_->waveFocusVel(), 0, kMaxVelLayers - 1);
            selectedRR_ = std::clamp(controller_->waveFocusRR(), 0, kMaxRoundRobins - 1);
        }
        needRepaint = true;
    }

    uint32_t dSeq = controller_->uiDirtySeq();
    if (dSeq != lastSeenDirtySeq_) {
        lastSeenDirtySeq_ = dSeq;
        needRepaint = true;
    }

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
        needRepaint = true;
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
            needRepaint = true;
        }
    }

    int vox = controller_->liveVoices();
    float ph = controller_->livePlayheadNorm();
    float ol = controller_->liveOutLevel();
    if (vox > 0 || vox != lastDrawnVoices_ || activePressedNote_ >= 0 ||
        std::fabs(ph - lastDrawnPlayhead_) > 0.002f ||
        std::fabs(ol - lastDrawnOutLvl_) > 0.004f ||
        ol > 0.004f) {
        lastDrawnVoices_ = vox;
        lastDrawnPlayhead_ = ph;
        lastDrawnOutLvl_ = ol;
        needRepaint = true;
    }

    if (needRepaint) {
        requestRepaint();
    }
}

} // namespace NoiseMu6ix
#endif
