#include "imfluent.h"
#include "imfluent_icons.h"
#include "imfluent_internal.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>

#if defined( _WIN32 )
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#endif

// [SECTION] ImFluentContext (all mutable globals)

struct ImFluentNextItemData
{
    const char * Header;
    const char * Description;
    const char * Glyph;
    const char * Error;
    bool HasHeader;
    bool HasDescription;
    bool HasGlyph;
    bool HasError;
    ImGuiWindow * Window; // Where and when SetNextItem*() ran: a widget that skips itself (clipped) never consumes
    int Frame;            // the data, and it must not leak to an unrelated widget later
};

struct ImFluentNextAutoSuggestData
{
    ImFluentAutoSuggestPredicate Predicate;
    void * UserData;
    bool HasPredicate;
};

struct ImFluentAutoSuggestScoredItem
{
    int Index;
    int Score;
};

struct ImFluentNextTextBoxData
{
    ImGuiInputTextCallback UserCallback;
    void * UserCallbackData;
    bool HasUserCallback;
};

struct ImFluentColorStackEntry
{
    ImFluentCol Idx;
    ImVec4 Prev;
};

enum ImFluentStyleVarKind
{
    ImFluentStyleVarKind_Float,
    ImFluentStyleVarKind_Vec2
};
struct ImFluentStyleVarStackEntry
{
    ImFluentStyleVar Idx;
    ImFluentStyleVarKind Kind;
    ImVec2 Prev;
};

struct ImFluentWrapPanelState
{
    float Avail;
    float HSpacing;
    float VSpacing;
    float CursorX;
    bool First;
};

struct ImFluentExpanderState
{
    char Label[ 128 ];
    bool * Open;
    ImFluentExpandDirection Direction;
    bool BodyActive;
};

struct ImFluentNavViewState
{
    bool InTabGroup; // Between BeginNavigationView and EndNavigationView: menu items form one tab stop
    bool FooterItem; // The item being submitted is a footer item (Settings): its own tab stop
    float CurrentWidth;
    bool ContentStarted;
    ImFluentNavViewMode Mode;
    ImFluentNavViewMode PrevMode;
    bool NextToggleVisible;
    bool NextToggleVisibleSet;
    bool MenuRegionOpen;       // Left modes: the menu items' scrolling region is open (see BeginNavMenuRegion)
    float FooterStartY = -1.f; // Pane cursor y where the footer items (Settings) start, -1 without footer
};

struct ImFluentPopupAnchor
{
    ImGuiID Id;
    ImRect Rect;
};

struct ImFluentCommandBarState
{
    char Id[ 64 ];
    char PopupId[ 80 ];
    float Height;
    bool HasItem              = false;
    bool SuppressAutoSameLine = false;
    bool IconOnly             = false; // CommandBarFlyout primary commands: no labels
    ImGuiWindow * Window      = NULL;  // The bar's child window (the overflow popup is another window)
    ImGuiID PrevFirst         = 0;    // First and last commands of the previous frame: Left/Right stop there
    ImGuiID PrevLast          = 0;
    ImGuiID First             = 0;
    ImGuiID Last              = 0;
    bool OverflowFocusLast    = false; // Opened with Up: focus the last overflow item (WinUI)
};

struct ImFluentSplitViewState
{
    char Id[ 64 ];
    bool * IsPaneOpen;
    ImFluentSplitViewDisplayMode Mode;
    ImFluentSplitViewPanePlacement Placement;
    float OpenPaneWidth;
    float CompactPaneWidth;
    float CurrentPaneWidth;
    float OuterStartCursorX;
    float OuterStartCursorY;
    float ContentWidth;
    float Height;
    bool PaneDone;
    bool ContentDone;
};

struct ImFluentTextStyleFont
{
    ImFont * Font;
    float FontSize;
};

// A control that is ONE tab stop (WinUI TabFocusNavigation="Once"): Tab lands on the selected, else last focused, else first item.
enum ImFluentTabGroupFlags_
{
    ImFluentTabGroupFlags_None                  = 0,
    ImFluentTabGroupFlags_Vertical              = 1 << 0, // A column: Up/Down stop at the ends, Left/Right do nothing
    ImFluentTabGroupFlags_Horizontal            = 1 << 1, // A row: Left/Right stop at the ends, Up/Down do nothing
    ImFluentTabGroupFlags_Wrap                  = 1 << 2, // With Vertical/Horizontal: the ends wrap around instead
    ImFluentTabGroupFlags_SelectionFollowsFocus = 1 << 3, // Arrow keys select the item they focus (not with Ctrl)
    ImFluentTabGroupFlags_HomeEnd               = 1 << 4, // Home/End focus the first/last item
    ImFluentTabGroupFlags_FirstIsStop           = 1 << 5, // The first item is always the tab stop (BreadcrumbBar)
    ImFluentTabGroupFlags_Grid                  = 1 << 6, // Arrows move to the nearest item of the group (GridView)
    ImFluentTabGroupFlags_ItemsContainer        = 1 << 7, // BeginListView/GridView/TreeView: their items join the group
    ImFluentTabGroupFlags_EnterAtEnds           = 1 << 8, // Without a selection: Tab enters on the first item, Shift+Tab on the last (ItemsRepeater)
    ImFluentTabGroupFlags_EnterOnLastFocused    = 1 << 9, // Tab enters on the last focused item, before the selected one (Selector: ListBox, TabView)
};

// Item rects of a Grid tab group, kept from the previous frame: arrows move between them. A TabView keeps its Left/Right order here.
struct ImFluentTabGroupLayout
{
    ImVector< ImGuiID > Items;
    ImVector< ImRect > Rects;
    ImVector< ImGuiID > NextItems; // This frame
    ImVector< ImRect > NextRects;
};

struct ImFluentTabGroup
{
    ImGuiID Id;
    int Flags;
    int LayoutIdx;     // Grid groups: index in ImFluentContext::TabGroupLayouts, else -1
    ImGuiID Stop;      // Item that is the tab stop this frame, decided from the previous frame
    ImGuiID PrevFirst; // First and last items of the previous frame
    ImGuiID PrevLast;
    ImGuiID Pending;      // Item that Home/End or a wrap moved focus to: scrolled into view when it is submitted
    bool PendingSelect;   // ...and selected (SelectionFollowsFocus without Ctrl)
    ImGuiID LastSelected; // The selected item last submitted, kept while it is hidden
    ImGuiID First;        // This frame
    ImGuiID Last;
    ImGuiID Selected;
    ImGuiID SelectedAncestor; // A collapsed item that hides the selected item (TabGroupHiddenSelection)
    ImGuiID Focused;
    bool StopSeen;
};

struct ImFluentBackdropState
{
    static const int TexSize = 96; // One monitor; non-square texels

    ImFluentBackdrop Kind = ImFluentBackdrop_None;
    ImFluentBackdropHooks Hooks;
    ImTextureData * Texture         = NULL; // RegisterUserTexture; the renderer backend uploads it
    bool HasImage                   = false;
    unsigned int BuiltSerial        = 0; // What the texture was built from
    ImFluentBackdrop BuiltKind      = -1;
    ImFluentThemePreset BuiltPreset = -1;
    ImVec2 BuiltMonitorSize;
    double NextPoll                  = 0.0;
    float Active                     = 1.f; // 0 when the app is not focused
    bool Native                      = false;
    ImFluentBackdrop NativeKind      = -1;
    ImFluentThemePreset NativePreset = -1;
};

struct ImFluentContext
{
    static const int MaxPopupAnchors = 16;

    ImFluentThemePreset Preset;
    ImFluentStyle Style;
    ImFluentActions Actions;
    ImFluentBackdropState Backdrop;
    ImFluentTextStyleFont Fonts[ ImFluentTextStyle_COUNT ];
    ImFont * IconFont = NULL; // INKEYS PATCH: 图标与正文的度量分离。

    ImFluentNextItemData NextItem;
    ImFluentNextAutoSuggestData NextAutoSuggest;
    ImFluentNextTextBoxData NextTextBox;

    ImVector< ImFluentColorStackEntry > ColorStack;
    ImVector< ImFluentStyleVarStackEntry > StyleVarStack;
    ImVector< ImFluentWrapPanelState > WrapPanelStack;
    ImVector< ImFluentExpanderState > ExpanderStack;
    ImVector< ImFluentCommandBarState > CommandBarStack;
    ImVector< float > NavItemIndentStack;
    ImVector< ImGuiID > NavItemParentStack; // Expanded BeginNavItem ids, with NavItemIndentStack
    ImVector< ImFluentTabGroup > TabGroupStack;
    ImPool< ImFluentTabGroupLayout > TabGroupLayouts;

    struct FlipViewState
    {
        ImGuiID Id;
        const char * Label;
        int * Current;
        int Count;
    };
    ImVector< FlipViewState > FlipViewStack;

    struct MenuBarState
    {
        static const int MaxItems = 16;
        bool InPage               = false; // BeginMenuBar( id ), else the application's main menu bar
        ImGuiID Id                = 0;
        int Count                 = 0;
        int OpenIndex             = -1; // The item whose menu is open this frame
        int PrevOpenIndex         = -1; // ...and last frame
        int SwitchDir             = 0;  // Left/Right inside an open menu: open the neighbouring menu
        ImGuiID ItemIds[ MaxItems ];
        ImGuiID PopupIds[ MaxItems ];
        ImRect Rects[ MaxItems ];
    };
    ImVector< MenuBarState > MenuBarStack;
    bool InRadioButtons;                  // RadioButtons() moves focus itself: its items are not loose radio buttons
    ImDrawListSplitter TextFrameSplitter; // A text control's fill goes under the text ImGui draws (see BeginTextControlFrame)
    ImVector< ImGuiID > TreeParentStack; // Open TreeNode ids (TreePop pops): Left on a child focuses its parent
    ImGuiID TreeFocusFirstChildOf = 0;   // Right on an expanded TreeNode: its first child takes focus

    struct TypeAhead
    {
        ImGuiID Id      = 0;
        char Buf[ 64 ]  = {};
        int Len         = 0;
        double LastTime = -1.0;
    } ComboTypeAhead;
    ImVector< ImFluentSplitViewState > SplitViewStack;
    ImVector< ImFluentAutoSuggestScoredItem > AutoSuggestVisible;

    ImFluentNavViewState NavView;

    ImFluentPopupAnchor PopupAnchors[ MaxPopupAnchors ];
    int PopupAnchorCount;

    ImFluentAppBarLabelPosition NextAppBarLabelPos;
    bool NextAppBarLabelPosSet;

    bool HasUserAccent;
    ImVec4 UserAccent;

    ImFluentContext()
        : Preset( ImFluentThemePreset_Dark )
        , PopupAnchorCount( 0 )
        , NextAppBarLabelPos( ImFluentAppBarLabelPosition_Bottom )
        , NextAppBarLabelPosSet( false )
        , HasUserAccent( false )
        , UserAccent( 0.f, 0.f, 0.f, 1.f )
    {
        for ( int i = 0; i < ImFluentTextStyle_COUNT; ++i )
            Fonts[ i ] = ImFluentTextStyleFont();
        NextItem        = ImFluentNextItemData();
        NextAutoSuggest = ImFluentNextAutoSuggestData();
        NextTextBox     = ImFluentNextTextBoxData();
        NavView         = ImFluentNavViewState();
    }
};

static ImFluentContext g_Ctx;

// INKEYS PATCH: ImFluent 当前使用进程级 context，Setting shutdown 时需显式清空。
void ImFluent::ResetContext()
{
    g_Ctx.~ImFluentContext();
    IM_PLACEMENT_NEW( &g_Ctx ) ImFluentContext();
}

// [SECTION] Internal helpers (palette, animation, draw primitives, focus, fills)

namespace ImFluent
{
    static void FillCommon( ImFluentStyle & s )
    {
        s.ControlCornerRadius       = 4.f;
        s.OverlayCornerRadius       = 8.f;
        s.ControlHeight             = 32.f;
        s.ControlMinWidth           = 120.f;
        s.FocusStrokeThicknessOuter = 2.f;
        s.FocusStrokeThicknessInner = 1.f;
        s.CardPadding               = 16.f;
        s.NavPaneCompactWidth       = 48.f;
        s.NavPaneOpenWidth          = 320.f;
        s.ControlContentPadding     = ImVec2( 11.f, 5.f );

        s.SpacingXSmall  = 2.f;
        s.SpacingSmall   = 4.f;
        s.SpacingMedium  = 8.f;
        s.SpacingLarge   = 12.f;
        s.SpacingXLarge  = 16.f;
        s.SpacingXXLarge = 24.f;

        s.StrokeThin   = 1.f;
        s.StrokeMedium = 1.5f;
        s.StrokeThick  = 2.f;

        s.ChevronGlyphSize = 4.f;
        s.StandardIconSize = 16.f;

        s.CheckboxSize               = 20.f;
        s.RadioButtonDiameter        = 20.f;
        s.ToggleSwitchWidth          = 40.f;
        s.ToggleSwitchHeight         = 20.f;
        s.ToggleSwitchThumbRadiusOff = 6.f;
        s.ToggleSwitchThumbRadiusOn  = 7.f;
        s.SliderTrackHeight          = 4.f;
        s.SliderThumbRadius          = 9.f;
        s.SliderThumbInnerRadius     = 6.f;
        s.ProgressBarHeight          = 4.f;
        s.ProgressRingThickness      = 3.f;
        s.RatingStarSize             = 20.f;

        s.NavItemHeight      = 36.f;
        s.MenuItemHeight     = 32.f;
        s.ListItemHeight     = 40.f;
        s.TitleBarHeight     = 40.f;
        s.AppBarButtonWidth  = 64.f;
        s.AppBarButtonHeight = 56.f;
        s.SpinButtonWidth    = 28.f;
        s.RevealButtonWidth  = 32.f;
        s.BadgeHeight        = 16.f;
        s.PipDotSize         = 8.f;

        s.SeverityBarThickness         = 4.f;
        s.SelectionIndicatorThickness  = 3.f;
        s.SelectionIndicatorInset      = 2.f;
        s.TextInputAccentLineThickness = 2.f;

        const char ** loc                              = s.LocalizationTable;
        loc[ ImFluentLocKey_AutoSuggestNoSuggestions ] = "No suggestions";
        loc[ ImFluentLocKey_DatePickerPickADate ]      = "Pick a date";
        loc[ ImFluentLocKey_DatePickerDayFormat ]      = "Day %d";
        loc[ ImFluentLocKey_DatePickerYearFormat ]     = "Year %d";
        loc[ ImFluentLocKey_TimePickerHourFormat ]     = "%02d h";
        loc[ ImFluentLocKey_TimePickerMinuteFormat ]   = "%02d min";
        loc[ ImFluentLocKey_MonthJanuary ]             = "January";
        loc[ ImFluentLocKey_MonthFebruary ]            = "February";
        loc[ ImFluentLocKey_MonthMarch ]               = "March";
        loc[ ImFluentLocKey_MonthApril ]               = "April";
        loc[ ImFluentLocKey_MonthMay ]                 = "May";
        loc[ ImFluentLocKey_MonthJune ]                = "June";
        loc[ ImFluentLocKey_MonthJuly ]                = "July";
        loc[ ImFluentLocKey_MonthAugust ]              = "August";
        loc[ ImFluentLocKey_MonthSeptember ]           = "September";
        loc[ ImFluentLocKey_MonthOctober ]             = "October";
        loc[ ImFluentLocKey_MonthNovember ]            = "November";
        loc[ ImFluentLocKey_MonthDecember ]            = "December";
        loc[ ImFluentLocKey_DayMon ]                   = "Mo";
        loc[ ImFluentLocKey_DayTue ]                   = "Tu";
        loc[ ImFluentLocKey_DayWed ]                   = "We";
        loc[ ImFluentLocKey_DayThu ]                   = "Th";
        loc[ ImFluentLocKey_DayFri ]                   = "Fr";
        loc[ ImFluentLocKey_DaySat ]                   = "Sa";
        loc[ ImFluentLocKey_DaySun ]                   = "Su";
    }

    static void BuildDarkPalette( ImFluentStyle & s )
    {
        ImVec4 * c = s.Colors;

        c[ ImFluentCol_TextPrimary ]   = ImColor( 255, 255, 255 );
        c[ ImFluentCol_TextSecondary ] = ImColor( 255, 255, 255, 197 );
        c[ ImFluentCol_TextTertiary ]  = ImColor( 255, 255, 255, 135 );
        c[ ImFluentCol_TextDisabled ]  = ImColor( 255, 255, 255, 93 );

        c[ ImFluentCol_TextOnAccentPrimary ]   = ImColor( 0, 0, 0 );
        c[ ImFluentCol_TextOnAccentSecondary ] = ImColor( 0, 0, 0, 128 );
        c[ ImFluentCol_TextOnAccentDisabled ]  = ImColor( 255, 255, 255, 135 );
        c[ ImFluentCol_TextOnAccentSelected ]  = ImColor( 255, 255, 255 );
        c[ ImFluentCol_AccentTextPrimary ]     = ImColor( 153, 235, 255 );
        c[ ImFluentCol_AccentTextSecondary ]   = ImColor( 118, 215, 255 );
        c[ ImFluentCol_AccentTextTertiary ]    = ImColor( 96, 205, 255 );
        c[ ImFluentCol_AccentTextDisabled ]    = ImColor( 255, 255, 255, 93 );

        c[ ImFluentCol_ControlFillDefault ]        = ImColor( 255, 255, 255, 15 );
        c[ ImFluentCol_ControlFillSecondary ]      = ImColor( 255, 255, 255, 21 );
        c[ ImFluentCol_ControlFillTertiary ]       = ImColor( 255, 255, 255, 8 );
        c[ ImFluentCol_ControlFillQuarternary ]    = ImColor( 255, 255, 255, 15 );
        c[ ImFluentCol_ControlFillDisabled ]       = ImColor( 255, 255, 255, 11 );
        c[ ImFluentCol_ControlFillTransparent ]    = ImColor( 0, 0, 0, 0 );
        c[ ImFluentCol_ControlFillInputActive ]    = ImColor( 30, 30, 30, 179 );
        c[ ImFluentCol_ControlAltFillTransparent ] = ImColor( 0, 0, 0, 0 );
        c[ ImFluentCol_ControlAltFillSecondary ]   = ImColor( 0, 0, 0, 25 );
        c[ ImFluentCol_ControlAltFillTertiary ]    = ImColor( 255, 255, 255, 11 );
        c[ ImFluentCol_ControlAltFillQuarternary ] = ImColor( 255, 255, 255, 18 );
        c[ ImFluentCol_ControlAltFillDisabled ]    = ImColor( 0, 0, 0, 0 );
        c[ ImFluentCol_ControlSolidFillDefault ]   = ImColor( 69, 69, 69 );
        c[ ImFluentCol_ControlStrongFillDefault ]  = ImColor( 255, 255, 255, 139 );
        c[ ImFluentCol_ControlStrongFillDisabled ] = ImColor( 255, 255, 255, 63 );

        c[ ImFluentCol_SubtleFillTransparent ] = ImColor( 255, 255, 255, 0 );
        c[ ImFluentCol_SubtleFillSecondary ]   = ImColor( 255, 255, 255, 15 );
        c[ ImFluentCol_SubtleFillTertiary ]    = ImColor( 255, 255, 255, 10 );
        c[ ImFluentCol_SubtleFillDisabled ]    = ImColor( 255, 255, 255, 0 );

        c[ ImFluentCol_AccentFillDefault ]        = ImColor( 76, 194, 255 );
        c[ ImFluentCol_AccentFillSecondary ]      = ImColor( 76, 194, 255, 230 );
        c[ ImFluentCol_AccentFillTertiary ]       = ImColor( 76, 194, 255, 204 );
        c[ ImFluentCol_AccentFillDisabled ]       = ImColor( 255, 255, 255, 40 );
        c[ ImFluentCol_AccentFillSelectedTextBg ] = ImColor( 0, 103, 192 );

        c[ ImFluentCol_CardBgDefault ]      = ImColor( 255, 255, 255, 13 );
        c[ ImFluentCol_CardBgSecondary ]    = ImColor( 255, 255, 255, 8 );
        c[ ImFluentCol_LayerFillDefault ]   = ImColor( 58, 58, 58, 76 );
        c[ ImFluentCol_LayerFillAlt ]       = ImColor( 44, 44, 44 );
        c[ ImFluentCol_SmokeFill ]          = ImColor( 0, 0, 0, 77 );
        c[ ImFluentCol_AcrylicFill ]        = ImColor( 44, 44, 44, 220 );
        c[ ImFluentCol_SolidBgBase ]        = ImColor( 32, 32, 32 );
        c[ ImFluentCol_SolidBgQuarternary ] = ImColor( 44, 44, 44 );

        c[ ImFluentCol_CardStrokeDefault ]              = ImColor( 0, 0, 0, 25 );
        c[ ImFluentCol_CardStrokeSolid ]                = ImColor( 28, 28, 28 );
        c[ ImFluentCol_ControlStrokeDefault ]           = ImColor( 255, 255, 255, 18 );
        c[ ImFluentCol_ControlStrokeSecondary ]         = ImColor( 255, 255, 255, 24 );
        c[ ImFluentCol_ControlStrokeOnAccentDefault ]   = ImColor( 255, 255, 255, 20 );
        c[ ImFluentCol_ControlStrokeOnAccentSecondary ] = ImColor( 0, 0, 0, 35 );
        c[ ImFluentCol_ControlStrongStrokeDefault ]     = ImColor( 255, 255, 255, 139 );
        c[ ImFluentCol_ControlStrongStrokeDisabled ]    = ImColor( 255, 255, 255, 40 );
        c[ ImFluentCol_SurfaceStrokeDefault ]           = ImColor( 117, 117, 117, 102 );
        c[ ImFluentCol_SurfaceStrokeFlyout ]            = ImColor( 0, 0, 0, 51 );
        c[ ImFluentCol_DividerStrokeDefault ]           = ImColor( 255, 255, 255, 21 );

        c[ ImFluentCol_FocusStrokeOuter ] = ImColor( 255, 255, 255 );
        c[ ImFluentCol_FocusStrokeInner ] = ImColor( 0, 0, 0, 179 );

        c[ ImFluentCol_ElevationControlTop ]               = ImColor( 255, 255, 255, 24 );
        c[ ImFluentCol_ElevationControlBottom ]            = ImColor( 255, 255, 255, 18 );
        c[ ImFluentCol_ElevationTextControlTop ]           = ImColor( 255, 255, 255, 18 );
        c[ ImFluentCol_ElevationTextControlBottom ]        = ImColor( 255, 255, 255, 139 );
        c[ ImFluentCol_ElevationTextControlFocusedBottom ] = ImColor( 76, 194, 255 );
        c[ ImFluentCol_ElevationAccentTop ]                = ImColor( 255, 255, 255, 20 );
        c[ ImFluentCol_ElevationAccentBottom ]             = ImColor( 0, 0, 0, 35 );

        c[ ImFluentCol_SystemFillSuccess ]   = ImColor( 108, 203, 95 );
        c[ ImFluentCol_SystemFillCaution ]   = ImColor( 252, 225, 0 );
        c[ ImFluentCol_SystemFillCritical ]  = ImColor( 255, 153, 164 );
        c[ ImFluentCol_SystemFillNeutral ]   = ImColor( 255, 255, 255, 139 );
        c[ ImFluentCol_SystemFillAttention ] = ImColor( 96, 205, 255 );
    }

    static void BuildLightPalette( ImFluentStyle & s )
    {
        BuildDarkPalette( s );
        ImVec4 * c                             = s.Colors;
        c[ ImFluentCol_TextPrimary ]           = ImColor( 0, 0, 0, 228 );
        c[ ImFluentCol_TextSecondary ]         = ImColor( 0, 0, 0, 158 );
        c[ ImFluentCol_TextTertiary ]          = ImColor( 0, 0, 0, 114 );
        c[ ImFluentCol_TextDisabled ]          = ImColor( 0, 0, 0, 92 );
        c[ ImFluentCol_TextOnAccentPrimary ]   = ImColor( 255, 255, 255 );
        c[ ImFluentCol_TextOnAccentSecondary ] = ImColor( 255, 255, 255, 179 );
        c[ ImFluentCol_TextOnAccentDisabled ]  = ImColor( 255, 255, 255 );
        c[ ImFluentCol_TextOnAccentSelected ]  = ImColor( 255, 255, 255 );
        c[ ImFluentCol_AccentTextPrimary ]     = ImColor( 0, 90, 158 );
        c[ ImFluentCol_AccentTextSecondary ]   = ImColor( 0, 78, 140 );
        c[ ImFluentCol_AccentTextTertiary ]    = ImColor( 0, 103, 192 );
        c[ ImFluentCol_AccentTextDisabled ]    = ImColor( 0, 0, 0, 92 );

        c[ ImFluentCol_ControlFillDefault ]        = ImColor( 255, 255, 255, 179 );
        c[ ImFluentCol_ControlFillSecondary ]      = ImColor( 249, 249, 249, 128 );
        c[ ImFluentCol_ControlFillTertiary ]       = ImColor( 249, 249, 249, 77 );
        c[ ImFluentCol_ControlFillQuarternary ]    = ImColor( 243, 243, 243, 194 );
        c[ ImFluentCol_ControlFillDisabled ]       = ImColor( 249, 249, 249, 77 );
        c[ ImFluentCol_ControlFillInputActive ]    = ImColor( 255, 255, 255 );
        c[ ImFluentCol_ControlAltFillTransparent ] = ImColor( 0, 0, 0, 0 );
        c[ ImFluentCol_ControlAltFillSecondary ]   = ImColor( 0, 0, 0, 6 );
        c[ ImFluentCol_ControlAltFillTertiary ]    = ImColor( 0, 0, 0, 15 );
        c[ ImFluentCol_ControlAltFillQuarternary ] = ImColor( 0, 0, 0, 24 );
        c[ ImFluentCol_ControlAltFillDisabled ]    = ImColor( 0, 0, 0, 0 );
        c[ ImFluentCol_ControlSolidFillDefault ]   = ImColor( 255, 255, 255 );
        c[ ImFluentCol_ControlStrongFillDefault ]  = ImColor( 0, 0, 0, 114 );
        c[ ImFluentCol_ControlStrongFillDisabled ] = ImColor( 0, 0, 0, 81 );

        c[ ImFluentCol_SubtleFillTransparent ] = ImColor( 0, 0, 0, 0 );
        c[ ImFluentCol_SubtleFillSecondary ]   = ImColor( 0, 0, 0, 9 );
        c[ ImFluentCol_SubtleFillTertiary ]    = ImColor( 0, 0, 0, 6 );
        c[ ImFluentCol_SubtleFillDisabled ]    = ImColor( 0, 0, 0, 0 );

        c[ ImFluentCol_AccentFillDefault ]        = ImColor( 0, 103, 192 );
        c[ ImFluentCol_AccentFillSecondary ]      = ImColor( 0, 103, 192, 230 );
        c[ ImFluentCol_AccentFillTertiary ]       = ImColor( 0, 103, 192, 204 );
        c[ ImFluentCol_AccentFillDisabled ]       = ImColor( 0, 0, 0, 55 );
        c[ ImFluentCol_AccentFillSelectedTextBg ] = ImColor( 0, 103, 192 );

        c[ ImFluentCol_CardBgDefault ]      = ImColor( 255, 255, 255, 179 );
        c[ ImFluentCol_CardBgSecondary ]    = ImColor( 246, 246, 246, 128 );
        c[ ImFluentCol_LayerFillDefault ]   = ImColor( 255, 255, 255, 128 );
        c[ ImFluentCol_LayerFillAlt ]       = ImColor( 255, 255, 255 );
        c[ ImFluentCol_SmokeFill ]          = ImColor( 0, 0, 0, 77 );
        c[ ImFluentCol_AcrylicFill ]        = ImColor( 252, 252, 252, 220 );
        c[ ImFluentCol_SolidBgBase ]        = ImColor( 243, 243, 243 );
        c[ ImFluentCol_SolidBgQuarternary ] = ImColor( 238, 238, 238 );

        c[ ImFluentCol_CardStrokeDefault ]              = ImColor( 0, 0, 0, 15 );
        c[ ImFluentCol_CardStrokeSolid ]                = ImColor( 235, 235, 235 );
        c[ ImFluentCol_ControlStrokeDefault ]           = ImColor( 0, 0, 0, 15 );
        c[ ImFluentCol_ControlStrokeSecondary ]         = ImColor( 0, 0, 0, 41 );
        c[ ImFluentCol_ControlStrokeOnAccentDefault ]   = ImColor( 255, 255, 255, 20 );
        c[ ImFluentCol_ControlStrokeOnAccentSecondary ] = ImColor( 0, 0, 0, 102 );
        c[ ImFluentCol_ControlStrongStrokeDefault ]     = ImColor( 0, 0, 0, 114 );
        c[ ImFluentCol_ControlStrongStrokeDisabled ]    = ImColor( 0, 0, 0, 55 );
        c[ ImFluentCol_SurfaceStrokeDefault ]           = ImColor( 117, 117, 117, 102 );
        c[ ImFluentCol_SurfaceStrokeFlyout ]            = ImColor( 0, 0, 0, 15 );
        c[ ImFluentCol_DividerStrokeDefault ]           = ImColor( 0, 0, 0, 15 );

        c[ ImFluentCol_FocusStrokeOuter ] = ImColor( 0, 0, 0, 228 );
        c[ ImFluentCol_FocusStrokeInner ] = ImColor( 255, 255, 255, 179 );

        c[ ImFluentCol_ElevationControlTop ]               = ImColor( 0, 0, 0, 15 );
        c[ ImFluentCol_ElevationControlBottom ]            = ImColor( 0, 0, 0, 41 );
        c[ ImFluentCol_ElevationTextControlTop ]           = ImColor( 0, 0, 0, 15 );
        c[ ImFluentCol_ElevationTextControlBottom ]        = ImColor( 0, 0, 0, 114 );
        c[ ImFluentCol_ElevationTextControlFocusedBottom ] = ImColor( 0, 103, 192 );
        c[ ImFluentCol_ElevationAccentTop ]                = ImColor( 255, 255, 255, 20 );
        c[ ImFluentCol_ElevationAccentBottom ]             = ImColor( 0, 0, 0, 102 );
        c[ ImFluentCol_SystemFillSuccess ]                 = ImColor( 15, 123, 15 );
        c[ ImFluentCol_SystemFillCaution ]                 = ImColor( 157, 93, 0 );
        c[ ImFluentCol_SystemFillCritical ]                = ImColor( 196, 43, 28 );
        c[ ImFluentCol_SystemFillNeutral ]                 = ImColor( 0, 0, 0, 114 );
    }

    static void BuildHighContrastPalette( ImFluentStyle & s )
    {
        BuildDarkPalette( s );
        ImVec4 * c = s.Colors;

        c[ ImFluentCol_TextPrimary ]                    = ImColor( 255, 255, 255 );
        c[ ImFluentCol_TextSecondary ]                  = ImColor( 255, 255, 255 );
        c[ ImFluentCol_TextTertiary ]                   = ImColor( 255, 255, 255 );
        c[ ImFluentCol_TextDisabled ]                   = ImColor( 0, 255, 255 );
        c[ ImFluentCol_TextOnAccentPrimary ]            = ImColor( 0, 0, 0 );
        c[ ImFluentCol_TextOnAccentSecondary ]          = ImColor( 0, 0, 0 );
        c[ ImFluentCol_TextOnAccentDisabled ]           = ImColor( 0, 0, 0 );
        c[ ImFluentCol_AccentTextPrimary ]              = ImColor( 255, 255, 0 );
        c[ ImFluentCol_AccentTextSecondary ]            = ImColor( 255, 255, 0 );
        c[ ImFluentCol_AccentTextTertiary ]             = ImColor( 255, 255, 0 );
        c[ ImFluentCol_AccentFillDefault ]              = ImColor( 255, 255, 0 );
        c[ ImFluentCol_AccentFillSecondary ]            = ImColor( 255, 255, 0 );
        c[ ImFluentCol_AccentFillTertiary ]             = ImColor( 255, 255, 0 );
        c[ ImFluentCol_SolidBgBase ]                    = ImColor( 0, 0, 0 );
        c[ ImFluentCol_SolidBgQuarternary ]             = ImColor( 0, 0, 0 );
        c[ ImFluentCol_LayerFillDefault ]               = ImColor( 0, 0, 0 );
        c[ ImFluentCol_LayerFillAlt ]                   = ImColor( 0, 0, 0 );
        c[ ImFluentCol_CardBgDefault ]                  = ImColor( 0, 0, 0 );
        c[ ImFluentCol_CardBgSecondary ]                = ImColor( 0, 0, 0 );
        c[ ImFluentCol_ControlFillDefault ]             = ImColor( 0, 0, 0 );
        c[ ImFluentCol_ControlFillSecondary ]           = ImColor( 30, 30, 30 );
        c[ ImFluentCol_ControlFillTertiary ]            = ImColor( 50, 50, 50 );
        c[ ImFluentCol_ControlFillDisabled ]            = ImColor( 0, 0, 0 );
        c[ ImFluentCol_SubtleFillSecondary ]            = ImColor( 30, 30, 30 );
        c[ ImFluentCol_SubtleFillTertiary ]             = ImColor( 50, 50, 50 );
        c[ ImFluentCol_ControlStrokeDefault ]           = ImColor( 255, 255, 255 );
        c[ ImFluentCol_ControlStrokeSecondary ]         = ImColor( 255, 255, 255 );
        c[ ImFluentCol_ControlStrongStrokeDefault ]     = ImColor( 255, 255, 255 );
        c[ ImFluentCol_ControlStrongStrokeDisabled ]    = ImColor( 0, 255, 255 );
        c[ ImFluentCol_ControlStrokeOnAccentDefault ]   = ImColor( 0, 0, 0 );
        c[ ImFluentCol_ControlStrokeOnAccentSecondary ] = ImColor( 0, 0, 0 );
        c[ ImFluentCol_CardStrokeDefault ]              = ImColor( 255, 255, 255 );
        c[ ImFluentCol_CardStrokeSolid ]                = ImColor( 255, 255, 255 );
        c[ ImFluentCol_SurfaceStrokeDefault ]           = ImColor( 255, 255, 255 );
        c[ ImFluentCol_SurfaceStrokeFlyout ]            = ImColor( 255, 255, 255 );
        c[ ImFluentCol_DividerStrokeDefault ]           = ImColor( 255, 255, 255 );
        c[ ImFluentCol_FocusStrokeOuter ]               = ImColor( 255, 255, 0 );
        c[ ImFluentCol_FocusStrokeInner ]               = ImColor( 0, 0, 0 );

        c[ ImFluentCol_AccentFillDisabled ]                = ImColor( 0, 255, 255 );
        c[ ImFluentCol_ControlFillTransparent ]            = ImColor( 0, 0, 0 );
        c[ ImFluentCol_ElevationControlTop ]               = ImColor( 0, 0, 0, 0 );
        c[ ImFluentCol_ElevationControlBottom ]            = ImColor( 0, 0, 0, 0 );
        c[ ImFluentCol_ElevationTextControlTop ]           = ImColor( 0, 0, 0, 0 );
        c[ ImFluentCol_ElevationTextControlBottom ]        = ImColor( 0, 0, 0, 0 );
        c[ ImFluentCol_ElevationTextControlFocusedBottom ] = ImColor( 255, 255, 0 );
        c[ ImFluentCol_ElevationAccentTop ]                = ImColor( 0, 0, 0, 0 );
        c[ ImFluentCol_ElevationAccentBottom ]             = ImColor( 0, 0, 0, 0 );

        c[ ImFluentCol_SystemFillSuccess ]   = ImColor( 0, 255, 0 );
        c[ ImFluentCol_SystemFillCaution ]   = ImColor( 255, 255, 0 );
        c[ ImFluentCol_SystemFillCritical ]  = ImColor( 255, 0, 0 );
        c[ ImFluentCol_SystemFillNeutral ]   = ImColor( 255, 255, 255 );
        c[ ImFluentCol_SystemFillAttention ] = ImColor( 0, 255, 255 );
    }

    static inline ImVec4 LerpVec4( const ImVec4 & a, const ImVec4 & b, float t )
    {
        return ImVec4( a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, a.z + ( b.z - a.z ) * t, a.w + ( b.w - a.w ) * t );
    }

    static void ApplyUserAccent( ImFluentStyle & s, const ImVec4 & base, ImFluentThemePreset preset )
    {
        ImVec4 * c = s.Colors;
        const ImVec4 white( 1.f, 1.f, 1.f, 1.f );
        const ImVec4 black( 0.f, 0.f, 0.f, 1.f );

        ImVec4 base_opaque = base;
        base_opaque.w      = 1.f;

        const bool dark = ( preset != ImFluentThemePreset_Light );

        const ImVec4 light1 = LerpVec4( base_opaque, white, 0.20f );
        const ImVec4 light2 = LerpVec4( base_opaque, white, 0.40f );
        const ImVec4 light3 = LerpVec4( base_opaque, white, 0.65f );
        const ImVec4 dark1  = LerpVec4( base_opaque, black, 0.20f );
        const ImVec4 dark2  = LerpVec4( base_opaque, black, 0.40f );

        c[ ImFluentCol_AccentFillDefault ]        = base_opaque;
        c[ ImFluentCol_AccentFillSecondary ]      = ImVec4( base_opaque.x, base_opaque.y, base_opaque.z, 230.f / 255.f );
        c[ ImFluentCol_AccentFillTertiary ]       = ImVec4( base_opaque.x, base_opaque.y, base_opaque.z, 204.f / 255.f );
        c[ ImFluentCol_AccentFillSelectedTextBg ] = dark ? dark1 : base_opaque;

        if ( dark )
        {
            c[ ImFluentCol_AccentTextPrimary ]   = light3;
            c[ ImFluentCol_AccentTextSecondary ] = light2;
            c[ ImFluentCol_AccentTextTertiary ]  = light1;
        }
        else
        {
            c[ ImFluentCol_AccentTextPrimary ]   = dark2;
            c[ ImFluentCol_AccentTextSecondary ] = dark1;
            c[ ImFluentCol_AccentTextTertiary ]  = base_opaque;
        }

        c[ ImFluentCol_ElevationTextControlFocusedBottom ] = base_opaque;
    }

    static void BuildThemePalette( ImFluentStyle & s, ImFluentThemePreset preset )
    {
        switch ( preset )
        {
            case ImFluentThemePreset_Light:
                BuildLightPalette( s );
                break;
            case ImFluentThemePreset_HighContrast:
                BuildHighContrastPalette( s );
                break;
            case ImFluentThemePreset_Dark:
            default:
                BuildDarkPalette( s );
                break;
        }
        if ( g_Ctx.HasUserAccent && preset != ImFluentThemePreset_HighContrast )
            ApplyUserAccent( s, g_Ctx.UserAccent, preset );
    }

    static inline ImU32 LerpColorU32( ImU32 a, ImU32 b, float t )
    {
        if ( t <= 0.f )
            return a;
        if ( t >= 1.f )
            return b;
        // INKEYS PATCH: 先按透明度加权，避免透明黑到浅色的中间帧合成出暗色；输出仍为普通 RGBA。
        const float aa = ( float )( ( a >> 24 ) & 0xFF );
        const float ba = ( float )( ( b >> 24 ) & 0xFF );
        const float wa = aa * ( 1.f - t );
        const float wb = ba * t;
        const float alpha = wa + wb;
        if ( alpha <= 0.f )
            return 0;
        ImU32 result = ( ImU32 )( alpha + 0.5f ) << 24;
        for ( int shift = 0; shift < 24; shift += 8 )
        {
            const float channel = ( ( float )( ( a >> shift ) & 0xFF ) * wa
                + ( float )( ( b >> shift ) & 0xFF ) * wb ) / alpha;
            result |= ( ImU32 )( channel + 0.5f ) << shift;
        }
        return result;
    }

    // Drops next-item data set in another frame, or in a window that is not the current one or one of its parents.
    static void DropStaleNextItem()
    {
        ImFluentNextItemData & d = g_Ctx.NextItem;
        if ( !( d.HasHeader || d.HasDescription || d.HasGlyph || d.HasError ) )
            return;
        ImGuiContext & g = *GImGui;
        bool same_scope  = d.Frame == g.FrameCount;
        if ( same_scope && d.Window )
        {
            same_scope = false;
            for ( ImGuiWindow * w = g.CurrentWindow; w && !same_scope; w = w->ParentWindow )
                same_scope = ( w == d.Window );
        }
        if ( !same_scope )
            d = ImFluentNextItemData();
    }

    static void RenderAndConsumePendingHeader()
    {
        DropStaleNextItem();
        if ( !g_Ctx.NextItem.HasHeader )
            return;
        const ImFluentStyle & style = ImFluent::GetStyle();
        ImFluent::TextBlock( g_Ctx.NextItem.Header, ImFluentTextStyle_Body );
        ImGui::Dummy( ImVec2( 0.f, FluentDpx( style.SpacingXSmall ) ) );
        g_Ctx.NextItem.Header    = NULL;
        g_Ctx.NextItem.HasHeader = false;
    }

    static void RenderAndConsumePendingDescription()
    {
        DropStaleNextItem();
        if ( !g_Ctx.NextItem.HasDescription )
            return;
        const ImFluentStyle & style = ImFluent::GetStyle();
        ImGui::Dummy( ImVec2( 0.f, FluentDpx( style.SpacingXSmall ) ) );
        ImFluent::TextBlockColored( g_Ctx.NextItem.Description, ImFluent::GetColorU32( ImFluentCol_TextSecondary ), ImFluentTextStyle_Caption );
        g_Ctx.NextItem.Description    = NULL;
        g_Ctx.NextItem.HasDescription = false;
    }

    static const char * ConsumePendingGlyph( const char * fallback )
    {
        DropStaleNextItem();
        if ( !g_Ctx.NextItem.HasGlyph )
            return fallback;
        const char * g          = g_Ctx.NextItem.Glyph;
        g_Ctx.NextItem.Glyph    = NULL;
        g_Ctx.NextItem.HasGlyph = false;
        return g;
    }

    static void ClearPendingNextItem()
    {
        g_Ctx.NextItem = ImFluentNextItemData();
    }

    static bool IsCurrentItemDisabled()
    {
        return ( ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled ) != 0;
    }

    // Mouse over `bb` and not covered by another window (a bare IsMouseHoveringRect fires through popups).
    static bool IsMouseOverWindowRect( const ImRect & bb, ImGuiHoveredFlags flags = ImGuiHoveredFlags_None )
    {
        return ImGui::IsMouseHoveringRect( bb.Min, bb.Max, false ) && ImGui::IsWindowHovered( ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | flags );
    }

    static bool HasPendingError()
    {
        DropStaleNextItem();
        return g_Ctx.NextItem.HasError;
    }

    static void RenderAndConsumePendingError( const ImRect & item_bb )
    {
        DropStaleNextItem();
        if ( !g_Ctx.NextItem.HasError )
            return;
        const ImFluentStyle & style = ImFluent::GetStyle();
        const float t               = FluentDpx( style.TextInputAccentLineThickness );
        const float r               = FluentDpx( style.ControlCornerRadius );
        const ImU32 col             = ImFluent::GetColorU32( ImFluentCol_SystemFillCritical );
        ImGui::GetWindowDrawList()->AddRectFilled( ImVec2( item_bb.Min.x + r * 0.5f, item_bb.Max.y - t ), ImVec2( item_bb.Max.x - r * 0.5f, item_bb.Max.y ), col );
        ImFluent::PushFont( ImFluentTextStyle_Caption );
        ImGui::PushStyleColor( ImGuiCol_Text, col );
        ImGui::TextUnformatted( g_Ctx.NextItem.Error );
        ImGui::PopStyleColor();
        ImFluent::PopFont();
        g_Ctx.NextItem.Error    = NULL;
        g_Ctx.NextItem.HasError = false;
    }

    static const ImU32 kAnimSeed = 0xF1ECF1ECu;

    static bool AnimFrameContiguous( ImGuiStorage * s, ImGuiID frame_key )
    {
        const int currentFrame = ImGui::GetFrameCount();
        const bool contiguous  = ( s->GetInt( frame_key, -1 ) == currentFrame - 1 );
        s->SetInt( frame_key, currentFrame );
        return contiguous;
    }

    static ImGuiID AnimStateKey( ImGuiID id, int channel, bool & out_continuous, ImGuiStorage *& out_storage )
    {
        ImGuiStorage * s  = ImGui::GetStateStorage();
        const ImGuiID kId = id ^ kAnimSeed ^ ( ( ImGuiID )channel * 0x9E3779B9u );
        out_continuous    = AnimFrameContiguous( s, kId + 1 );
        out_storage       = s;
        return kId;
    }

    static ImU32 AnimateColorU32( ImGuiID id, ImU32 target, float seconds = 0.083f, int channel = 0 )
    {
        bool continuous;
        ImGuiStorage * s;
        const ImGuiID kId = AnimStateKey( id, channel, continuous, s );
        if ( !continuous )
        {
            s->SetInt( kId, ( int )target );
            return target;
        }
        ImU32 cur = ( ImU32 )s->GetInt( kId, ( int )target );
        if ( cur == target )
            return target;
        const float dt = ImGui::GetIO().DeltaTime;
        const float t  = ( seconds <= 0.f ) ? 1.f : 1.f - expf( -dt / seconds );
        cur            = LerpColorU32( cur, target, t );
        s->SetInt( kId, ( int )cur );
        return cur;
    }

    static float AnimateFloat( ImGuiID id, float target, float seconds = 0.20f, int channel = 0 )
    {
        bool continuous;
        ImGuiStorage * s;
        const ImGuiID kId = AnimStateKey( id, channel, continuous, s );
        if ( !continuous )
        {
            s->SetFloat( kId, target );
            return target;
        }
        float cur = s->GetFloat( kId, target );
        if ( cur == target )
            return target;
        const float dt = ImGui::GetIO().DeltaTime;
        const float tt = ( seconds <= 0.f ) ? 1.f : 1.f - expf( -dt / seconds );
        cur            = cur + ( target - cur ) * tt;
        s->SetFloat( kId, cur );
        return cur;
    }

    // Fluent's focus ring expands outside the widget's bounding box by `pad`, so
    // drawing into the widget's own draw list would (a) get clipped by the parent
    // window's clip rect when the widget is near an edge, and (b) get overdrawn
    // by sibling widgets whose frames overlap the expanded area. Draw into the
    // viewport foreground draw list, which is composited last and ignores both.
    static void RenderNavFocusRing( const ImRect & bb, float rounding )
    {
        const ImFluentStyle & style = ImFluent::GetStyle();
        const float outer           = FluentDpx( style.FocusStrokeThicknessOuter );
        const float inner           = FluentDpx( style.FocusStrokeThicknessInner );
        const float pad             = outer;

        ImDrawList * dl = ImGui::GetForegroundDrawList();

        dl->AddRect( bb.Min, bb.Max, ImFluent::GetColorU32( ImFluentCol_FocusStrokeInner ), rounding, 0, inner );

        dl->AddRect( ImVec2( bb.Min.x - pad, bb.Min.y - pad ), ImVec2( bb.Max.x + pad, bb.Max.y + pad ), ImFluent::GetColorU32( ImFluentCol_FocusStrokeOuter ), rounding + pad, 0, outer );
    }

    static void RenderCenteredText( ImDrawList * dl, const ImVec2 & center, float font_size, ImU32 col, const char * text )
    {
        if ( !text || !*text )
            return;
        ImFont * font = ImGui::GetFont();
        if ( !font )
            return;
        const float fs  = ( font_size > 0.f ) ? font_size : ImGui::GetFontSize();
        const ImVec2 ts = font->CalcTextSizeA( fs, FLT_MAX, 0.f, text );
        const ImVec2 pos( center.x - ts.x * 0.5f, center.y - ts.y * 0.5f );
        dl->AddText( font, fs, pos, col, text );
    }

    // A 1 dp stroke in whole pixels, as WinUI layout rounding draws it: 1 at 100% and 125%, 2 at 150% and 200%.
    static float StrokeWidth()
    {
        return ImMax( 1.f, IM_ROUND( FluentDpx( 1.f ) ) );
    }

    // A 1 dp outline inside [a, b] on whole pixels (AddRect centres a stroke half a pixel in, which blurs wider strokes).
    static void StrokeRect( ImDrawList * dl, const ImVec2 & a, const ImVec2 & b, ImU32 col, float rounding, ImDrawFlags flags = 0 )
    {
        const float t = StrokeWidth();
        const float h = t * 0.5f;
        dl->PathRect( ImVec2( a.x + h, a.y + h ), ImVec2( b.x - h, b.y - h ), ImMax( 0.f, rounding - h ), flags );
        dl->PathStroke( col, ImDrawFlags_Closed, t );
    }

    // Fill inside the 1 dp border (WinUI BackgroundSizing="InnerBorderEdge"), so the border blends with what is behind the control.
    static void FillInsideBorder( ImDrawList * dl, const ImVec2 & a, const ImVec2 & b, ImU32 col, float rounding, ImDrawFlags flags = 0 )
    {
        const float t = StrokeWidth();
        dl->AddRectFilled( ImVec2( a.x + t, a.y + t ), ImVec2( b.x - t, b.y - t ), col, ImMax( 0.f, rounding - t ), flags );
    }

    // The colour that, drawn over `under`, composites to `want` on any background (clamped where no colour can).
    static ImU32 OverlayToComposite( ImU32 under, ImU32 want )
    {
        const float a_under = ( float )( ( under >> IM_COL32_A_SHIFT ) & 0xFF ) / 255.f;
        const float a_want  = ( float )( ( want >> IM_COL32_A_SHIFT ) & 0xFF ) / 255.f;
        const float a       = a_under < 1.f ? ImMax( 0.f, ( a_want - a_under ) / ( 1.f - a_under ) ) : 0.f;
        if ( a <= 0.f )
            return 0;
        ImU32 out = ( ImU32 )( a * 255.f + 0.5f ) << IM_COL32_A_SHIFT;
        for ( int shift : { IM_COL32_R_SHIFT, IM_COL32_G_SHIFT, IM_COL32_B_SHIFT } )
        {
            const float u = ( float )( ( under >> shift ) & 0xFF );
            const float w = ( float )( ( want >> shift ) & 0xFF );
            out |= ( ImU32 )ImClamp( ( w * a_want - u * a_under * ( 1.f - a ) ) / a + 0.5f, 0.f, 255.f ) << shift;
        }
        return out;
    }

    // WinUI elevation border: the side colour with a 1 dp edge of another colour at the bottom (light theme, accent controls)
    // or at the top (dark theme); the edge strip goes over the side stroke, in the colour that composites to the edge colour.
    static void RenderElevationBorder( ImDrawList * dl, const ImRect & bb, float rounding, ImU32 sides, ImU32 top, ImU32 bottom, float edge_thickness )
    {
        StrokeRect( dl, bb.Min, bb.Max, sides, rounding );

        const float t = ImMax( StrokeWidth(), IM_ROUND( FluentDpx( edge_thickness > 0.f ? edge_thickness : 1.f ) ) );
        if ( top != sides )
            if ( const ImU32 strip = OverlayToComposite( sides, top ) )
                dl->AddRectFilled( ImVec2( bb.Min.x + rounding, bb.Min.y ), ImVec2( bb.Max.x - rounding, bb.Min.y + t ), strip );
        if ( bottom != sides )
            if ( const ImU32 strip = OverlayToComposite( sides, bottom ) )
                dl->AddRectFilled( ImVec2( bb.Min.x + rounding, bb.Max.y - t ), ImVec2( bb.Max.x - rounding, bb.Max.y ), strip );
    }

    static void RenderElevationShadow( ImDrawList * dl, const ImRect & bb, float rounding, int layers )
    {
        if ( layers < 1 )
            layers = 1;
        for ( int i = 0; i < layers; ++i )
        {
            const float k   = ( float )( i + 1 );
            const float pad = FluentDpx( ImFluent::GetStyle().SpacingXSmall * k );
            const ImU32 col = IM_COL32( 0, 0, 0, ( int )( 40.f / k ) );
            dl->AddRectFilled( ImVec2( bb.Min.x - pad, bb.Min.y - pad + k ), ImVec2( bb.Max.x + pad, bb.Max.y + pad + k ), col, rounding + pad );
        }
    }

    static ImU32 ResolveControlFillState( bool disabled, bool held, bool hovered )
    {
        using namespace ImFluent;
        if ( disabled )
            return ImFluent::GetColorU32( ImFluentCol_ControlFillDisabled );
        if ( held )
            return ImFluent::GetColorU32( ImFluentCol_ControlFillTertiary );
        if ( hovered )
            return ImFluent::GetColorU32( ImFluentCol_ControlFillSecondary );
        return ImFluent::GetColorU32( ImFluentCol_ControlFillDefault );
    }

    static ImU32 ResolveAccentFillState( bool disabled, bool held, bool hovered )
    {
        using namespace ImFluent;
        if ( disabled )
            return ImFluent::GetColorU32( ImFluentCol_AccentFillDisabled );
        if ( held )
            return ImFluent::GetColorU32( ImFluentCol_AccentFillTertiary );
        if ( hovered )
            return ImFluent::GetColorU32( ImFluentCol_AccentFillSecondary );
        return ImFluent::GetColorU32( ImFluentCol_AccentFillDefault );
    }

    static ImU32 ResolveSubtleFillState( bool selected, bool held, bool hovered )
    {
        using namespace ImFluent;
        if ( selected )
            return ImFluent::GetColorU32( held ? ImFluentCol_SubtleFillTertiary : ImFluentCol_SubtleFillSecondary );
        if ( held )
            return ImFluent::GetColorU32( ImFluentCol_SubtleFillTertiary );
        if ( hovered )
            return ImFluent::GetColorU32( ImFluentCol_SubtleFillSecondary );
        return ImFluent::GetColorU32( ImFluentCol_SubtleFillTransparent );
    }

    static ImU32 ResolveControlAltFillState( bool disabled, bool held, bool hovered )
    {
        using namespace ImFluent;
        if ( disabled )
            return ImFluent::GetColorU32( ImFluentCol_ControlAltFillDisabled );
        if ( held )
            return ImFluent::GetColorU32( ImFluentCol_ControlAltFillQuarternary );
        if ( hovered )
            return ImFluent::GetColorU32( ImFluentCol_ControlAltFillTertiary );
        return ImFluent::GetColorU32( ImFluentCol_ControlAltFillSecondary );
    }

    static void RenderChevron( ImDrawList * dl, ImVec2 c, ImGuiDir dir, ImU32 col, float L, float th = 1.5f )
    {
        switch ( dir )
        {
            case ImGuiDir_Down:
                dl->AddLine( ImVec2( c.x - L, c.y - L * 0.5f ), ImVec2( c.x, c.y + L * 0.5f ), col, th );
                dl->AddLine( ImVec2( c.x, c.y + L * 0.5f ), ImVec2( c.x + L, c.y - L * 0.5f ), col, th );
                break;
            case ImGuiDir_Up:
                dl->AddLine( ImVec2( c.x - L, c.y + L * 0.5f ), ImVec2( c.x, c.y - L * 0.5f ), col, th );
                dl->AddLine( ImVec2( c.x, c.y - L * 0.5f ), ImVec2( c.x + L, c.y + L * 0.5f ), col, th );
                break;
            case ImGuiDir_Right:
                dl->AddLine( ImVec2( c.x - L * 0.5f, c.y - L ), ImVec2( c.x + L * 0.5f, c.y ), col, th );
                dl->AddLine( ImVec2( c.x + L * 0.5f, c.y ), ImVec2( c.x - L * 0.5f, c.y + L ), col, th );
                break;
            case ImGuiDir_Left:
            default:
                dl->AddLine( ImVec2( c.x + L * 0.5f, c.y - L ), ImVec2( c.x - L * 0.5f, c.y ), col, th );
                dl->AddLine( ImVec2( c.x - L * 0.5f, c.y ), ImVec2( c.x + L * 0.5f, c.y + L ), col, th );
                break;
        }
    }

    static void RenderSelectionIndicator( ImDrawList * dl, const ImRect & bb, ImGuiDir side, ImU32 col, float length_frac = 0.5f )
    {
        const ImFluentStyle & style = ImFluent::GetStyle();
        const float thickness       = FluentDpx( style.SelectionIndicatorThickness );
        const float inset           = FluentDpx( style.SelectionIndicatorInset );
        const float corner          = FluentDpx( style.SpacingXSmall );
        if ( side == ImGuiDir_Left || side == ImGuiDir_Right )
        {
            const float h  = bb.GetHeight() * length_frac;
            const float cy = ( bb.Min.y + bb.Max.y ) * 0.5f;
            const float x  = ( side == ImGuiDir_Left ) ? bb.Min.x + inset : bb.Max.x - inset - thickness;
            dl->AddRectFilled( ImVec2( x, cy - h * 0.5f ), ImVec2( x + thickness, cy + h * 0.5f ), col, corner );
        }
        else
        {
            const float w  = bb.GetWidth() * length_frac;
            const float cx = ( bb.Min.x + bb.Max.x ) * 0.5f;
            const float y  = ( side == ImGuiDir_Up ) ? bb.Min.y + inset : bb.Max.y - inset - thickness;
            dl->AddRectFilled( ImVec2( cx - w * 0.5f, y ), ImVec2( cx + w * 0.5f, y + thickness ), col, corner );
        }
    }

    static void PushControlFrameStyle( float h )
    {
        using namespace ImFluent;
        const ImFluentStyle & style = ImFluent::GetStyle();
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, ImFluent::FluentDpx( style.ControlCornerRadius ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( ImFluent::FluentDpx( style.SpacingLarge ), ( h - ImGui::GetFontSize() ) * 0.5f ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, ImFluent::GetColorU32( ImFluentCol_ControlFillDefault ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImFluent::GetColorU32( ImFluentCol_ControlFillSecondary ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImFluent::GetColorU32( ImFluentCol_ControlFillInputActive ) );
        ImGui::PushStyleColor( ImGuiCol_Border, ImFluent::GetColorU32( ImFluentCol_ControlStrokeDefault ) );
    }

    static void PopControlFrameStyle()
    {
        ImGui::PopStyleColor( 4 );
        ImGui::PopStyleVar( 2 );
    }

    // A WinUI text control frame around the InputText between Begin and End: fill by state, 1 dp border, 1 dp strong bottom edge.
    // ImGui draws only the text (transparent frame, no border); the fill goes on a draw channel under it.
    static void BeginTextControlFrame( float h )
    {
        PushControlFrameStyle( h );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 0, 0, 0, 0 ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.f );
        ImDrawList * dl = ImGui::GetWindowDrawList();
        g_Ctx.TextFrameSplitter.Split( dl, 2 );
        g_Ctx.TextFrameSplitter.SetCurrentChannel( dl, 1 );
    }

    static void EndTextControlFrame()
    {
        using namespace ImFluent;
        const ImFluentStyle & style = ImFluent::GetStyle();
        ImDrawList * dl             = ImGui::GetWindowDrawList();
        const ImRect bb             = ImGui::GetCurrentContext()->LastItemData.Rect;
        const bool disabled         = IsCurrentItemDisabled();
        const bool active           = ImGui::IsItemActive();
        const float r               = FluentDpx( style.ControlCornerRadius );

        ImFluentCol fill = ImFluentCol_ControlFillDefault;
        if ( disabled )
            fill = ImFluentCol_ControlFillDisabled;
        else if ( active )
            fill = ImFluentCol_ControlFillInputActive;
        else if ( ImGui::IsItemHovered() )
            fill = ImFluentCol_ControlFillSecondary;
        g_Ctx.TextFrameSplitter.SetCurrentChannel( dl, 0 );
        FillInsideBorder( dl, bb.Min, bb.Max, GetColorU32( fill ), r );
        g_Ctx.TextFrameSplitter.Merge( dl );

        const ImU32 stroke = GetColorU32( ImFluentCol_ControlStrokeDefault );
        if ( disabled )
            StrokeRect( dl, bb.Min, bb.Max, stroke, r );
        else
            RenderElevationBorder( dl, bb, r, stroke, GetColorU32( ImFluentCol_ElevationTextControlTop ), GetColorU32( ImFluentCol_ElevationTextControlBottom ), 1.f );

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        PopControlFrameStyle();
    }

    static void PushOverlayWindowStyle( float padding_x, float padding_y )
    {
        using namespace ImFluent;
        const ImFluentStyle & style = ImFluent::GetStyle();
        ImGui::PushStyleColor( ImGuiCol_PopupBg, ImFluent::GetColorU32( ImFluentCol_SolidBgQuarternary ) );
        ImGui::PushStyleColor( ImGuiCol_Border, ImFluent::GetColorU32( ImFluentCol_SurfaceStrokeFlyout ) );
        ImGui::PushStyleVar( ImGuiStyleVar_PopupRounding, FluentDpx( style.OverlayCornerRadius ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( FluentDpx( padding_x ), FluentDpx( padding_y ) ) );
    }

    static void PopOverlayWindowStyle()
    {
        ImGui::PopStyleVar( 2 );
        ImGui::PopStyleColor( 2 );
    }

    const char * LocalizeGetMsg( ImFluentLocKey key )
    {
        if ( key < 0 || key >= ImFluentLocKey_COUNT )
            return "*Missing Text*";
        const char * msg = g_Ctx.Style.LocalizationTable[ key ];
        return msg ? msg : "*Missing Text*";
    }

    void LocalizeRegisterEntries( const ImFluentLocEntry * entries, int count )
    {
        if ( !entries || count <= 0 )
            return;
        for ( int n = 0; n < count; ++n )
        {
            const ImFluentLocKey k = entries[ n ].Key;
            if ( k < 0 || k >= ImFluentLocKey_COUNT )
                continue;
            g_Ctx.Style.LocalizationTable[ k ] = entries[ n ].Text;
        }
    }

    static void StorePopupAnchor( ImGuiID id, const ImRect & rect )
    {
        for ( int i = 0; i < g_Ctx.PopupAnchorCount; ++i )
            if ( g_Ctx.PopupAnchors[ i ].Id == id )
            {
                g_Ctx.PopupAnchors[ i ].Rect = rect;
                return;
            }
        if ( g_Ctx.PopupAnchorCount < ImFluentContext::MaxPopupAnchors )
        {
            g_Ctx.PopupAnchors[ g_Ctx.PopupAnchorCount ].Id   = id;
            g_Ctx.PopupAnchors[ g_Ctx.PopupAnchorCount ].Rect = rect;
            ++g_Ctx.PopupAnchorCount;
        }
    }

    static const ImRect * FindPopupAnchor( ImGuiID id )
    {
        for ( int i = 0; i < g_Ctx.PopupAnchorCount; ++i )
            if ( g_Ctx.PopupAnchors[ i ].Id == id )
                return &g_Ctx.PopupAnchors[ i ].Rect;
        return NULL;
    }

    static void ApplyPopupAnchor( const char * str_id )
    {
        if ( !ImGui::IsPopupOpen( str_id, ImGuiPopupFlags_None ) )
            return;
        const ImGuiID pid     = ImGui::GetID( str_id );
        const ImRect * anchor = FindPopupAnchor( pid );
        if ( !anchor )
            return;
        const ImVec2 pos( anchor->Min.x, anchor->Max.y + FluentDpx( ImFluent::GetStyle().SpacingXSmall ) );
        ImGui::SetNextWindowPos( pos, ImGuiCond_Appearing );
    }

    static ImFluentAppBarLabelPosition ConsumeAppBarLabelPos()
    {
        if ( !g_Ctx.NextAppBarLabelPosSet )
            return ( !g_Ctx.CommandBarStack.empty() && g_Ctx.CommandBarStack.back().IconOnly ) ? ImFluentAppBarLabelPosition_Collapsed : ImFluentAppBarLabelPosition_Bottom;
        const ImFluentAppBarLabelPosition v = g_Ctx.NextAppBarLabelPos;
        g_Ctx.NextAppBarLabelPosSet         = false;
        return v;
    }

    static void RenderAppBarContent( ImDrawList * dl, const ImRect & bb, const char * label, const char * glyph, ImU32 textCol, ImFluentAppBarLabelPosition pos, const ImFluentStyle & style )
    {
        const float W   = bb.GetWidth();
        const float H   = bb.GetHeight();
        const ImVec2 ts = ( pos != ImFluentAppBarLabelPosition_Collapsed && label ) ? ImGui::CalcTextSize( label ) : ImVec2( 0, 0 );
        const ImVec2 gs = glyph ? ImGui::CalcTextSize( glyph ) : ImVec2( 0, 0 );

        if ( pos == ImFluentAppBarLabelPosition_Right )
        {
            const float pad   = FluentDpx( style.SpacingMedium );
            const float gap   = ( glyph && label ) ? FluentDpx( style.SpacingMedium ) : 0.f;
            const float run_w = gs.x + gap + ts.x;
            float x           = bb.Min.x + ( W - run_w ) * 0.5f;
            if ( glyph )
            {
                dl->AddText( ImVec2( x, bb.Min.y + ( H - gs.y ) * 0.5f ), textCol, glyph );
                x += gs.x + gap;
            }
            if ( label && pos != ImFluentAppBarLabelPosition_Collapsed )
                dl->AddText( ImVec2( x, bb.Min.y + ( H - ts.y ) * 0.5f ), textCol, label );
            ( void )pad;
        }
        else if ( pos == ImFluentAppBarLabelPosition_Collapsed )
        {
            if ( glyph )
                dl->AddText( ImVec2( bb.Min.x + ( W - gs.x ) * 0.5f, bb.Min.y + ( H - gs.y ) * 0.5f ), textCol, glyph );
        }
        else
        {
            if ( glyph )
                dl->AddText( ImVec2( bb.Min.x + ( W - gs.x ) * 0.5f, bb.Min.y + FluentDpx( style.SpacingMedium ) ), textCol, glyph );
            if ( label )
                dl->AddText( ImVec2( bb.Min.x + ( W - ts.x ) * 0.5f, bb.Max.y - ts.y - FluentDpx( style.SpacingMedium - 2.f ) ), textCol, label );
        }
    }

    static ImGuiID AppBarButtonId( ImGuiWindow * w, const char * label, const char * glyph )
    {
        return label ? w->GetID( label ) : w->GetID( ( const void * )glyph );
    }

    static ImVec2 CalcAppBarButtonSize( const char * label, const char * glyph, ImFluentAppBarLabelPosition pos, const ImVec2 & size_arg, const ImFluentStyle & style )
    {
        float defW = FluentDpx( style.AppBarButtonWidth );
        float defH = FluentDpx( style.AppBarButtonHeight );
        if ( pos == ImFluentAppBarLabelPosition_Right )
        {
            const float ts_x = label ? ImGui::CalcTextSize( label ).x : 0.f;
            const float gs_x = glyph ? ImGui::CalcTextSize( glyph ).x : 0.f;
            defW             = gs_x + ( glyph && label ? FluentDpx( style.SpacingMedium ) : 0.f ) + ts_x + FluentDpx( style.SpacingXLarge ) * 2.f;
            defH             = FluentDpx( style.ControlHeight );
        }
        else if ( pos == ImFluentAppBarLabelPosition_Collapsed )
        {
            defW = FluentDpx( style.AppBarButtonHeight );
            defH = FluentDpx( style.AppBarButtonHeight );
        }
        return ImVec2( size_arg.x > 0 ? size_arg.x : defW, size_arg.y > 0 ? size_arg.y : defH );
    }

    static ImVec2 CalcButtonSize( const char * label, const ImVec2 & size_arg, float min_height_dpx, float pad_x, float pad_y )
    {
        const ImVec2 ts = ImGui::CalcTextSize( label, NULL, true );
        return ImVec2( size_arg.x > 0.f ? size_arg.x : ts.x + pad_x * 2.f, size_arg.y > 0.f ? size_arg.y : ImMax( min_height_dpx, ts.y + pad_y * 2.f ) );
    }

    static void RenderButtonLabel( ImDrawList *, const ImRect & bb, const char * label, ImU32 col, float pad_x, float pad_y )
    {
        ImGui::PushStyleColor( ImGuiCol_Text, col );
        const ImVec2 ts = ImGui::CalcTextSize( label, NULL, true );
        const ImVec2 text_min( bb.Min.x + pad_x, bb.Min.y + pad_y );
        const ImVec2 text_max( bb.Max.x - pad_x, bb.Max.y - pad_y );
        ImGui::RenderTextClipped( text_min, text_max, label, NULL, &ts, ImVec2( 0.5f, 0.5f ), &bb );
        ImGui::PopStyleColor();
    }

    static bool IsItemFocused( ImGuiID id )
    {
        ImGuiContext & g = *ImGui::GetCurrentContext();
        return g.NavId == id && g.NavCursorVisible;
    }

    // Item label and state for the Dear ImGui Test Engine, as ImGui's own widgets report them (no-op without IMGUI_ENABLE_TEST_ENGINE).
    static void ReportItemInfo( ImGuiID id, const char * label, ImGuiItemStatusFlags flags = 0 )
    {
#ifdef IMGUI_ENABLE_TEST_ENGINE
        ImGuiContext & g = *GImGui;
        IMGUI_TEST_ENGINE_ITEM_INFO( id, label, g.LastItemData.StatusFlags | flags );
#else
        IM_UNUSED( id );
        IM_UNUSED( label );
        IM_UNUSED( flags );
#endif
    }

    // INKEYS PATCH: 1.92.7 的语义状态仅供 Test Engine，关闭时与 ReportItemInfo 一同跳过。
    static ImGuiItemStatusFlags CheckedFlags( bool checked )
    {
#ifdef IMGUI_ENABLE_TEST_ENGINE
        return ImGuiItemStatusFlags_Checkable | ( checked ? ImGuiItemStatusFlags_Checked : 0 );
#else
        IM_UNUSED( checked );
        return 0;
#endif
    }

    static ImGuiItemStatusFlags OpenedFlags( bool opened )
    {
#ifdef IMGUI_ENABLE_TEST_ENGINE
        return ImGuiItemStatusFlags_Openable | ( opened ? ImGuiItemStatusFlags_Opened : 0 );
#else
        IM_UNUSED( opened );
        return 0;
#endif
    }

    // Overload of RenderNavFocusRing that performs the IsItemFocused(id) check
    // itself — mirrors ImGui's own RenderNavCursor(bb, id) idiom.
    static void RenderNavFocusRing( ImGuiID id, const ImRect & bb, float rounding )
    {
        if ( IsItemFocused( id ) )
            RenderNavFocusRing( bb, rounding );
    }

    // WinUI controls take arrow keys themselves: if `id` has nav focus and the nav move goes in `dir`, cancel it and return true.
    static bool ConsumeNavArrow( ImGuiID id, ImGuiDir dir )
    {
        ImGuiContext & g = *GImGui;
        if ( g.NavId != id || g.NavMoveDir != dir || !g.NavMoveSubmitted )
            return false;
        ImGui::NavClearPreferredPosForAxis( ( dir == ImGuiDir_Left || dir == ImGuiDir_Right ) ? ImGuiAxis_X : ImGuiAxis_Y );
        ImGui::NavMoveRequestCancel();
        g.NavCursorVisible = true;
        return true;
    }

    // Chord press with exact modifiers, wherever focus is. A 0 chord (action turned off) is never pressed.
    static bool IsActionPressed( ImGuiKeyChord chord )
    {
        return chord != 0 && ImGui::IsKeyChordPressed( chord );
    }

    // Chord press (exact modifiers) whose key `owner_id` takes, so ImGui's navigation does not also use it (Home/End/PageUp/PageDown scroll).
    static bool IsChordPressedOwned( ImGuiKeyChord chord, ImGuiID owner_id, bool repeat = true )
    {
        if ( chord == 0 )
            return false;
        ImGui::SetKeyOwner( ( ImGuiKey )( chord & ~ImGuiMod_Mask_ ), owner_id );
        return ImGui::IsKeyChordPressed( chord, repeat ? ImGuiInputFlags_Repeat : ImGuiInputFlags_None, owner_id );
    }

    // Chord press for the nav-focused item: an arrow chord is taken from the nav move request, other keys are owned by the item.
    static bool IsFocusedItemChordPressed( ImGuiID id, ImGuiKeyChord chord, bool repeat = true )
    {
        ImGuiContext & g = *GImGui;
        if ( chord == 0 || g.NavId != id || g.ActiveId != 0 )
            return false;
        const ImGuiKey key = ( ImGuiKey )( chord & ~ImGuiMod_Mask_ );
        if ( key == ImGuiKey_LeftArrow || key == ImGuiKey_RightArrow || key == ImGuiKey_UpArrow || key == ImGuiKey_DownArrow )
        {
            if ( g.IO.KeyMods != ( chord & ImGuiMod_Mask_ ) )
                return false;
            const ImGuiDir dir = key == ImGuiKey_LeftArrow ? ImGuiDir_Left : key == ImGuiKey_RightArrow ? ImGuiDir_Right : key == ImGuiKey_UpArrow ? ImGuiDir_Up : ImGuiDir_Down;
            return ConsumeNavArrow( id, dir );
        }
        return IsChordPressedOwned( chord, id, repeat );
    }

    // WinUI CheckBox, RadioButton and ToggleSwitch ignore Enter: only Space (and the mouse) toggle them.
    static bool IsEnterActivation( ImGuiID id )
    {
        ImGuiContext & g = *GImGui;
        return g.NavActivatePressedId == id && ( g.NavActivateFlags & ImGuiActivateFlags_PreferInput ) != 0;
    }

    // WinUI TextBox, PasswordBox and RichEditBox keep their text on Escape (ImGui's InputText reverts). Call before the InputText:
    // Escape then only leaves edit mode and is not also a nav cancel. NumberBox keeps the revert, as in WinUI.
    static void TextInputEscapeKeepsText( ImGuiID input_id )
    {
        ImGuiContext & g = *GImGui;
        if ( g.ActiveId != input_id || !ImGui::IsKeyPressed( ImGuiKey_Escape, ImGuiInputFlags_None, ImGuiKeyOwner_Any ) )
            return;
        ImGui::ClearActiveID();
        ImGui::SetKeyOwner( ImGuiKey_Escape, input_id, ImGuiInputFlags_LockUntilRelease );
    }

    static const ImGuiID TABGROUP_KEY_SELECTED       = 0x7A6B0001u;
    static const ImGuiID TABGROUP_KEY_FOCUSED        = 0x7A6B0002u;
    static const ImGuiID TABGROUP_KEY_FIRST          = 0x7A6B0003u;
    static const ImGuiID TABGROUP_KEY_LAST           = 0x7A6B0004u;
    static const ImGuiID TABGROUP_KEY_PENDING        = 0x7A6B0005u;
    static const ImGuiID TABGROUP_KEY_PENDING_SELECT = 0x7A6B0006u;
    static const ImGuiID TABGROUP_KEY_ANCESTOR       = 0x7A6B0007u;
    static const ImGuiID TABGROUP_KEY_LAST_SELECTED  = 0x7A6B0008u;

    // One-tab-stop group around members in one window. Each member: TabGroupItemFlags() on its ItemAdd, then TabGroupItemNav() after
    // its behavior, which returns true when focus just arrived in a SelectionFollowsFocus group (the caller selects it).
    static void BeginTabGroup( ImGuiID group_id, int flags )
    {
        ImGuiContext & g       = *GImGui;
        ImGuiStorage * st      = ImGui::GetStateStorage();
        const ImGuiID selected = ( ImGuiID )st->GetInt( group_id ^ TABGROUP_KEY_SELECTED, 0 );
        const ImGuiID ancestor = ( ImGuiID )st->GetInt( group_id ^ TABGROUP_KEY_ANCESTOR, 0 );
        const ImGuiID focused  = ( ImGuiID )st->GetInt( group_id ^ TABGROUP_KEY_FOCUSED, 0 );
        ImFluentTabGroup grp;
        grp.Id               = group_id;
        grp.Flags            = flags;
        grp.PrevFirst        = ( ImGuiID )st->GetInt( group_id ^ TABGROUP_KEY_FIRST, 0 );
        grp.PrevLast         = ( ImGuiID )st->GetInt( group_id ^ TABGROUP_KEY_LAST, 0 );
        grp.Pending          = ( ImGuiID )st->GetInt( group_id ^ TABGROUP_KEY_PENDING, 0 );
        grp.PendingSelect    = st->GetBool( group_id ^ TABGROUP_KEY_PENDING_SELECT, false );
        grp.LastSelected     = ( ImGuiID )st->GetInt( group_id ^ TABGROUP_KEY_LAST_SELECTED, 0 );
        const bool tab_back  = g.NavMoveSubmitted && ( g.NavMoveFlags & ImGuiNavMoveFlags_IsTabbing ) && g.NavTabbingDir < 0;
        const ImGuiID enter  = ( flags & ImFluentTabGroupFlags_EnterAtEnds ) ? ( tab_back ? grp.PrevLast : grp.PrevFirst ) : focused ? focused : grp.PrevFirst;
        const ImGuiID chosen = ( ( flags & ImFluentTabGroupFlags_EnterOnLastFocused ) && focused ) ? focused : selected ? selected : ancestor ? ancestor : enter;
        // Focus inside the group stays the stop, so Tab and Shift+Tab leave the group from it
        grp.Stop             = ( focused && focused == g.NavId ) ? focused : ( flags & ImFluentTabGroupFlags_FirstIsStop ) ? grp.PrevFirst : chosen;
        grp.SelectedAncestor = 0;
        grp.First = grp.Last = grp.Selected = grp.Focused = 0;
        grp.StopSeen                                      = false;
        grp.LayoutIdx                                     = -1;
        if ( flags & ImFluentTabGroupFlags_Grid )
        {
            ImFluentTabGroupLayout * lay = g_Ctx.TabGroupLayouts.GetOrAddByKey( group_id );
            lay->NextItems.resize( 0 );
            lay->NextRects.resize( 0 );
            grp.LayoutIdx = g_Ctx.TabGroupLayouts.GetIndex( lay );
        }
        g_Ctx.TabGroupStack.push_back( grp );
    }

    // Grid groups: records a member's rect, also for a clipped member (call it before returning on ItemAdd()).
    static void TabGroupItemRect( ImGuiID item_id, const ImRect & bb )
    {
        if ( g_Ctx.TabGroupStack.empty() || g_Ctx.TabGroupStack.back().LayoutIdx < 0 )
            return;
        ImFluentTabGroupLayout * lay = g_Ctx.TabGroupLayouts.GetByIndex( g_Ctx.TabGroupStack.back().LayoutIdx );
        lay->NextItems.push_back( item_id );
        lay->NextRects.push_back( bb );
    }

    // Nearest previous-frame member from `from` in `dir`, 0 at the edge: Left/Right stay in the row, Up/Down prefer the column.
    static ImGuiID TabGroupNeighbour( const ImFluentTabGroupLayout & lay, ImGuiID from, ImGuiDir dir )
    {
        int fi = -1;
        for ( int i = 0; i < lay.Items.Size && fi < 0; ++i )
            if ( lay.Items[ i ] == from )
                fi = i;
        if ( fi < 0 )
            return 0;
        const ImRect a        = lay.Rects[ fi ];
        const bool horizontal = ( dir == ImGuiDir_Left || dir == ImGuiDir_Right );
        const float sign      = ( dir == ImGuiDir_Left || dir == ImGuiDir_Up ) ? -1.f : 1.f;
        ImGuiID best          = 0;
        float best_score      = FLT_MAX;
        for ( int i = 0; i < lay.Items.Size; ++i )
        {
            if ( i == fi )
                continue;
            const ImRect & b   = lay.Rects[ i ];
            const float along  = sign * ( horizontal ? b.GetCenter().x - a.GetCenter().x : b.GetCenter().y - a.GetCenter().y );
            const float across = horizontal ? ImMax( 0.f, ImMax( a.Min.y - b.Max.y, b.Min.y - a.Max.y ) ) : ImMax( 0.f, ImMax( a.Min.x - b.Max.x, b.Min.x - a.Max.x ) );
            if ( along <= 0.5f || ( horizontal && across > 0.f ) )
                continue;
            const float score = along + across * 4.f;
            if ( score < best_score )
            {
                best_score = score;
                best       = lay.Items[ i ];
            }
        }
        return best;
    }

    static ImGuiItemFlags TabGroupItemFlags( ImGuiID item_id, bool selected )
    {
        if ( g_Ctx.TabGroupStack.empty() )
            return ImGuiItemFlags_None;
        ImFluentTabGroup & grp = g_Ctx.TabGroupStack.back();
        if ( !grp.First )
            grp.First = item_id;
        grp.Last = item_id;
        if ( selected && !grp.Selected )
            grp.Selected = item_id;
        if ( GImGui->NavId == item_id )
            grp.Focused = item_id;
        const ImGuiID stop = grp.Stop ? grp.Stop : grp.First;
        if ( item_id == stop )
            grp.StopSeen = true;
        return ( item_id == stop ) ? ImGuiItemFlags_None : ImGuiItemFlags_NoTabStop;
    }

    // A collapsed member whose hidden descendant `hidden_selected` is the group's selected item takes the tab stop (WinUI NavigationView).
    static void TabGroupHiddenSelection( ImGuiID item_id, ImGuiID hidden_selected )
    {
        if ( g_Ctx.TabGroupStack.empty() )
            return;
        ImFluentTabGroup & grp = g_Ctx.TabGroupStack.back();
        if ( hidden_selected != 0 && hidden_selected == grp.LastSelected && !grp.SelectedAncestor )
            grp.SelectedAncestor = item_id;
    }

    static bool TabGroupItemNav( ImGuiID item_id )
    {
        if ( g_Ctx.TabGroupStack.empty() )
            return false;
        ImGuiContext & g       = *GImGui;
        ImFluentTabGroup & grp = g_Ctx.TabGroupStack.back();
        const int flags        = grp.Flags;
        const bool follows     = ( flags & ImFluentTabGroupFlags_SelectionFollowsFocus ) != 0;
        bool arrived           = false;
        if ( grp.Pending == item_id )
        {
            arrived           = grp.PendingSelect;
            grp.Pending       = 0;
            grp.PendingSelect = false;
            if ( g.LastItemData.ID == item_id )
                ImGui::ScrollToItem( ImGuiScrollFlags_KeepVisibleEdgeX | ImGuiScrollFlags_KeepVisibleEdgeY );
        }
        if ( g.NavId == item_id )
        {
            const bool vertical   = ( flags & ImFluentTabGroupFlags_Vertical ) != 0;
            const bool horizontal = ( flags & ImFluentTabGroupFlags_Horizontal ) != 0;
            const bool wrap       = ( flags & ImFluentTabGroupFlags_Wrap ) != 0;
            ImGuiID jump          = 0;
            if ( vertical || horizontal )
            {
                const ImGuiDir prev_dir = vertical ? ImGuiDir_Up : ImGuiDir_Left;
                const ImGuiDir next_dir = vertical ? ImGuiDir_Down : ImGuiDir_Right;
                if ( item_id == grp.PrevFirst && ConsumeNavArrow( item_id, prev_dir ) && wrap )
                    jump = grp.PrevLast;
                if ( item_id == grp.PrevLast && ConsumeNavArrow( item_id, next_dir ) && wrap )
                    jump = grp.PrevFirst;
                ConsumeNavArrow( item_id, vertical ? ImGuiDir_Left : ImGuiDir_Up );
                ConsumeNavArrow( item_id, vertical ? ImGuiDir_Right : ImGuiDir_Down );
            }
            if ( grp.LayoutIdx >= 0 )
            {
                for ( int dir = ImGuiDir_Left; dir <= ImGuiDir_Down; ++dir )
                    if ( ConsumeNavArrow( item_id, ( ImGuiDir )dir ) )
                        jump = TabGroupNeighbour( *g_Ctx.TabGroupLayouts.GetByIndex( grp.LayoutIdx ), item_id, ( ImGuiDir )dir );
            }
            if ( flags & ImFluentTabGroupFlags_HomeEnd )
            {
                if ( IsFocusedItemChordPressed( item_id, g_Ctx.Actions.FirstItem ) )
                    jump = grp.PrevFirst;
                if ( IsFocusedItemChordPressed( item_id, g_Ctx.Actions.LastItem ) )
                    jump = grp.PrevLast;
            }
            if ( jump && jump != item_id )
            {
                ImGui::SetFocusID( jump, g.CurrentWindow );
                g.NavCursorVisible = true;
                grp.Pending        = jump; // Scrolled into view (and selected) when it is submitted
                grp.PendingSelect  = follows && !g.IO.KeyCtrl;
            }
        }
        if ( follows && g.NavJustMovedToId == item_id && !g.NavJustMovedToIsTabbing && !( g.NavJustMovedToKeyMods & ImGuiMod_Ctrl ) )
            arrived = true;
        return arrived;
    }

    static void EndTabGroup()
    {
        if ( g_Ctx.TabGroupStack.empty() )
            return;
        const ImFluentTabGroup & grp = g_Ctx.TabGroupStack.back();
        ImGuiStorage * st            = ImGui::GetStateStorage();
        // WinUI Selector::SetFocusedItem: a selection change also moves the remembered focus
        const bool selection_changed = grp.Selected && grp.Selected != ( ImGuiID )st->GetInt( grp.Id ^ TABGROUP_KEY_SELECTED, 0 );
        const ImGuiID focused        = grp.Focused ? grp.Focused : selection_changed ? grp.Selected : 0;
        st->SetInt( grp.Id ^ TABGROUP_KEY_SELECTED, ( int )grp.Selected );
        st->SetInt( grp.Id ^ TABGROUP_KEY_ANCESTOR, ( int )grp.SelectedAncestor );
        if ( grp.Selected )
            st->SetInt( grp.Id ^ TABGROUP_KEY_LAST_SELECTED, ( int )grp.Selected );
        if ( focused || !grp.StopSeen )
            st->SetInt( grp.Id ^ TABGROUP_KEY_FOCUSED, ( int )focused ); // Forget a remembered item that went away
        st->SetInt( grp.Id ^ TABGROUP_KEY_FIRST, ( int )grp.First );
        st->SetInt( grp.Id ^ TABGROUP_KEY_LAST, ( int )grp.Last );
        st->SetInt( grp.Id ^ TABGROUP_KEY_PENDING, ( int )grp.Pending );
        st->SetBool( grp.Id ^ TABGROUP_KEY_PENDING_SELECT, grp.PendingSelect );
        if ( grp.LayoutIdx >= 0 )
        {
            ImFluentTabGroupLayout * lay = g_Ctx.TabGroupLayouts.GetByIndex( grp.LayoutIdx );
            lay->Items.swap( lay->NextItems );
            lay->Rects.swap( lay->NextRects );
        }
        g_Ctx.TabGroupStack.pop_back();
    }

    static bool InItemsContainer()
    {
        return !g_Ctx.TabGroupStack.empty() && ( g_Ctx.TabGroupStack.back().Flags & ImFluentTabGroupFlags_ItemsContainer ) != 0;
    }

    // WinUI TreeView rows: Right on an expanded node focuses its first child; a Left that did nothing on a row focuses the parent.
    static void TreeRowTakeFirstChildFocus( ImGuiID id )
    {
        if ( g_Ctx.TreeParentStack.empty() || g_Ctx.TreeFocusFirstChildOf != g_Ctx.TreeParentStack.back() )
            return;
        g_Ctx.TreeFocusFirstChildOf = 0;
        ImGui::SetFocusID( id, GImGui->CurrentWindow );
        GImGui->NavCursorVisible = true;
    }

    static void TreeRowLeftToParent( ImGuiID id )
    {
        if ( g_Ctx.TreeParentStack.empty() || !ConsumeNavArrow( id, ImGuiDir_Left ) )
            return;
        ImGui::SetFocusID( g_Ctx.TreeParentStack.back(), GImGui->CurrentWindow );
        GImGui->NavCursorVisible = true;
    }

    // -1 / +1 for a focused item that takes Left/Down as "decrease" and Right/Up as "increase".
    static int ConsumeNavIncrement( ImGuiID id )
    {
        if ( ConsumeNavArrow( id, ImGuiDir_Left ) || ConsumeNavArrow( id, ImGuiDir_Down ) )
            return -1;
        if ( ConsumeNavArrow( id, ImGuiDir_Right ) || ConsumeNavArrow( id, ImGuiDir_Up ) )
            return +1;
        return 0;
    }

    // Derive a stable popup id keyed by an owning widget id + popup name. Keeps
    // PushOverrideID / GetID / PopID boilerplate out of the call site.
    static ImGuiID GetWidgetPopupId( ImGuiID widget_id, const char * popup_name )
    {
        ImGui::PushOverrideID( widget_id );
        const ImGuiID popup_id = ImGui::GetID( popup_name );
        ImGui::PopID();
        return popup_id;
    }

    static int ResolveComboPopupMaxItems( ImGuiComboFlags flags )
    {
        if ( flags & ImGuiComboFlags_HeightSmall )
            return 4;
        if ( flags & ImGuiComboFlags_HeightLarge )
            return 20;
        if ( flags & ImGuiComboFlags_HeightLargest )
            return -1;
        return 8;
    }

    static float CalcComboPopupMaxHeight( int max_items, float row_h )
    {
        if ( max_items <= 0 )
            return FLT_MAX;
        const ImFluentStyle & style = ImFluent::GetStyle();
        const float spacing_y       = ImGui::GetStyle().ItemSpacing.y;
        const float padding_y       = ImFluent::FluentDpx( style.SpacingXSmall );
        return row_h * max_items + spacing_y * ImMax( 0, max_items - 1 ) + padding_y * 2.f;
    }

    static void RenderChevronLerp( ImDrawList * dl, float cx, float cy, float L, float t, ImU32 col, float th, bool point_up_at_zero = false )
    {
        const float s = point_up_at_zero ? -1.f : 1.f;
        const ImVec2 a( cx - L, cy + s * ( -L * 0.5f + t * L ) );
        const ImVec2 b( cx, cy + s * ( L * 0.5f - t * L ) );
        const ImVec2 c( cx + L, cy + s * ( -L * 0.5f + t * L ) );
        dl->AddLine( a, b, col, th );
        dl->AddLine( b, c, col, th );
    }

    static void RenderComboChevron( ImDrawList * dl, const ImRect & chev_bb, ImU32 col, ImGuiID anim_id, bool open )
    {
        const ImFluentStyle & style = ImFluent::GetStyle();
        const float L               = ImFluent::FluentDpx( style.ChevronGlyphSize );
        const float th              = ImFluent::FluentDpx( style.StrokeMedium );
        const float t               = ImFluent::AnimateFloat( anim_id, open ? 1.f : 0.f, 0.20f );
        const float cx              = ( chev_bb.Min.x + chev_bb.Max.x ) * 0.5f;
        const float cy              = ( chev_bb.Min.y + chev_bb.Max.y ) * 0.5f;
        RenderChevronLerp( dl, cx, cy, L, t, col, th );
    }

    static bool ButtonExStyled( const char * label, const ImVec2 & size_arg, ImU32 fillRest, ImU32 fillHover, ImU32 fillPress, ImU32 fillDisabled, ImU32 textCol, ImU32 textDisabled, ImU32 stroke, ImU32 strokeTop, ImU32 strokeBottom, ImGuiButtonFlags flags )
    {
        ImGuiWindow * w = ImGui::GetCurrentWindow();
        if ( w->SkipItems )
            return false;

        const ImFluentStyle & style = ImFluent::GetStyle();
        const ImGuiID id            = w->GetID( label );
        const float pad_x           = FluentDpx( style.ControlContentPadding.x );
        const float pad_y           = FluentDpx( style.ControlContentPadding.y );
        const float min_h           = FluentDpx( style.ControlHeight );
        const ImVec2 sz             = CalcButtonSize( label, size_arg, min_h, pad_x, pad_y );
        const ImVec2 pos            = w->DC.CursorPos;
        const ImRect bb( pos, ImVec2( pos.x + sz.x, pos.y + sz.y ) );
        ImGui::ItemSize( bb );
        if ( !ImGui::ItemAdd( bb, id ) )
            return false;

        bool hovered = false, held = false;
        const bool pressed  = ImGui::ButtonBehavior( bb, id, &hovered, &held, flags );
        const bool disabled = ImFluent::IsCurrentItemDisabled();
        ReportItemInfo( id, label );

        ImU32 fillTarget;
        if ( disabled )
            fillTarget = fillDisabled;
        else if ( held )
            fillTarget = fillPress;
        else if ( hovered )
            fillTarget = fillHover;
        else
            fillTarget = fillRest;

        const ImU32 fillAnim = AnimateColorU32( id, fillTarget );
        const float r        = FluentDpx( style.ControlCornerRadius );
        ImDrawList * dl      = w->DrawList;

        FillInsideBorder( dl, bb.Min, bb.Max, fillAnim, r );
        if ( !held && !disabled )
            RenderElevationBorder( dl, bb, r, stroke, strokeTop, strokeBottom, 1.f );
        else
            ImFluent::StrokeRect( dl, bb.Min, bb.Max, stroke, r );

        RenderButtonLabel( dl, bb, label, disabled ? textDisabled : textCol, pad_x, pad_y );

        RenderNavFocusRing( id, bb, r );
        return pressed;
    }
} // namespace ImFluent

// [SECTION] Style & Theme

// Metrics and texts once; SetThemePreset() then changes only the colours.
IMGUI_API ImFluentStyle::ImFluentStyle()
{
    ImFluent::FillCommon( *this );
    ImFluent::BuildThemePalette( *this, g_Ctx.Preset );
}

ImFluentStyle & ImFluent::GetStyle()
{
    return g_Ctx.Style;
}

IMGUI_API ImFluentActions::ImFluentActions()
{
    RevealPassword      = ImGuiMod_Alt | ImGuiKey_F8;
    TogglePopup         = ImGuiKey_F4;
    OpenPopup           = ImGuiMod_Alt | ImGuiKey_DownArrow;
    ClosePopup          = ImGuiMod_Alt | ImGuiKey_UpArrow;
    PickerOpenCommit    = ImGuiMod_Alt | ImGuiKey_DownArrow;
    PickerOpenCommitAlt = ImGuiMod_Alt | ImGuiKey_UpArrow;
    NextTab             = ImGuiMod_Ctrl | ImGuiKey_Tab;
    PreviousTab         = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Tab;
    CloseTab            = ImGuiMod_Ctrl | ImGuiKey_F4;
    CalendarZoomOut     = ImGuiMod_Ctrl | ImGuiKey_UpArrow;
    CalendarZoomIn      = ImGuiMod_Ctrl | ImGuiKey_DownArrow;
    FirstItem           = ImGuiKey_Home;
    LastItem            = ImGuiKey_End;
    PageUp              = ImGuiKey_PageUp;
    PageDown            = ImGuiKey_PageDown;
    CloseDialog         = ImGuiKey_Escape;
    RepeatStepButtons   = true;
}

ImFluentActions & ImFluent::GetActions()
{
    return g_Ctx.Actions;
}

float ImFluent::FluentDpx( float v )
{
    return v * ImGui::GetStyle().FontScaleDpi;
}
ImVec2 ImFluent::FluentDpx( const ImVec2 & v )
{
    return ImVec2( FluentDpx( v.x ), FluentDpx( v.y ) );
}


ImU32 ImFluent::GetColorU32( ImFluentCol idx, float alpha_mul )
{
    if ( idx < 0 || idx >= ImFluentCol_COUNT )
        return 0;
    const ImU32 c = ( ImColor )g_Ctx.Style.Colors[ idx ];
    if ( alpha_mul >= 1.f )
        return c;
    if ( alpha_mul <= 0.f )
        return c & ~IM_COL32_A_MASK;
    const ImU32 a = ( ImU32 )( ( float )( ( c >> IM_COL32_A_SHIFT ) & 0xFF ) * alpha_mul );
    return ( c & ~IM_COL32_A_MASK ) | ( a << IM_COL32_A_SHIFT );
}

const ImVec4 & ImFluent::GetStyleColorVec4( ImFluentCol idx )
{
    static const ImVec4 zero( 0, 0, 0, 0 );
    if ( idx < 0 || idx >= ImFluentCol_COUNT )
        return zero;
    return g_Ctx.Style.Colors[ idx ];
}

ImFluentThemePreset ImFluent::GetThemePreset()
{
    return g_Ctx.Preset;
}

void ImFluent::SetAccentColor( const ImColor & color )
{
    g_Ctx.HasUserAccent = true;
    g_Ctx.UserAccent    = color.Value;
    SetThemePreset( g_Ctx.Preset );
}

ImColor ImFluent::GetAccentColor()
{
    if ( g_Ctx.HasUserAccent )
        return ImColor( g_Ctx.UserAccent );
    return ImColor( g_Ctx.Style.Colors[ ImFluentCol_AccentFillDefault ] );
}

bool ImFluent::HasUserAccentColor()
{
    return g_Ctx.HasUserAccent;
}

// [SECTION] Backdrop (Mica)
// The blurred, tinted wallpaper in a small texture behind all windows, mapped to the monitor so it stays still.

namespace
{
    struct MicaParams
    {
        ImVec4 Tint;
        float TintOpacity;
        float LuminosityOpacity;
    };

    // WinUI MicaController defaults per kind and theme.
    MicaParams GetMicaParams( ImFluentBackdrop kind, bool dark )
    {
        if ( kind == ImFluentBackdrop_MicaAlt )
            return dark ? MicaParams{ ImColor( 0x0A, 0x0A, 0x0A ).Value, 0.f, 1.f } : MicaParams{ ImColor( 0xDA, 0xDA, 0xDA ).Value, 0.5f, 1.f };
        return dark ? MicaParams{ ImColor( 0x20, 0x20, 0x20 ).Value, 0.8f, 1.f } : MicaParams{ ImColor( 0xF3, 0xF3, 0xF3 ).Value, 0.5f, 1.f };
    }

    // W3C compositing Lum and SetLum (with ClipColor).
    float Lum( const ImVec4 & c )
    {
        return 0.3f * c.x + 0.59f * c.y + 0.11f * c.z;
    }

    ImVec4 SetLum( ImVec4 c, float l )
    {
        const float d = l - Lum( c );
        c.x += d;
        c.y += d;
        c.z += d;
        const float lum = Lum( c );
        const float n   = ImMin( c.x, ImMin( c.y, c.z ) );
        const float x   = ImMax( c.x, ImMax( c.y, c.z ) );
        float * ch[ 3 ] = { &c.x, &c.y, &c.z };
        for ( float * v : ch )
        {
            if ( n < 0.f )
                *v = lum + ( *v - lum ) * lum / ( lum - n );
            if ( x > 1.f )
                *v = lum + ( *v - lum ) * ( 1.f - lum ) / ( x - lum );
        }
        return c;
    }

    ImVec4 Mix( const ImVec4 & a, const ImVec4 & b, float t )
    {
        return ImVec4( a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, a.z + ( b.z - a.z ) * t, 1.f );
    }

    ImVec4 SampleWallpaper( const ImFluentWallpaper & wp, float u, float v )
    {
        if ( wp.Repeat )
        {
            u -= ImFloor( u );
            v -= ImFloor( v );
        }
        else if ( u < 0.f || v < 0.f || u >= 1.f || v >= 1.f )
            return ImGui::ColorConvertU32ToFloat4( wp.BackgroundColor );
        const int x             = ImClamp( ( int )( u * wp.Width ), 0, wp.Width - 1 );
        const int y             = ImClamp( ( int )( v * wp.Height ), 0, wp.Height - 1 );
        const unsigned char * p = wp.Pixels + ( ( size_t )y * wp.Width + x ) * 4;
        return ImVec4( p[ 0 ] / 255.f, p[ 1 ] / 255.f, p[ 2 ] / 255.f, 1.f );
    }

    void BoxBlur( ImVec4 * img, ImVec4 * tmp, int n, int r, bool vertical )
    {
        const float inv = 1.f / ( 2 * r + 1 );
        for ( int line = 0; line < n; ++line )
        {
            auto at = [ & ]( int i ) -> ImVec4 &
            {
                return vertical ? img[ ImClamp( i, 0, n - 1 ) * n + line ] : img[ line * n + ImClamp( i, 0, n - 1 ) ];
            };
            ImVec4 sum( 0.f, 0.f, 0.f, 0.f );
            for ( int i = -r; i <= r; ++i )
                sum = ImVec4( sum.x + at( i ).x, sum.y + at( i ).y, sum.z + at( i ).z, 1.f );
            for ( int i = 0; i < n; ++i )
            {
                tmp[ i ]           = ImVec4( sum.x * inv, sum.y * inv, sum.z * inv, 1.f );
                const ImVec4 & add = at( i + r + 1 );
                const ImVec4 & sub = at( i - r );
                sum                = ImVec4( sum.x + add.x - sub.x, sum.y + add.y - sub.y, sum.z + add.z - sub.z, 1.f );
            }
            for ( int i = 0; i < n; ++i )
                ( vertical ? img[ i * n + line ] : img[ line * n + i ] ) = tmp[ i ];
        }
    }

    void BuildBackdropTexture( const ImFluentWallpaper & wp, const ImVec2 & monitor_size, ImFluentBackdrop kind, bool dark )
    {
        ImFluentBackdropState & bd = g_Ctx.Backdrop;
        const int n                = ImFluentBackdropState::TexSize;
        ImVector< ImVec4 > img, tmp;
        img.resize( n * n );
        tmp.resize( n );

        const int ss = 4;
        for ( int y = 0; y < n; ++y )
            for ( int x = 0; x < n; ++x )
            {
                ImVec4 sum( 0.f, 0.f, 0.f, 0.f );
                for ( int sy = 0; sy < ss; ++sy )
                    for ( int sx = 0; sx < ss; ++sx )
                    {
                        const float fx = ( x + ( sx + 0.5f ) / ss ) / n;
                        const float fy = ( y + ( sy + 0.5f ) / ss ) / n;
                        const ImVec4 c = SampleWallpaper( wp, wp.UV0.x + ( wp.UV1.x - wp.UV0.x ) * fx, wp.UV0.y + ( wp.UV1.y - wp.UV0.y ) * fy );
                        sum            = ImVec4( sum.x + c.x, sum.y + c.y, sum.z + c.z, 1.f );
                    }
                img[ y * n + x ] = ImVec4( sum.x / ( ss * ss ), sum.y / ( ss * ss ), sum.z / ( ss * ss ), 1.f );
            }

        // Three box passes ~ Gaussian; ry follows the aspect ratio so the blur is round on screen.
        const float aspect = ( monitor_size.x > 0.f && monitor_size.y > 0.f ) ? monitor_size.x / monitor_size.y : 16.f / 9.f;
        const int rx       = 3;
        const int ry       = ImMax( 1, ( int )( rx * aspect + 0.5f ) );
        for ( int pass = 0; pass < 3; ++pass )
        {
            BoxBlur( img.Data, tmp.Data, n, rx, false );
            BoxBlur( img.Data, tmp.Data, n, ry, true );
        }

        // Mica: the tint's luminosity with the wallpaper's hue and saturation, then the tint on top.
        const MicaParams mp = GetMicaParams( kind, dark );
        const float tint_l  = Lum( mp.Tint );
        if ( !bd.Texture )
        {
            bd.Texture = IM_NEW( ImTextureData )();
            bd.Texture->Create( ImTextureFormat_RGBA32, n, n );
            ImGui::RegisterUserTexture( bd.Texture );
        }
        for ( int i = 0; i < n * n; ++i )
        {
            const ImVec4 lum_layer = Mix( img[ i ], SetLum( img[ i ], tint_l ), mp.LuminosityOpacity );
            const ImVec4 c         = Mix( lum_layer, SetLum( mp.Tint, Lum( lum_layer ) ), mp.TintOpacity );
            unsigned char * p      = bd.Texture->Pixels + i * 4;
            p[ 0 ]                 = ( unsigned char )ImClamp( c.x * 255.f + 0.5f, 0.f, 255.f );
            p[ 1 ]                 = ( unsigned char )ImClamp( c.y * 255.f + 0.5f, 0.f, 255.f );
            p[ 2 ]                 = ( unsigned char )ImClamp( c.z * 255.f + 0.5f, 0.f, 255.f );
            p[ 3 ]                 = 255;
        }
        bd.Texture->UseColors = true;
        if ( bd.Texture->Status != ImTextureStatus_WantCreate )
        {
            ImTextureRect all = { 0, 0, ( unsigned short )n, ( unsigned short )n };
            bd.Texture->Updates.resize( 0 );
            bd.Texture->Updates.push_back( all );
            bd.Texture->UpdateRect = all;
            bd.Texture->UsedRect   = all;
            bd.Texture->Status     = ImTextureStatus_WantUpdates;
        }
        bd.HasImage         = true;
        bd.BuiltSerial      = wp.Serial;
        bd.BuiltKind        = kind;
        bd.BuiltPreset      = g_Ctx.Preset;
        bd.BuiltMonitorSize = monitor_size;
    }
} // namespace

void ImFluent::SetBackdrop( ImFluentBackdrop backdrop )
{
    g_Ctx.Backdrop.Kind     = backdrop;
    g_Ctx.Backdrop.NextPoll = 0.0;
}

ImFluentBackdrop ImFluent::GetBackdrop()
{
    return g_Ctx.Backdrop.Kind;
}

ImFluentBackdropHooks & ImFluent::GetBackdropHooks()
{
    return g_Ctx.Backdrop.Hooks;
}

bool ImFluent::IsBackdropVisible()
{
    // WinUI turns Mica off in high contrast.
    return g_Ctx.Backdrop.Kind != ImFluentBackdrop_None && g_Ctx.Preset != ImFluentThemePreset_HighContrast;
}

void ImFluent::PlaceWallpaper( ImFluentWallpaper * wp, ImFluentWallpaperFit fit, float image_w, float image_h, const ImVec4 & monitor, const ImVec4 & desktop )
{
    const float mw = monitor.z - monitor.x, mh = monitor.w - monitor.y;
    wp->Repeat = false;
    if ( fit == ImFluentWallpaperFit_Stretch || image_w <= 0.f || image_h <= 0.f || mw <= 0.f || mh <= 0.f )
    {
        wp->UV0 = ImVec2( 0.f, 0.f );
        wp->UV1 = ImVec2( 1.f, 1.f );
        return;
    }
    if ( fit == ImFluentWallpaperFit_Tile )
    {
        wp->UV0    = ImVec2( 0.f, 0.f );
        wp->UV1    = ImVec2( mw / image_w, mh / image_h );
        wp->Repeat = true;
        return;
    }
    // The image is centred on `area` at `s` screen units per image pixel.
    const ImVec4 area = ( fit == ImFluentWallpaperFit_Span && desktop.z > desktop.x && desktop.w > desktop.y ) ? desktop : monitor;
    const float aw = area.z - area.x, ah = area.w - area.y;
    float s = 1.f;
    if ( fit == ImFluentWallpaperFit_Fill || fit == ImFluentWallpaperFit_Span )
        s = ImMax( aw / image_w, ah / image_h );
    else if ( fit == ImFluentWallpaperFit_Fit )
        s = ImMin( aw / image_w, ah / image_h );
    const float ox = area.x + ( aw - image_w * s ) * 0.5f;
    const float oy = area.y + ( ah - image_h * s ) * 0.5f;
    wp->UV0        = ImVec2( ( monitor.x - ox ) / ( image_w * s ), ( monitor.y - oy ) / ( image_h * s ) );
    wp->UV1        = ImVec2( ( monitor.z - ox ) / ( image_w * s ), ( monitor.w - oy ) / ( image_h * s ) );
}

void ImFluent::RenderBackdrop( ImGuiViewport * vp )
{
    ImFluentBackdropState & bd          = g_Ctx.Backdrop;
    const ImFluentBackdropHooks & hooks = bd.Hooks;
    if ( !vp )
        vp = ImGui::GetMainViewport();
    const bool visible = IsBackdropVisible();
    const bool dark    = g_Ctx.Preset == ImFluentThemePreset_Dark;

    if ( hooks.SetNativeBackdrop && ( bd.NativeKind != ( visible ? bd.Kind : ImFluentBackdrop_None ) || bd.NativePreset != g_Ctx.Preset ) )
    {
        bd.NativeKind   = visible ? bd.Kind : ImFluentBackdrop_None;
        bd.NativePreset = g_Ctx.Preset;
        bd.Native       = hooks.SetNativeBackdrop( vp, bd.NativeKind, dark, hooks.UserData ) && visible;
    }
    if ( !visible || bd.Native )
        return;

    ImVec4 client, monitor;
    const bool has_rects  = hooks.GetScreenRects && hooks.GetScreenRects( vp, &client, &monitor, hooks.UserData ) && monitor.z > monitor.x && monitor.w > monitor.y;
    const ImVec2 mon_size = has_rects ? ImVec2( monitor.z - monitor.x, monitor.w - monitor.y ) : vp->Size;

    // Poll about once a second, and at once after a kind or theme change.
    const double now = ImGui::GetTime();
    if ( bd.BuiltKind != bd.Kind || bd.BuiltPreset != g_Ctx.Preset )
        bd.NextPoll = 0.0;
    if ( hooks.GetWallpaper && now >= bd.NextPoll )
    {
        bd.NextPoll = now + 1.0;
        ImFluentWallpaper wp;
        if ( !hooks.GetWallpaper( vp, &wp, hooks.UserData ) || !wp.Pixels || wp.Width <= 0 || wp.Height <= 0 )
            bd.HasImage = false;
        else if ( !bd.HasImage || wp.Serial != bd.BuiltSerial || bd.BuiltKind != bd.Kind || bd.BuiltPreset != g_Ctx.Preset || bd.BuiltMonitorSize.x != mon_size.x || bd.BuiltMonitorSize.y != mon_size.y )
            BuildBackdropTexture( wp, mon_size, bd.Kind, dark );
    }

    // Mica shows its fallback colour while the app is not focused.
    bd.Active = ImLinearSweep( bd.Active, ImGui::GetIO().AppFocusLost ? 0.f : 1.f, ImGui::GetIO().DeltaTime / 0.25f );

    ImDrawList * dl = ImGui::GetBackgroundDrawList( vp );
    const ImVec2 p0 = vp->Pos;
    const ImVec2 p1( vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y );
    dl->AddRectFilled( p0, p1, GetColorU32( ImFluentCol_SolidBgBase ) );
    if ( bd.HasImage && bd.Texture && bd.Active > 0.f )
    {
        ImVec2 uv0( 0.f, 0.f ), uv1( 1.f, 1.f );
        if ( has_rects )
        {
            uv0 = ImVec2( ( client.x - monitor.x ) / mon_size.x, ( client.y - monitor.y ) / mon_size.y );
            uv1 = ImVec2( ( client.z - monitor.x ) / mon_size.x, ( client.w - monitor.y ) / mon_size.y );
        }
        dl->AddImage( bd.Texture->GetTexRef(), p0, p1, uv0, uv1, IM_COL32( 255, 255, 255, ( int )( bd.Active * 255.f ) ) );
    }
}

void ImFluent::SetThemePreset( ImFluentThemePreset preset )
{
    g_Ctx.Preset = preset;
    BuildThemePalette( g_Ctx.Style, preset );

    ImGuiStyle & s    = ImGui::GetStyle();
    ImVec4 * c        = s.Colors;
    const ImVec4 * fc = g_Ctx.Style.Colors;

    c[ ImGuiCol_Text ]                  = fc[ ImFluentCol_TextPrimary ];
    c[ ImGuiCol_TextDisabled ]          = fc[ ImFluentCol_TextDisabled ];
    c[ ImGuiCol_WindowBg ]              = fc[ ImFluentCol_SolidBgBase ];
    c[ ImGuiCol_ChildBg ]               = fc[ ImFluentCol_LayerFillDefault ];
    c[ ImGuiCol_PopupBg ]               = fc[ ImFluentCol_LayerFillAlt ];
    c[ ImGuiCol_Border ]                = fc[ ImFluentCol_ControlStrokeDefault ];
    c[ ImGuiCol_BorderShadow ]          = fc[ ImFluentCol_ElevationControlBottom ];
    c[ ImGuiCol_FrameBg ]               = fc[ ImFluentCol_ControlFillDefault ];
    c[ ImGuiCol_FrameBgHovered ]        = fc[ ImFluentCol_ControlFillSecondary ];
    c[ ImGuiCol_FrameBgActive ]         = fc[ ImFluentCol_ControlFillTertiary ];
    c[ ImGuiCol_TitleBg ]               = fc[ ImFluentCol_SolidBgBase ];
    c[ ImGuiCol_TitleBgActive ]         = fc[ ImFluentCol_SolidBgBase ];
    c[ ImGuiCol_TitleBgCollapsed ]      = fc[ ImFluentCol_SolidBgBase ];
    c[ ImGuiCol_MenuBarBg ]             = fc[ ImFluentCol_LayerFillDefault ];
    c[ ImGuiCol_ScrollbarBg ]           = fc[ ImFluentCol_ControlFillTransparent ];
    c[ ImGuiCol_ScrollbarGrab ]         = fc[ ImFluentCol_ControlStrongFillDefault ];
    c[ ImGuiCol_ScrollbarGrabHovered ]  = fc[ ImFluentCol_ControlStrongFillDefault ];
    c[ ImGuiCol_ScrollbarGrabActive ]   = fc[ ImFluentCol_ControlStrongFillDefault ];
    c[ ImGuiCol_CheckMark ]                 = fc[ ImFluentCol_AccentFillDefault ];
    c[ ImGuiCol_SliderGrab ]            = fc[ ImFluentCol_AccentFillDefault ];
    c[ ImGuiCol_SliderGrabActive ]      = fc[ ImFluentCol_AccentFillSecondary ];
    c[ ImGuiCol_Button ]                = fc[ ImFluentCol_ControlFillDefault ];
    c[ ImGuiCol_ButtonHovered ]         = fc[ ImFluentCol_ControlFillSecondary ];
    c[ ImGuiCol_ButtonActive ]          = fc[ ImFluentCol_ControlFillTertiary ];
    // Selection: the accent at the WinUI DataGrid opacities.
    const ImVec4 accent                     = fc[ ImFluentCol_AccentFillDefault ];
    c[ ImGuiCol_Header ]                    = ImVec4( accent.x, accent.y, accent.z, 0.4f );
    c[ ImGuiCol_HeaderHovered ]             = ImVec4( accent.x, accent.y, accent.z, 0.2f );
    c[ ImGuiCol_HeaderActive ]              = ImVec4( accent.x, accent.y, accent.z, 0.6f );
    c[ ImGuiCol_Separator ]             = fc[ ImFluentCol_DividerStrokeDefault ];
    c[ ImGuiCol_SeparatorHovered ]      = fc[ ImFluentCol_DividerStrokeDefault ];
    c[ ImGuiCol_SeparatorActive ]       = fc[ ImFluentCol_DividerStrokeDefault ];
    c[ ImGuiCol_ResizeGrip ]            = fc[ ImFluentCol_ControlStrokeDefault ];
    c[ ImGuiCol_ResizeGripHovered ]     = fc[ ImFluentCol_ControlStrokeSecondary ];
    c[ ImGuiCol_ResizeGripActive ]      = fc[ ImFluentCol_ControlStrokeSecondary ];
    c[ ImGuiCol_Tab ]                   = fc[ ImFluentCol_SubtleFillTransparent ];
    c[ ImGuiCol_TabHovered ]            = fc[ ImFluentCol_SubtleFillSecondary ];
    c[ ImGuiCol_TabSelected ]           = fc[ ImFluentCol_LayerFillAlt ];
    c[ ImGuiCol_TabDimmed ]             = fc[ ImFluentCol_SubtleFillTransparent ];
    c[ ImGuiCol_TabDimmedSelected ]     = fc[ ImFluentCol_LayerFillAlt ];
    c[ ImGuiCol_TabSelectedOverline ]       = fc[ ImFluentCol_AccentFillDefault ];
    c[ ImGuiCol_TabDimmedSelectedOverline ] = fc[ ImFluentCol_AccentFillDefault ];
    c[ ImGuiCol_PlotLines ]                 = fc[ ImFluentCol_AccentFillDefault ];
    c[ ImGuiCol_PlotLinesHovered ]          = fc[ ImFluentCol_AccentFillSecondary ];
    c[ ImGuiCol_PlotHistogram ]             = fc[ ImFluentCol_AccentFillDefault ];
    c[ ImGuiCol_PlotHistogramHovered ]      = fc[ ImFluentCol_AccentFillSecondary ];
    c[ ImGuiCol_TextLink ]                  = fc[ ImFluentCol_AccentTextPrimary ];
    c[ ImGuiCol_InputTextCursor ]           = fc[ ImFluentCol_TextPrimary ];
    c[ ImGuiCol_TreeLines ]                 = fc[ ImFluentCol_DividerStrokeDefault ];
    c[ ImGuiCol_DragDropTarget ]            = fc[ ImFluentCol_AccentFillDefault ];
    c[ ImGuiCol_DragDropTargetBg ]          = ImVec4( accent.x, accent.y, accent.z, 0.1f );
    c[ ImGuiCol_UnsavedMarker ]             = fc[ ImFluentCol_TextPrimary ];
    c[ ImGuiCol_TableHeaderBg ]         = fc[ ImFluentCol_LayerFillDefault ];
    c[ ImGuiCol_TableBorderStrong ]     = fc[ ImFluentCol_DividerStrokeDefault ];
    c[ ImGuiCol_TableBorderLight ]      = fc[ ImFluentCol_DividerStrokeDefault ];
    c[ ImGuiCol_TableRowBg ]            = fc[ ImFluentCol_SubtleFillTransparent ];
    c[ ImGuiCol_TableRowBgAlt ]         = fc[ ImFluentCol_SubtleFillSecondary ];
    c[ ImGuiCol_TextSelectedBg ]        = fc[ ImFluentCol_AccentFillSelectedTextBg ];
    c[ ImGuiCol_NavCursor ]             = fc[ ImFluentCol_FocusStrokeOuter ];
    c[ ImGuiCol_NavWindowingHighlight ] = fc[ ImFluentCol_FocusStrokeOuter ];
    c[ ImGuiCol_NavWindowingDimBg ]     = fc[ ImFluentCol_SmokeFill ];
    c[ ImGuiCol_ModalWindowDimBg ]      = fc[ ImFluentCol_SmokeFill ];

    s.WindowPadding     = ImTrunc( ImVec2( 16.f * s.FontScaleDpi, 16.f * s.FontScaleDpi ) );
    s.FramePadding      = ImTrunc( ImVec2( g_Ctx.Style.ControlContentPadding.x * s.FontScaleDpi, g_Ctx.Style.ControlContentPadding.y * s.FontScaleDpi ) );
    s.ItemSpacing       = ImTrunc( ImVec2( 8.f * s.FontScaleDpi, 4.f * s.FontScaleDpi ) );
    s.ItemInnerSpacing  = ImTrunc( ImVec2( 8.f * s.FontScaleDpi, 4.f * s.FontScaleDpi ) );
    s.IndentSpacing     = ImTrunc( 16.f * s.FontScaleDpi );
    s.ScrollbarSize     = ImTrunc( 12.f * s.FontScaleDpi );
    s.GrabMinSize       = ImTrunc( 16.f * s.FontScaleDpi );
    s.WindowBorderSize  = ImMax( 1.f, IM_ROUND( 1.f * s.FontScaleDpi ) );
    s.ChildBorderSize   = ImMax( 1.f, IM_ROUND( 1.f * s.FontScaleDpi ) );
    s.PopupBorderSize   = ImMax( 1.f, IM_ROUND( 1.f * s.FontScaleDpi ) );
    s.FrameBorderSize   = ImMax( 1.f, IM_ROUND( 1.f * s.FontScaleDpi ) );
    s.WindowRounding    = ImTrunc( g_Ctx.Style.OverlayCornerRadius * s.FontScaleDpi );
    s.ChildRounding     = ImTrunc( g_Ctx.Style.ControlCornerRadius * s.FontScaleDpi );
    s.PopupRounding     = ImTrunc( g_Ctx.Style.OverlayCornerRadius * s.FontScaleDpi );
    s.FrameRounding     = ImTrunc( g_Ctx.Style.ControlCornerRadius * s.FontScaleDpi );
    s.GrabRounding      = ImTrunc( g_Ctx.Style.ControlCornerRadius * s.FontScaleDpi );
    s.ScrollbarRounding = ImTrunc( g_Ctx.Style.OverlayCornerRadius * s.FontScaleDpi );
    s.TabRounding       = ImTrunc( g_Ctx.Style.ControlCornerRadius * s.FontScaleDpi );
}

// [SECTION] Style stack (PushFluentStyle / PushStyleColor / PushStyleVar / BeginDisabled)

void ImFluent::PushFluentStyle()
{
    if ( g_Ctx.Fonts[ ImFluentTextStyle_Body ].Font )
        ImFluent::PushFont( ImFluentTextStyle_Body );
    else
        ImGui::PushFont( ImGui::GetFont() );
}
void ImFluent::PopFluentStyle()
{
    ImGui::PopFont();
}

static float * StyleVarFloatPtr( ImFluentStyle & s, ImFluentStyleVar idx )
{
    switch ( idx )
    {
        case ImFluentStyleVar_ControlCornerRadius:
            return &s.ControlCornerRadius;
        case ImFluentStyleVar_OverlayCornerRadius:
            return &s.OverlayCornerRadius;
        case ImFluentStyleVar_ControlHeight:
            return &s.ControlHeight;
        case ImFluentStyleVar_ControlMinWidth:
            return &s.ControlMinWidth;
        case ImFluentStyleVar_CardPadding:
            return &s.CardPadding;
        case ImFluentStyleVar_SpacingXSmall:
            return &s.SpacingXSmall;
        case ImFluentStyleVar_SpacingSmall:
            return &s.SpacingSmall;
        case ImFluentStyleVar_SpacingMedium:
            return &s.SpacingMedium;
        case ImFluentStyleVar_SpacingLarge:
            return &s.SpacingLarge;
        case ImFluentStyleVar_SpacingXLarge:
            return &s.SpacingXLarge;
        case ImFluentStyleVar_SpacingXXLarge:
            return &s.SpacingXXLarge;
        case ImFluentStyleVar_StrokeThin:
            return &s.StrokeThin;
        case ImFluentStyleVar_StrokeMedium:
            return &s.StrokeMedium;
        case ImFluentStyleVar_StrokeThick:
            return &s.StrokeThick;
        case ImFluentStyleVar_FocusStrokeThicknessOuter:
            return &s.FocusStrokeThicknessOuter;
        case ImFluentStyleVar_FocusStrokeThicknessInner:
            return &s.FocusStrokeThicknessInner;
        case ImFluentStyleVar_NavPaneCompactWidth:
            return &s.NavPaneCompactWidth;
        case ImFluentStyleVar_NavPaneOpenWidth:
            return &s.NavPaneOpenWidth;
        default:
            return NULL;
    }
}

static ImVec2 * StyleVarVec2Ptr( ImFluentStyle & s, ImFluentStyleVar idx )
{
    if ( idx == ImFluentStyleVar_ControlContentPadding )
        return &s.ControlContentPadding;
    return NULL;
}

void ImFluent::PushStyleColor( ImFluentCol idx, ImU32 col )
{
    PushStyleColor( idx, ImGui::ColorConvertU32ToFloat4( col ) );
}

void ImFluent::PushStyleColor( ImFluentCol idx, const ImVec4 & col )
{
    if ( idx < 0 || idx >= ImFluentCol_COUNT )
        return;
    ImFluentColorStackEntry e;
    e.Idx  = idx;
    e.Prev = g_Ctx.Style.Colors[ idx ];
    g_Ctx.ColorStack.push_back( e );
    g_Ctx.Style.Colors[ idx ] = col;
}

void ImFluent::PopStyleColor( int count )
{
    while ( count > 0 && !g_Ctx.ColorStack.empty() )
    {
        const ImFluentColorStackEntry & e = g_Ctx.ColorStack.back();
        g_Ctx.Style.Colors[ e.Idx ]       = e.Prev;
        g_Ctx.ColorStack.pop_back();
        --count;
    }
}

void ImFluent::PushStyleVar( ImFluentStyleVar idx, float val )
{
    float * p = StyleVarFloatPtr( g_Ctx.Style, idx );
    if ( !p )
    {
        IM_ASSERT( 0 && "PushStyleVar: idx is not a float" );
        return;
    }
    ImFluentStyleVarStackEntry e;
    e.Idx  = idx;
    e.Kind = ImFluentStyleVarKind_Float;
    e.Prev = ImVec2( *p, 0.f );
    g_Ctx.StyleVarStack.push_back( e );
    *p = val;
}

void ImFluent::PushStyleVar( ImFluentStyleVar idx, const ImVec2 & val )
{
    ImVec2 * p = StyleVarVec2Ptr( g_Ctx.Style, idx );
    if ( !p )
    {
        IM_ASSERT( 0 && "PushStyleVar: idx is not a Vec2" );
        return;
    }
    ImFluentStyleVarStackEntry e;
    e.Idx  = idx;
    e.Kind = ImFluentStyleVarKind_Vec2;
    e.Prev = *p;
    g_Ctx.StyleVarStack.push_back( e );
    *p = val;
}

void ImFluent::PopStyleVar( int count )
{
    while ( count > 0 && !g_Ctx.StyleVarStack.empty() )
    {
        const ImFluentStyleVarStackEntry & e = g_Ctx.StyleVarStack.back();
        if ( e.Kind == ImFluentStyleVarKind_Float )
        {
            float * p = StyleVarFloatPtr( g_Ctx.Style, e.Idx );
            if ( p )
                *p = e.Prev.x;
        }
        else
        {
            ImVec2 * p = StyleVarVec2Ptr( g_Ctx.Style, e.Idx );
            if ( p )
                *p = e.Prev;
        }
        g_Ctx.StyleVarStack.pop_back();
        --count;
    }
}

void ImFluent::BeginDisabled( bool disabled )
{
    ImGui::BeginDisabled( disabled );
}

void ImFluent::EndDisabled()
{
    ImGui::EndDisabled();
}

// [SECTION] Fonts (LoadFluentFonts / PushFont / PopFont)

static bool FontFileExists( const char * path )
{
    if ( !path || !path[ 0 ] )
        return false;
    ImFileHandle f = ImFileOpen( path, "rb" );
    if ( !f )
        return false;
    ImFileClose( f );
    return true;
}

static const char * FirstExistingFont( const char * const * candidates, int count )
{
    for ( int i = 0; i < count; ++i )
        if ( FontFileExists( candidates[ i ] ) )
            return candidates[ i ];
    return NULL;
}

// Text font with the icon font merged in. Missing files fall back to Dear ImGui's default font and no icons.
static ImFont * LoadFontAndMergeWithIcons( const char * font_path, const char * icons_path )
{
    ImFontConfig cfg;
    cfg.SizePixels = 16.0;
    ImFont * font  = FontFileExists( font_path ) ? ImGui::GetIO().Fonts->AddFontFromFileTTF( font_path, 0.0f, &cfg ) : NULL;
    if ( !font )
        font = ImGui::GetIO().Fonts->AddFontDefault();
    if ( FontFileExists( icons_path ) )
    {
        static const ImWchar fluentIconsRange[] = { 0xE000, 0xF8FF, 0 };
        cfg.MergeMode                           = true;
        cfg.PixelSnapH                          = true;
        cfg.GlyphOffset                         = ImVec2( 0.0f, cfg.SizePixels / 8.0f );
        ImGui::GetIO().Fonts->AddFontFromFileTTF( icons_path, 0.0f, &cfg, fluentIconsRange );
    }
    return font;
}

void ImFluent::LoadFluentFonts( const char * regular_ttf, const char * semibold_ttf, const char * icons_ttf )
{
    ImFont * regularFont = LoadFontAndMergeWithIcons( regular_ttf, icons_ttf );
    ImFont * strongFont  = FontFileExists( semibold_ttf ) ? LoadFontAndMergeWithIcons( semibold_ttf, icons_ttf ) : regularFont;

    // Fluent type ramp in em; Dear ImGui sizes Segoe UI by ascent + descent = 1.33 em.
    const float em = 2724.f / 2048.f;
    SetFluentTextStyleFont( ImFluentTextStyle_Caption, regularFont, 12.f * em );
    SetFluentTextStyleFont( ImFluentTextStyle_Body, regularFont, 14.f * em );
    SetFluentTextStyleFont( ImFluentTextStyle_BodyStrong, strongFont, 14.f * em );
    SetFluentTextStyleFont( ImFluentTextStyle_Subtitle, strongFont, 20.f * em );
    SetFluentTextStyleFont( ImFluentTextStyle_Title, strongFont, 28.f * em );
    SetFluentTextStyleFont( ImFluentTextStyle_TitleLarge, strongFont, 40.f * em );
    SetFluentTextStyleFont( ImFluentTextStyle_Display, strongFont, 68.f * em );

    if ( ImGui::GetIO().FontDefault == NULL )
    {
        ImGui::GetIO().FontDefault     = g_Ctx.Fonts[ ImFluentTextStyle_Body ].Font;
        ImGui::GetStyle().FontSizeBase = g_Ctx.Fonts[ ImFluentTextStyle_Body ].FontSize;
    }
}

// Segoe UI, Segoe UI Semibold and the Segoe icon font where they are installed; elsewhere a common UI font.
void ImFluent::LoadFluentSystemFonts()
{
#if defined( _WIN32 )
    char dir[ MAX_PATH ] = { 0 };
    ::GetWindowsDirectoryA( dir, MAX_PATH );
    char regular[ MAX_PATH ], semibold[ MAX_PATH ], bold[ MAX_PATH ], icons[ MAX_PATH ], mdl2[ MAX_PATH ];
    ImFormatString( regular, MAX_PATH, "%s/Fonts/segoeui.ttf", dir );
    ImFormatString( semibold, MAX_PATH, "%s/Fonts/seguisb.ttf", dir );
    ImFormatString( bold, MAX_PATH, "%s/Fonts/segoeuib.ttf", dir );
    ImFormatString( icons, MAX_PATH, "%s/Fonts/SegoeIcons.ttf", dir ); // Windows 11
    ImFormatString( mdl2, MAX_PATH, "%s/Fonts/segmdl2.ttf", dir );     // Windows 10
    const char * semibolds[] = { semibold, bold };
    const char * iconsets[]  = { icons, mdl2 };
    LoadFluentFonts( regular, FirstExistingFont( semibolds, 2 ), FirstExistingFont( iconsets, 2 ) );
#elif defined( __APPLE__ )
    static const char * regulars[]  = { "/Library/Fonts/Segoe UI.ttf", "/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc" };
    static const char * semibolds[] = { "/Library/Fonts/Segoe UI Semibold.ttf", "/Library/Fonts/Segoe UI Bold.ttf" };
    static const char * iconsets[]  = { "/Library/Fonts/Segoe Fluent Icons.ttf", "/Library/Fonts/Segoe MDL2 Assets.ttf" };
    LoadFluentFonts( FirstExistingFont( regulars, IM_ARRAYSIZE( regulars ) ), FirstExistingFont( semibolds, IM_ARRAYSIZE( semibolds ) ), FirstExistingFont( iconsets, IM_ARRAYSIZE( iconsets ) ) );
#else
    static const char * regulars[]  = { "/usr/share/fonts/truetype/msttcorefonts/segoeui.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/google-noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf" };
    static const char * semibolds[] = { "/usr/share/fonts/truetype/msttcorefonts/seguisb.ttf", "/usr/share/fonts/truetype/noto/NotoSans-SemiBold.ttf", "/usr/share/fonts/noto/NotoSans-SemiBold.ttf", "/usr/share/fonts/google-noto/NotoSans-SemiBold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf", "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf" };
    static const char * iconsets[]  = { "/usr/share/fonts/truetype/msttcorefonts/SegoeIcons.ttf", "/usr/share/fonts/truetype/msttcorefonts/segmdl2.ttf" };
    LoadFluentFonts( FirstExistingFont( regulars, IM_ARRAYSIZE( regulars ) ), FirstExistingFont( semibolds, IM_ARRAYSIZE( semibolds ) ), FirstExistingFont( iconsets, IM_ARRAYSIZE( iconsets ) ) );
#endif
}

void ImFluent::PushFont( ImFluentTextStyle style, float size )
{
    if ( style < 0 || style >= ImFluentTextStyle_COUNT )
        style = ImFluentTextStyle_Body;

    ImFluentTextStyleFont * font = &g_Ctx.Fonts[ style ];
    ImGui::PushFont( font->Font, size > 0 ? size : font->FontSize );
}

void ImFluent::PopFont()
{
    ImGui::PopFont();
}

// [SECTION] Next-item attributes

static void MarkNextItemScope()
{
    ImFluent::DropStaleNextItem(); // Data left over by a skipped widget does not join the new data
    g_Ctx.NextItem.Window = GImGui->CurrentWindow;
    g_Ctx.NextItem.Frame  = GImGui->FrameCount;
}

void ImFluent::SetNextItemHeader( const char * text )
{
    MarkNextItemScope();
    g_Ctx.NextItem.Header    = text;
    g_Ctx.NextItem.HasHeader = ( text != NULL );
}
void ImFluent::SetNextItemDescription( const char * text )
{
    MarkNextItemScope();
    g_Ctx.NextItem.Description    = text;
    g_Ctx.NextItem.HasDescription = ( text != NULL );
}
void ImFluent::SetNextItemGlyph( const char * glyph )
{
    MarkNextItemScope();
    g_Ctx.NextItem.Glyph    = glyph;
    g_Ctx.NextItem.HasGlyph = ( glyph != NULL );
}
void ImFluent::SetNextItemError( const char * error )
{
    MarkNextItemScope();
    g_Ctx.NextItem.Error    = error;
    g_Ctx.NextItem.HasError = ( error != NULL );
}

void ImFluent::SetFluentTextStyleFont( ImFluentTextStyle style, ImFont * font, float size )
{
    IM_ASSERT( size > 0.0f );
    if ( style >= 0 && style < ImFluentTextStyle_COUNT )
        g_Ctx.Fonts[ style ] = { font, size };
}

// INKEYS PATCH: 只调整图标位置，字形仍由 ImGui 字体 atlas 绘制。
void ImFluent::SetIconFont( ImFont * font )
{
    g_Ctx.IconFont = font;
}

void ImFluent::DrawIcon( const char * glyph, const ImVec2 & min,
    const ImVec2 & max, float size_dip, ImU32 color )
{
    if ( !glyph || !*glyph ) return;
    ImGui::PushFont( g_Ctx.IconFont ? g_Ctx.IconFont : ImGui::GetFont(), size_dip );
    unsigned int codepoint = 0;
    ImTextCharFromUtf8( &codepoint, glyph, NULL );
    const ImFontGlyph * shape = ImGui::GetFontBaked()->FindGlyph( ( ImWchar )codepoint );
    if ( shape )
    {
        const ImVec2 origin( ( min.x + max.x - shape->X0 - shape->X1 ) * 0.5f,
            ( min.y + max.y - shape->Y0 - shape->Y1 ) * 0.5f );
        ImGui::GetWindowDrawList()->AddText( ImGui::GetFont(), ImGui::GetFontSize(), origin, color, glyph );
    }
    ImGui::PopFont();
}

// [SECTION] Buttons

bool ImFluent::Button( const char * label, const ImVec2 & size )
{
    return ButtonExStyled( label, size, ImFluent::GetColorU32( ImFluentCol_ControlFillDefault ), ImFluent::GetColorU32( ImFluentCol_ControlFillSecondary ), ImFluent::GetColorU32( ImFluentCol_ControlFillTertiary ), ImFluent::GetColorU32( ImFluentCol_ControlFillDisabled ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), ImFluent::GetColorU32( ImFluentCol_TextDisabled ),
        ImFluent::GetColorU32( ImFluentCol_ControlStrokeDefault ), ImFluent::GetColorU32( ImFluentCol_ElevationControlTop ), ImFluent::GetColorU32( ImFluentCol_ElevationControlBottom ), ImGuiButtonFlags_None );
}

bool ImFluent::AccentButton( const char * label, const ImVec2 & size )
{
    return ButtonExStyled( label, size, ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), ImFluent::GetColorU32( ImFluentCol_AccentFillSecondary ), ImFluent::GetColorU32( ImFluentCol_AccentFillTertiary ), ImFluent::GetColorU32( ImFluentCol_AccentFillDisabled ), ImFluent::GetColorU32( ImFluentCol_TextOnAccentPrimary ), ImFluent::GetColorU32( ImFluentCol_TextOnAccentDisabled ),
        ImFluent::GetColorU32( ImFluentCol_ControlStrokeOnAccentDefault ), ImFluent::GetColorU32( ImFluentCol_ElevationAccentTop ), ImFluent::GetColorU32( ImFluentCol_ElevationAccentBottom ), ImGuiButtonFlags_None );
}

bool ImFluent::RepeatButton( const char * label, const ImVec2 & size )
{
    ImGui::PushItemFlag( ImGuiItemFlags_ButtonRepeat, true );
    const bool pressed = ButtonExStyled( label, size, ImFluent::GetColorU32( ImFluentCol_ControlFillDefault ), ImFluent::GetColorU32( ImFluentCol_ControlFillSecondary ), ImFluent::GetColorU32( ImFluentCol_ControlFillTertiary ), ImFluent::GetColorU32( ImFluentCol_ControlFillDisabled ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), ImFluent::GetColorU32( ImFluentCol_TextDisabled ),
        ImFluent::GetColorU32( ImFluentCol_ControlStrokeDefault ), ImFluent::GetColorU32( ImFluentCol_ElevationControlTop ), ImFluent::GetColorU32( ImFluentCol_ElevationControlBottom ), ImGuiButtonFlags_None );
    ImGui::PopItemFlag();
    return pressed;
}

bool ImFluent::HyperlinkButton( const char * label )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );
    const ImVec2 ts             = ImGui::CalcTextSize( label, NULL, true );
    const float pad_y           = FluentDpx( style.SpacingXSmall );
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + ts.x, pos.y + ts.y + pad_y * 2.f ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id ) )
        return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );
    ReportItemInfo( id, label );
    const ImU32 col    = ImFluent::GetColorU32( held ? ImFluentCol_AccentTextTertiary : hovered ? ImFluentCol_AccentTextSecondary : ImFluentCol_AccentTextPrimary );
    const ImU32 anim   = AnimateColorU32( id, col );

    ImDrawList * dl = w->DrawList;
    // INKEYS PATCH: 显示文本与稳定 ##/### 控件 ID 分离。
    dl->AddText( ImVec2( bb.Min.x, bb.Min.y + pad_y ), anim, label, ImGui::FindRenderedTextEnd( label ) );
    if ( hovered )
        dl->AddLine( ImVec2( bb.Min.x, bb.Max.y - 1.f ), ImVec2( bb.Min.x + ts.x, bb.Max.y - 1.f ), anim, 1.f );
    RenderNavFocusRing( id, bb, FluentDpx( style.ControlCornerRadius ) );
    return pressed;
}

bool ImFluent::ToggleButton( const char * label, bool * v, const ImVec2 & size )
{
    bool pressed;
    if ( v && *v )
    {
        pressed = ButtonExStyled( label, size, ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), ImFluent::GetColorU32( ImFluentCol_AccentFillSecondary ), ImFluent::GetColorU32( ImFluentCol_AccentFillTertiary ), ImFluent::GetColorU32( ImFluentCol_AccentFillDisabled ), ImFluent::GetColorU32( ImFluentCol_TextOnAccentPrimary ), ImFluent::GetColorU32( ImFluentCol_TextOnAccentDisabled ),
            ImFluent::GetColorU32( ImFluentCol_ControlStrokeOnAccentDefault ), ImFluent::GetColorU32( ImFluentCol_ElevationAccentTop ), ImFluent::GetColorU32( ImFluentCol_ElevationAccentBottom ), ImGuiButtonFlags_None );
    }
    else
    {
        pressed = ButtonExStyled( label, size, ImFluent::GetColorU32( ImFluentCol_ControlFillDefault ), ImFluent::GetColorU32( ImFluentCol_ControlFillSecondary ), ImFluent::GetColorU32( ImFluentCol_ControlFillTertiary ), ImFluent::GetColorU32( ImFluentCol_ControlFillDisabled ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), ImFluent::GetColorU32( ImFluentCol_TextDisabled ),
            ImFluent::GetColorU32( ImFluentCol_ControlStrokeDefault ), ImFluent::GetColorU32( ImFluentCol_ElevationControlTop ), ImFluent::GetColorU32( ImFluentCol_ElevationControlBottom ), ImGuiButtonFlags_None );
    }
    if ( pressed && v )
        *v = !*v;
    ReportItemInfo( ImGui::GetCurrentContext()->LastItemData.ID, label, CheckedFlags( v && *v ) );
    return pressed;
}

bool ImFluent::DropDownButtonEx( const char * label, bool * v_state, bool * dropdown_clicked, const ImVec2 & size, bool split, bool toggled )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;

    const ImFluentStyle & style = ImFluent::GetStyle();
    const float pad_x           = FluentDpx( style.ControlContentPadding.x );
    const float pad_y           = FluentDpx( style.ControlContentPadding.y );
    const float min_h           = FluentDpx( style.ControlHeight );
    const float chev_w          = FluentDpx( style.SpinButtonWidth );
    const ImVec2 ts             = ImGui::CalcTextSize( label, NULL, true );
    const float total_w         = ( size.x > 0.f ? size.x : ts.x + pad_x * 2.f + chev_w );
    const float total_h         = ( size.y > 0.f ? size.y : ImMax( min_h, ts.y + pad_y * 2.f ) );

    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb_total( pos, ImVec2( pos.x + total_w, pos.y + total_h ) );
    const ImRect bb_main( pos, ImVec2( pos.x + total_w - ( split ? chev_w : 0.f ), pos.y + total_h ) );
    const ImRect bb_chev( ImVec2( bb_main.Max.x, pos.y ), bb_total.Max );

    ImGui::ItemSize( bb_total );
    const ImGuiID id_main = w->GetID( label );
    const ImGuiID id_chev = w->GetID( ( const void * )( ( intptr_t )id_main + 1 ) );
    if ( !ImGui::ItemAdd( bb_total, id_main ) )
        return false;

    bool main_hovered = false, main_held = false;
    const bool main_pressed = ImGui::ButtonBehavior( bb_main, id_main, &main_hovered, &main_held );

    bool chev_hovered = false, chev_held = false, chev_pressed = false;
    if ( split )
        chev_pressed = ImGui::ButtonBehavior( bb_chev, id_chev, &chev_hovered, &chev_held );

    const bool disabled = ImFluent::IsCurrentItemDisabled();
    ReportItemInfo( id_main, label, ( split && v_state ) ? CheckedFlags( toggled ) : 0 );

    ImU32 fillRest, fillHover, fillPress, fillDisabled, textCol, textDisabled, strokeCol, strokeTop, strokeBottom;
    if ( toggled )
    {
        fillRest     = ImFluent::GetColorU32( ImFluentCol_AccentFillDefault );
        fillHover    = ImFluent::GetColorU32( ImFluentCol_AccentFillSecondary );
        fillPress    = ImFluent::GetColorU32( ImFluentCol_AccentFillTertiary );
        fillDisabled = ImFluent::GetColorU32( ImFluentCol_AccentFillDisabled );
        textCol      = ImFluent::GetColorU32( ImFluentCol_TextOnAccentPrimary );
        textDisabled = ImFluent::GetColorU32( ImFluentCol_TextOnAccentDisabled );
        strokeCol    = ImFluent::GetColorU32( ImFluentCol_ControlStrokeOnAccentDefault );
        strokeTop    = ImFluent::GetColorU32( ImFluentCol_ElevationAccentTop );
        strokeBottom = ImFluent::GetColorU32( ImFluentCol_ElevationAccentBottom );
    }
    else
    {
        fillRest     = ImFluent::GetColorU32( ImFluentCol_ControlFillDefault );
        fillHover    = ImFluent::GetColorU32( ImFluentCol_ControlFillSecondary );
        fillPress    = ImFluent::GetColorU32( ImFluentCol_ControlFillTertiary );
        fillDisabled = ImFluent::GetColorU32( ImFluentCol_ControlFillDisabled );
        textCol      = ImFluent::GetColorU32( ImFluentCol_TextPrimary );
        textDisabled = ImFluent::GetColorU32( ImFluentCol_TextDisabled );
        strokeCol    = ImFluent::GetColorU32( ImFluentCol_ControlStrokeDefault );
        strokeTop    = ImFluent::GetColorU32( ImFluentCol_ElevationControlTop );
        strokeBottom = ImFluent::GetColorU32( ImFluentCol_ElevationControlBottom );
    }

    auto pickFill = [ & ]( bool hov, bool hel ) -> ImU32
    {
        if ( disabled )
            return fillDisabled;
        if ( hel )
            return fillPress;
        if ( hov )
            return fillHover;
        return fillRest;
    };
    const ImU32 mainTarget = pickFill( main_hovered, main_held );
    const ImU32 mainAnim   = AnimateColorU32( id_main, mainTarget );
    const float r          = FluentDpx( style.ControlCornerRadius );

    ImDrawList * dl = w->DrawList;
    if ( split )
    {

        const float sw = StrokeWidth();
        dl->AddRectFilled( ImVec2( bb_main.Min.x + sw, bb_main.Min.y + sw ), ImVec2( bb_main.Max.x, bb_main.Max.y - sw ), mainAnim, r - sw, ImDrawFlags_RoundCornersLeft );
        const ImU32 chevTarget = pickFill( chev_hovered, chev_held );
        const ImU32 chevAnim   = AnimateColorU32( id_chev, chevTarget );
        dl->AddRectFilled( ImVec2( bb_chev.Min.x, bb_chev.Min.y + sw ), ImVec2( bb_chev.Max.x - sw, bb_chev.Max.y - sw ), chevAnim, r - sw, ImDrawFlags_RoundCornersRight );
        dl->AddLine( ImVec2( bb_main.Max.x, bb_main.Min.y + FluentDpx( style.SpacingMedium ) ), ImVec2( bb_main.Max.x, bb_main.Max.y - FluentDpx( style.SpacingMedium ) ), ImFluent::GetColorU32( ImFluentCol_DividerStrokeDefault ), 1.f );
        if ( !disabled )
            RenderElevationBorder( dl, bb_total, r, strokeCol, strokeTop, strokeBottom, 1.f );
        else
            ImFluent::StrokeRect( dl, bb_total.Min, bb_total.Max, strokeCol, r );
    }
    else
    {
        FillInsideBorder( dl, bb_total.Min, bb_total.Max, mainAnim, r );
        if ( !main_held && !disabled )
            RenderElevationBorder( dl, bb_total, r, strokeCol, strokeTop, strokeBottom, 1.f );
        else
            ImFluent::StrokeRect( dl, bb_total.Min, bb_total.Max, strokeCol, r );
    }

    RenderButtonLabel( dl, ImRect( bb_main.Min, ImVec2( bb_main.Max.x - ( split ? 0.f : chev_w ), bb_main.Max.y ) ), label, disabled ? textDisabled : textCol, pad_x, pad_y );

    {
        const ImVec2 cen( ( split ? bb_chev.Min.x + bb_chev.GetWidth() * 0.5f : bb_main.Max.x - chev_w * 0.5f ), ( bb_total.Min.y + bb_total.Max.y ) * 0.5f );
        RenderChevron( dl, cen, ImGuiDir_Down, disabled ? textDisabled : textCol, FluentDpx( style.ChevronGlyphSize ) );
    }

    RenderNavFocusRing( id_main, bb_total, r );

    // SplitButton keyboard (WinUI): one tab stop; Space/Enter run the primary action, Alt+Down and F4 open the flyout.
    if ( split && !disabled && ( IsFocusedItemChordPressed( id_main, g_Ctx.Actions.TogglePopup, false ) || IsFocusedItemChordPressed( id_main, g_Ctx.Actions.OpenPopup, false ) ) )
        chev_pressed = true;
    if ( dropdown_clicked )
        *dropdown_clicked = split ? chev_pressed : false;
    if ( v_state && main_pressed && !split )
        *v_state = !*v_state;
    return main_pressed;
}

bool ImFluent::DropDownButton( const char * label, const ImVec2 & size )
{
    bool dummy;
    return DropDownButtonEx( label, NULL, &dummy, size, false, false );
}
bool ImFluent::SplitButton( const char * label, bool * dropdown_clicked, const ImVec2 & size )
{
    return DropDownButtonEx( label, NULL, dropdown_clicked, size, true, false );
}
bool ImFluent::ToggleSplitButton( const char * label, bool * v, bool * dropdown_clicked, const ImVec2 & size )
{
    bool main = DropDownButtonEx( label, v, dropdown_clicked, size, true, ( v && *v ) );
    if ( main && v )
        *v = !*v;
    return main;
}

// [SECTION] Selection (Checkbox / RadioButton / ToggleSwitch / RatingControl)

bool ImFluent::CheckboxEx( const char * label, int * v_tri, bool * v_bool )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;

    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );
    const float box             = FluentDpx( style.CheckboxSize );
    const float gap             = FluentDpx( style.SpacingMedium );
    const ImVec2 ts             = ImGui::CalcTextSize( label, NULL, true );
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect box_bb( pos, ImVec2( pos.x + box, pos.y + box ) );
    const ImRect total_bb( pos, ImVec2( pos.x + box + ( ts.x > 0 ? gap + ts.x : 0.f ), pos.y + ImMax( box, ts.y ) ) );
    ImGui::ItemSize( total_bb );
    if ( !ImGui::ItemAdd( total_bb, id ) )
        return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior( total_bb, id, &hovered, &held ) && !IsEnterActivation( id );
    if ( pressed )
    {
        if ( v_tri )
        {
            int & s = *v_tri;
            s       = ( s > 0 ) ? 0 : 1;
        }
        if ( v_bool )
        {
            *v_bool = !*v_bool;
        }
    }

    int state = v_tri ? *v_tri : ( v_bool && *v_bool ? 1 : 0 );
    state     = state < 0 ? -1 : ( state > 0 ? 1 : 0 );
    if ( v_tri )
        *v_tri = state;
    const bool checked  = state > 0;
    const bool indet    = state < 0;
    const bool disabled = ImFluent::IsCurrentItemDisabled();
    ReportItemInfo( id, label, CheckedFlags( checked ) );

    ImDrawList * dl = w->DrawList;
    const float r   = FluentDpx( style.ControlCornerRadius );

    ImU32 boxFill, boxStroke;
    if ( disabled )
    {
        boxFill   = ImFluent::GetColorU32( ImFluentCol_ControlFillDisabled );
        boxStroke = ImFluent::GetColorU32( ImFluentCol_ControlStrongStrokeDisabled );
    }
    else if ( checked || indet )
    {
        boxFill   = ResolveAccentFillState( disabled, held, hovered );
        boxStroke = boxFill;
    }
    else
    {
        boxFill   = ImFluent::GetColorU32( held ? ImFluentCol_ControlAltFillQuarternary : hovered ? ImFluentCol_ControlAltFillTertiary : ImFluentCol_ControlAltFillSecondary );
        boxStroke = ImFluent::GetColorU32( ImFluentCol_ControlStrongStrokeDefault );
    }
    const ImU32 fillAnim   = AnimateColorU32( id, boxFill );
    const ImU32 strokeAnim = AnimateColorU32( id, boxStroke, 0.083f, 1 );
    ImGui::PushStyleColor( ImGuiCol_Border, strokeAnim );
    ImGui::RenderFrame( box_bb.Min, box_bb.Max, fillAnim, true, r );
    ImGui::PopStyleColor();

    if ( checked )
    {
        const ImU32 mark = ImFluent::GetColorU32( disabled ? ImFluentCol_TextOnAccentDisabled : ImFluentCol_TextOnAccentPrimary );
        const ImVec2 a( box_bb.Min.x + box * 0.22f, box_bb.Min.y + box * 0.52f );
        const ImVec2 b( box_bb.Min.x + box * 0.42f, box_bb.Min.y + box * 0.72f );
        const ImVec2 c( box_bb.Min.x + box * 0.78f, box_bb.Min.y + box * 0.32f );
        dl->AddLine( a, b, mark, FluentDpx( style.StrokeMedium ) );
        dl->AddLine( b, c, mark, FluentDpx( style.StrokeMedium ) );
    }
    else if ( indet )
    {
        const ImU32 mark = ImFluent::GetColorU32( disabled ? ImFluentCol_TextOnAccentDisabled : ImFluentCol_TextOnAccentPrimary );
        dl->AddRectFilled( ImVec2( box_bb.Min.x + box * 0.25f, box_bb.Min.y + box * 0.45f ), ImVec2( box_bb.Max.x - box * 0.25f, box_bb.Max.y - box * 0.45f ), mark, FluentDpx( style.StrokeThin ) );
    }

    if ( ts.x > 0 )
    {
        const ImU32 lc = ImFluent::GetColorU32( disabled ? ImFluentCol_TextDisabled : ImFluentCol_TextPrimary );
        ImGui::PushStyleColor( ImGuiCol_Text, lc );
        ImGui::RenderTextClipped( ImVec2( box_bb.Max.x + gap, pos.y ), ImVec2( box_bb.Max.x + gap + ts.x, pos.y + box ), label, NULL, &ts, ImVec2( 0.f, 0.5f ) );
        ImGui::PopStyleColor();
    }
    RenderNavFocusRing( id, box_bb, r );
    return pressed;
}

bool ImFluent::Checkbox( const char * label, bool * v )
{
    return CheckboxEx( label, NULL, v );
}
bool ImFluent::CheckboxTristate( const char * label, int * v )
{
    return CheckboxEx( label, v, NULL );
}

bool ImFluent::RadioButton( const char * label, bool active )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );
    const float D               = FluentDpx( style.CheckboxSize );
    const float gap             = FluentDpx( style.SpacingMedium );
    const ImVec2 ts             = ImGui::CalcTextSize( label, NULL, true );
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect ring_bb( pos, ImVec2( pos.x + D, pos.y + D ) );
    const ImRect total_bb( pos, ImVec2( pos.x + D + ( ts.x > 0 ? gap + ts.x : 0.f ), pos.y + ImMax( D, ts.y ) ) );
    ImGui::ItemSize( total_bb );
    if ( !ImGui::ItemAdd( total_bb, id ) )
        return false;
    bool hovered = false, held = false;
    const bool pressed  = ImGui::ButtonBehavior( total_bb, id, &hovered, &held ) && !IsEnterActivation( id );
    const bool disabled = ImFluent::IsCurrentItemDisabled();
    ReportItemInfo( id, label, CheckedFlags( active ) );

    ImDrawList * dl = w->DrawList;
    const ImVec2 c( ( ring_bb.Min.x + ring_bb.Max.x ) * 0.5f, ( ring_bb.Min.y + ring_bb.Max.y ) * 0.5f );
    const float ring_r    = D * 0.5f;
    const ImU32 strokeCol = ImFluent::GetColorU32( disabled ? ImFluentCol_ControlStrongStrokeDisabled : active ? ImFluentCol_AccentFillDefault : ImFluentCol_ControlStrongStrokeDefault );
    const ImU32 ringFill  = ImFluent::GetColorU32( active ? held ? ImFluentCol_AccentFillTertiary : hovered ? ImFluentCol_AccentFillSecondary : ImFluentCol_AccentFillDefault : held ? ImFluentCol_ControlAltFillTertiary : hovered ? ImFluentCol_ControlAltFillSecondary : ImFluentCol_ControlAltFillTransparent );
    dl->AddCircleFilled( c, ring_r, AnimateColorU32( id, ringFill ), 32 );
    dl->AddCircle( c, ring_r, AnimateColorU32( id, strokeCol, 0.083f, 1 ), 32, 1.f );
    if ( active )
    {
        const float dot_r = AnimateFloat( id, ( held ? D * 0.20f : ( hovered ? D * 0.30f : D * 0.25f ) ), 0.083f, 2 );
        dl->AddCircleFilled( c, dot_r, ImFluent::GetColorU32( ImFluentCol_TextOnAccentPrimary ), 24 );
    }
    if ( ts.x > 0 )
    {
        const ImU32 lc = ImFluent::GetColorU32( disabled ? ImFluentCol_TextDisabled : ImFluentCol_TextPrimary );
        ImGui::PushStyleColor( ImGuiCol_Text, lc );
        ImGui::RenderTextClipped( ImVec2( ring_bb.Max.x + gap, pos.y ), ImVec2( ring_bb.Max.x + gap + ts.x, pos.y + D ), label, NULL, &ts, ImVec2( 0.f, 0.5f ) );
        ImGui::PopStyleColor();
    }
    RenderNavFocusRing( id, ring_bb, ring_r + FluentDpx( style.StrokeThin ) );
    return pressed;
}

// Loose radio buttons sharing one int* are a group (WinUI GroupName): arrows move focus only, wrapping; links kept from last frame.
static void LooseRadioGroupNav( const int * group, ImGuiID id )
{
    static const ImGuiID KEY_FRAME = 0x4AD10001u, KEY_FIRST = 0x4AD10002u, KEY_LAST = 0x4AD10003u, KEY_PREV_LAST = 0x4AD10004u;
    static const ImGuiID KEY_NEXT = 0x4AD10005u, KEY_PREV = 0x4AD10006u;
    ImGuiContext & g   = *GImGui;
    ImGuiStorage * st  = ImGui::GetStateStorage();
    const ImGuiID gkey = ImHashData( &group, sizeof( group ) );
    if ( st->GetInt( gkey ^ KEY_FRAME, -1 ) != g.FrameCount )
    {
        st->SetInt( gkey ^ KEY_FRAME, g.FrameCount );
        st->SetInt( gkey ^ KEY_PREV_LAST, st->GetInt( gkey ^ KEY_LAST, 0 ) );
        st->SetInt( gkey ^ KEY_FIRST, ( int )id );
        st->SetInt( id ^ KEY_PREV, 0 );
    }
    else
    {
        const ImGuiID prev = ( ImGuiID )st->GetInt( gkey ^ KEY_LAST, 0 );
        st->SetInt( prev ^ KEY_NEXT, ( int )id );
        st->SetInt( id ^ KEY_PREV, ( int )prev );
    }
    st->SetInt( gkey ^ KEY_LAST, ( int )id );
    if ( g.NavId != id )
        return;
    const ImGuiID first     = ( ImGuiID )st->GetInt( gkey ^ KEY_FIRST, 0 );
    const ImGuiID prev_last = ( ImGuiID )st->GetInt( gkey ^ KEY_PREV_LAST, 0 );
    const bool down         = ImFluent::ConsumeNavArrow( id, ImGuiDir_Down ); // Consume both keys of each pair
    const bool right        = ImFluent::ConsumeNavArrow( id, ImGuiDir_Right );
    const bool up           = ImFluent::ConsumeNavArrow( id, ImGuiDir_Up );
    const bool left         = ImFluent::ConsumeNavArrow( id, ImGuiDir_Left );
    const bool fwd          = down || right;
    const bool back         = up || left;
    ImGuiID target          = 0;
    if ( fwd )
        target = ( id == prev_last ) ? first : ( ImGuiID )st->GetInt( id ^ KEY_NEXT, 0 );
    else if ( back )
        target = ( id == first ) ? prev_last : ( ImGuiID )st->GetInt( id ^ KEY_PREV, 0 );
    if ( target && target != id )
    {
        ImGui::SetFocusID( target, g.CurrentWindow );
        g.NavCursorVisible = true;
    }
}

bool ImFluent::RadioButton( const char * label, int * v, int v_button )
{
    const bool active  = v && ( *v == v_button );
    const bool pressed = RadioButton( label, active );
    if ( v && !g_Ctx.InRadioButtons && GImGui->LastItemData.ID == ImGui::GetID( label ) )
        LooseRadioGroupNav( v, GImGui->LastItemData.ID );
    if ( pressed )
    {
        if ( v )
            *v = v_button;
        return true;
    }
    return false;
}

bool ImFluent::RadioButtons( const char * label, int * v, const char * const items[], int items_count, int max_columns )
{
    if ( !items || items_count <= 0 )
        return false;
    if ( max_columns < 1 )
        max_columns = 1;
    bool changed = false;
    ImGui::PushID( label );
    if ( label && *label )
    {
        RenderAndConsumePendingHeader();
        ImGui::TextUnformatted( label );
    }
    // WinUI RadioButtons: one tab stop; Up/Down by index (column-major), Left/Right by column, stop at the ends; select unless Ctrl.
    ImGuiContext & g = *GImGui;
    const int rows   = ( items_count + max_columns - 1 ) / max_columns;
    int nav_target   = -1;
    BeginTabGroup( ImGui::GetID( "##radio-group" ), ImFluentTabGroupFlags_None );
    for ( int row = 0; row < rows; ++row )
    {
        for ( int col = 0; col < max_columns; ++col )
        {
            const int i = col * rows + row;
            if ( i >= items_count )
                continue;
            if ( col > 0 )
                ImGui::SameLine();
            const ImGuiID item_id = ImGui::GetID( items[ i ] );
            ImGui::PushItemFlag( ImGuiItemFlags_NoTabStop, TabGroupItemFlags( item_id, v && *v == i ) != ImGuiItemFlags_None );
            g_Ctx.InRadioButtons = true;
            if ( RadioButton( items[ i ], v, i ) )
                changed = true;
            g_Ctx.InRadioButtons = false;
            ImGui::PopItemFlag();
            if ( g.NavId == item_id )
            {
                if ( ConsumeNavArrow( item_id, ImGuiDir_Down ) && i + 1 < items_count )
                    nav_target = i + 1;
                if ( ConsumeNavArrow( item_id, ImGuiDir_Up ) && i > 0 )
                    nav_target = i - 1;
                if ( ConsumeNavArrow( item_id, ImGuiDir_Right ) && i + rows < items_count )
                    nav_target = i + rows;
                if ( ConsumeNavArrow( item_id, ImGuiDir_Left ) && i - rows >= 0 )
                    nav_target = i - rows;
            }
        }
    }
    if ( nav_target >= 0 )
    {
        ImGui::SetFocusID( ImGui::GetID( items[ nav_target ] ), ImGui::GetCurrentWindow() );
        g.NavCursorVisible = true;
        if ( v && !g.IO.KeyCtrl && *v != nav_target )
        {
            *v      = nav_target;
            changed = true;
        }
    }
    EndTabGroup();
    RenderAndConsumePendingDescription();
    ImGui::PopID();
    return changed;
}

bool ImFluent::ToggleSwitch( const char * label, bool * v, const char * on_text, const char * off_text )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );
    const float W               = FluentDpx( style.ToggleSwitchWidth );
    const float H               = FluentDpx( style.ToggleSwitchHeight );
    const float gap             = FluentDpx( style.SpacingLarge );
    const ImVec2 lab_size       = ImGui::CalcTextSize( label, NULL, true );
    const char * state_text     = ( v && *v ) ? on_text : off_text;
    const ImVec2 state_size     = state_text ? ImGui::CalcTextSize( state_text, NULL, true ) : ImVec2( 0, 0 );

    const ImVec2 pos = w->DC.CursorPos;
    const ImRect track_bb( pos, ImVec2( pos.x + W, pos.y + H ) );
    const ImRect state_bb( ImVec2( track_bb.Max.x + gap, pos.y ), ImVec2( track_bb.Max.x + gap + state_size.x, pos.y + H ) );
    const float right_x = ( state_size.x > 0 ? state_bb.Max.x + ( lab_size.x > 0 ? gap : 0 ) : track_bb.Max.x + ( lab_size.x > 0 ? gap : 0 ) );
    const ImRect total_bb( pos, ImVec2( right_x + lab_size.x, pos.y + ImMax( H, ImMax( lab_size.y, state_size.y ) ) ) );
    ImGui::ItemSize( total_bb );
    if ( !ImGui::ItemAdd( total_bb, id ) )
        return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior( track_bb, id, &hovered, &held ) && !IsEnterActivation( id );
    if ( pressed && v )
        *v = !*v;
    const bool on       = v && *v;
    const bool disabled = ImFluent::IsCurrentItemDisabled();
    ReportItemInfo( id, label, CheckedFlags( on ) );

    ImU32 trackTarget;
    if ( disabled )
        trackTarget = ImFluent::GetColorU32( on ? ImFluentCol_AccentFillDisabled : ImFluentCol_ControlAltFillDisabled );
    else if ( on )
        trackTarget = ResolveAccentFillState( disabled, held, hovered );
    else
        trackTarget = ImFluent::GetColorU32( held ? ImFluentCol_ControlAltFillQuarternary : hovered ? ImFluentCol_ControlAltFillTertiary : ImFluentCol_ControlAltFillSecondary );
    const ImU32 trackAnim = AnimateColorU32( id, trackTarget );

    ImDrawList * dl = w->DrawList;
    dl->AddRectFilled( track_bb.Min, track_bb.Max, trackAnim, H * 0.5f );
    if ( !on )
        ImFluent::StrokeRect( dl, track_bb.Min, track_bb.Max, ImFluent::GetColorU32( ImFluentCol_ControlStrongStrokeDefault ), H * 0.5f );

    const float pad            = FluentDpx( style.SpacingSmall );
    const float thumb_r_target = on ? FluentDpx( style.ToggleSwitchThumbRadiusOn ) : FluentDpx( style.ToggleSwitchThumbRadiusOff );
    const float thumb_r        = AnimateFloat( id, thumb_r_target, 0.083f, 1 );
    const float cx_off         = track_bb.Min.x + pad + FluentDpx( style.ToggleSwitchThumbRadiusOff );
    const float cx_on          = track_bb.Max.x - pad - FluentDpx( style.ToggleSwitchThumbRadiusOff );
    const float t_anim         = AnimateFloat( id, on ? 1.f : 0.f, 0.083f, 2 );
    const float cx             = cx_off + ( cx_on - cx_off ) * t_anim;
    const float cy             = ( track_bb.Min.y + track_bb.Max.y ) * 0.5f;
    const ImU32 thumbColTarget = ImFluent::GetColorU32( disabled ? ImFluentCol_ControlStrongFillDisabled : on ? ImFluentCol_TextOnAccentPrimary : ImFluentCol_ControlStrongFillDefault );
    const ImU32 thumbCol       = AnimateColorU32( id, thumbColTarget, 0.083f, 3 );
    dl->AddCircleFilled( ImVec2( cx, cy ), thumb_r, thumbCol, 24 );

    float cursor_x = track_bb.Max.x + gap;
    if ( state_size.x > 0 )
    {
        dl->AddText( ImVec2( cursor_x, pos.y + ( H - state_size.y ) * 0.5f ), ImFluent::GetColorU32( disabled ? ImFluentCol_TextDisabled : ImFluentCol_TextSecondary ), state_text );
        cursor_x += state_size.x + gap;
    }
    if ( lab_size.x > 0 )
        dl->AddText( ImVec2( cursor_x, pos.y + ( H - lab_size.y ) * 0.5f ), ImFluent::GetColorU32( disabled ? ImFluentCol_TextDisabled : ImFluentCol_TextPrimary ), label );
    RenderNavFocusRing( id, track_bb, H * 0.5f );
    return pressed;
}

bool ImFluent::RatingControl( const char * label, float * value, int max_stars )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );
    const float star            = FluentDpx( style.RatingStarSize );
    const float gap             = FluentDpx( style.SpacingSmall );
    const ImVec2 pos            = w->DC.CursorPos;
    const float total_w         = max_stars * star + ( max_stars - 1 ) * gap;
    const ImRect bb( pos, ImVec2( pos.x + total_w, pos.y + star ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id ) )
        return false;

    const bool hovered = ImGui::ItemHoverable( bb, id, ImGuiItemFlags_None );
    bool changed       = false;
    ReportItemInfo( id, label );

    // Keyboard: arrows step by one star, Home clears, End fills (WinUI RatingControl).
    if ( value && !IsCurrentItemDisabled() )
    {
        float new_val = *value;
        if ( const int step = ConsumeNavIncrement( id ) )
            new_val = ImClamp( ImFloor( new_val + ( step > 0 ? 1.f : -0.5f ) ), 0.f, ( float )max_stars );
        if ( IsFocusedItemChordPressed( id, g_Ctx.Actions.FirstItem ) )
            new_val = 0.f;
        if ( IsFocusedItemChordPressed( id, g_Ctx.Actions.LastItem ) )
            new_val = ( float )max_stars;
        if ( new_val != *value )
        {
            *value  = new_val;
            changed = true;
            ImGui::MarkItemEdited( id );
        }
    }
    if ( hovered && ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
    {
        const float mx = ImGui::GetMousePos().x - bb.Min.x;
        float new_val  = ( mx / ( star + gap ) );
        if ( new_val < 0 )
            new_val = 0;
        if ( new_val > ( float )max_stars )
            new_val = ( float )max_stars;

        const float snapped = ( float )( ( int )( new_val * 2.f + 0.5f ) ) * 0.5f;
        if ( value && *value != snapped )
        {
            *value  = snapped;
            changed = true;
        }
    }
    const float v        = value ? *value : 0.f;
    ImDrawList * dl      = w->DrawList;
    const ImU32 trackCol = ImFluent::GetColorU32( ImFluentCol_TextSecondary );
    const ImU32 fillCol  = ImFluent::GetColorU32( ImFluentCol_AccentFillDefault );
    // WinUI RatingControl: Segoe star glyphs, an outline for unset items, a filled star (cut for half values) for the value.
    ImFont * font        = ImGui::GetFont();
    for ( int i = 0; i < max_stars; ++i )
    {
        const ImVec2 p( pos.x + i * ( star + gap ), pos.y );
        const float fill = ImClamp( v - ( float )i, 0.f, 1.f );
        if ( fill < 1.f )
            dl->AddText( font, star, p, trackCol, ImFluentIcon_FavoriteStar );
        if ( fill > 0.f )
        {
            dl->PushClipRect( p, ImVec2( p.x + star * fill, p.y + star ), true );
            dl->AddText( font, star, p, fillCol, ImFluentIcon_FavoriteStarFill );
            dl->PopClipRect();
        }
    }
    RenderNavFocusRing( id, bb, FluentDpx( style.ControlCornerRadius ) );
    return changed;
}

// [SECTION] Sliders & Progress

static double SliderValueToDouble( ImGuiDataType dtype, const void * p )
{
    switch ( dtype )
    {
        case ImGuiDataType_S8:
            return *( const ImS8 * )p;
        case ImGuiDataType_U8:
            return *( const ImU8 * )p;
        case ImGuiDataType_S16:
            return *( const ImS16 * )p;
        case ImGuiDataType_U16:
            return *( const ImU16 * )p;
        case ImGuiDataType_S32:
            return *( const ImS32 * )p;
        case ImGuiDataType_U32:
            return *( const ImU32 * )p;
        case ImGuiDataType_S64:
            return ( double )*( const ImS64 * )p;
        case ImGuiDataType_U64:
            return ( double )*( const ImU64 * )p;
        case ImGuiDataType_Float:
            return *( const float * )p;
        case ImGuiDataType_Double:
            return *( const double * )p;
        default:
            return 0.0;
    }
}

static void SliderValueFromDouble( ImGuiDataType dtype, void * p, double d )
{
    const bool is_float = ( dtype == ImGuiDataType_Float || dtype == ImGuiDataType_Double );
    if ( !is_float )
        d = ( d < 0.0 ) ? ( double )( ImS64 )( d - 0.5 ) : ( double )( ImS64 )( d + 0.5 );
    switch ( dtype )
    {
        case ImGuiDataType_S8:
            *( ImS8 * )p = ( ImS8 )d;
            break;
        case ImGuiDataType_U8:
            *( ImU8 * )p = ( ImU8 )d;
            break;
        case ImGuiDataType_S16:
            *( ImS16 * )p = ( ImS16 )d;
            break;
        case ImGuiDataType_U16:
            *( ImU16 * )p = ( ImU16 )d;
            break;
        case ImGuiDataType_S32:
            *( ImS32 * )p = ( ImS32 )d;
            break;
        case ImGuiDataType_U32:
            *( ImU32 * )p = ( ImU32 )d;
            break;
        case ImGuiDataType_S64:
            *( ImS64 * )p = ( ImS64 )d;
            break;
        case ImGuiDataType_U64:
            *( ImU64 * )p = ( ImU64 )d;
            break;
        case ImGuiDataType_Float:
            *( float * )p = ( float )d;
            break;
        case ImGuiDataType_Double:
            *( double * )p = d;
            break;
        default:
            break;
    }
}

// WinUI Slider keys: arrows 1 (integers) or 1% of the range, PageUp/PageDown 10%, Home/End the ends. Steps come from
// [step_min, step_max] when given (a range slider thumb is clamped by the other thumb but steps over the full range).
static bool SliderKeyboardStep( ImGuiID id, ImGuiDataType dtype, void * v, const void * v_min, const void * v_max, const void * step_min = NULL, const void * step_max = NULL )
{
    const double lo    = SliderValueToDouble( dtype, v_min );
    const double hi    = SliderValueToDouble( dtype, v_max );
    const double range = ImAbs( SliderValueToDouble( dtype, step_max ? step_max : v_max ) - SliderValueToDouble( dtype, step_min ? step_min : v_min ) );
    const bool is_int  = !( dtype == ImGuiDataType_Float || dtype == ImGuiDataType_Double );
    const double small = is_int ? 1.0 : range / 100.0;
    const double large = is_int ? ImMax( 1.0, floor( range / 10.0 ) ) : range / 10.0;
    const double cur   = SliderValueToDouble( dtype, v );
    double next        = cur;
    if ( const int step = ImFluent::ConsumeNavIncrement( id ) )
        next += step * small;
    if ( ImFluent::IsFocusedItemChordPressed( id, g_Ctx.Actions.PageUp ) )
        next += large;
    if ( ImFluent::IsFocusedItemChordPressed( id, g_Ctx.Actions.PageDown ) )
        next -= large;
    if ( ImFluent::IsFocusedItemChordPressed( id, g_Ctx.Actions.FirstItem ) )
        next = ImMin( lo, hi );
    if ( ImFluent::IsFocusedItemChordPressed( id, g_Ctx.Actions.LastItem ) )
        next = ImMax( lo, hi );
    next = ImClamp( next, ImMin( lo, hi ), ImMax( lo, hi ) );
    if ( next == cur )
        return false;
    SliderValueFromDouble( dtype, v, next );
    return true;
}

bool ImFluent::Slider( const char * label, ImGuiDataType dtype, void * v, const void * v_min, const void * v_max, const char * format, ImGuiSliderFlags flags )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    RenderAndConsumePendingHeader();
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );
    const float h               = FluentDpx( style.ControlHeight );
    const float w_avail         = ImGui::CalcItemWidth();
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect frame_bb( pos, ImVec2( pos.x + w_avail, pos.y + h ) );

    ImGui::ItemSize( frame_bb );
    if ( !ImGui::ItemAdd( frame_bb, id ) )
    {
        ClearPendingNextItem();
        return false;
    }

    ImGuiContext & g   = *ImGui::GetCurrentContext();
    const bool hovered = ImGui::ItemHoverable( frame_bb, id, g.LastItemData.ItemFlags );
    const bool clicked = hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left );
    if ( clicked || g.NavActivateId == id )
    {
        ImGui::SetActiveID( id, w );
        ImGui::SetFocusID( id, w );
        ImGui::FocusWindow( w );
        g.ActiveIdUsingNavDirMask |= ( 1u << ImGuiDir_Left ) | ( 1u << ImGuiDir_Right );
    }

    ImRect grab_bb;
    bool changed = ImGui::SliderBehavior( frame_bb, id, dtype, v, v_min, v_max, format, flags, &grab_bb );
    if ( g.ActiveId != id && !IsCurrentItemDisabled() && SliderKeyboardStep( id, dtype, v, v_min, v_max ) )
    {
        changed = true;
        ImGui::SliderBehavior( frame_bb, id, dtype, v, v_min, v_max, format, flags | ImGuiSliderFlags_ReadOnly, &grab_bb ); // Grab rect for the new value
    }
    if ( changed )
        ImGui::MarkItemEdited( id );
    ReportItemInfo( id, label );

    const bool active = ImGui::IsItemActive();

    ImDrawList * dl     = w->DrawList;
    const float track_h = FluentDpx( style.SliderTrackHeight );
    const ImVec2 track_min( frame_bb.Min.x, ( frame_bb.Min.y + frame_bb.Max.y ) * 0.5f - track_h * 0.5f );
    const ImVec2 track_max( frame_bb.Max.x, track_min.y + track_h );
    dl->AddRectFilled( track_min, track_max, ImFluent::GetColorU32( ImFluentCol_ControlStrongFillDefault ), track_h * 0.5f );
    const float thumb_cx = ( grab_bb.Min.x + grab_bb.Max.x ) * 0.5f;
    if ( thumb_cx > track_min.x )
        dl->AddRectFilled( track_min, ImVec2( thumb_cx, track_max.y ), ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), track_h * 0.5f );

    const float thumb_r      = FluentDpx( style.SliderThumbRadius );
    const float inner_rest   = FluentDpx( style.SliderThumbInnerRadius );
    const float inner_delta  = FluentDpx( 1.f );
    const float inner_target = active ? inner_rest - inner_delta : hovered ? inner_rest + inner_delta : inner_rest;
    const float inner_r      = AnimateFloat( id, inner_target, 0.083f, 1 );
    const ImVec2 c( thumb_cx, ( track_min.y + track_max.y ) * 0.5f );
    dl->AddCircleFilled( c, thumb_r, ImFluent::GetColorU32( ImFluentCol_ControlSolidFillDefault ), 32 );
    dl->AddCircleFilled( c, inner_r, ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), 24 );

    RenderNavFocusRing( id, frame_bb, FluentDpx( style.ControlCornerRadius ) );
    RenderAndConsumePendingDescription();
    return changed;
}

bool ImFluent::Slider( const char * label, float * v, float v_min, float v_max, const char * format, ImGuiSliderFlags flags )
{
    return Slider( label, ImGuiDataType_Float, v, &v_min, &v_max, format, flags );
}
bool ImFluent::SliderInt( const char * label, int * v, int v_min, int v_max, const char * format, ImGuiSliderFlags flags )
{
    return Slider( label, ImGuiDataType_S32, v, &v_min, &v_max, format, flags );
}

bool ImFluent::RangeSlider( const char * label, float * v_min, float * v_max, float v_lo, float v_hi, const char * format )
{
    if ( !v_min || !v_max || v_hi <= v_lo )
        return false;
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
    {
        ClearPendingNextItem();
        return false;
    }
    RenderAndConsumePendingHeader();

    *v_min = ImClamp( *v_min, v_lo, v_hi );
    *v_max = ImClamp( *v_max, v_lo, v_hi );
    *v_min = ImMin( *v_min, *v_max );
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id_lo         = w->GetID( label );
    const ImGuiID id_hi         = w->GetID( ( const void * )( ( intptr_t )id_lo + 1 ) );
    const float h               = FluentDpx( style.ControlHeight );
    const float w_avail         = ImGui::CalcItemWidth();
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect frame_bb( pos, ImVec2( pos.x + w_avail, pos.y + h ) );
    ImGui::ItemSize( frame_bb );
    ImGui::SetNextItemAllowOverlap();
    if ( !ImGui::ItemAdd( frame_bb, id_lo ) )
    {
        ClearPendingNextItem();
        return false;
    }
    ImGui::SetNextItemAllowOverlap();
    ImGui::ItemAdd( frame_bb, id_hi );
    {
        const char * hi_label;
        const char * hi_label_end;
        ImFormatStringToTempBuffer( &hi_label, &hi_label_end, "%s (upper)", label );
        ReportItemInfo( id_lo, label );
        ReportItemInfo( id_hi, hi_label );
    }

    const float thumb_r     = FluentDpx( style.SliderThumbRadius );
    const float track_h     = FluentDpx( style.SliderTrackHeight );
    const float track_left  = frame_bb.Min.x + thumb_r;
    const float track_right = frame_bb.Max.x - thumb_r;
    const float track_w     = ImMax( 1.f, track_right - track_left );
    const float track_y     = ( frame_bb.Min.y + frame_bb.Max.y ) * 0.5f;
    const float range       = v_hi - v_lo;

    auto value_to_x = [ & ]( float val )
    {
        return track_left + ( ( val - v_lo ) / range ) * track_w;
    };
    auto x_to_value = [ & ]( float x )
    {
        return v_lo + ImClamp( ( x - track_left ) / track_w, 0.f, 1.f ) * range;
    };

    const float lo_x = value_to_x( *v_min );
    const float hi_x = value_to_x( *v_max );

    const ImRect lo_bb( ImVec2( lo_x - thumb_r, track_y - thumb_r ), ImVec2( lo_x + thumb_r, track_y + thumb_r ) );
    const ImRect hi_bb( ImVec2( hi_x - thumb_r, track_y - thumb_r ), ImVec2( hi_x + thumb_r, track_y + thumb_r ) );

    bool changed = false;
    bool lo_hov = false, lo_held = false;
    bool hi_hov = false, hi_held = false;
    ImGui::ButtonBehavior( lo_bb, id_lo, &lo_hov, &lo_held );
    ImGui::ButtonBehavior( hi_bb, id_hi, &hi_hov, &hi_held );
    // Space/Enter also "hold" a button; only a mouse drag moves a thumb to the pointer.
    if ( ImGui::GetCurrentContext()->ActiveIdSource != ImGuiInputSource_Mouse )
        lo_held = hi_held = false;

    if ( !IsCurrentItemDisabled() )
    {
        const float lo_cur = *v_min, hi_cur = *v_max;
        const float hi_lim = hi_cur, lo_lim = lo_cur;
        SliderKeyboardStep( id_lo, ImGuiDataType_Float, v_min, &v_lo, &hi_lim, &v_lo, &v_hi );
        SliderKeyboardStep( id_hi, ImGuiDataType_Float, v_max, &lo_lim, &v_hi, &v_lo, &v_hi );
        if ( *v_min != lo_cur || *v_max != hi_cur )
            changed = true;
    }
    if ( lo_held )
    {
        const float nv = ImClamp( x_to_value( ImGui::GetMousePos().x ), v_lo, *v_max );
        if ( nv != *v_min )
        {
            *v_min  = nv;
            changed = true;
        }
    }
    if ( hi_held )
    {
        const float nv = ImClamp( x_to_value( ImGui::GetMousePos().x ), *v_min, v_hi );
        if ( nv != *v_max )
        {
            *v_max  = nv;
            changed = true;
        }
    }

    ImDrawList * dl = w->DrawList;
    dl->AddRectFilled( ImVec2( track_left, track_y - track_h * 0.5f ), ImVec2( track_right, track_y + track_h * 0.5f ), ImFluent::GetColorU32( ImFluentCol_ControlStrongFillDefault ), track_h * 0.5f );
    dl->AddRectFilled( ImVec2( lo_x, track_y - track_h * 0.5f ), ImVec2( hi_x, track_y + track_h * 0.5f ), ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), track_h * 0.5f );

    const float inner_rest       = FluentDpx( style.SliderThumbInnerRadius );
    const float inner_step       = FluentDpx( style.SpacingXSmall );
    const float stroke_thin      = FluentDpx( style.StrokeThin );
    const ImU32 col_thumb_solid  = ImFluent::GetColorU32( ImFluentCol_ControlSolidFillDefault );
    const ImU32 col_thumb_stroke = ImFluent::GetColorU32( ImFluentCol_ControlStrokeDefault );
    const ImU32 col_thumb_inner  = ImFluent::GetColorU32( ImFluentCol_AccentFillDefault );
    for ( int side = 0; side < 2; ++side )
    {
        const ImVec2 c           = ( side == 0 ) ? ImVec2( lo_x, track_y ) : ImVec2( hi_x, track_y );
        const bool hov           = ( side == 0 ) ? lo_hov : hi_hov;
        const bool act           = ( side == 0 ) ? lo_held : hi_held;
        const float inner_target = act ? inner_rest - inner_step : hov ? inner_rest + inner_step : inner_rest;
        const float inner_r      = AnimateFloat( side == 0 ? id_lo : id_hi, inner_target, 0.083f, 1 );
        dl->AddCircleFilled( c, thumb_r, col_thumb_solid, 32 );
        dl->AddCircle( c, thumb_r, col_thumb_stroke, 32, stroke_thin );
        dl->AddCircleFilled( c, inner_r, col_thumb_inner, 24 );
        RenderNavFocusRing( side == 0 ? id_lo : id_hi, ImRect( c.x - thumb_r, c.y - thumb_r, c.x + thumb_r, c.y + thumb_r ), thumb_r );
    }

    if ( format && *format )
    {
        const char * txt;
        const char * txt_end;
        ImFormatStringToTempBuffer( &txt, &txt_end, format, *v_min );
        ImGui::SameLine();
        ImGui::TextUnformatted( txt, txt_end );
        ImGui::SameLine();
        ImGui::TextUnformatted( "\xe2\x80\x93" );
        ImGui::SameLine();
        ImFormatStringToTempBuffer( &txt, &txt_end, format, *v_max );
        ImGui::TextUnformatted( txt, txt_end );
    }

    RenderAndConsumePendingDescription();
    return changed;
}

bool ImFluent::ColorPicker( const char * label, float col[ 4 ], ImGuiColorEditFlags flags )
{
    RenderAndConsumePendingHeader();
    const ImFluentStyle & style = ImFluent::GetStyle();

    ImFluentStackGuard g;
    g.PushStyleVar( ImGuiStyleVar_FrameRounding, FluentDpx( style.ControlCornerRadius ) );
    g.PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( FluentDpx( style.SpacingLarge ), ( FluentDpx( style.ControlHeight ) - ImGui::GetFontSize() ) * 0.5f ) );
    g.PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( FluentDpx( style.SpacingMedium ), FluentDpx( style.SpacingSmall ) ) );
    g.PushStyleColor( ImGuiCol_FrameBg, ImFluent::GetColorU32( ImFluentCol_ControlFillDefault ) );
    g.PushStyleColor( ImGuiCol_FrameBgHovered, ImFluent::GetColorU32( ImFluentCol_ControlFillSecondary ) );
    g.PushStyleColor( ImGuiCol_FrameBgActive, ImFluent::GetColorU32( ImFluentCol_ControlFillTertiary ) );
    g.PushStyleColor( ImGuiCol_Button, ImFluent::GetColorU32( ImFluentCol_ControlFillDefault ) );
    g.PushStyleColor( ImGuiCol_ButtonHovered, ImFluent::GetColorU32( ImFluentCol_ControlFillSecondary ) );
    g.PushStyleColor( ImGuiCol_ButtonActive, ImFluent::GetColorU32( ImFluentCol_ControlFillTertiary ) );
    g.PushStyleColor( ImGuiCol_Border, ImFluent::GetColorU32( ImFluentCol_ControlStrokeDefault ) );

    const bool changed = ImGui::ColorPicker4( label, col, flags );

    RenderAndConsumePendingDescription();
    return changed;
}

bool ImFluent::ColorEdit( const char * label, float col[ 4 ], ImGuiColorEditFlags flags )
{
    RenderAndConsumePendingHeader();
    const ImFluentStyle & style = ImFluent::GetStyle();
    const float h               = FluentDpx( style.ControlHeight );

    ImFluentStackGuard g;
    g.PushStyleVar( ImGuiStyleVar_FrameRounding, FluentDpx( style.ControlCornerRadius ) );
    g.PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( FluentDpx( style.SpacingLarge ), ( h - ImGui::GetFontSize() ) * 0.5f ) );
    g.PushStyleColor( ImGuiCol_FrameBg, ImFluent::GetColorU32( ImFluentCol_ControlFillDefault ) );
    g.PushStyleColor( ImGuiCol_FrameBgHovered, ImFluent::GetColorU32( ImFluentCol_ControlFillSecondary ) );
    g.PushStyleColor( ImGuiCol_FrameBgActive, ImFluent::GetColorU32( ImFluentCol_ControlFillTertiary ) );
    g.PushStyleColor( ImGuiCol_Button, ImFluent::GetColorU32( ImFluentCol_ControlFillDefault ) );
    g.PushStyleColor( ImGuiCol_ButtonHovered, ImFluent::GetColorU32( ImFluentCol_ControlFillSecondary ) );
    g.PushStyleColor( ImGuiCol_ButtonActive, ImFluent::GetColorU32( ImFluentCol_ControlFillTertiary ) );
    g.PushStyleColor( ImGuiCol_PopupBg, ImFluent::GetColorU32( ImFluentCol_SolidBgQuarternary ) );
    g.PushStyleColor( ImGuiCol_Border, ImFluent::GetColorU32( ImFluentCol_SurfaceStrokeFlyout ) );

    const bool changed = ImGui::ColorEdit4( label, col, flags );

    const ImRect input_rect = ImGui::GetCurrentContext()->LastItemData.Rect;
    RenderAndConsumePendingDescription();
    RenderAndConsumePendingError( input_rect );
    return changed;
}

bool ImFluent::ColorButton( const char * desc_id, const ImVec4 & col, ImGuiColorEditFlags flags, const ImVec2 & size )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImFluentStackGuard g;
    g.PushStyleVar( ImGuiStyleVar_FrameRounding, FluentDpx( style.ControlCornerRadius ) );
    g.PushStyleColor( ImGuiCol_Border, ImFluent::GetColorU32( ImFluentCol_ControlStrokeDefault ) );
    const float d      = ( size.x > 0.f && size.y > 0.f ) ? 0.f : FluentDpx( style.ControlHeight );
    const ImVec2 sz    = ( size.x > 0.f && size.y > 0.f ) ? size : ImVec2( d, d );
    const bool pressed = ImGui::ColorButton( desc_id, col, flags, sz );
    g.Restore();

    const ImGuiContext & gc = *ImGui::GetCurrentContext();
    const ImRect bb         = gc.LastItemData.Rect;
    const ImGuiID id        = gc.LastItemData.ID;
    const bool hovered      = ImGui::IsItemHovered();
    const bool held         = ImGui::IsItemActive();
    if ( hovered || held )
    {
        ImDrawList * dl  = ImGui::GetWindowDrawList();
        const float r    = FluentDpx( style.ControlCornerRadius );
        const float th   = ImFluent::FluentDpx( held ? style.StrokeThick : style.StrokeMedium );
        const ImU32 ring = ImFluent::AnimateColorU32( id, ImFluent::GetColorU32( held ? ImFluentCol_AccentFillTertiary : ImFluentCol_AccentFillDefault ), 0.083f, 1 );
        dl->AddRect( bb.Min, bb.Max, ring, r, 0, th );
    }
    return pressed;
}

void ImFluent::ProgressBar( float fraction, const ImVec2 & size_arg, const char * overlay, ImFluentProgressBarState state )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return;
    const ImFluentStyle & style = ImFluent::GetStyle();

    const float h    = ( size_arg.y > 0 ? size_arg.y : FluentDpx( style.ProgressBarHeight ) );
    const float W    = ( size_arg.x < 0.f ) ? ImGui::GetContentRegionAvail().x : ( size_arg.x > 0.f ) ? size_arg.x : ImGui::CalcItemWidth();
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + W, pos.y + h ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, 0 ) )
        return;
    if ( fraction < 0 )
        fraction = 0;
    if ( fraction > 1 )
        fraction = 1;

    const ImU32 fill_col = ImFluent::GetColorU32( ( state == ImFluentProgressBarState_Error ) ? ImFluentCol_SystemFillCritical : ( state == ImFluentProgressBarState_Paused ) ? ImFluentCol_SystemFillCaution : ImFluentCol_AccentFillDefault );

    ImDrawList * dl = w->DrawList;
    dl->AddRectFilled( bb.Min, bb.Max, ImFluent::GetColorU32( ImFluentCol_ControlStrongFillDefault, 0.20f ), h * 0.5f );
    if ( fraction > 0.f )
        dl->AddRectFilled( bb.Min, ImVec2( bb.Min.x + W * fraction, bb.Max.y ), fill_col, h * 0.5f );
    if ( overlay )
    {
        const ImVec2 ts = ImGui::CalcTextSize( overlay );
        dl->AddText( ImVec2( bb.Min.x + ( W - ts.x ) * 0.5f, bb.Max.y + FluentDpx( style.SpacingSmall ) ), ImFluent::GetColorU32( ImFluentCol_TextSecondary ), overlay );
    }
}

void ImFluent::ProgressRing( float diameter_dpx, float fraction )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const float D               = FluentDpx( diameter_dpx );
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + D, pos.y + D ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, 0 ) )
        return;
    ImDrawList * dl = w->DrawList;
    const ImVec2 c( pos.x + D * 0.5f, pos.y + D * 0.5f );
    const float radius    = D * 0.5f - FluentDpx( style.SpacingXSmall );
    const float thickness = FluentDpx( style.ProgressRingThickness );
    if ( fraction < 0.f )
    {
        const float t              = ( float )ImGui::GetTime();
        const float period         = 1.333f;
        const float sweep_per_half = IM_PI * 1.5f;
        const int cycle_index      = ( int )( t / period );
        const float phase          = ( t - cycle_index * period ) / period;
        const float base_rotation  = t * ( IM_PI * 0.5f );
        const float cycle_anchor   = cycle_index * sweep_per_half;

        const float head_p_raw = ImClamp( phase * 2.f, 0.f, 1.f );
        const float tail_p_raw = ImClamp( ( phase - 0.5f ) * 2.f, 0.f, 1.f );
        const float head_p     = ( head_p_raw < 0.5f ) ? 2.f * head_p_raw * head_p_raw : 1.f - 2.f * ( 1.f - head_p_raw ) * ( 1.f - head_p_raw );
        const float tail_p     = ( tail_p_raw < 0.5f ) ? 2.f * tail_p_raw * tail_p_raw : 1.f - 2.f * ( 1.f - tail_p_raw ) * ( 1.f - tail_p_raw );

        const float tail_angle = base_rotation + cycle_anchor + tail_p * sweep_per_half;
        const float head_angle = base_rotation + cycle_anchor + head_p * sweep_per_half;
        const float arc_span   = head_angle - tail_angle;

        if ( arc_span > 0.0005f )
        {
            const int segs = ImMax( 8, ( int )( arc_span * 16.f ) );
            dl->PathClear();
            dl->PathArcTo( c, radius, tail_angle, head_angle, segs );
            dl->PathStroke( ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), 0, thickness );
        }
    }
    else
    {
        if ( fraction > 1.f )
            fraction = 1.f;
        const int segs = 64;

        dl->AddCircle( c, radius, ImFluent::GetColorU32( ImFluentCol_ControlStrongFillDefault, 0.20f ), segs, thickness );
        if ( fraction > 0.f )
        {
            const float a0 = -IM_PI * 0.5f;
            const float a1 = a0 + IM_PI * 2.f * fraction;
            dl->PathClear();
            dl->PathArcTo( c, radius, a0, a1, ( int )( segs * fraction ) );
            dl->PathStroke( ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), 0, thickness );
        }
    }
}

// [SECTION] Text input (TextBox / PasswordBox / NumberBox / RichEditBox / AutoSuggestBox / TextBlock)

struct ImFluentTextInputChainCb
{
    const char * Buf;
    int MaxChars;
    ImGuiInputTextCallback UserCallback;
    void * UserCallbackData;
    bool ClearPending;
};

static int ImFluentTextInputChainCbProc( ImGuiInputTextCallbackData * data )
{
    ImFluentTextInputChainCb * cc = ( ImFluentTextInputChainCb * )data->UserData;
    if ( !cc )
        return 0;
    if ( cc->ClearPending && data->EventFlag == ImGuiInputTextFlags_CallbackAlways )
    {
        if ( data->BufTextLen > 0 )
            data->DeleteChars( 0, data->BufTextLen );
        cc->ClearPending = false;
        return 0;
    }
    if ( cc->MaxChars > 0 && data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter )
    {
        int n_chars = 0;
        if ( cc->Buf )
            for ( const char * p = cc->Buf; *p; ++p )
                if ( ( *p & 0xC0 ) != 0x80 )
                    ++n_chars;
        if ( n_chars >= cc->MaxChars )
            return 1;
    }
    if ( cc->UserCallback )
    {
        void * saved   = data->UserData;
        data->UserData = cc->UserCallbackData;
        const int r    = cc->UserCallback( data );
        data->UserData = saved;
        cc->Buf = data->Buf;
        return r;
    }
    return 0;
}

static void PrepareTextInputCallback( const char * buf, int max_length, bool clear_pending, ImGuiInputTextFlags & flags, ImFluentTextInputChainCb & chain, ImGuiInputTextCallback & out_cb, void *& out_cb_user )
{
    const ImGuiInputTextCallback user_cb = g_Ctx.NextTextBox.HasUserCallback ? g_Ctx.NextTextBox.UserCallback : NULL;
    void * const user_cb_data            = g_Ctx.NextTextBox.UserCallbackData;
    g_Ctx.NextTextBox                    = ImFluentNextTextBoxData();

    chain.Buf              = buf;
    chain.MaxChars         = max_length;
    chain.UserCallback     = user_cb;
    chain.UserCallbackData = user_cb_data;
    chain.ClearPending     = clear_pending;

    out_cb      = NULL;
    out_cb_user = NULL;
    if ( max_length > 0 || user_cb || clear_pending )
    {
        if ( max_length > 0 )
            flags |= ImGuiInputTextFlags_CallbackCharFilter;
        if ( clear_pending )
            flags |= ImGuiInputTextFlags_CallbackAlways;
        out_cb      = ImFluentTextInputChainCbProc;
        out_cb_user = &chain;
    }
}

// Stand-alone resize trampoline used by the std::string overloads of TextBox / RichEditBox.
// We install this as the next user callback so the base char* implementation drives it via
// SetNextTextInputTextCallback's chain — no special-casing inside the base function.
struct ImFluentStdStringResizeChain
{
    std::string * Str;
    ImGuiInputTextCallback ChainCallback;
    void * ChainCallbackData;
};

static int ImFluentStdStringResizeCb( ImGuiInputTextCallbackData * data )
{
    ImFluentStdStringResizeChain * rc = ( ImFluentStdStringResizeChain * )data->UserData;
    if ( !rc )
        return 0;
    if ( data->EventFlag == ImGuiInputTextFlags_CallbackResize )
    {
        std::string * str = rc->Str;
        IM_ASSERT( data->Buf == str->c_str() );
        str->resize( data->BufTextLen );
        data->Buf = ( char * )str->c_str();
        return 0;
    }
    if ( rc->ChainCallback )
    {
        void * saved   = data->UserData;
        data->UserData = rc->ChainCallbackData;
        const int r    = rc->ChainCallback( data );
        data->UserData = saved;
        return r;
    }
    return 0;
}

void ImFluent::SetNextTextInputTextCallback( ImGuiInputTextCallback callback, void * user_data )
{
    g_Ctx.NextTextBox.UserCallback     = callback;
    g_Ctx.NextTextBox.UserCallbackData = user_data;
    g_Ctx.NextTextBox.HasUserCallback  = ( callback != NULL );
}

bool ImFluent::TextBox( const char * label, char * buf, size_t buf_size, const char * hint, ImGuiInputTextFlags extra_flags, ImFluentTextBoxFlags fluent_flags, int max_length )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    RenderAndConsumePendingHeader();

    const ImFluentStyle & style = ImFluent::GetStyle();
    BeginTextControlFrame( FluentDpx( style.ControlHeight ) );

    const ImGuiID input_id_predicted = ImGui::GetID( label );
    ImGuiStorage * storage           = ImGui::GetStateStorage();
    const ImGuiID clear_pending_key  = input_id_predicted ^ 0xC1EA8C1Eu;
    const bool consume_clear         = storage->GetBool( clear_pending_key, false );
    if ( consume_clear )
    {
        storage->SetBool( clear_pending_key, false );

        // Safety net: a mouse click on the visual clear button already activated the
        // InputText naturally, but for nav (Space/Enter) activations — and for any
        // case where focus was lost between frames — we still need it active so
        // CallbackAlways fires for DeleteChars. NavActivateId being already set means
        // a prior SetKeyboardFocusHere has resolved this frame, no need to re-submit.
        const ImGuiContext & g = *ImGui::GetCurrentContext();
        if ( g.ActiveId != input_id_predicted && g.NavActivateId != input_id_predicted )
            ImGui::SetKeyboardFocusHere();
    }

    ImFluentTextInputChainCb chain;
    ImGuiInputTextCallback cb;
    void * cb_user;
    PrepareTextInputCallback( buf, max_length, consume_clear, extra_flags, chain, cb, cb_user );

    TextInputEscapeKeepsText( input_id_predicted );
    bool changed = hint ? ImGui::InputTextWithHint( label, hint, buf, buf_size, extra_flags, cb, cb_user ) : ImGui::InputText( label, buf, buf_size, extra_flags, cb, cb_user );

    // SetKeyboardFocusHere has a one-frame latency: nav-activate Enter on the clear
    // button schedules the focus, but the InputText doesn't actually become active
    // until the next frame. If the trampoline didn't consume ClearPending this frame
    // (still true), re-arm the storage flag so DeleteChars fires once focus resolves.
    if ( chain.ClearPending )
        storage->SetBool( clear_pending_key, true );

    EndTextControlFrame();

    // Snapshot the InputText's LastItemData so caller-side IsItem*() queries keep
    // working — the clear button's ItemAdd, the counter's TextUnformatted, and the
    // pending description/error helpers all overwrite g.LastItemData below.
    const ImGuiLastItemData input_item_data = ImGui::GetCurrentContext()->LastItemData;
    const ImRect & input_bb                 = input_item_data.Rect;
    const ImGuiID input_id                  = input_item_data.ID;
    const bool active                       = ImGui::IsItemActive();
    const bool inputtext_hov                = ImGui::IsItemHovered();

    const char * cur_buf      = chain.Buf;
    const bool show_clear_btn = ( fluent_flags & ImFluentTextBoxFlags_ClearButton ) && cur_buf && cur_buf[ 0 ];

    if ( show_clear_btn )
    {
        const float btn_w = FluentDpx( style.RevealButtonWidth );
        const float inset = FluentDpx( style.SpacingXSmall );
        const ImRect btn_bb( ImVec2( input_bb.Max.x - btn_w - inset, input_bb.Min.y + inset ), ImVec2( input_bb.Max.x - inset, input_bb.Max.y - inset ) );

        // Register the button for keyboard/gamepad nav (Tab focus + Space/Enter
        // activate). We deliberately skip ItemHoverable/ButtonBehavior so the button
        // does not claim mouse hover or consume the click — mouse falls through to
        // the InputText, which activates naturally and fires all its usual callbacks.
        const ImGuiID button_id = input_id ^ 0xC1EA8u;
        ImGui::KeepAliveID( button_id );
        ImGui::ItemAdd( btn_bb, button_id, NULL, ImGuiItemFlags_NoTabStop );
        ReportItemInfo( button_id, "Clear" );

        const ImGuiContext & g = *ImGui::GetCurrentContext();
        const bool nav_focused = ( g.NavId == button_id && g.NavCursorVisible );
        const bool mouse_in    = ImGui::IsMouseHoveringRect( btn_bb.Min, btn_bb.Max );
        const bool hov         = ( inputtext_hov && mouse_in ) || nav_focused;
        const bool held        = ( hov && ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) || ( nav_focused && g.NavActivateDownId == button_id );

        ImDrawList * dl          = w->DrawList;
        const float r            = FluentDpx( style.ControlCornerRadius );
        const ImU32 clear_target = ImFluent::GetColorU32( ( hov || held ) ? held ? ImFluentCol_SubtleFillTertiary : ImFluentCol_SubtleFillSecondary : ImFluentCol_SubtleFillTransparent );
        const ImU32 clear_anim   = ImFluent::AnimateColorU32( button_id, clear_target );
        if ( hov || held )
        {
            const ImU32 frame_bg = ImFluent::GetColorU32( active ? ImFluentCol_ControlFillInputActive : ImFluentCol_ControlFillDefault );
            dl->AddRectFilled( btn_bb.Min, btn_bb.Max, ImFluent::GetColorU32( ImFluentCol_LayerFillAlt ), r, ImDrawFlags_RoundCornersRight );
            dl->AddRectFilled( btn_bb.Min, btn_bb.Max, frame_bg, r, ImDrawFlags_RoundCornersRight );
        }
        dl->AddRectFilled( btn_bb.Min, btn_bb.Max, clear_anim, r, ImDrawFlags_RoundCornersRight );
        const char * x_glyph = ImFluentIcon_Cancel;
        const ImVec2 ts      = ImGui::CalcTextSize( x_glyph );
        dl->AddText( ImVec2( btn_bb.Min.x + ( btn_bb.GetWidth() - ts.x ) * 0.5f, btn_bb.Min.y + ( btn_bb.GetHeight() - ts.y ) * 0.5f ), ImFluent::GetColorU32( ImFluentCol_TextSecondary ), x_glyph );

        ImGui::RenderNavCursor( btn_bb, button_id );

        const bool mouse_press = inputtext_hov && mouse_in && ImGui::IsMouseClicked( ImGuiMouseButton_Left );
        const bool nav_press   = ( g.NavActivateId == button_id );
        if ( mouse_press || nav_press )
            storage->SetBool( clear_pending_key, true );
    }

    if ( active && !HasPendingError() )
    {
        ImDrawList * dl = w->DrawList;
        const float t   = FluentDpx( style.SpacingXSmall );
        dl->AddRectFilled( ImVec2( input_bb.Min.x + FluentDpx( style.ControlCornerRadius ), input_bb.Max.y - t ), ImVec2( input_bb.Max.x - FluentDpx( style.ControlCornerRadius ), input_bb.Max.y ), ImFluent::GetColorU32( ImFluentCol_ElevationTextControlFocusedBottom ) );
    }

    if ( ( fluent_flags & ImFluentTextBoxFlags_ShowCounter ) && max_length > 0 && cur_buf )
    {
        int n_chars = 0;
        for ( const char * p = cur_buf; *p; ++p )
            if ( ( *p & 0xC0 ) != 0x80 )
                ++n_chars;
        const char * counter;
        const char * counter_end;
        ImFormatStringToTempBuffer( &counter, &counter_end, "%d / %d", n_chars, max_length );
        const ImVec2 ts = ImGui::CalcTextSize( counter, counter_end );
        ImFluent::PushFont( ImFluentTextStyle_Caption );
        ImGui::PushStyleColor( ImGuiCol_Text, ImFluent::GetColorU32( ImFluentCol_TextSecondary ) );
        ImGui::SetCursorScreenPos( ImVec2( input_bb.Max.x - ts.x, input_bb.Max.y + FluentDpx( style.SpacingXSmall ) ) );
        ImGui::TextUnformatted( counter, counter_end );
        ImGui::PopStyleColor();
        ImFluent::PopFont();
    }

    RenderAndConsumePendingDescription();
    RenderAndConsumePendingError( input_bb );

    ImGui::GetCurrentContext()->LastItemData = input_item_data;
    return changed;
}

bool ImFluent::TextBox( const char * label, std::string & str, const char * hint, ImGuiInputTextFlags extra_flags, ImFluentTextBoxFlags fluent_flags, int max_length )
{
    ImFluentStdStringResizeChain resize_chain;
    resize_chain.Str               = &str;
    resize_chain.ChainCallback     = g_Ctx.NextTextBox.HasUserCallback ? g_Ctx.NextTextBox.UserCallback : NULL;
    resize_chain.ChainCallbackData = g_Ctx.NextTextBox.UserCallbackData;
    ImFluent::SetNextTextInputTextCallback( ImFluentStdStringResizeCb, &resize_chain );

    return ImFluent::TextBox( label, ( char * )str.c_str(), str.capacity() + 1, hint, extra_flags | ImGuiInputTextFlags_CallbackResize, fluent_flags, max_length );
}

bool ImFluent::PasswordBox( const char * label, char * buf, size_t buf_size, const char * hint, ImGuiInputTextFlags flags )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;

    RenderAndConsumePendingHeader();

    const ImFluentStyle & style = ImFluent::GetStyle();
    const float h               = FluentDpx( style.ControlHeight );

    const ImVec2 cursor          = w->DC.CursorPos;
    const float full_w           = ImGui::CalcItemWidth();
    const ImRect input_predicted = ImRect( cursor, ImVec2( cursor.x + full_w, cursor.y + h ) );
    const float reveal_w         = FluentDpx( style.RevealButtonWidth );
    const float inset            = FluentDpx( style.SpacingXSmall );
    const ImRect btn_pred( ImVec2( input_predicted.Max.x - reveal_w - inset, input_predicted.Min.y + inset ), ImVec2( input_predicted.Max.x - inset, input_predicted.Max.y - inset ) );

    const bool hov  = IsMouseOverWindowRect( btn_pred );
    const bool held = hov && ImGui::IsMouseDown( ImGuiMouseButton_Left );

    // WinUI PasswordBox: Alt+F8 (ImFluentActions::RevealPassword) shows the password while the key is held.
    ImGuiContext & g           = *GImGui;
    const ImGuiID pw_id        = ImGui::GetID( label );
    ImGuiStorage * st          = ImGui::GetStateStorage();
    const ImGuiID peek_key     = pw_id ^ 0x9EE6F8u;
    const bool has_focus       = g.ActiveId == pw_id || g.NavId == pw_id;
    const ImGuiKeyChord reveal = g_Ctx.Actions.RevealPassword;
    const ImGuiKey reveal_key  = ( ImGuiKey )( reveal & ~ImGuiMod_Mask_ );
    bool key_peek              = reveal != 0 && st->GetBool( peek_key, false ) && has_focus && ImGui::IsKeyDown( reveal_key );
    if ( has_focus && IsActionPressed( reveal ) )
        key_peek = true;
    st->SetBool( peek_key, key_peek );
    const bool revealed = held || key_peek;

    ImGuiInputTextFlags fl = revealed ? flags : ( flags | ImGuiInputTextFlags_Password );

    ImFluentTextInputChainCb chain;
    ImGuiInputTextCallback cb;
    void * cb_user;
    PrepareTextInputCallback( buf, /*max_length*/ 0, /*clear_pending*/ false, fl, chain, cb, cb_user );

    BeginTextControlFrame( h );
    TextInputEscapeKeepsText( pw_id );
    const bool changed = hint ? ImGui::InputTextWithHint( label, hint, buf, buf_size, fl, cb, cb_user ) : ImGui::InputText( label, buf, buf_size, fl, cb, cb_user );
    EndTextControlFrame();

    const bool input_active = ImGui::IsItemActive();
    const ImRect input_rect = ImGui::GetCurrentContext()->LastItemData.Rect;

    if ( input_active && !HasPendingError() )
    {
        ImDrawList * dl0 = w->DrawList;
        const float t    = FluentDpx( style.SpacingXSmall );
        dl0->AddRectFilled( ImVec2( input_rect.Min.x + FluentDpx( style.ControlCornerRadius ), input_rect.Max.y - t ), ImVec2( input_rect.Max.x - FluentDpx( style.ControlCornerRadius ), input_rect.Max.y ), ImFluent::GetColorU32( ImFluentCol_ElevationTextControlFocusedBottom ) );
    }

    const ImRect btn_bb( ImVec2( input_rect.Max.x - reveal_w - inset, input_rect.Min.y + inset ), ImVec2( input_rect.Max.x - inset, input_rect.Max.y - inset ) );

    ImDrawList * dl = w->DrawList;
    const float r   = FluentDpx( style.ControlCornerRadius );
    if ( hov || held )
    {
        const ImU32 frame_bg = ImFluent::GetColorU32( input_active ? ImFluentCol_ControlFillInputActive : ImFluentCol_ControlFillDefault );
        dl->AddRectFilled( btn_bb.Min, btn_bb.Max, ImFluent::GetColorU32( ImFluentCol_LayerFillAlt ), r, ImDrawFlags_RoundCornersRight );
        dl->AddRectFilled( btn_bb.Min, btn_bb.Max, frame_bg, r, ImDrawFlags_RoundCornersRight );
        const ImU32 hi = ImFluent::GetColorU32( held ? ImFluentCol_SubtleFillTertiary : ImFluentCol_SubtleFillSecondary );
        dl->AddRectFilled( btn_bb.Min, btn_bb.Max, hi, r, ImDrawFlags_RoundCornersRight );
    }

    const char * eye_glyph = ImFluentIcon_View;
    const ImVec2 ts        = ImGui::CalcTextSize( eye_glyph );
    dl->AddText( ImVec2( btn_bb.Min.x + ( btn_bb.GetWidth() - ts.x ) * 0.5f, btn_bb.Min.y + ( btn_bb.GetHeight() - ts.y ) * 0.5f ), ImFluent::GetColorU32( ImFluentCol_TextSecondary ), eye_glyph );

    RenderAndConsumePendingDescription();
    RenderAndConsumePendingError( input_rect );
    return changed;
}

bool ImFluent::PasswordBox( const char * label, std::string & str, const char * hint, ImGuiInputTextFlags flags )
{
    ImFluentStdStringResizeChain resize_chain;
    resize_chain.Str               = &str;
    resize_chain.ChainCallback     = g_Ctx.NextTextBox.HasUserCallback ? g_Ctx.NextTextBox.UserCallback : NULL;
    resize_chain.ChainCallbackData = g_Ctx.NextTextBox.UserCallbackData;
    ImFluent::SetNextTextInputTextCallback( ImFluentStdStringResizeCb, &resize_chain );

    return ImFluent::PasswordBox( label, ( char * )str.c_str(), str.capacity() + 1, hint, flags | ImGuiInputTextFlags_CallbackResize );
}

bool ImFluent::NumberBox( const char * label, double * v, double step, double step_fast, const char * format, ImGuiInputTextFlags flags )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems || !v )
    {
        ClearPendingNextItem();
        return false;
    }
    RenderAndConsumePendingHeader();

    const ImFluentStyle & style = ImFluent::GetStyle();
    const float h               = FluentDpx( style.ControlHeight );

    BeginTextControlFrame( h );

    ImGui::SetNextItemAllowOverlap();

    bool changed = ImGui::InputDouble( label, v, 0.0, 0.0, format, flags );
    EndTextControlFrame();

    const bool input_active = ImGui::IsItemActive();
    const ImRect input_rect = ImGui::GetCurrentContext()->LastItemData.Rect;
    const ImGuiID input_id  = ImGui::GetCurrentContext()->LastItemData.ID;

    const float spin_w = FluentDpx( style.SpinButtonWidth + style.SpacingMedium ); // 36: WinUI inline spin button
    const float inset  = FluentDpx( style.SpacingXSmall );
    // WinUI NumberBox SpinButtonPlacementMode="Inline": up and down buttons side by side at the right of the box.
    const ImRect spin_area( ImVec2( input_rect.Max.x - spin_w * 2.f - inset, input_rect.Min.y + inset ), ImVec2( input_rect.Max.x - inset, input_rect.Max.y - inset ) );
    const ImRect up_bb( spin_area.Min, ImVec2( spin_area.Min.x + spin_w, spin_area.Max.y ) );
    const ImRect dn_bb( ImVec2( spin_area.Min.x + spin_w, spin_area.Min.y ), spin_area.Max );

    ImDrawList * dl = w->DrawList;
    const float r   = FluentDpx( style.ControlCornerRadius );

    ImGui::PushItemFlag( ImGuiItemFlags_ButtonRepeat, g_Ctx.Actions.RepeatStepButtons );

    struct SpinBtn
    {
        const ImRect bb;
        const ImGuiID id;
        const ImGuiDir dir;
        const double sign;
    };
    const SpinBtn btns[ 2 ] = {
        { up_bb, input_id ^ 0x5817u, ImGuiDir_Up, +1.0 },
        { dn_bb, input_id ^ 0xD0E1u, ImGuiDir_Down, -1.0 },
    };

    bool b_state[ 2 ][ 2 ] = {};
    for ( int i = 0; i < 2; ++i )
    {
        const SpinBtn & b = btns[ i ];
        ImGui::ItemAdd( b.bb, b.id, NULL, ImGuiItemFlags_NoNav ); // WinUI spin buttons are not tab stops
        ReportItemInfo( b.id, b.sign > 0.0 ? "Increase" : "Decrease" );
        bool hov = false, held = false;
        const bool pr = ImGui::ButtonBehavior( b.bb, b.id, &hov, &held );
        if ( pr && v )
        {
            const double s = ImGui::GetIO().KeyCtrl ? step_fast : step;
            *v += b.sign * s;
            changed = true;
        }
        b_state[ i ][ 0 ] = hov;
        b_state[ i ][ 1 ] = held;
    }

    const ImU32 spin_layer_on    = ImFluent::GetColorU32( ImFluentCol_LayerFillAlt );
    const ImU32 spin_frame_on    = ImFluent::GetColorU32( input_active ? ImFluentCol_ControlFillInputActive : ImFluentCol_ControlFillDefault );
    const ImU32 spin_transparent = IM_COL32( 0, 0, 0, 0 );

    for ( int i = 0; i < 2; ++i )
    {
        const SpinBtn & b = btns[ i ];
        const bool hov    = b_state[ i ][ 0 ];
        const bool held   = b_state[ i ][ 1 ];

        const ImU32 layer_target = ( hov || held ) ? spin_layer_on : spin_transparent;
        const ImU32 frame_target = ( hov || held ) ? spin_frame_on : spin_transparent;
        const ImU32 hi_target    = held ? ImFluent::GetColorU32( ImFluentCol_SubtleFillTertiary ) : hov ? ImFluent::GetColorU32( ImFluentCol_SubtleFillSecondary ) : spin_transparent;

        dl->AddRectFilled( b.bb.Min, b.bb.Max, ImFluent::AnimateColorU32( b.id, layer_target, 0.083f, 1 ), r );
        dl->AddRectFilled( b.bb.Min, b.bb.Max, ImFluent::AnimateColorU32( b.id, frame_target, 0.083f, 2 ), r );
        dl->AddRectFilled( b.bb.Min, b.bb.Max, ImFluent::AnimateColorU32( b.id, hi_target ), r );

        const ImVec2 c( ( b.bb.Min.x + b.bb.Max.x ) * 0.5f, ( b.bb.Min.y + b.bb.Max.y ) * 0.5f );
        RenderChevron( dl, c, b.dir, ImFluent::GetColorU32( ImFluentCol_TextSecondary ), FluentDpx( style.ChevronGlyphSize + 2.f ) ); // 12 wide
    }

    ImGui::PopItemFlag();

    // Keyboard while editing (WinUI NumberBox): Up/Down step by `step`, PageUp/PageDown by `step_fast`.
    if ( input_active && v )
    {
        double delta = 0.0;
        if ( ImGui::IsKeyPressed( ImGuiKey_UpArrow, ImGuiInputFlags_Repeat, input_id ) )
            delta += step;
        if ( ImGui::IsKeyPressed( ImGuiKey_DownArrow, ImGuiInputFlags_Repeat, input_id ) )
            delta -= step;
        if ( IsChordPressedOwned( g_Ctx.Actions.PageUp, input_id ) )
            delta += step_fast;
        if ( IsChordPressedOwned( g_Ctx.Actions.PageDown, input_id ) )
            delta -= step_fast;
        if ( delta != 0.0 )
        {
            *v += delta;
            changed = true;
            if ( ImGuiInputTextState * state = ImGui::GetInputTextState( input_id ) )
                state->ReloadUserBufAndSelectAll(); // Show the new value in the field being edited
        }
    }

    RenderAndConsumePendingDescription();
    RenderAndConsumePendingError( input_rect );
    return changed;
}

bool ImFluent::RichEditBox( const char * label, char * buf, size_t buf_size, const ImVec2 & size, ImGuiInputTextFlags flags, int max_length )
{
    RenderAndConsumePendingHeader();
    const ImFluentStyle & style = ImFluent::GetStyle();

    ImFluentTextInputChainCb chain;
    ImGuiInputTextCallback cb;
    void * cb_user;
    PrepareTextInputCallback( buf, max_length, /*clear_pending*/ false, flags, chain, cb, cb_user );

    BeginTextControlFrame( ImGui::GetFontSize() + FluentDpx( style.SpacingXLarge ) );
    TextInputEscapeKeepsText( ImGui::GetID( label ) );
    const bool changed = ImGui::InputTextMultiline( label, buf, buf_size, size, flags, cb, cb_user );
    EndTextControlFrame();
    const ImRect input_rect = ImGui::GetCurrentContext()->LastItemData.Rect;
    RenderAndConsumePendingDescription();
    RenderAndConsumePendingError( input_rect );
    return changed;
}

bool ImFluent::RichEditBox( const char * label, std::string & str, const ImVec2 & size, ImGuiInputTextFlags flags, int max_length )
{
    ImFluentStdStringResizeChain resize_chain;
    resize_chain.Str               = &str;
    resize_chain.ChainCallback     = g_Ctx.NextTextBox.HasUserCallback ? g_Ctx.NextTextBox.UserCallback : NULL;
    resize_chain.ChainCallbackData = g_Ctx.NextTextBox.UserCallbackData;
    ImFluent::SetNextTextInputTextCallback( ImFluentStdStringResizeCb, &resize_chain );

    return ImFluent::RichEditBox( label, ( char * )str.c_str(), str.capacity() + 1, size, flags | ImGuiInputTextFlags_CallbackResize, max_length );
}

void ImFluent::SetNextAutoSuggestBoxPredicate( ImFluentAutoSuggestPredicate predicate, void * user_data )
{
    g_Ctx.NextAutoSuggest.Predicate    = predicate;
    g_Ctx.NextAutoSuggest.UserData     = user_data;
    g_Ctx.NextAutoSuggest.HasPredicate = ( predicate != NULL );
}

static const ImGuiID AUTOSUGGEST_POPUP_ID_SALT   = 0xA17051E5u;
static const ImGuiID AUTOSUGGEST_SEL_IDX_KEY_SALT = 0x5E1Eu;
static const ImGuiID AUTOSUGGEST_SCROLL_KEY_SALT = 0x5C01u;

// Returns the stack level of the open popup matching popup_id, or -1 if not open.
static int FindOpenPopupLevel( ImGuiContext & g, ImGuiID popup_id )
{
    for ( int i = g.OpenPopupStack.Size - 1; i >= 0; --i )
    {
        if ( g.OpenPopupStack[ i ].PopupId == popup_id )
            return i;
    }
    return -1;
}

// Close the popup with the given id, if it is currently open. No-op otherwise.
static void ClosePopupById( ImGuiContext & g, ImGuiID popup_id )
{
    const int level = FindOpenPopupLevel( g, popup_id );
    if ( level >= 0 )
        ImGui::ClosePopupToLevel( level, true );
}

// True when the last submitted item has the Disabled item flag set.
static bool IsLastItemDisabled()
{
    const ImGuiContext & g = *ImGui::GetCurrentContext();
    return ( g.LastItemData.ItemFlags & ImGuiItemFlags_Disabled ) != 0;
}

static void CommitAutoSuggestSelection( char * buf, size_t buf_size, const char * item, int item_index, int * out_selected_index, ImGuiID input_id, bool & out_changed )
{
    ImStrncpy( buf, item, buf_size );
    if ( out_selected_index )
        *out_selected_index = item_index;
    out_changed = true;

    ImGuiInputTextState & state = ImGui::GetCurrentContext()->InputTextState;
    if ( state.ID == input_id )
        state.ReloadUserBufAndMoveToEnd();
}

bool ImFluent::AutoSuggestBox( const char * label, char * buf, size_t buf_size, const char * const items[], int items_count, int * selected_index, const char * hint, ImGuiInputTextFlags flags, ImGuiComboFlags combo_flags )
{
    RenderAndConsumePendingHeader();
    const ImFluentStyle & style = ImFluent::GetStyle();

    const ImFluentAutoSuggestPredicate pred_fn = g_Ctx.NextAutoSuggest.HasPredicate ? g_Ctx.NextAutoSuggest.Predicate : NULL;
    void * const pred_user                     = g_Ctx.NextAutoSuggest.UserData;
    g_Ctx.NextAutoSuggest                      = ImFluentNextAutoSuggestData();

    bool changed = TextBox( label, buf, buf_size, hint, flags );

    ImGuiContext & g           = *ImGui::GetCurrentContext();
    const ImGuiID input_id     = g.LastItemData.ID;
    const ImRect input_rect    = g.LastItemData.Rect;
    const bool input_activated = ImGui::IsItemActivated();
    const bool input_active    = ImGui::IsItemActive();
    const bool input_edited    = ImGui::IsItemEdited();

    const bool input_deactivated = ImGui::IsItemDeactivated();

    const ImGuiID popup_id      = input_id ^ AUTOSUGGEST_POPUP_ID_SALT;
    const int popup_level       = FindOpenPopupLevel( g, popup_id );
    const bool popup_open       = ( popup_level >= 0 );
    ImGuiWindow * popup_window  = popup_open ? g.OpenPopupStack[ popup_level ].Window : NULL;
    const bool mouse_over_popup = popup_window && ImGui::IsMouseHoveringRect( popup_window->Rect().Min, popup_window->Rect().Max, false );
    const bool mouse_clicked    = ImGui::IsMouseClicked( ImGuiMouseButton_Left );

    // A click landing in our popup must NOT trigger any of the dismiss paths below:
    // the click also deactivates the InputText (InputTextEx clears ActiveID on
    // click-outside), and if we close the popup here, BeginPopupEx returns false
    // and the Selectable that the user clicked never renders — swallowing the click.
    // The Selectable's own pressed-handler will commit and call CloseCurrentPopup.
    const bool click_in_popup = mouse_clicked && mouse_over_popup;
    const bool click_in_input = mouse_clicked && IsMouseOverWindowRect( input_rect );
    // Click landed outside both input and popup — we own the dismiss because the
    // popup uses NoFocusOnAppearing + NoNav, so ImGui's own ClosePopupsOverWindow
    // doesn't fire when the click stays within the parent window.
    const bool click_outside  = mouse_clicked && !click_in_input && !mouse_over_popup;

    if ( items_count > 0 )
    {
        ImVector< ImFluentAutoSuggestScoredItem > & visible = g_Ctx.AutoSuggestVisible;
        visible.resize( 0 );
        if ( visible.Capacity < items_count )
            visible.reserve( items_count );
        for ( int i = 0; i < items_count; ++i )
        {
            if ( !items[ i ] || !*items[ i ] )
                continue;
            int score = 0;
            if ( pred_fn )
            {
                score = pred_fn( items[ i ], buf, pred_user );
                if ( score <= 0 )
                    continue;
            }
            else if ( buf[ 0 ] && !ImStristr( items[ i ], NULL, buf, NULL ) )
            {
                continue;
            }
            visible.push_back( ImFluentAutoSuggestScoredItem{ i, score } );
        }
        if ( pred_fn && visible.Size > 1 )
        {
            ImQsort( visible.Data, ( size_t )visible.Size, sizeof( ImFluentAutoSuggestScoredItem ), []( const void * a, const void * b ) -> int
            {
                const ImFluentAutoSuggestScoredItem * sa = ( const ImFluentAutoSuggestScoredItem * )a;
                const ImFluentAutoSuggestScoredItem * sb = ( const ImFluentAutoSuggestScoredItem * )b;
                if ( sa->Score != sb->Score )
                    return sb->Score - sa->Score;
                return sa->Index - sb->Index;
            } );
        }

        ImGuiStorage * storage   = ImGui::GetStateStorage();
        const ImGuiID sel_key    = popup_id ^ AUTOSUGGEST_SEL_IDX_KEY_SALT;
        const ImGuiID scroll_key = popup_id ^ AUTOSUGGEST_SCROLL_KEY_SALT;
        int selected_visible     = storage->GetInt( sel_key, 0 );
        bool nav_key_pressed     = false;

        if ( input_activated || input_edited )
            selected_visible = 0;

        const bool input_held = input_active || input_deactivated;
        const bool key_enter  = popup_open && input_held && visible.Size > 0 && ( ImGui::IsKeyPressed( ImGuiKey_Enter, false ) || ImGui::IsKeyPressed( ImGuiKey_KeypadEnter, false ) );
        if ( popup_open && input_active && visible.Size > 0 )
        {
            if ( ImGui::IsKeyPressed( ImGuiKey_DownArrow, true ) )
            {
                selected_visible = ( selected_visible + 1 ) % visible.Size;
                nav_key_pressed  = true;
            }
            else if ( ImGui::IsKeyPressed( ImGuiKey_UpArrow, true ) )
            {
                selected_visible = ( selected_visible - 1 + visible.Size ) % visible.Size;
                nav_key_pressed  = true;
            }
        }
        if ( visible.Size > 0 )
            selected_visible = ImClamp( selected_visible, 0, visible.Size - 1 );
        else
            selected_visible = 0;
        storage->SetInt( sel_key, selected_visible );
        if ( nav_key_pressed )
            storage->SetInt( scroll_key, 1 );

        if ( key_enter )
        {
            const int i = visible[ selected_visible ].Index;
            CommitAutoSuggestSelection( buf, buf_size, items[ i ], i, selected_index, input_id, changed );
        }

        const bool dismiss_popup = popup_open && !click_in_popup && ( input_deactivated || click_outside || key_enter );
        if ( dismiss_popup )
        {
            ImGui::ClosePopupToLevel( popup_level, true );
        }
        else if ( !popup_open && ( input_activated || ( input_active && ( input_edited || click_in_input ) ) ) )
        {
            ImGui::OpenPopupEx( popup_id, ImGuiPopupFlags_None );
        }

        const float h             = FluentDpx( style.ControlHeight );
        const int popup_max_items = ResolveComboPopupMaxItems( combo_flags );
        const float popup_max_h   = CalcComboPopupMaxHeight( popup_max_items, h );

        ImGui::SetNextWindowPos( ImVec2( input_rect.Min.x, input_rect.Max.y + FluentDpx( style.SpacingXSmall ) ) );
        ImGui::SetNextWindowSizeConstraints( ImVec2( input_rect.GetWidth(), 0.f ), ImVec2( input_rect.GetWidth(), popup_max_h ) );
        PushOverlayWindowStyle( style.SpacingXSmall, style.SpacingXSmall );

        const ImGuiWindowFlags wflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        if ( ImGui::BeginPopupEx( popup_id, wflags ) )
        {
            ImGui::BringWindowToDisplayFront( ImGui::GetCurrentWindow() );

            if ( visible.Size == 0 )
            {
                ImFluent::Selectable( ImFluent::LocalizeGetMsg( ImFluentLocKey_AutoSuggestNoSuggestions ), false, NULL, h );
                storage->SetInt( scroll_key, 0 );
            }
            else
            {
                const bool scroll_to_selected = storage->GetInt( scroll_key, 0 ) != 0;
                const float row_advance       = h + ImGui::GetStyle().ItemSpacing.y;
                ImGuiListClipper clipper;
                clipper.Begin( visible.Size, row_advance );
                clipper.IncludeItemByIndex( selected_visible );
                while ( clipper.Step() )
                {
                    for ( int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n )
                    {
                        const int i               = visible[ n ].Index;
                        const bool is_highlighted = ( n == selected_visible );
                        ImGui::PushID( i );
                        if ( is_highlighted && scroll_to_selected )
                            ImGui::SetScrollHereY();
                        if ( ImFluent::Selectable( items[ i ], is_highlighted, NULL, h ) )
                        {
                            CommitAutoSuggestSelection( buf, buf_size, items[ i ], i, selected_index, input_id, changed );
                            ImGui::CloseCurrentPopup();
                        }
                        if ( is_highlighted && g.NavId == input_id && g.NavCursorVisible )
                        {
                            const ImRect sel_bb( ImGui::GetItemRectMin(), ImGui::GetItemRectMax() );
                            RenderNavFocusRing( sel_bb, FluentDpx( style.ControlCornerRadius ) );
                        }
                        ImGui::PopID();
                    }
                }
                storage->SetInt( scroll_key, 0 );
            }
            ImGui::EndPopup();
        }
        PopOverlayWindowStyle();
    }

    RenderAndConsumePendingDescription();
    return changed;
}

bool ImFluent::AutoSuggestBox( const char * label, std::string & str, const char * const items[], int items_count, int * selected_index, const char * hint, ImGuiInputTextFlags flags, ImGuiComboFlags combo_flags )
{
    size_t needed = str.size();
    for ( int i = 0; i < items_count; ++i )
        if ( items[ i ] )
            needed = ImMax( needed, ImStrlen( items[ i ] ) );
    if ( str.capacity() < needed )
        str.reserve( needed );

    ImFluentStdStringResizeChain resize_chain;
    resize_chain.Str               = &str;
    resize_chain.ChainCallback     = g_Ctx.NextTextBox.HasUserCallback ? g_Ctx.NextTextBox.UserCallback : NULL;
    resize_chain.ChainCallbackData = g_Ctx.NextTextBox.UserCallbackData;
    ImFluent::SetNextTextInputTextCallback( ImFluentStdStringResizeCb, &resize_chain );

    const bool changed = ImFluent::AutoSuggestBox( label, ( char * )str.c_str(), str.capacity() + 1, items, items_count, selected_index, hint, flags | ImGuiInputTextFlags_CallbackResize, combo_flags );

    if ( changed )
        str.resize( ImStrlen( str.c_str() ) );

    return changed;
}

void ImFluent::TextBlock( const char * text, ImFluentTextStyle style )
{
    ImGui::AlignTextToFramePadding();

    PushFont( style );
    ImGui::TextUnformatted( text );
    PopFont();
}

void ImFluent::TextBlockColored( const char * text, ImU32 col, ImFluentTextStyle style )
{
    ImGui::AlignTextToFramePadding();

    PushFont( style );
    ImGui::PushStyleColor( ImGuiCol_Text, col );
    ImGui::TextUnformatted( text );
    ImGui::PopStyleColor();
    PopFont();
}

// [SECTION] Separator & Tooltip

void ImFluent::Separator()
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return;
    const float thickness       = 1.f;
    const ImVec2 pos            = w->DC.CursorPos;
    const float W               = ImGui::GetContentRegionAvail().x;
    const ImRect bb( pos, ImVec2( pos.x + W, pos.y + thickness ) );
    ImGui::ItemSize( ImVec2( W, thickness + ImGui::GetStyle().ItemSpacing.y ) );
    if ( !ImGui::ItemAdd( bb, 0 ) )
        return;
    w->DrawList->AddRectFilled( bb.Min, bb.Max, ImFluent::GetColorU32( ImFluentCol_DividerStrokeDefault ) );
}

void ImFluent::SetItemTooltip( const char * fmt, ... )
{
    if ( !ImGui::IsItemHovered( ImGuiHoveredFlags_ForTooltip ) )
        return;
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImFluentStackGuard g;
    g.PushStyleColor( ImGuiCol_PopupBg, ImFluent::GetColorU32( ImFluentCol_SolidBgQuarternary ) );
    g.PushStyleColor( ImGuiCol_Border, ImFluent::GetColorU32( ImFluentCol_SurfaceStrokeFlyout ) );
    g.PushStyleVar( ImGuiStyleVar_PopupRounding, FluentDpx( style.OverlayCornerRadius ) );
    g.PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( FluentDpx( style.SpacingLarge ), FluentDpx( style.SpacingMedium ) ) );
    va_list ap;
    va_start( ap, fmt );
    ImGui::SetTooltipV( fmt, ap );
    va_end( ap );
}

// [SECTION] Card & SettingsCard

bool ImFluent::BeginCard( const char * id, const ImVec2 & size, ImFluentCardStyle card_style )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImU32 bg              = ImFluent::GetColorU32( ( card_style == ImFluentCardStyle_Outlined ) ? ImFluentCol_CardBgSecondary : ImFluentCol_CardBgDefault );
    const ImU32 stroke          = ImFluent::GetColorU32( ImFluentCol_CardStrokeDefault );
    ImGui::PushStyleColor( ImGuiCol_ChildBg, bg );
    ImGui::PushStyleColor( ImGuiCol_Border, stroke );
    ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, FluentDpx( style.OverlayCornerRadius ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ChildBorderSize, 0.f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( FluentDpx( style.CardPadding ), FluentDpx( style.CardPadding ) ) );
    // INKEYS PATCH: 手绘边框的 child 没有原生 border，须显式保留 CardPadding，避免内容测量和绘制宽度失配。
    const bool open   = ImGui::BeginChild( id, size, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground );
    // Fill and border on whole pixels (ImGui's child border straddles half pixels and blurs at 150%), as WinUI draws them.
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    const ImRect rect = w->Rect();
    w->DrawList->PushClipRect( rect.Min, rect.Max, false );
    FillInsideBorder( w->DrawList, rect.Min, rect.Max, bg, FluentDpx( style.OverlayCornerRadius ) );
    StrokeRect( w->DrawList, rect.Min, rect.Max, stroke, FluentDpx( style.OverlayCornerRadius ) );
    w->DrawList->PopClipRect();
    return open;
}

void ImFluent::EndCard()
{
    ImGui::EndChild();
    ImGui::PopStyleVar( 3 );
    ImGui::PopStyleColor( 2 );
}

bool ImFluent::BeginSettingsCard( const char * id, const char * header, const char * description, const char * glyph )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    const bool card_open        = BeginCard( id, ImVec2( -FLT_MIN, 0 ), ImFluentCardStyle_Filled );
    if ( !card_open )
    {
        EndCard();
        return false;
    }
    ImGui::PushID( id );

    const float font_h    = ImGui::GetFontSize();
    const float gap_y     = description ? FluentDpx( style.SpacingXSmall ) : 0.f;
    const float text_h    = font_h + ( description ? font_h + gap_y : 0.f );
    const float control_h = FluentDpx( style.ControlHeight );
    const float row_h     = ImMax( text_h, control_h );
    const float glyph_w   = glyph ? FluentDpx( style.SpinButtonWidth ) : 0.f;
    const float glyph_gap = glyph ? FluentDpx( style.SpacingLarge ) : 0.f;
    const float trail_w   = FluentDpx( style.ControlMinWidth );

    const ImVec2 pos    = ImGui::GetCursorScreenPos();
    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImDrawList * dl     = ImGui::GetWindowDrawList();

    if ( glyph )
    {
        const ImVec2 gs = ImGui::CalcTextSize( glyph );
        dl->AddText( ImVec2( pos.x + ( glyph_w - gs.x ) * 0.5f, pos.y + ( row_h - gs.y ) * 0.5f ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), glyph );
    }
    const float text_x = pos.x + glyph_w + glyph_gap;
    const float text_y = pos.y + ( row_h - text_h ) * 0.5f;
    if ( header )
        dl->AddText( ImVec2( text_x, text_y ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), header );
    if ( description )
        dl->AddText( ImVec2( text_x, text_y + font_h + gap_y ), ImFluent::GetColorU32( ImFluentCol_TextSecondary ), description );

    const float trail   = ImMin( trail_w, avail_w );
    const float spacer  = ImMax( 0.f, avail_w - trail );
    ImGui::Dummy( ImVec2( spacer, row_h ) );
    ImGui::SameLine();
    ImGui::SetCursorScreenPos( ImVec2( pos.x + spacer, pos.y + ( row_h - control_h ) * 0.5f ) );
    ImGui::PushItemWidth( trail );
    return true;
}

void ImFluent::EndSettingsCard()
{
    ImGui::PopItemWidth();
    ImGui::PopID();
    EndCard();
}

// [SECTION] StackPanel & WrapPanel

void ImFluent::BeginStackPanelHorizontal( float spacing )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    if ( spacing < 0.f )
        spacing = FluentDpx( style.SpacingMedium );
    ImGui::BeginGroup();
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( spacing, ImGui::GetStyle().ItemSpacing.y ) );
}

void ImFluent::BeginStackPanelVertical( float spacing )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    if ( spacing < 0.f )
        spacing = FluentDpx( style.SpacingMedium );
    ImGui::BeginGroup();
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( ImGui::GetStyle().ItemSpacing.x, spacing ) );
}

void ImFluent::EndStackPanel()
{
    ImGui::PopStyleVar();
    ImGui::EndGroup();
}

void ImFluent::BeginWrapPanel( float h_spacing, float v_spacing )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    if ( h_spacing < 0.f )
        h_spacing = FluentDpx( style.SpacingMedium );
    if ( v_spacing < 0.f )
        v_spacing = FluentDpx( style.SpacingMedium );
    ImFluentWrapPanelState s;
    s.Avail    = ImGui::GetContentRegionAvail().x;
    s.HSpacing = h_spacing;
    s.VSpacing = v_spacing;
    s.CursorX  = 0.f;
    s.First    = true;
    g_Ctx.WrapPanelStack.push_back( s );
    ImGui::BeginGroup();
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( h_spacing, v_spacing ) );
}

bool ImFluent::WrapPanelNextItem( float item_width )
{
    if ( g_Ctx.WrapPanelStack.empty() )
        return false;
    ImFluentWrapPanelState & s = g_Ctx.WrapPanelStack.back();
    if ( s.First )
    {
        s.First   = false;
        s.CursorX = item_width;
        return true;
    }
    const float needed = item_width + s.HSpacing;
    if ( s.CursorX + needed > s.Avail )
    {
        s.CursorX = item_width;
        return true;
    }
    s.CursorX += needed;
    ImGui::SameLine();
    return true;
}

void ImFluent::EndWrapPanel()
{
    if ( g_Ctx.WrapPanelStack.empty() )
    {
        IM_ASSERT( 0 && "EndWrapPanel without matching Begin" );
        return;
    }
    g_Ctx.WrapPanelStack.pop_back();
    ImGui::PopStyleVar();
    ImGui::EndGroup();
}

// [SECTION] Expander

static bool ExpanderHeader( const char * label, bool * open, ImFluentExpandDirection dir )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );
    const float row_h           = ImFluent::FluentDpx( style.ControlHeight );
    const float W               = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + W, pos.y + row_h ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id ) )
        return open && *open;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );
    if ( pressed && open )
        *open = !*open;
    const bool isOpen = open && *open;
    ImFluent::ReportItemInfo( id, label, ImFluent::OpenedFlags( isOpen ) );

    const float r   = ImFluent::FluentDpx( style.ControlCornerRadius );
    ImU32 fill      = ImFluent::ResolveControlFillState( false, held, hovered );
    fill            = ImFluent::AnimateColorU32( id, fill );
    ImDrawList * dl = w->DrawList;
    ImFluent::FillInsideBorder( dl, bb.Min, bb.Max, fill, r );
    ImFluent::RenderElevationBorder( dl, bb, r, ImFluent::GetColorU32( ImFluentCol_ControlStrokeDefault ), ImFluent::GetColorU32( ImFluentCol_ElevationControlTop ), ImFluent::GetColorU32( ImFluentCol_ElevationControlBottom ), 1.f );

    const float closed_phase = ( dir == ImFluentExpandDirection_Up ) ? 1.f : 0.f;
    const float open_phase   = ( dir == ImFluentExpandDirection_Up ) ? 0.f : 1.f;
    const float t_anim       = ImFluent::AnimateFloat( id, isOpen ? open_phase : closed_phase, 0.20f, 1 );
    const float cx           = bb.Max.x - ImFluent::FluentDpx( style.CheckboxSize );
    const float cy           = ( bb.Min.y + bb.Max.y ) * 0.5f;
    const float L            = ImFluent::FluentDpx( style.ChevronGlyphSize + 1.f );
    ImFluent::RenderChevronLerp( dl, cx, cy, L, t_anim, ImFluent::GetColorU32( ImFluentCol_TextPrimary ), ImFluent::FluentDpx( style.StrokeMedium ), true );

    // INKEYS PATCH: 极窄正文仍为展开箭头保留独立槽位，完整标题可悬停查看。
    const float text_left = bb.Min.x + ImFluent::FluentDpx( style.SpacingXLarge );
    const float text_right = ImMax( text_left, cx - L - ImFluent::FluentDpx( 12.f ) );
    const ImVec4 label_clip( text_left, bb.Min.y, text_right, bb.Max.y );
    dl->AddText( ImGui::GetFont(), ImGui::GetFontSize(),
        ImVec2( text_left, cy - ImGui::GetFontSize() * 0.5f ),
        ImFluent::GetColorU32( ImFluentCol_TextPrimary ), label, ImGui::FindRenderedTextEnd( label ), 0.f, &label_clip );
    if ( hovered && ImGui::CalcTextSize( label, NULL, true ).x > text_right - text_left )
        ImFluent::SetItemTooltip( "%.*s", ( int )( ImGui::FindRenderedTextEnd( label ) - label ), label );
    if ( ImFluent::IsItemFocused( id ) )
        ImFluent::RenderNavFocusRing( bb, r );
    return isOpen;
}

static bool ExpanderPushBody( const char * label )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    const float r               = ImFluent::FluentDpx( style.ControlCornerRadius );
    const float W               = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor( ImGuiCol_ChildBg, ImFluent::GetColorU32( ImFluentCol_CardBgDefault ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, r );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( ImFluent::FluentDpx( style.SpacingXLarge ), ImFluent::FluentDpx( style.SpacingLarge ) ) );
    const bool open = ImGui::BeginChild( label, ImVec2( W, 0.f ), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened );
    if ( !open )
    {
        ImGui::EndChild();
        ImGui::PopStyleVar( 2 );
        ImGui::PopStyleColor();
        return false;
    }
    return true;
}

static void ExpanderPopBody()
{
    ImGui::EndChild();
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor();
}

bool ImFluent::BeginExpander( const char * label, bool * open, ImFluentExpandDirection direction, bool * out_just_expanded, bool * out_just_collapsed )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;

    const ImGuiID id       = w->GetID( label );
    ImGuiStorage * st      = ImGui::GetStateStorage();
    const ImGuiID prev_key = id ^ 0xEC0CACEDu;
    const bool prev_open   = st->GetBool( prev_key, false );

    auto report = [&]( bool now_open ) {
        if ( out_just_expanded )
            *out_just_expanded = ( !prev_open && now_open );
        if ( out_just_collapsed )
            *out_just_collapsed = ( prev_open && !now_open );
        st->SetBool( prev_key, now_open );
    };

    if ( direction == ImFluentExpandDirection_Down )
    {
        const bool isOpen = ExpanderHeader( label, open, direction );
        report( open && *open );
        if ( !isOpen )
            return false;
        ImFluentExpanderState s;
        ImStrncpy( s.Label, label, sizeof( s.Label ) );
        s.Open       = open;
        s.Direction  = direction;
        s.BodyActive = ExpanderPushBody( label );
        // INKEYS PATCH: 被裁掉的 body 已自行结束，不能留下未配对的 scope。
        if ( s.BodyActive ) g_Ctx.ExpanderStack.push_back( s );
        return s.BodyActive;
    }
    else
    {
        const bool cur_open = open && *open;
        if ( !cur_open )
        {
            ExpanderHeader( label, open, direction );
            report( open && *open );
            return false;
        }
        report( cur_open );
        ImFluentExpanderState s;
        ImStrncpy( s.Label, label, sizeof( s.Label ) );
        s.Open       = open;
        s.Direction  = direction;
        s.BodyActive = ExpanderPushBody( label );
        // INKEYS PATCH: 被裁掉的 body 已自行结束，不能留下未配对的 scope。
        if ( s.BodyActive ) g_Ctx.ExpanderStack.push_back( s );
        return s.BodyActive;
    }
}

void ImFluent::EndExpander()
{
    if ( g_Ctx.ExpanderStack.empty() )
        return;
    const ImFluentExpanderState s = g_Ctx.ExpanderStack.back();
    g_Ctx.ExpanderStack.pop_back();
    if ( s.BodyActive )
        ExpanderPopBody();
    if ( s.Direction == ImFluentExpandDirection_Up )
        ExpanderHeader( s.Label, s.Open, s.Direction );
}

// [SECTION] ScrollView

bool ImFluent::BeginScrollView( const char * id, const ImVec2 & size )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImGui::PushStyleColor( ImGuiCol_ChildBg, ImFluent::GetColorU32( ImFluentCol_SubtleFillTransparent ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, FluentDpx( style.ControlCornerRadius ) );
    if ( !ImGui::BeginChild( id, size, ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_HorizontalScrollbar ) )
    {
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        return false;
    }
    return true;
}

void ImFluent::EndScrollView()
{
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// [SECTION] TabView

// Tab and tab bar height (32) for the calls that lay out tabs; the tab content keeps the regular frame padding.
static void PushTabFramePadding()
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( ImFluent::FluentDpx( style.SpacingLarge ), ImMax( 0.f, ( ImFluent::FluentDpx( style.ControlHeight ) - ImGui::GetFontSize() ) * 0.5f ) ) );
}

bool ImFluent::BeginTabView( const char * id )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImGui::PushStyleColor( ImGuiCol_Tab, ImFluent::GetColorU32( ImFluentCol_SubtleFillTransparent ) );
    ImGui::PushStyleColor( ImGuiCol_TabHovered, ImFluent::GetColorU32( ImFluentCol_SubtleFillSecondary ) );
    ImGui::PushStyleColor( ImGuiCol_TabSelected, ImFluent::GetColorU32( ImFluentCol_LayerFillAlt ) );
    ImGui::PushStyleColor( ImGuiCol_TabDimmed, ImFluent::GetColorU32( ImFluentCol_SubtleFillTransparent ) );
    ImGui::PushStyleColor( ImGuiCol_TabDimmedSelected, ImFluent::GetColorU32( ImFluentCol_LayerFillAlt ) );
    // WinUI TabView: 32 high tabs with 8 rounded top corners, no overline, close buttons on every tab.
    ImGui::PushStyleVar( ImGuiStyleVar_TabRounding, FluentDpx( style.OverlayCornerRadius ) );
    ImGui::PushStyleVar( ImGuiStyleVar_TabBarOverlineSize, 0.f );
    PushTabFramePadding();
    const bool open = ImGui::BeginTabBar( id, ImGuiTabBarFlags_None );
    ImGui::PopStyleVar();
    if ( open )
    {
        BeginTabGroup( ImGui::GetCurrentContext()->CurrentTabBar->ID, ImFluentTabGroupFlags_Horizontal | ImFluentTabGroupFlags_Wrap | ImFluentTabGroupFlags_HomeEnd | ImFluentTabGroupFlags_EnterOnLastFocused );
        return true;
    }
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor( 5 );
    return false;
}

static const ImGuiID TABVIEW_FOCUS_KEY_SALT = 0x7AB5F0C5u;
static const ImGuiID TABVIEW_RING_SALT      = 0x7AB5F0C6u;

static ImGuiID TabCloseButtonId( ImGuiID tab_id )
{
    return ImHashStr( "#CLOSE", 0, tab_id ); // ImGui's id for a tab's close button
}

// The current TabView's focus ring, NULL for a plain ImGui tab bar.
static ImFluentTabGroupLayout * TabViewRing()
{
    const ImGuiTabBar * tab_bar = GImGui->CurrentTabBar;
    if ( !tab_bar || g_Ctx.TabGroupStack.empty() || g_Ctx.TabGroupStack.back().Id != tab_bar->ID )
        return NULL;
    return g_Ctx.TabGroupLayouts.GetOrAddByKey( tab_bar->ID ^ TABVIEW_RING_SALT );
}

// WinUI TabView::MoveFocus: Left/Right go tab 1, its close button, tab 2, ..., the Add button, and wrap.
static void TabViewRingItem( ImGuiID id )
{
    ImFluentTabGroupLayout * ring = TabViewRing();
    if ( !ring )
        return;
    ring->NextItems.push_back( id );
    const int n = ring->Items.Size;
    const int i = ring->Items.find_index( id );
    if ( i < 0 || n < 2 )
        return;
    const int step = ImFluent::ConsumeNavArrow( id, ImGuiDir_Right ) ? 1 : ImFluent::ConsumeNavArrow( id, ImGuiDir_Left ) ? -1 : 0;
    if ( step == 0 )
        return;
    ImGui::SetFocusID( ring->Items[ ( i + step + n ) % n ], GImGui->CurrentWindow );
    ImGui::SetNavCursorVisible( true );
}

// WinUI TabView::RequestCloseTab: if the tab or its close button has focus, focus goes to the next tab on the right, else on the left, else to Add.
static void TabViewCloseTab( bool * p_open, ImGuiID tab_id )
{
    *p_open                             = false;
    ImGuiContext & g                    = *GImGui;
    const ImFluentTabGroupLayout * ring = TabViewRing();
    const int i                         = ring ? ring->Items.find_index( tab_id ) : -1;
    if ( i < 0 || ( g.NavId != tab_id && g.NavId != TabCloseButtonId( tab_id ) ) )
        return;
    ImGuiID next = 0, prev = 0, add = 0;
    for ( int k = 0; k < ring->Items.Size; ++k )
    {
        const ImGuiTabItem * tab = ImGui::TabBarFindTabByID( g.CurrentTabBar, ring->Items[ k ] );
        if ( !tab || k == i )
            continue; // Close buttons are not tabs
        if ( tab->Flags & ImGuiTabItemFlags_Button )
            add = tab->ID;
        else if ( k > i && !next )
            next = tab->ID;
        else if ( k < i )
            prev = tab->ID;
    }
    const ImGuiID target = next ? next : prev ? prev : add;
    if ( !target )
        return;
    const bool cursor_visible = g.NavCursorVisible;
    ImGui::SetFocusID( target, g.CurrentWindow );
    g.NavCursorVisible = cursor_visible;
}

bool ImFluent::BeginTabItem( const char * label, bool * p_open, ImGuiTabItemFlags flags )
{
    // Ctrl+F4 asks to close the selected tab while focus is inside the tab view (WinUI TabView accelerator).
    ImGuiContext & g = *GImGui;
    if ( ImGuiTabBar * tab_bar = g.CurrentTabBar )
        if ( p_open && *p_open && tab_bar->SelectedTabId == ImGui::GetID( label ) && ImGui::GetStateStorage()->GetBool( tab_bar->ID ^ TABVIEW_FOCUS_KEY_SALT, false ) && IsActionPressed( g_Ctx.Actions.CloseTab ) )
            TabViewCloseTab( p_open, ImGui::GetID( label ) );
    if ( p_open && !*p_open )
        return ImGui::BeginTabItem( label, p_open, flags );
    // WinUI TabView: one tab stop on the last focused (or selected) tab; Left/Right wrap, Home/End (focus only), Up/Down stay in the strip.
    const ImGuiID tab_id   = ImGui::GetID( label );
    ImGuiTabBar * tab_bar  = g.CurrentTabBar;
    const bool is_selected = tab_bar && tab_bar->SelectedTabId == tab_id;
    // WinUI TabView look: equal-width tabs (100 to 240), secondary text when unselected, dividers between unselected tabs.
    // In an auto-sizing window each tab fits its label: equal widths would grow the window every frame.
    if ( tab_bar && tab_bar->Tabs.Size > 0 )
    {
        const ImGuiWindow * win = g.CurrentWindow;
        const bool auto_width   = ( win->Flags & ImGuiWindowFlags_AlwaysAutoResize ) || ( win->ChildFlags & ImGuiChildFlags_AutoResizeX );
        const float pad_x       = FluentDpx( ImFluent::GetStyle().SpacingLarge );
        const float fit_w       = ImGui::CalcTextSize( label, NULL, true ).x + pad_x * 2.f + ( p_open ? ImGui::GetFontSize() + pad_x * 0.5f : 0.f );
        const float w           = auto_width ? fit_w : tab_bar->BarRect.GetWidth() / ( float )tab_bar->Tabs.Size;
        ImGui::SetNextItemWidth( ImClamp( w, FluentDpx( 100.f ), FluentDpx( 240.f ) ) );
    }
    if ( !is_selected )
        ImGui::PushStyleColor( ImGuiCol_Text, ImFluent::GetColorU32( ImFluentCol_TextSecondary ) );
    ImGui::PushItemFlag( ImGuiItemFlags_NoTabStop, TabGroupItemFlags( tab_id, is_selected ) != ImGuiItemFlags_None );
    PushTabFramePadding();
    // ImGui's close button takes the tab's item flags, so on the selected tab it would be a Tab stop (WinUI: never).
    // ImFluent draws it instead; ImGui's hidden unsaved marker keeps the label clear of it.
    ImGuiTabItemFlags tab_flags = flags;
    const bool hide_marker      = p_open && !( flags & ImGuiTabItemFlags_UnsavedDocument );
    if ( p_open )
        tab_flags |= ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_UnsavedDocument;
    if ( hide_marker )
        ImGui::PushStyleColor( ImGuiCol_UnsavedMarker, IM_COL32( 0, 0, 0, 0 ) );
    if ( p_open )
        ImGui::SetNextItemAllowOverlap(); // The close button over the tab takes the hover
    const bool open = ImGui::BeginTabItem( label, p_open, tab_flags );
    if ( hide_marker )
        ImGui::PopStyleColor();
    if ( p_open && *p_open && g.LastItemData.ID == tab_id )
    {
        const ImRect tab_bb            = g.LastItemData.Rect;
        const ImVec2 pad               = ImGui::GetStyle().FramePadding;
        const ImGuiLastItemData backup = g.LastItemData;
        const ImGuiID close_id         = TabCloseButtonId( tab_id );
        ImGui::PushItemFlag( ImGuiItemFlags_NoTabStop, true );
        if ( ImGui::CloseButton( close_id, ImVec2( tab_bb.Max.x - pad.x - ImGui::GetFontSize(), tab_bb.Min.y + pad.y ) ) )
            TabViewCloseTab( p_open, tab_id );
        ImGui::PopItemFlag();
        ReportItemInfo( close_id, "Close tab" );
        TabViewRingItem( close_id );
        g.LastItemData = backup;
    }
    ImGui::PopStyleVar();
    ImGui::PopItemFlag();
    if ( !is_selected )
        ImGui::PopStyleColor();
    if ( tab_bar && !is_selected )
    {
        const ImGuiTabItem * tab = ImGui::TabBarFindTabByID( tab_bar, tab_id );
        const int index          = tab ? tab_bar->Tabs.index_from_ptr( tab ) : -1;
        if ( index >= 0 && index + 1 < tab_bar->Tabs.Size && tab_bar->Tabs[ index + 1 ].ID != tab_bar->SelectedTabId )
        {
            const ImRect r = g.LastItemData.Rect;
            const float x  = r.Max.x + ImGui::GetStyle().ItemInnerSpacing.x * 0.5f;
            const float h  = FluentDpx( 16.f );
            const float cy = ( r.Min.y + r.Max.y ) * 0.5f;
            ImGui::GetWindowDrawList()->AddLine( ImVec2( x, cy - h * 0.5f ), ImVec2( x, cy + h * 0.5f ), ImFluent::GetColorU32( ImFluentCol_DividerStrokeDefault ) );
        }
    }
    TabViewRingItem( tab_id );
    TabGroupItemNav( tab_id );
    if ( p_open && !( flags & ImGuiTabItemFlags_NoCloseWithMiddleMouseButton ) && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenBlockedByPopup ) && ImGui::IsMouseClicked( ImGuiMouseButton_Middle ) )
        TabViewCloseTab( p_open, tab_id );
    return open;
}
void ImFluent::EndTabItem()
{
    ImGui::EndTabItem();
}

bool ImFluent::TabAddButton()
{
    PushTabFramePadding();
    const bool pressed = ImGui::TabItemButton( ImFluentIcon_Add, ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip );
    ImGui::PopStyleVar();
    ReportItemInfo( ImGui::GetItemID(), "Add tab" );
    TabViewRingItem( ImGui::GetItemID() );
    return pressed;
}

void ImFluent::EndTabView()
{
    // WinUI TabView accelerators while focus is inside the tab view (NextTab, PreviousTab, wrapping).
    // Applications turn off ImGui's Ctrl+Tab window switcher (ConfigNavWindowingKeyNext/Prev = 0) so the keys reach the tab view.
    ImGuiContext & g = *GImGui;
    if ( ImGuiTabBar * tab_bar = g.CurrentTabBar )
    {
        bool focus_inside = false;
        if ( g.NavId != 0 && g.NavWindow && ( g.NavWindow == g.CurrentWindow || g.NavWindow->RootWindowForNav == g.CurrentWindow->RootWindowForNav ) )
        {
            const ImRect view( tab_bar->BarRect.Min, ImVec2( tab_bar->BarRect.Max.x, g.CurrentWindow->DC.CursorPos.y ) );
            const ImRect nav = ImGui::WindowRectRelToAbs( g.NavWindow, g.NavWindow->NavRectRel[ g.NavLayer ] );
            focus_inside     = view.Contains( nav.GetCenter() );
        }
        ImGui::GetStateStorage()->SetBool( tab_bar->ID ^ TABVIEW_FOCUS_KEY_SALT, focus_inside );
        const int n = tab_bar->Tabs.Size;
        if ( focus_inside && n > 1 )
        {
            int dir = 0;
            if ( IsActionPressed( g_Ctx.Actions.NextTab ) )
                dir = +1;
            else if ( IsActionPressed( g_Ctx.Actions.PreviousTab ) )
                dir = -1;
            if ( dir != 0 )
            {
                int cur = 0;
                for ( int i = 0; i < n; ++i )
                    if ( tab_bar->Tabs[ i ].ID == tab_bar->SelectedTabId )
                        cur = i;
                for ( int step = 1; step < n; ++step )
                {
                    const ImGuiTabItem & tab = tab_bar->Tabs[ ( ( cur + dir * step ) % n + n ) % n ];
                    if ( tab.Flags & ( ImGuiTabItemFlags_Button | ImGuiTabItemFlags_Leading | ImGuiTabItemFlags_Trailing ) )
                        continue; // The "+" button is not a tab
                    tab_bar->NextSelectedTabId = tab.ID;
                    break;
                }
            }
        }
    }
    if ( ImFluentTabGroupLayout * ring = TabViewRing() )
    {
        // The tab bar's visual order (the Add button is Trailing), each tab followed by its close button
        ring->Items.resize( 0 );
        for ( const ImGuiTabItem & tab : g.CurrentTabBar->Tabs )
        {
            if ( ring->NextItems.contains( tab.ID ) )
                ring->Items.push_back( tab.ID );
            if ( ring->NextItems.contains( TabCloseButtonId( tab.ID ) ) )
                ring->Items.push_back( TabCloseButtonId( tab.ID ) );
        }
        ring->NextItems.resize( 0 );
    }
    EndTabGroup();
    ImGui::EndTabBar();
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor( 5 );
}

// [SECTION] SelectorBar

bool ImFluent::BeginSelectorBar( const char * id )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImGui::PushID( id );
    ImGui::BeginGroup();
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( FluentDpx( style.SpacingXSmall ), 0.f ) );
    // Keys as WinUI SelectorBar.
    BeginTabGroup( ImGui::GetID( "##selector-group" ), ImFluentTabGroupFlags_Horizontal | ImFluentTabGroupFlags_HomeEnd | ImFluentTabGroupFlags_SelectionFollowsFocus );
    return true;
}

bool ImFluent::SelectorBarItem( const char * label, bool selected, const char * glyph )
{
    glyph           = ConsumePendingGlyph( glyph );
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const char * txt;
    const char * txt_end;
    if ( glyph )
        ImFormatStringToTempBuffer( &txt, &txt_end, "%s  %s", glyph, label );
    else
        ImFormatStringToTempBuffer( &txt, &txt_end, "%s", label );
    const ImGuiID id  = w->GetID( label );
    const float pad_x = FluentDpx( style.SpacingXLarge );
    const float pad_y = FluentDpx( style.SpacingMedium - 2.f );
    const ImVec2 ts   = ImGui::CalcTextSize( txt, txt_end, true );
    const ImVec2 pos  = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + ts.x + pad_x * 2.f, pos.y + ts.y + pad_y * 2.f ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id, NULL, TabGroupItemFlags( id, selected ) ) )
    {
        ImGui::SameLine();
        return false;
    }
    bool hovered = false, held = false;
    const bool clicked = ImGui::ButtonBehavior( bb, id, &hovered, &held );
    const bool pressed = TabGroupItemNav( id ) || clicked;
    ReportItemInfo( id, label, CheckedFlags( selected ) );
    ImDrawList * dl    = w->DrawList;
    ImU32 fillTarget;
    fillTarget           = ResolveSubtleFillState( selected, held, hovered );
    const ImU32 fillAnim = AnimateColorU32( id, fillTarget );
    dl->AddRectFilled( bb.Min, bb.Max, fillAnim, FluentDpx( style.ControlCornerRadius ) );
    dl->AddText( ImGui::GetFont(), ImGui::GetFontSize(), ImVec2( bb.Min.x + pad_x, bb.Min.y + pad_y ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), txt, txt_end );
    if ( selected )
    {
        const float bar_w = FluentDpx( style.SpacingXLarge );
        const float bx    = ( bb.Min.x + bb.Max.x ) * 0.5f;
        dl->AddRectFilled( ImVec2( bx - bar_w * 0.5f, bb.Max.y - FluentDpx( style.SpacingXSmall ) ), ImVec2( bx + bar_w * 0.5f, bb.Max.y ), ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), FluentDpx( style.StrokeThin ) );
    }
    RenderNavFocusRing( id, bb, FluentDpx( style.ControlCornerRadius ) );
    ImGui::SameLine();
    return pressed;
}

void ImFluent::EndSelectorBar()
{
    EndTabGroup();
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    ImGui::PopID();

    ImGui::NewLine();
}

// [SECTION] NavigationView

void ImFluent::SetNextNavPaneToggleButtonVisible( bool visible )
{
    g_Ctx.NavView.NextToggleVisible    = visible;
    g_Ctx.NavView.NextToggleVisibleSet = true;
}

bool ImFluent::IsNavPaneOpening()
{
    return g_Ctx.NavView.PrevMode != ImFluentNavViewMode_LeftOpen && g_Ctx.NavView.Mode == ImFluentNavViewMode_LeftOpen;
}

bool ImFluent::IsNavPaneClosing()
{
    return g_Ctx.NavView.PrevMode == ImFluentNavViewMode_LeftOpen && g_Ctx.NavView.Mode != ImFluentNavViewMode_LeftOpen;
}

// Left modes (WinUI): menu items scroll in their own region, footer items stay pinned under it (footer height from last frame).
static const ImGuiID NAVVIEW_KEY_FOOTER_H = 0xF0073Eu;

static void BeginNavMenuRegion()
{
    const float footer_h = ImGui::GetStateStorage()->GetFloat( NAVVIEW_KEY_FOOTER_H, 0.f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.f, 0.f ) );
    ImGui::BeginChild( "##nav-menu", ImVec2( 0.f, footer_h > 0.f ? -footer_h : 0.f ), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar );
    ImGui::PopStyleVar();
    g_Ctx.NavView.MenuRegionOpen = true;
    g_Ctx.NavView.FooterStartY   = -1.f;
}

static void EndNavMenuRegion()
{
    if ( !g_Ctx.NavView.MenuRegionOpen )
        return;
    ImGui::EndChild();
    g_Ctx.NavView.MenuRegionOpen = false;
    g_Ctx.NavView.FooterStartY   = ImGui::GetCursorPosY();
}

bool ImFluent::BeginNavigationView( const char * id, ImFluentNavViewMode * mode_io )
{
    const ImFluentStyle & style = ImFluent::GetStyle();

    const ImGuiID nav_id          = ImGui::GetID( id );
    ImGuiStorage * st             = ImGui::GetStateStorage();
    const ImGuiID mode_key        = nav_id ^ 0xA0DE5u;
    const ImFluentNavViewMode mode = mode_io ? *mode_io : ImFluentNavViewMode_LeftCompact;
    const ImFluentNavViewMode prev_mode = ( ImFluentNavViewMode )st->GetInt( mode_key, ( int )ImFluentNavViewMode_LeftCompact );
    st->SetInt( mode_key, ( int )mode );

    // WinUI NavigationView: menu items are one tab stop with Up/Down and Home/End; Settings is its own stop.
    // Tab enters on the selected item (or the collapsed item that hides it), else on the first item, Shift+Tab on the last.
    BeginTabGroup( nav_id ^ 0x7AB1E5u, ImFluentTabGroupFlags_HomeEnd | ImFluentTabGroupFlags_EnterAtEnds );
    g_Ctx.NavView.InTabGroup = true;

    g_Ctx.NavView.PrevMode = prev_mode;
    g_Ctx.NavView.Mode     = mode;

    const bool toggle_visible          = g_Ctx.NavView.NextToggleVisibleSet ? g_Ctx.NavView.NextToggleVisible : true;
    g_Ctx.NavView.NextToggleVisibleSet = false;

    if ( g_Ctx.NavView.Mode == ImFluentNavViewMode_Top )
    {
        const float h              = FluentDpx( style.NavItemHeight );
        g_Ctx.NavView.CurrentWidth = ImGui::GetContentRegionAvail().x;

        ImGui::PushID( id );
        ImGui::BeginGroup();
        // WinUI: the pane has no fill of its own; the window's base color shows through.
        ImGui::PushStyleColor( ImGuiCol_ChildBg, IM_COL32( 0, 0, 0, 0 ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, 0.f );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( FluentDpx( style.SpacingSmall ), FluentDpx( style.SpacingSmall ) ) );
        const bool pane_open = ImGui::BeginChild( "##nav-pane", ImVec2( 0, h + FluentDpx( style.SpacingSmall ) * 2.f ), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar );
        return pane_open ? true : true;
    }

    const float target_w       = ( g_Ctx.NavView.Mode == ImFluentNavViewMode_LeftOpen ) ? FluentDpx( style.NavPaneOpenWidth ) : FluentDpx( style.NavPaneCompactWidth );
    g_Ctx.NavView.CurrentWidth = AnimateFloat( nav_id, target_w, 0.20f );

    ImGui::PushID( id );
    ImGui::BeginGroup();
    ImGui::PushStyleColor( ImGuiCol_ChildBg, IM_COL32( 0, 0, 0, 0 ) ); // As in Top mode: no pane fill
    ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, 0.f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( FluentDpx( style.SpacingSmall ), FluentDpx( style.SpacingSmall ) ) );
    const bool pane_open = ImGui::BeginChild( "##nav-pane", ImVec2( g_Ctx.NavView.CurrentWidth, 0.f ), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar );
    if ( !pane_open )
        return true;

    if ( !toggle_visible )
    {
        BeginNavMenuRegion();
        return true;
    }

    const float row_h = FluentDpx( style.NavItemHeight );
    // InvisibleButton is NoNav by default: EnableNav keeps the pane toggle reachable with Tab.
    if ( ImGui::InvisibleButton( "##nav-toggle", ImVec2( g_Ctx.NavView.CurrentWidth - FluentDpx( style.SpacingMedium ), row_h ), ImGuiButtonFlags_EnableNav ) )
    {
        if ( mode_io )
            *mode_io = ( g_Ctx.NavView.Mode == ImFluentNavViewMode_LeftOpen ) ? ImFluentNavViewMode_LeftCompact : ImFluentNavViewMode_LeftOpen;
    }
    {
        const ImGuiLastItemData & item = ImGui::GetCurrentContext()->LastItemData;
        const ImRect bb                = item.Rect;
        ImDrawList * dl                = ImGui::GetWindowDrawList();
        const bool hov                 = ImGui::IsItemHovered();
        ReportItemInfo( item.ID, "Toggle pane" );
        if ( hov )
            dl->AddRectFilled( bb.Min, bb.Max, ImFluent::GetColorU32( ImFluentCol_SubtleFillSecondary ), FluentDpx( style.ControlCornerRadius ) );
        RenderNavFocusRing( item.ID, bb, FluentDpx( style.ControlCornerRadius ) );

        const float cx = bb.Min.x + FluentDpx( style.SpacingXXLarge );
        const float cy = ( bb.Min.y + bb.Max.y ) * 0.5f;
        const float lw = FluentDpx( style.StandardIconSize - 2.f );
        for ( int i = -1; i <= 1; ++i )
            dl->AddLine( ImVec2( cx - lw * 0.5f, cy + ( float )i * FluentDpx( style.SpacingSmall + 1.f ) ), ImVec2( cx + lw * 0.5f, cy + ( float )i * FluentDpx( style.SpacingSmall + 1.f ) ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), FluentDpx( style.StrokeMedium ) );
    }
    BeginNavMenuRegion();
    return true;
}

// NavigationView item icons are 16 dp (WinUI), smaller than the label, and centered on the row.
static void RenderNavGlyph( ImDrawList * dl, float x, const ImRect & row, const char * glyph )
{
    const float size = ImFluent::FluentDpx( 16.f );
    dl->AddText( ImGui::GetFont(), size, ImVec2( x, ( row.Min.y + row.Max.y - size ) * 0.5f ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), glyph );
}

// The hover/selection fill of a pane item row: 4 dp in from each side of the pane, as WinUI's item presenter.
static ImRect NavItemPillRect( const ImRect & row )
{
    const float m = ImFluent::FluentDpx( ImFluent::GetStyle().SpacingSmall );
    return ImRect( row.Min.x + m, row.Min.y, row.Max.x - m, row.Max.y );
}

// NavigationView menu items form one tab-stop group; footer items and items outside a NavigationView are ordinary stops.
static bool InNavMenuGroup()
{
    return g_Ctx.NavView.InTabGroup && !g_Ctx.NavView.FooterItem;
}

static const ImGuiID NAVITEM_SELECTED_CHILD_SALT = 0x5E1C4D17u;

static ImGuiItemFlags NavMenuItemFlags( ImGuiID id, bool selected )
{
    if ( selected )
        for ( ImGuiID parent : g_Ctx.NavItemParentStack )
            ImGui::GetStateStorage()->SetInt( parent ^ NAVITEM_SELECTED_CHILD_SALT, ( int )id ); // Found again while collapsed
    return InNavMenuGroup() ? ImFluent::TabGroupItemFlags( id, selected ) : ImGuiItemFlags_None;
}

static void NavMenuItemNav( ImGuiID id )
{
    if ( InNavMenuGroup() )
        ImFluent::TabGroupItemNav( id );
}

// After a BeginNavItem row, clipped or not: opens its children when expanded.
static bool NavItemBeginChildren( ImGuiID id, bool expanded )
{
    ImGuiStorage * storage  = ImGui::GetStateStorage();
    const ImGuiID child_key = id ^ NAVITEM_SELECTED_CHILD_SALT;
    if ( !expanded )
    {
        if ( InNavMenuGroup() )
            ImFluent::TabGroupHiddenSelection( id, ( ImGuiID )storage->GetInt( child_key, 0 ) );
        return false;
    }
    storage->SetInt( child_key, 0 );
    // WinUI: child items are indented so that their label (no icon column) lines up with the parent's label.
    const ImFluentStyle & style = ImFluent::GetStyle();
    const float indent_amt      = ImFluent::FluentDpx( style.NavPaneCompactWidth - style.SpacingXLarge );
    ImGui::Indent( indent_amt );
    g_Ctx.NavItemIndentStack.push_back( indent_amt );
    g_Ctx.NavItemParentStack.push_back( id );
    return true;
}

bool ImFluent::NavItem( const char * label, bool selected, const char * glyph )
{
    glyph           = ConsumePendingGlyph( glyph );
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );

    if ( g_Ctx.NavView.Mode == ImFluentNavViewMode_Top )
    {
        const float h      = FluentDpx( style.NavItemHeight );
        const float pad_x  = FluentDpx( style.SpacingLarge );
        const ImVec2 ts    = ImGui::CalcTextSize( label );
        const float gw     = glyph ? ImGui::CalcTextSize( glyph ).x + FluentDpx( style.SpacingMedium ) : 0.f;
        const float w_item = pad_x * 2.f + gw + ts.x;
        const ImVec2 pos   = w->DC.CursorPos;
        const ImRect bb( pos, ImVec2( pos.x + w_item, pos.y + h ) );
        ImGui::ItemSize( bb );
        if ( !ImGui::ItemAdd( bb, id, NULL, NavMenuItemFlags( id, selected ) ) )
        {
            ImGui::SameLine();
            return false;
        }
        bool hov = false, held = false;
        const bool pressed = ImGui::ButtonBehavior( bb, id, &hov, &held );
        NavMenuItemNav( id );
        ReportItemInfo( id, label, CheckedFlags( selected ) );
        const float r      = FluentDpx( style.ControlCornerRadius );
        const ImU32 fill   = AnimateColorU32( id, ResolveSubtleFillState( selected, held, hov ) );
        ImDrawList * dl    = w->DrawList;
        dl->AddRectFilled( bb.Min, bb.Max, fill, r );
        const float cy = ( bb.Min.y + bb.Max.y - ImGui::GetFontSize() ) * 0.5f;
        float text_x   = bb.Min.x + pad_x;
        if ( glyph )
        {
            RenderNavGlyph( dl, text_x, bb, glyph );
            text_x += gw;
        }
        dl->AddText( ImVec2( text_x, cy ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), label );
        if ( selected )
        {
            const float bw = bb.GetWidth() * 0.5f;
            const float bx = ( bb.Min.x + bb.Max.x ) * 0.5f;
            dl->AddRectFilled( ImVec2( bx - bw * 0.5f, bb.Max.y - FluentDpx( style.SelectionIndicatorThickness ) ), ImVec2( bx + bw * 0.5f, bb.Max.y ), ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), FluentDpx( style.SpacingXSmall ) );
        }
        RenderNavFocusRing( id, bb, r );
        ImGui::SameLine();
        return pressed;
    }

    return NavItemEx( label, label, selected, glyph,
        g_Ctx.NavView.Mode != ImFluentNavViewMode_LeftOpen );
}

// INKEYS PATCH: 独立容器与内置 NavigationView 共用条目，保持 24-DIP 图标轴。
bool ImFluent::NavItemEx( const char * id_label, const char * label,
    bool selected, const char * glyph, bool compact )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems ) return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id = w->GetID( id_label );
    const float row_h = FluentDpx( style.NavItemHeight );
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + ImGui::GetContentRegionAvail().x, pos.y + row_h ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id, NULL, NavMenuItemFlags( id, selected ) ) ) return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );
    NavMenuItemNav( id );
    ReportItemInfo( id, label, CheckedFlags( selected ) );
    const float radius = FluentDpx( style.ControlCornerRadius );
    ImDrawList * dl = w->DrawList;
    dl->AddRectFilled( bb.Min, bb.Max,
        AnimateColorU32( id, ResolveSubtleFillState( selected, held, hovered ) ), radius );
    if ( selected )
    {
        const float middle = ( bb.Min.y + bb.Max.y ) * 0.5f;
        dl->AddRectFilled( ImVec2( bb.Min.x, middle - FluentDpx( 8.f ) ),
            ImVec2( bb.Min.x + FluentDpx( 3.f ), middle + FluentDpx( 8.f ) ),
            ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), FluentDpx( 1.5f ) );
    }
    const float icon_left = bb.Min.x + FluentDpx( 8.f );
    DrawIcon( glyph, ImVec2( icon_left, bb.Min.y ),
        ImVec2( icon_left + FluentDpx( 24.f ), bb.Max.y ), style.StandardIconSize,
        ImFluent::GetColorU32( ImFluentCol_TextPrimary ) );
    if ( !compact )
    {
        const ImVec4 clip( bb.Min.x + FluentDpx( 44.f ), bb.Min.y, bb.Max.x - FluentDpx( 8.f ), bb.Max.y );
        dl->AddText( ImGui::GetFont(), ImGui::GetFontSize(),
            ImVec2( clip.x, ( bb.Min.y + bb.Max.y - ImGui::GetFontSize() ) * 0.5f ),
            ImFluent::GetColorU32( ImFluentCol_TextPrimary ), label, ImGui::FindRenderedTextEnd( label ), 0.f, &clip );
    }
    else if ( hovered )
        ImFluent::SetItemTooltip( "%s", label );
    RenderNavFocusRing( id, bb, radius );
    return pressed;
}

bool ImFluent::BeginNavItem( const char * label, bool selected, const char * glyph )
{
    glyph           = ImFluent::ConsumePendingGlyph( glyph );
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );

    const float row_h    = FluentDpx( style.NavItemHeight );
    const ImVec2 pos     = w->DC.CursorPos;
    const float indent_x = w->DC.Indent.x;
    const ImRect bb( ImVec2( w->Pos.x, pos.y ), ImVec2( w->Pos.x + w->Size.x, pos.y + row_h ) );
    // ImGui::SetNextItemOpen() sets the expanded state, as for a TreeNode (read before ItemAdd clears it).
    ImGuiContext & g       = *GImGui;
    ImGuiStorage * storage = ImGui::GetStateStorage();
    if ( g.NextItemData.HasFlags & ImGuiNextItemDataFlags_HasOpen )
    {
        const bool first_use = storage->GetInt( id, -1 ) == -1;
        if ( ( g.NextItemData.OpenCond & ( ImGuiCond_Always | ImGuiCond_Appearing ) ) || first_use )
            storage->SetBool( id, g.NextItemData.OpenVal );
    }
    ImGui::ItemSize( ImVec2( w->Size.x, row_h ) );
    if ( !ImGui::ItemAdd( bb, id, NULL, NavMenuItemFlags( id, selected ) ) )
        return NavItemBeginChildren( id, storage->GetBool( id, false ) ); // Its children can be in view

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );
    NavMenuItemNav( id );

    bool expanded = storage->GetBool( id, false );
    if ( pressed )
    {
        expanded = !expanded;
        storage->SetBool( id, expanded );
    }
    // WinUI NavigationView: Right expands, Left collapses.
    if ( ( expanded && ConsumeNavArrow( id, ImGuiDir_Left ) ) || ( !expanded && ConsumeNavArrow( id, ImGuiDir_Right ) ) )
    {
        expanded = !expanded;
        storage->SetBool( id, expanded );
    }
    ImFluent::ReportItemInfo( id, label, ImFluent::OpenedFlags( expanded ) | ImFluent::CheckedFlags( selected ) );

    const float r        = FluentDpx( style.ControlCornerRadius );
    ImU32 fillTarget     = ImFluent::ResolveSubtleFillState( selected, held, hovered );
    const ImU32 fillAnim = ImFluent::AnimateColorU32( id, fillTarget );
    ImDrawList * dl      = w->DrawList;
    const ImRect pill    = NavItemPillRect( bb );
    dl->AddRectFilled( pill.Min, pill.Max, fillAnim, r );

    if ( selected )
    {
        const float bx = bb.Min.x + indent_x + FluentDpx( style.SpacingSmall );
        const float bh = row_h * 0.5f;
        dl->AddRectFilled( ImVec2( bx, ( bb.Min.y + bb.Max.y - bh ) * 0.5f ), ImVec2( bx + FluentDpx( style.SelectionIndicatorThickness ), ( bb.Min.y + bb.Max.y + bh ) * 0.5f ), ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), FluentDpx( style.SpacingXSmall ) );
    }
    const float icon_x = bb.Min.x + indent_x + FluentDpx( ( style.NavPaneCompactWidth - style.StandardIconSize ) * 0.5f );
    const float text_x = bb.Min.x + indent_x + FluentDpx( style.NavPaneCompactWidth );
    const float cy     = ( bb.Min.y + bb.Max.y - ImGui::GetFontSize() ) * 0.5f;
    if ( glyph )
        RenderNavGlyph( dl, icon_x, bb, glyph );

    const bool show_text = g_Ctx.NavView.CurrentWidth > FluentDpx( style.AppBarButtonWidth );
    if ( show_text )
        dl->AddText( ImVec2( text_x, cy ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), label );

    if ( show_text )
    {
        const float chev_phase = ImFluent::AnimateFloat( id, expanded ? 1.f : 0.f, 0.20f, 1 );
        const float L          = FluentDpx( style.ChevronGlyphSize + 1.f );
        const float cx_start   = bb.Max.x - FluentDpx( style.SpacingLarge ) - L * 2.f;
        const float ccy        = ( bb.Min.y + bb.Max.y ) * 0.5f;
        const ImU32 chev_col   = ImFluent::GetColorU32( ImFluentCol_TextSecondary );
        ImFluent::RenderChevronLerp( dl, cx_start + L, ccy, L, chev_phase, chev_col, FluentDpx( style.StrokeMedium ) );
    }

    if ( ImFluent::IsItemFocused( id ) )
        ImFluent::RenderNavFocusRing( pill, r );
    return NavItemBeginChildren( id, expanded );
}

void ImFluent::EndNavItem()
{
    if ( g_Ctx.NavItemIndentStack.empty() )
    {
        IM_ASSERT( 0 && "EndNavItem without matching Begin" );
        return;
    }
    const float indent_amt = g_Ctx.NavItemIndentStack.back();
    g_Ctx.NavItemIndentStack.pop_back();
    g_Ctx.NavItemParentStack.pop_back();
    ImGui::Unindent( indent_amt );
}

void ImFluent::NavSubHeader( const char * text )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    if ( g_Ctx.NavView.CurrentWidth < FluentDpx( style.AppBarButtonWidth + style.SpacingXXLarge + style.SpacingMedium ) )
        return;
    ImGui::Dummy( ImVec2( 0.f, FluentDpx( style.SpacingMedium ) ) );
    ImGui::PushStyleColor( ImGuiCol_Text, ImFluent::GetColorU32( ImFluentCol_TextSecondary ) );
    ImGui::SetCursorPosX( FluentDpx( style.StandardIconSize - 2.f ) );
    ImGui::TextUnformatted( text );
    ImGui::PopStyleColor();
}

void ImFluent::NavPaneTitle( const char * text )
{
    if ( !text || !*text )
        return;
    const ImFluentStyle & style = ImFluent::GetStyle();
    if ( g_Ctx.NavView.CurrentWidth < FluentDpx( style.AppBarButtonWidth + style.SpacingXXLarge + style.SpacingMedium ) )
        return;
    ImGui::SetCursorPosX( FluentDpx( style.StandardIconSize - 2.f ) );
    ImFluent::TextBlock( text, ImFluentTextStyle_BodyStrong );
    ImGui::Dummy( ImVec2( 0.f, FluentDpx( style.SpacingXSmall ) ) );
}

bool ImFluent::NavPaneAutoSuggestBox( const char * label, char * buf, size_t buf_size, const char * const items[], int items_count, int * selected_index, const char * hint )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    if ( g_Ctx.NavView.CurrentWidth < FluentDpx( style.AppBarButtonWidth + style.SpacingXXLarge + style.SpacingMedium ) )
        return false;
    ImGui::SetCursorPosX( FluentDpx( style.SpacingSmall ) );
    ImGui::PushItemWidth( g_Ctx.NavView.CurrentWidth - FluentDpx( style.SpacingSmall ) * 2.f );
    const bool changed = ImFluent::AutoSuggestBox( label, buf, buf_size, items, items_count, selected_index, hint );
    ImGui::PopItemWidth();
    ImGui::Dummy( ImVec2( 0.f, FluentDpx( style.SpacingXSmall ) ) );
    return changed;
}

bool ImFluent::NavBackButton( bool enabled, bool visible )
{
    if ( !visible )
        return false;
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const float h               = FluentDpx( style.NavItemHeight );
    const float W               = ImMax( h, g_Ctx.NavView.CurrentWidth - FluentDpx( style.SpacingMedium ) );
    const ImGuiID id            = w->GetID( "##nav-back" );
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + W, pos.y + h ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id ) )
        return false;

    bool hovered = false, held = false;
    bool pressed = false;
    if ( enabled )
        pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );
    ReportItemInfo( id, "Back" );

    ImDrawList * dl = w->DrawList;
    const float r   = FluentDpx( style.ControlCornerRadius );
    if ( enabled && ( hovered || held ) )
    {
        const ImU32 fill = ImFluent::GetColorU32( held ? ImFluentCol_SubtleFillTertiary : ImFluentCol_SubtleFillSecondary );
        dl->AddRectFilled( bb.Min, bb.Max, fill, r );
    }

    const ImU32 textCol = ImFluent::GetColorU32( enabled ? ImFluentCol_TextPrimary : ImFluentCol_TextDisabled );
    const float cy      = ( bb.Min.y + bb.Max.y - ImGui::GetFontSize() ) * 0.5f;
    dl->AddText( ImVec2( bb.Min.x + FluentDpx( style.StandardIconSize - 2.f ), cy ), textCol, ImFluentIcon_Back );

    if ( enabled && IsItemFocused( id ) )
        RenderNavFocusRing( bb, r );
    return pressed;
}

void ImFluent::NavPaneFooterBegin()
{
    ImGui::Dummy( ImVec2( 0.f, ImGui::GetContentRegionAvail().y - GetStyle().NavItemHeight * ImGui::GetStyle().FontScaleDpi - GetStyle().SpacingMedium * ImGui::GetStyle().FontScaleDpi ) );
}

void ImFluent::NavPaneFooterEnd()
{
}

bool ImFluent::NavSettingsItem( bool selected )
{
    EndNavMenuRegion(); // A footer item: pinned under the scrolling menu items
    SetNextItemGlyph( ImFluentIcon_Settings );
    g_Ctx.NavView.FooterItem = true;
    const bool pressed       = NavItem( "Settings", selected );
    g_Ctx.NavView.FooterItem = false;
    return pressed;
}

void ImFluent::EndNavigationView()
{
    const bool had_region = g_Ctx.NavView.MenuRegionOpen || g_Ctx.NavView.FooterStartY >= 0.f;
    EndNavMenuRegion();
    if ( had_region )
    {
        const float footer_h = ImMax( 0.f, ImGui::GetCursorPosY() - g_Ctx.NavView.FooterStartY );
        ImGui::GetStateStorage()->SetFloat( NAVVIEW_KEY_FOOTER_H, footer_h );
        g_Ctx.NavView.FooterStartY = -1.f;
    }
    ImGui::EndChild();
    g_Ctx.NavView.InTabGroup = false;
    EndTabGroup();
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    ImGui::PopID();
    ImGui::SameLine( 0.f, 0.f );
}

void ImFluent::NavigationViewBeginContent( const ImVec2 & padding )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    // WinUI: a content layer over the base color, 1 px stroke, rounded top-left corner; drawn in the parent, under the child.
    {
        ImDrawList * dl    = ImGui::GetWindowDrawList();
        const ImVec2 p0    = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 p1( p0.x + avail.x, p0.y + avail.y );
        const float r = FluentDpx( style.OverlayCornerRadius );
        dl->AddRectFilled( p0, p1, ImFluent::GetColorU32( ImFluentCol_LayerFillDefault ), r, ImDrawFlags_RoundCornersTopLeft );
        ImFluent::StrokeRect( dl, p0, ImVec2( p1.x + r, p1.y + r ), ImFluent::GetColorU32( ImFluentCol_CardStrokeDefault ), r, ImDrawFlags_RoundCornersTopLeft );
    }
    ImGui::PushStyleColor( ImGuiCol_ChildBg, IM_COL32( 0, 0, 0, 0 ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( padding.x >= 0.f ? padding.x : FluentDpx( style.SpacingXXLarge ), padding.y >= 0.f ? padding.y : FluentDpx( style.SpacingXXLarge ) ) );
    // Fluent scroll bar (as ScrollViewer): a thin rounded thumb on a clear track. Only for this window's own bar.
    ImGui::PushStyleColor( ImGuiCol_ScrollbarBg, IM_COL32( 0, 0, 0, 0 ) );
    ImGui::PushStyleColor( ImGuiCol_ScrollbarGrab, ImFluent::GetColorU32( ImFluentCol_ControlStrongFillDefault ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ScrollbarSize, FluentDpx( style.SpacingMedium ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ScrollbarRounding, FluentDpx( style.SpacingMedium ) );
    ImGui::BeginChild( "##nav-content", ImVec2( 0, 0 ), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened );
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor( 2 );
    g_Ctx.NavView.ContentStarted = true;
}

void ImFluent::NavContentHeader( const char * title )
{
    if ( !title || !*title )
        return;
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImFluent::TextBlock( title, ImFluentTextStyle_Title );
    ImGui::Dummy( ImVec2( 0.f, FluentDpx( style.SpacingMedium ) ) );
}

void ImFluent::NavigationViewEndContent()
{
    if ( !g_Ctx.NavView.ContentStarted )
        return;
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    g_Ctx.NavView.ContentStarted = false;
}

// [SECTION] SplitView

bool ImFluent::BeginSplitView( const char * id, bool * is_pane_open, ImFluentSplitViewDisplayMode display_mode, ImFluentSplitViewPanePlacement placement, float open_pane_width_dpx, float compact_pane_width_dpx )
{
    ImFluentSplitViewState s;
    ImStrncpy( s.Id, id, sizeof( s.Id ) );
    s.IsPaneOpen       = is_pane_open;
    s.Mode             = display_mode;
    s.Placement        = placement;
    s.OpenPaneWidth    = FluentDpx( open_pane_width_dpx );
    s.CompactPaneWidth = FluentDpx( compact_pane_width_dpx );
    s.PaneDone         = false;
    s.ContentDone      = false;

    const bool open       = is_pane_open && *is_pane_open;
    const bool is_compact = ( display_mode == ImFluentSplitViewDisplayMode_CompactInline || display_mode == ImFluentSplitViewDisplayMode_CompactOverlay );
    const bool is_overlay = ( display_mode == ImFluentSplitViewDisplayMode_Overlay || display_mode == ImFluentSplitViewDisplayMode_CompactOverlay );

    const float min_w     = is_compact ? s.CompactPaneWidth : 0.f;
    const float target_w  = open ? s.OpenPaneWidth : min_w;
    const ImGuiID anim_id = ImGui::GetID( id );
    s.CurrentPaneWidth    = ImFluent::AnimateFloat( anim_id, target_w, 0.20f );

    const ImVec2 avail  = ImGui::GetContentRegionAvail();
    const float outer_w = avail.x;
    const float outer_h = ( avail.y > 0.f ) ? avail.y : ImGui::GetTextLineHeightWithSpacing() * 12.f;
    s.ContentWidth      = is_overlay ? outer_w : ImMax( 0.f, outer_w - s.CurrentPaneWidth );
    s.Height            = outer_h;

    ImGui::PushID( id );
    ImGui::BeginGroup();
    s.OuterStartCursorX = ImGui::GetCursorScreenPos().x;
    s.OuterStartCursorY = ImGui::GetCursorScreenPos().y;

    g_Ctx.SplitViewStack.push_back( s );
    return true;
}

bool ImFluent::BeginSplitViewPane()
{
    if ( g_Ctx.SplitViewStack.empty() )
        return false;
    ImFluentSplitViewState & s  = g_Ctx.SplitViewStack.back();
    const ImFluentStyle & style = ImFluent::GetStyle();

    if ( s.CurrentPaneWidth < 1.f )
    {
        s.PaneDone = true;
        return false;
    }

    const bool is_overlay = ( s.Mode == ImFluentSplitViewDisplayMode_Overlay || s.Mode == ImFluentSplitViewDisplayMode_CompactOverlay );

    float pane_x = s.OuterStartCursorX;
    if ( s.Placement == ImFluentSplitViewPanePlacement_Right )
    {
        pane_x = s.OuterStartCursorX + ( is_overlay ? s.ContentWidth - s.CurrentPaneWidth : s.ContentWidth );
    }

    ImGui::SetCursorScreenPos( ImVec2( pane_x, s.OuterStartCursorY ) );

    const ImU32 pane_bg = ImFluent::GetColorU32( is_overlay ? ImFluentCol_LayerFillAlt : ImFluentCol_LayerFillDefault );
    ImGui::PushStyleColor( ImGuiCol_ChildBg, pane_bg );
    ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, 0.f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( FluentDpx( style.SpacingSmall ), FluentDpx( style.SpacingSmall ) ) );
    char child_id[ 80 ];
    ImFormatString( child_id, sizeof( child_id ), "##sv-pane-%s", s.Id );
    const bool pane_open = ImGui::BeginChild( child_id, ImVec2( s.CurrentPaneWidth, s.Height ), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar );
    if ( !pane_open )
    {
        ImGui::EndChild();
        ImGui::PopStyleVar( 2 );
        ImGui::PopStyleColor();
        s.PaneDone = true;
        return false;
    }
    return true;
}

void ImFluent::EndSplitViewPane()
{
    if ( g_Ctx.SplitViewStack.empty() )
        return;
    ImFluentSplitViewState & s = g_Ctx.SplitViewStack.back();
    if ( s.PaneDone )
        return;
    ImGui::EndChild();
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor();
    s.PaneDone = true;
}

bool ImFluent::BeginSplitViewContent()
{
    if ( g_Ctx.SplitViewStack.empty() )
        return false;
    ImFluentSplitViewState & s  = g_Ctx.SplitViewStack.back();
    const ImFluentStyle & style = ImFluent::GetStyle();

    const bool is_overlay = ( s.Mode == ImFluentSplitViewDisplayMode_Overlay || s.Mode == ImFluentSplitViewDisplayMode_CompactOverlay );

    float content_x = s.OuterStartCursorX;
    if ( !is_overlay && s.Placement == ImFluentSplitViewPanePlacement_Left )
        content_x = s.OuterStartCursorX + s.CurrentPaneWidth;
    else if ( is_overlay )
        content_x = s.OuterStartCursorX;

    ImGui::SetCursorScreenPos( ImVec2( content_x, s.OuterStartCursorY ) );

    const float pad = FluentDpx( style.CardPadding );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( pad, pad ) );
    char child_id[ 80 ];
    ImFormatString( child_id, sizeof( child_id ), "##sv-content-%s", s.Id );
    const bool open = ImGui::BeginChild( child_id, ImVec2( s.ContentWidth, s.Height ), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    if ( !open )
    {
        ImGui::EndChild();
        ImGui::PopStyleVar();
        s.ContentDone = true;
        return false;
    }
    return true;
}

void ImFluent::EndSplitViewContent()
{
    if ( g_Ctx.SplitViewStack.empty() )
        return;
    ImFluentSplitViewState & s = g_Ctx.SplitViewStack.back();
    if ( s.ContentDone )
        return;
    ImGui::EndChild();
    ImGui::PopStyleVar();
    s.ContentDone = true;
}

void ImFluent::EndSplitView()
{
    if ( g_Ctx.SplitViewStack.empty() )
        return;
    ImGui::EndGroup();
    ImGui::PopID();
    g_Ctx.SplitViewStack.pop_back();
}

// [SECTION] Lists & Pickers (ComboBox / ListBox / TreeNode / GridView / PipsPager / BreadcrumbBar)

// ComboBox type-ahead (WinUI): the search string resets after one second; the first case-insensitive prefix match wins
// (leading spaces ignored). A new string searches after the current item, an extended one from it. Returns -1 if none.
static int ComboTypeAheadSearch( ImGuiID id, int current, const char * const items[], int items_count )
{
    ImGuiContext & g = *GImGui;
    if ( g.IO.InputQueueCharacters.Size == 0 || items_count <= 0 )
        return -1;
    ImFluentContext::TypeAhead & ta = g_Ctx.ComboTypeAhead;
    const double now                = ImGui::GetTime();
    const bool fresh                = ( ta.Id != id || now - ta.LastTime > 1.0 );
    if ( fresh )
    {
        ta.Id       = id;
        ta.Len      = 0;
        ta.Buf[ 0 ] = 0;
    }
    for ( ImWchar c : g.IO.InputQueueCharacters )
    {
        if ( c < 32 || ( c == ' ' && ta.Len == 0 ) )
            continue;
        char utf8[ 5 ];
        const int n = ImTextCharToUtf8( utf8, c );
        if ( ta.Len + n >= ( int )sizeof( ta.Buf ) )
            break;
        memcpy( ta.Buf + ta.Len, utf8, ( size_t )n );
        ta.Len += n;
        ta.Buf[ ta.Len ] = 0;
    }
    ta.LastTime = now;
    if ( ta.Len == 0 )
        return -1;
    const int start = fresh ? current + 1 : ImMax( current, 0 );
    for ( int k = 0; k < items_count; ++k )
    {
        const int i      = ( ( start + k ) % items_count + items_count ) % items_count;
        const char * itm = items[ i ];
        if ( !itm )
            continue;
        while ( *itm == ' ' )
            ++itm;
        if ( ImStrnicmp( itm, ta.Buf, ( size_t )ta.Len ) == 0 )
            return i;
    }
    return -1;
}

bool ImFluent::ComboBox( const char * label, int * current_item, const char * const items[], int items_count, ImGuiComboFlags flags, const char * placeholder )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
    {
        ClearPendingNextItem();
        return false;
    }
    RenderAndConsumePendingHeader();

    IM_ASSERT( ( flags & ( ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview ) ) != ( ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview ) );

    const ImFluentStyle & style = ImFluent::GetStyle();
    const float h               = FluentDpx( style.ControlHeight );
    const float r               = FluentDpx( style.ControlCornerRadius );
    const float chev_w          = ( flags & ImGuiComboFlags_NoArrowButton ) ? 0.f : h;
    const char * preview        = ( current_item && *current_item >= 0 && *current_item < items_count && items[ *current_item ] ) ? items[ *current_item ] : "";

    float full_w;
    if ( flags & ImGuiComboFlags_NoPreview )
        full_w = chev_w;
    else if ( flags & ImGuiComboFlags_WidthFitPreview )
        full_w = chev_w + ( preview && *preview ? ImGui::CalcTextSize( preview ).x : 0.f ) + FluentDpx( style.SpacingLarge ) * 2.f;
    else
        full_w = ImGui::CalcItemWidth();

    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + full_w, pos.y + h ) );

    const ImGuiID id = w->GetID( label );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id ) )
    {
        ClearPendingNextItem();
        return false;
    }

    ImGuiContext & gctx = *ImGui::GetCurrentContext();
    bool hovered = false, held = false;
    ImGui::ButtonBehavior( bb, id, &hovered, &held );
    const bool disabled = IsLastItemDisabled();

    const ImGuiID popup_id = GetWidgetPopupId( id, "##fl_cb_popup" );
    const bool popup_open = ImGui::IsPopupOpen( popup_id, ImGuiPopupFlags_None );
    ReportItemInfo( id, label, OpenedFlags( popup_open ) );

    const bool click_in_bb  = ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && IsMouseOverWindowRect( bb );
    const bool nav_activate = ( gctx.NavActivateId == id );
    // WinUI ComboBox also opens with Alt+Down and F4.
    const bool key_open     = !popup_open && !disabled && ( IsFocusedItemChordPressed( id, g_Ctx.Actions.TogglePopup, false ) || IsFocusedItemChordPressed( id, g_Ctx.Actions.OpenPopup, false ) );
    const bool toggle       = ( click_in_bb || nav_activate || key_open ) && !disabled;

    if ( toggle )
    {
        if ( popup_open )
        {
            ClosePopupById( gctx, popup_id );
        }
        else
        {
            ImGui::OpenPopupEx( popup_id, ImGuiPopupFlags_None );
        }
    }

    // Closed (WinUI): Up/Down step without opening (no wrap), Home/End jump to the ends, typing selects the first match.
    bool changed = false;
    if ( !popup_open && !toggle && !disabled && current_item && !gctx.IO.KeyAlt && items_count > 0 )
    {
        int sel = *current_item;
        if ( ConsumeNavArrow( id, ImGuiDir_Down ) )
            sel = ( sel < 0 ) ? 0 : ImMin( sel + 1, items_count - 1 );
        if ( ConsumeNavArrow( id, ImGuiDir_Up ) && sel > 0 )
            sel = sel - 1;
        if ( IsFocusedItemChordPressed( id, g_Ctx.Actions.FirstItem ) )
            sel = 0;
        if ( IsFocusedItemChordPressed( id, g_Ctx.Actions.LastItem ) )
            sel = items_count - 1;
        if ( gctx.NavId == id && gctx.ActiveId == 0 )
        {
            const int match = ComboTypeAheadSearch( id, sel, items, items_count );
            if ( match >= 0 )
                sel = match;
        }
        if ( sel != *current_item )
        {
            *current_item = sel;
            changed       = true;
            preview       = items[ sel ] ? items[ sel ] : "";
        }
    }

    ImDrawList * dl = w->DrawList;
    ImU32 fill_target;
    if ( disabled )
        fill_target = ImFluent::GetColorU32( ImFluentCol_ControlFillDisabled );
    else if ( held || popup_open )
        fill_target = ImFluent::GetColorU32( ImFluentCol_ControlFillTertiary );
    else if ( hovered )
        fill_target = ImFluent::GetColorU32( ImFluentCol_ControlFillSecondary );
    else
        fill_target = ImFluent::GetColorU32( ImFluentCol_ControlFillDefault );
    const ImU32 fill = AnimateColorU32( id, fill_target );
    FillInsideBorder( dl, bb.Min, bb.Max, fill, r );

    const ImU32 stroke     = ImFluent::GetColorU32( ImFluentCol_ControlStrokeDefault );
    const ImU32 stroke_top = ImFluent::GetColorU32( ImFluentCol_ElevationControlTop );
    const ImU32 stroke_bot = ImFluent::GetColorU32( ImFluentCol_ElevationControlBottom );
    if ( !held && !popup_open && !disabled )
        RenderElevationBorder( dl, bb, r, stroke, stroke_top, stroke_bot, 1.f );
    else
        ImFluent::StrokeRect( dl, bb.Min, bb.Max, stroke, r );

    // No selection shows the placeholder (WinUI PlaceholderText) in the secondary text colour.
    const bool is_placeholder = !( preview && *preview ) && placeholder && *placeholder;
    const char * shown        = is_placeholder ? placeholder : preview;
    const ImU32 text_col      = ImFluent::GetColorU32( disabled ? ImFluentCol_TextDisabled : is_placeholder ? ImFluentCol_TextSecondary : ImFluentCol_TextPrimary );
    if ( !( flags & ImGuiComboFlags_NoPreview ) && shown && *shown )
    {
        const float pad_x = FluentDpx( style.SpacingLarge );
        ImFluentStackGuard g;
        g.PushStyleColor( ImGuiCol_Text, text_col );
        const ImVec2 tmin( bb.Min.x + pad_x, bb.Min.y );
        const ImVec2 tmax( bb.Max.x - chev_w, bb.Max.y );
        const ImRect text_clip( tmin, tmax );
        ImGui::RenderTextClipped( tmin, tmax, shown, ImGui::FindRenderedTextEnd( shown ), NULL, ImVec2( 0.f, 0.5f ), &text_clip );
    }

    if ( chev_w > 0.f )
    {
        const ImRect chev_bb( ImVec2( bb.Max.x - chev_w, bb.Min.y ), bb.Max );
        const ImU32 chev_col = ImFluent::GetColorU32( disabled ? ImFluentCol_TextDisabled : ImFluentCol_TextSecondary );
        RenderComboChevron( dl, chev_bb, chev_col, id ^ 0xCAFEu, popup_open );
    }

    RenderNavFocusRing( id, bb, r );

    const int popup_max_items = ResolveComboPopupMaxItems( flags );
    const float popup_max_h   = CalcComboPopupMaxHeight( popup_max_items, h );
    ImGui::SetNextWindowPos( ImVec2( bb.Min.x, bb.Max.y + FluentDpx( style.SpacingXSmall ) ) );
    ImGui::SetNextWindowSizeConstraints( ImVec2( bb.GetWidth(), 0.f ), ImVec2( bb.GetWidth(), popup_max_h ) );

    PushOverlayWindowStyle( style.SpacingXSmall, style.SpacingXSmall );
    const ImGuiWindowFlags cb_wflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    if ( ImGui::BeginPopupEx( popup_id, cb_wflags ) )
    {
        ImGuiWindow * popup_window = ImGui::GetCurrentWindow();

        // Open: typing selects at once and moves focus to the match; Home/End focus the first/last item (WinUI).
        int focus_match = -1;
        if ( current_item && gctx.NavWindow == popup_window )
        {
            focus_match = ComboTypeAheadSearch( id, *current_item, items, items_count );
            if ( focus_match >= 0 && focus_match != *current_item )
            {
                *current_item = focus_match;
                changed       = true;
            }
        }
        int focus_only = -1;
        if ( gctx.NavWindow == popup_window && gctx.NavId != 0 )
        {
            if ( IsChordPressedOwned( g_Ctx.Actions.FirstItem, popup_id, false ) )
                focus_only = 0;
            if ( IsChordPressedOwned( g_Ctx.Actions.LastItem, popup_id, false ) )
                focus_only = items_count - 1;
        }

        const float row_advance = h + ImGui::GetStyle().ItemSpacing.y;
        ImGuiListClipper clipper;
        clipper.Begin( items_count, row_advance );
        if ( current_item && *current_item >= 0 && *current_item < items_count )
            clipper.IncludeItemByIndex( *current_item );
        if ( focus_only >= 0 )
            clipper.IncludeItemByIndex( focus_only );
        while ( clipper.Step() )
        {
            for ( int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i )
            {
                if ( !items[ i ] )
                    continue;
                ImGui::PushID( i );
                const bool sel = ( current_item && *current_item == i );
                if ( ImFluent::Selectable( items[ i ], sel, NULL, h ) )
                {
                    if ( current_item )
                        *current_item = i;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                const ImGuiID item_id = gctx.LastItemData.ID;
                if ( sel )
                    ImGui::SetItemDefaultFocus(); // Keyboard focus starts on the selected item
                if ( i == 0 )
                    ConsumeNavArrow( item_id, ImGuiDir_Up ); // The list does not wrap (WinUI ComboBox)
                if ( i == items_count - 1 )
                    ConsumeNavArrow( item_id, ImGuiDir_Down );
                if ( i == focus_match || i == focus_only )
                {
                    ImGui::SetFocusID( item_id, popup_window );
                    ImGui::SetScrollHereY();
                    gctx.NavCursorVisible = true;
                }
                ImGui::PopID();
            }
        }
        // Only a list that was already open closes: the key press that opened it must not close it again.
        // Tab closes without committing, as in WinUI; focus returns to the box.
        if ( popup_open && ( IsActionPressed( g_Ctx.Actions.TogglePopup ) || IsActionPressed( g_Ctx.Actions.ClosePopup ) || ImGui::IsKeyPressed( ImGuiKey_Tab, false ) ) )
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    PopOverlayWindowStyle();

    RenderAndConsumePendingDescription();
    RenderAndConsumePendingError( bb );
    return changed;
}

bool ImFluent::Selectable( const char * label, bool selected, const char * glyph, float height )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    glyph           = ConsumePendingGlyph( glyph );
    if ( w->SkipItems )
        return false;

    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );
    const float h               = ( height > 0.f ) ? height : FluentDpx( style.ControlHeight );
    const float pad_x           = FluentDpx( style.SpacingLarge );
    const float gap             = FluentDpx( style.SpacingMedium );
    const float w_avail         = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + w_avail, pos.y + h ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id ) )
        return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );
    ReportItemInfo( id, label, CheckedFlags( selected ) );

    ImU32 fill_target;
    if ( held || selected )
        fill_target = ImFluent::GetColorU32( ImFluentCol_SubtleFillTertiary );
    else if ( hovered )
        fill_target = ImFluent::GetColorU32( ImFluentCol_SubtleFillSecondary );
    else
        fill_target = ImFluent::GetColorU32( ImFluentCol_SubtleFillTransparent );
    const ImU32 fill = AnimateColorU32( id, fill_target );
    ImDrawList * dl  = w->DrawList;
    const float r    = FluentDpx( style.ControlCornerRadius );
    dl->AddRectFilled( bb.Min, bb.Max, fill, r );

    if ( selected )
        RenderSelectionIndicator( dl, bb, ImGuiDir_Left, ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), 0.55f );

    float cx             = bb.Min.x + pad_x;
    const ImU32 text_col = ImFluent::GetColorU32( ImFluentCol_TextPrimary );
    ImFluentStackGuard g;
    g.PushStyleColor( ImGuiCol_Text, text_col );

    if ( glyph && *glyph )
    {
        const ImVec2 gs = ImGui::CalcTextSize( glyph );
        dl->AddText( ImVec2( cx, bb.Min.y + ( h - gs.y ) * 0.5f ), text_col, glyph );
        cx += gs.x + gap;
    }

    const char * text_end = ImGui::FindRenderedTextEnd( label );
    const ImVec2 tmin( cx, bb.Min.y );
    const ImVec2 tmax( bb.Max.x - pad_x, bb.Max.y );
    const ImRect text_clip( tmin, tmax );
    ImGui::RenderTextClipped( tmin, tmax, label, text_end, NULL, ImVec2( 0.f, 0.5f ), &text_clip );

    RenderNavFocusRing( id, bb, r );
    return pressed;
}

bool ImFluent::ListBox( const char * label, int * current_item, const char * const items[], int items_count, int height_in_items )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImFluentStackGuard g;
    g.PushStyleColor( ImGuiCol_FrameBg, ImFluent::GetColorU32( ImFluentCol_LayerFillDefault ) );
    g.PushStyleVar( ImGuiStyleVar_FrameRounding, FluentDpx( style.ControlCornerRadius ) );
    bool changed = false;
    const ImVec2 sz( ImGui::CalcItemWidth(), FluentDpx( ( float )height_in_items * style.ControlHeight ) );
    // WinUI ListBox: one tab stop, selection follows focus; the frame is a flattened child, so Tab reaches the items directly.
    ImGui::BeginGroup();
    const ImGuiID list_id = ImGui::GetID( label );
    if ( ImGui::BeginChild( list_id, sz, ImGuiChildFlags_FrameStyle | ImGuiChildFlags_NavFlattened ) )
    {
        BeginTabGroup( list_id, ImFluentTabGroupFlags_Vertical | ImFluentTabGroupFlags_HomeEnd | ImFluentTabGroupFlags_SelectionFollowsFocus | ImFluentTabGroupFlags_EnterOnLastFocused );
        for ( int i = 0; i < items_count; ++i )
        {
            if ( !items[ i ] )
                continue;
            const bool sel        = ( current_item && *current_item == i );
            const ImGuiID item_id = ImGui::GetID( items[ i ] );
            ImGui::PushItemFlag( ImGuiItemFlags_NoTabStop, TabGroupItemFlags( item_id, sel ) != ImGuiItemFlags_None );
            bool pressed = ImFluent::Selectable( items[ i ], sel );
            ImGui::PopItemFlag();
            pressed |= TabGroupItemNav( item_id );
            if ( pressed && current_item && *current_item != i )
            {
                *current_item = i;
                changed       = true;
            }
        }
        EndTabGroup();
    }
    ImGui::EndChild();
    const char * label_end = ImGui::FindRenderedTextEnd( label );
    if ( label_end > label )
    {
        ImGui::SameLine( 0.f, ImGui::GetStyle().ItemInnerSpacing.x );
        ImGui::TextUnformatted( label, label_end );
    }
    ImGui::EndGroup();
    return changed;
}

// [SECTION] ListView / GridView / TreeView containers
// The items between Begin and End are one tab stop (WinUI TabNavigation="Once"); Home/End go to the first/last item.

static void BeginItemsContainer( const char * id, int flags )
{
    ImGui::PushID( id );
    ImGui::BeginGroup();
    ImFluent::BeginTabGroup( ImGui::GetID( "##items" ), flags | ImFluentTabGroupFlags_ItemsContainer | ImFluentTabGroupFlags_HomeEnd );
}

static void EndItemsContainer()
{
    ImFluent::EndTabGroup();
    ImGui::EndGroup();
    ImGui::PopID();
}

static int ItemsViewSelectFlags( ImFluentItemsViewFlags flags )
{
    return ( flags & ImFluentItemsViewFlags_NoSelectOnFocus ) ? 0 : ImFluentTabGroupFlags_SelectionFollowsFocus;
}

void ImFluent::BeginListView( const char * id, ImFluentItemsViewFlags flags )
{
    BeginItemsContainer( id, ImFluentTabGroupFlags_Vertical | ItemsViewSelectFlags( flags ) );
}

void ImFluent::EndListView()
{
    EndItemsContainer();
}

void ImFluent::BeginGridView( const char * id, ImFluentItemsViewFlags flags )
{
    BeginItemsContainer( id, ImFluentTabGroupFlags_Grid | ItemsViewSelectFlags( flags ) );
}

void ImFluent::EndGridView()
{
    EndItemsContainer();
}

void ImFluent::BeginTreeView( const char * id, ImFluentItemsViewFlags flags )
{
    BeginItemsContainer( id, ImFluentTabGroupFlags_Vertical | ItemsViewSelectFlags( flags ) );
}

void ImFluent::EndTreeView()
{
    EndItemsContainer();
}

bool ImFluent::ListViewItem( const char * label, bool selected, const char * glyph )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    glyph           = ConsumePendingGlyph( glyph );
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );
    const float h               = FluentDpx( style.ControlHeight + 4.f );
    const float W               = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + W, pos.y + h ) );
    ImGui::ItemSize( bb );
    const bool in_list_view = InItemsContainer();
    if ( !ImGui::ItemAdd( bb, id, NULL, in_list_view ? TabGroupItemFlags( id, selected ) : ImGuiItemFlags_None ) )
        return false;
    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );
    TreeRowTakeFirstChildFocus( id ); // A leaf row inside a TreeNode
    TreeRowLeftToParent( id );
    if ( in_list_view )
        pressed |= TabGroupItemNav( id );
    ReportItemInfo( id, label, CheckedFlags( selected ) );
    ImDrawList * dl    = w->DrawList;
    const float r      = FluentDpx( style.ControlCornerRadius );
    ImU32 fillTarget;
    fillTarget = ResolveSubtleFillState( selected, held, hovered );
    dl->AddRectFilled( bb.Min, bb.Max, AnimateColorU32( id, fillTarget ), r );
    if ( selected )
    {
        const float bx = bb.Min.x + FluentDpx( style.SpacingSmall );
        const float bh = h * 0.5f;
        dl->AddRectFilled( ImVec2( bx, ( bb.Min.y + bb.Max.y - bh ) * 0.5f ), ImVec2( bx + FluentDpx( style.SelectionIndicatorThickness ), ( bb.Min.y + bb.Max.y + bh ) * 0.5f ), ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), FluentDpx( style.SpacingXSmall ) );
    }
    const float text_x = bb.Min.x + FluentDpx( glyph ? style.NavPaneCompactWidth - 4.f : style.SpacingXLarge );
    const float cy     = ( bb.Min.y + bb.Max.y - ImGui::GetFontSize() ) * 0.5f;
    if ( glyph )
        dl->AddText( ImVec2( bb.Min.x + FluentDpx( style.StandardIconSize - 2.f ), cy ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), glyph );
    dl->AddText( ImVec2( text_x, cy ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), label );
    RenderNavFocusRing( id, bb, r );
    return pressed;
}

// An open row indents its children and is their parent for Left (TreePop undoes it).
static bool TreeRowBeginChildren( ImGuiID id, bool open )
{
    if ( open )
    {
        ImGui::Indent( ImFluent::FluentDpx( ImFluent::GetStyle().CheckboxSize ) );
        g_Ctx.TreeParentStack.push_back( id );
    }
    return open;
}

// A TreeView row. single_selected < 0: TreeNode (a click toggles *p_open, optional checkbox for multiple selection).
// Else TreeViewItem: a click, Space/Enter or Up/Down arrival sets *out_select, the chevron toggles *p_open (WinUI single selection).
static bool TreeRow( const char * label, bool * p_open, bool * p_checked, int single_selected, bool * out_select )
{
    using namespace ImFluent;
    ImGuiWindow * w   = ImGui::GetCurrentWindow();
    const bool single = single_selected >= 0;
    if ( w->SkipItems )
        return single && p_open && TreeRowBeginChildren( w->GetID( label ), *p_open ); // TreeViewItem callers TreePop() while *p_open
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );
    const float h               = FluentDpx( style.ControlHeight );
    const ImVec2 pos            = w->DC.CursorPos;

    const float row_left  = w->WorkRect.Min.x;
    const float row_right = w->WorkRect.Max.x;
    const ImRect bb( ImVec2( row_left, pos.y ), ImVec2( row_right, pos.y + h ) );

    const char * glyph = ConsumePendingGlyph( NULL );

    ImGui::ItemSize( ImVec2( row_right - pos.x, h ) );

    const float pad         = FluentDpx( style.SpacingMedium );
    const float check_w     = FluentDpx( style.CheckboxSize );
    const float check_gap   = FluentDpx( style.SpacingMedium );
    const float chevron_w   = FluentDpx( style.SpacingXLarge );
    const float chevron_gap = FluentDpx( style.SpacingMedium );
    const float glyph_w     = FluentDpx( style.StandardIconSize + 2.f );
    const float glyph_gap   = FluentDpx( style.SpacingMedium );

    float x = pos.x + pad;
    ImRect check_bb;
    if ( p_checked )
    {
        check_bb = ImRect( ImVec2( x, bb.Min.y + ( h - check_w ) * 0.5f ), ImVec2( x + check_w, bb.Min.y + ( h + check_w ) * 0.5f ) );
        x += check_w + check_gap;
    }
    const float chevron_cx = x + chevron_w * 0.5f;
    x += chevron_w + chevron_gap;

    const float glyph_x = x;
    if ( glyph )
        x += glyph_w + glyph_gap;
    const float label_x = x;

    const bool chevron_button = single && p_open;
    if ( p_checked || chevron_button )
        ImGui::SetNextItemAllowOverlap();
    const bool in_tree_view = InItemsContainer();
    if ( !ImGui::ItemAdd( bb, id, NULL, in_tree_view ? TabGroupItemFlags( id, single_selected > 0 ) : ImGuiItemFlags_None ) )
        return TreeRowBeginChildren( id, p_open && *p_open );

    bool hovered = false, held = false;
    const bool row_pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );

    // WinUI TreeView: Right expands, then moves to the first child; Left collapses, then moves to the parent.
    TreeRowTakeFirstChildFocus( id );
    if ( p_open && *p_open )
    {
        if ( ConsumeNavArrow( id, ImGuiDir_Left ) )
            *p_open = false;
        else if ( ConsumeNavArrow( id, ImGuiDir_Right ) )
            g_Ctx.TreeFocusFirstChildOf = id;
    }
    else if ( p_open && ConsumeNavArrow( id, ImGuiDir_Right ) )
        *p_open = true;
    else
        TreeRowLeftToParent( id );
    const bool arrived = in_tree_view && TabGroupItemNav( id );

    if ( chevron_button )
    {
        const float half  = ( chevron_w + chevron_gap ) * 0.5f;
        const ImGuiID vid = w->GetID( ( const void * )( ( intptr_t )id ^ 0xC4EDu ) );
        const ImRect chevron_bb( ImVec2( chevron_cx - half, bb.Min.y ), ImVec2( chevron_cx + half, bb.Max.y ) );
        ImGui::ItemAdd( chevron_bb, vid, NULL, ImGuiItemFlags_NoTabStop | ImGuiItemFlags_NoNav );
        bool chevron_hov = false, chevron_held = false;
        if ( ImGui::ButtonBehavior( chevron_bb, vid, &chevron_hov, &chevron_held ) )
            *p_open = !*p_open;
        ReportItemInfo( vid, "Expand", OpenedFlags( *p_open ) );
        hovered |= chevron_hov;
    }
    if ( out_select )
        *out_select = row_pressed || arrived;

    // The checkbox is not a tab stop: Space toggles it (WinUI TreeView multiple selection); Enter and clicks expand/collapse.
    const bool space_check = p_checked && row_pressed && GImGui->NavActivatePressedId == id && !IsEnterActivation( id );
    bool check_pressed = false, check_hov = false, check_held = false;
    if ( p_checked )
    {
        const ImGuiID cid = w->GetID( ( const void * )( ( intptr_t )id ^ 0xC4ECu ) );
        ImGui::ItemAdd( check_bb, cid, NULL, ImGuiItemFlags_NoTabStop | ImGuiItemFlags_NoNav );
        check_pressed = ImGui::ButtonBehavior( check_bb, cid, &check_hov, &check_held );
        if ( check_pressed || space_check )
            *p_checked = !*p_checked;
        ReportItemInfo( cid, "Check", CheckedFlags( *p_checked ) );
    }
    if ( !single && row_pressed && !check_pressed && !space_check && p_open )
        *p_open = !*p_open;

    const bool isOpen   = p_open && *p_open;
    const bool selected = single ? single_selected > 0 : p_checked && *p_checked;
    ReportItemInfo( id, label, OpenedFlags( isOpen ) | ( single ? CheckedFlags( selected ) : 0 ) );

    ImDrawList * dl = w->DrawList;
    const float r   = FluentDpx( style.ControlCornerRadius );
    const float cy  = ( bb.Min.y + bb.Max.y ) * 0.5f;

    const ImU32 fill = ResolveSubtleFillState( selected, held, hovered );
    dl->AddRectFilled( bb.Min, bb.Max, AnimateColorU32( id, fill ), r );

    if ( selected )
        RenderSelectionIndicator( dl, bb, ImGuiDir_Left, ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), 0.5f );

    if ( p_checked )
    {
        const float box_r = FluentDpx( style.ControlCornerRadius );
        const ImGuiID cid = w->GetID( ( const void * )( ( intptr_t )id ^ 0xC4ECu ) );
        ImU32 box_fill, box_stroke;
        if ( *p_checked )
        {
            box_fill   = ImFluent::GetColorU32( check_held ? ImFluentCol_AccentFillTertiary : check_hov ? ImFluentCol_AccentFillSecondary : ImFluentCol_AccentFillDefault );
            box_stroke = box_fill;
        }
        else
        {
            box_fill   = ResolveControlAltFillState( false, check_held, check_hov );
            box_stroke = ImFluent::GetColorU32( ImFluentCol_ControlStrongStrokeDefault );
        }
        const ImU32 box_fill_anim   = AnimateColorU32( cid, box_fill );
        const ImU32 box_stroke_anim = AnimateColorU32( cid, box_stroke, 0.083f, 1 );
        ImGui::PushStyleColor( ImGuiCol_Border, box_stroke_anim );
        ImGui::RenderFrame( check_bb.Min, check_bb.Max, box_fill_anim, true, box_r );
        ImGui::PopStyleColor();
        RenderNavFocusRing( cid, check_bb, box_r );
        if ( *p_checked )
        {
            const ImU32 mark = ImFluent::GetColorU32( ImFluentCol_TextOnAccentPrimary );
            const ImVec2 a( check_bb.Min.x + check_w * 0.22f, check_bb.Min.y + check_w * 0.52f );
            const ImVec2 mid( check_bb.Min.x + check_w * 0.42f, check_bb.Min.y + check_w * 0.72f );
            const ImVec2 c( check_bb.Min.x + check_w * 0.78f, check_bb.Min.y + check_w * 0.32f );
            dl->AddLine( a, mid, mark, FluentDpx( style.StrokeMedium ) );
            dl->AddLine( mid, c, mark, FluentDpx( style.StrokeMedium ) );
        }
    }

    if ( !single || p_open ) // A TreeViewItem leaf has no chevron
        RenderChevron( dl, ImVec2( chevron_cx, cy ), isOpen ? ImGuiDir_Down : ImGuiDir_Right, ImFluent::GetColorU32( ImFluentCol_TextPrimary ), FluentDpx( style.ChevronGlyphSize ) );

    if ( glyph )
        dl->AddText( ImVec2( glyph_x, cy - ImGui::GetFontSize() * 0.5f ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), glyph );

    dl->AddText( ImVec2( label_x, cy - ImGui::GetFontSize() * 0.5f ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), label );

    RenderNavFocusRing( id, bb, r );
    return TreeRowBeginChildren( id, isOpen );
}

bool ImFluent::TreeNode( const char * label, bool * p_open )
{
    return TreeRow( label, p_open, NULL, -1, NULL );
}

bool ImFluent::TreeNode( const char * label, bool * p_open, bool * p_checked )
{
    return TreeRow( label, p_open, p_checked, -1, NULL );
}

bool ImFluent::TreeViewItem( const char * label, bool selected, bool * p_open )
{
    bool select = false;
    TreeRow( label, p_open, NULL, selected ? 1 : 0, &select );
    return select;
}

void ImFluent::TreePop()
{
    ImGui::Unindent( FluentDpx( GetStyle().CheckboxSize ) );
    if ( !g_Ctx.TreeParentStack.empty() )
        g_Ctx.TreeParentStack.pop_back();
    g_Ctx.TreeFocusFirstChildOf = 0; // A node with no child row this frame has no first child to focus
}

bool ImFluent::GridViewItem( const char * label, bool selected, const ImVec2 & size )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    ConsumePendingGlyph( NULL );
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );
    const ImVec2 sz             = ( size.x > 0 && size.y > 0 ) ? size : ImVec2( FluentDpx( style.ControlMinWidth + style.SpacingXXLarge + style.SpacingXLarge ), FluentDpx( style.ControlMinWidth ) );
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + sz.x, pos.y + sz.y ) );
    ImGui::ItemSize( bb );
    const bool in_grid_view = InItemsContainer();
    if ( in_grid_view )
        TabGroupItemRect( id, bb );
    if ( !ImGui::ItemAdd( bb, id, NULL, in_grid_view ? TabGroupItemFlags( id, selected ) : ImGuiItemFlags_None ) )
        return false;
    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );
    if ( in_grid_view )
        pressed |= TabGroupItemNav( id );
    ReportItemInfo( id, label, CheckedFlags( selected ) );
    const float r      = FluentDpx( style.OverlayCornerRadius );
    ImU32 fill         = ImFluent::GetColorU32( selected ? ImFluentCol_AccentFillSecondary : held ? ImFluentCol_ControlFillTertiary : hovered ? ImFluentCol_ControlFillSecondary : ImFluentCol_CardBgDefault );
    ImGui::PushStyleColor( ImGuiCol_Border, ImFluent::GetColorU32( ImFluentCol_CardStrokeDefault ) );
    ImGui::RenderFrame( bb.Min, bb.Max, AnimateColorU32( id, fill ), true, r );
    ImGui::PopStyleColor();
    const ImVec2 ts       = ImGui::CalcTextSize( label );
    const ImU32 label_col = ImFluent::GetColorU32( selected ? ImFluentCol_TextOnAccentPrimary : ImFluentCol_TextPrimary );
    ImGui::PushStyleColor( ImGuiCol_Text, label_col );
    ImGui::RenderTextClipped( ImVec2( bb.Min.x, bb.Max.y - ts.y - FluentDpx( style.SpacingLarge ) ), ImVec2( bb.Max.x, bb.Max.y - FluentDpx( style.SpacingLarge ) ), label, NULL, &ts, ImVec2( 0.5f, 0.5f ), &bb );
    ImGui::PopStyleColor();
    RenderNavFocusRing( id, bb, r );
    return pressed;
}

bool ImFluent::PipsPager( const char * id, int * current_item, int total_pages )
{
    if ( total_pages <= 0 )
    {
        ClearPendingNextItem();
        return false;
    }
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const float dot     = FluentDpx( style.SpacingMedium );
    const float gap     = FluentDpx( style.SpacingMedium );
    const float total_w = total_pages * dot + ( total_pages - 1 ) * gap;
    const ImVec2 pos    = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + total_w, pos.y + dot ) );
    ImGui::ItemSize( bb );
    const ImGuiID gid = w->GetID( id );
    if ( !ImGui::ItemAdd( bb, 0 ) )
        return false;
    // WinUI PipsPager: one tab stop on the selected pip, arrows focus only (Up/Down as Left/Right); past the end pips they move on spatially.
    BeginTabGroup( gid, ImFluentTabGroupFlags_None );
    ImGuiContext & g         = *GImGui;
    int nav_target           = -1;
    bool changed             = false;
    ImDrawList * dl          = w->DrawList;
    const ImU32 col_pip_sel  = ImFluent::GetColorU32( ImFluentCol_AccentFillDefault );
    const ImU32 col_pip_hov  = ImFluent::GetColorU32( ImFluentCol_TextSecondary );
    const ImU32 col_pip_rest = ImFluent::GetColorU32( ImFluentCol_TextDisabled );
    for ( int i = 0; i < total_pages; ++i )
    {
        const ImVec2 c( pos.x + i * ( dot + gap ) + dot * 0.5f, pos.y + dot * 0.5f );
        const bool sel = current_item && ( *current_item == i );
        ImGui::PushID( i );
        const ImGuiID iid = w->GetID( i );
        const ImRect rect( ImVec2( c.x - dot * 0.5f, c.y - dot * 0.5f ), ImVec2( c.x + dot * 0.5f, c.y + dot * 0.5f ) );
        if ( !ImGui::ItemAdd( rect, iid, NULL, TabGroupItemFlags( iid, sel ) ) )
        {
            ImGui::PopID();
            continue;
        }
        bool hov = false, held = false;
        const bool pr = ImGui::ButtonBehavior( rect, iid, &hov, &held );
        TabGroupItemNav( iid );
        if ( g.NavId == iid )
        {
            if ( i > 0 && ConsumeNavArrow( iid, ImGuiDir_Up ) )
                nav_target = i - 1;
            if ( i + 1 < total_pages && ConsumeNavArrow( iid, ImGuiDir_Down ) )
                nav_target = i + 1;
        }
        {
            char pip_label[ 24 ];
            ImFormatString( pip_label, sizeof( pip_label ), "Page %d", i + 1 );
            ReportItemInfo( iid, pip_label, CheckedFlags( sel ) );
        }
        RenderNavFocusRing( iid, rect, dot * 0.5f );
        if ( pr && current_item )
        {
            *current_item = i;
            changed       = true;
        }
        const ImU32 col_target = sel ? col_pip_sel : hov ? col_pip_hov : col_pip_rest;
        dl->AddCircleFilled( c, dot * 0.5f, AnimateColorU32( iid, col_target ), 16 );
        ImGui::PopID();
    }
    if ( nav_target >= 0 )
    {
        ImGui::PushID( nav_target );
        ImGui::SetFocusID( w->GetID( nav_target ), w );
        ImGui::PopID();
        g.NavCursorVisible = true;
    }
    EndTabGroup();
    return changed;
}

static bool RenderPagerButton( const char * btn_id, const char * name, const char * text, float sz, bool enabled, bool repeat, bool active )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( btn_id );
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + sz, pos.y + sz ) );
    ImGui::ItemSize( bb );
    const ImGuiItemFlags item_flags = repeat ? ImGuiItemFlags_ButtonRepeat : ImGuiItemFlags_None;
    if ( !ImGui::ItemAdd( bb, id, nullptr, item_flags ) )
        return false;

    bool hov = false, held = false;
    const bool pressed      = enabled ? ImGui::ButtonBehavior( bb, id, &hov, &held ) : false;
    ImFluent::ReportItemInfo( id, name, active ? ImFluent::CheckedFlags( true ) : 0 );
    const float r           = ImFluent::FluentDpx( style.ControlCornerRadius );
    const ImU32 fill_target = !enabled ? IM_COL32( 0, 0, 0, 0 ) : active ? ImFluent::ResolveAccentFillState( false, held, hov ) : ImFluent::ResolveSubtleFillState( false, held, hov );
    w->DrawList->AddRectFilled( bb.Min, bb.Max, ImFluent::AnimateColorU32( id, fill_target ), r );
    const ImU32 fg = ImFluent::GetColorU32( active ? ImFluentCol_TextOnAccentPrimary : enabled ? ImFluentCol_TextPrimary : ImFluentCol_TextDisabled );
    ImFluent::RenderCenteredText( w->DrawList, ImVec2( ( bb.Min.x + bb.Max.x ) * 0.5f, ( bb.Min.y + bb.Max.y ) * 0.5f ), ImGui::GetFontSize(), fg, text );
    if ( enabled && ImFluent::IsItemFocused( id ) )
        ImFluent::RenderNavFocusRing( bb, r );
    return pressed;
}

bool ImFluent::PagerControl( const char * id, int * current_page, int total_pages, ImFluentPagerDisplayMode display_mode, ImFluentPagerButtonVisibility first_last_visibility, ImFluentPagerButtonVisibility prev_next_visibility )
{
    if ( total_pages <= 0 || !current_page )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();

    int cur = *current_page;
    if ( cur < 0 )
        cur = 0;
    if ( cur >= total_pages )
        cur = total_pages - 1;

    bool changed = false;
    ImGui::PushID( id );
    ImGui::BeginGroup();

    const float btn_sz = FluentDpx( style.ControlHeight );
    const float gap    = FluentDpx( style.SpacingXSmall );

    struct NavBtn
    {
        const char * id;
        const char * name;
        const char * glyph;
        int target_page;
        bool enabled;
        ImFluentPagerButtonVisibility vis;
        bool is_edge_min;
    };
    auto draw_nav = [ & ]( const NavBtn & b )
    {
        if ( b.vis == ImFluentPagerButtonVisibility_Hidden )
            return;
        if ( b.vis == ImFluentPagerButtonVisibility_HiddenOnEdge && !b.enabled )
            return;
        if ( RenderPagerButton( b.id, b.name, b.glyph, btn_sz, b.enabled, g_Ctx.Actions.RepeatStepButtons, false ) )
        {
            *current_page = b.target_page;
            changed       = true;
        }
        ImGui::SameLine( 0.f, gap );
    };

    draw_nav( { "##first", "First page", ImFluentIcon_Previous, 0, cur > 0, first_last_visibility, true } );
    draw_nav( { "##prev", "Previous page", ImFluentIcon_ChevronLeft, cur - 1, cur > 0, prev_next_visibility, true } );

    if ( display_mode == ImFluentPagerDisplayMode_ButtonPanel )
    {
        const int window = ImMin( 7, total_pages );
        int start        = cur - window / 2;
        if ( start < 0 )
            start = 0;
        if ( start + window > total_pages )
            start = total_pages - window;
        for ( int i = 0; i < window; ++i )
        {
            const int page = start + i;
            ImGui::PushID( page );
            char num[ 8 ];
            ImFormatString( num, sizeof( num ), "%d", page + 1 );
            if ( RenderPagerButton( "##np", num, num, btn_sz, true, false, page == cur ) )
            {
                *current_page = page;
                changed       = true;
            }
            ImGui::SameLine( 0.f, gap );
            ImGui::PopID();
        }
    }
    else if ( display_mode == ImFluentPagerDisplayMode_ComboBox )
    {
        const int kLabelLen = 12;
        ImVector< char > labels_buf;
        ImVector< const char * > labels;
        labels_buf.resize( total_pages * kLabelLen );
        labels.resize( total_pages );
        for ( int p = 0; p < total_pages; ++p )
        {
            char * dst = labels_buf.Data + p * kLabelLen;
            ImFormatString( dst, kLabelLen, "%d", p + 1 );
            labels[ p ] = dst;
        }
        ImGui::PushItemWidth( FluentDpx( style.ControlMinWidth * 0.6f ) );
        int sel = cur;
        if ( ImFluent::ComboBox( "##pager-combo", &sel, labels.Data, total_pages ) )
        {
            *current_page = sel;
            changed       = true;
        }
        ImGui::PopItemWidth();
        ImGui::SameLine( 0.f, gap );
    }
    else
    {
        ImGui::PushItemWidth( btn_sz * 2.f );
        int v = cur + 1;
        ImFluentStackGuard g;
        g.PushStyleVar( ImGuiStyleVar_FrameRounding, FluentDpx( style.ControlCornerRadius ) );
        g.PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( FluentDpx( style.SpacingMedium ), ( btn_sz - ImGui::GetFontSize() ) * 0.5f ) );
        g.PushStyleColor( ImGuiCol_FrameBg, ImFluent::GetColorU32( ImFluentCol_ControlFillDefault ) );
        g.PushStyleColor( ImGuiCol_FrameBgHovered, ImFluent::GetColorU32( ImFluentCol_ControlFillSecondary ) );
        g.PushStyleColor( ImGuiCol_FrameBgActive, ImFluent::GetColorU32( ImFluentCol_ControlFillInputActive ) );
        ImGui::InputInt( "##pager-num", &v, 0, 0 );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            if ( v < 1 )
                v = 1;
            if ( v > total_pages )
                v = total_pages;
            if ( v - 1 != cur )
            {
                *current_page = v - 1;
                changed       = true;
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine( 0.f, FluentDpx( style.SpacingSmall ) );
        const char * total_txt;
        const char * total_end;
        ImFormatStringToTempBuffer( &total_txt, &total_end, "of %d", total_pages );
        const ImVec2 sz  = ImGui::CalcTextSize( total_txt, total_end );
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy( ImVec2( sz.x, btn_sz ) );
        ImDrawList * dl = ImGui::GetCurrentWindow()->DrawList;
        dl->AddText( ImVec2( pos.x, pos.y + ( btn_sz - sz.y ) * 0.5f ), ImFluent::GetColorU32( ImFluentCol_TextSecondary ), total_txt, total_end );
        ImGui::SameLine( 0.f, gap );
    }

    draw_nav( { "##next", "Next page", ImFluentIcon_ChevronRight, cur + 1, cur < total_pages - 1, prev_next_visibility, false } );
    draw_nav( { "##last", "Last page", ImFluentIcon_Next, total_pages - 1, cur < total_pages - 1, first_last_visibility, false } );

    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

int ImFluent::BreadcrumbBar( const char * id, const char * const items[], int items_count )
{
    int clicked = -1;
    ImFluentStackGuard g;
    g.PushID( id );
    g.BeginGroup();
    // WinUI BreadcrumbBar: one tab stop entering on the first item; Space/Enter click any item, the current one too.
    BeginTabGroup( ImGui::GetID( "##crumbs" ), ImFluentTabGroupFlags_Horizontal | ImFluentTabGroupFlags_FirstIsStop );
    // WinUI look: every item is plain primary text (the current one too), separated by a small chevron.
    ImGuiWindow * w             = ImGui::GetCurrentWindow();
    const ImFluentStyle & style = ImFluent::GetStyle();
    const float pad_y           = FluentDpx( style.SpacingXSmall );
    const float gap             = FluentDpx( style.SpacingMedium );
    const float chevron         = FluentDpx( 12.f );
    for ( int i = 0; i < items_count; ++i )
    {
        if ( !items[ i ] )
            continue;
        const bool last = ( i == items_count - 1 );
        ImFluentStackGuard item_g;
        item_g.PushID( i );
        const ImGuiID item_id = ImGui::GetID( items[ i ] );
        const ImVec2 ts       = ImGui::CalcTextSize( items[ i ], NULL, true );
        const ImVec2 pos      = w->DC.CursorPos;
        const ImRect bb( pos, ImVec2( pos.x + ts.x, pos.y + ts.y + pad_y * 2.f ) );
        ImGui::ItemSize( bb );
        if ( ImGui::ItemAdd( bb, item_id, NULL, TabGroupItemFlags( item_id, false ) ) )
        {
            bool hov = false, held = false;
            if ( ImGui::ButtonBehavior( bb, item_id, &hov, &held ) )
                clicked = i;
            TabGroupItemNav( item_id );
            ReportItemInfo( item_id, items[ i ] );
            const ImU32 col = ImFluent::GetColorU32( held ? ImFluentCol_TextTertiary : hov ? ImFluentCol_TextSecondary : ImFluentCol_TextPrimary );
            w->DrawList->AddText( ImVec2( bb.Min.x, bb.Min.y + pad_y ), col, items[ i ], ImGui::FindRenderedTextEnd( items[ i ] ) );
            RenderNavFocusRing( item_id, bb, FluentDpx( style.ControlCornerRadius ) );
        }
        if ( !last )
        {
            ImGui::SameLine( 0.f, gap );
            const ImVec2 cp = w->DC.CursorPos;
            w->DrawList->AddText( ImGui::GetFont(), chevron, ImVec2( cp.x, cp.y + ( bb.GetHeight() - chevron ) * 0.5f ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), ImFluentIcon_ChevronRight );
            ImGui::Dummy( ImVec2( chevron, bb.GetHeight() ) );
            ImGui::SameLine( 0.f, gap );
        }
    }
    EndTabGroup();
    return clicked;
}

// [SECTION] Flyout

void ImFluent::OpenFlyout( const char * id )
{
    ImGui::OpenPopup( id );
    ImGuiContext & g = *ImGui::GetCurrentContext();
    StorePopupAnchor( ImGui::GetID( id ), g.LastItemData.Rect );
}

bool ImFluent::BeginFlyout( const char * id )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImFluentStackGuard g;
    g.PushStyleColor( ImGuiCol_PopupBg, ImFluent::GetColorU32( ImFluentCol_SolidBgQuarternary ) );
    g.PushStyleColor( ImGuiCol_Border, ImFluent::GetColorU32( ImFluentCol_SurfaceStrokeFlyout ) );
    g.PushStyleVar( ImGuiStyleVar_PopupRounding, FluentDpx( style.OverlayCornerRadius ) );
    g.PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( FluentDpx( style.SpacingXLarge ), FluentDpx( style.SpacingXLarge ) ) );
    ApplyPopupAnchor( id );
    if ( !ImGui::BeginPopup( id ) )
        return false;
    g.Forget();
    return true;
}

void ImFluent::EndFlyout()
{
    if ( ImGui::IsPopupOpen( "##__noop", 0 ) )
    {
    }

    if ( ImGuiWindow * w = ImGui::GetCurrentWindow() )
    {
        const ImFluentStyle & style = ImFluent::GetStyle();
        const ImRect bb( w->Pos, ImVec2( w->Pos.x + w->Size.x, w->Pos.y + w->Size.y ) );

        ImDrawList * fg = ImGui::GetForegroundDrawList( w );
        RenderElevationShadow( fg, bb, FluentDpx( style.OverlayCornerRadius ), 4 );
    }
    ImGui::EndPopup();
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor( 2 );
}

// [SECTION] MenuFlyout

void ImFluent::OpenMenuFlyout( const char * id )
{
    ImGui::OpenPopup( id );
    ImGuiContext & g = *ImGui::GetCurrentContext();
    StorePopupAnchor( ImGui::GetID( id ), g.LastItemData.Rect );
}

bool ImFluent::BeginMenuFlyout( const char * id )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImFluentStackGuard g;
    g.PushStyleColor( ImGuiCol_PopupBg, ImFluent::GetColorU32( ImFluentCol_SolidBgQuarternary ) );
    g.PushStyleColor( ImGuiCol_Border, ImFluent::GetColorU32( ImFluentCol_SurfaceStrokeFlyout ) );
    g.PushStyleColor( ImGuiCol_HeaderHovered, ImFluent::GetColorU32( ImFluentCol_SubtleFillSecondary ) );
    g.PushStyleColor( ImGuiCol_HeaderActive, ImFluent::GetColorU32( ImFluentCol_SubtleFillTertiary ) );
    g.PushStyleVar( ImGuiStyleVar_PopupRounding, FluentDpx( style.OverlayCornerRadius ) );
    g.PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( FluentDpx( style.SpacingSmall ), FluentDpx( style.SpacingSmall ) ) );
    g.PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 0.f, 0.f ) );
    ApplyPopupAnchor( id );
    if ( !ImGui::BeginPopup( id ) )
        return false;
    g.Forget();
    return true;
}

// Invoking a menu item closes the whole menu (WinUI MenuFlyout): the open popups each begun inside the previous one.
static void CloseMenuFlyoutChain()
{
    ImGuiContext & g = *GImGui;
    int level        = -1;
    for ( int i = g.OpenPopupStack.Size - 1; i >= 0; --i )
        if ( g.OpenPopupStack[ i ].Window == g.CurrentWindow )
        {
            level = i;
            break;
        }
    if ( level < 0 )
        return;
    while ( level > 0 && g.OpenPopupStack[ level ].Window && g.OpenPopupStack[ level ].Window->ParentWindow == g.OpenPopupStack[ level - 1 ].Window )
        --level;
    ImGui::ClosePopupToLevel( level, true );
}

bool ImFluent::MenuFlyoutItem( const char * label, const char * shortcut, const char * glyph, bool selected, bool enabled )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    glyph           = ConsumePendingGlyph( glyph );
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );
    const float h               = FluentDpx( style.MenuItemHeight );
    const float min_w           = FluentDpx( style.NavPaneCompactWidth * 3.75f );
    const ImVec2 ts             = ImGui::CalcTextSize( label );
    const ImVec2 ss             = shortcut ? ImGui::CalcTextSize( shortcut ) : ImVec2( 0, 0 );
    const ImVec2 pos            = w->DC.CursorPos;
    const float W               = ImMax( min_w, ts.x + ss.x + FluentDpx( style.AppBarButtonWidth ) );
    const ImRect bb( pos, ImVec2( pos.x + W, pos.y + h ) );
    ImGui::ItemSize( bb );
    // A disabled item carries ImGuiItemFlags_Disabled so keyboard navigation skips it, as in WinUI.
    if ( !enabled )
        ImGui::PushItemFlag( ImGuiItemFlags_Disabled, true );
    const bool added = ImGui::ItemAdd( bb, id, NULL, ImGuiItemFlags_NoTabStop ); // WinUI menus ignore Tab
    if ( !enabled )
        ImGui::PopItemFlag();
    if ( !added )
        return false;
    bool hov = false, held = false;
    const bool pressed    = enabled ? ImGui::ButtonBehavior( bb, id, &hov, &held ) : false;
    ReportItemInfo( id, label );
    ImDrawList * dl       = w->DrawList;
    const float r         = FluentDpx( style.ControlCornerRadius );
    const ImU32 mf_target = ResolveSubtleFillState( selected, held, hov || IsItemFocused( id ) );
    dl->AddRectFilled( bb.Min, bb.Max, AnimateColorU32( id, mf_target ), r );
    RenderNavFocusRing( id, bb, r );
    const float cy = ( bb.Min.y + bb.Max.y - ts.y ) * 0.5f;
    if ( glyph )
        dl->AddText( ImVec2( bb.Min.x + FluentDpx( style.SpacingLarge ), cy ), ImFluent::GetColorU32( enabled ? ImFluentCol_TextPrimary : ImFluentCol_TextDisabled ), glyph );
    dl->AddText( ImVec2( bb.Min.x + FluentDpx( style.NavItemHeight ), cy ), ImFluent::GetColorU32( enabled ? ImFluentCol_TextPrimary : ImFluentCol_TextDisabled ), label );
    if ( shortcut )
        dl->AddText( ImVec2( bb.Max.x - FluentDpx( style.SpacingLarge ) - ss.x, cy ), ImFluent::GetColorU32( ImFluentCol_TextSecondary ), shortcut );
    if ( pressed )
        CloseMenuFlyoutChain();
    return pressed;
}

bool ImFluent::ToggleMenuFlyoutItem( const char * label, bool * v, const char * shortcut, bool enabled )
{
    const bool selected = v && *v;
    const char * mark   = selected ? ImFluentIcon_CheckMark : NULL;
    const bool pressed  = MenuFlyoutItem( label, shortcut, mark, false, enabled );
    if ( pressed && v )
        *v = !*v;
    ReportItemInfo( ImGui::GetCurrentContext()->LastItemData.ID, label, CheckedFlags( v && *v ) );
    return pressed;
}

bool ImFluent::RadioMenuFlyoutItem( const char * label, int * v, int v_button, const char * shortcut, bool enabled )
{
    const bool selected = v && ( *v == v_button );
    const char * mark   = selected ? ImFluentIcon_CheckMark : NULL;
    const bool pressed  = MenuFlyoutItem( label, shortcut, mark, false, enabled );
    if ( pressed && v )
        *v = v_button;
    ReportItemInfo( ImGui::GetCurrentContext()->LastItemData.ID, label, CheckedFlags( v && *v == v_button ) );
    return pressed;
}

bool ImFluent::BeginMenuFlyoutSubItem( const char * label, const char * glyph, bool enabled )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    glyph                       = ConsumePendingGlyph( glyph );
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );
    const float h               = FluentDpx( style.MenuItemHeight );
    const float min_w           = FluentDpx( style.NavPaneCompactWidth * 3.75f );
    const ImVec2 ts             = ImGui::CalcTextSize( label );
    const ImVec2 pos            = w->DC.CursorPos;
    const float W               = ImMax( min_w, ts.x + FluentDpx( style.AppBarButtonWidth ) );
    const ImRect bb( pos, ImVec2( pos.x + W, pos.y + h ) );
    ImGui::ItemSize( bb );
    if ( !enabled )
        ImGui::PushItemFlag( ImGuiItemFlags_Disabled, true );
    const bool added = ImGui::ItemAdd( bb, id, NULL, ImGuiItemFlags_NoTabStop ); // WinUI menus ignore Tab
    if ( !enabled )
        ImGui::PopItemFlag();
    if ( !added )
        return false;

    // Enter, Space or Right open the submenu with focus inside; Left or Escape close it. Hover opens it only while the mouse
    // is in use (NoHoveredOnFocus, NavHighlightItemUnderNav), so Escape is not undone next frame. Same rule as ImGui's BeginMenu.
    bool hov = false, held = false;
    const bool pressed     = enabled ? ImGui::ButtonBehavior( bb, id, &hov, &held, ImGuiButtonFlags_PressedOnClick | ImGuiButtonFlags_NoHoveredOnFocus ) : false;
    const bool open        = ImGui::IsPopupOpen( label, ImGuiPopupFlags_None );
    const bool nav_open    = enabled && !open && ConsumeNavArrow( id, ImGuiDir_Right );
    const bool mouse_hover = hov && !GImGui->NavHighlightItemUnderNav;
    if ( pressed || nav_open || ( mouse_hover && !open ) )
    {
        ImGui::OpenPopup( label );
    }
    ReportItemInfo( id, label, OpenedFlags( open ) );

    ImDrawList * dl        = w->DrawList;
    const float r          = FluentDpx( style.ControlCornerRadius );
    const ImU32 mfs_target = ResolveSubtleFillState( open, held, hov || IsItemFocused( id ) );
    dl->AddRectFilled( bb.Min, bb.Max, AnimateColorU32( id, mfs_target ), r );
    RenderNavFocusRing( id, bb, r );
    const float cy = ( bb.Min.y + bb.Max.y - ts.y ) * 0.5f;
    if ( glyph )
        dl->AddText( ImVec2( bb.Min.x + FluentDpx( style.SpacingLarge ), cy ), ImFluent::GetColorU32( enabled ? ImFluentCol_TextPrimary : ImFluentCol_TextDisabled ), glyph );
    dl->AddText( ImVec2( bb.Min.x + FluentDpx( style.NavItemHeight ), cy ), ImFluent::GetColorU32( enabled ? ImFluentCol_TextPrimary : ImFluentCol_TextDisabled ), label );
    RenderChevron( dl, ImVec2( bb.Max.x - FluentDpx( style.SpacingLarge ), ( bb.Min.y + bb.Max.y ) * 0.5f ), ImGuiDir_Right, ImFluent::GetColorU32( enabled ? ImFluentCol_TextSecondary : ImFluentCol_TextDisabled ), FluentDpx( style.ChevronGlyphSize ) );

    ImFluentStackGuard g;
    g.PushStyleColor( ImGuiCol_PopupBg, ImFluent::GetColorU32( ImFluentCol_SolidBgQuarternary ) );
    g.PushStyleColor( ImGuiCol_Border, ImFluent::GetColorU32( ImFluentCol_SurfaceStrokeFlyout ) );
    g.PushStyleColor( ImGuiCol_HeaderHovered, ImFluent::GetColorU32( ImFluentCol_SubtleFillSecondary ) );
    g.PushStyleColor( ImGuiCol_HeaderActive, ImFluent::GetColorU32( ImFluentCol_SubtleFillTertiary ) );
    g.PushStyleVar( ImGuiStyleVar_PopupRounding, FluentDpx( style.OverlayCornerRadius ) );
    g.PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( FluentDpx( style.SpacingSmall ), FluentDpx( style.SpacingSmall ) ) );
    g.PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 0.f, 0.f ) );
    ImGui::SetNextWindowPos( ImVec2( bb.Max.x, bb.Min.y ), ImGuiCond_Appearing );
    if ( ImGui::BeginPopup( label ) )
    {
        g.Forget();
        return true;
    }
    return false;
}

void ImFluent::EndMenuFlyoutSubItem()
{
    // Left closes this submenu when it has keyboard focus; ImGui then restores focus to the parent item.
    ImGuiContext & g = *GImGui;
    if ( g.NavWindow == ImGui::GetCurrentWindow() && g.NavMoveDir == ImGuiDir_Left && g.NavMoveSubmitted )
    {
        ImGui::NavMoveRequestCancel();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    ImGui::PopStyleVar( 3 );
    ImGui::PopStyleColor( 4 );
}

void ImFluent::MenuFlyoutSeparator()
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImGuiWindow * w             = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return;
    const ImVec2 pos = w->DC.CursorPos;
    const float W    = ImGui::GetContentRegionAvail().x;
    const float pad  = FluentDpx( style.SpacingSmall );
    const ImRect bb( ImVec2( pos.x + pad, pos.y + 2.f ), ImVec2( pos.x + W - pad, pos.y + 3.f ) );
    ImGui::ItemSize( ImVec2( W, FluentDpx( style.SpacingMedium - 2.f ) ) );
    if ( !ImGui::ItemAdd( bb, 0 ) )
        return;
    w->DrawList->AddRectFilled( bb.Min, bb.Max, ImFluent::GetColorU32( ImFluentCol_DividerStrokeDefault ) );
}

void ImFluent::EndMenuFlyout()
{
    ImGui::EndPopup();
    ImGui::PopStyleVar( 3 );
    ImGui::PopStyleColor( 4 );
}

// [SECTION] ContentDialog

void ImFluent::OpenContentDialog( const char * id )
{
    ImGui::OpenPopup( id );
}

bool ImFluent::BeginContentDialog( const char * id, const char * title )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImFluentStackGuard g;
    g.PushStyleColor( ImGuiCol_PopupBg, ImFluent::GetColorU32( ImFluentCol_SolidBgBase ) );
    g.PushStyleColor( ImGuiCol_Border, ImFluent::GetColorU32( ImFluentCol_SurfaceStrokeDefault ) );
    g.PushStyleColor( ImGuiCol_ModalWindowDimBg, ImFluent::GetColorU32( ImFluentCol_SmokeFill ) );
    g.PushStyleVar( ImGuiStyleVar_PopupRounding, FluentDpx( style.OverlayCornerRadius ) );
    g.PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( FluentDpx( style.SpacingXXLarge ), FluentDpx( style.SpacingXXLarge ) ) );
    const ImVec2 vp_size = ImGui::GetMainViewport()->WorkSize;
    const ImVec2 vp_pos  = ImGui::GetMainViewport()->WorkPos;
    const float dlg_w    = FluentDpx( style.ControlMinWidth * 4.f );
    ImGui::SetNextWindowPos( ImVec2( vp_pos.x + vp_size.x * 0.5f, vp_pos.y + vp_size.y * 0.5f ), ImGuiCond_Always, ImVec2( 0.5f, 0.5f ) );
    // Lock width, let height auto-fit. ImGuiWindowFlags_AlwaysAutoResize is what
    // makes ImGui hide the window for one frame to measure content — without it
    // the first frame renders at height 0 (cursor advances past a 0-height area),
    // which makes EndContentDialog's GetContentRegionAvail().x button layout and
    // the pivot-0.5 vertical centering settle on frame 2 instead of frame 1.
    ImGui::SetNextWindowSizeConstraints( ImVec2( dlg_w, 0.f ), ImVec2( dlg_w, FLT_MAX ) );
    if ( !ImGui::BeginPopupModal( id, NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize ) )
        return false;
    g.Forget();
    if ( title )
        ImFluent::TextBlock( title, ImFluentTextStyle_Subtitle );
    ImGui::Dummy( ImVec2( 0, FluentDpx( style.SpacingMedium ) ) );
    return true;
}

int ImFluent::EndContentDialog( const char * primary, const char * secondary, const char * close_text, ImFluentContentDialogButton default_button )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    int result                  = 0;
    ImGui::Dummy( ImVec2( 0, FluentDpx( style.SpacingXLarge ) ) );
    const float gap = FluentDpx( style.SpacingMedium );
    const float w   = ImGui::GetContentRegionAvail().x;
    int btn_count   = ( primary ? 1 : 0 ) + ( secondary ? 1 : 0 ) + ( close_text ? 1 : 0 );
    if ( btn_count == 0 )
        btn_count = 1;
    const float btn_w = ( w - gap * ( btn_count - 1 ) ) / ( float )btn_count;
    const ImVec2 btn_sz( btn_w, FluentDpx( style.ControlHeight ) );

    auto run = [ & ]( const char * label, ImFluentContentDialogButton kind, int code )
    {
        const bool use_accent = ( default_button == kind );
        const bool clicked    = use_accent ? ImFluent::AccentButton( label, btn_sz ) : ImFluent::Button( label, btn_sz );
        if ( use_accent )
            ImGui::SetItemDefaultFocus();
        if ( clicked )
        {
            result = code;
            ImGui::CloseCurrentPopup();
        }
    };

    int placed = 0;
    if ( primary )
    {
        run( primary, ImFluentContentDialogButton_Primary, 1 );
        if ( ++placed < btn_count )
            ImGui::SameLine( 0.f, gap );
    }
    if ( secondary )
    {
        run( secondary, ImFluentContentDialogButton_Secondary, 2 );
        if ( ++placed < btn_count )
            ImGui::SameLine( 0.f, gap );
    }
    if ( close_text )
    {
        run( close_text, ImFluentContentDialogButton_Close, 3 );
    }
    // Escape works like the close button (WinUI ContentDialog). ImGui does not close modals on Escape by itself.
    if ( close_text && result == 0 && ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && IsActionPressed( g_Ctx.Actions.CloseDialog ) )
    {
        result = 3;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor( 3 );
    return result;
}

// [SECTION] InfoBar / InfoBadge

bool ImFluent::InfoBar( ImFluentInfoSeverity sev, const char * title, const char * msg, bool * is_open, const char * glyph_override, bool show_icon, const char * action_label )
{
    if ( is_open && !*is_open )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();

    ImU32 sevCol;
    const char * glyph;
    switch ( sev )
    {
        case ImFluentInfoSeverity_Success:
            sevCol = ImFluent::GetColorU32( ImFluentCol_SystemFillSuccess );
            glyph  = "\xEE\xA1\x9F";
            break;
        case ImFluentInfoSeverity_Warning:
            sevCol = ImFluent::GetColorU32( ImFluentCol_SystemFillCaution );
            glyph  = "\xEE\x9D\xA3";
            break;
        case ImFluentInfoSeverity_Critical:
            sevCol = ImFluent::GetColorU32( ImFluentCol_SystemFillCritical );
            glyph  = "\xEE\xA3\x83";
            break;
        case ImFluentInfoSeverity_Informational:
        default:
            sevCol = ImFluent::GetColorU32( ImFluentCol_SystemFillNeutral );
            glyph  = "\xEE\xA5\x86";
            break;
    }
    if ( glyph_override )
        glyph = glyph_override;

    const ImU32 bgTint = ( sev == ImFluentInfoSeverity_Informational ) ? 0u : ( ( sevCol & ~IM_COL32_A_MASK ) | ( 36u << IM_COL32_A_SHIFT ) );

    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;

    const bool has_action       = ( action_label && *action_label );
    const float font_h          = ImGui::GetFontSize();
    const float W               = ImGui::GetContentRegionAvail().x;
    const float pad_x           = FluentDpx( style.SpacingXLarge );
    const float pad_y           = FluentDpx( style.SpacingLarge );
    const float icon_col        = show_icon ? FluentDpx( style.SpinButtonWidth ) : 0.f;
    const float close_w         = is_open ? FluentDpx( style.ControlHeight ) : 0.f;
    const float action_pad      = has_action ? FluentDpx( style.SpacingMedium ) : 0.f;
    const ImVec2 action_text_sz = has_action ? ImGui::CalcTextSize( action_label ) : ImVec2( 0, 0 );
    const float action_w        = has_action ? ( action_text_sz.x + FluentDpx( style.SpacingLarge ) * 2.f ) : 0.f;
    const float text_x          = pad_x + icon_col;
    const float text_avail      = ImMax( 1.f, W - text_x - close_w - action_w - action_pad - pad_x );

    const ImVec2 title_sz = title && *title ? ImGui::CalcTextSize( title, NULL, false, text_avail ) : ImVec2( 0, 0 );
    const ImVec2 msg_sz   = msg && *msg ? ImGui::CalcTextSize( msg, NULL, false, text_avail ) : ImVec2( 0, 0 );
    const float gap       = ( title_sz.y > 0 && msg_sz.y > 0 ) ? FluentDpx( style.SpacingXSmall ) : 0.f;
    const float text_h    = title_sz.y + gap + msg_sz.y;
    const float min_h     = FluentDpx( style.NavPaneCompactWidth );
    const float h         = ImMax( min_h, text_h + pad_y * 2.f );

    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + W, pos.y + h ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, 0 ) )
        return false;

    ImDrawList * dl = w->DrawList;
    const float r   = FluentDpx( style.OverlayCornerRadius );

    dl->AddRectFilled( bb.Min, bb.Max, ImFluent::GetColorU32( ImFluentCol_CardBgDefault ), r );
    if ( bgTint )
        dl->AddRectFilled( bb.Min, bb.Max, bgTint, r );
    ImFluent::StrokeRect( dl, bb.Min, bb.Max, ImFluent::GetColorU32( ImFluentCol_CardStrokeDefault ), r );

    dl->AddRectFilled( bb.Min, ImVec2( bb.Min.x + FluentDpx( style.SeverityBarThickness ), bb.Max.y ), sevCol, r );

    if ( show_icon )
        dl->AddText( ImVec2( bb.Min.x + FluentDpx( style.SpacingXLarge ), bb.Min.y + pad_y ), sevCol, glyph );

    const float text_left = bb.Min.x + text_x;
    float text_y          = bb.Min.y + pad_y;

    if ( title_sz.y > 0 )
    {
        dl->AddText( ImGui::GetFont(), font_h, ImVec2( text_left, text_y ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), title, NULL, text_avail );
        text_y += title_sz.y + gap;
    }
    if ( msg_sz.y > 0 )
    {
        dl->AddText( ImGui::GetFont(), font_h, ImVec2( text_left, text_y ), ImFluent::GetColorU32( ImFluentCol_TextSecondary ), msg, NULL, text_avail );
    }

    bool action_clicked = false;
    if ( has_action )
    {
        const float btn_h  = FluentDpx( style.ControlHeight );
        const float by     = bb.Min.y + ( h - btn_h ) * 0.5f;
        const float bx_max = bb.Max.x - close_w - ( close_w > 0.f ? FluentDpx( style.SpacingXSmall ) : pad_x );
        const ImRect btn_bb( ImVec2( bx_max - action_w, by ), ImVec2( bx_max, by + btn_h ) );
        const ImGuiID aid = is_open ? ( w->GetID( ( const void * )is_open ) ^ 0x9E3779B9u ) : w->GetID( ( const void * )action_label );
        bool ahov = false, aheld = false;
        if ( ImGui::ItemAdd( btn_bb, aid ) )
        {
            action_clicked = ImGui::ButtonBehavior( btn_bb, aid, &ahov, &aheld );
            ReportItemInfo( aid, action_label );
        }
        const ImU32 fill_target = ResolveControlFillState( false, aheld, ahov );
        const float r_btn       = FluentDpx( style.ControlCornerRadius );
        FillInsideBorder( dl, btn_bb.Min, btn_bb.Max, ImFluent::AnimateColorU32( aid, fill_target ), r_btn );
        RenderElevationBorder( dl, btn_bb, r_btn, ImFluent::GetColorU32( ImFluentCol_ControlStrokeDefault ), ImFluent::GetColorU32( ImFluentCol_ElevationControlTop ), ImFluent::GetColorU32( ImFluentCol_ElevationControlBottom ), 1.f );
        const float ax = btn_bb.Min.x + ( btn_bb.GetWidth() - action_text_sz.x ) * 0.5f;
        const float ay = btn_bb.Min.y + ( btn_bb.GetHeight() - action_text_sz.y ) * 0.5f;
        dl->AddText( ImVec2( ax, ay ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), action_label );
        RenderNavFocusRing( aid, btn_bb, r_btn );
    }

    if ( is_open )
    {
        const float close_sz = FluentDpx( style.SpacingXXLarge );
        const float cx_min   = bb.Max.x - close_sz - FluentDpx( style.SpacingMedium );
        const float cy_min   = bb.Min.y + FluentDpx( style.SpacingMedium );
        const ImRect close_bb( ImVec2( cx_min, cy_min ), ImVec2( cx_min + close_sz, cy_min + close_sz ) );
        const ImGuiID cid = w->GetID( ( const void * )is_open );
        bool hov = false, held = false;
        if ( ImGui::ItemAdd( close_bb, cid ) )
        {
            const bool pr = ImGui::ButtonBehavior( close_bb, cid, &hov, &held );
            ReportItemInfo( cid, "Close" );
            if ( pr )
                *is_open = false;
        }
        const ImU32 close_fill = ImFluent::AnimateColorU32( cid, ResolveSubtleFillState( false, held, hov ) );
        dl->AddRectFilled( close_bb.Min, close_bb.Max, close_fill, FluentDpx( style.ControlCornerRadius ) );
        const ImVec2 cc( ( close_bb.Min.x + close_bb.Max.x ) * 0.5f, ( close_bb.Min.y + close_bb.Max.y ) * 0.5f );
        const float L = FluentDpx( style.ChevronGlyphSize + 1.f );
        dl->AddLine( ImVec2( cc.x - L, cc.y - L ), ImVec2( cc.x + L, cc.y + L ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), FluentDpx( style.StrokeMedium ) );
        dl->AddLine( ImVec2( cc.x - L, cc.y + L ), ImVec2( cc.x + L, cc.y - L ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), FluentDpx( style.StrokeMedium ) );
        RenderNavFocusRing( cid, close_bb, FluentDpx( style.ControlCornerRadius ) );
    }
    return action_clicked;
}

void ImFluent::InfoBadge( int count, const char * glyph )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const bool dot              = ( count < 0 && !glyph );
    if ( dot )
    {
        const float D    = FluentDpx( style.SpacingMedium );
        const ImVec2 pos = w->DC.CursorPos;
        const ImRect bb( pos, ImVec2( pos.x + D, pos.y + D ) );
        ImGui::ItemSize( bb );
        if ( !ImGui::ItemAdd( bb, 0 ) )
            return;
        w->DrawList->AddCircleFilled( ImVec2( pos.x + D * 0.5f, pos.y + D * 0.5f ), D * 0.5f, ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), 16 );
        return;
    }
    const char * text     = glyph;
    const char * text_end = NULL;
    if ( !glyph && count >= 0 )
    {
        if ( count > 99 )
            ImFormatStringToTempBuffer( &text, &text_end, "99+" );
        else
            ImFormatStringToTempBuffer( &text, &text_end, "%d", count );
    }
    if ( !text )
        text = "";
    char txt_buf[ 16 ];
    if ( text_end )
    {
        const size_t n    = ( size_t )( text_end - text );
        const size_t copy = n < sizeof( txt_buf ) - 1 ? n : sizeof( txt_buf ) - 1;
        memcpy( txt_buf, text, copy );
        txt_buf[ copy ] = 0;
        text            = txt_buf;
    }
    const ImVec2 ts   = ImGui::CalcTextSize( text );
    const float pad_x = FluentDpx( style.SpacingMedium - 2.f );
    const float h     = FluentDpx( style.BadgeHeight );
    const ImVec2 pos  = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + ImMax( ts.x + pad_x * 2.f, h ), pos.y + h ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, 0 ) )
        return;
    w->DrawList->AddRectFilled( bb.Min, bb.Max, ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), h * 0.5f );
    ImFluent::RenderCenteredText( w->DrawList, ImVec2( ( bb.Min.x + bb.Max.x ) * 0.5f, ( bb.Min.y + bb.Max.y ) * 0.5f ), ImGui::GetFontSize(), ImFluent::GetColorU32( ImFluentCol_TextOnAccentPrimary ), text );
}

// [SECTION] TeachingTip

void ImFluent::OpenTeachingTip( const char * id )
{
    ImGui::OpenPopup( id );
    ImGuiContext & g = *ImGui::GetCurrentContext();
    StorePopupAnchor( ImGui::GetID( id ), g.LastItemData.Rect );
}

bool ImFluent::BeginTeachingTip( const char * id, const char * title, ImFluentTeachingTipPlacement placement )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImFluentStackGuard g;
    g.PushStyleColor( ImGuiCol_PopupBg, ImFluent::GetColorU32( ImFluentCol_SolidBgQuarternary ) );
    g.PushStyleColor( ImGuiCol_Border, ImFluent::GetColorU32( ImFluentCol_SurfaceStrokeFlyout ) );
    g.PushStyleVar( ImGuiStyleVar_PopupRounding, FluentDpx( style.OverlayCornerRadius ) );
    g.PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( FluentDpx( style.SpacingXLarge ), FluentDpx( style.SpacingLarge ) ) );

    if ( ImGui::IsPopupOpen( id, ImGuiPopupFlags_None ) )
    {
        const ImGuiID pid     = ImGui::GetID( id );
        const ImRect * anchor = FindPopupAnchor( pid );
        if ( anchor )
        {
            const float gap = FluentDpx( style.SpacingMedium );
            ImVec2 pos;
            ImVec2 pivot( 0.f, 0.f );
            switch ( placement )
            {
                case ImFluentTeachingTipPlacement_Top:
                    pos   = ImVec2( ( anchor->Min.x + anchor->Max.x ) * 0.5f, anchor->Min.y - gap );
                    pivot = ImVec2( 0.5f, 1.f );
                    break;
                case ImFluentTeachingTipPlacement_Left:
                    pos   = ImVec2( anchor->Min.x - gap, ( anchor->Min.y + anchor->Max.y ) * 0.5f );
                    pivot = ImVec2( 1.f, 0.5f );
                    break;
                case ImFluentTeachingTipPlacement_Right:
                    pos   = ImVec2( anchor->Max.x + gap, ( anchor->Min.y + anchor->Max.y ) * 0.5f );
                    pivot = ImVec2( 0.f, 0.5f );
                    break;
                case ImFluentTeachingTipPlacement_Bottom:
                default:
                    pos   = ImVec2( ( anchor->Min.x + anchor->Max.x ) * 0.5f, anchor->Max.y + gap );
                    pivot = ImVec2( 0.5f, 0.f );
                    break;
            }
            ImGui::SetNextWindowPos( pos, ImGuiCond_Appearing, pivot );
        }
    }

    if ( !ImGui::BeginPopup( id ) )
        return false;
    g.Forget();
    if ( title && *title )
    {
        TextBlock( title, ImFluentTextStyle_BodyStrong );
        ImGui::Dummy( ImVec2( 0.f, FluentDpx( style.SpacingXSmall ) ) );
    }
    return true;
}

void ImFluent::EndTeachingTip()
{
    ImGui::EndPopup();
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor( 2 );
}

// [SECTION] TitleBar

bool ImFluent::BeginTitleBar( const char * title, float height )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    const float ctrl_h          = FluentDpx( style.ControlHeight );
    const float h               = ImMax( ctrl_h, ( height > 0.f ) ? height : FluentDpx( style.TitleBarHeight ) );
    const float pad_y           = ( h - ctrl_h ) * 0.5f;
    ImFluentStackGuard g;
    g.PushStyleColor( ImGuiCol_ChildBg, ImFluent::GetColorU32( ImFluentCol_SolidBgBase ) );
    g.PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( FluentDpx( style.SpacingMedium ), pad_y ) );
    g.PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( FluentDpx( style.SpacingMedium ), 0.f ) );
    if ( !ImGui::BeginChild( "##fluent-titlebar", ImVec2( 0, h ), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ) )
    {
        ImGui::EndChild();
        return false;
    }
    g.Forget();
    if ( title && *title )
    {
        ImFluent::PushFont( ImFluentTextStyle_Body );
        const float shift = ImMax( 0.f, ( ctrl_h - ImGui::GetFontSize() ) * 0.5f );
        ImGui::SetCursorPosY( ImGui::GetCursorPosY() + shift );
        ImGui::TextUnformatted( title );
        ImFluent::PopFont();
        ImGui::SameLine();
    }
    return true;
}

void ImFluent::EndTitleBar()
{
    ImGui::EndChild();
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor();
}

static bool TitleBarChromeButton( const char * id, const char * name, const char * glyph, bool enabled )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const float bar_h           = ImGui::GetWindowSize().y;
    const float btn_w           = ImFluent::FluentDpx( style.NavItemHeight );
    const ImGuiID iid           = w->GetID( id );
    const float top_y           = w->Pos.y;
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( ImVec2( pos.x, top_y ), ImVec2( pos.x + btn_w, top_y + bar_h ) );
    ImGui::ItemSize( ImVec2( btn_w, ImGui::GetFontSize() ) );
    if ( !ImGui::ItemAdd( bb, iid ) )
        return false;
    bool hov = false, held = false;
    const bool pressed = enabled ? ImGui::ButtonBehavior( bb, iid, &hov, &held ) : false;
    ImFluent::ReportItemInfo( iid, name );
    ImDrawList * dl    = w->DrawList;
    const float r      = ImFluent::FluentDpx( style.ControlCornerRadius );
    if ( enabled )
    {
        const ImU32 fill_target = ImFluent::ResolveSubtleFillState( false, held, hov );
        dl->AddRectFilled( bb.Min, bb.Max, ImFluent::AnimateColorU32( iid, fill_target ), r );
    }
    const ImU32 textCol = ImFluent::GetColorU32( enabled ? ImFluentCol_TextPrimary : ImFluentCol_TextDisabled );
    // WinUI title bar icons are 16 dp.
    const float glyph_size = ImFluent::FluentDpx( 16.f );
    const ImVec2 ts        = ImGui::GetFont()->CalcTextSizeA( glyph_size, FLT_MAX, 0.f, glyph );
    dl->AddText( ImGui::GetFont(), glyph_size, ImVec2( bb.Min.x + ( btn_w - ts.x ) * 0.5f, bb.Min.y + ( bar_h - ts.y ) * 0.5f ), textCol, glyph );
    if ( enabled )
        ImFluent::RenderNavFocusRing( iid, bb, r );
    ImGui::SameLine();
    return pressed;
}

bool ImFluent::TitleBarBackButton( bool enabled, bool visible )
{
    if ( !visible )
        return false;
    return TitleBarChromeButton( "##tb-back", "Back", ImFluentIcon_Back, enabled );
}

bool ImFluent::TitleBarPaneToggleButton( bool enabled, bool visible )
{
    if ( !visible )
        return false;
    return TitleBarChromeButton( "##tb-toggle", "Toggle pane", ImFluentIcon_GlobalNavButton, enabled );
}

static void TitleBarVerticallyCenterCursor()
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    const float ctrl_h          = ImFluent::FluentDpx( style.ControlHeight );
    const float bar_h           = ImGui::GetWindowSize().y;
    const float pad_y           = ImMax( 0.f, ( bar_h - ctrl_h ) * 0.5f );
    const float shift           = ImMax( 0.f, ( ctrl_h - ImGui::GetFontSize() ) * 0.5f );
    const float top             = ImGui::GetWindowPos().y + pad_y + shift;
    ImGui::SetCursorScreenPos( ImVec2( ImGui::GetCursorScreenPos().x, top ) );
}

void ImFluent::TitleBarIcon( const char * glyph )
{
    if ( !glyph || !*glyph )
        return;
    TitleBarVerticallyCenterCursor();
    ImGui::TextUnformatted( glyph );
    ImGui::SameLine();
}

void ImFluent::TitleBarTitle( const char * text )
{
    if ( !text || !*text )
        return;
    ImFluent::PushFont( ImFluentTextStyle_Body );
    TitleBarVerticallyCenterCursor();
    ImGui::TextUnformatted( text );
    ImFluent::PopFont();
    ImGui::SameLine();
}

void ImFluent::TitleBarSubtitle( const char * subtitle )
{
    if ( !subtitle || !*subtitle )
        return;
    ImGui::SameLine();
    ImFluent::PushFont( ImFluentTextStyle_Caption );
    TitleBarVerticallyCenterCursor();
    ImGui::PushStyleColor( ImGuiCol_Text, ImFluent::GetColorU32( ImFluentCol_TextSecondary ) );
    ImGui::TextUnformatted( subtitle );
    ImGui::PopStyleColor();
    ImFluent::PopFont();
}

// [SECTION] MenuBar

bool ImFluent::BeginMenuBar()
{
    ImGui::PushStyleColor( ImGuiCol_MenuBarBg, ImFluent::GetColorU32( ImFluentCol_LayerFillDefault ) );
    if ( !ImGui::BeginMainMenuBar() )
    {
        ImGui::PopStyleColor();
        return false;
    }
    g_Ctx.MenuBarStack.push_back( ImFluentContext::MenuBarState() );
    return true;
}

// WinUI MenuBar in the page: one tab stop, Left/Right wrap, Enter/Space/Down open; while open, Left/Right or hover switch menus.
static const ImGuiID MENUBAR_KEY_OPEN = 0x4D42u; // Index of the open menu, in the window storage

bool ImFluent::BeginMenuBar( const char * id )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    ImGui::PushID( id );
    ImGui::BeginGroup();
    ImFluentContext::MenuBarState mb;
    mb.InPage        = true;
    mb.Id            = ImGui::GetID( "##menubar" );
    mb.PrevOpenIndex = ImGui::GetStateStorage()->GetInt( mb.Id ^ MENUBAR_KEY_OPEN, -1 );
    g_Ctx.MenuBarStack.push_back( mb );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 0.f, 0.f ) );
    BeginTabGroup( mb.Id, ImFluentTabGroupFlags_Horizontal | ImFluentTabGroupFlags_Wrap );
    return true;
}

bool ImFluent::BeginMenuBarItem( const char * label )
{
    IM_ASSERT( !g_Ctx.MenuBarStack.empty() && g_Ctx.MenuBarStack.back().InPage && "BeginMenuBarItem() needs BeginMenuBar( id )" );
    ImFluentContext::MenuBarState & mb = g_Ctx.MenuBarStack.back();
    ImGuiContext & g                   = *GImGui;
    ImGuiWindow * w                    = g.CurrentWindow;
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( label );
    char popup_name[ 80 ];
    ImFormatString( popup_name, sizeof( popup_name ), "##menubar-%s", label );
    const ImGuiID popup_id = w->GetID( popup_name );

    const float pad_x = FluentDpx( style.SpacingLarge );
    const ImVec2 ts   = ImGui::CalcTextSize( label, NULL, true );
    const ImVec2 pos  = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + ts.x + pad_x * 2.f, pos.y + FluentDpx( style.ControlHeight ) ) );
    ImGui::ItemSize( bb );
    const int index = mb.Count;
    if ( mb.Count < ImFluentContext::MenuBarState::MaxItems )
    {
        mb.ItemIds[ mb.Count ]  = id;
        mb.PopupIds[ mb.Count ] = popup_id;
        mb.Rects[ mb.Count ]    = bb;
        ++mb.Count;
    }
    if ( !ImGui::ItemAdd( bb, id, NULL, TabGroupItemFlags( id, false ) ) )
    {
        ImGui::SameLine();
        return false;
    }
    bool hovered = false, held = false;
    const bool pressed  = ImGui::ButtonBehavior( bb, id, &hovered, &held, ImGuiButtonFlags_PressedOnClick );
    const bool key_open = ConsumeNavArrow( id, ImGuiDir_Down );
    TabGroupItemNav( id );
    const bool open     = ImGui::IsPopupOpen( popup_id, ImGuiPopupFlags_None );
    const bool was_open = mb.PrevOpenIndex == index;
    // A click on the open menu's own item closes it: ImGui already closed the popup on the click outside it.
    bool want_open      = ( pressed && !open && !was_open ) || ( key_open && !open );
    if ( !open && mb.PrevOpenIndex >= 0 && !was_open && IsMouseOverWindowRect( bb, ImGuiHoveredFlags_AllowWhenBlockedByPopup ) && ( g.IO.MouseDelta.x != 0.f || g.IO.MouseDelta.y != 0.f ) )
        want_open = true;
    if ( want_open )
    {
        ImGui::ClosePopupsOverWindow( w, false );
        StorePopupAnchor( popup_id, bb );
        ImGui::OpenPopupEx( popup_id, ImGuiPopupFlags_None );
    }
    ReportItemInfo( id, label, OpenedFlags( open || want_open ) );
    ImDrawList * dl = w->DrawList;
    const float r   = FluentDpx( style.ControlCornerRadius );
    dl->AddRectFilled( bb.Min, bb.Max, AnimateColorU32( id, ResolveSubtleFillState( open, held, hovered ) ), r );
    dl->AddText( ImVec2( bb.Min.x + pad_x, ( bb.Min.y + bb.Max.y - ts.y ) * 0.5f ), ImFluent::GetColorU32( ImFluentCol_TextPrimary ), label, ImGui::FindRenderedTextEnd( label ) );
    RenderNavFocusRing( id, bb, r );
    ImGui::SameLine();

    if ( !BeginMenuFlyout( popup_name ) )
        return false;
    mb.OpenIndex = index;
    return true;
}

void ImFluent::EndMenuBarItem()
{
    ImFluentContext::MenuBarState & mb = g_Ctx.MenuBarStack.back();
    ImGuiContext & g                   = *GImGui;
    // Left/Right on this menu (a submenu item consumes them first) open the neighbouring menu.
    if ( g.NavWindow == g.CurrentWindow && g.NavMoveSubmitted && ( g.NavMoveDir == ImGuiDir_Left || g.NavMoveDir == ImGuiDir_Right ) )
    {
        mb.SwitchDir = g.NavMoveDir == ImGuiDir_Left ? -1 : 1;
        ImGui::NavMoveRequestCancel();
    }
    EndMenuFlyout();
}

void ImFluent::EndMenuBar()
{
    IM_ASSERT( !g_Ctx.MenuBarStack.empty() && "EndMenuBar() without BeginMenuBar()" );
    const ImFluentContext::MenuBarState mb = g_Ctx.MenuBarStack.back();
    g_Ctx.MenuBarStack.pop_back();
    if ( !mb.InPage )
    {
        ImGui::EndMainMenuBar();
        ImGui::PopStyleColor();
        return;
    }
    EndTabGroup();
    ImGui::PopStyleVar();
    int open_index = mb.OpenIndex;
    if ( mb.SwitchDir != 0 && mb.OpenIndex >= 0 && mb.Count > 1 )
    {
        const int next = ( mb.OpenIndex + mb.SwitchDir + mb.Count ) % mb.Count;
        ImGui::ClosePopupsOverWindow( ImGui::GetCurrentWindow(), false );
        // Escape in the new menu gives focus back to its own bar item.
        ImGui::SetFocusID( mb.ItemIds[ next ], ImGui::GetCurrentWindow() );
        ImGui::SetNavCursorVisible( true ); // SetFocusID hides it when the last active input was the mouse
        StorePopupAnchor( mb.PopupIds[ next ], mb.Rects[ next ] );
        ImGui::OpenPopupEx( mb.PopupIds[ next ], ImGuiPopupFlags_None );
        open_index = next;
    }
    ImGui::GetStateStorage()->SetInt( mb.Id ^ MENUBAR_KEY_OPEN, open_index );
    ImGui::EndGroup();
    ImGui::PopID();
    ImGui::NewLine();
}

// [SECTION] CommandBar & AppBar

static const ImGuiID COMMANDBAR_KEY_FIRST = 0xCBF1F57u; // In the bar's child window storage
static const ImGuiID COMMANDBAR_KEY_LAST  = 0xCB1A57u;

bool ImFluent::BeginCommandBar( const char * id, float height )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    const float h               = ( height > 0.f ) ? height : FluentDpx( style.AppBarButtonHeight );

    ImFluentStackGuard g;
    g.PushStyleColor( ImGuiCol_ChildBg, ImFluent::GetColorU32( ImFluentCol_LayerFillDefault ) );
    g.PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( FluentDpx( style.SpacingMedium ), 0.f ) );
    g.PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( FluentDpx( style.SpacingXSmall ), 0.f ) );

    // In an auto-sizing window the bar takes its commands' width: filling the available width would collapse it.
    const ImGuiWindow * parent = ImGui::GetCurrentWindow();
    const bool auto_width      = ( parent->Flags & ImGuiWindowFlags_AlwaysAutoResize ) || ( parent->ChildFlags & ImGuiChildFlags_AutoResizeX );
    const ImGuiChildFlags cf   = ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened | ( auto_width ? ImGuiChildFlags_AutoResizeX : 0 );
    if ( !ImGui::BeginChild( id, ImVec2( 0, h ), cf, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ) )
    {
        ImGui::EndChild();
        return false;
    }
    g.Forget();

    ImFluentCommandBarState s;
    ImStrncpy( s.Id, id, sizeof( s.Id ) );
    ImFormatString( s.PopupId, sizeof( s.PopupId ), "##cb-overflow-%s", id );
    s.Height          = h;
    s.Window          = ImGui::GetCurrentWindow();
    ImGuiStorage * st = ImGui::GetStateStorage();
    s.PrevFirst       = ( ImGuiID )st->GetInt( COMMANDBAR_KEY_FIRST, 0 );
    s.PrevLast        = ( ImGuiID )st->GetInt( COMMANDBAR_KEY_LAST, 0 );
    g_Ctx.CommandBarStack.push_back( s );
    return true;
}

// WinUI CommandBar: each primary command is a tab stop; Left/Right stop at the first one and at More. The overflow uses menu keys.
static void CommandBarItemNav( ImGuiID id )
{
    if ( g_Ctx.CommandBarStack.empty() )
        return;
    ImFluentCommandBarState & s = g_Ctx.CommandBarStack.back();
    if ( GImGui->CurrentWindow != s.Window )
        return;
    if ( !s.First )
        s.First = id;
    s.Last = id;
    if ( id == s.PrevFirst )
        ImFluent::ConsumeNavArrow( id, ImGuiDir_Left );
    if ( id == s.PrevLast )
        ImFluent::ConsumeNavArrow( id, ImGuiDir_Right );
}

bool ImFluent::BeginCommandBarOverflow()
{
    if ( g_Ctx.CommandBarStack.empty() )
        return false;
    ImFluentCommandBarState & s = g_Ctx.CommandBarStack.back();
    const ImFluentStyle & style = ImFluent::GetStyle();
    const float chev_w          = FluentDpx( style.AppBarButtonHeight );
    const float right           = ImGui::GetWindowContentRegionMax().x;
    const float curX            = ImGui::GetCursorPosX();
    if ( curX < right - chev_w )
    {
        // This overflow chevron right-aligns itself, so the automatic SameLine must not run and re-pack it
        // against the previous item.
        ImGui::SameLine( right - chev_w );
        s.SuppressAutoSameLine = true;
    }
    SetNextAppBarLabelPosition( ImFluentAppBarLabelPosition_Collapsed );
    const bool pressed    = AppBarButton( "##cb-more", ImFluentIcon_More, ImVec2( chev_w, s.Height ) );
    const ImGuiID more_id = GImGui->LastItemData.ID;
    // WinUI: Down or Up on More also opens the overflow, Up on its last item.
    const bool down       = ConsumeNavArrow( more_id, ImGuiDir_Down );
    const bool up         = ConsumeNavArrow( more_id, ImGuiDir_Up );
    if ( pressed || down || up )
        OpenMenuFlyout( s.PopupId );
    s.OverflowFocusLast = up;
    return BeginMenuFlyout( s.PopupId );
}

void ImFluent::EndCommandBarOverflow()
{
    if ( !g_Ctx.CommandBarStack.empty() && g_Ctx.CommandBarStack.back().OverflowFocusLast )
        ImGui::SetItemDefaultFocus();
    EndMenuFlyout();
}

void ImFluent::EndCommandBar()
{
    if ( !g_Ctx.CommandBarStack.empty() )
    {
        const ImFluentCommandBarState & s = g_Ctx.CommandBarStack.back();
        ImGui::GetStateStorage()->SetInt( COMMANDBAR_KEY_FIRST, ( int )s.First );
        ImGui::GetStateStorage()->SetInt( COMMANDBAR_KEY_LAST, ( int )s.Last );
        g_Ctx.CommandBarStack.pop_back();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor();
}

// [SECTION] CommandBarFlyout
// WinUI CommandBarFlyout: focus opens on the first primary command, Left/Right stop at the ends of the row,
// a secondary command closes the flyout, Escape closes it all.

bool ImFluent::BeginCommandBarFlyout( const char * id )
{
    if ( !BeginFlyout( id ) )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImFluentCommandBarState s;
    ImStrncpy( s.Id, id, sizeof( s.Id ) );
    s.PopupId[ 0 ]    = 0;
    s.Height          = FluentDpx( style.AppBarButtonHeight );
    s.IconOnly        = true;
    s.Window          = ImGui::GetCurrentWindow();
    ImGuiStorage * st = ImGui::GetStateStorage();
    s.PrevFirst       = ( ImGuiID )st->GetInt( COMMANDBAR_KEY_FIRST, 0 );
    s.PrevLast        = ( ImGuiID )st->GetInt( COMMANDBAR_KEY_LAST, 0 );
    g_Ctx.CommandBarStack.push_back( s );
    return true;
}

static void EndCommandBarFlyoutPrimary()
{
    if ( g_Ctx.CommandBarStack.empty() || g_Ctx.CommandBarStack.back().Window != ImGui::GetCurrentWindow() )
        return;
    const ImFluentCommandBarState & s = g_Ctx.CommandBarStack.back();
    ImGui::GetStateStorage()->SetInt( COMMANDBAR_KEY_FIRST, ( int )s.First );
    ImGui::GetStateStorage()->SetInt( COMMANDBAR_KEY_LAST, ( int )s.Last );
    g_Ctx.CommandBarStack.pop_back();
}

void ImFluent::CommandBarFlyoutSecondary()
{
    EndCommandBarFlyoutPrimary();
    MenuFlyoutSeparator();
}

void ImFluent::EndCommandBarFlyout()
{
    EndCommandBarFlyoutPrimary();
    EndFlyout();
}

void ImFluent::SetNextAppBarLabelPosition( ImFluentAppBarLabelPosition pos )
{
    g_Ctx.NextAppBarLabelPos    = pos;
    g_Ctx.NextAppBarLabelPosSet = true;
}

// A command bar is horizontal by definition, but the AppBar* items are ordinary ImGui items and ImGui starts a
// new line after each one. Without this, every item after the first lands below the bar's fixed height and is
// clipped away -- a bar of ten actions renders as one. Outside a command bar the stack is empty and layout is
// left exactly as the caller arranged it.
static void AppBarLayoutNextItem()
{
    if ( g_Ctx.CommandBarStack.empty() )
        return;
    ImFluentCommandBarState & s = g_Ctx.CommandBarStack.back();
    if ( s.HasItem && !s.SuppressAutoSameLine )
        ImGui::SameLine();
    s.SuppressAutoSameLine = false;
    s.HasItem              = true;
}

bool ImFluent::AppBarButton( const char * label, const char * glyph, const ImVec2 & size_arg )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    AppBarLayoutNextItem();
    glyph                                 = ConsumePendingGlyph( glyph );
    const ImFluentAppBarLabelPosition pos = ConsumeAppBarLabelPos();
    const ImFluentStyle & style           = ImFluent::GetStyle();
    const ImGuiID id                      = AppBarButtonId( w, label, glyph );

    const ImVec2 size = CalcAppBarButtonSize( label, glyph, pos, size_arg, style );
    const float W     = size.x;
    const float H     = size.y;

    const ImVec2 p = w->DC.CursorPos;
    const ImRect bb( p, ImVec2( p.x + W, p.y + H ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id ) )
        return false;
    bool hov = false, held = false;
    const bool pressed = ImGui::ButtonBehavior( bb, id, &hov, &held );
    CommandBarItemNav( id );
    ReportItemInfo( id, label ? label : glyph );
    ImDrawList * dl    = w->DrawList;
    const float r      = FluentDpx( style.ControlCornerRadius );
    ImU32 fill         = ResolveSubtleFillState( false, held, hov );
    dl->AddRectFilled( bb.Min, bb.Max, AnimateColorU32( id, fill ), r );
    RenderAppBarContent( dl, bb, label, glyph, ImFluent::GetColorU32( ImFluentCol_TextPrimary ), pos, style );
    RenderNavFocusRing( id, bb, r );
    return pressed;
}

bool ImFluent::AppBarToggleButton( const char * label, const char * glyph, bool * v, const ImVec2 & size_arg )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    AppBarLayoutNextItem();
    glyph                                 = ConsumePendingGlyph( glyph );
    const ImFluentAppBarLabelPosition pos = ConsumeAppBarLabelPos();
    const ImFluentStyle & style           = ImFluent::GetStyle();
    const ImGuiID id                      = AppBarButtonId( w, label, glyph );

    const ImVec2 size = CalcAppBarButtonSize( label, glyph, pos, size_arg, style );
    const float W     = size.x;
    const float H     = size.y;

    const ImVec2 p = w->DC.CursorPos;
    const ImRect bb( p, ImVec2( p.x + W, p.y + H ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id ) )
        return false;
    bool hov = false, held = false;
    const bool pressed = ImGui::ButtonBehavior( bb, id, &hov, &held );
    CommandBarItemNav( id );
    if ( pressed && v )
        *v = !*v;
    const bool on = v && *v;
    ReportItemInfo( id, label ? label : glyph, CheckedFlags( on ) );

    ImDrawList * dl = w->DrawList;
    const float r   = FluentDpx( style.ControlCornerRadius );
    ImU32 fill;
    if ( on )
        fill = ResolveAccentFillState( false, held, hov );
    else
        fill = ResolveSubtleFillState( false, held, hov );
    dl->AddRectFilled( bb.Min, bb.Max, AnimateColorU32( id, fill ), r );
    const ImU32 textCol = ImFluent::GetColorU32( on ? ImFluentCol_TextOnAccentPrimary : ImFluentCol_TextPrimary );
    RenderAppBarContent( dl, bb, label, glyph, textCol, pos, style );
    RenderNavFocusRing( id, bb, r );
    return pressed;
}

void ImFluent::AppBarSeparator()
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImGuiWindow * w             = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return;
    AppBarLayoutNextItem();
    const float H    = FluentDpx( style.NavItemHeight );
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb( ImVec2( pos.x + FluentDpx( style.SpacingSmall ), pos.y + FluentDpx( style.SpacingMedium ) ), ImVec2( pos.x + FluentDpx( style.SpacingSmall + 1.f ), pos.y + H - FluentDpx( style.SpacingMedium ) ) );
    ImGui::ItemSize( ImVec2( FluentDpx( style.SpacingMedium + 1.f ), H ) );
    if ( !ImGui::ItemAdd( bb, 0 ) )
        return;
    w->DrawList->AddRectFilled( bb.Min, bb.Max, ImFluent::GetColorU32( ImFluentCol_DividerStrokeDefault ) );
}

static int DaysInMonth( int year, int month )
{
    static const int kDays[ 12 ] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    month                        = ImClamp( month, 1, 12 );
    int d                        = kDays[ month - 1 ];
    if ( month == 2 )
    {
        const bool leap = ( year % 4 == 0 && ( year % 100 != 0 || year % 400 == 0 ) );
        if ( leap )
            d = 29;
    }
    return d;
}

static int DayOfWeekMonStart( int year, int month, int day )
{
    int y = year, m = month;
    if ( m < 3 )
    {
        m += 12;
        y -= 1;
    }
    const int K = y % 100;
    const int J = y / 100;
    const int h = ( day + 13 * ( m + 1 ) / 5 + K + K / 4 + J / 4 + 5 * J ) % 7;
    return ( h + 5 ) % 7;
}

static int CompareDate( int ay, int am, int ad, int by, int bm, int bd )
{
    if ( ay != by )
        return ay - by;
    if ( am != bm )
        return am - bm;
    return ad - bd;
}

static bool DateInRange( int y, int m, int d, const ImFluent::ImFluentDate * min_d, const ImFluent::ImFluentDate * max_d )
{
    if ( min_d && CompareDate( y, m, d, min_d->Year, min_d->Month, min_d->Day ) < 0 )
        return false;
    if ( max_d && CompareDate( y, m, d, max_d->Year, max_d->Month, max_d->Day ) > 0 )
        return false;
    return true;
}

static bool MonthOverlapsRange( int y, int m, const ImFluent::ImFluentDate * min_d, const ImFluent::ImFluentDate * max_d )
{
    const int dim = DaysInMonth( y, m );
    if ( min_d && CompareDate( y, m, dim, min_d->Year, min_d->Month, min_d->Day ) < 0 )
        return false;
    if ( max_d && CompareDate( y, m, 1, max_d->Year, max_d->Month, max_d->Day ) > 0 )
        return false;
    return true;
}

static bool YearOverlapsRange( int y, const ImFluent::ImFluentDate * min_d, const ImFluent::ImFluentDate * max_d )
{
    if ( min_d && CompareDate( y, 12, 31, min_d->Year, min_d->Month, min_d->Day ) < 0 )
        return false;
    if ( max_d && CompareDate( y, 1, 1, max_d->Year, max_d->Month, max_d->Day ) > 0 )
        return false;
    return true;
}

static void GetTodayDate( int & out_y, int & out_m, int & out_d )
{
    const time_t t = time( NULL );
    struct tm lt;
#if defined( _WIN32 )
    localtime_s( &lt, &t );
#else
    lt = *localtime( &t );
#endif
    out_y = lt.tm_year + 1900;
    out_m = lt.tm_mon + 1;
    out_d = lt.tm_mday;
}

// [SECTION] Date & Time pickers

namespace ImFluent
{
    static const int SPINNER_VISIBLE_ROWS            = 7;
    static const int SPINNER_CENTER_ROW              = SPINNER_VISIBLE_ROWS / 2;
    static const ImGuiID PICKER_KEY_INIT             = 0xD9FEu;
    static const ImGuiID PICKER_KEY_WAS              = 0xD7FEu;
    static const ImGuiID PICKER_KEY_DAY              = 0xDAFEu;
    static const ImGuiID PICKER_KEY_MONTH            = 0xD0FEu;
    static const ImGuiID PICKER_KEY_YEAR             = 0xD3FEu;
    static const ImGuiID PICKER_KEY_HOUR             = 0xDA17u;
    static const ImGuiID PICKER_KEY_MINUTE           = 0xD117u;
    static const float DATEPICKER_DAY_RATIO          = 56.f / 256.f;
    static const float DATEPICKER_MON_RATIO          = 120.f / 256.f;
    static const ImGuiID CAL_KEY_WAS                 = 0xC7FEu;
    static const ImGuiID CAL_KEY_VIEW_Y              = 0xC1FEu;
    static const ImGuiID CAL_KEY_VIEW_M              = 0xC2FEu;
    static const ImGuiID CAL_KEY_MODE                = 0xC3FEu;
    static const ImGuiID CAL_KEY_FOCUS_Y             = 0xC4FEu;
    static const ImGuiID CAL_KEY_FOCUS_M             = 0xC5FEu;
    static const ImGuiID CAL_KEY_FOCUS_D             = 0xC6FEu;
    static const ImGuiWindowFlags PICKER_POPUP_FLAGS = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;

    static void SetupPickerPopup( const ImRect & trigger_bb, float popup_w, float popup_pad_y_dpx )
    {
        const float trigger_cy = ( trigger_bb.Min.y + trigger_bb.Max.y ) * 0.5f;
        const float row_h      = ImFloor( ImFluent::FluentDpx( ImFluent::GetStyle().ControlHeight ) );
        const float popup_y    = trigger_cy - ( ( float )SPINNER_CENTER_ROW + 0.5f ) * row_h - ImFluent::FluentDpx( popup_pad_y_dpx );
        ImGui::SetNextWindowPos( ImVec2( trigger_bb.Min.x, popup_y ) );
        ImGui::SetNextWindowSizeConstraints( ImVec2( popup_w, 0.f ), ImVec2( popup_w, FLT_MAX ) );
    }

    static void RenderSpinnerChevronButton( ImDrawList * dl, const ImRect & bb, ImGuiDir dir, bool hov, bool held )
    {
        const ImFluentStyle & style = ImFluent::GetStyle();
        const float r               = ImFluent::FluentDpx( style.ControlCornerRadius );
        ImU32 fill                  = 0;
        if ( held )
            fill = ImFluent::GetColorU32( ImFluentCol_SubtleFillTertiary );
        else if ( hov )
            fill = ImFluent::GetColorU32( ImFluentCol_SubtleFillSecondary );
        else
            fill = ImFluent::GetColorU32( ImFluentCol_SolidBgQuarternary );
        dl->AddRectFilled( bb.Min, bb.Max, fill, r );
        const ImVec2 c( ( bb.Min.x + bb.Max.x ) * 0.5f, ( bb.Min.y + bb.Max.y ) * 0.5f );
        const float L  = ImFluent::FluentDpx( style.ChevronGlyphSize );
        const float th = ImFluent::FluentDpx( style.StrokeMedium );
        RenderChevron( dl, c, dir, ImFluent::GetColorU32( ImFluentCol_TextPrimary ), L, th );
    }

    typedef const char * ( *SpinnerLabelFn )( int index, char * buf, size_t buf_size, const void * user_data );

    // One picker flyout column (WinUI LoopingSelector) over [0, count): one focus stop on the selection band, rows answer the mouse only.
    // Up/Down (wrap when `loop`), PageUp/Down by SPINNER_CENTER_ROW, Home/End; Enter/Space/Alt+Up/Alt+Down commit. Returns the new index.
    static int RenderSpinnerColumnEx( const char * id, const char * name, int index, int count, SpinnerLabelFn label_fn, const void * label_user, float col_w, bool loop, bool default_focus, bool * out_commit )
    {
        const ImFluentStyle & style = ImFluent::GetStyle();
        const float row_h           = ImFloor( ImFluent::FluentDpx( style.ControlHeight ) );
        const float win_h           = SPINNER_VISIBLE_ROWS * row_h;
        const float pad_h           = SPINNER_CENTER_ROW * row_h;
        const float r               = ImFluent::FluentDpx( style.ControlCornerRadius );

        int new_index = ImClamp( index, 0, count - 1 );

        ImGui::PushID( id );
        ImGuiWindow * host   = ImGui::GetCurrentWindow();
        const ImVec2 col_pos = ImGui::GetCursorScreenPos();
        const ImRect band( ImVec2( col_pos.x, col_pos.y + pad_h ), ImVec2( col_pos.x + col_w, col_pos.y + pad_h + row_h ) );
        const ImGuiID focus_id = host->GetID( "##column" );

        // Keyboard, before the scroll position for this frame is chosen. Alt+Up/Alt+Down commit (WinUI).
        if ( IsFocusedItemChordPressed( focus_id, g_Ctx.Actions.PickerOpenCommit, false ) || IsFocusedItemChordPressed( focus_id, g_Ctx.Actions.PickerOpenCommitAlt, false ) )
            *out_commit = true;
        else if ( !ImGui::GetIO().KeyAlt )
        {
            if ( ConsumeNavArrow( focus_id, ImGuiDir_Up ) )
                new_index = ( new_index > 0 ) ? new_index - 1 : ( loop ? count - 1 : 0 );
            if ( ConsumeNavArrow( focus_id, ImGuiDir_Down ) )
                new_index = ( new_index < count - 1 ) ? new_index + 1 : ( loop ? 0 : count - 1 );
            if ( IsFocusedItemChordPressed( focus_id, g_Ctx.Actions.PageUp ) )
                new_index = ImMax( 0, new_index - SPINNER_CENTER_ROW );
            if ( IsFocusedItemChordPressed( focus_id, g_Ctx.Actions.PageDown ) )
                new_index = ImMin( count - 1, new_index + SPINNER_CENTER_ROW );
            if ( IsFocusedItemChordPressed( focus_id, g_Ctx.Actions.FirstItem ) )
                new_index = 0;
            if ( IsFocusedItemChordPressed( focus_id, g_Ctx.Actions.LastItem ) )
                new_index = count - 1;
        }
        if ( GImGui->NavActivateId == focus_id )
            *out_commit = true;

        ImGui::SetNextWindowScroll( ImVec2( -1.f, ( float )new_index * row_h ) );

        ImFluentStackGuard gw;
        gw.PushStyleColor( ImGuiCol_ChildBg, IM_COL32_BLACK_TRANS );
        gw.PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.f, 0.f ) );
        gw.PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 0.f, 0.f ) );

        ImGui::BeginChild( id, ImVec2( col_w, win_h ), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );

        const bool col_hovered = ImGui::IsWindowHovered();

        ImGuiStorage * cs         = ImGui::GetStateStorage();
        const ImGuiID acc_key     = ImGui::GetID( "##sp_acc" );
        const ImGuiID wheel_owner = ImGui::GetCurrentWindow()->ID;
        float acc                 = cs->GetFloat( acc_key, 0.f );
        if ( col_hovered )
            ImGui::SetKeyOwner( ImGuiKey_MouseWheelY, wheel_owner );
        else
            acc = 0.f;
        if ( ImGui::TestKeyOwner( ImGuiKey_MouseWheelY, wheel_owner ) )
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if ( wheel != 0.f )
            {
                acc += wheel;
                const int wheel_steps = ( acc >= 0.f ) ? ( int )ImFloor( acc ) : ( int )ImCeil( acc );
                if ( wheel_steps != 0 )
                {
                    new_index = ImClamp( new_index - wheel_steps, 0, count - 1 );
                    acc -= ( float )wheel_steps;
                }
            }
        }
        cs->SetFloat( acc_key, acc );

        ImGui::Dummy( ImVec2( 0.f, pad_h ) );

        ImGuiWindow * cw = ImGui::GetCurrentWindow();
        ImDrawList * dl  = cw->DrawList;

        const bool can_up          = ( new_index > 0 );
        const bool can_down        = ( new_index < count - 1 );
        const int top_chev_k       = new_index - SPINNER_CENTER_ROW;
        const int bot_chev_k       = new_index + SPINNER_CENTER_ROW;
        const ImU32 col_subtle_ter = ImFluent::GetColorU32( ImFluentCol_SubtleFillTertiary );
        const ImU32 col_subtle_sec = ImFluent::GetColorU32( ImFluentCol_SubtleFillSecondary );
        const ImU32 col_on_acc     = ImFluent::GetColorU32( ImFluentCol_TextOnAccentPrimary );
        const ImU32 col_text_pri   = ImFluent::GetColorU32( ImFluentCol_TextPrimary );
        const ImU32 col_text_sec   = ImFluent::GetColorU32( ImFluentCol_TextSecondary );

        for ( int k = 0; k < count; ++k )
        {
            char buf[ 32 ];
            const char * txt = label_fn( k, buf, sizeof( buf ), label_user );

            ImGui::PushID( k );
            const ImGuiID item_id = cw->GetID( "##si" );
            ImGui::PopID();

            const ImVec2 pos = cw->DC.CursorPos;
            const ImRect bb( pos, ImVec2( pos.x + col_w, pos.y + row_h ) );
            ImGui::ItemSize( bb );
            if ( !ImGui::ItemAdd( bb, item_id, NULL, ImGuiItemFlags_NoNav ) ) // Rows are not focus stops
                continue;

            const bool chev_covers = col_hovered && ( ( can_up && k == top_chev_k ) || ( can_down && k == bot_chev_k ) );
            if ( chev_covers )
                continue;

            bool hov = false, held = false;
            const bool pressed = ImGui::ButtonBehavior( bb, item_id, &hov, &held );
            if ( pressed )
                new_index = k;

            const bool sel = ( k == new_index );
            ReportItemInfo( item_id, txt, CheckedFlags( sel ) );
            if ( !sel )
            {
                const ImU32 hover_fill = held ? col_subtle_ter : ( hov ? col_subtle_sec : 0u );
                if ( hover_fill )
                    dl->AddRectFilled( bb.Min, bb.Max, hover_fill, r );
            }

            const ImU32 text_col = sel ? col_on_acc : ( hov ? col_text_pri : col_text_sec );
            const ImVec2 ts      = ImGui::CalcTextSize( txt );
            dl->AddText( ImVec2( bb.Min.x + ( col_w - ts.x ) * 0.5f, bb.Min.y + ( row_h - ts.y ) * 0.5f ), text_col, txt );
        }

        ImGui::Dummy( ImVec2( 0.f, pad_h ) );

        if ( col_hovered )
        {
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 ws = ImGui::GetWindowSize();
            if ( can_up )
            {
                const ImRect top_bb( wp, ImVec2( wp.x + ws.x, wp.y + row_h ) );
                const bool top_hov     = ImGui::IsMouseHoveringRect( top_bb.Min, top_bb.Max, false );
                const bool top_held    = top_hov && ImGui::IsMouseDown( ImGuiMouseButton_Left );
                const bool top_clicked = top_hov && ImGui::IsMouseClicked( ImGuiMouseButton_Left, g_Ctx.Actions.RepeatStepButtons );
                RenderSpinnerChevronButton( dl, top_bb, ImGuiDir_Up, top_hov, top_held );
                if ( top_clicked )
                    new_index = ImMax( 0, new_index - 1 );
            }
            if ( can_down )
            {
                const ImRect bot_bb( ImVec2( wp.x, wp.y + ws.y - row_h ), ImVec2( wp.x + ws.x, wp.y + ws.y ) );
                const bool bot_hov     = ImGui::IsMouseHoveringRect( bot_bb.Min, bot_bb.Max, false );
                const bool bot_held    = bot_hov && ImGui::IsMouseDown( ImGuiMouseButton_Left );
                const bool bot_clicked = bot_hov && ImGui::IsMouseClicked( ImGuiMouseButton_Left, g_Ctx.Actions.RepeatStepButtons );
                RenderSpinnerChevronButton( dl, bot_bb, ImGuiDir_Down, bot_hov, bot_held );
                if ( bot_clicked )
                    new_index = ImMin( count - 1, new_index + 1 );
            }
        }

        ImGui::EndChild();
        gw.Restore();

        // The focus stop, registered after the child so it does not move the layout.
        ImGui::ItemAdd( band, focus_id );
        if ( default_focus )
            ImGui::SetItemDefaultFocus();
        ReportItemInfo( focus_id, name );
        RenderNavFocusRing( focus_id, band, r );

        ImGui::PopID();
        return new_index;
    }

    struct SpinnerRange
    {
        int Min, Step;
        bool MonthNames;
        const char * Format;
    };

    static const char * SpinnerRangeLabel( int index, char * buf, size_t buf_size, const void * user_data )
    {
        const SpinnerRange & range = *( const SpinnerRange * )user_data;
        const int v                = range.Min + index * range.Step;
        if ( range.MonthNames )
            return ImFluent::LocalizeGetMsg( ( ImFluentLocKey )( ImFluentLocKey_MonthJanuary + v - 1 ) );
        ImFormatString( buf, buf_size, range.Format, v );
        return buf;
    }

    static const char * SpinnerListLabel( int index, char *, size_t, const void * user_data )
    {
        return ( ( const char * const * )user_data )[ index ];
    }

    // Numeric column: values min_v, min_v + step, ... max_v (month names when `month_names`).
    static int RenderSpinnerColumn( const char * id, const char * name, int value, int min_v, int max_v, bool month_names, const char * format, float col_w, int step, bool loop, bool default_focus, bool * out_commit )
    {
        if ( step < 1 )
            step = 1;
        const SpinnerRange range = { min_v, step, month_names, format };
        const int count          = ( max_v - min_v ) / step + 1;
        const int index          = ( ImClamp( value, min_v, max_v ) - min_v ) / step;
        return min_v + step * RenderSpinnerColumnEx( id, name, index, count, SpinnerRangeLabel, &range, col_w, loop, default_focus, out_commit );
    }

    static int RenderSpinnerLabelColumn( const char * id, const char * name, int value, const char * const * labels, int count, float col_w, bool loop, bool * out_commit )
    {
        return RenderSpinnerColumnEx( id, name, value, count, SpinnerListLabel, labels, col_w, loop, false, out_commit );
    }

    static bool RenderPickerActionButton( const char * label, const char * name, const ImVec2 & size )
    {
        ImGuiWindow * w = ImGui::GetCurrentWindow();
        if ( w->SkipItems )
            return false;
        const ImFluentStyle & style = ImFluent::GetStyle();
        const float r               = ImFluent::FluentDpx( style.ControlCornerRadius );
        const ImGuiID id            = w->GetID( label );
        const ImVec2 pos            = w->DC.CursorPos;
        const ImRect bb( pos, ImVec2( pos.x + size.x, pos.y + size.y ) );
        ImGui::ItemSize( bb );
        if ( !ImGui::ItemAdd( bb, id ) )
            return false;

        bool hov = false, held = false;
        const bool pressed = ImGui::ButtonBehavior( bb, id, &hov, &held );
        ReportItemInfo( id, name );

        ImU32 fill_target;
        if ( held )
            fill_target = ImFluent::GetColorU32( ImFluentCol_SubtleFillTertiary );
        else if ( hov )
            fill_target = ImFluent::GetColorU32( ImFluentCol_SubtleFillSecondary );
        else
            fill_target = ImFluent::GetColorU32( ImFluentCol_SubtleFillTransparent );
        const ImU32 fill = ImFluent::AnimateColorU32( id, fill_target );
        ImDrawList * dl  = w->DrawList;
        dl->AddRectFilled( bb.Min, bb.Max, fill, r );

        const char * text_end = ImGui::FindRenderedTextEnd( label );
        const ImVec2 ts       = ImGui::CalcTextSize( label, text_end );
        const ImU32 text_col  = ImFluent::GetColorU32( ImFluentCol_TextPrimary );
        dl->AddText( ImVec2( bb.Min.x + ( size.x - ts.x ) * 0.5f, bb.Min.y + ( size.y - ts.y ) * 0.5f ), text_col, label, text_end );

        RenderNavFocusRing( id, bb, r );
        return pressed;
    }

    static void RenderSpinnerSelectionBand( ImDrawList * dl, ImVec2 group_origin, float total_w, float row_h )
    {
        const ImFluentStyle & style = ImFluent::GetStyle();
        const float spinners_h      = SPINNER_VISIBLE_ROWS * row_h;
        const float band_top        = ImFloor( group_origin.y + ( spinners_h - row_h ) * 0.5f );
        dl->AddRectFilled( ImVec2( group_origin.x, band_top ), ImVec2( group_origin.x + total_w, band_top + row_h ), ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), ImFluent::FluentDpx( style.ControlCornerRadius ) );
    }

    static void RenderPickerTriggerFrame( ImDrawList * dl, const ImRect & bb, float r, bool hovered, bool held, bool open, bool disabled, ImGuiID id )
    {
        ImU32 fill_target;
        if ( disabled )
            fill_target = ImFluent::GetColorU32( ImFluentCol_ControlFillDisabled );
        else if ( held || open )
            fill_target = ImFluent::GetColorU32( ImFluentCol_ControlFillTertiary );
        else if ( hovered )
            fill_target = ImFluent::GetColorU32( ImFluentCol_ControlFillSecondary );
        else
            fill_target = ImFluent::GetColorU32( ImFluentCol_ControlFillDefault );
        const ImU32 fill = ImFluent::AnimateColorU32( id, fill_target );
        ImFluent::FillInsideBorder( dl, bb.Min, bb.Max, fill, r );

        const ImU32 stroke     = ImFluent::GetColorU32( ImFluentCol_ControlStrokeDefault );
        const ImU32 stroke_top = ImFluent::GetColorU32( ImFluentCol_ElevationControlTop );
        const ImU32 stroke_bot = ImFluent::GetColorU32( ImFluentCol_ElevationControlBottom );
        if ( !held && !open && !disabled )
            ImFluent::RenderElevationBorder( dl, bb, r, stroke, stroke_top, stroke_bot, 1.f );
        else
            ImFluent::StrokeRect( dl, bb.Min, bb.Max, stroke, r );
    }

    static void RenderColumnSeparator( ImDrawList * dl, float x, float ymin, float ymax, ImU32 col )
    {
        dl->AddLine( ImVec2( x, ymin ), ImVec2( x, ymax ), col, 1.f );
    }

    static void RenderCenteredColumnText( ImDrawList * dl, float x0, float x1, float ycenter, ImU32 col, const char * text )
    {
        const ImVec2 ts = ImGui::CalcTextSize( text );
        dl->AddText( ImVec2( x0 + ( ( x1 - x0 ) - ts.x ) * 0.5f, ycenter - ts.y * 0.5f ), col, text );
    }

    // Opens the picker flyout on click, Space/Enter and, when `alt_arrows_open`, the PickerOpenCommit chords (WinUI).
    static bool PickerTogglePopup( ImGuiID popup_id, const ImRect & bb, ImGuiID trig_id, bool disabled, bool alt_arrows_open )
    {
        ImGuiContext & gctx     = *ImGui::GetCurrentContext();
        const bool popup_open   = ImGui::IsPopupOpen( popup_id, ImGuiPopupFlags_None );
        const bool click_in_bb  = ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && IsMouseOverWindowRect( bb );
        const bool nav_activate = ( gctx.NavActivateId == trig_id );
        const bool key_open     = alt_arrows_open && !popup_open && ( IsFocusedItemChordPressed( trig_id, g_Ctx.Actions.PickerOpenCommit, false ) || IsFocusedItemChordPressed( trig_id, g_Ctx.Actions.PickerOpenCommitAlt, false ) );
        const bool toggle       = ( click_in_bb || nav_activate || key_open ) && !disabled;
        if ( toggle )
        {
            if ( popup_open )
                ClosePopupById( gctx, popup_id );
            else
                ImGui::OpenPopupEx( popup_id, ImGuiPopupFlags_None );
        }
        return popup_open;
    }

    static int RenderPickerActionFooter( ImGuiStorage * st, ImGuiID init_key, float total_w, bool commit )
    {
        const ImFluentStyle & style = ImFluent::GetStyle();
        const float btn_w           = total_w * 0.5f;
        const float btn_h           = ImFluent::FluentDpx( style.ControlHeight );

        int fired = commit ? 1 : 0;
        ImGui::Dummy( ImVec2( 0.f, ImFluent::FluentDpx( style.SpacingXSmall ) ) );
        if ( RenderPickerActionButton( ImFluentIcon_CheckMark "##ok", "Accept", ImVec2( btn_w, btn_h ) ) && !fired )
            fired = 1;
        ImGui::SameLine( 0.f, 0.f );
        if ( RenderPickerActionButton( ImFluentIcon_Cancel "##cancel", "Dismiss", ImVec2( btn_w, btn_h ) ) && !fired )
            fired = 2;
        if ( fired )
        {
            st->SetBool( init_key, false );
            ImGui::CloseCurrentPopup();
        }
        return fired;
    }
} // namespace ImFluent

bool ImFluent::DatePicker( const char * label, ImFluentDate * date )
{
    if ( !date )
        return false;
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    RenderAndConsumePendingHeader();

    const ImFluentStyle & style = GetStyle();
    const float h               = FluentDpx( style.ControlHeight );
    const float r               = FluentDpx( style.ControlCornerRadius );
    const float full_w          = ImGui::CalcItemWidth();
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + full_w, pos.y + h ) );

    const ImGuiID id = w->GetID( label );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id ) )
        return false;

    bool hovered = false, held = false;
    ImGui::ButtonBehavior( bb, id, &hovered, &held );
    const bool disabled = IsLastItemDisabled();

    const ImGuiID popup_id = GetWidgetPopupId( id, "##fl_dp_popup" );

    const bool popup_open = PickerTogglePopup( popup_id, bb, id, disabled, true );
    ReportItemInfo( id, label, OpenedFlags( popup_open ) );

    ImDrawList * dl = w->DrawList;
    RenderPickerTriggerFrame( dl, bb, r, hovered, held, popup_open, disabled, id );

    const float ratio_d = DATEPICKER_DAY_RATIO;
    const float ratio_m = DATEPICKER_MON_RATIO;
    const float c1_x    = bb.Min.x;
    const float c2_x    = bb.Min.x + full_w * ratio_d;
    const float c3_x    = c2_x + full_w * ratio_m;
    const float c_end   = bb.Max.x;
    const float cy      = ( bb.Min.y + bb.Max.y ) * 0.5f;

    const ImU32 text_col = GetColorU32( disabled ? ImFluentCol_TextDisabled : ImFluentCol_TextPrimary );
    const ImU32 div_col  = GetColorU32( ImFluentCol_DividerStrokeDefault );

    char buf[ 32 ];
    ImFormatString( buf, sizeof( buf ), "%d", date->Day );
    RenderCenteredColumnText( dl, c1_x, c2_x, cy, text_col, buf );
    RenderColumnSeparator( dl, c2_x, bb.Min.y + h * 0.2f, bb.Min.y + h * 0.8f, div_col );

    const char * mname = LocalizeGetMsg( ( ImFluentLocKey )( ImFluentLocKey_MonthJanuary + date->Month - 1 ) );
    RenderCenteredColumnText( dl, c2_x, c3_x, cy, text_col, mname );
    RenderColumnSeparator( dl, c3_x, bb.Min.y + h * 0.2f, bb.Min.y + h * 0.8f, div_col );

    ImFormatString( buf, sizeof( buf ), "%d", date->Year );
    RenderCenteredColumnText( dl, c3_x, c_end, cy, text_col, buf );

    RenderNavFocusRing( id, bb, r );

    ImGuiStorage * st        = &w->StateStorage;
    const ImGuiID init_key   = id ^ PICKER_KEY_INIT;
    const ImGuiID was_key    = id ^ PICKER_KEY_WAS;
    const ImGuiID day_key    = id ^ PICKER_KEY_DAY;
    const ImGuiID mon_key    = id ^ PICKER_KEY_MONTH;
    const ImGuiID yr_key     = id ^ PICKER_KEY_YEAR;
    const bool was_open_last = st->GetBool( was_key, false );
    if ( was_open_last && !popup_open )
        st->SetBool( init_key, false );

    bool changed = false;
    SetupPickerPopup( bb, full_w, style.SpacingMedium );
    PushOverlayWindowStyle( style.SpacingMedium, style.SpacingMedium );

    if ( ImGui::BeginPopupEx( popup_id, PICKER_POPUP_FLAGS ) )
    {
        if ( !st->GetBool( init_key, false ) )
        {
            st->SetInt( day_key, date->Day );
            st->SetInt( mon_key, date->Month );
            st->SetInt( yr_key, date->Year );
            st->SetBool( init_key, true );
        }
        int wd = st->GetInt( day_key, date->Day );
        int wm = st->GetInt( mon_key, date->Month );
        int wy = st->GetInt( yr_key, date->Year );

        const float avail_w = ImGui::GetContentRegionAvail().x;
        const float c_day_w = ImFloor( avail_w * DATEPICKER_DAY_RATIO );
        const float c_mon_w = ImFloor( avail_w * DATEPICKER_MON_RATIO );
        const float c_yr_w  = avail_w - c_day_w - c_mon_w;

        const ImVec2 group_origin = ImGui::GetCursorScreenPos();
        const float row_h         = ImFloor( FluentDpx( style.ControlHeight ) );
        const float total_w       = c_day_w + c_mon_w + c_yr_w;
        RenderSpinnerSelectionBand( ImGui::GetWindowDrawList(), group_origin, total_w, row_h );

        // Columns left to right so Tab and the initial focus follow the visual order. Day and Month loop, Year does not (WinUI).
        bool commit = false;
        {
            ImFluentStackGuard sp_group;
            sp_group.BeginGroup();
            wd = ImClamp( wd, 1, DaysInMonth( wy, wm ) );
            ImGui::SetCursorScreenPos( group_origin );
            wd = RenderSpinnerColumn( "##sp_day", "Day", wd, 1, DaysInMonth( wy, wm ), false, "%d", c_day_w, 1, true, true, &commit );
            ImGui::SetCursorScreenPos( ImVec2( group_origin.x + c_day_w, group_origin.y ) );
            wm = RenderSpinnerColumn( "##sp_mon", "Month", wm, 1, 12, true, NULL, c_mon_w, 1, true, false, &commit );
            ImGui::SetCursorScreenPos( ImVec2( group_origin.x + c_day_w + c_mon_w, group_origin.y ) );
            wy = RenderSpinnerColumn( "##sp_yr", "Year", wy, 1900, 2100, false, "%d", c_yr_w, 1, false, false, &commit );
            wd = ImClamp( wd, 1, DaysInMonth( wy, wm ) ); // A month or year change can shorten the month
        }

        {
            ImDrawList * pdl    = ImGui::GetWindowDrawList();
            const ImU32 div_col = GetColorU32( ImFluentCol_DividerStrokeDefault );
            const float sep_y0  = group_origin.y;
            const float sep_y1  = group_origin.y + ( float )SPINNER_VISIBLE_ROWS * row_h;
            pdl->AddLine( ImVec2( group_origin.x + c_day_w, sep_y0 ), ImVec2( group_origin.x + c_day_w, sep_y1 ), div_col, 1.f );
            pdl->AddLine( ImVec2( group_origin.x + c_day_w + c_mon_w, sep_y0 ), ImVec2( group_origin.x + c_day_w + c_mon_w, sep_y1 ), div_col, 1.f );
        }

        st->SetInt( day_key, wd );
        st->SetInt( mon_key, wm );
        st->SetInt( yr_key, wy );

        if ( RenderPickerActionFooter( st, init_key, total_w, commit ) == 1 )
        {
            date->Day   = wd;
            date->Month = wm;
            date->Year  = wy;
            changed     = true;
        }
        ImGui::EndPopup();
    }
    PopOverlayWindowStyle();

    st->SetBool( was_key, popup_open );

    if ( label )
    {
        const char * label_end = ImGui::FindRenderedTextEnd( label );
        if ( label_end > label )
        {
            ImGui::SameLine();
            ImGui::TextUnformatted( label, label_end );
        }
    }

    RenderAndConsumePendingError( bb );
    return changed;
}

bool ImFluent::TimePicker( const char * label, ImFluentTime * time, ImFluentTimePickerFlags flags, int minute_increment )
{
    if ( !time )
        return false;
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    RenderAndConsumePendingHeader();

    const bool is_12h              = ( flags & ImFluentTimePickerFlags_Hours12 ) != 0;
    static const char * kAmPm[ 2 ] = { "AM", "PM" };
    if ( minute_increment < 1 )
        minute_increment = 1;
    if ( minute_increment > 30 )
        minute_increment = 30;

    const ImFluentStyle & style = GetStyle();
    const float h               = FluentDpx( style.ControlHeight );
    const float r               = FluentDpx( style.ControlCornerRadius );
    const float full_w          = ImGui::CalcItemWidth();
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + full_w, pos.y + h ) );

    const ImGuiID id = w->GetID( label );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id ) )
        return false;

    bool hovered = false, held = false;
    ImGui::ButtonBehavior( bb, id, &hovered, &held );
    const bool disabled = IsLastItemDisabled();

    const ImGuiID popup_id = GetWidgetPopupId( id, "##fl_tp_popup" );

    const bool popup_open = PickerTogglePopup( popup_id, bb, id, disabled, true );
    ReportItemInfo( id, label, OpenedFlags( popup_open ) );

    ImDrawList * dl = w->DrawList;
    RenderPickerTriggerFrame( dl, bb, r, hovered, held, popup_open, disabled, id );

    const int n_cols     = is_12h ? 3 : 2;
    const float col_step = full_w / ( float )n_cols;
    const float cy       = ( bb.Min.y + bb.Max.y ) * 0.5f;

    const ImU32 text_col = GetColorU32( disabled ? ImFluentCol_TextDisabled : ImFluentCol_TextPrimary );
    const ImU32 div_col  = GetColorU32( ImFluentCol_DividerStrokeDefault );

    int disp_h = time->Hour;
    int ap_idx = 0;
    if ( is_12h )
    {
        ap_idx = ( time->Hour >= 12 ) ? 1 : 0;
        disp_h = time->Hour % 12;
        if ( disp_h == 0 )
            disp_h = 12;
    }

    char buf[ 16 ];
    ImFormatString( buf, sizeof( buf ), is_12h ? "%d" : "%02d", disp_h );
    RenderCenteredColumnText( dl, bb.Min.x, bb.Min.x + col_step, cy, text_col, buf );
    RenderColumnSeparator( dl, bb.Min.x + col_step, bb.Min.y + h * 0.2f, bb.Min.y + h * 0.8f, div_col );

    const int minute_max  = ( 60 / minute_increment - 1 ) * minute_increment;
    const int disp_minute = ImMin( ( time->Minute / minute_increment ) * minute_increment, minute_max );
    ImFormatString( buf, sizeof( buf ), "%02d", disp_minute );
    RenderCenteredColumnText( dl, bb.Min.x + col_step, bb.Min.x + col_step * 2.f, cy, text_col, buf );

    if ( is_12h )
    {
        RenderColumnSeparator( dl, bb.Min.x + col_step * 2.f, bb.Min.y + h * 0.2f, bb.Min.y + h * 0.8f, div_col );
        RenderCenteredColumnText( dl, bb.Min.x + col_step * 2.f, bb.Max.x, cy, text_col, kAmPm[ ap_idx ] );
    }

    RenderNavFocusRing( id, bb, r );

    ImGuiStorage * st        = &w->StateStorage;
    const ImGuiID init_key   = id ^ PICKER_KEY_INIT;
    const ImGuiID was_key    = id ^ PICKER_KEY_WAS;
    const ImGuiID hr_key     = id ^ PICKER_KEY_HOUR;
    const ImGuiID mn_key     = id ^ PICKER_KEY_MINUTE;
    const bool was_open_last = st->GetBool( was_key, false );
    if ( was_open_last && !popup_open )
        st->SetBool( init_key, false );

    bool changed = false;
    SetupPickerPopup( bb, full_w, style.SpacingMedium );
    PushOverlayWindowStyle( style.SpacingMedium, style.SpacingMedium );

    if ( ImGui::BeginPopupEx( popup_id, PICKER_POPUP_FLAGS ) )
    {
        if ( !st->GetBool( init_key, false ) )
        {
            st->SetInt( hr_key, time->Hour );
            st->SetInt( mn_key, time->Minute );
            st->SetBool( init_key, true );
        }
        int wh24 = st->GetInt( hr_key, time->Hour );
        int wm   = st->GetInt( mn_key, time->Minute );

        const float avail_w = ImGui::GetContentRegionAvail().x;
        float col_w_h, col_w_m, col_w_ap;
        if ( is_12h )
        {
            col_w_h  = ImFloor( avail_w / 3.f );
            col_w_m  = ImFloor( avail_w / 3.f );
            col_w_ap = avail_w - col_w_h - col_w_m;
        }
        else
        {
            col_w_h  = ImFloor( avail_w * 0.5f );
            col_w_m  = avail_w - col_w_h;
            col_w_ap = 0.f;
        }

        const ImVec2 group_origin = ImGui::GetCursorScreenPos();
        const float row_h         = ImFloor( FluentDpx( style.ControlHeight ) );
        const float total_w       = col_w_h + col_w_m + col_w_ap;
        RenderSpinnerSelectionBand( ImGui::GetWindowDrawList(), group_origin, total_w, row_h );

        int w_disp_h = wh24;
        int w_ap     = 0;
        if ( is_12h )
        {
            w_ap     = ( wh24 >= 12 ) ? 1 : 0;
            w_disp_h = wh24 % 12;
            if ( w_disp_h == 0 )
                w_disp_h = 12;
        }

        const int m_max = ( 60 / minute_increment - 1 ) * minute_increment;

        bool commit = false;
        {
            ImFluentStackGuard sp_group;
            sp_group.BeginGroup();
            if ( is_12h )
            {
                w_disp_h = RenderSpinnerColumn( "##sp_h", "Hour", w_disp_h, 1, 12, false, "%d", col_w_h, 1, true, true, &commit );
                ImGui::SameLine( 0.f, 0.f );
                wm = RenderSpinnerColumn( "##sp_m", "Minute", wm, 0, m_max, false, "%02d", col_w_m, minute_increment, true, false, &commit );
                ImGui::SameLine( 0.f, 0.f );
                w_ap = RenderSpinnerLabelColumn( "##sp_ap", "Period", w_ap, kAmPm, 2, col_w_ap, false, &commit );
            }
            else
            {
                wh24 = RenderSpinnerColumn( "##sp_h", "Hour", wh24, 0, 23, false, "%02d", col_w_h, 1, true, true, &commit );
                ImGui::SameLine( 0.f, 0.f );
                wm = RenderSpinnerColumn( "##sp_m", "Minute", wm, 0, m_max, false, "%02d", col_w_m, minute_increment, true, false, &commit );
            }
        }

        {
            ImDrawList * pdl    = ImGui::GetWindowDrawList();
            const ImU32 div_col = GetColorU32( ImFluentCol_DividerStrokeDefault );
            const float sep_y0  = group_origin.y;
            const float sep_y1  = group_origin.y + ( float )SPINNER_VISIBLE_ROWS * row_h;
            pdl->AddLine( ImVec2( group_origin.x + col_w_h, sep_y0 ), ImVec2( group_origin.x + col_w_h, sep_y1 ), div_col, 1.f );
            if ( is_12h )
                pdl->AddLine( ImVec2( group_origin.x + col_w_h + col_w_m, sep_y0 ), ImVec2( group_origin.x + col_w_h + col_w_m, sep_y1 ), div_col, 1.f );
        }

        if ( is_12h )
        {
            const int hbase = ( w_disp_h == 12 ) ? 0 : w_disp_h;
            wh24            = hbase + ( w_ap == 1 ? 12 : 0 );
        }

        st->SetInt( hr_key, wh24 );
        st->SetInt( mn_key, wm );

        if ( RenderPickerActionFooter( st, init_key, total_w, commit ) == 1 )
        {
            time->Hour   = wh24;
            time->Minute = wm;
            changed      = true;
        }
        ImGui::EndPopup();
    }
    PopOverlayWindowStyle();

    st->SetBool( was_key, popup_open );

    if ( label )
    {
        const char * label_end = ImGui::FindRenderedTextEnd( label );
        if ( label_end > label )
        {
            ImGui::SameLine();
            ImGui::TextUnformatted( label, label_end );
        }
    }

    RenderAndConsumePendingError( bb );
    return changed;
}

static ImVec2 CalendarViewCalcSize()
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    const float cell            = ImFluent::FluentDpx( style.ControlHeight + style.SpacingSmall );
    const float grid_w          = cell * 7.f;
    const float header_h        = ImFluent::FluentDpx( style.ControlHeight + style.SpacingMedium );
    const float dow_h           = ImFluent::FluentDpx( style.ControlHeight - style.SpacingMedium );
    const float day_grid_h      = 6.f * cell;
    return ImVec2( grid_w, header_h + dow_h + day_grid_h );
}

static void CalendarAddDays( int & y, int & m, int & d, int n )
{
    while ( n > 0 )
    {
        const int dim = DaysInMonth( y, m );
        if ( d + n <= dim )
        {
            d += n;
            n = 0;
        }
        else
        {
            n -= dim - d + 1;
            d = 1;
            if ( ++m > 12 )
            {
                m = 1;
                ++y;
            }
        }
    }
    while ( n < 0 )
    {
        if ( d + n >= 1 )
        {
            d += n;
            n = 0;
        }
        else
        {
            n += d;
            if ( --m < 1 )
            {
                m = 12;
                --y;
            }
            d = DaysInMonth( y, m );
        }
    }
}

static void CalendarAddMonths( int & y, int & m, int & d, int n )
{
    const int total = y * 12 + ( m - 1 ) + n;
    y               = total / 12;
    m               = total % 12 + 1;
    d               = ImMin( d, DaysInMonth( y, m ) );
}

static void CalendarClampToRange( int & y, int & m, int & d, const ImFluent::ImFluentDate * min_d, const ImFluent::ImFluentDate * max_d )
{
    if ( min_d && CompareDate( y, m, d, min_d->Year, min_d->Month, min_d->Day ) < 0 )
    {
        y = min_d->Year;
        m = min_d->Month;
        d = min_d->Day;
    }
    if ( max_d && CompareDate( y, m, d, max_d->Year, max_d->Month, max_d->Day ) > 0 )
    {
        y = max_d->Year;
        m = max_d->Month;
        d = max_d->Day;
    }
}

// WinUI CalendarView keys: header, Previous and Next are tab stops, the grid is ONE more, on the focus date. Arrows, Home/End and
// PageUp/PageDown move the focus date and never leave the grid; Enter/Space select or drill down; Ctrl+Up/Down zoom.
bool ImFluent::CalendarView( const char * id, ImFluentDate * date, const ImFluentDate * min_date, const ImFluentDate * max_date )
{
    if ( !date )
        return false;
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;

    const ImFluentStyle & style = GetStyle();
    const float cell            = FluentDpx( style.ControlHeight + style.SpacingSmall );
    const float grid_w          = cell * 7.f;
    const float header_h        = FluentDpx( style.ControlHeight + style.SpacingMedium );
    const float dow_h           = FluentDpx( style.ControlHeight - style.SpacingMedium );
    const float day_grid_h      = 6.f * cell;
    const float chev_btn_w      = FluentDpx( style.ControlHeight );
    const float r2              = FluentDpx( style.ControlCornerRadius );

    ImGui::PushID( id );
    const ImGuiID scope_id    = w->GetID( "##cv_scope" );
    ImGuiStorage * st         = &w->StateStorage;
    const ImGuiID viewy_key   = scope_id ^ CAL_KEY_VIEW_Y;
    const ImGuiID viewm_key   = scope_id ^ CAL_KEY_VIEW_M;
    const ImGuiID mode_key    = scope_id ^ CAL_KEY_MODE;
    const ImGuiID frame_key   = scope_id ^ CAL_KEY_WAS;
    const ImGuiID focus_y_key = scope_id ^ CAL_KEY_FOCUS_Y;
    const ImGuiID focus_m_key = scope_id ^ CAL_KEY_FOCUS_M;
    const ImGuiID focus_d_key = scope_id ^ CAL_KEY_FOCUS_D;
    const ImGuiID grid_id     = w->GetID( "##cal_grid" );

    int today_y, today_m, today_d;
    GetTodayDate( today_y, today_m, today_d );

    const bool reopened = !AnimFrameContiguous( st, frame_key );
    const bool has_date = date->Year > 0;
    const int start_y   = has_date ? date->Year : today_y;
    const int start_m   = has_date ? date->Month : today_m;
    const int start_d   = has_date ? date->Day : today_d;

    int view_y    = st->GetInt( viewy_key, start_y );
    int view_m    = st->GetInt( viewm_key, start_m );
    int view_mode = st->GetInt( mode_key, 0 );
    int focus_y   = st->GetInt( focus_y_key, start_y );
    int focus_m   = st->GetInt( focus_m_key, start_m );
    int focus_d   = st->GetInt( focus_d_key, start_d );

    if ( reopened )
    {
        view_y = focus_y = start_y;
        view_m = focus_m = start_m;
        focus_d          = start_d;
        view_mode = 0;
    }

    // Keyboard on the grid focus stop, before layout so this frame already shows the result.
    ImGuiContext & g = *GImGui;
    if ( g.NavId == grid_id )
    {
        const bool zoom_out = IsFocusedItemChordPressed( grid_id, g_Ctx.Actions.CalendarZoomOut, false );
        const bool zoom_in  = IsFocusedItemChordPressed( grid_id, g_Ctx.Actions.CalendarZoomIn, false );
        if ( zoom_out || zoom_in )
        {
            if ( zoom_out && view_mode < 2 )
                view_mode++;
            if ( zoom_in && view_mode > 0 )
                view_mode--;
            view_y = focus_y;
            view_m = focus_m;
        }
        else if ( g.IO.KeyCtrl ) // Other Ctrl+arrows do nothing
        {
            for ( int dir = ImGuiDir_Left; dir <= ImGuiDir_Down; ++dir )
                ConsumeNavArrow( grid_id, ( ImGuiDir )dir );
        }
        else
        {
            const int dx      = ConsumeNavArrow( grid_id, ImGuiDir_Left ) ? -1 : ConsumeNavArrow( grid_id, ImGuiDir_Right ) ? 1 : 0;
            const int dy      = ConsumeNavArrow( grid_id, ImGuiDir_Up ) ? -1 : ConsumeNavArrow( grid_id, ImGuiDir_Down ) ? 1 : 0;
            const bool k_home = IsFocusedItemChordPressed( grid_id, g_Ctx.Actions.FirstItem );
            const bool k_end  = IsFocusedItemChordPressed( grid_id, g_Ctx.Actions.LastItem );
            const bool k_pgup = IsFocusedItemChordPressed( grid_id, g_Ctx.Actions.PageUp );
            const bool k_pgdn = IsFocusedItemChordPressed( grid_id, g_Ctx.Actions.PageDown );
            int ny = focus_y, nm = focus_m, nd = focus_d;
            if ( view_mode == 0 )
            {
                if ( dx )
                {
                    CalendarAddDays( ny, nm, nd, dx );
                    CalendarClampToRange( ny, nm, nd, min_date, max_date );
                }
                if ( dy )
                {
                    int ty = ny, tm = nm, td = nd;
                    CalendarAddDays( ty, tm, td, dy * 7 );
                    if ( DateInRange( ty, tm, td, min_date, max_date ) ) // No move past MinDate/MaxDate
                    {
                        ny = ty;
                        nm = tm;
                        nd = td;
                    }
                }
                if ( k_home || k_end )
                {
                    ny = view_y;
                    nm = view_m;
                    nd = k_home ? 1 : DaysInMonth( view_y, view_m );
                    CalendarClampToRange( ny, nm, nd, min_date, max_date );
                }
                if ( k_pgup || k_pgdn )
                {
                    CalendarAddMonths( ny, nm, nd, k_pgup ? -1 : 1 );
                    CalendarClampToRange( ny, nm, nd, min_date, max_date );
                }
                if ( ny != focus_y || nm != focus_m )
                {
                    view_y = ny;
                    view_m = nm;
                }
            }
            else if ( view_mode == 1 )
            {
                int ty = ny, tm = nm, td = nd;
                if ( dx || dy )
                    CalendarAddMonths( ty, tm, td, dx + dy * 4 );
                if ( k_home || k_end )
                    tm = k_home ? 1 : 12;
                if ( k_pgup || k_pgdn )
                    ty += k_pgup ? -1 : 1;
                if ( ( ty != ny || tm != nm ) && MonthOverlapsRange( ty, tm, min_date, max_date ) )
                {
                    ny = ty;
                    nm = tm;
                }
                nd     = ImMin( nd, DaysInMonth( ny, nm ) );
                view_y = ny;
            }
            else
            {
                const int decade_start = ( view_y / 10 ) * 10;
                int ty                 = ny + dx + dy * 4;
                if ( k_home || k_end )
                    ty = k_home ? decade_start : decade_start + 9;
                if ( k_pgup || k_pgdn )
                    ty += k_pgup ? -10 : 10;
                if ( ty != ny && YearOverlapsRange( ty, min_date, max_date ) )
                    ny = ty;
                nd     = ImMin( nd, DaysInMonth( ny, nm ) );
                view_y = ny;
            }
            focus_y = ny;
            focus_m = nm;
            focus_d = nd;
        }
    }

    ImFluentStackGuard sg;
    sg.PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 0.f, 0.f ) );
    sg.BeginGroup();

    bool changed = false;

    const ImVec2 hdr_origin = ImGui::GetCursorScreenPos();
    ImDrawList * pdl        = w->DrawList;
    // WinUI CalendarView sits on a bordered panel. Inside a flyout (CalendarDatePicker) the flyout is the frame.
    if ( !( w->Flags & ImGuiWindowFlags_Popup ) )
    {
        const float pr = FluentDpx( style.OverlayCornerRadius );
        const ImVec2 p0( hdr_origin.x - 1.f, hdr_origin.y - 1.f );
        const ImVec2 p1( hdr_origin.x + grid_w + 1.f, hdr_origin.y + header_h + dow_h + day_grid_h + 1.f );
        pdl->AddRectFilled( p0, p1, GetColorU32( ImFluentCol_ControlFillInputActive ), pr );
        StrokeRect( pdl, p0, p1, GetColorU32( ImFluentCol_CardStrokeDefault ), pr );
    }

    {
        const ImVec2 cv_size = CalendarViewCalcSize();
        const ImRect cv_bb( hdr_origin, ImVec2( hdr_origin.x + cv_size.x, hdr_origin.y + cv_size.y ) );
        if ( IsMouseOverWindowRect( cv_bb ) )
        {
            ImGui::SetKeyOwner( ImGuiKey_MouseWheelY, scope_id );
        }
        if ( ImGui::TestKeyOwner( ImGuiKey_MouseWheelY, scope_id ) )
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            const int steps   = ( wheel >= 0.f ) ? ( int )ImFloor( wheel ) : ( int )ImCeil( wheel );
            if ( steps != 0 )
            {
                if ( view_mode == 0 )
                {
                    view_m -= steps;
                    while ( view_m < 1 )
                    {
                        view_m += 12;
                        view_y--;
                    }
                    while ( view_m > 12 )
                    {
                        view_m -= 12;
                        view_y++;
                    }
                }
                else if ( view_mode == 1 )
                {
                    view_y -= steps;
                }
                else
                {
                    view_y -= steps * 10;
                }
            }
        }
    }

    char title_buf[ 64 ];
    if ( view_mode == 0 )
    {
        const char * mname = LocalizeGetMsg( ( ImFluentLocKey )( ImFluentLocKey_MonthJanuary + view_m - 1 ) );
        ImFormatString( title_buf, sizeof( title_buf ), "%s %d", mname, view_y );
    }
    else if ( view_mode == 1 )
    {
        ImFormatString( title_buf, sizeof( title_buf ), "%d", view_y );
    }
    else
    {
        const int decade_start = ( view_y / 10 ) * 10;
        ImFormatString( title_buf, sizeof( title_buf ), "%d - %d", decade_start, decade_start + 9 );
    }

    const float title_w = grid_w - chev_btn_w * 2.f;
    const ImRect title_bb( hdr_origin, ImVec2( hdr_origin.x + title_w, hdr_origin.y + header_h ) );

    ImGui::SetCursorScreenPos( hdr_origin );
    const ImGuiID title_id = w->GetID( "##cal_title" );
    ImGui::ItemSize( title_bb );
    // The header button has nothing above the decade view: it is disabled there and keyboard focus skips it.
    if ( view_mode >= 2 )
        ImGui::PushItemFlag( ImGuiItemFlags_Disabled, true );
    const bool title_added = ImGui::ItemAdd( title_bb, title_id );
    if ( view_mode >= 2 )
        ImGui::PopItemFlag();
    if ( title_added )
    {
        bool t_hov = false, t_held = false;
        const bool t_pr = view_mode < 2 ? ImGui::ButtonBehavior( title_bb, title_id, &t_hov, &t_held ) : false;
        ReportItemInfo( title_id, title_buf );
        ImU32 t_fill_target;
        if ( t_held )
            t_fill_target = GetColorU32( ImFluentCol_SubtleFillTertiary );
        else if ( t_hov )
            t_fill_target = GetColorU32( ImFluentCol_SubtleFillSecondary );
        else
            t_fill_target = GetColorU32( ImFluentCol_SubtleFillTransparent );
        const ImU32 t_fill = AnimateColorU32( title_id, t_fill_target );
        pdl->AddRectFilled( title_bb.Min, title_bb.Max, t_fill, r2 );

        const ImVec2 t_ts = ImGui::CalcTextSize( title_buf );
        const float t_pad = FluentDpx( style.SpacingMedium );
        pdl->AddText( ImVec2( title_bb.Min.x + t_pad, title_bb.Min.y + ( header_h - t_ts.y ) * 0.5f ), GetColorU32( ImFluentCol_TextPrimary ), title_buf );
        RenderNavFocusRing( title_id, title_bb, r2 );

        if ( t_pr )
        {
            if ( view_mode == 0 )
                view_mode = 1;
            else if ( view_mode == 1 )
                view_mode = 2;
        }
    }

    const float chev_y = hdr_origin.y + ( header_h - chev_btn_w ) * 0.5f;
    const float prev_x = hdr_origin.x + grid_w - chev_btn_w * 2.f;
    const float next_x = hdr_origin.x + grid_w - chev_btn_w;

    ImGui::PushItemFlag( ImGuiItemFlags_ButtonRepeat, g_Ctx.Actions.RepeatStepButtons );
    ImGui::SetCursorScreenPos( ImVec2( prev_x, chev_y ) );
    if ( RenderPickerActionButton( ImFluentIcon_ChevronUpMed "##prev", "Previous", ImVec2( chev_btn_w, chev_btn_w ) ) )
    {
        if ( view_mode == 0 )
        {
            view_m--;
            if ( view_m < 1 )
            {
                view_m = 12;
                view_y--;
            }
        }
        else if ( view_mode == 1 )
        {
            view_y--;
        }
        else
        {
            view_y -= 10;
        }
    }
    ImGui::SetCursorScreenPos( ImVec2( next_x, chev_y ) );
    if ( RenderPickerActionButton( ImFluentIcon_ChevronDownMed "##next", "Next", ImVec2( chev_btn_w, chev_btn_w ) ) )
    {
        if ( view_mode == 0 )
        {
            view_m++;
            if ( view_m > 12 )
            {
                view_m = 1;
                view_y++;
            }
        }
        else if ( view_mode == 1 )
        {
            view_y++;
        }
        else
        {
            view_y += 10;
        }
    }
    ImGui::PopItemFlag();

    {
        const float div_y = hdr_origin.y + header_h - 1.f;
        pdl->AddLine( ImVec2( hdr_origin.x, div_y ), ImVec2( hdr_origin.x + grid_w, div_y ), GetColorU32( ImFluentCol_DividerStrokeDefault ), 1.f );
    }

    if ( view_mode == 0 )
    {
        ImGui::SetCursorScreenPos( ImVec2( hdr_origin.x, hdr_origin.y + header_h ) );
        const ImVec2 dow_origin = ImGui::GetCursorScreenPos();
        pdl->AddRectFilled( dow_origin, ImVec2( dow_origin.x + grid_w, dow_origin.y + dow_h ), GetColorU32( ImFluentCol_LayerFillDefault ) );
        for ( int i = 0; i < 7; ++i )
        {
            const char * dn = LocalizeGetMsg( ( ImFluentLocKey )( ImFluentLocKey_DayMon + i ) );
            const ImVec2 ts = ImGui::CalcTextSize( dn );
            pdl->AddText( ImVec2( dow_origin.x + i * cell + ( cell - ts.x ) * 0.5f, dow_origin.y + ( dow_h - ts.y ) * 0.5f ), GetColorU32( ImFluentCol_TextSecondary ), dn );
        }
        ImGui::SetCursorScreenPos( ImVec2( dow_origin.x, dow_origin.y + dow_h ) );
        const ImVec2 grid_origin = ImGui::GetCursorScreenPos();

        const int dim      = DaysInMonth( view_y, view_m );
        const int day1_dow = DayOfWeekMonStart( view_y, view_m, 1 );

        int prev_m = view_m - 1;
        int prev_y = view_y;
        if ( prev_m < 1 )
        {
            prev_m = 12;
            prev_y--;
        }
        const int prev_dim = DaysInMonth( prev_y, prev_m );

        int next_m = view_m + 1;
        int next_y = view_y;
        if ( next_m > 12 )
        {
            next_m = 1;
            next_y++;
        }

        // A focus date outside the shown grid (after Previous/Next) moves to the same day of the shown month.
        {
            const bool in_view = focus_y == view_y && focus_m == view_m;
            const bool in_prev = focus_y == prev_y && focus_m == prev_m && focus_d > prev_dim - day1_dow;
            const bool in_next = focus_y == next_y && focus_m == next_m && focus_d <= 42 - day1_dow - dim;
            if ( !in_view && !in_prev && !in_next )
            {
                focus_y = view_y;
                focus_m = view_m;
                focus_d = ImMin( focus_d, dim );
                CalendarClampToRange( focus_y, focus_m, focus_d, min_date, max_date );
            }
        }

        const ImU32 col_subtle_trans  = GetColorU32( ImFluentCol_SubtleFillTransparent );
        const ImU32 col_subtle_sec    = GetColorU32( ImFluentCol_SubtleFillSecondary );
        const ImU32 col_subtle_ter    = GetColorU32( ImFluentCol_SubtleFillTertiary );
        const ImU32 col_accent_fill   = GetColorU32( ImFluentCol_AccentFillDefault );
        const ImU32 col_text_disabled = GetColorU32( ImFluentCol_TextDisabled );
        const ImU32 col_text_on_acc   = GetColorU32( ImFluentCol_TextOnAccentPrimary );
        const ImU32 col_text_tert     = GetColorU32( ImFluentCol_TextTertiary );
        const ImU32 col_text_primary  = GetColorU32( ImFluentCol_TextPrimary );
        const float circ_r            = cell * 0.5f - FluentDpx( style.SpacingXSmall );
        const float today_th          = FluentDpx( style.StrokeMedium );

        for ( int row = 0; row < 6; ++row )
        {
            for ( int col = 0; col < 7; ++col )
            {
                const int idx = row * 7 + col;
                int cell_y, cell_m, cell_d;
                bool other_month = false;
                if ( idx < day1_dow )
                {
                    cell_y      = prev_y;
                    cell_m      = prev_m;
                    cell_d      = prev_dim - ( day1_dow - 1 - idx );
                    other_month = true;
                }
                else if ( idx < day1_dow + dim )
                {
                    cell_y = view_y;
                    cell_m = view_m;
                    cell_d = idx - day1_dow + 1;
                }
                else
                {
                    cell_y      = next_y;
                    cell_m      = next_m;
                    cell_d      = idx - day1_dow - dim + 1;
                    other_month = true;
                }

                const ImVec2 cp( grid_origin.x + col * cell, grid_origin.y + row * cell );
                const ImRect cbb( cp, ImVec2( cp.x + cell, cp.y + cell ) );

                // The focus date's cell carries the grid's one focus stop; other cells answer the mouse only.
                const bool is_focus = ( cell_y == focus_y && cell_m == focus_m && cell_d == focus_d );
                ImGui::PushID( idx );
                const ImGuiID cell_id = is_focus ? grid_id : w->GetID( "##d" );
                ImGui::PopID();

                ImGui::ItemSize( cbb );
                if ( !ImGui::ItemAdd( cbb, cell_id, NULL, is_focus ? ImGuiItemFlags_None : ImGuiItemFlags_NoNav ) )
                    continue;

                const bool out_of_range = !DateInRange( cell_y, cell_m, cell_d, min_date, max_date );

                bool chov = false, chld = false;
                const bool cpr = !out_of_range && ImGui::ButtonBehavior( cbb, cell_id, &chov, &chld, is_focus ? ImGuiButtonFlags_None : ImGuiButtonFlags_NoNavFocus );

                const bool is_sel = ( cell_y == date->Year && cell_m == date->Month && cell_d == date->Day );
                {
                    char cell_label[ 16 ];
                    ImFormatString( cell_label, sizeof( cell_label ), "%04d-%02d-%02d", cell_y, cell_m, cell_d );
                    ReportItemInfo( cell_id, cell_label, CheckedFlags( is_sel ) );
                }

                ImU32 fill_target;
                if ( out_of_range )
                    fill_target = col_subtle_trans;
                else if ( is_sel )
                    fill_target = col_accent_fill;
                else if ( chld )
                    fill_target = col_subtle_ter;
                else if ( chov )
                    fill_target = col_subtle_sec;
                else
                    fill_target = col_subtle_trans;
                const ImU32 fill = AnimateColorU32( cell_id, fill_target );

                const float cx = ( cbb.Min.x + cbb.Max.x ) * 0.5f;
                const float cy = ( cbb.Min.y + cbb.Max.y ) * 0.5f;
                pdl->AddCircleFilled( ImVec2( cx, cy ), circ_r, fill, 24 );

                const bool is_today = ( cell_y == today_y && cell_m == today_m && cell_d == today_d );
                if ( is_today && !is_sel && !out_of_range )
                {
                    pdl->AddCircle( ImVec2( cx, cy ), circ_r, col_accent_fill, 24, today_th );
                }

                ImU32 cell_text_col;
                if ( out_of_range )
                    cell_text_col = col_text_disabled;
                else if ( is_sel )
                    cell_text_col = col_text_on_acc;
                else if ( other_month )
                    cell_text_col = col_text_tert;
                else
                    cell_text_col = col_text_primary;

                char ds[ 8 ];
                ImFormatString( ds, sizeof( ds ), "%d", cell_d );
                const ImVec2 ts = ImGui::CalcTextSize( ds );
                pdl->AddText( ImVec2( cx - ts.x * 0.5f, cy - ts.y * 0.5f ), cell_text_col, ds );
                if ( is_focus )
                    RenderNavFocusRing( grid_id, ImRect( cx - circ_r, cy - circ_r, cx + circ_r, cy + circ_r ), circ_r );

                if ( cpr )
                {
                    date->Year  = cell_y;
                    date->Month = cell_m;
                    date->Day   = cell_d;
                    changed     = true;
                    view_y      = cell_y;
                    view_m      = cell_m;
                    focus_y     = cell_y;
                    focus_m     = cell_m;
                    focus_d     = cell_d;
                    if ( !is_focus )
                        ImGui::SetFocusID( grid_id, w ); // Keyboard continues from the clicked day
                }
            }
        }
    }
    else
    {
        ImGui::SetCursorScreenPos( ImVec2( hdr_origin.x, hdr_origin.y + header_h ) );
        const ImVec2 grid_origin       = ImGui::GetCursorScreenPos();
        const float cell_w             = grid_w / 4.f;
        const float cell_h             = day_grid_h / 3.f;
        const float cell_inset         = FluentDpx( style.SpacingSmall );
        const float my_today_th        = FluentDpx( style.StrokeMedium );
        const ImU32 col_subtle_trans2  = GetColorU32( ImFluentCol_SubtleFillTransparent );
        const ImU32 col_subtle_sec2    = GetColorU32( ImFluentCol_SubtleFillSecondary );
        const ImU32 col_subtle_ter2    = GetColorU32( ImFluentCol_SubtleFillTertiary );
        const ImU32 col_accent_fill2   = GetColorU32( ImFluentCol_AccentFillDefault );
        const ImU32 col_text_disabled2 = GetColorU32( ImFluentCol_TextDisabled );
        const ImU32 col_text_on_acc2   = GetColorU32( ImFluentCol_TextOnAccentPrimary );
        const ImU32 col_text_tert2     = GetColorU32( ImFluentCol_TextTertiary );
        const ImU32 col_text_primary2  = GetColorU32( ImFluentCol_TextPrimary );
        const int decade_start         = ( view_y / 10 ) * 10;
        const int grid_mode            = view_mode; // A drill-down below changes view_mode for the next frame only

        // Same rule as the month view: the focus stop follows the shown year or decade.
        if ( view_mode == 1 && focus_y != view_y )
            focus_y = view_y;
        if ( view_mode == 2 && ( focus_y < decade_start - 1 || focus_y > decade_start + 10 ) )
            focus_y = ImClamp( focus_y, decade_start, decade_start + 9 );

        for ( int row = 0; row < 3; ++row )
        {
            for ( int col = 0; col < 4; ++col )
            {
                const int idx = row * 4 + col;
                int cell_val;
                bool other_range   = false;
                bool is_sel        = false;
                bool is_today_cell = false;
                bool out_of_range  = false;
                char buf2[ 16 ];
                if ( grid_mode == 1 )
                {
                    cell_val        = idx + 1;
                    is_sel          = ( view_y == date->Year && cell_val == date->Month );
                    is_today_cell   = ( view_y == today_y && cell_val == today_m );
                    out_of_range    = !MonthOverlapsRange( view_y, cell_val, min_date, max_date );
                    const char * mn = LocalizeGetMsg( ( ImFluentLocKey )( ImFluentLocKey_MonthJanuary + idx ) );
                    int ci          = 0;
                    int cp          = 0;
                    while ( cp < 3 && mn[ ci ] && ci < ( int )sizeof( buf2 ) - 1 )
                    {
                        buf2[ ci ] = mn[ ci ];
                        ++ci;
                        while ( mn[ ci ] && ( ( unsigned char )mn[ ci ] & 0xC0 ) == 0x80 && ci < ( int )sizeof( buf2 ) - 1 )
                        {
                            buf2[ ci ] = mn[ ci ];
                            ++ci;
                        }
                        ++cp;
                    }
                    buf2[ ci ] = 0;
                }
                else
                {
                    cell_val      = decade_start - 1 + idx;
                    other_range   = ( cell_val < decade_start || cell_val > decade_start + 9 );
                    is_sel        = ( cell_val == date->Year );
                    is_today_cell = ( cell_val == today_y );
                    out_of_range  = !YearOverlapsRange( cell_val, min_date, max_date );
                    ImFormatString( buf2, sizeof( buf2 ), "%d", cell_val );
                }

                const ImVec2 cp( grid_origin.x + col * cell_w, grid_origin.y + row * cell_h );
                const ImRect cbb( cp, ImVec2( cp.x + cell_w, cp.y + cell_h ) );

                const bool is_focus = ( grid_mode == 1 ) ? ( cell_val == focus_m ) : ( cell_val == focus_y );
                ImGui::PushID( idx );
                const ImGuiID cell_id = is_focus ? grid_id : w->GetID( grid_mode == 1 ? "##m" : "##y" );
                ImGui::PopID();

                ImGui::ItemSize( cbb );
                if ( !ImGui::ItemAdd( cbb, cell_id, NULL, is_focus ? ImGuiItemFlags_None : ImGuiItemFlags_NoNav ) )
                    continue;

                bool chov = false, chld = false;
                const bool cpr = !out_of_range && ImGui::ButtonBehavior( cbb, cell_id, &chov, &chld, is_focus ? ImGuiButtonFlags_None : ImGuiButtonFlags_NoNavFocus );
                ReportItemInfo( cell_id, buf2, CheckedFlags( is_sel ) );

                ImU32 fill_target;
                if ( out_of_range )
                    fill_target = col_subtle_trans2;
                else if ( is_sel )
                    fill_target = col_accent_fill2;
                else if ( chld )
                    fill_target = col_subtle_ter2;
                else if ( chov )
                    fill_target = col_subtle_sec2;
                else
                    fill_target = col_subtle_trans2;
                const ImU32 fill = AnimateColorU32( cell_id, fill_target );

                const ImVec2 cell_min( cbb.Min.x + cell_inset, cbb.Min.y + cell_inset );
                const ImVec2 cell_max( cbb.Max.x - cell_inset, cbb.Max.y - cell_inset );
                pdl->AddRectFilled( cell_min, cell_max, fill, r2 );
                if ( is_today_cell && !is_sel && !out_of_range )
                {
                    pdl->AddRect( cell_min, cell_max, col_accent_fill2, r2, 0, my_today_th );
                }

                ImU32 cell_text_col;
                if ( out_of_range )
                    cell_text_col = col_text_disabled2;
                else if ( is_sel )
                    cell_text_col = col_text_on_acc2;
                else if ( other_range )
                    cell_text_col = col_text_tert2;
                else
                    cell_text_col = col_text_primary2;

                ImFluentStackGuard tg2;
                tg2.PushStyleColor( ImGuiCol_Text, cell_text_col );
                const ImRect text_clip( ImVec2( cbb.Min.x + cell_inset, cbb.Min.y ), ImVec2( cbb.Max.x - cell_inset, cbb.Max.y ) );
                ImGui::RenderTextClipped( text_clip.Min, text_clip.Max, buf2, NULL, NULL, ImVec2( 0.5f, 0.5f ), &text_clip );
                if ( is_focus )
                    RenderNavFocusRing( grid_id, ImRect( cell_min, cell_max ), r2 );

                // Enter/Space or a click drills down; focus goes to the same day in the chosen month or year.
                if ( cpr )
                {
                    if ( grid_mode == 1 )
                    {
                        view_m    = cell_val;
                        view_mode = 0;
                        focus_y   = view_y;
                        focus_m   = cell_val;
                    }
                    else
                    {
                        view_y    = cell_val;
                        view_mode = 1;
                        focus_y   = cell_val;
                    }
                    focus_d = ImMin( focus_d, DaysInMonth( focus_y, focus_m ) );
                    if ( !is_focus )
                        ImGui::SetFocusID( grid_id, w );
                }
            }
        }
    }

    st->SetInt( viewy_key, view_y );
    st->SetInt( viewm_key, view_m );
    st->SetInt( mode_key, view_mode );
    st->SetInt( focus_y_key, focus_y );
    st->SetInt( focus_m_key, focus_m );
    st->SetInt( focus_d_key, focus_d );

    sg.Restore();
    ImGui::PopID();
    return changed;
}

bool ImFluent::CalendarDatePicker( const char * label, ImFluentDate * date, const char * hint, const ImFluentDate * min_date, const ImFluentDate * max_date )
{
    if ( !date )
        return false;
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    RenderAndConsumePendingHeader();

    const ImFluentStyle & style = GetStyle();
    const float h               = FluentDpx( style.ControlHeight );
    const float r               = FluentDpx( style.ControlCornerRadius );
    const float full_w          = ImGui::CalcItemWidth();
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + full_w, pos.y + h ) );

    const ImGuiID id = w->GetID( label );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id ) )
        return false;

    bool hovered = false, held = false;
    ImGui::ButtonBehavior( bb, id, &hovered, &held );
    const bool disabled = IsLastItemDisabled();

    const ImGuiID popup_id = GetWidgetPopupId( id, "##fl_cdp_popup" );

    const bool popup_open = PickerTogglePopup( popup_id, bb, id, disabled, false );
    ReportItemInfo( id, label, OpenedFlags( popup_open ) );

    ImDrawList * dl = w->DrawList;
    RenderPickerTriggerFrame( dl, bb, r, hovered, held, popup_open, disabled, id );

    const float pad_x    = FluentDpx( style.SpacingLarge );
    const float icon_w   = h;
    const ImU32 text_col = GetColorU32( disabled ? ImFluentCol_TextDisabled : ImFluentCol_TextPrimary );
    const ImU32 hint_col = GetColorU32( disabled ? ImFluentCol_TextDisabled : ImFluentCol_TextSecondary );

    char buf[ 32 ];
    const char * preview;
    bool is_hint = false;
    if ( date->Year > 0 )
    {
        ImFormatString( buf, sizeof( buf ), "%04d-%02d-%02d", date->Year, date->Month, date->Day );
        preview = buf;
    }
    else
    {
        preview = hint ? hint : LocalizeGetMsg( ImFluentLocKey_DatePickerPickADate );
        is_hint = true;
    }

    {
        ImFluentStackGuard tg;
        tg.PushStyleColor( ImGuiCol_Text, is_hint ? hint_col : text_col );
        const ImVec2 tmin( bb.Min.x + pad_x, bb.Min.y );
        const ImVec2 tmax( bb.Max.x - icon_w, bb.Max.y );
        const ImRect text_clip( tmin, tmax );
        ImGui::RenderTextClipped( tmin, tmax, preview, NULL, NULL, ImVec2( 0.f, 0.5f ), &text_clip );
    }

    {
        const char * icon = ImFluentIcon_Calendar;
        const ImVec2 ts   = ImGui::CalcTextSize( icon );
        const float cx    = bb.Max.x - icon_w * 0.5f;
        const float cy    = ( bb.Min.y + bb.Max.y ) * 0.5f;
        dl->AddText( ImVec2( cx - ts.x * 0.5f, cy - ts.y * 0.5f ), text_col, icon );
    }

    RenderNavFocusRing( id, bb, r );

    bool changed = false;

    const ImVec2 cv_size = CalendarViewCalcSize();
    const float pop_pad  = FluentDpx( style.SpacingMedium );
    const float popup_w  = cv_size.x + pop_pad * 2.f;
    const float popup_h  = cv_size.y + pop_pad * 2.f;

    ImGui::SetNextWindowPos( ImVec2( bb.Min.x, bb.Max.y + FluentDpx( style.SpacingXSmall ) ) );
    ImGui::SetNextWindowSize( ImVec2( popup_w, popup_h ) );
    PushOverlayWindowStyle( style.SpacingMedium, style.SpacingMedium );

    const ImGuiWindowFlags cal_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if ( ImGui::BeginPopupEx( popup_id, cal_flags ) )
    {
        if ( ImFluent::CalendarView( "##cv", date, min_date, max_date ) )
        {
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    PopOverlayWindowStyle();

    if ( label )
    {
        const char * label_end = ImGui::FindRenderedTextEnd( label );
        if ( label_end > label )
        {
            ImGui::SameLine();
            ImGui::TextUnformatted( label, label_end );
        }
    }

    RenderAndConsumePendingError( bb );
    return changed;
}

// [SECTION] PersonPicture / Image

// Initials of a display name: the first letter of the first and of the last word ("Ada Lovelace" -> "AL").
static void PersonInitials( const char * name, char * out, int out_size )
{
    out[ 0 ] = 0;
    if ( !name )
        return;
    const char * first = NULL;
    const char * last  = NULL;
    for ( const char * p = name; *p; ++p )
        if ( *p != ' ' && ( p == name || p[ -1 ] == ' ' ) )
        {
            if ( !first )
                first = p;
            last = p;
        }
    int n = 0;
    for ( const char * src : { first, last != first ? last : NULL } )
    {
        if ( !src )
            continue;
        unsigned int c   = 0;
        const int length = ImTextCharFromUtf8( &c, src, NULL );
        if ( n + length >= out_size )
            break;
        if ( c < 128 )
            out[ n++ ] = ( char )ImToUpper( ( char )c );
        else
            for ( int i = 0; i < length; ++i )
                out[ n++ ] = src[ i ];
    }
    out[ n ] = 0;
}

static bool HasTexture( ImTextureRef texture )
{
    return texture._TexData != NULL || texture._TexID != ImTextureID_Invalid;
}

void ImFluent::PersonPicture( const char * display_name, float size, int badge_number, ImTextureRef texture, const char * initials )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const float D               = size > 0.f ? size : FluentDpx( 100.f );
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + D, pos.y + D ) );
    const ImGuiID id = w->GetID( display_name && *display_name ? display_name : "##person" );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id, NULL, ImGuiItemFlags_NoNav ) )
        return;
    ReportItemInfo( id, display_name ? display_name : "PersonPicture" );

    ImDrawList * dl = w->DrawList;
    const ImVec2 c( pos.x + D * 0.5f, pos.y + D * 0.5f );
    if ( HasTexture( texture ) )
        dl->AddImageRounded( texture, bb.Min, bb.Max, ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32_WHITE, D * 0.5f );
    else
    {
        dl->AddCircleFilled( c, D * 0.5f, ImFluent::GetColorU32( ImFluentCol_ControlAltFillQuarternary ), 48 );
        char buf[ 16 ];
        if ( initials && *initials )
            ImStrncpy( buf, initials, sizeof( buf ) );
        else
            PersonInitials( display_name, buf, sizeof( buf ) );
        // WinUI: initials at about 40% of the picture size; the person glyph when there is no name.
        const char * text = buf[ 0 ] ? buf : ImFluentIcon_Contact;
        ImFluent::RenderCenteredText( dl, c, D * 0.4f, ImFluent::GetColorU32( ImFluentCol_TextPrimary ), text );
    }

    if ( badge_number > 0 )
    {
        const float bd = ImMax( FluentDpx( style.BadgeHeight ), D * 0.25f );
        const ImVec2 bc( bb.Max.x - bd * 0.5f, bb.Min.y + bd * 0.5f );
        char num[ 8 ];
        ImFormatString( num, sizeof( num ), badge_number > 99 ? "99+" : "%d", badge_number );
        dl->AddCircleFilled( bc, bd * 0.5f + FluentDpx( style.StrokeMedium ), ImFluent::GetColorU32( ImFluentCol_SolidBgBase ), 32 );
        dl->AddCircleFilled( bc, bd * 0.5f, ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), 32 );
        ImFluent::RenderCenteredText( dl, bc, bd * 0.6f, ImFluent::GetColorU32( ImFluentCol_TextOnAccentPrimary ), num );
    }
}

void ImFluent::Image( ImTextureRef texture, const ImVec2 & size, float corner_radius, const ImVec2 & uv0, const ImVec2 & uv1 )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + size.x, pos.y + size.y ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, 0 ) )
        return;
    const float r = corner_radius >= 0.f ? corner_radius : FluentDpx( style.OverlayCornerRadius );
    if ( HasTexture( texture ) )
        w->DrawList->AddImageRounded( texture, bb.Min, bb.Max, uv0, uv1, IM_COL32_WHITE, r );
    else
        w->DrawList->AddRectFilled( bb.Min, bb.Max, ImFluent::GetColorU32( ImFluentCol_ControlAltFillQuarternary ), r );
}

// [SECTION] Border / ScrollViewer

bool ImFluent::BeginBorder( const char * id, const ImVec2 & size, ImFluentCol background, ImFluentCol stroke, float thickness, float corner_radius, const ImVec2 & padding )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImVec2 pad            = ImVec2( padding.x >= 0.f ? padding.x : FluentDpx( style.SpacingLarge ), padding.y >= 0.f ? padding.y : FluentDpx( style.SpacingLarge ) );
    ImGui::PushStyleColor( ImGuiCol_ChildBg, ImFluent::GetColorU32( background ) );
    ImGui::PushStyleColor( ImGuiCol_Border, ImFluent::GetColorU32( stroke ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, corner_radius >= 0.f ? corner_radius : FluentDpx( style.ControlCornerRadius ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ChildBorderSize, thickness );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, pad );
    ImGuiChildFlags flags = ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened;
    if ( thickness > 0.f )
        flags |= ImGuiChildFlags_Borders;
    if ( size.x == 0.f )
        flags |= ImGuiChildFlags_AutoResizeX;
    if ( size.y == 0.f )
        flags |= ImGuiChildFlags_AutoResizeY;
    const bool visible = ImGui::BeginChild( id, size, flags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    ImGui::PopStyleVar( 3 );
    ImGui::PopStyleColor( 2 );
    return visible;
}

void ImFluent::EndBorder()
{
    ImGui::EndChild();
}

bool ImFluent::BeginScrollViewer( const char * id, const ImVec2 & size, bool horizontal )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    // Fluent scroll bars: a thin rounded thumb on a clear track.
    ImGui::PushStyleColor( ImGuiCol_ScrollbarBg, IM_COL32( 0, 0, 0, 0 ) );
    ImGui::PushStyleColor( ImGuiCol_ScrollbarGrab, ImFluent::GetColorU32( ImFluentCol_ControlStrongFillDefault ) );
    ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabHovered, ImFluent::GetColorU32( ImFluentCol_TextSecondary ) );
    ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabActive, ImFluent::GetColorU32( ImFluentCol_TextPrimary ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ScrollbarSize, FluentDpx( style.SpacingMedium ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ScrollbarRounding, FluentDpx( style.SpacingMedium ) );
    return ImGui::BeginChild( id, size, ImGuiChildFlags_NavFlattened, horizontal ? ImGuiWindowFlags_HorizontalScrollbar : ImGuiWindowFlags_None );
}

void ImFluent::EndScrollViewer()
{
    ImGui::EndChild();
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor( 4 );
}

// [SECTION] FlipView / Pivot

// WinUI FlipView: one tab stop, Left/Right without wrap; previous/next buttons show on hover or focus and are not tab stops.
bool ImFluent::BeginFlipView( const char * id, int * current_item, int items_count, const ImVec2 & size )
{
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImGui::PushID( id );
    g_Ctx.FlipViewStack.push_back( { ImGui::GetID( "##flip" ), id, current_item, items_count } );
    ImGui::PushStyleColor( ImGuiCol_ChildBg, ImFluent::GetColorU32( ImFluentCol_CardBgDefault ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, FluentDpx( style.OverlayCornerRadius ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.f, 0.f ) );
    const bool visible = ImGui::BeginChild( "##flip-content", size, ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor();
    return visible;
}

static bool FlipViewButton( const char * str_id, const char * name, const ImRect & bb, const char * glyph )
{
    ImGuiWindow * w             = ImGui::GetCurrentWindow();
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( str_id );
    if ( !ImGui::ItemAdd( bb, id, NULL, ImGuiItemFlags_NoNav | ( g_Ctx.Actions.RepeatStepButtons ? ImGuiItemFlags_ButtonRepeat : 0 ) ) )
        return false;
    bool hov = false, held = false;
    const bool pressed = ImGui::ButtonBehavior( bb, id, &hov, &held );
    ImFluent::ReportItemInfo( id, name );
    const float r   = ImFluent::FluentDpx( style.ControlCornerRadius );
    ImDrawList * dl = w->DrawList;
    dl->AddRectFilled( bb.Min, bb.Max, ImFluent::AnimateColorU32( id, ImFluent::ResolveControlFillState( false, held, hov ) ), r );
    ImFluent::StrokeRect( dl, bb.Min, bb.Max, ImFluent::GetColorU32( ImFluentCol_ControlStrokeDefault ), r );
    ImFluent::RenderCenteredText( dl, bb.GetCenter(), ImGui::GetFontSize() * 0.7f, ImFluent::GetColorU32( ImFluentCol_TextSecondary ), glyph );
    return pressed;
}

void ImFluent::EndFlipView()
{
    IM_ASSERT( !g_Ctx.FlipViewStack.empty() && "EndFlipView() without BeginFlipView()" );
    const ImFluentContext::FlipViewState st = g_Ctx.FlipViewStack.back();
    g_Ctx.FlipViewStack.pop_back();
    ImGuiContext & g            = *GImGui;
    const ImFluentStyle & style = ImFluent::GetStyle();
    int cur                     = st.Current ? ImClamp( *st.Current, 0, ImMax( 0, st.Count - 1 ) ) : 0;

    // Overlay buttons, drawn inside the child so they sit above the item content.
    ImGuiWindow * cw  = ImGui::GetCurrentWindow();
    const ImRect area = cw->Rect();
    const bool show   = st.Count > 1 && ( ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) || g.NavId == st.Id );
    if ( show )
    {
        const float bw  = FluentDpx( 20.f );
        const float bh  = FluentDpx( 36.f );
        const float pad = FluentDpx( style.SpacingXSmall );
        const float cy  = ( area.Min.y + area.Max.y ) * 0.5f;
        if ( cur > 0 && FlipViewButton( "##flip-prev", "Previous", ImRect( area.Min.x + pad, cy - bh * 0.5f, area.Min.x + pad + bw, cy + bh * 0.5f ), ImFluentIcon_ChevronLeft ) )
            --cur;
        if ( cur < st.Count - 1 && FlipViewButton( "##flip-next", "Next", ImRect( area.Max.x - pad - bw, cy - bh * 0.5f, area.Max.x - pad, cy + bh * 0.5f ), ImFluentIcon_ChevronRight ) )
            ++cur;
    }
    ImGui::EndChild();

    const ImRect view( ImGui::GetItemRectMin(), ImGui::GetItemRectMax() );
    if ( ImGui::ItemAdd( view, st.Id ) )
    {
        ReportItemInfo( st.Id, st.Label );
        if ( g.NavId == st.Id )
        {
            if ( ConsumeNavArrow( st.Id, ImGuiDir_Left ) && cur > 0 )
                --cur;
            if ( ConsumeNavArrow( st.Id, ImGuiDir_Right ) && cur < st.Count - 1 )
                ++cur;
        }
        RenderNavFocusRing( st.Id, view, FluentDpx( style.OverlayCornerRadius ) );
    }
    if ( st.Current )
        *st.Current = cur;
    ImGui::PopID();
}

// WinUI Pivot: an accent pipe under the selected header; the headers are one tab stop and Left/Right wrap.
bool ImFluent::BeginPivot( const char * id )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    ImGui::PushID( id );
    ImGui::BeginGroup();
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( FluentDpx( style.SpacingSmall ), 0.f ) );
    ImFluent::PushFont( ImFluentTextStyle_Subtitle );
    BeginTabGroup( ImGui::GetID( "##pivot-group" ), ImFluentTabGroupFlags_Horizontal | ImFluentTabGroupFlags_Wrap | ImFluentTabGroupFlags_SelectionFollowsFocus );
    return true;
}

bool ImFluent::PivotItem( const char * header, bool selected )
{
    ImGuiWindow * w = ImGui::GetCurrentWindow();
    if ( w->SkipItems )
        return false;
    const ImFluentStyle & style = ImFluent::GetStyle();
    const ImGuiID id            = w->GetID( header );
    const float pad_x           = FluentDpx( style.SpacingMedium );
    const float pad_y           = FluentDpx( style.SpacingSmall );
    const float pipe_h          = FluentDpx( 3.f );
    const ImVec2 ts             = ImGui::CalcTextSize( header, NULL, true );
    const ImVec2 pos            = w->DC.CursorPos;
    const ImRect bb( pos, ImVec2( pos.x + ts.x + pad_x * 2.f, pos.y + ts.y + pad_y * 2.f + pipe_h ) );
    ImGui::ItemSize( bb );
    if ( !ImGui::ItemAdd( bb, id, NULL, TabGroupItemFlags( id, selected ) ) )
    {
        ImGui::SameLine();
        return false;
    }
    bool hovered = false, held = false;
    const bool clicked = ImGui::ButtonBehavior( bb, id, &hovered, &held );
    const bool pressed = TabGroupItemNav( id ) || clicked;
    ReportItemInfo( id, header, CheckedFlags( selected ) );
    ImDrawList * dl          = w->DrawList;
    const ImFluentCol text_c = selected ? ImFluentCol_TextPrimary : held ? ImFluentCol_TextTertiary : hovered ? ImFluentCol_TextPrimary : ImFluentCol_TextSecondary;
    dl->AddText( ImVec2( bb.Min.x + pad_x, bb.Min.y + pad_y ), AnimateColorU32( id, ImFluent::GetColorU32( text_c ) ), header, ImGui::FindRenderedTextEnd( header ) );
    if ( selected )
        dl->AddRectFilled( ImVec2( bb.Min.x + pad_x, bb.Max.y - pipe_h ), ImVec2( bb.Max.x - pad_x, bb.Max.y ), ImFluent::GetColorU32( ImFluentCol_AccentFillDefault ), pipe_h * 0.5f );
    RenderNavFocusRing( id, bb, FluentDpx( style.ControlCornerRadius ) );
    ImGui::SameLine();
    return pressed;
}

void ImFluent::EndPivot()
{
    EndTabGroup();
    ImFluent::PopFont();
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    ImGui::PopID();
    ImGui::NewLine();
}
