module;

#include "../../../IdtMain.h"
#include "../../../IdtState.h"

#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

export module Inkeys.UI.Bar:Button;

export import Inkeys.UI.UI3;
import :State;
import Inkeys.Other.Config;
import Inkeys.UI.Bar.LayoutModel;

using namespace Inkeys::UI::UI3;

enum class BarButtonPresetEnum : int
{
	None,
	Divider,

	Select,
	Draw,
	Eraser,
	Geometry,

	Recall,
	Redo,
	Clean,

	Whiteboard,
	Freeze,
	EndShow,

	More,
	Setting,
	Pierce,
	CanvasDebug
};

class BarButtonClass : public ButtonClass
{
public:
	BarButtonClass() = default;
	// 产品元数据留在主栏；通用视觉与共享状态指针由基类保持原拷贝语义。
	BarButtonClass(const BarButtonClass& other)
		: ButtonClass(other),
		baseSize(other.baseSize),
		baseUserVisible(other.baseUserVisible),
		preset(other.preset),
		id(other.id),
		userVisible(other.userVisible),
		hide(other.hide),
		only(other.only),
		compactMultilineLabel(other.compactMultilineLabel),
		closeMoreAfterAction(other.closeMoreAfterAction)
	{}

	bool IsVisible() const
	{
		return userVisible.load() && !hide.load();
	}
	// 新增英文长标签使用两行，局部缩小字号，不改变现有按钮网格。
	double LabelFontSize(double standardFontSize) const
	{
		return (preset == BarButtonPresetEnum::Select || preset == BarButtonPresetEnum::Pierce)
			&& (compactMultilineLabel || name.content.GetVal().find(L'\n') != wstring::npos)
			? 9.0 : standardFontSize;
	}
	// 图标和文字只在内容实际变化时启动同一套缩放、淡出与回弹过程。
	bool TransitionContent(const wstring& iconResourceName, const wstring& label, bool immediateLabel = false);

public:
	// 基准元数据由注册/Load更新；运行时有效值只由统一主栏投影提交。
	IdtAtomic<ButtonSizeEnum> baseSize = ButtonSizeEnum::twoTwo;
	IdtAtomic<bool> baseUserVisible = true;
	IdtAtomic<BarButtonPresetEnum> preset = BarButtonPresetEnum::None;
	std::string id;
	// 配置显隐与运行时上下文 hide 分离，所有消费方统一读取 IsVisible()。
	IdtAtomic<bool> userVisible = true;
	IdtAtomic<bool> hide = true;
	IdtAtomic<bool> only = true;
	IdtAtomic<bool> compactMultilineLabel = false;

	// 默认动作完成后关闭更多浮层；需要连续操作的按钮可显式保留浮层。
	bool closeMoreAfterAction = true;

};

enum class BarButtonLayoutZoneEnum : int
	{
		FixedA1,
		Extension,
		FixedA2
	};

	enum class BarButtonRegistrationKindEnum : int
	{
		EntityButton,
		LayoutMarker,
	};

	struct BarButtonRegistrationClass
	{
		std::string id;
		shared_ptr<BarButtonClass> button;
		BarButtonRegistrationKindEnum kind = BarButtonRegistrationKindEnum::EntityButton;
		bool allowMultiple = false;
		BarButtonLayoutZoneEnum zone = BarButtonLayoutZoneEnum::Extension;
		// 注册时写死的默认尺寸/用户显隐；A 区配置不可改 Visible，Size 本轮也纠正回这里。
		ButtonSizeEnum defaultSize = ButtonSizeEnum::twoTwo;
		bool defaultUserVisible = true;
		wstring categoryName;
		wstring settingsName;
		bool closeMoreAfterAction = true;
	};

	struct BarMoreButtonSnapshotClass
	{
		vector<shared_ptr<BarButtonClass>> forcedOverflow;
		vector<shared_ptr<BarButtonClass>> explicitMore;

		bool HasButtons() const
		{
			return !forcedOverflow.empty() || !explicitMore.empty();
		}
	};

using BarButtonListClass = ButtonListOf<BarButtonClass>;

struct BarMainButtonLayoutEntry
{
	const BarButtonClass* identity = nullptr;
	ButtonSizeEnum size = ButtonSizeEnum::twoTwo;
	bool visible = false;
	bool operator==(const BarMainButtonLayoutEntry&) const = default;
};

class BarButtonSetClass
{
public:
	// 只保护基准、列表和有效投影/目标扫描；不跨窗口等待、面板Close或呈现。
	mutable recursive_mutex presentationMutex;
	BarButtonListClass buttonList;
	IdtAtomic<int> tot = 0; // 顺序列顶，开

	// 按钮状态
	unordered_map<int, ButtonStateClass> barButtonState;

	// 预设按钮模态
	BarButtonClass* preset[40]{};

public:
bool RegisterButton(
			const std::string& id,
			BarButtonClass* button,
			bool allowMultiple,
			BarButtonLayoutZoneEnum zone,
			bool defaultUserVisible = true,
		const wstring& categoryName = {},
		const wstring& settingsName = {},
		bool closeMoreAfterAction = true);
		bool RegisterLayoutMarker(const std::string& id);
		bool TryGetRegistration(const std::string& id, BarButtonRegistrationClass& outRegistration) const;
		vector<BarButtonRegistrationClass> GetExtensionRegistrations() const;
		BarMoreButtonSnapshotClass GetMoreButtonSnapshot() const;
		BarButtonClass* GetMoreButton() const { return moreButton.get(); }
		void PresetInitialization();
		void RefreshLocalizedText();
		void RegisterBuiltInComponents();
		void StateUpdate();
		void StateUpdate(const StateModeClass& stateMode);
		void PrepareMainPanels(const StateModeClass& stateMode);
		void UpdateMainPresentation(const StateModeClass& stateMode);
		vector<BarMainButtonLayoutEntry> MainLayoutSnapshot();
		void ExecuteClearClick(bool doubleClickContinuation);

		void Load();
		// Applied 只表示内存配置与主栏已更新；写盘由调用方经设置业务队列完成（渲染线程不得同步写盘）。
		enum class ApplyResult { Applied, Unchanged, Stale };
		Inkeys::UI::Bar::BarLayoutCatalog GetLayoutCatalog() const;
		std::optional<Inkeys::UI::Bar::BarLayoutModel> ReadBarLayout() const;
		ApplyResult ApplyBarLayout(const Inkeys::UI::Bar::BarLayoutModel& expected,
			const Inkeys::UI::Bar::BarLayoutModel& next, bool resetLegacy = false);
		void InitializeBarLayout();
		bool ConsumeLayoutReload();
		void ReleaseRetiredLayoutInstances();
		bool IsActiveLayoutButton(const shared_ptr<BarButtonClass>& button, bool inMore) const;
		// 每次实际重载布局递增；按下时记录命中冻结时刻的值，抬起时不一致则不执行动作。
		[[nodiscard]] std::uint64_t LayoutGeneration() const noexcept { return layoutGeneration.load(std::memory_order_acquire); }
		// 重载后到新命中表成功发布前，旧帧矩形可能指向已移动或已替换的按钮，期间拒绝按钮命中。
		[[nodiscard]] bool LayoutHitSuspended() const noexcept { return layoutHitSuspended.load(std::memory_order_acquire); }
		void ResetIconCaches();

	protected:
		void UpdateDrawButtonStyle(const StateModeClass& stateMode, bool immediateLabel = false);
		void UpdateWhiteboardButtonStyle(bool active, bool minimized, bool immediateLabel = false);
		void ApplyMainPresentationLocked(const StateModeClass& stateMode, bool immediateLabel = false);
		void UpdateEraserButtonStyle(const StateModeClass& stateMode, bool immediateLabel = false);
		void UpdateGeometryButtonStyle(const StateModeClass& stateMode, bool immediateLabel = false);
		void PresetHoming(const StateModeClass& stateMode);
		static Inkeys::BarButtonSizeKind ToConfigSize(ButtonSizeEnum size);
		static ButtonSizeEnum ToRuntimeSize(Inkeys::BarButtonSizeKind size);
			// 交界分割线仅进运行时列表，不写回配置。
			void AppendBoundaryDivider(
				vector<shared_ptr<BarButtonClass>>& activeButtons,
				size_t boundaryIndex);

		mutable shared_mutex registrationMutex;
		IdtAtomic<int> drawButtonStyleKey = -1;
		IdtAtomic<int> eraserButtonStyleKey = -1;
		IdtAtomic<int> geometryButtonStyleKey = -1;
		unordered_map<std::string, BarButtonRegistrationClass> registrations;
		vector<std::string> registrationOrder;
		shared_ptr<BarButtonClass> boundaryDividers[2];
		shared_ptr<BarButtonClass> moreButton;
		// 运行时插在 Recall 前，不进入 FixedButtonsA1。
		shared_ptr<BarButtonClass> mainRedoButton;
		// 运行时依附定格，不注册旧 Pierce 持久化 ID。
		shared_ptr<BarButtonClass> mainPierceButton;
		// 画布调试可视化入口：运行时插在 A1 工具之后，仅画布调试开启时可见，不进入 FixedButtonsA1。
		shared_ptr<BarButtonClass> mainCanvasDebugButton;
		mutable mutex moreSnapshotMutex;
		BarMoreButtonSnapshotClass moreSnapshot;
		mutable mutex layoutCommitMutex;
		std::optional<Inkeys::UI::Bar::BarLayoutModel> committedLayout;
		// 串行化启动导入与编辑提交（校验 expected → 替换 → 发布）；不覆盖磁盘写入。
		mutex layoutApplyMutex;
		std::atomic<bool> reloadPending{ false };
		std::atomic<std::uint64_t> layoutGeneration{ 0 };
		std::atomic<bool> layoutHitSuspended{ false };
		struct Instance
		{
			size_t index;
			bool visible;
			shared_ptr<BarButtonClass> button;
		};
		using InstanceGroups = unordered_map<std::string, vector<Instance>>;
		std::array<InstanceGroups, 4> instances;
		vector<shared_ptr<BarButtonClass>> retiredInstances;
		bool clearAttemptedForDoubleClick = false;
		bool clearAcceptedForDoubleClick = false;
	};

namespace Inkeys::UI::Bar
{
	shared_ptr<BarButtonClass> HitPresentedLayoutButtonForTest(POINT point);
	// 离屏回归驱动真实 More 按下/抬起协程，允许在两条消息之间提交布局。
	bool ReplayMoreLayoutPress(const shared_ptr<BarButtonClass>& button, const std::function<void()>& duringPress);
	// 补上主栏默认值后转调 UI3 入口。
	bool StartButtonHoverVisual(BarButtonClass& button) noexcept;
	bool StopButtonHoverVisual(BarButtonClass& button, bool immediate,
		bool preserveVisual = false) noexcept;
	void RetargetButtonInteractionVisual(BarButtonClass& button,
		bool visible, bool enabled, bool selected,
		double durationSeconds, std::optional<COLORREF> contentColor = std::nullopt,
		bool iconVisible = true, std::optional<COLORREF> fillColorOverride = std::nullopt,
		std::optional<COLORREF> frameColorOverride = std::nullopt) noexcept;
	void RetargetFixedButtonVisual(const ButtonVisualParts& parts,
		bool visible, bool enabled, bool selected, bool pressed, double duration,
		std::optional<COLORREF> contentColor = std::nullopt, bool iconVisible = true,
		std::optional<COLORREF> fillColor = std::nullopt,
		std::optional<COLORREF> frameColor = std::nullopt) noexcept;
	void ConfigureButtonVisualTargets(BarButtonClass& button,
		ButtonVisualLayoutKind layoutKind, double durationSeconds,
		double scale = 1.0, bool direct = false,
		std::optional<double> iconHeightOverride = std::nullopt,
		std::optional<double> fontTargetScale = std::nullopt);
	[[nodiscard]] ButtonAnimationResult AdvanceButtonVisual(
		BarButtonClass& button, const ButtonAnimationContext& frame,
		UiWordClass* secondary = nullptr);
	[[nodiscard]] ButtonHitSnapshot ResolveButtonHitSnapshot(
		BarButtonClass& button, const UiInheritClass& inherit) noexcept;
	bool DrawButtonVisual(UIRendering& renderer,
		ID2D1DeviceContext* deviceContext, BarButtonClass& button,
		const UiInheritClass& inherit,
		const ButtonDrawOptions& options = {});
}
