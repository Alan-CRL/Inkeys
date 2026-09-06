#pragma once

#include "imgui.h"

#include <string>

enum ImFluentThemePreset_
{
    ImFluentThemePreset_Light = 0,
    ImFluentThemePreset_Dark,
    ImFluentThemePreset_HighContrast,
    ImFluentThemePreset_COUNT
};
typedef int ImFluentThemePreset;

enum ImFluentTextStyle_
{
    ImFluentTextStyle_Caption = 0,
    ImFluentTextStyle_Body,
    ImFluentTextStyle_BodyStrong,
    ImFluentTextStyle_Subtitle,
    ImFluentTextStyle_Title,
    ImFluentTextStyle_TitleLarge,
    ImFluentTextStyle_Display,
    ImFluentTextStyle_COUNT
};
typedef int ImFluentTextStyle;

enum ImFluentCol_
{

    ImFluentCol_TextPrimary = 0,
    ImFluentCol_TextSecondary,
    ImFluentCol_TextTertiary,
    ImFluentCol_TextDisabled,
    ImFluentCol_TextOnAccentPrimary,
    ImFluentCol_TextOnAccentSecondary,
    ImFluentCol_TextOnAccentDisabled,
    ImFluentCol_TextOnAccentSelected,
    ImFluentCol_AccentTextPrimary,
    ImFluentCol_AccentTextSecondary,
    ImFluentCol_AccentTextTertiary,
    ImFluentCol_AccentTextDisabled,

    ImFluentCol_ControlFillDefault,
    ImFluentCol_ControlFillSecondary,
    ImFluentCol_ControlFillTertiary,
    ImFluentCol_ControlFillQuarternary,
    ImFluentCol_ControlFillDisabled,
    ImFluentCol_ControlFillTransparent,
    ImFluentCol_ControlFillInputActive,
    ImFluentCol_ControlAltFillTransparent,
    ImFluentCol_ControlAltFillSecondary,
    ImFluentCol_ControlAltFillTertiary,
    ImFluentCol_ControlAltFillQuarternary,
    ImFluentCol_ControlAltFillDisabled,
    ImFluentCol_ControlSolidFillDefault,
    ImFluentCol_ControlStrongFillDefault,
    ImFluentCol_ControlStrongFillDisabled,

    ImFluentCol_SubtleFillTransparent,
    ImFluentCol_SubtleFillSecondary,
    ImFluentCol_SubtleFillTertiary,
    ImFluentCol_SubtleFillDisabled,

    ImFluentCol_AccentFillDefault,
    ImFluentCol_AccentFillSecondary,
    ImFluentCol_AccentFillTertiary,
    ImFluentCol_AccentFillDisabled,
    ImFluentCol_AccentFillSelectedTextBg,

    ImFluentCol_CardBgDefault,
    ImFluentCol_CardBgSecondary,
    ImFluentCol_LayerFillDefault,
    ImFluentCol_LayerFillAlt,
    ImFluentCol_SmokeFill,
    ImFluentCol_AcrylicFill,
    ImFluentCol_SolidBgBase,
    ImFluentCol_SolidBgQuarternary,

    ImFluentCol_CardStrokeDefault,
    ImFluentCol_CardStrokeSolid,
    ImFluentCol_ControlStrokeDefault,
    ImFluentCol_ControlStrokeSecondary,
    ImFluentCol_ControlStrokeOnAccentDefault,
    ImFluentCol_ControlStrokeOnAccentSecondary,
    ImFluentCol_ControlStrongStrokeDefault,
    ImFluentCol_ControlStrongStrokeDisabled,
    ImFluentCol_SurfaceStrokeDefault,
    ImFluentCol_SurfaceStrokeFlyout,
    ImFluentCol_DividerStrokeDefault,

    ImFluentCol_FocusStrokeOuter,
    ImFluentCol_FocusStrokeInner,

    ImFluentCol_ElevationControlTop,
    ImFluentCol_ElevationControlBottom,
    ImFluentCol_ElevationTextControlTop,
    ImFluentCol_ElevationTextControlBottom,
    ImFluentCol_ElevationTextControlFocusedBottom,
    ImFluentCol_ElevationAccentTop,
    ImFluentCol_ElevationAccentBottom,

    ImFluentCol_SystemFillSuccess,
    ImFluentCol_SystemFillCaution,
    ImFluentCol_SystemFillCritical,
    ImFluentCol_SystemFillNeutral,
    ImFluentCol_SystemFillAttention,

    ImFluentCol_COUNT
};
typedef int ImFluentCol;

enum ImFluentCardStyle_
{
    ImFluentCardStyle_Filled = 0,
    ImFluentCardStyle_Outlined,
};
typedef int ImFluentCardStyle;

enum ImFluentNavViewMode_
{
    ImFluentNavViewMode_LeftCompact = 0,
    ImFluentNavViewMode_LeftOpen,
    ImFluentNavViewMode_LeftAuto,
    ImFluentNavViewMode_Top,
};
typedef int ImFluentNavViewMode;

enum ImFluentInfoSeverity_
{
    ImFluentInfoSeverity_Informational = 0,
    ImFluentInfoSeverity_Success,
    ImFluentInfoSeverity_Warning,
    ImFluentInfoSeverity_Critical,
};
typedef int ImFluentInfoSeverity;

enum ImFluentTeachingTipPlacement_
{
    ImFluentTeachingTipPlacement_Top = 0,
    ImFluentTeachingTipPlacement_Bottom,
    ImFluentTeachingTipPlacement_Left,
    ImFluentTeachingTipPlacement_Right,
};
typedef int ImFluentTeachingTipPlacement;

enum ImFluentAppBarLabelPosition_
{
    ImFluentAppBarLabelPosition_Bottom = 0,
    ImFluentAppBarLabelPosition_Right,
    ImFluentAppBarLabelPosition_Collapsed,
};
typedef int ImFluentAppBarLabelPosition;

enum ImFluentContentDialogButton_
{
    ImFluentContentDialogButton_None = 0,
    ImFluentContentDialogButton_Primary,
    ImFluentContentDialogButton_Secondary,
    ImFluentContentDialogButton_Close,
};
typedef int ImFluentContentDialogButton;

enum ImFluentProgressBarState_
{
    ImFluentProgressBarState_Running = 0,
    ImFluentProgressBarState_Paused,
    ImFluentProgressBarState_Error,
};
typedef int ImFluentProgressBarState;

enum ImFluentExpandDirection_
{
    ImFluentExpandDirection_Down = 0,
    ImFluentExpandDirection_Up,
};
typedef int ImFluentExpandDirection;

enum ImFluentSplitViewDisplayMode_
{
    ImFluentSplitViewDisplayMode_Inline = 0,
    ImFluentSplitViewDisplayMode_CompactInline,
    ImFluentSplitViewDisplayMode_Overlay,
    ImFluentSplitViewDisplayMode_CompactOverlay,
};
typedef int ImFluentSplitViewDisplayMode;

enum ImFluentSplitViewPanePlacement_
{
    ImFluentSplitViewPanePlacement_Left = 0,
    ImFluentSplitViewPanePlacement_Right,
};
typedef int ImFluentSplitViewPanePlacement;

enum ImFluentPagerDisplayMode_
{
    ImFluentPagerDisplayMode_ButtonPanel = 0,
    ImFluentPagerDisplayMode_ComboBox,
    ImFluentPagerDisplayMode_NumberBox,
};
typedef int ImFluentPagerDisplayMode;

enum ImFluentPagerButtonVisibility_
{
    ImFluentPagerButtonVisibility_Visible = 0,
    ImFluentPagerButtonVisibility_HiddenOnEdge,
    ImFluentPagerButtonVisibility_Hidden,
};
typedef int ImFluentPagerButtonVisibility;

enum ImFluentTextBoxFlags_
{
    ImFluentTextBoxFlags_None        = 0,
    ImFluentTextBoxFlags_ClearButton = 1 << 0,
    ImFluentTextBoxFlags_ShowCounter = 1 << 1,
};
typedef int ImFluentTextBoxFlags;

enum ImFluentTimePickerFlags_
{
    ImFluentTimePickerFlags_None    = 0,
    ImFluentTimePickerFlags_Hours12 = 1 << 0,
};
typedef int ImFluentTimePickerFlags;

enum ImFluentStyleVar_
{
    ImFluentStyleVar_ControlCornerRadius = 0,
    ImFluentStyleVar_OverlayCornerRadius,
    ImFluentStyleVar_ControlHeight,
    ImFluentStyleVar_ControlMinWidth,
    ImFluentStyleVar_CardPadding,
    ImFluentStyleVar_SpacingXSmall,
    ImFluentStyleVar_SpacingSmall,
    ImFluentStyleVar_SpacingMedium,
    ImFluentStyleVar_SpacingLarge,
    ImFluentStyleVar_SpacingXLarge,
    ImFluentStyleVar_SpacingXXLarge,
    ImFluentStyleVar_StrokeThin,
    ImFluentStyleVar_StrokeMedium,
    ImFluentStyleVar_StrokeThick,
    ImFluentStyleVar_FocusStrokeThicknessOuter,
    ImFluentStyleVar_FocusStrokeThicknessInner,
    ImFluentStyleVar_NavPaneCompactWidth,
    ImFluentStyleVar_NavPaneOpenWidth,
    ImFluentStyleVar_ControlContentPadding,
    ImFluentStyleVar_COUNT
};
typedef int ImFluentStyleVar;

enum ImFluentLocKey_
{
    ImFluentLocKey_AutoSuggestNoSuggestions = 0,
    ImFluentLocKey_DatePickerPickADate,
    ImFluentLocKey_DatePickerDayFormat,
    ImFluentLocKey_DatePickerYearFormat,
    ImFluentLocKey_TimePickerHourFormat,
    ImFluentLocKey_TimePickerMinuteFormat,
    ImFluentLocKey_MonthJanuary,
    ImFluentLocKey_MonthFebruary,
    ImFluentLocKey_MonthMarch,
    ImFluentLocKey_MonthApril,
    ImFluentLocKey_MonthMay,
    ImFluentLocKey_MonthJune,
    ImFluentLocKey_MonthJuly,
    ImFluentLocKey_MonthAugust,
    ImFluentLocKey_MonthSeptember,
    ImFluentLocKey_MonthOctober,
    ImFluentLocKey_MonthNovember,
    ImFluentLocKey_MonthDecember,
    ImFluentLocKey_DayMon,
    ImFluentLocKey_DayTue,
    ImFluentLocKey_DayWed,
    ImFluentLocKey_DayThu,
    ImFluentLocKey_DayFri,
    ImFluentLocKey_DaySat,
    ImFluentLocKey_DaySun,
    ImFluentLocKey_COUNT
};
typedef int ImFluentLocKey;

struct ImFluentLocEntry
{
    ImFluentLocKey Key;
    const char * Text;
};

struct ImFluentStyle
{
    float ControlCornerRadius;
    float OverlayCornerRadius;
    float ControlHeight;
    float ControlMinWidth;
    float FocusStrokeThicknessOuter;
    float FocusStrokeThicknessInner;
    float CardPadding;
    float NavPaneCompactWidth;
    float NavPaneOpenWidth;
    ImVec2 ControlContentPadding;

    float SpacingXSmall;
    float SpacingSmall;
    float SpacingMedium;
    float SpacingLarge;
    float SpacingXLarge;
    float SpacingXXLarge;

    float StrokeThin;
    float StrokeMedium;
    float StrokeThick;

    float ChevronGlyphSize;
    float StandardIconSize;

    float CheckboxSize;
    float RadioButtonDiameter;
    float ToggleSwitchWidth;
    float ToggleSwitchHeight;
    float ToggleSwitchThumbRadiusOff;
    float ToggleSwitchThumbRadiusOn;
    float SliderTrackHeight;
    float SliderThumbRadius;
    float SliderThumbInnerRadius;
    float ProgressBarHeight;
    float ProgressRingThickness;
    float RatingStarSize;

    float NavItemHeight;
    float MenuItemHeight;
    float ListItemHeight;
    float TitleBarHeight;
    float AppBarButtonWidth;
    float AppBarButtonHeight;
    float SpinButtonWidth;
    float RevealButtonWidth;
    float BadgeHeight;
    float PipDotSize;

    float SeverityBarThickness;
    float SelectionIndicatorThickness;
    float SelectionIndicatorInset;
    float TextInputAccentLineThickness;

    IMGUI_API ImFluentStyle();

    ImVec4 Colors[ ImFluentCol_COUNT ];
    const char * LocalizationTable[ ImFluentLocKey_COUNT ];
};

typedef int ( *ImFluentAutoSuggestPredicate )( const char * item, const char * filter, void * user_data );

namespace ImFluent
{
    IMGUI_API ImFluentStyle & GetStyle();

    IMGUI_API void SetThemePreset( ImFluentThemePreset preset );
    IMGUI_API ImFluentThemePreset GetThemePreset();

    IMGUI_API void SetAccentColor( const ImColor & color );
    IMGUI_API ImColor GetAccentColor();
    IMGUI_API bool HasUserAccentColor();

    IMGUI_API void LoadFluentSystemFonts();

    // INKEYS PATCH: 项目使用内嵌字体，并在 ImGui context 销毁前重置库状态。
    IMGUI_API void SetFluentTextStyleFont( ImFluentTextStyle style, ImFont * font, float size );
    IMGUI_API void ResetContext();
    // INKEYS PATCH: 独立图标字体及显式 pane 中的导航条目复用同一控件实现。
    IMGUI_API void SetIconFont( ImFont * font );
    IMGUI_API void DrawIcon( const char * glyph, const ImVec2 & min, const ImVec2 & max, float size_dip, ImU32 color );
    IMGUI_API bool NavItemEx( const char * id, const char * label, bool selected, const char * glyph, bool compact );

    IMGUI_API void PushFluentStyle();
    IMGUI_API void PopFluentStyle();

    IMGUI_API void PushStyleColor( ImFluentCol idx, ImU32 col );
    IMGUI_API void PushStyleColor( ImFluentCol idx, const ImVec4 & col );
    IMGUI_API void PopStyleColor( int count = 1 );

    IMGUI_API void PushStyleVar( ImFluentStyleVar idx, float val );
    IMGUI_API void PushStyleVar( ImFluentStyleVar idx, const ImVec2 & val );
    IMGUI_API void PopStyleVar( int count = 1 );

    IMGUI_API void BeginDisabled( bool disabled = true );
    IMGUI_API void EndDisabled();

    IMGUI_API void PushFont( ImFluentTextStyle style, float size = 0.0f );
    IMGUI_API void PopFont();

    IMGUI_API void SetNextItemHeader( const char * text );
    IMGUI_API void SetNextItemDescription( const char * text );
    IMGUI_API void SetNextItemGlyph( const char * glyph );
    IMGUI_API void SetNextItemError( const char * error );

    IMGUI_API bool Button( const char * label, const ImVec2 & size = ImVec2( 0, 0 ) );
    IMGUI_API bool AccentButton( const char * label, const ImVec2 & size = ImVec2( 0, 0 ) );
    IMGUI_API bool HyperlinkButton( const char * label );
    IMGUI_API bool ToggleButton( const char * label, bool * v, const ImVec2 & size = ImVec2( 0, 0 ) );
    IMGUI_API bool RepeatButton( const char * label, const ImVec2 & size = ImVec2( 0, 0 ) );
    IMGUI_API bool DropDownButton( const char * label, const ImVec2 & size = ImVec2( 0, 0 ) );
    IMGUI_API bool DropDownButtonEx( const char * label, bool * v_state, bool * dropdown_clicked, const ImVec2 & size, bool split, bool toggled );
    IMGUI_API bool SplitButton( const char * label, bool * dropdown_clicked, const ImVec2 & size = ImVec2( 0, 0 ) );
    IMGUI_API bool ToggleSplitButton( const char * label, bool * v, bool * dropdown_clicked, const ImVec2 & size = ImVec2( 0, 0 ) );

    IMGUI_API bool Checkbox( const char * label, bool * v );
    IMGUI_API bool CheckboxTristate( const char * label, int * v_state );
    IMGUI_API bool CheckboxEx( const char * label, int * v_tri, bool * v_bool );

    IMGUI_API bool RadioButton( const char * label, bool active );
    IMGUI_API bool RadioButton( const char * label, int * v, int v_button );
    IMGUI_API bool RadioButtons( const char * label, int * v, const char * const items[], int items_count, int max_columns = 1 );
    IMGUI_API bool ToggleSwitch( const char * label, bool * v, const char * on_text = "On", const char * off_text = "Off" );
    IMGUI_API bool RatingControl( const char * label, float * value, int max_stars = 5 );

    IMGUI_API bool Slider( const char * label, ImGuiDataType dtype, void * v, const void * v_min, const void * v_max, const char * format, ImGuiSliderFlags flags );
    IMGUI_API bool Slider( const char * label, float * v, float v_min, float v_max, const char * format = "%.2f", ImGuiSliderFlags flags = 0 );
    IMGUI_API bool SliderInt( const char * label, int * v, int v_min, int v_max, const char * format = "%d", ImGuiSliderFlags flags = 0 );
    IMGUI_API bool RangeSlider( const char * label, float * v_min, float * v_max, float v_lo, float v_hi, const char * format = "%.2f" );

    IMGUI_API bool ColorPicker( const char * label, float col[ 4 ], ImGuiColorEditFlags flags = 0 );
    IMGUI_API bool ColorEdit( const char * label, float col[ 4 ], ImGuiColorEditFlags flags = 0 );
    IMGUI_API bool ColorButton( const char * desc_id, const ImVec4 & col, ImGuiColorEditFlags flags = 0, const ImVec2 & size = ImVec2( 0, 0 ) );
    IMGUI_API void ProgressBar( float fraction, const ImVec2 & size_arg = ImVec2( -1.f, 0 ), const char * overlay = NULL, ImFluentProgressBarState state = ImFluentProgressBarState_Running );
    IMGUI_API void ProgressRing( float diameter_dpx = 32.f, float fraction = -1.f );

    IMGUI_API void SetNextTextInputTextCallback( ImGuiInputTextCallback callback, void * user_data = NULL );
    IMGUI_API bool TextBox( const char * label, char * buf, size_t buf_size, const char * hint = NULL, ImGuiInputTextFlags flags = 0, ImFluentTextBoxFlags fluent_flags = ImFluentTextBoxFlags_None, int max_length = 0 );
    IMGUI_API bool TextBox( const char * label, std::string & str, const char * hint = NULL, ImGuiInputTextFlags flags = 0, ImFluentTextBoxFlags fluent_flags = ImFluentTextBoxFlags_None, int max_length = 0 );
    IMGUI_API bool PasswordBox( const char * label, char * buf, size_t buf_size, const char * hint = NULL, ImGuiInputTextFlags flags = 0 );
    IMGUI_API bool PasswordBox( const char * label, std::string & str, const char * hint = NULL, ImGuiInputTextFlags flags = 0 );
    IMGUI_API bool NumberBox( const char * label, double * v, double step = 1.0, double step_fast = 10.0, const char * format = "%.3f", ImGuiInputTextFlags flags = 0 );
    IMGUI_API bool RichEditBox( const char * label, char * buf, size_t buf_size, const ImVec2 & size = ImVec2( 0, 0 ), ImGuiInputTextFlags flags = 0, int max_length = 0 );
    IMGUI_API bool RichEditBox( const char * label, std::string & str, const ImVec2 & size = ImVec2( 0, 0 ), ImGuiInputTextFlags flags = 0, int max_length = 0 );

    IMGUI_API void SetNextAutoSuggestBoxPredicate( ImFluentAutoSuggestPredicate predicate, void * user_data = NULL );
    IMGUI_API bool AutoSuggestBox( const char * label, char * buf, size_t buf_size, const char * const items[], int items_count, int * selected_index = NULL, const char * hint = NULL, ImGuiInputTextFlags flags = 0, ImGuiComboFlags combo_flags = 0 );
    IMGUI_API bool AutoSuggestBox( const char * label, std::string & str, const char * const items[], int items_count, int * selected_index = NULL, const char * hint = NULL, ImGuiInputTextFlags flags = 0, ImGuiComboFlags combo_flags = 0 );

    IMGUI_API void TextBlock( const char * text, ImFluentTextStyle style = ImFluentTextStyle_Body );
    IMGUI_API void TextBlockColored( const char * text, ImU32 color_u32, ImFluentTextStyle style = ImFluentTextStyle_Body );

    IMGUI_API void Separator();
    IMGUI_API void SetItemTooltip( const char * fmt, ... ) IM_FMTARGS( 1 );

    IMGUI_API bool BeginCard( const char * id, const ImVec2 & size = ImVec2( 0, 0 ), ImFluentCardStyle style = ImFluentCardStyle_Filled );
    IMGUI_API void EndCard();

    IMGUI_API bool BeginSettingsCard( const char * id, const char * header, const char * description = NULL, const char * glyph = NULL );
    IMGUI_API void EndSettingsCard();

    IMGUI_API void BeginStackPanelHorizontal( float spacing = -1.f );
    IMGUI_API void BeginStackPanelVertical( float spacing = -1.f );
    IMGUI_API void EndStackPanel();

    IMGUI_API void BeginWrapPanel( float h_spacing = -1.f, float v_spacing = -1.f );
    IMGUI_API bool WrapPanelNextItem( float item_width );
    IMGUI_API void EndWrapPanel();

    IMGUI_API bool BeginExpander( const char * label, bool * open, ImFluentExpandDirection direction = ImFluentExpandDirection_Down, bool * out_just_expanded = NULL, bool * out_just_collapsed = NULL );
    IMGUI_API void EndExpander();

    IMGUI_API bool BeginScrollView( const char * id, const ImVec2 & size = ImVec2( 0, 0 ) );
    IMGUI_API void EndScrollView();

    IMGUI_API bool BeginTabView( const char * id );
    IMGUI_API void EndTabView();
    IMGUI_API bool BeginTabItem( const char * label, bool * p_open = NULL, ImGuiTabItemFlags flags = 0 );
    IMGUI_API void EndTabItem();
    IMGUI_API bool TabAddButton();

    IMGUI_API bool BeginSelectorBar( const char * id );
    IMGUI_API bool SelectorBarItem( const char * label, bool selected, const char * glyph = NULL );
    IMGUI_API void EndSelectorBar();

    IMGUI_API bool BeginNavigationView( const char * id, ImFluentNavViewMode * mode );
    IMGUI_API void SetNextNavPaneToggleButtonVisible( bool visible );
    IMGUI_API bool NavBackButton( bool enabled = true, bool visible = true );
    IMGUI_API bool NavItem( const char * label, bool selected, const char * glyph = NULL );
    IMGUI_API bool BeginNavItem( const char * label, bool selected = false, const char * glyph = NULL );
    IMGUI_API void EndNavItem();
    IMGUI_API void NavSubHeader( const char * text );
    IMGUI_API void NavPaneTitle( const char * text );
    IMGUI_API bool NavPaneAutoSuggestBox( const char * label, char * buf, size_t buf_size, const char * const items[], int items_count, int * selected_index = NULL, const char * hint = NULL );
    IMGUI_API void NavPaneFooterBegin();
    IMGUI_API void NavPaneFooterEnd();
    IMGUI_API bool NavSettingsItem( bool selected );
    IMGUI_API void EndNavigationView();
    IMGUI_API void NavigationViewBeginContent();
    IMGUI_API void NavContentHeader( const char * title );
    IMGUI_API void NavigationViewEndContent();

    IMGUI_API bool BeginSplitView( const char * id, bool * is_pane_open, ImFluentSplitViewDisplayMode display_mode = ImFluentSplitViewDisplayMode_Inline, ImFluentSplitViewPanePlacement placement = ImFluentSplitViewPanePlacement_Left, float open_pane_width_dpx = 320.f, float compact_pane_width_dpx = 48.f );
    IMGUI_API bool BeginSplitViewPane();
    IMGUI_API void EndSplitViewPane();
    IMGUI_API bool BeginSplitViewContent();
    IMGUI_API void EndSplitViewContent();
    IMGUI_API void EndSplitView();
    IMGUI_API bool IsNavPaneOpening();
    IMGUI_API bool IsNavPaneClosing();

    IMGUI_API bool ComboBox( const char * label, int * current_item, const char * const items[], int items_count, ImGuiComboFlags flags = 0 );
    IMGUI_API bool ListBox( const char * label, int * current_item, const char * const items[], int items_count, int height_in_items = 7 );
    IMGUI_API bool Selectable( const char * label, bool selected = false, const char * glyph = NULL, float height = 0.f );
    IMGUI_API bool ListViewItem( const char * label, bool selected = false, const char * glyph = NULL );
    IMGUI_API bool TreeNode( const char * label, bool * p_open );

    IMGUI_API bool TreeNode( const char * label, bool * p_open, bool * p_checked );
    IMGUI_API void TreePop();
    IMGUI_API bool GridViewItem( const char * label, bool selected, const ImVec2 & size );
    IMGUI_API bool PipsPager( const char * id, int * current_item, int total_pages );
    IMGUI_API bool PagerControl( const char * id, int * current_page, int total_pages, ImFluentPagerDisplayMode display_mode = ImFluentPagerDisplayMode_ButtonPanel, ImFluentPagerButtonVisibility first_last_visibility = ImFluentPagerButtonVisibility_Hidden, ImFluentPagerButtonVisibility prev_next_visibility = ImFluentPagerButtonVisibility_Visible );
    IMGUI_API int BreadcrumbBar( const char * id, const char * const items[], int items_count );

    IMGUI_API void OpenFlyout( const char * id );
    IMGUI_API bool BeginFlyout( const char * id );
    IMGUI_API void EndFlyout();

    IMGUI_API void OpenMenuFlyout( const char * id );
    IMGUI_API bool BeginMenuFlyout( const char * id );
    IMGUI_API bool MenuFlyoutItem( const char * label, const char * shortcut = NULL, const char * glyph = NULL, bool selected = false, bool enabled = true );
    IMGUI_API bool ToggleMenuFlyoutItem( const char * label, bool * v, const char * shortcut = NULL, bool enabled = true );
    IMGUI_API bool RadioMenuFlyoutItem( const char * label, int * v, int v_button, const char * shortcut = NULL, bool enabled = true );
    IMGUI_API bool BeginMenuFlyoutSubItem( const char * label, const char * glyph = NULL, bool enabled = true );
    IMGUI_API void EndMenuFlyoutSubItem();
    IMGUI_API void MenuFlyoutSeparator();
    IMGUI_API void EndMenuFlyout();

    IMGUI_API bool BeginCommandBarFlyout( const char * id );
    IMGUI_API void EndCommandBarFlyout();

    IMGUI_API void OpenContentDialog( const char * id );
    IMGUI_API bool BeginContentDialog( const char * id, const char * title );
    IMGUI_API int EndContentDialog( const char * primary = "OK", const char * secondary = NULL, const char * close_text = "Cancel", ImFluentContentDialogButton default_button = ImFluentContentDialogButton_Primary );

    IMGUI_API bool InfoBar( ImFluentInfoSeverity severity, const char * title, const char * message, bool * is_open = NULL, const char * glyph_override = NULL, bool show_icon = true, const char * action_label = NULL );
    IMGUI_API void InfoBadge( int count = -1, const char * glyph = NULL );


    IMGUI_API void OpenTeachingTip( const char * id );
    IMGUI_API bool BeginTeachingTip( const char * id, const char * title, ImFluentTeachingTipPlacement placement = ImFluentTeachingTipPlacement_Bottom );
    IMGUI_API void EndTeachingTip();

    IMGUI_API bool BeginTitleBar( const char * title = NULL, float height = 0.f );
    IMGUI_API bool TitleBarBackButton( bool enabled = true, bool visible = true );
    IMGUI_API bool TitleBarPaneToggleButton( bool enabled = true, bool visible = true );
    IMGUI_API void TitleBarIcon( const char * glyph );
    IMGUI_API void TitleBarTitle( const char * text );
    IMGUI_API void TitleBarSubtitle( const char * subtitle );
    IMGUI_API void EndTitleBar();

    IMGUI_API bool BeginMenuBar();
    IMGUI_API void EndMenuBar();

    IMGUI_API bool BeginCommandBar( const char * id, float height = 0.f );
    IMGUI_API bool BeginCommandBarOverflow();
    IMGUI_API void EndCommandBarOverflow();
    IMGUI_API void EndCommandBar();
    IMGUI_API bool AppBarButton( const char * label, const char * glyph, const ImVec2 & size = ImVec2( 0, 0 ) );
    IMGUI_API bool AppBarToggleButton( const char * label, const char * glyph, bool * v, const ImVec2 & size = ImVec2( 0, 0 ) );
    IMGUI_API void AppBarSeparator();
    IMGUI_API void SetNextAppBarLabelPosition( ImFluentAppBarLabelPosition pos );

    struct ImFluentDate
    {
        int Year;
        int Month;
        int Day;
    };
    struct ImFluentTime
    {
        int Hour;
        int Minute;
    };

    IMGUI_API bool DatePicker( const char * label, ImFluentDate * date );
    IMGUI_API bool TimePicker( const char * label, ImFluentTime * time, ImFluentTimePickerFlags flags = ImFluentTimePickerFlags_None, int minute_increment = 1 );
    IMGUI_API bool CalendarView( const char * id, ImFluentDate * date, const ImFluentDate * min_date = NULL, const ImFluentDate * max_date = NULL );
    IMGUI_API bool CalendarDatePicker( const char * label, ImFluentDate * date, const char * hint = "Pick a date", const ImFluentDate * min_date = NULL, const ImFluentDate * max_date = NULL );

    IMGUI_API void ShowDemoWindow( bool * p_open = NULL );

} // namespace ImFluent
