#pragma once

#include "Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h"

#include "IdtMain.h"

#include <cstdint>
#include "Inkeys/Business/PenWidthPolicy.hpp"
#include "../inkStrokeModelerTest/draw3/shape_core.h"

enum class StateModeSelectEnum
{
	IdtSelection,
	IdtPen,
	IdtEraser,
	IdtShape,

	IdtTouchTest,
	IdtClickThrough
};

constexpr bool IsComputerInteractionMode(StateModeSelectEnum mode) noexcept
{
	return mode == StateModeSelectEnum::IdtSelection ||
		mode == StateModeSelectEnum::IdtClickThrough;
}
enum class PenModeSelectEnum
{
	// Brush1 是历史名称，当前默认工具语义为软笔；保留别名避免旧调用失效。
	IdtPenSoftPen = 0,
	IdtPenBrush1 = IdtPenSoftPen,
	IdtPenHardPen,
	IdtPenBrush2 = IdtPenHardPen,
	IdtPenHighlighter1
};
enum class ShapeModeSelectEnum
{
	IdtShapeStraightLine1,
	IdtShapeDashedLine1,
	IdtShapeTriangle1,
	IdtShapeRectangle1,
	IdtShapeCircle1,
	IdtShapeArrow1,
	IdtShapeRoundedRectangle1,
	IdtShapeEllipse1,
	IdtShapeParallelogram1,
	IdtShapeRhombus1,
	IdtShapeTrapezoid1,
	IdtShapeRegularPolygon1,
	IdtShapePolyline1,
	IdtShapePolygon1,
	IdtShapeArc1,
	IdtShapeSector1,
	IdtShapeCuboid1,
	IdtShapeCube1,
	IdtShapeCylinder1,
	IdtShapeCone1,
	IdtShapePrism1,
	IdtShapePyramid1
};

// 开放图形与闭合图形分别复用现有直线、矩形的颜色和宽度槽。

class StateModeClass
{
public:
	StateModeClass()
	{
		{
			Pen.ModeSelect = PenModeSelectEnum::IdtPenSoftPen;
			Pen.Brush1.width = Pen.Brush1.widthPreset = static_cast<float>(Inkeys::Business::PenWidth::Resolve(
				{}, Inkeys::Business::PenWidth::Family::Brush, penWidthContext).resolvedWidthDip);
			Pen.Brush1.color = RGBA(255, 16, 0, 255);
			Pen.Highlighter1.width = Pen.Highlighter1.widthPreset = static_cast<float>(Inkeys::Business::PenWidth::Resolve(
				{}, Inkeys::Business::PenWidth::Family::Highlighter, penWidthContext).resolvedWidthDip);
			Pen.Highlighter1.color = RGBA(255, 30, 207, 255); // TODO 后续添加透明度选项
			for (int index = 0; index < 3; ++index)
				Pen.Laser.widthPreset[index] = static_cast<float>(Inkeys::Business::PenWidth::PresetDip(
					static_cast<Inkeys::Business::PenWidth::Preset>(index),
					Inkeys::Business::PenWidth::Family::Laser, penWidthContext));
			Pen.Laser.width = Pen.Laser.widthPreset[1];
			Pen.Laser.color = RGBA(255, 16, 0, 255);
		}
		{
			Shape.ModeSelect = ShapeModeSelectEnum::IdtShapeStraightLine1;
			Shape.StraightLine1.width = Pen.Brush1.width;
			Shape.StraightLine1.color = RGBA(255, 16, 0, 255);
			Shape.Rectangle1.width = Pen.Brush1.width;
			Shape.Rectangle1.color = RGBA(255, 16, 0, 255);
		}
	}

public:
	StateModeSelectEnum StateModeSelect = StateModeSelectEnum::IdtSelection;
	StateModeSelectEnum StateModeSelectTarget = StateModeSelectEnum::IdtSelection;
	StateModeSelectEnum StateModeSelectEcho = StateModeSelectEnum::IdtSelection;
	// 记录最后选择的笔型是否为 Laser；离开 Pen 时保留，返回 Pen 时恢复。
	bool laserActive = false;
	// 穿透退出或切到定格时只恢复顶层工具，具体笔型与属性仍使用原有记忆。
	StateModeSelectEnum lastAnnotationMode = StateModeSelectEnum::IdtPen;
	Inkeys::Business::PenWidth::Context penWidthContext;

	struct
	{
		PenModeSelectEnum ModeSelect;

		struct
		{
			float width;
			Inkeys::Business::PenWidth::Selection widthSelection;
			COLORREF color;

			float widthPreset;
		}Brush1;
		struct
		{
			float width;
			Inkeys::Business::PenWidth::Selection widthSelection;
			COLORREF color;

			float widthPreset;
		}Highlighter1;
		struct
		{
			float width;
			Inkeys::Business::PenWidth::Selection widthSelection;
			// 激光颜色独立于普通笔和荧光笔，默认使用红色预设。
			COLORREF color;
			// 激光粗细独立于硬笔和荧光笔，固定为细/中/粗三档。
			float widthPreset[3];
		}Laser;
	}Pen;
	struct
	{
		ShapeModeSelectEnum ModeSelect;
		// 创建方式、视觉样式与类别独立；仅会话记忆，不新增启动配置。
		draw3::shapes::CreationMode circleMode = draw3::shapes::CreationMode::CenterRadius;
		std::uint32_t polygonSides = 3;
		draw3::shapes::FillMode fillMode = draw3::shapes::FillMode::Outline;
		draw3::shapes::LinePattern openPattern = draw3::shapes::LinePattern::Solid;
		draw3::shapes::LinePattern closedPattern = draw3::shapes::LinePattern::Solid;

		struct
		{
			float width;
			Inkeys::Business::PenWidth::Selection widthSelection;
			COLORREF color;
		}StraightLine1;
		struct
		{
			float width;
			Inkeys::Business::PenWidth::Selection widthSelection;
			COLORREF color;
		}Rectangle1;
	}Shape;
};
extern StateModeClass stateMode;
StateModeClass GetStateModeSnapshot();
struct StateModeVersionedSnapshot
{
	StateModeClass state;
	std::uint64_t revision = 0;
};
StateModeVersionedSnapshot GetStateModeVersionedSnapshot();

bool IsLaserPenSelected() noexcept;
bool IsLaserToolActive() noexcept;
bool IsLaserToolActive(const StateModeClass& snapshot) noexcept;
bool SetPenWidth(double targetWidth, bool setMemory = true);
bool SetPenWidthIfRevision(double targetWidth,
	std::uint64_t expectedRevision, bool setMemory = true);
Inkeys::Business::PenWidth::Selection GetPenWidthSelection(const StateModeClass& snapshot) noexcept;
Inkeys::Business::PenWidth::Family GetPenWidthFamily(const StateModeClass& snapshot) noexcept;
Inkeys::Business::PenWidth::Metrics GetPenWidthMetrics(const StateModeClass& snapshot) noexcept;
Inkeys::Business::PenWidth::Context GetPenWidthContext();
bool ApplyPenWidthContext(Inkeys::Business::PenWidth::Context context);
bool SetPenWidthPresetIfRevision(int index, std::uint64_t expectedRevision, bool setMemory = true);
void RestorePenWidthSelections(Inkeys::Business::PenWidth::Selection brush,
    Inkeys::Business::PenWidth::Selection highlighter, Inkeys::Business::PenWidth::Selection laser,
    const COLORREF* brushColor = nullptr, const COLORREF* highlighterColor = nullptr);
int RunPenWidthStateProductionTest() noexcept;
bool SetPenColor(COLORREF targetColor, bool setMemory = true);
float GetPenWidth();
float GetPenWidth(const StateModeClass& snapshot) noexcept;
COLORREF GetPenColor();
COLORREF GetPenColor(const StateModeClass& snapshot) noexcept;
float GetEffectivePenOpacity();
float GetEffectivePenOpacity(const StateModeClass& snapshot) noexcept;

bool ChangeStateModeToSelection();
bool ChangeStateModeToSelectionFromButton();
bool ChangeStateModeToClickThrough();
bool ToggleClickThroughFromButton();
int GetSelectAndClickThroughMode() noexcept;
bool SetSelectAndClickThroughMode(int mode);
void ToggleFreezeFromButton();
std::uint64_t StateModeTransitionRevision() noexcept;
bool ChangeStateModeToSelectionIfRevision(std::uint64_t expectedRevision);
bool ChangeStateModeToPen();
enum class PenToolSelectionEnum : std::uint8_t { SoftPen, HardPen, Highlighter, Laser };
bool PenToolSelectionWouldChange(PenToolSelectionEnum tool);
bool ChangeStateModeToPenTool(PenToolSelectionEnum tool);
enum class PptAnnotationTool : int { Pen = 1, Laser = 2, Highlighter = 3 };
bool ChangeStateModeToPptAnnotation(PptAnnotationTool tool, std::uint64_t expectedRevision);
bool ChangeStateModeToShape();
bool SetShapeModeSelect(ShapeModeSelectEnum shape);
bool SetShapeModeSelectIfRevision(ShapeModeSelectEnum shape,
	std::uint64_t expectedRevision);
draw3::shapes::ToolSpec GetShapeToolSpec(const StateModeClass& snapshot) noexcept;
draw3::shapes::ShapeStyle GetShapeStyle(const StateModeClass& snapshot) noexcept;
draw3::shapes::FillMode GetShapeFillMode(const StateModeClass& snapshot) noexcept;
bool IsOpenShapeTool(const StateModeClass& snapshot) noexcept;
bool SetShapeToolPresetIfRevision(draw3::shapes::PresetKind preset, std::uint64_t expectedRevision);
bool SetShapeFillModeIfRevision(draw3::shapes::FillMode mode, std::uint64_t expectedRevision);
bool SetShapeLinePatternIfRevision(draw3::shapes::LinePattern pattern, std::uint64_t expectedRevision);
bool SetShapeCircleModeIfRevision(draw3::shapes::CreationMode mode, std::uint64_t expectedRevision);
bool SetShapePolygonSidesIfRevision(std::uint32_t sides, std::uint64_t expectedRevision);
int RunShapeToolStateProductionTest() noexcept;
bool ChangeStateModeToEraser();
bool ChangeStateModeToTouchTest();
// 将当前工具与显式选择模式发布到 Draw3 bridge。
void SyncDraw3State();
void InitializeEraserInputPreferences();
Inkeys::Drawing::Draw3::SpeedEraser::InputSettings EraserPreferencesSnapshot();
void SetGlobalEraserPreference(int baseDiameterDip = -1, int sensitivity = -1, int automatic = -1);
bool AutomaticPenResponseAvailable() noexcept;
int EraserWidthPreference(int entry);
int EraserPenResponsePreference(int entry);
void SetEraserInputPreference(int entry,int kind,int penResponse = -1);
// 按当前模式和 Draw3 当前页内容统一同步 Drawpad 显隐、穿透和 Bar。
void ReconcileDraw3Presentation();

// 白板按钮和翻页控件只提交请求，具体工作区事务由 StateMonitoring 串行执行。
inline constexpr bool IsWhiteboardFeatureEnabled() noexcept { return true; }
void RequestWhiteboardActive(bool active) noexcept;
bool WhiteboardRequested() noexcept;
bool WhiteboardActive() noexcept;
bool WhiteboardTransactionActive() noexcept;
void RequestWhiteboardPreviousPage() noexcept;
void RequestWhiteboardNextPage() noexcept;
void RequestWhiteboardTheme(unsigned int rgb, int pattern, std::uint64_t generation, int currentPage) noexcept;

// 显式早期 CLI 仅执行真实状态/bridge 逻辑，不启动窗口或读取、保存配置。
int RunStateModeProductionTest() noexcept;

void StateMonitoring();

struct StateModeStruct_Discard
{
	float brushWidth;
	COLORREF brushColor;

	float brushMode;
	/* 过时设定，后续抛弃
	* 1 画笔
	* 2 荧光笔
	* 3 直线
	* 4 矩形
	*/
};
bool GetStateMode_Discard(StateModeStruct_Discard* stateModeInfo);
