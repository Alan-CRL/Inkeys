module;

#include <d2d1.h>
#include <cstdint>

export module Inkeys.UI.UI3.DragSession;

export namespace Inkeys::UI::UI3
{
	enum class DragPointerKind
	{
		Mouse,
		Touch,
		Pen
	};
	enum class DragPhase
	{
		Idle,
		Pressed,
		Dragging
	};
	struct DragPayload
	{
		std::uint64_t token;
	};

	class DragSession
	{
	  public:
		enum class ReleaseResult
		{
			None,
			Click,
			Drop
		};
		void Press(std::uint32_t id, DragPointerKind kind, D2D1_POINT_2F point, DragPayload payload, float pixelsPerDip)
		{
			if (phase_ != DragPhase::Idle) return;
			id_ = id;
			kind_ = kind;
			payload_ = payload;
			origin_ = point_ = point;
			threshold_ = (kind == DragPointerKind::Mouse ? 4.f : 10.f) * pixelsPerDip;
			phase_ = DragPhase::Pressed;
		}
		void BeginDragging(std::uint32_t id, DragPointerKind kind, D2D1_POINT_2F point, DragPayload payload)
		{
			if (phase_ != DragPhase::Idle) return;
			Press(id, kind, point, payload, 1);
			phase_ = DragPhase::Dragging;
		}
		bool Move(std::uint32_t id, D2D1_POINT_2F point)
		{
			if (phase_ == DragPhase::Idle || id != id_) return false;
			point_ = point;
			const float dx = point.x - origin_.x, dy = point.y - origin_.y;
			if (phase_ != DragPhase::Pressed || dx * dx + dy * dy < threshold_ * threshold_) return false;
			phase_ = DragPhase::Dragging;
			return true;
		}
		ReleaseResult Release(std::uint32_t id, D2D1_POINT_2F point)
		{
			if (phase_ == DragPhase::Idle || id != id_) return ReleaseResult::None;
			// 抬起可能与最后一次移动合并，仍按实际终点判断阈值。
			Move(id, point);
			const auto result = phase_ == DragPhase::Dragging ? ReleaseResult::Drop : ReleaseResult::Click;
			Cancel();
			return result;
		}
		void Cancel()
		{
			phase_ = DragPhase::Idle;
		}
		DragPhase Phase() const
		{
			return phase_;
		}
		DragPayload Payload() const
		{
			return payload_;
		}
		D2D1_POINT_2F Point() const
		{
			return point_;
		}
		D2D1_POINT_2F Origin() const
		{
			return origin_;
		}
		std::uint32_t PointerId() const
		{
			return id_;
		}
		DragPointerKind Kind() const
		{
			return kind_;
		}

	  private:
		DragPhase phase_ = DragPhase::Idle;
		DragPointerKind kind_ = DragPointerKind::Mouse;
		DragPayload payload_{};
		D2D1_POINT_2F point_{}, origin_{};
		std::uint32_t id_ = 0;
		float threshold_ = 0;
	};
}
