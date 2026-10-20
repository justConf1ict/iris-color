#include "IrisPlugin.h"
#include "src/IrisColorBands.h"
#include "src/IrisPresets.h"
#include "src/IrisCreative.h"
#include "IrisEditorInput.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include "IrisTypography.h"
#include "IrisBodyVisual.h"
#include "src/IrisBody.h"

extern HINSTANCE ghInst;
namespace iris::vst3 {
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace {
constexpr int kEditorWidth = 1040, kEditorHeight = 760;
// OpenDesign reference contract: warm mint, cream, and dark ink. Material
// colors belong to the sound model; text and values retain strong contrast.
constexpr COLORREF kBackground = RGB(240, 244, 235), kField = RGB(250, 250, 244);
constexpr COLORREF kInset = RGB(230, 238, 224), kLine = RGB(199, 212, 197);
constexpr COLORREF kText = RGB(40, 60, 52), kMuted = RGB(84, 104, 91), kFaint = RGB(90, 107, 96);
constexpr COLORREF kDry = RGB(153, 142, 116), kWet = RGB(72, 121, 94), kAmber = RGB(165, 131, 59);
constexpr COLORREF kSage = RGB(119, 162, 137), kRose = RGB(166, 87, 70), kViolet = RGB(116, 100, 146);
constexpr double kPi = 3.14159265358979323846;
constexpr float kVisualLow = 20.0f, kVisualHigh = 20000.0f;
constexpr int kPlotX = 52, kPlotY = 154, kPlotW = 936, kPlotH = 188;
constexpr int kKeyX = 377, kKeyY = 612, kWhiteW = 90, kWhiteH = 82, kBlackW = 50, kBlackH = 50;
constexpr wchar_t kWindowClass[] = L"IndependentAudioLab.IrisColor.Editor.v5";
constexpr int kEqBands=16, kEqFocusBase=100;
constexpr int kEqX=282, kEqY=347, kEqW=700, kEqH=94;
constexpr UINT kCancelInputMessage=WM_APP+51;

std::wstring wide(const char* s) {
    std::wstring result;
    while (*s) result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s++)));
    return result;
}
RECT box(int x, int y, int w, int h) { return RECT{x, y, x+w, y+h}; }
bool contains(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
Gdiplus::Color gcolor(COLORREF c, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}
COLORREF blend(COLORREF a, COLORREF b, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    auto c = [amount](int x, int y) { return static_cast<BYTE>(std::lround(x+(y-x)*amount)); };
    return RGB(c(GetRValue(a), GetRValue(b)), c(GetGValue(a), GetGValue(b)), c(GetBValue(a), GetBValue(b)));
}
void line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1) {
    auto pen = CreatePen(PS_SOLID, width, color); auto old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr); LineTo(dc, x2, y2); SelectObject(dc, old); DeleteObject(pen);
}
void fill(HDC dc, RECT r, COLORREF color) {
    auto brush = CreateSolidBrush(color); FillRect(dc, &r, brush); DeleteObject(brush);
}
float frequencyX(float hz) {
    return kPlotX + kPlotW * static_cast<float>(std::log(std::clamp(hz, kVisualLow, kVisualHigh)/kVisualLow)/
                                               std::log(kVisualHigh/kVisualLow));
}
float xFrequency(float x) {
    return kVisualLow * std::pow(kVisualHigh/kVisualLow, std::clamp((x-kPlotX)/kPlotW, 0.0f, 1.0f));
}
std::wstring frequencyLabel(double hz) {
    wchar_t value[32]{};
    if (hz >= 1000) std::swprintf(value, 32, L"%.2f kHz", hz/1000.0);
    else std::swprintf(value, 32, L"%.0f Hz", hz);
    return value;
}
bool isBlack(unsigned note) { return note == 1 || note == 3 || note == 6 || note == 8 || note == 10; }
RECT noteRect(unsigned note) {
    constexpr int positions[] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    const int x = kKeyX + positions[note]*kWhiteW;
    return isBlack(note) ? box(x+kWhiteW-kBlackW/2, kKeyY, kBlackW, kBlackH) :
                           box(x, kKeyY, kWhiteW-2, kWhiteH);
}
int noteAt(int x, int y) {
    for (unsigned note = 0; note < 12; ++note)
        if (isBlack(note) && contains(noteRect(note), x, y)) return static_cast<int>(note);
    for (unsigned note = 0; note < 12; ++note)
        if (!isBlack(note) && contains(noteRect(note), x, y)) return static_cast<int>(note);
    return -1;
}
enum class EditorPage { main, body, bands, motion };
enum class ControlKind { knob, lowBoundary, highBoundary, gate, splitLow, splitHigh, clarity, material };
struct Slider {
    Param id; RECT bounds; const wchar_t* label; const wchar_t* help; COLORREF accent; int radius;
    ControlKind kind = ControlKind::knob;
};
constexpr int kBodyFirstControl=32, kBodyMaterialControl=37;
const std::array<Slider, 38> kSliders {{
    {kColor, box(38,419,166,166), L"Color", L"Global color: dry / wet up to 100%; added resonance above 100%.", kWet, 67},
    {kSnap, box(585,453,82,82), L"Snap", L"Snap: move frequencies toward the selected notes.", kWet, 33},
    {kTransient, box(671,453,82,82), L"Transient", L"Transient: keep the original attack in the processed sound.", kRose, 33},
    {kMorph, box(239,429,128,128), L"MORPH", L"Morph: blend wet magnitudes toward dry magnitudes, retaining the wet phase.", kViolet, 48},
    {kLowHz, box(52,126,100,28), L"LOW", L"Low: drag the left boundary to set the lowest processed frequency.", kText, 0, ControlKind::lowBoundary},
    {kHighHz, box(888,126,100,28), L"HIGH", L"High: drag the right boundary to set the highest processed frequency.", kText, 0, ControlKind::highBoundary},
    {kGateDb, box(395,442,172,100), L"GATE", L"Gate: each dry spectral bin controls the matching wet bin. Drag the threshold vertically.", kAmber, 0, ControlKind::gate},
    {kDecayMs, box(757,453,82,82), L"Tail decay", L"Tail decay: resonant-tail length, heard with Global color above 100%.", kViolet, 33},
    {kWidth, box(843,453,82,82), L"Width", L"Width: stereo side level. 100% preserves the processed width.", kWet, 33},
    {kOutputDb, box(929,453,82,82), L"Output", L"Output: final gain. Lower it to keep headroom when adding color.", kWet, 33},
    {kColorLow, box(276,508,64,64), L"Color", L"Low Color: added resonance below the L/M crossover. Requires Global Color above 100%.", kDry, 27},
    {kColorEqLowDb, box(390,508,64,64), L"Broad EQ", L"Low Broad EQ: a smooth wet-only tone control, added to the 16-band Paint EQ.", kSage, 27},
    {kColorMid, box(532,508,64,64), L"Color", L"Mid Color: added resonance between the two crossovers. Requires Global Color above 100%.", kDry, 27},
    {kColorEqMidDb, box(646,508,64,64), L"Broad EQ", L"Mid Broad EQ: a smooth wet-only tone control, added to the 16-band Paint EQ.", kSage, 27},
    {kColorHigh, box(788,508,64,64), L"Color", L"High Color: added resonance above the M/H crossover. Requires Global Color above 100%.", kDry, 27},
    {kColorEqHighDb, box(902,508,64,64), L"Broad EQ", L"High Broad EQ: a smooth wet-only tone control, added to the 16-band Paint EQ.", kSage, 27},
    {kColorLowMidHz, box(0,0,0,0), L"Low / Mid", L"L/M crossover: drag the colored line, or click its frequency field to type an exact value.", kDry, 0, ControlKind::splitLow},
    {kColorMidHighHz, box(0,0,0,0), L"Mid / High", L"M/H crossover: drag the colored line, or type Hz / kHz. It stays at least 1.25 times L/M.", kSage, 0, ControlKind::splitHigh},
    {kAttackColor, box(41,326,70,70), L"Attack", L"Attack Color: multiplier for the added resonance at each detected onset; Global Color must exceed 100%.", kDry, 28},
    {kTailColor, box(143,326,70,70), L"Tail", L"Tail Color: added-resonance multiplier reached after the onset transition.", kDry, 28},
    {kColorTransitionMs, box(245,326,70,70), L"Transition", L"Transition: time for resonance to move from Attack Color to Tail Color.", kDry, 28},
    {kPitchDiveSemitones, box(405,326,70,70), L"Pitch start", L"Pitch Dive: resonance begins this many semitones from its target and settles in key.", kWet, 28},
    {kPitchDiveMs, box(548,326,70,70), L"Dive time", L"Dive time: how quickly the added-resonance pitch returns to the selected notes.", kWet, 28},
    {kScaleMotionDepth, box(53,485,76,76), L"Depth", L"Scale Motion depth: rotate emphasis through the allowed notes in the added resonance.", kSage, 29},
    {kScaleMotionRateHz, box(191,485,76,76), L"Free rate", L"Scale Motion free rate in Hz. Turn Sync off to use this rate.", kSage, 29},
    {kFormantMix, box(537,485,76,76), L"Mix", L"Formant mix: the wet signal's moving vowel-shaped spectral envelope.", kRose, 29},
    {kFormantVowel, box(653,485,76,76), L"Vowel", L"Vowel: continuously move among A, E, I, O and U; this shapes wet tone without retuning notes.", kRose, 29},
    {kFormantMotion, box(769,485,76,76), L"Motion", L"Formant motion: depth of automatic movement through vowel shapes.", kRose, 29},
    {kFormantRateHz, box(885,485,76,76), L"Rate", L"Formant rate: speed of the vowel movement in cycles per second.", kRose, 29},
    {kPeakTamerAmount, box(740,326,70,70), L"Amount", L"Peak Tamer: dynamic reduction of narrow peaks above their local spectral neighborhood.", kAmber, 28},
    {kPeakTamerMaxDb, box(883,326,70,70), L"Max cut", L"Peak Tamer Max cut: the largest allowed attenuation; this is not an output limiter.", kAmber, 28},
    {kClarity, box(760,405,248,31), L"Clarity", L"Clarity: open the upper harmonics inside your selected range. Available in Prism.", kWet, 0, ControlKind::clarity},
    {kBodyMix, box(53,438,148,138), L"Amount", L"Body Amount: add material resonance to the existing sound. At 0%, the original Iris Color sound is preserved.", kViolet, 55},
    {kBodyDecayMs, box(264,451,120,116), L"Decay", L"Body Decay: the low mode's ring time. Higher partials settle faster as Damping increases.", kViolet, 43},
    {kBodyDamping, box(462,451,120,116), L"Damping", L"Body Damping: shorten the higher partials for a softer, less metallic tail.", kWet, 43},
    {kBodyInharmonicity, box(660,451,120,116), L"Inharmonicity", L"Body Inharmonicity: move the partials away from harmonic spacing. Each material responds differently.", kViolet, 43},
    {kBodyStrike, box(858,451,120,116), L"Strike", L"Body Strike: emphasize attacks in the sound that excites the material.", kRose, 43},
    {kBodyMaterial, box(436,103,556,52), L"Material", L"Material: Glass, Metal, Wood, or a harmonic String model. The pitch set below tunes the body.", kViolet, 0, ControlKind::material},
}};
const RECT kPresetButton=box(734,28,174,34), kBypassButton=box(922,28,86,34);
const RECT kEngineButton=box(77,60,184,26);
const RECT kMainTab=box(370,28,66,34), kBodyTab=box(440,28,68,34);
const RECT kBandsTab=box(512,28,112,34), kMotionTab=box(628,28,88,34);
const RECT kBodySummary=box(275,99,359,29);
constexpr const wchar_t* kBodyLabels[]={L"Glass",L"Metal",L"Wood",L"String"};
RECT bodyMaterialRect(int material) { return box(436+material*141,103,133,52); }
COLORREF bodyAccent(int material) {
    return material==1 ? kRose : material==2 ? kAmber : material==3 ? kWet : kViolet;
}
const RECT kMorphButton=box(243,409,120,25), kGateButton=box(395,409,111,25);
const RECT kRootButton=box(42,655,83,33), kScaleButton=box(135,655,211,33);
const RECT kMidiButton=box(229,612,117,27), kLimitButton=box(882,723,126,25);
const RECT kSplitLowInput=box(266,100,163,29), kSplitHighInput=box(439,100,163,29);
const RECT kEqValueInput=box(791,310,108,27), kEqFlatButton=box(916,310,80,27);
const RECT kAttackToggle=box(254,283,65,25), kDiveToggle=box(582,283,65,25), kTamerToggle=box(922,283,65,25);
const RECT kScaleToggle=box(419,448,65,25), kFormantToggle=box(922,448,65,25);
const RECT kScaleSyncButton=box(305,493,179,27), kScaleDivisionButton=box(305,530,179,30);
constexpr const wchar_t* kDivisionLabels[]={L"1/1",L"1/2",L"1/4",L"1/8",L"1/16",L"1/32"};
int CALLBACK fontProbe(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM l) {
    *reinterpret_cast<bool*>(l) = true; return 0;
}
bool fontAvailable(HDC dc, const wchar_t* name) {
    LOGFONTW query{}; query.lfCharSet=DEFAULT_CHARSET;
    std::wcsncpy(query.lfFaceName, name, LF_FACESIZE-1);
    bool found=false;
    EnumFontFamiliesExW(dc, &query, fontProbe, reinterpret_cast<LPARAM>(&found), 0);
    return found;
}
}

class NativeEditor final : public EditorView, public IPlugViewContentScaleSupport {
public:
    explicit NativeEditor(Controller* c) : EditorView(c), owner_(c) {
        rect = ViewRect(0,0,kEditorWidth,kEditorHeight);
        dryDisplay_.fill(-120); wetDisplay_.fill(-120); gateDisplay_.fill(1);
    }
    ~NativeEditor() override { closeWindow(); }
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override {
        return type && std::strcmp(type,kPlatformTypeHWND)==0 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API attached(void* parent, FIDString type) override {
        if (!parent || isPlatformTypeSupported(type)!=kResultTrue || window_) return kResultFalse;
        WNDCLASSEXW cls{};
        cls.cbSize=sizeof(cls); cls.style=CS_DBLCLKS; cls.lpfnWndProc=windowProc;
        cls.hInstance=ghInst; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.lpszClassName=kWindowClass;
        if (!RegisterClassExW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return kResultFalse;
        Gdiplus::GdiplusStartupInput gdiInput;
        if (Gdiplus::GdiplusStartup(&gdiToken_,&gdiInput,nullptr)!=Gdiplus::Ok) {
            UnregisterClassW(kWindowClass,ghInst); return kResultFalse;
        }
        makeFonts();
        window_=CreateWindowExW(0,kWindowClass,L"Iris Color",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN|WS_TABSTOP,
            0,0,rect.getWidth(),rect.getHeight(),static_cast<HWND>(parent),nullptr,ghInst,this);
        if (!window_) {
            freeFonts(); Gdiplus::GdiplusShutdown(gdiToken_); gdiToken_=0;
            UnregisterClassW(kWindowClass,ghInst); return kResultFalse;
        }
        SetTimer(window_,1,33,nullptr); owner_->requestVisual();
        return EditorView::attached(parent,type);
    }
    tresult PLUGIN_API removed() override { closeWindow(); return EditorView::removed(); }
    tresult PLUGIN_API onSize(ViewRect* size) override {
        if (!size) return kInvalidArgument;
        finishInput(false);
        if (window_) SetWindowPos(window_,nullptr,0,0,size->getWidth(),size->getHeight(),SWP_NOMOVE|SWP_NOZORDER);
        return EditorView::onSize(size);
    }
    tresult PLUGIN_API checkSizeConstraint(ViewRect* size) override {
        if (!size) return kInvalidArgument;
        size->right=size->left+static_cast<int32>(std::lround(kEditorWidth*scale_));
        size->bottom=size->top+static_cast<int32>(std::lround(kEditorHeight*scale_));
        return kResultTrue;
    }
    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override {
        if (!std::isfinite(factor) || factor<=0 || factor>8) return kInvalidArgument;
        scale_=factor; ViewRect size=rect; checkSizeConstraint(&size);
        if (plugFrame) return plugFrame->resizeView(this,&size);
        return onSize(&size);
    }
    OBJ_METHODS(NativeEditor, EditorView)
    DEFINE_INTERFACES
        DEF_INTERFACE(IPlugViewContentScaleSupport)
    END_DEFINE_INTERFACES(EditorView)
    REFCOUNT_METHODS(EditorView)
private:
    Controller* owner_;
    HWND window_=nullptr;
    std::array<HFONT,10> fonts_{};
    editor::PrivateTypography typography_;
    ULONG_PTR gdiToken_=0;
    double scale_=1.0;
    int dragging_=-1, focused_=0, hover_=-1, hoverNote_=-1, plotHoverX_=-1, hoverMaterial_=-1;
    int dragX_=0, dragY_=0;
    double dragValue_=0;
    HWND valueEdit_=nullptr;
    WNDPROC oldEditProc_=nullptr;
    HFONT inputFont_=nullptr;
    HBRUSH inputBrush_=nullptr;
    Param inputParam_=kColor;
    bool inputInvalid_=false, eqPainting_=false, hoverInput_=false, hoverAction_=false, hoverBodySummary_=false;
    int eqSelected_=7, eqLastBand_=0;
    double eqLastDb_=0;
    std::array<bool,kEqBands> eqGestures_{};
    Values lastPainted_{};
    EditorPage page_=EditorPage::main;
    iris::VisualFrame visual_{};
    bool haveVisual_=false;
    DWORD lastVisualTick_=0;
    std::array<float,iris::kVisualBins> dryDisplay_{}, wetDisplay_{}, gateDisplay_{};

    void makeFonts() {
        HDC dc=GetDC(nullptr);
        const wchar_t* face=fontAvailable(dc,L"Segoe UI") ? L"Segoe UI" :
            fontAvailable(dc,L"Arial") ? L"Arial" : L"Liberation Sans";
        ReleaseDC(nullptr,dc);
        typography_.initialize(ghInst,face);
        // GDI handles remain available for native value-entry fields. The main
        // surface uses private GDI+ fonts at the actual host scale.
        for (std::size_t i=0; i<fonts_.size(); ++i)
            fonts_[i]=CreateFontW(-static_cast<int>(std::lround(editor::PrivateTypography::sizes[i])),0,0,0,
                i==2 || i==4 || i==8 ? FW_MEDIUM : FW_NORMAL,
                FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,face);
    }
    void freeFonts() {
        for (auto& font: fonts_) { if (font) DeleteObject(font); font=nullptr; }
        typography_.clear();
    }
    void closeWindow() {
        finishInput(false);
        finishGesture();
        if (window_) { KillTimer(window_,1); DestroyWindow(window_); window_=nullptr; }
        UnregisterClassW(kWindowClass,ghInst); freeFonts();
        if (gdiToken_) { Gdiplus::GdiplusShutdown(gdiToken_); gdiToken_=0; }
    }
    template<class Function> void graphics(HDC dc, Function&& draw) {
        const int saved=SaveDC(dc); SetMapMode(dc,MM_TEXT);
        RECT client{}; GetClientRect(window_,&client);
        {
            Gdiplus::Graphics g(dc);
            g.ScaleTransform(client.right/static_cast<float>(kEditorWidth),client.bottom/static_cast<float>(kEditorHeight));
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality); draw(g);
        }
        RestoreDC(dc,saved);
    }
    void rounded(HDC dc, RECT r, COLORREF color, COLORREF outline, float radius=7) {
        graphics(dc,[&](Gdiplus::Graphics& g) {
            const float x=static_cast<float>(r.left), y=static_cast<float>(r.top);
            const float w=static_cast<float>(r.right-r.left-1), h=static_cast<float>(r.bottom-r.top-1);
            const float d=std::min({radius*2,w,h});
            Gdiplus::GraphicsPath path;
            path.AddArc(x,y,d,d,180,90); path.AddArc(x+w-d,y,d,d,270,90);
            path.AddArc(x+w-d,y+h-d,d,d,0,90); path.AddArc(x,y+h-d,d,d,90,90); path.CloseFigure();
            Gdiplus::SolidBrush brush(gcolor(color)); Gdiplus::Pen pen(gcolor(outline),1);
            g.FillPath(&brush,&path); g.DrawPath(&pen,&path);
        });
    }
    void text(HDC dc, const wchar_t* label, RECT r, COLORREF color=kText, int font=0, UINT align=DT_LEFT) {
        if (const auto* face=typography_.face(static_cast<std::size_t>(font))) {
            bool drawn=false;
            graphics(dc,[&](Gdiplus::Graphics& g) {
                g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
                Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
                // These are single-line UI labels. LineLimit can suppress an
                // otherwise fitting unit label when the font's line box is
                // taller than the control's compact text rectangle.
                format.SetFormatFlags((format.GetFormatFlags()|Gdiplus::StringFormatFlagsNoWrap)&
                    ~Gdiplus::StringFormatFlagsLineLimit);
                format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                format.SetAlignment((align&DT_CENTER) ? Gdiplus::StringAlignmentCenter :
                    (align&DT_RIGHT) ? Gdiplus::StringAlignmentFar : Gdiplus::StringAlignmentNear);
                const Gdiplus::RectF bounds(static_cast<float>(r.left),static_cast<float>(r.top),
                    static_cast<float>(r.right-r.left),static_cast<float>(r.bottom-r.top));
                Gdiplus::SolidBrush ink(gcolor(color));
                drawn=g.DrawString(label,-1,face,bounds,&format,&ink)==Gdiplus::Ok;
            });
            if (drawn) return;
        }
        auto old=SelectObject(dc,fonts_[font]); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color);
        DrawTextW(dc,label,-1,&r,align|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        SelectObject(dc,old);
    }
    void dot(HDC dc, int x, int y, COLORREF color, int radius=3) {
        graphics(dc,[&](Gdiplus::Graphics& g) {
            Gdiplus::SolidBrush brush(gcolor(color));
            g.FillEllipse(&brush,static_cast<float>(x-radius),static_cast<float>(y-radius),
                          static_cast<float>(2*radius),static_cast<float>(2*radius));
        });
    }
    void toggle(HDC dc, RECT r, const wchar_t* label, bool on, COLORREF accent=kWet, bool minimal=false) {
        if (!minimal) rounded(dc,r,on ? blend(kField,accent,.14f) : kField,
                              on ? blend(kLine,accent,.28f) : kLine,4);
        dot(dc,r.left+11,(r.top+r.bottom)/2,on ? accent : kLine,on ? 3 : 2);
        text(dc,label,box(r.left+23,r.top,r.right-r.left-27,r.bottom-r.top),on ? kText : kMuted,4);
    }
    void menuButton(HDC dc, RECT r, const wchar_t* label, COLORREF color=kText) {
        rounded(dc,r,kField,kLine,4);
        text(dc,label,box(r.left+12,r.top,r.right-r.left-35,r.bottom-r.top),color,0);
        line(dc,r.right-18,r.top+(r.bottom-r.top)/2-2,r.right-14,r.top+(r.bottom-r.top)/2+2,kMuted);
        line(dc,r.right-14,r.top+(r.bottom-r.top)/2+2,r.right-10,r.top+(r.bottom-r.top)/2-2,kMuted);
    }
    void tabButton(HDC dc, RECT r, const wchar_t* label, EditorPage page) {
        const bool active=page_==page;
        const COLORREF accent=page==EditorPage::body ? kViolet : kWet;
        const auto surface=active ? blend(kBackground,accent,.10f) : kBackground;
        rounded(dc,r,surface,surface,4);
        text(dc,label,r,active ? accent : kMuted,4,DT_CENTER);
        if (active) line(dc,r.left+14,r.bottom-3,r.right-14,r.bottom-3,accent,2);
    }
    bool visibleControl(std::size_t i) const {
        if (i>=kEqFocusBase) return page_==EditorPage::bands && i<kEqFocusBase+kEqBands;
        if (i>=kSliders.size()) return false;
        if (page_==EditorPage::body) return i>=kBodyFirstControl;
        if (i>=kBodyFirstControl) return false;
        if (i==31) return page_==EditorPage::main;
        if (i==4 || i==5) return true;
        if (i==0) return page_!=EditorPage::motion;
        return page_==EditorPage::main ? i<10 : page_==EditorPage::bands ? i>=10 && i<18 : i>=18;
    }
    Slider control(std::size_t i) const {
        auto s=kSliders[i];
        if (i==0 && page_==EditorPage::bands) s.bounds=box(38,347,166,166);
        if (i>=kBodyFirstControl && i<kBodyMaterialControl && s.id!=kBodyDamping && s.id!=kBodyStrike)
            s.accent=bodyAccent(discrete(kBodyMaterial,owner_->getParamNormalized(kBodyMaterial)));
        return s;
    }
    static bool eqFocus(int index) { return index>=kEqFocusBase && index<kEqFocusBase+kEqBands; }
    static Param eqParam(int index) { return static_cast<Param>(kWetEqBand0+std::clamp(index,0,kEqBands-1)); }
    Param focusedParam() const { return eqFocus(focused_) ? eqParam(focused_-kEqFocusBase) : kSliders[focused_].id; }
    static bool bandColor(Param id) { return id==kColorLow || id==kColorMid || id==kColorHigh; }
    static bool bandEq(Param id) { return id==kColorEqLowDb || id==kColorEqMidDb || id==kColorEqHighDb; }
    static bool bodyKnob(Param id) { return id>=kBodyMix && id<=kBodyStrike; }
    static bool horizontalControl(ControlKind kind) {
        return kind==ControlKind::lowBoundary || kind==ControlKind::highBoundary ||
               kind==ControlKind::splitLow || kind==ControlKind::splitHigh;
    }
    int fieldHeight() const { return page_==EditorPage::main ? kPlotH : page_==EditorPage::bands ? 70 : 60; }
    void selectPage(EditorPage page) {
        if (page_==page) return;
        finishInput(false); finishGesture(); page_=page; hover_=-1; plotHoverX_=-1; hoverMaterial_=-1;
        hoverBodySummary_=false;
        if (!visibleControl(static_cast<std::size_t>(focused_)))
            focused_=page==EditorPage::body ? kBodyFirstControl : page==EditorPage::motion ? 18 : 0;
        InvalidateRect(window_,nullptr,FALSE);
    }
    RECT splitLabel(bool high,const Values& v) const {
        const double low=plain(kColorLowMidHz,v[kColorLowMidHz]);
        const double upper=std::max(low*1.25,plain(kColorMidHighHz,v[kColorMidHighHz]));
        int left=std::clamp(static_cast<int>(frequencyX(static_cast<float>(low)))-53,kPlotX,kPlotX+kPlotW-106);
        int right=std::clamp(static_cast<int>(frequencyX(static_cast<float>(upper)))-53,kPlotX,kPlotX+kPlotW-106);
        // The true split lines may be close together; keep both labels reachable.
        if (right-left<110) {
            left=std::clamp((left+right-110)/2,kPlotX,kPlotX+kPlotW-216);
            right=left+110;
        }
        return box(high ? right : left,kPlotY+fieldHeight()+3,106,24);
    }
    bool freshVisual() const { return haveVisual_ && static_cast<DWORD>(GetTickCount()-lastVisualTick_)<=200; }
    bool updateVisual() {
        owner_->requestVisual();
        iris::VisualFrame incoming{}; bool changed=false;
        if (owner_->visualSnapshot(incoming)) {
            visual_=incoming; haveVisual_=true; lastVisualTick_=GetTickCount(); changed=true;
        }
        const bool fresh=freshVisual();
        for (int i=0; i<iris::kVisualBins; ++i) {
            auto update=[&changed](float& display,float target,bool meter) {
                if (!std::isfinite(target)) target=meter ? -120.0f : 1.0f;
                target=meter ? std::clamp(target,-120.0f,12.0f) : std::clamp(target,0.0f,1.0f);
                const float previous=display;
                display+=(target-display)*(meter ? (target>display ? .75f : .22f) : .55f);
                if (std::abs(target-display)<(meter ? .04f : .001f)) display=target;
                changed|=std::abs(display-previous)>(meter ? .01f : .001f);
            };
            update(dryDisplay_[i],fresh ? visual_.dryDb[i] : -120.0f,true);
            update(wetDisplay_[i],fresh ? visual_.wetDb[i] : -120.0f,true);
            update(gateDisplay_[i],fresh ? visual_.gateGain[i] : 1.0f,false);
        }
        return changed;
    }
    float displayAt(const std::array<float,iris::kVisualBins>& values,float unit) const {
        const float at=std::clamp(unit,0.0f,1.0f)*(iris::kVisualBins-1);
        const int lo=static_cast<int>(at), hi=std::min(lo+1,iris::kVisualBins-1);
        return values[lo]+(values[hi]-values[lo])*(at-lo);
    }
    void drawField(HDC dc,const Values& values) {
        const int height=fieldHeight();
        graphics(dc,[&](Gdiplus::Graphics& g) {
            g.SetClip(Gdiplus::Rect(kPlotX,kPlotY,kPlotW,height));
            constexpr float grid[]={20,50,100,200,500,1000,2000,5000,10000,20000};
            for (const float hz:grid) {
                const float x=frequencyX(hz); Gdiplus::Pen pen(gcolor(kLine,105),1);
                g.DrawLine(&pen,x,static_cast<float>(kPlotY),x,static_cast<float>(kPlotY+height));
            }
            const float baseline=static_cast<float>(kPlotY+height-2);
            std::array<Gdiplus::PointF,iris::kVisualBins> dry{}, wet{};
            std::array<Gdiplus::PointF,iris::kVisualBins+2> area{};
            bool dryPresent=false, wetPresent=false;
            for (int i=0;i<iris::kVisualBins;++i) {
                const float x=kPlotX+static_cast<float>(i)/(iris::kVisualBins-1)*kPlotW;
                const auto levelY=[&](float db) {
                    return baseline-std::clamp((db+96.0f)/96.0f,0.0f,1.0f)*(height-8);
                };
                dry[i]=Gdiplus::PointF(x,levelY(dryDisplay_[i]));
                wet[i]=Gdiplus::PointF(x,levelY(wetDisplay_[i]));
                area[i+1]=wet[i];
                dryPresent|=dryDisplay_[i]>-95.5f;
                wetPresent|=wetDisplay_[i]>-95.5f;
            }
            area.front()=Gdiplus::PointF(static_cast<float>(kPlotX),baseline);
            area.back()=Gdiplus::PointF(static_cast<float>(kPlotX+kPlotW),baseline);
            if (wetPresent) {
                Gdiplus::SolidBrush tint(gcolor(kSage,37));
                g.FillPolygon(&tint,area.data(),static_cast<int>(area.size()));
            }
            if (dryPresent) {
                Gdiplus::Pen source(gcolor(kDry,195),1.15f);
                g.DrawLines(&source,dry.data(),static_cast<int>(dry.size()));
            }
            if (wetPresent) {
                Gdiplus::Pen processed(gcolor(kWet,240),1.7f);
                g.DrawLines(&processed,wet.data(),static_cast<int>(wet.size()));
            }
            const std::uint16_t mask=freshVisual() && values[kMidiEnabled]>=.5 ? visual_.activeMask : activeMask(values);
            for (int note=16; note<=127; ++note) {
                const float hz=440.0f*std::pow(2.0f,(note-69)/12.0f);
                if (hz<kVisualLow || hz>kVisualHigh || !(mask&(1u<<(note%12)))) continue;
                const float x=frequencyX(hz);
                Gdiplus::Pen tick(gcolor(kSage,140),1);
                g.DrawLine(&tick,x,static_cast<float>(kPlotY+height-6),x,static_cast<float>(kPlotY+height));
            }
            if (freshVisual()) {
                const auto count=std::min<std::size_t>({visual_.moveCount,visual_.moves.size(),12});
                for (std::size_t i=0; i<count; ++i) {
                    const auto& move=visual_.moves[i];
                    if (!std::isfinite(move.sourceHz) || !std::isfinite(move.targetHz) || !std::isfinite(move.levelDb) ||
                        move.sourceHz<kVisualLow || move.sourceHz>kVisualHigh || move.targetHz<kVisualLow ||
                        move.targetHz>kVisualHigh || move.levelDb< -72) continue;
                    const float x0=frequencyX(move.sourceHz), x1=frequencyX(move.targetHz);
                    if (std::abs(x1-x0)<1.1f) continue;
                    const float y=kPlotY+25.0f+(i%3)*12.0f, lift=std::min(22.0f,6.0f+std::abs(x1-x0)*.3f);
                    const BYTE alpha=static_cast<BYTE>(std::clamp((move.levelDb+84)*2.0f,20.0f,95.0f));
                    Gdiplus::Pen arc(gcolor(kWet,alpha),1);
                    g.DrawBezier(&arc,x0,y,x0,y-lift,x1,y-lift,x1,y);
                    const float sign=x1>x0 ? 1.0f : -1.0f;
                    g.DrawLine(&arc,x1,y,x1-2.5f*sign,y-3.5f);
                }
            }
            const float low=frequencyX(static_cast<float>(plain(kLowHz,values[kLowHz])));
            const float high=frequencyX(static_cast<float>(std::max(plain(kLowHz,values[kLowHz])+20,plain(kHighHz,values[kHighHz]))));
            Gdiplus::SolidBrush outside(gcolor(kField,185));
            g.FillRectangle(&outside,static_cast<float>(kPlotX),static_cast<float>(kPlotY),low-kPlotX,static_cast<float>(height));
            g.FillRectangle(&outside,high,static_cast<float>(kPlotY),kPlotX+kPlotW-high,static_cast<float>(height));
            if (values[kGateEnabled]>=.5) for (int i=0; i<iris::kVisualBins; ++i) {
                const float x=kPlotX+static_cast<float>(i)/(iris::kVisualBins-1)*kPlotW;
                if (x<low || x>high) continue;
                const float level=std::clamp((dryDisplay_[i]+96)/72,0.0f,1.0f), reduction=(1-gateDisplay_[i])*level;
                if (reduction<.005f) continue;
                Gdiplus::Pen gate(gcolor(kAmber,static_cast<BYTE>(reduction*185)),3);
                g.DrawLine(&gate,x,static_cast<float>(kPlotY+height-2),x+static_cast<float>(kPlotW)/iris::kVisualBins,static_cast<float>(kPlotY+height-2));
            }
        });
    }
    RECT boundaryLabel(bool high,const Values& v) const {
        const float hz=static_cast<float>(high ? std::max(plain(kLowHz,v[kLowHz])+20,plain(kHighHz,v[kHighHz])) : plain(kLowHz,v[kLowHz]));
        return box(std::clamp(static_cast<int>(frequencyX(hz))-53,kPlotX,kPlotX+kPlotW-106),128,106,22);
    }
    static float eqX(int band) { return kEqX+band*kEqW/static_cast<float>(kEqBands-1); }
    static float eqY(double db) { return kEqY+static_cast<float>((12-std::clamp(db,-24.0,12.0))*kEqH/36.0); }
    static int eqBandAt(int x) { return std::clamp(static_cast<int>(std::lround((x-kEqX)*(kEqBands-1.0)/kEqW)),0,kEqBands-1); }
    static double eqDbAtY(int y) { return std::clamp(12-(y-kEqY)*36.0/kEqH,-24.0,12.0); }
    bool inEq(int x,int y) const {
        return page_==EditorPage::bands && contains(box(kEqX-18,kEqY-5,kEqW+36,kEqH+10),x,y);
    }
    void valueButton(HDC dc,RECT r,const wchar_t* label,COLORREF accent=kWet) {
        rounded(dc,r,blend(kField,accent,.05f),blend(kLine,accent,.20f),4);
        text(dc,label,box(r.left+8,r.top,r.right-r.left-24,r.bottom-r.top),kText,0,DT_CENTER);
        // The small pencil makes the value's direct-entry affordance explicit.
        line(dc,r.right-15,r.top+18,r.right-9,r.top+11,accent,1);
        line(dc,r.right-16,r.top+19,r.right-13,r.top+18,accent,1);
    }
    void drawPaintEq(HDC dc,const Values& v) {
        rounded(dc,box(244,298,772,180),kField,kLine,5);
        text(dc,L"Paint EQ",box(263,309,130,27),kText,3);
        text(dc,L"16 bands / wet",box(376,312,180,22),kFaint,1);
        const auto selectedHz=frequencyLabel(iris::graphicEqNodeHz(eqSelected_));
        text(dc,selectedHz.c_str(),box(661,312,119,23),kMuted,0,DT_RIGHT);
        wchar_t value[32]{};
        std::swprintf(value,32,L"%+.1f dB",plain(eqParam(eqSelected_),v[eqParam(eqSelected_)]));
        valueButton(dc,kEqValueInput,value,kSage);
        rounded(dc,kEqFlatButton,kInset,kLine,5);
        text(dc,L"Flat EQ",kEqFlatButton,kText,0,DT_CENTER);
        for (const int db:{12,0,-12,-24}) {
            const int y=static_cast<int>(std::lround(eqY(db)));
            line(dc,kEqX-15,y,kEqX+kEqW+15,y,db==0 ? blend(kLine,kSage,.25f) : blend(kField,kLine,.46f));
            wchar_t label[16]{}; std::swprintf(label,16,db>0 ? L"+%d" : L"%d",db);
            text(dc,label,box(246,y-8,30,16),kFaint,1,DT_RIGHT);
        }
        std::array<float,iris::kGraphicEqBands> db{};
        for (int i=0;i<kEqBands;++i) db[i]=static_cast<float>(plain(eqParam(i),v[eqParam(i)]));
        graphics(dc,[&](Gdiplus::Graphics& g) {
            const float zero=eqY(0);
            for (int i=0;i<kEqBands;++i) {
                const float x=eqX(i), y=eqY(db[i]);
                const bool selected=eqSelected_==i;
                Gdiplus::Pen rail(gcolor(kLine,selected ? 160 : 65),1);
                g.DrawLine(&rail,x,static_cast<float>(kEqY),x,static_cast<float>(kEqY+kEqH));
                const COLORREF accent=db[i]>=0 ? kWet : kSage;
                Gdiplus::SolidBrush bar(gcolor(accent,selected ? 90 : 45));
                g.FillRectangle(&bar,x-16,std::min(y,zero),32.0f,std::max(1.0f,std::abs(y-zero)));
            }
            std::array<Gdiplus::PointF,256> curve{};
            for (std::size_t i=0;i<curve.size();++i) {
                const float u=static_cast<float>(i)/(curve.size()-1);
                const float hz=iris::kGraphicEqMinHz*std::pow(iris::kGraphicEqMaxHz/iris::kGraphicEqMinHz,u);
                curve[i]=Gdiplus::PointF(kEqX+kEqW*u,eqY(iris::graphicEqDbAt(hz,db)));
            }
            Gdiplus::Pen curvePen(gcolor(kWet,235),1.7f);
            g.DrawLines(&curvePen,curve.data(),static_cast<int>(curve.size()));
            for (int i=0;i<kEqBands;++i) {
                const bool selected=eqSelected_==i;
                const float radius=selected ? 4.8f : 3.2f;
                Gdiplus::SolidBrush body(gcolor(selected ? kText : kWet));
                g.FillEllipse(&body,eqX(i)-radius,eqY(db[i])-radius,2*radius,2*radius);
                if (selected) {
                    Gdiplus::Pen ring(gcolor(kSage,145),1);
                    g.DrawEllipse(&ring,eqX(i)-8,eqY(db[i])-8,16.0f,16.0f);
                }
            }
        });
        for (const int i:{0,3,6,9,12,15}) {
            auto label=frequencyLabel(iris::graphicEqNodeHz(i));
            text(dc,label.c_str(),box(static_cast<int>(eqX(i))-39,447,78,19),kFaint,1,DT_CENTER);
        }
    }
    void drawSplits(HDC dc,const Values& v) {
        const double low=plain(kColorLowMidHz,v[kColorLowMidHz]);
        const double high=std::max(low*1.25,plain(kColorMidHighHz,v[kColorMidHighHz]));
        for (const bool upper:{false,true}) {
            const float x=frequencyX(static_cast<float>(upper ? high : low));
            const bool hot=hover_==(upper ? 17 : 16) || dragging_==(upper ? 17 : 16);
            const COLORREF accent=upper ? kSage : kDry;
            graphics(dc,[&](Gdiplus::Graphics& g) {
                if (hot) {
                    Gdiplus::Pen glow(gcolor(accent,30),7);
                    g.DrawLine(&glow,x,static_cast<float>(kPlotY),x,static_cast<float>(kPlotY+fieldHeight()));
                }
                Gdiplus::Pen split(gcolor(accent,hot ? 255 : 195),hot ? 2.0f : 1.4f);
                g.DrawLine(&split,x,static_cast<float>(kPlotY),x,static_cast<float>(kPlotY+fieldHeight()+5));
            });
            const RECT r=splitLabel(upper,v);
            rounded(dc,r,blend(kInset,accent,hot ? .18f : .06f),blend(kLine,accent,hot ? .65f : .35f),4);
            line(dc,static_cast<int>(x),r.top-3,static_cast<int>(x),r.top+2,accent);
            const std::wstring label=(upper ? L"M/H  " : L"L/M  ")+frequencyLabel(upper ? high : low);
            text(dc,label.c_str(),r,hot ? kText : blend(kText,accent,.45f),1,DT_CENTER);
        }
        valueButton(dc,kSplitLowInput,(L"L/M  "+frequencyLabel(low)).c_str(),kDry);
        valueButton(dc,kSplitHighInput,(L"M/H  "+frequencyLabel(high)).c_str(),kSage);
    }
    void drawSpectrum(HDC dc,const Values& v) {
        const bool main=page_==EditorPage::main, bands=page_==EditorPage::bands;
        const int cardHeight=main ? 304 : bands ? 200 : 166;
        const int axisY=main ? 353 : bands ? 253 : 220;
        const int hintY=main ? 371 : bands ? 269 : 238;
        rounded(dc,box(24,92,992,cardHeight),kField,kLine,5);
        text(dc,L"Spectrum",box(44,104,216,20),kText,4);
        if (main) {
            const int material=std::clamp(discrete(kBodyMaterial,v[kBodyMaterial]),0,3);
            const COLORREF accent=bodyAccent(material);
            const bool on=v[kBodyMix]>0.0;
            rounded(dc,kBodySummary,blend(kField,accent,hoverBodySummary_ ? .09f : .045f),
                hoverBodySummary_ ? accent : blend(kLine,accent,.24f),4);
            dot(dc,kBodySummary.left+13,113,on ? accent : kFaint,on ? 3 : 2);
            wchar_t summary[80]{};
            if (on && plain(kBodyMix,v[kBodyMix])<1.0) std::swprintf(summary,80,L"Body  /  %ls  /  <1%%",kBodyLabels[material]);
            else if (on) std::swprintf(summary,80,L"Body  /  %ls  /  %.0f%%",kBodyLabels[material],plain(kBodyMix,v[kBodyMix]));
            else std::swprintf(summary,80,L"Body  /  %ls  /  Off",kBodyLabels[material]);
            text(dc,summary,box(kBodySummary.left+25,101,294,25),hoverBodySummary_ ? kText : kMuted,8);
            line(dc,kBodySummary.right-18,110,kBodySummary.right-14,114,accent);
            line(dc,kBodySummary.right-14,114,kBodySummary.right-18,118,accent);
        }
        dot(dc,668,114,kDry); text(dc,L"Dry",box(679,103,48,22),kMuted,8);
        dot(dc,746,114,kWet); text(dc,L"Processed",box(757,103,130,22),kMuted,8);
        dot(dc,920,114,v[kGateEnabled]>=.5 ? kAmber : kFaint); text(dc,L"Gate",box(931,103,52,22),kMuted,8);
        drawField(dc,v);
        for (const bool high:{false,true}) {
            const Param id=high ? kHighHz : kLowHz;
            const double hz=high ? std::max(plain(kLowHz,v[kLowHz])+20,plain(kHighHz,v[id])) : plain(kLowHz,v[id]);
            const int x=static_cast<int>(std::lround(frequencyX(static_cast<float>(hz))));
            const bool hot=hover_==(high ? 5 : 4) || dragging_==(high ? 5 : 4);
            line(dc,x,kPlotY-2,x,kPlotY+fieldHeight()+4,hot ? kWet : blend(kWet,kField,.22f),hot ? 2 : 1);
            const RECT r=boundaryLabel(high,v);
            if (hot) rounded(dc,r,kInset,kLine,4);
            const std::wstring label=(high ? L"High  " : L"Low  ")+frequencyLabel(hz);
            text(dc,label.c_str(),r,hot ? kText : kMuted,1,DT_CENTER);
            line(dc,x-3,kPlotY+fieldHeight()+4,x+4,kPlotY+fieldHeight()+4,kMuted);
        }
        if (bands) drawSplits(dc,v);
        constexpr float frequencies[]={20,50,100,200,500,1000,2000,5000,10000,20000};
        const wchar_t* labels[]={L"20",L"50",L"100",L"200",L"500",L"1k",L"2k",L"5k",L"10k",L"20k"};
        for (std::size_t i=0; i<std::size(frequencies); ++i)
            text(dc,labels[i],box(static_cast<int>(frequencyX(frequencies[i]))-22,axisY,44,17),kFaint,1,DT_CENTER);
        if (main) text(dc,L"Hz",box(956,hintY,32,15),kFaint,1,DT_RIGHT);
        if (plotHoverX_>=kPlotX && plotHoverX_<=kPlotX+kPlotW) {
            const float hz=xFrequency(static_cast<float>(plotHoverX_));
            const int midi=static_cast<int>(std::lround(69+12*std::log2(hz/440)));
            const int pc=((midi%12)+12)%12; const float unit=(plotHoverX_-kPlotX)/static_cast<float>(kPlotW);
            wchar_t probe[160]{};
            std::swprintf(probe,160,L"%.0f Hz  /  %ls%d     DRY  %.1f dB     PROCESSED  %.1f dB",
                hz,wide(kNoteNames[pc]).c_str(),midi/12-1,displayAt(dryDisplay_,unit),displayAt(wetDisplay_,unit));
            text(dc,probe,box(44,hintY,925,16),kMuted,1);
            graphics(dc,[&](Gdiplus::Graphics& g) {
                Gdiplus::Pen crosshair(gcolor(kText,95),1); crosshair.SetDashStyle(Gdiplus::DashStyleDot);
                g.DrawLine(&crosshair,static_cast<float>(plotHoverX_),static_cast<float>(kPlotY+1),
                           static_cast<float>(plotHoverX_),static_cast<float>(kPlotY+fieldHeight()));
            });
        } else if (page_==EditorPage::motion) {
            wchar_t status[160]{};
            const bool prism=v[kEngineMode]>=.5;
            std::swprintf(status,160,prism ?
                L"Global Color %.0f%%  /  Prism: Attack-Tail, Pitch Dive and Scale Motion shape the main color, also at 100%%." :
                L"Global Color %.0f%%  /  Legacy: Attack-Tail, Pitch Dive and Scale Motion shape added resonance above 100%%.",plain(kColor,v[kColor]));
            text(dc,status,box(44,hintY,944,17),plain(kColor,v[kColor])>(prism ? 0 : 100) ? kMuted : blend(kText,kAmber,.4f),1);
        } else text(dc,bands ? L"Drag either colored divider anywhere along the line  /  Click a frequency field to type Hz or kHz" :
                    L"Drag the boundaries to choose your range",box(44,hintY,935,17),kFaint,1);
    }
    void drawBodyMaterials(HDC dc,const Values& v) {
        const int material=std::clamp(discrete(kBodyMaterial,v[kBodyMaterial]),0,3);
        const bool focused=GetFocus()==window_ && focused_==kBodyMaterialControl;
        for (int m=0;m<4;++m) {
            const auto r=bodyMaterialRect(m); const bool selected=m==material, hot=m==hoverMaterial_;
            const auto accent=bodyAccent(m);
            rounded(dc,r,blend(kField,accent,selected ? .10f : hot ? .055f : .015f),
                selected ? accent : hot ? blend(kLine,accent,.65f) : kLine,5);
            graphics(dc,[&](Gdiplus::Graphics& g) {
                editor::drawBodyMaterialSymbol(g,m,Gdiplus::RectF(static_cast<float>(r.left+10),
                    static_cast<float>(r.top+14),26.0f,24.0f),selected || hot ? accent : kMuted);
            });
            text(dc,kBodyLabels[m],box(r.left+43,r.top+3,r.right-r.left-49,r.bottom-r.top-6),kText,4,DT_CENTER);
            if (selected) line(dc,r.left+50,r.bottom-7,r.right-15,r.bottom-7,accent,2);
            if (selected && focused) {
                RECT ring{r.left-3,r.top-3,r.right+3,r.bottom+3};
                graphics(dc,[&](Gdiplus::Graphics& g) {
                    Gdiplus::Pen pen(gcolor(kText,215),1); pen.SetDashStyle(Gdiplus::DashStyleDot);
                    g.DrawRectangle(&pen,static_cast<float>(ring.left),static_cast<float>(ring.top),
                        static_cast<float>(ring.right-ring.left),static_cast<float>(ring.bottom-ring.top));
                });
            }
        }
    }
    void drawBodyModel(HDC dc,const Values& v) {
        const int material=std::clamp(discrete(kBodyMaterial,v[kBodyMaterial]),0,3);
        const COLORREF accent=bodyAccent(material);
        const float inharmonicity=static_cast<float>(plain(kBodyInharmonicity,v[kBodyInharmonicity]));
        const float damping=static_cast<float>(plain(kBodyDamping,v[kBodyDamping]));
        const float decayMs=static_cast<float>(plain(kBodyDecayMs,v[kBodyDecayMs]));
        const float anchorDecay=iris::bodyModeDecaySeconds(material,0,decayMs,damping);
        const float lastRatio=iris::bodyModeRatio(material,iris::kBodyModeCount-1,inharmonicity);
        const float maxRatio=std::pow(2.0f,std::ceil(std::log2(std::max(16.0f,lastRatio))));
        constexpr float plotX=453.0f, plotY=259.0f, plotW=526.0f, plotH=102.0f;
        const auto toX=[&](float ratio) { return plotX+std::log2(std::max(1.0f,ratio))/std::log2(maxRatio)*plotW; };
        const wchar_t* descriptions[]={L"Clear edges, an uneven ring.",L"Bright modes and a metallic shimmer.",
            L"A warm, quickly settling body.",L"Harmonic partials, with a little stretch."};
        rounded(dc,box(24,179,992,232),kField,kLine,6);
        text(dc,kBodyLabels[material],box(49,194,320,29),kText,3);
        text(dc,descriptions[material],box(49,221,355,22),kMuted,1);
        text(dc,L"Modeled partials",box(451,196,250,29),kText,3);
        text(dc,L"Height: relative decay  /  Spacing: pitch ratio",box(451,226,536,22),kMuted,1);
        line(dc,418,201,418,388,kLine);
        graphics(dc,[&](Gdiplus::Graphics& g) {
            editor::drawBodyMaterialSymbol(g,material,Gdiplus::RectF(105.0f,247.0f,242.0f,133.0f),accent,
                static_cast<float>(v[kBodyStrike]),static_cast<float>(v[kBodyInharmonicity]));
            Gdiplus::Pen grid(gcolor(kLine,140),1), baseline(gcolor(kMuted,160),1);
            for (float ratio=1;ratio<=maxRatio;ratio*=2) {
                const float x=toX(ratio);
                g.DrawLine(&grid,x,plotY,x,plotY+plotH);
            }
            g.DrawLine(&baseline,plotX,plotY+plotH,plotX+plotW,plotY+plotH);
            const Gdiplus::RectF clip(plotX-3,plotY-4,plotW+6,plotH+7);
            g.SetClip(clip);
            for (int mode=0;mode<iris::kBodyModeCount;++mode) {
                const float ratio=iris::bodyModeRatio(material,mode,inharmonicity);
                const float ring=iris::bodyModeDecaySeconds(material,mode,decayMs,damping);
                const float relative=std::clamp(ring/std::max(.0001f,anchorDecay),0.0f,1.0f);
                const float x=toX(ratio), height=std::max(1.0f,relative*plotH);
                Gdiplus::Pen stem(gcolor(accent,mode==0 ? 240 : 180),mode==0 ? 3.0f : 2.0f);
                stem.SetStartCap(Gdiplus::LineCapRound); stem.SetEndCap(Gdiplus::LineCapRound);
                g.DrawLine(&stem,x,plotY+plotH,x,plotY+plotH-height);
                Gdiplus::SolidBrush tip(gcolor(accent,230));
                g.FillEllipse(&tip,x-2,plotY+plotH-height-2,4.0f,4.0f);
            }
        });
        for (float ratio=1;ratio<=maxRatio;ratio*=2) {
            wchar_t label[20]{}; std::swprintf(label,20,L"%.0f\u00d7",ratio);
            const int x=static_cast<int>(std::lround(toX(ratio)));
            text(dc,label,box(x-23,365,46,22),kMuted,1,DT_CENTER);
        }
        text(dc,L"Material sketch",box(49,385,340,18),kFaint,1,DT_CENTER);
        wchar_t guide[112]{};
        std::swprintf(guide,112,L"Lowest-mode decay  %.0f ms  /  tuned by your pitch set",1000.0f*anchorDecay);
        text(dc,guide,box(451,388,540,18),kFaint,1);
    }
    void drawBody(HDC dc,const Values& v) {
        text(dc,L"Body",box(41,97,360,39),kText,2);
        text(dc,L"A little material. A new way to resonate.",box(42,137,380,25),kMuted,0);
        drawBodyMaterials(dc,v); drawBodyModel(dc,v);
        text(dc,L"Add a material response to your sound",box(44,423,576,22),kMuted,8);
        const int material=std::clamp(discrete(kBodyMaterial,v[kBodyMaterial]),0,3);
        const auto accent=bodyAccent(material); const bool on=v[kBodyMix]>0.0;
        wchar_t state[80]{};
        if (on && plain(kBodyMix,v[kBodyMix])<1.0) std::swprintf(state,80,L"%ls  /  Amount <1%%",kBodyLabels[material]);
        else if (on) std::swprintf(state,80,L"%ls  /  Amount %.0f%%",kBodyLabels[material],plain(kBodyMix,v[kBodyMix]));
        else std::swprintf(state,80,L"Amount 0%%  /  ready");
        dot(dc,759,434,on ? accent : kFaint,on ? 3 : 2);
        text(dc,state,box(770,422,223,24),kMuted,1,DT_RIGHT);
    }
    void drawKnob(HDC dc,const Slider& s,double value,bool disabled,bool hovered) {
        const float cx=(s.bounds.left+s.bounds.right)*.5f, cy=(s.bounds.top+s.bounds.bottom)*.5f;
        const float radius=static_cast<float>(s.radius); const COLORREF accent=disabled ? kFaint : s.accent;
        graphics(dc,[&](Gdiplus::Graphics& g) {
            auto arc=[&](Gdiplus::Pen& pen,float start,float sweep) {
                std::array<Gdiplus::PointF,272> points{};
                const int segments=std::max(1,static_cast<int>(std::ceil(std::abs(sweep))));
                for (int i=0; i<=segments; ++i) {
                    const float a=static_cast<float>((start+sweep*i/segments)*kPi/180);
                    points[i]=Gdiplus::PointF(cx+radius*std::cos(a),cy+radius*std::sin(a));
                }
                g.DrawLines(&pen,points.data(),segments+1);
            };
            const float thickness=s.id==kColor || s.id==kBodyMix ? 3.2f : s.id==kMorph || bodyKnob(s.id) ? 2.5f : 2.0f;
            Gdiplus::Pen track(gcolor(kLine),thickness);
            track.SetStartCap(Gdiplus::LineCapRound); track.SetEndCap(Gdiplus::LineCapRound); arc(track,135,270);
            Gdiplus::Pen progress(gcolor(accent),thickness);
            progress.SetStartCap(Gdiplus::LineCapRound); progress.SetEndCap(Gdiplus::LineCapRound);
            if (bandEq(s.id) || s.id==kPitchDiveSemitones) {
                if (std::abs(value-.5)>.0001) arc(progress,270,static_cast<float>(270*(value-.5)));
            } else if (value>.0001) arc(progress,135,static_cast<float>(270*value));
            const float inner=radius-(s.id==kColor ? 9.0f : 7.0f);
            const Gdiplus::RectF body(cx-inner,cy-inner,2*inner,2*inner);
            Gdiplus::SolidBrush face(gcolor(blend(kField,accent,disabled ? .025f : .065f)));
            g.FillEllipse(&face,body);
            if (hovered) {
                Gdiplus::Pen outline(gcolor(accent,bodyKnob(s.id) ? 210 : 150),1);
                g.DrawEllipse(&outline,body);
            }
            if (s.id==kColor) for (int i:{0,4,8}) {
                const float a=static_cast<float>((135+i*33.75)*kPi/180);
                Gdiplus::Pen tick(gcolor(i==4 ? kMuted : kFaint,165),1);
                g.DrawLine(&tick,cx+(radius+9)*std::cos(a),cy+(radius+9)*std::sin(a),
                           cx+(radius+(i==4 ? 15 : 12))*std::cos(a),cy+(radius+(i==4 ? 15 : 12))*std::sin(a));
            }
            const float a=static_cast<float>((135+270*value)*kPi/180);
            Gdiplus::Pen mark(gcolor(accent),s.id==kColor ? 2.0f : 1.6f);
            mark.SetStartCap(Gdiplus::LineCapRound); mark.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLine(&mark,cx+(radius-9)*std::cos(a),cy+(radius-9)*std::sin(a),
                       cx+(radius-4)*std::cos(a),cy+(radius-4)*std::sin(a));
        });
        wchar_t number[40]{}; const double real=plain(s.id,value);
        if (s.id==kOutputDb || bandEq(s.id)) std::swprintf(number,40,L"%+.1f",real);
        else if (s.id==kPitchDiveSemitones) std::swprintf(number,40,L"%+.1f",real);
        else if (s.id==kScaleMotionRateHz || s.id==kFormantRateHz)
            std::swprintf(number,40,real<1 ? L"%.2f" : L"%.1f",real);
        else if (s.id==kPeakTamerMaxDb) std::swprintf(number,40,L"%.1f",real);
        else if (s.id==kBodyMix && real>0.0 && real<1.0) std::swprintf(number,40,L"<1");
        else if (s.id==kFormantVowel) {
            constexpr const wchar_t* vowels[]={L"A",L"E",L"I",L"O",L"U"};
            const int nearestVowel=std::clamp(static_cast<int>(std::lround(real)),0,4);
            if (std::abs(real-nearestVowel)<.08) std::swprintf(number,40,L"%ls",vowels[nearestVowel]);
            else {
                const int left=std::clamp(static_cast<int>(real),0,3);
                std::swprintf(number,40,L"%ls/%ls",vowels[left],vowels[left+1]);
            }
        } else std::swprintf(number,40,L"%.0f",real);
        const int x=static_cast<int>(cx), y=static_cast<int>(cy);
        if (s.id==kColor) {
            text(dc,number,box(x-59,y-30,118,50),kText,6,DT_CENTER);
            text(dc,L"%",box(x-20,y+22,40,17),kMuted,7,DT_CENTER);
            text(dc,L"Color",box(x-72,y+75,144,23),kText,4,DT_CENTER);
        } else if (s.id==kMorph) {
            text(dc,number,box(x-44,y-25,88,38),disabled ? kFaint : kText,5,DT_CENTER);
            text(dc,L"%",box(x-20,y+13,40,17),kMuted,7,DT_CENTER);
            text(dc,L"Amount",box(x-52,y+56,104,20),kMuted,8,DT_CENTER);
        } else if (bodyKnob(s.id)) {
            text(dc,number,box(x-53,y-23,106,37),kText,s.id==kBodyMix ? 5 : 9,DT_CENTER);
            text(dc,wide(kSpecs[s.id].units).c_str(),box(x-28,y+15,56,17),kMuted,1,DT_CENTER);
            text(dc,s.label,box(x-79,y+s.radius+12,158,23),kText,8,DT_CENTER);
        } else {
            const int numberFont=s.radius<30 && std::wcslen(number)>=4 ? 3 : 9;
            text(dc,number,box(x-37,y-17,74,26),disabled ? kFaint : kText,numberFont,DT_CENTER);
            const auto unit=wide(s.id==kFormantVowel ? "vowel" : kSpecs[s.id].units);
            text(dc,unit.c_str(),box(x-28,y+8,56,14),kMuted,1,DT_CENTER);
            text(dc,s.label,box(x-53,y+s.radius+10,106,20),hovered ? kText : kMuted,8,DT_CENTER);
        }
    }
    void drawGate(HDC dc,const Values& v) {
        const bool enabled=v[kGateEnabled]>=.5; const auto r=kSliders[6].bounds;
        rounded(dc,r,kField,hover_==6 ? blend(kLine,kAmber,.5f) : kLine,4);
        const int left=r.left+10, top=r.top+10, right=r.right-10, bottom=r.bottom-12;
        for (const int db:{-24,-48,-72}) {
            const int y=bottom-static_cast<int>((db+96)/96.0*(bottom-top)); line(dc,left,y,right,y,blend(kField,kLine,.6f));
        }
        graphics(dc,[&](Gdiplus::Graphics& g) {
            g.SetClip(Gdiplus::Rect(left,top,right-left,bottom-top));
            std::array<Gdiplus::PointF,iris::kVisualBins> curve{};
            for (int i=0; i<iris::kVisualBins; ++i) {
                const float x=left+static_cast<float>(i)/(iris::kVisualBins-1)*(right-left);
                const float strength=std::clamp((dryDisplay_[i]+96)/96.0f,0.0f,1.0f);
                curve[i]=Gdiplus::PointF(x,bottom-strength*(bottom-top));
                Gdiplus::Pen bar(gcolor(kDry,enabled ? 45 : 20),1);
                g.DrawLine(&bar,x,static_cast<float>(bottom),x,curve[i].Y);
            }
            Gdiplus::Pen pen(gcolor(enabled ? kDry : kFaint,170),1);
            g.DrawLines(&pen,curve.data(),static_cast<int>(curve.size()));
        });
        const int y=bottom-static_cast<int>(v[kGateDb]*(bottom-top));
        line(dc,left,y,right,y,enabled ? kAmber : kFaint,1);
        fill(dc,box(left-2,y-3,4,7),enabled ? kAmber : kFaint);
        wchar_t db[40]{}; std::swprintf(db,40,L"%.1f dB",plain(kGateDb,v[kGateDb]));
        const int labelY=std::clamp(y-23,top+1,bottom-19);
        fill(dc,box(right-90,labelY,90,19),kField);
        text(dc,db,box(right-89,labelY,89,18),enabled ? blend(kText,kAmber,.45f) : kMuted,4,DT_RIGHT);
        text(dc,L"Dry threshold",box(r.left-2,549,r.right-r.left+4,20),kMuted,8,DT_CENTER);
    }
    void drawMorphWeights(HDC dc,const Values& v) {
        const bool enabled=v[kMorphEnabled]>=.5; const float dry=enabled ? static_cast<float>(v[kMorph]) : 0;
        constexpr int x=245, y=582, width=320;
        text(dc,L"Amplitude",box(x,y-2,77,16),kFaint,1);
        const int barX=x+84, barW=91;
        rounded(dc,box(barX,y+3,barW,4),kDry,kDry,2);
        const int wetW=static_cast<int>(std::lround(barW*(1-dry)));
        if (wetW>0) rounded(dc,box(barX,y+3,std::max(2,wetW),4),enabled ? kWet : kFaint,enabled ? kWet : kFaint,2);
        wchar_t label[64]{}; std::swprintf(label,64,L"Wet %.0f  /  Dry %.0f",(1-dry)*100,dry*100);
        text(dc,label,box(x+185,y-3,width-185,18),enabled ? kMuted : kFaint,1,DT_RIGHT);
    }
    void drawClarity(HDC dc,const Values& v) {
        const bool enabled=v[kEngineMode]>=.5;
        const bool active=hover_==31 || dragging_==31 || (GetFocus()==window_ && focused_==31);
        text(dc,L"Clarity",box(760,405,66,31),enabled ? kText : kFaint,8);
        if (!enabled) {
            text(dc,L"Prism only",box(836,405,172,31),kFaint,1,DT_RIGHT);
            return;
        }
        constexpr int x=836, y=418, width=101;
        rounded(dc,box(x,y,width,5),kLine,kLine,2);
        const int amount=static_cast<int>(std::lround(v[kClarity]*(width-1)));
        if (amount>0) rounded(dc,box(x,y,std::max(2,amount),5),kSage,kSage,2);
        dot(dc,x+amount,y+2,kWet,active ? 5 : 4);
        wchar_t value[24]{};
        std::swprintf(value,24,L"%.0f%%",plain(kClarity,v[kClarity]));
        text(dc,value,box(943,405,65,31),active ? kWet : kText,8,DT_RIGHT);
    }
    void drawBandControls(HDC dc,const Values& v) {
        const double low=plain(kColorLowMidHz,v[kColorLowMidHz]);
        const double high=std::max(low*1.25,plain(kColorMidHighHz,v[kColorMidHighHz]));
        const wchar_t* names[]={L"Low",L"Mid",L"High"};
        const std::wstring ranges[]={L"< "+frequencyLabel(low),frequencyLabel(low)+L" \u2013 "+frequencyLabel(high),
                                     L"> "+frequencyLabel(high)};
        rounded(dc,box(24,298,204,302),blend(kField,kSage,.065f),kLine,5);
        text(dc,L"Resonance",box(42,314,168,24),kWet,4,DT_CENTER);
        text(dc,L"A spectrum in your key",box(34,341,183,19),kFaint,1,DT_CENTER);
        for (int band=0; band<3; ++band) {
            const int x=244+256*band;
            rounded(dc,box(x,486,band==2 ? 260 : 244,114),kField,kLine,5);
            text(dc,names[band],box(x+15,490,48,20),kText,4);
            text(dc,ranges[band].c_str(),box(x+60,490,band==2 ? 184 : 168,20),kFaint,1,DT_RIGHT);
        }
        const bool addedResonance=plain(kColor,v[kColor])>100;
        text(dc,addedResonance ? (v[kEngineMode]>=.5 ? L"Local note Q + gain" : L"Added color is active") :
            L"Raise Color above 100%",box(35,545,182,23),addedResonance ? kWet : blend(kText,kAmber,.4f),1,DT_CENTER);
        text(dc,L"EQ always shapes the wet",box(35,568,182,21),kFaint,1,DT_CENTER);
        drawPaintEq(dc,v);
    }
    const wchar_t* sliderHelp(const Slider& slider,const Values& v) const {
        if (v[kEngineMode]>=.5) switch (slider.id) {
            case kColor: return L"Prism Color: dry / wet to 100%; above 100%, sharpen and boost the note bands.";
            case kDecayMs: return L"Decay: shape the ring of the note bands. Higher notes settle faster.";
            case kColorLow: return L"Prism Low Color: local note sharpness (Q) and gain below L/M. Active above Global Color 100%.";
            case kColorMid: return L"Prism Mid Color: local note sharpness (Q) and gain between the crossovers. Active above Global Color 100%.";
            case kColorHigh: return L"Prism High Color: local note sharpness (Q) and gain above M/H. Active above Global Color 100%.";
            case kAttackColor: return L"Prism Attack Color: main-color multiplier at each detected onset; also works at Global Color 100%.";
            case kTailColor: return L"Prism Tail Color: main-color multiplier reached after the onset transition.";
            case kColorTransitionMs: return L"Prism Transition: time for the main color to move from Attack Color to Tail Color.";
            case kPitchDiveSemitones: return L"Prism Pitch Dive: the main color begins this many semitones from its target, then settles in key.";
            case kPitchDiveMs: return L"Prism Dive time: how quickly the main-color pitch returns to the selected notes.";
            case kScaleMotionDepth: return L"Prism Scale Motion: rotate emphasis through allowed notes in the main color, also at Global Color 100%.";
            default: break;
        }
        return slider.help;
    }
    bool disabledControl(Param id,const Values& v) const {
        if (id==kClarity) return v[kEngineMode]<.5;
        const bool extra=plain(kColor,v[kColor])>100;
        const bool motionColor=v[kEngineMode]>=.5 ? plain(kColor,v[kColor])>0 : extra;
        if (id==kMorph) return v[kMorphEnabled]<.5;
        if (bandColor(id)) return !extra;
        if (id==kAttackColor || id==kTailColor || id==kColorTransitionMs) return v[kAttackTailEnabled]<.5 || !motionColor;
        if (id==kPitchDiveSemitones || id==kPitchDiveMs) return v[kPitchDiveEnabled]<.5 || !motionColor;
        if (id==kScaleMotionDepth) return v[kScaleMotionEnabled]<.5 || !motionColor;
        if (id==kScaleMotionRateHz) return v[kScaleMotionEnabled]<.5 || !motionColor || v[kScaleMotionSync]>=.5;
        if (id==kFormantMix || id==kFormantVowel || id==kFormantMotion || id==kFormantRateHz) return v[kFormantEnabled]<.5;
        if (id==kPeakTamerAmount || id==kPeakTamerMaxDb) return v[kPeakTamerEnabled]<.5;
        return false;
    }
    void effectCard(HDC dc,RECT r,const wchar_t* title,const wchar_t* subtitle,Param enable,RECT button,COLORREF accent,const Values& v) {
        const bool on=v[enable]>=.5;
        rounded(dc,r,on ? blend(kField,accent,.045f) : kField,on ? blend(kLine,accent,.30f) : kLine,5);
        text(dc,title,box(r.left+17,r.top+11,button.left-r.left-28,25),kText,3);
        text(dc,subtitle,box(r.left+17,r.top+37,r.right-r.left-34,18),kFaint,1);
        toggle(dc,button,on ? L"On" : L"Off",on,accent);
    }
    void drawMotionControls(HDC dc,const Values& v) {
        const bool active=freshVisual();
        wchar_t tamer[96]{};
        if (active && v[kPeakTamerEnabled]>=.5)
            std::swprintf(tamer,96,L"Narrow peaks  /  live reduction %.1f dB",visual_.tamerReductionDb);
        else std::swprintf(tamer,96,L"Soften isolated peaks in the wet");
        effectCard(dc,box(24,272,310,158),L"Attack / Tail",L"Give the attack room to breathe",kAttackTailEnabled,kAttackToggle,kDry,v);
        effectCard(dc,box(346,272,326,158),L"Pitch Dive",L"A falling resonance, landing in key",kPitchDiveEnabled,kDiveToggle,kWet,v);
        effectCard(dc,box(684,272,332,158),L"Peak Tamer",tamer,kPeakTamerEnabled,kTamerToggle,kAmber,v);
        effectCard(dc,box(24,438,480,162),L"Scale Motion",L"Rhythmic emphasis on allowed notes",kScaleMotionEnabled,kScaleToggle,kSage,v);
        effectCard(dc,box(516,438,500,162),L"Formant",L"A vowel-shaped motion in the wet",kFormantEnabled,kFormantToggle,kRose,v);
        toggle(dc,kScaleSyncButton,v[kScaleMotionSync]>=.5 ? L"Tempo sync" : L"Free running",v[kScaleMotionSync]>=.5,kSage);
        const int division=discrete(kScaleMotionDivision,v[kScaleMotionDivision]);
        menuButton(dc,kScaleDivisionButton,(std::wstring(L"Beat  ")+kDivisionLabels[std::clamp(division,0,5)]).c_str(),
                   v[kScaleMotionSync]>=.5 ? kText : kFaint);
        const wchar_t* syncStatus=v[kScaleMotionEnabled]<.5 ? L"Scale Motion is off" :
            v[kEngineMode]<.5 && plain(kColor,v[kColor])<=100 ? L"Raise Global Color above 100%" :
            plain(kColor,v[kColor])<=0 ? L"Color 0 is dry" :
            v[kScaleMotionSync]<.5 ? L"Free-rate clock" :
            !active ? L"Sync armed / waiting for audio" :
            visual_.motionHostSynced ? L"Host tempo + beat" : L"Internal beat / no song position";
        text(dc,syncStatus,box(39,582,446,17),v[kScaleMotionEnabled]>=.5 && v[kScaleMotionSync]>=.5 && active && !visual_.motionHostSynced ? blend(kText,kAmber,.4f) : kFaint,1);
        if (active && v[kFormantEnabled]>=.5) {
            constexpr const wchar_t* vowels[]={L"A",L"E",L"I",L"O",L"U"};
            const int index=std::clamp(static_cast<int>(std::lround(visual_.formantVowel)),0,4);
            const auto label=std::wstring(L"Live vowel  ")+vowels[index];
            text(dc,label.c_str(),box(812,582,188,17),blend(kText,kRose,.55f),1,DT_RIGHT);
        }
    }
    void drawKeyboard(HDC dc,const Values& v) {
        text(dc,L"Pitch set",box(42,610,166,27),kText,4);
        text(dc,v[kMidiEnabled]>=.5 ? L"Following held MIDI notes" : L"Choose your notes",box(42,636,304,15),kFaint,1);
        toggle(dc,kMidiButton,L"MIDI notes",v[kMidiEnabled]>=.5,kDry);
        menuButton(dc,kRootButton,wide(kNoteNames[discrete(kRoot,v[kRoot])]).c_str());
        menuButton(dc,kScaleButton,wide(kScaleNames[discrete(kScale,v[kScale])]).c_str());
        const auto mask=freshVisual() && v[kMidiEnabled]>=.5 ? visual_.activeMask : activeMask(v);
        for (const bool black:{false,true}) for (unsigned note=0; note<12; ++note) {
            if (isBlack(note)!=black) continue;
            const auto r=noteRect(note); const bool active=(mask&(1u<<note))!=0;
            const COLORREF base=black ? RGB(74,79,68) : kField;
            const COLORREF selected=black ? kWet : blend(kField,kSage,.27f);
            rounded(dc,r,active ? selected : base,hoverNote_==static_cast<int>(note) ? kWet : black ? base : kLine,3);
            if (active) {
                const int inset=black ? 9 : 14;
                line(dc,r.left+inset,r.top+2,r.right-inset,r.top+2,black ? kAmber : kWet,2);
            }
            const RECT label=black ? box(r.left,r.top+26,r.right-r.left,20) : box(r.left,r.bottom-30,r.right-r.left,23);
            text(dc,wide(kNoteNames[note]).c_str(),label,black ? kField : active ? kText : kMuted,black ? 7 : 0,DT_CENTER);
        }
    }
    void paint(HDC target) {
        RECT client{}; GetClientRect(window_,&client);
        HDC dc=CreateCompatibleDC(target);
        HBITMAP bitmap=CreateCompatibleBitmap(target,std::max(1L,client.right),std::max(1L,client.bottom));
        if (!dc || !bitmap) { if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); return; }
        auto oldBitmap=SelectObject(dc,bitmap);
        SetMapMode(dc,MM_ANISOTROPIC); SetWindowExtEx(dc,kEditorWidth,kEditorHeight,nullptr);
        SetViewportExtEx(dc,client.right,client.bottom,nullptr);
        fill(dc,box(0,0,kEditorWidth,kEditorHeight),kBackground);
        const auto v=owner_->snapshot(); lastPainted_=v;
        graphics(dc,[&](Gdiplus::Graphics& g) {
            Gdiplus::Pen outline(gcolor(kWet),1.45f), petal(gcolor(kViolet),1.35f);
            Gdiplus::SolidBrush leaf(gcolor(kSage,72)), seed(gcolor(kRose,180));
            g.FillEllipse(&leaf,34.0f,32.0f,20.0f,25.0f);
            g.DrawEllipse(&outline,33.0f,29.0f,29.0f,29.0f);
            g.DrawEllipse(&petal,42.0f,30.0f,14.0f,23.0f);
            g.FillEllipse(&seed,43.0f,40.0f,7.0f,7.0f);
        });
        text(dc,L"Iris Color",box(77,23,300,35),kText,2);
        menuButton(dc,kEngineButton,(std::wstring(L"Core  ")+wide(kEngineModeNames[discrete(kEngineMode,v[kEngineMode])])).c_str(),
                   v[kEngineMode]>=.5 ? kWet : kMuted);
        text(dc,L"v0.7.0",box(273,64,94,20),kFaint,1);
        tabButton(dc,kMainTab,L"Main",EditorPage::main);
        tabButton(dc,kBodyTab,L"Body",EditorPage::body);
        tabButton(dc,kBandsTab,L"Bands + EQ",EditorPage::bands);
        tabButton(dc,kMotionTab,L"Motion",EditorPage::motion);
        menuButton(dc,kPresetButton,wide(iris::presetName(owner_->currentPresetIndex())).c_str());
        toggle(dc,kBypassButton,L"Bypass",v[kBypass]>=.5,kAmber);
        if (page_==EditorPage::body) drawBody(dc,v);
        else drawSpectrum(dc,v);
        if (page_==EditorPage::main) {
            text(dc,L"Resonance",box(48,399,158,22),kMuted,8,DT_CENTER);
            toggle(dc,kMorphButton,L"Morph",v[kMorphEnabled]>=.5,kViolet,true);
            toggle(dc,kGateButton,L"Gate",v[kGateEnabled]>=.5,kAmber,true);
            text(dc,L"Shape & output",box(590,409,170,25),kMuted,8);
            drawClarity(dc,v);
            line(dc,224,425,224,589,kLine);
        } else if (page_==EditorPage::bands) drawBandControls(dc,v);
        else if (page_==EditorPage::motion) drawMotionControls(dc,v);
        for (std::size_t i=0; i<kSliders.size(); ++i) {
            const auto s=control(i); if (s.kind!=ControlKind::knob || !visibleControl(i)) continue;
            drawKnob(dc,s,v[s.id],disabledControl(s.id,v),
                hover_==static_cast<int>(i) || (GetFocus()==window_ && focused_==static_cast<int>(i)));
        }
        if (page_==EditorPage::main) {
            drawGate(dc,v); drawMorphWeights(dc,v);
        }
        line(dc,32,604,1008,604,kLine); drawKeyboard(dc,v);
        line(dc,32,715,1008,715,kLine);
        const wchar_t* help=valueEdit_ ? (inputInvalid_ ? L"Enter a valid number with the correct unit. Examples: 700 Hz, 3k, 3 kHz, -3 dB." :
            L"Enter to apply   /   Escape to cancel") :
            eqFocus(hover_) ? L"Paint across bands  /  Double-click: reset band  /  Right-click or Enter: type dB  /  Flat EQ: reset all EQ" :
            hover_>=0 && hover_<static_cast<int>(kSliders.size()) ? sliderHelp(kSliders[hover_],v) :
            hoverBodySummary_ ? L"Open Body to add Glass, Metal, Wood, or String resonance." :
            hoverNote_>=0 ? L"Click a key to edit your Custom pitch set. Selected notes repeat across octaves." :
            v[kMidiEnabled]>=.5 ? L"Held MIDI notes select the color pitches. Release them to return to your pitch set." :
            page_==EditorPage::body ? L"Drag to shape  /  Shift: fine  /  Right-click: type  /  Ctrl+1\u20134: pages" :
            page_==EditorPage::bands ? L"Drag the dividers   /   Draw your EQ curve   /   Shift for fine adjustment" :
            L"Drag to adjust   /   Double-click to reset   /   Right-click to enter a value";
        text(dc,help,box(32,721,827,30),inputInvalid_ ? blend(kText,kRose,.5f) : kMuted,1);
        toggle(dc,kLimitButton,L"Soft limit",v[kSoftLimit]>=.5,kWet,true);
        SetMapMode(dc,MM_TEXT); BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);
        SelectObject(dc,oldBitmap); DeleteObject(bitmap); DeleteDC(dc);
    }
    int sliderAt(int x,int y) const {
        const auto v=owner_->snapshot();
        if (page_==EditorPage::body) {
            for (std::size_t i=kBodyFirstControl;i<kSliders.size();++i)
                if (contains(kSliders[i].bounds,x,y)) return static_cast<int>(i);
            return -1;
        }
        if (page_==EditorPage::bands) {
            if (contains(kSplitLowInput,x,y) || contains(splitLabel(false,v),x,y)) return 16;
            if (contains(kSplitHighInput,x,y) || contains(splitLabel(true,v),x,y)) return 17;
            // The actual colored divider is draggable, not just its label.
            // Crossover hits win over a coincident white processing boundary;
            // that white boundary remains independently reachable at its label.
            if (y>=kPlotY && y<=kPlotY+fieldHeight()+7) {
                const int low=static_cast<int>(frequencyX(static_cast<float>(plain(kColorLowMidHz,v[kColorLowMidHz]))));
                const int high=static_cast<int>(frequencyX(static_cast<float>(std::max(plain(kColorLowMidHz,v[kColorLowMidHz])*1.25,plain(kColorMidHighHz,v[kColorMidHighHz])))));
                const int dl=std::abs(x-low), dh=std::abs(x-high);
                if (std::min(dl,dh)<=10) return dl<=dh ? 16 : 17;
            }
            if (inEq(x,y)) return kEqFocusBase+eqBandAt(x);
        }
        if (y>=126 && y<=kPlotY+fieldHeight()+8) {
            const int low=static_cast<int>(frequencyX(static_cast<float>(plain(kLowHz,v[kLowHz]))));
            const int high=static_cast<int>(frequencyX(static_cast<float>(std::max(plain(kLowHz,v[kLowHz])+20,plain(kHighHz,v[kHighHz])))));
            if (std::abs(x-low)<=9 || contains(boundaryLabel(false,v),x,y)) return 4;
            if (std::abs(x-high)<=9 || contains(boundaryLabel(true,v),x,y)) return 5;
        }
        for (std::size_t i=0; i<kSliders.size(); ++i)
            if (visibleControl(i) && !horizontalControl(kSliders[i].kind) &&
                contains(control(i).bounds,x,y)) return static_cast<int>(i);
        return -1;
    }
    void finishGesture() {
        if (dragging_>=0) {
            const auto id=kSliders[dragging_].id; dragging_=-1; owner_->endEdit(id);
        }
        if (eqPainting_) {
            eqPainting_=false;
            for (int i=0;i<kEqBands;++i) if (eqGestures_[i]) {
                eqGestures_[i]=false; owner_->endEdit(eqParam(i));
            }
            owner_->finishGroupEdit();
        }
        if (window_ && GetCapture()==window_) ReleaseCapture();
    }
    void editEqBand(int band,double db) {
        const auto id=eqParam(band);
        if (!eqGestures_[band]) { owner_->beginEdit(id); eqGestures_[band]=true; }
        const double value=normalized(id,db);
        owner_->setParamNormalized(id,value); owner_->performEdit(id,value);
    }
    void paintEqAt(int x,int y) {
        const int band=eqBandAt(x); const double db=eqDbAtY(y);
        const int count=std::abs(band-eqLastBand_), direction=band>=eqLastBand_ ? 1 : -1;
        if (count==0) editEqBand(band,db);
        else for (int step=1;step<=count;++step)
            editEqBand(eqLastBand_+direction*step,eqLastDb_+(db-eqLastDb_)*step/count);
        eqLastBand_=eqSelected_=band; eqLastDb_=db; focused_=kEqFocusBase+band;
        InvalidateRect(window_,nullptr,FALSE);
    }
    void beginEqPaint(int x,int y) {
        finishGesture(); eqGestures_.fill(false); eqPainting_=true;
        eqLastBand_=eqBandAt(x); eqLastDb_=eqDbAtY(y);
        owner_->startGroupEdit(); SetCapture(window_); paintEqAt(x,y);
    }
    void flatEq() {
        finishGesture(); owner_->startGroupEdit();
        for (int i=0;i<kEqBands;++i) owner_->edit(eqParam(i),normalized(eqParam(i),0));
        for (const auto id:{kColorEqLowDb,kColorEqMidDb,kColorEqHighDb}) owner_->edit(id,normalized(id,0));
        owner_->finishGroupEdit(); InvalidateRect(window_,nullptr,FALSE);
    }
    double boundedValue(Param id,double value) {
        value=sanitize(id,value);
        if (id==kLowHz) value=normalized(id,std::min(plain(id,value),plain(kHighHz,owner_->getParamNormalized(kHighHz))-20));
        if (id==kHighHz) value=normalized(id,std::max(plain(id,value),plain(kLowHz,owner_->getParamNormalized(kLowHz))+20));
        if (id==kColorLowMidHz) value=normalized(id,std::min(plain(id,value),plain(kColorMidHighHz,owner_->getParamNormalized(kColorMidHighHz))/1.25));
        if (id==kColorMidHighHz) value=normalized(id,std::max(plain(id,value),plain(kColorLowMidHz,owner_->getParamNormalized(kColorLowMidHz))*1.25));
        return value;
    }
    void gestureValue(double value) {
        const auto id=kSliders[dragging_].id; value=boundedValue(id,value);
        owner_->setParamNormalized(id,value); owner_->performEdit(id,value); InvalidateRect(window_,nullptr,FALSE);
    }
    bool finishInput(bool apply) {
        if (!valueEdit_) return true;
        if (apply) {
            wchar_t content[128]{}; GetWindowTextW(valueEdit_,content,128);
            double number=0;
            if (!editor::parseNumber(content,kSpecs[inputParam_].units,number)) {
                inputInvalid_=true; InvalidateRect(valueEdit_,nullptr,TRUE);
                InvalidateRect(window_,nullptr,FALSE); SetFocus(valueEdit_); return false;
            }
            owner_->edit(inputParam_,boundedValue(inputParam_,normalized(inputParam_,number)));
        }
        const bool ownedFocus=GetFocus()==valueEdit_;
        HWND edit=valueEdit_; valueEdit_=nullptr; inputInvalid_=false;
        if (oldEditProc_) SetWindowLongPtrW(edit,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(oldEditProc_));
        oldEditProc_=nullptr; DestroyWindow(edit);
        if (inputFont_) { DeleteObject(inputFont_); inputFont_=nullptr; }
        if (inputBrush_) { DeleteObject(inputBrush_); inputBrush_=nullptr; }
        if (window_) { if (ownedFocus) SetFocus(window_); InvalidateRect(window_,nullptr,FALSE); }
        return true;
    }
    RECT entryRect(Param id) const {
        if (id==kClarity) return box(878,405,130,31);
        if (id==kColorLowMidHz) return kSplitLowInput;
        if (id==kColorMidHighHz) return kSplitHighInput;
        if (id>=kWetEqBand0 && id<kWetEqBand0+kEqBands) return kEqValueInput;
        for (std::size_t i=0;i<kSliders.size();++i) if (kSliders[i].id==id) {
            const auto s=control(i);
            if (s.kind==ControlKind::lowBoundary || s.kind==ControlKind::highBoundary)
                return boundaryLabel(s.kind==ControlKind::highBoundary,owner_->snapshot());
            return box((s.bounds.left+s.bounds.right)/2-59,(s.bounds.top+s.bounds.bottom)/2-15,118,30);
        }
        return box(460,350,120,30);
    }
    void startInput(Param id) {
        finishInput(false); finishGesture(); inputParam_=id; inputInvalid_=false;
        if (id>=kWetEqBand0 && id<kWetEqBand0+kEqBands) {
            eqSelected_=static_cast<int>(id-kWetEqBand0); focused_=kEqFocusBase+eqSelected_;
        }
        const auto r=entryRect(id); RECT client{}; GetClientRect(window_,&client);
        wchar_t value[64]{}; const auto number=plain(id,owner_->getParamNormalized(id));
        std::swprintf(value,64,L"%.3f",number);
        std::wstring initial(value);
        while (!initial.empty() && initial.back()==L'0') initial.pop_back();
        if (!initial.empty() && initial.back()==L'.') initial.pop_back();
        valueEdit_=CreateWindowExW(0,L"EDIT",initial.c_str(),WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_CENTER,
            r.left*client.right/kEditorWidth,r.top*client.bottom/kEditorHeight,
            (r.right-r.left)*client.right/kEditorWidth,(r.bottom-r.top)*client.bottom/kEditorHeight,
            window_,reinterpret_cast<HMENU>(static_cast<UINT_PTR>(1000+id)),ghInst,nullptr);
        if (!valueEdit_) return;
        SetWindowLongPtrW(valueEdit_,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));
        oldEditProc_=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(valueEdit_,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(inputProc)));
        LOGFONTW lf{}; GetObjectW(fonts_[0],sizeof(lf),&lf);
        lf.lfHeight=-static_cast<LONG>(std::lround(16.0*client.bottom/kEditorHeight));
        inputFont_=CreateFontIndirectW(&lf); inputBrush_=CreateSolidBrush(kInset);
        SendMessageW(valueEdit_,WM_SETFONT,reinterpret_cast<WPARAM>(inputFont_),TRUE);
        SendMessageW(valueEdit_,EM_SETLIMITTEXT,100,0); SendMessageW(valueEdit_,EM_SETSEL,0,-1);
        SetFocus(valueEdit_); InvalidateRect(window_,nullptr,FALSE);
    }
    static LRESULT CALLBACK inputProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
        auto* self=reinterpret_cast<NativeEditor*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if (!self || !self->oldEditProc_) return DefWindowProcW(hwnd,message,w,l);
        if (message==WM_GETDLGCODE) return DLGC_WANTALLKEYS|DLGC_WANTCHARS;
        if (message==WM_KEYDOWN) {
            if (w==VK_RETURN) { self->finishInput(true); return 0; }
            if (w==VK_ESCAPE) { self->finishInput(false); return 0; }
            if (w==VK_TAB) {
                if (self->finishInput(true)) PostMessageW(self->window_,WM_KEYDOWN,VK_TAB,0);
                return 0;
            }
        }
        if (message==WM_CHAR && (w==VK_RETURN || w==VK_ESCAPE || w==VK_TAB)) return 0;
        if (message==WM_KILLFOCUS) PostMessageW(self->window_,kCancelInputMessage,reinterpret_cast<WPARAM>(hwnd),0);
        return CallWindowProcW(self->oldEditProc_,hwnd,message,w,l);
    }
    void popup(int kind,RECT anchor) {
        finishInput(false); finishGesture();
        HMENU menu=CreatePopupMenu();
        const int count=kind==0 ? 12 : kind==2 ? iris::kPresetCount : kind==4 ? 2 : kind==5 ? 4 : 6;
        const auto v=owner_->snapshot();
        const auto param=kind==0 ? kRoot : kind==1 ? kScale : kind==4 ? kEngineMode :
            kind==5 ? kBodyMaterial : kScaleMotionDivision;
        const int current=kind==2 ? owner_->currentPresetIndex() : discrete(param,v[param]);
        for (int i=0; i<count; ++i) {
            UINT flags=MF_STRING;
            if (current==i) flags|=MF_CHECKED;
            const auto label=kind==5 ? std::wstring(kBodyLabels[i]) : kind==4 ? wide(kEngineModeNames[i]) :
                kind==3 ? std::wstring(kDivisionLabels[i]) :
                wide(kind==0 ? kNoteNames[i] : kind==1 ? kScaleNames[i] : iris::presetName(i));
            AppendMenuW(menu,flags,static_cast<UINT_PTR>(i+1),label.c_str());
        }
        RECT client{}; GetClientRect(window_,&client);
        POINT at{anchor.left*client.right/kEditorWidth,anchor.bottom*client.bottom/kEditorHeight};
        ClientToScreen(window_,&at);
        const int selected=static_cast<int>(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN|TPM_NONOTIFY,
            at.x,at.y,0,window_,nullptr))-1;
        DestroyMenu(menu); if (selected<0) return;
        if (kind==2) owner_->loadPreset(selected);
        else owner_->edit(param,normalized(param,selected));
        InvalidateRect(window_,nullptr,FALSE);
    }
    void click(int x,int y,bool reset) {
        finishInput(false);
        SetFocus(window_);
        if (contains(kMainTab,x,y)) { selectPage(EditorPage::main); return; }
        if (contains(kBodyTab,x,y) || (page_==EditorPage::main && contains(kBodySummary,x,y))) {
            selectPage(EditorPage::body); return;
        }
        if (contains(kBandsTab,x,y)) { selectPage(EditorPage::bands); return; }
        if (contains(kMotionTab,x,y)) { selectPage(EditorPage::motion); return; }
        if (contains(kEngineButton,x,y)) { popup(4,kEngineButton); return; }
        if (page_==EditorPage::body) for (int material=0;material<4;++material)
            if (contains(bodyMaterialRect(material),x,y)) {
                finishGesture(); focused_=kBodyMaterialControl;
                owner_->edit(kBodyMaterial,normalized(kBodyMaterial,reset ? 0 : material));
                InvalidateRect(window_,nullptr,FALSE); return;
            }
        const std::array<std::pair<Param,RECT>,5> toggles {{
            {kBypass,kBypassButton},{kMidiEnabled,kMidiButton},{kMorphEnabled,kMorphButton},{kGateEnabled,kGateButton},{kSoftLimit,kLimitButton}
        }};
        for (const auto& t:toggles) if (contains(t.second,x,y) &&
                (page_==EditorPage::main || (t.first!=kMorphEnabled && t.first!=kGateEnabled))) {
            owner_->edit(t.first,owner_->getParamNormalized(t.first)<.5 ? 1.0 : 0.0);
            InvalidateRect(window_,nullptr,FALSE); return;
        }
        if (page_==EditorPage::motion) {
            const std::array<std::pair<Param,RECT>,6> motionToggles {{
                {kAttackTailEnabled,kAttackToggle},{kPitchDiveEnabled,kDiveToggle},
                {kPeakTamerEnabled,kTamerToggle},{kScaleMotionEnabled,kScaleToggle},
                {kFormantEnabled,kFormantToggle},{kScaleMotionSync,kScaleSyncButton}
            }};
            for (const auto& t:motionToggles) if (contains(t.second,x,y)) {
                finishGesture(); owner_->edit(t.first,owner_->getParamNormalized(t.first)<.5 ? 1.0 : 0.0);
                InvalidateRect(window_,nullptr,FALSE); return;
            }
            if (contains(kScaleDivisionButton,x,y)) { popup(3,kScaleDivisionButton); return; }
        }
        if (page_==EditorPage::bands) {
            if (contains(kSplitLowInput,x,y)) { focused_=16; startInput(kColorLowMidHz); return; }
            if (contains(kSplitHighInput,x,y)) { focused_=17; startInput(kColorMidHighHz); return; }
            if (contains(kEqValueInput,x,y)) { startInput(eqParam(eqSelected_)); return; }
            if (contains(kEqFlatButton,x,y)) { flatEq(); return; }
            if (inEq(x,y)) {
                eqSelected_=eqBandAt(x); focused_=kEqFocusBase+eqSelected_;
                if (reset) { finishGesture(); owner_->edit(eqParam(eqSelected_),normalized(eqParam(eqSelected_),0)); }
                else beginEqPaint(x,y);
                InvalidateRect(window_,nullptr,FALSE); return;
            }
        }
        const int slider=sliderAt(x,y);
        if (slider>=0 && slider<static_cast<int>(kSliders.size())) {
            focused_=slider; const auto s=control(slider);
            if (s.kind==ControlKind::material) return;
            if (reset) { finishGesture(); owner_->edit(s.id,boundedValue(s.id,normalized(s.id,kSpecs[s.id].initial))); }
            else {
                finishGesture(); dragging_=slider; dragX_=x; dragY_=y;
                dragValue_=owner_->getParamNormalized(s.id); owner_->beginEdit(s.id); SetCapture(window_);
                if (s.kind==ControlKind::gate && !(GetKeyState(VK_SHIFT)&0x8000)) {
                    const int top=s.bounds.top+10, bottom=s.bounds.bottom-12;
                    dragValue_=std::clamp((bottom-y)/static_cast<double>(bottom-top),0.0,1.0); gestureValue(dragValue_);
                } else if (s.kind==ControlKind::clarity && x>=836 && x<=937 &&
                           !(GetKeyState(VK_SHIFT)&0x8000)) {
                    dragValue_=std::clamp((x-836)/100.0,0.0,1.0); gestureValue(dragValue_);
                }
            }
            InvalidateRect(window_,nullptr,FALSE); return;
        }
        if (contains(kRootButton,x,y)) { popup(0,kRootButton); return; }
        if (contains(kScaleButton,x,y)) { popup(1,kScaleButton); return; }
        if (contains(kPresetButton,x,y)) { popup(2,kPresetButton); return; }
        const int note=noteAt(x,y);
        if (note>=0) {
            const auto v=owner_->snapshot(); const auto mask=activeMask(v)^(1u<<note);
            owner_->startGroupEdit();
            for (unsigned i=0; i<12; ++i) owner_->edit(kCustomC+i,(mask&(1u<<i)) ? 1.0 : 0.0);
            owner_->edit(kScale,1.0); owner_->finishGroupEdit(); InvalidateRect(window_,nullptr,FALSE);
        }
    }
    void rightClick(int x,int y) {
        const int hit=sliderAt(x,y);
        if (hit==kBodyMaterialControl) {
            focused_=hit; popup(5,kSliders[kBodyMaterialControl].bounds); return;
        }
        if (eqFocus(hit)) { eqSelected_=hit-kEqFocusBase; focused_=hit; startInput(eqParam(eqSelected_)); return; }
        if (hit>=0 && hit<static_cast<int>(kSliders.size())) {
            focused_=hit; startInput(kSliders[hit].id);
        }
    }
    void drag(int x,int y,bool shift) {
        const auto s=control(dragging_); const double fine=shift ? .15 : 1.0;
        if (horizontalControl(s.kind)) {
            const double hz=plain(s.id,dragValue_)*std::exp((x-dragX_)*std::log(kVisualHigh/kVisualLow)/kPlotW*fine);
            gestureValue(normalized(s.id,hz));
        } else if (s.kind==ControlKind::gate)
            gestureValue(dragValue_+(dragY_-y)/static_cast<double>(s.bounds.bottom-s.bounds.top-22)*fine);
        else if (s.kind==ControlKind::clarity)
            gestureValue(dragValue_+(x-dragX_)/100.0*fine);
        else gestureValue(dragValue_+(dragY_-y+.25*(x-dragX_))/180.0*fine);
        // Incremental movement keeps the value continuous when Shift changes mid-drag.
        dragValue_=owner_->getParamNormalized(s.id); dragX_=x; dragY_=y;
    }
    void nextFocus(bool backwards) {
        std::array<int,kSliders.size()+kEqBands> order{}; int count=0;
        for (std::size_t i=0;i<kSliders.size();++i) if (visibleControl(i)) order[count++]=static_cast<int>(i);
        if (page_==EditorPage::bands) for (int i=0;i<kEqBands;++i) order[count++]=kEqFocusBase+i;
        int at=0;
        for (int i=0;i<count;++i) if (order[i]==focused_) { at=i; break; }
        focused_=order[(at+(backwards ? count-1 : 1))%count];
        if (eqFocus(focused_)) eqSelected_=focused_-kEqFocusBase;
        InvalidateRect(window_,nullptr,FALSE);
    }
    static LRESULT CALLBACK windowProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
        auto* self=reinterpret_cast<NativeEditor*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if (message==WM_NCCREATE) {
            self=static_cast<NativeEditor*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); self->window_=hwnd;
        }
        if (!self) return DefWindowProcW(hwnd,message,w,l);
        RECT client{}; GetClientRect(hwnd,&client);
        const int x=static_cast<int>(GET_X_LPARAM(l)*kEditorWidth/std::max(1L,client.right));
        const int y=static_cast<int>(GET_Y_LPARAM(l)*kEditorHeight/std::max(1L,client.bottom));
        switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps{}; auto dc=BeginPaint(hwnd,&ps); self->paint(dc); EndPaint(hwnd,&ps); return 0; }
        case WM_LBUTTONDOWN: self->click(x,y,false); return 0;
        case WM_LBUTTONDBLCLK: self->click(x,y,true); return 0;
        case WM_RBUTTONDOWN: self->rightClick(x,y); return 0;
        case WM_LBUTTONUP: self->finishGesture(); return 0;
        case WM_CAPTURECHANGED: self->finishGesture(); return 0;
        case WM_CANCELMODE: self->finishInput(false); self->finishGesture(); return 0;
        case kCancelInputMessage:
            if (self->valueEdit_==reinterpret_cast<HWND>(w) && GetFocus()!=self->valueEdit_) self->finishInput(false);
            return 0;
        case WM_CTLCOLOREDIT:
            if (reinterpret_cast<HWND>(l)==self->valueEdit_) {
                SetTextColor(reinterpret_cast<HDC>(w),self->inputInvalid_ ? blend(kText,kRose,.5f) : kText);
                SetBkColor(reinterpret_cast<HDC>(w),kInset);
                return reinterpret_cast<LRESULT>(self->inputBrush_);
            }
            break;
        case WM_MOUSEMOVE:
            if (self->eqPainting_) self->paintEqAt(x,y);
            else if (self->dragging_>=0) self->drag(x,y,(w&MK_SHIFT)!=0);
            else {
                const int hover=self->sliderAt(x,y), note=noteAt(x,y);
                int material=-1;
                if (self->page_==EditorPage::body) for (int m=0;m<4;++m)
                    if (contains(bodyMaterialRect(m),x,y)) { material=m; break; }
                const bool bodySummary=self->page_==EditorPage::main && contains(kBodySummary,x,y);
                self->hoverAction_=bodySummary || material>=0 || contains(kMainTab,x,y) || contains(kBodyTab,x,y) ||
                    contains(kBandsTab,x,y) || contains(kMotionTab,x,y) || contains(kEngineButton,x,y) ||
                    contains(kPresetButton,x,y) || contains(kBypassButton,x,y) || contains(kRootButton,x,y) ||
                    contains(kScaleButton,x,y) || contains(kMidiButton,x,y) || contains(kLimitButton,x,y);
                self->hoverInput_=self->page_==EditorPage::bands &&
                    (contains(kSplitLowInput,x,y) || contains(kSplitHighInput,x,y) || contains(kEqValueInput,x,y));
                const int plot=self->page_!=EditorPage::body &&
                    contains(box(kPlotX,kPlotY,kPlotW,self->fieldHeight()),x,y) && hover!=16 && hover!=17 ? x : -1;
                if (hover!=self->hover_ || note!=self->hoverNote_ || plot!=self->plotHoverX_ ||
                        material!=self->hoverMaterial_ || bodySummary!=self->hoverBodySummary_) {
                    self->hover_=hover; self->hoverNote_=note; self->plotHoverX_=plot;
                    self->hoverMaterial_=material; self->hoverBodySummary_=bodySummary;
                    InvalidateRect(hwnd,nullptr,FALSE);
                }
                TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0}; TrackMouseEvent(&track);
            }
            return 0;
        case WM_MOUSELEAVE:
            self->hover_=self->hoverNote_=self->plotHoverX_=self->hoverMaterial_=-1;
            self->hoverInput_=self->hoverAction_=self->hoverBodySummary_=false;
            InvalidateRect(hwnd,nullptr,FALSE); return 0;
        case WM_SETCURSOR:
            if (LOWORD(l)==HTCLIENT) {
                const int active=self->dragging_>=0 ? self->dragging_ : self->hover_;
                SetCursor(LoadCursorW(nullptr,self->hoverInput_ ? IDC_IBEAM :
                    self->eqPainting_ || eqFocus(active) ? IDC_CROSS :
                    self->hoverAction_ || active==kBodyMaterialControl ? IDC_HAND :
                    active>=0 && active<static_cast<int>(kSliders.size()) &&
                        (horizontalControl(kSliders[active].kind) || kSliders[active].kind==ControlKind::clarity) ? IDC_SIZEWE :
                    active>=0 ? IDC_SIZENS : self->hoverNote_>=0 ? IDC_HAND : IDC_ARROW)); return TRUE;
            }
            break;
        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)}; ScreenToClient(hwnd,&point);
            const int hit=self->sliderAt(static_cast<int>(point.x*kEditorWidth/std::max(1L,client.right)),
                                          static_cast<int>(point.y*kEditorHeight/std::max(1L,client.bottom)));
            if (hit>=0) {
                const auto id=eqFocus(hit) ? eqParam(hit-kEqFocusBase) : kSliders[hit].id;
                const double step=id==kBodyMaterial ? 1.0/3.0 : eqFocus(hit) ? ((GET_KEYSTATE_WPARAM(w)&MK_SHIFT) ? .1/36 : .5/36) :
                    (GET_KEYSTATE_WPARAM(w)&MK_SHIFT) ? .001 : .01;
                self->focused_=hit;
                if (eqFocus(hit)) self->eqSelected_=hit-kEqFocusBase;
                self->owner_->edit(id,self->boundedValue(id,self->owner_->getParamNormalized(id)+GET_WHEEL_DELTA_WPARAM(w)/120.0*step));
                InvalidateRect(hwnd,nullptr,FALSE); return 0;
            }
            break;
        }
        case WM_GETDLGCODE: return DLGC_WANTARROWS|DLGC_WANTTAB;
        case WM_KEYDOWN:
            if ((GetKeyState(VK_CONTROL)&0x8000) && w>='1' && w<='4') {
                const EditorPage pages[]={EditorPage::main,EditorPage::body,EditorPage::bands,EditorPage::motion};
                self->selectPage(pages[w-'1']); return 0;
            }
            if (w==VK_TAB) {
                self->nextFocus((GetKeyState(VK_SHIFT)&0x8000)!=0); return 0;
            }
            if (w==VK_RETURN) {
                if (self->focused_==kBodyMaterialControl) self->popup(5,kSliders[kBodyMaterialControl].bounds);
                else self->startInput(self->focusedParam());
                return 0;
            }
            if (w==VK_ESCAPE) { self->finishInput(false); self->finishGesture(); return 0; }
            if (w==VK_LEFT || w==VK_DOWN || w==VK_RIGHT || w==VK_UP || w==VK_HOME || w==VK_END) {
                const auto id=self->focusedParam(); double value=self->owner_->getParamNormalized(id);
                const double delta=id==kBodyMaterial ? 1.0/3.0 : eqFocus(self->focused_) ? ((GetKeyState(VK_SHIFT)&0x8000) ? .1/36 : .5/36) :
                    (GetKeyState(VK_SHIFT)&0x8000) ? .001 : .01;
                value=w==VK_HOME ? 0 : w==VK_END ? 1 : value+((w==VK_LEFT || w==VK_DOWN) ? -delta : delta);
                self->owner_->edit(id,self->boundedValue(id,value)); InvalidateRect(hwnd,nullptr,FALSE); return 0;
            }
            break;
        case WM_TIMER:
            if (self->updateVisual() || self->owner_->snapshot()!=self->lastPainted_) InvalidateRect(hwnd,nullptr,FALSE);
            return 0;
        case WM_SETFOCUS: case WM_KILLFOCUS: InvalidateRect(hwnd,nullptr,FALSE); return 0;
        case WM_NCDESTROY: self->window_=nullptr; SetWindowLongPtrW(hwnd,GWLP_USERDATA,0); break;
        }
        return DefWindowProcW(hwnd,message,w,l);
    }
};
IPlugView* createNativeEditor(Controller* controller) { return new NativeEditor(controller); }
}
#else
namespace iris::vst3 {
Steinberg::IPlugView* createNativeEditor(Controller*) { return nullptr; }
}
#endif
