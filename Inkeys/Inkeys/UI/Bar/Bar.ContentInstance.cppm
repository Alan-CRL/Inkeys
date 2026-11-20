module;

#include "../../../IdtMain.h"
#include <d2d1_1.h>
#include <dwrite_1.h>
#include <chrono>
#include <memory>
#include <optional>
#include <vector>
#include <variant>
#include <string>

export module Inkeys.UI.Bar:ContentInstance;
import Inkeys.UI.UI3;
import Inkeys.UI.Bar.Metrics;
import Inkeys.UI.Bar.LayoutModel;
import :Button;

export namespace Inkeys::UI::Bar
{
	struct PreviewInputs
	{
		std::chrono::steady_clock::time_point time;
		double width = 0, height = 0, targetHeight = 64;
		BarSizeMode sizeMode = BarSizeMode::Large;
		int iconOrder = 2;
		bool presentation = false, whiteboardEnabled = true, freezeAvailable = true;
		bool settingsVisible = true, mouse = true, editing = false, reserveHintSpace = false;
		bool edgeLighting = true, dynamicLighting = true, fractionalFont = false;
		// 指针、可用宽高和命中几何均用目标局部像素。
		std::optional<D2D1_POINT_2F> pointer;
		std::uint64_t pointerSerial = 0;
		UI3::LocalizedTypography typography;
		IDWriteFactory* writeFactory = nullptr;
		IDWriteFontCollection* fonts = nullptr;
	};
	enum class PreviewHitKind { Empty, Item, UserDivider, Boundary, Companion, More, Panel, Close };
	struct PreviewHit
	{
		PreviewHitKind kind = PreviewHitKind::Empty;
		std::optional<Position> position;
		D2D1_RECT_F rect{};
	};
	struct PreviewGap
	{
		Position position;
		D2D1_POINT_2F start, end;
		D2D1_RECT_F area;
	};
	struct PreviewGeometry
	{
		std::uint64_t frameId = 0, revision = 0;
		D2D1_RECT_F body{};
		std::vector<PreviewHit> items;
		std::vector<PreviewGap> gaps;
		float dividerReach = 0;
	};
	struct PreviewFrame
	{
		std::uint64_t frameId = 0, revision = 0;
		double scale = 1;
		D2D1_SIZE_F extent{};
		D2D1_RECT_F bounds{};
		bool active = false;
		std::optional<std::chrono::steady_clock::time_point> hintDeadline;
	};
	class BarContentInstance
	{
	public:
		BarContentInstance();
		~BarContentInstance();
		void SetLayout(const BarLayoutModel& model, std::uint64_t revision);
		bool Lift(Position source, std::uint64_t revision);
		// originalGap 使用删除前的下标，revision 是宿主开始编辑时取得的版本。
		bool Place(const BarLayoutModel& accepted, Position originalGap, std::uint64_t revision);
		void CancelLift();
		void SetMoreExpanded(bool expanded);
		void Suspend();
		PreviewFrame Advance(const PreviewInputs& inputs);
		UI3::UIRendering& Renderer();
		static UI3::LocalizedTypography CurrentTypography();
		// 浮层与预览共用同代上下文；point 是浮层纹理内的像素坐标。
		HRESULT DrawFollowingHint(UI3::FrameTarget& target, D2D1_POINT_2F point, const std::wstring& hint, double offsetDip);
		HRESULT DrawGhost(UI3::FrameTarget& target, const std::variant<Position,std::string>& source,
			D2D1_POINT_2F point, const std::wstring& hint, bool danger);
		// 宿主负责 Begin/End，EndDraw 成功后才确认本帧几何。
		HRESULT Draw(UI3::FrameTarget& target, const PreviewFrame& frame);
		void ConfirmFrame(std::uint64_t frameId, bool succeeded);
		const PreviewGeometry& Geometry() const;
		PreviewHit HitTest(D2D1_POINT_2F point) const;
		std::vector<PreviewGap> InsertionGaps(D2D1_POINT_2F point) const;
	private:
		friend struct PreviewTestAccess;
		struct State;
		std::unique_ptr<State> state_;
	};
}
namespace Inkeys::UI::Bar
{
	using namespace UI3;
	struct PreviewTestAccess
	{
		static D2D1_RECT_F HintBounds(const BarContentInstance& instance);
		static void ShowGeometryBadge(BarContentInstance& instance);
		static bool OwnsInactiveActions(const BarContentInstance& instance);
		static std::vector<double> ButtonState(const BarContentInstance& instance);
	};
	struct MoreChrome
	{
		UiShapeClass *panel, *divider, *close;
		UiSVGClass* closeSvg;
		UiValueClass& pressScale;
	};
	struct MoreSideLimit { double left, right; bool side; };
	void PositionContentMore(MoreChrome chrome, BarButtonClass* moreButton, UiShapeClass& body,
		const BarMoreButtonSnapshotClass& snapshot, BarSizeMode sizeMode,
		UiValueClass& progress, UiValueClass& opacity, bool open, bool side,
		double windowWidth, double windowHeight, double frameZoom, std::optional<MoreSideLimit> limit,
		bool dark, bool closePressed, IdtAtomic<ButtonHoverStageEnum>& closeHover, UIRendering& labelRenderer, std::optional<double> viewportMargin = {});
	void DrawContentBody(UIRendering& renderer, ID2D1DeviceContext* dc, Panel& panel, UiShapeClass& body,
		const std::vector<std::shared_ptr<BarButtonClass>>& buttons, const std::vector<BarMainButtonLayoutEntry>& layout,
		RECT* bounds, double idleFade);
	void DrawContentMore(UIRendering& renderer, ID2D1DeviceContext* dc, Panel& panel, MoreChrome chrome,
		UiShapeClass& body, const BarMoreButtonSnapshotClass& buttons, RECT* bounds);
	void DrawContentHint(UIRendering& renderer, ID2D1DeviceContext* dc, const BarHoverHintRect& r,
		const std::wstring& text, double frameZoom, double light, std::optional<UiShapeClass>& shape,
		FormatCache& cache, IDWriteFontCollection* fonts);
}
