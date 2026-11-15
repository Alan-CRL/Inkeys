#pragma once

#include "../../Drawing/Draw3/Draw3.ShapeInteraction.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Inkeys::UI::Bar
{
	// 接触拓扑改变时锁存当前变换，新的质心/跨度只影响后续位移，不能让对象跳到新质心。
	class ShapeContactGesture
	{
	public:
		using Vec2 = draw3::shapes::Vec2;
		using Transform = Inkeys::Drawing::Draw3::ShapeGestureTransform;
		struct Key { std::uint64_t source = 0; std::uint32_t id = 0; bool operator==(const Key&) const = default; };
		struct Contact { Key key; Vec2 reference, point; };
		void Reset() { count_ = 0; base_ = {}; current_ = {}; allowRotation_ = true; }
		bool Add(Key key, Vec2 point, bool allowRotation)
		{
			if (Find(key) != count_ || count_ == contacts_.size() || !Finite(point)) return false;
			if (!count_) { Reset(); current_.pivot = point; allowRotation_ = allowRotation; }
			Rebase(); contacts_[count_++] = {key,point,point}; return true;
		}
		bool Update(Key key, Vec2 point)
		{
			const auto index = Find(key); if (index == count_ || !Finite(point)) return false;
			contacts_[index].point = point; current_ = Resolve(); return true;
		}
		bool Remove(Key key)
		{
			const auto index = Find(key); if (index == count_) return false;
			for (auto i = index+1; i<count_; ++i) contacts_[i-1] = contacts_[i];
			--count_; Rebase(); return true;
		}
		Transform Predicted(Key key, Vec2 point) const
		{
			auto copy = *this; copy.Update(key,point); return copy.current_;
		}
		const Transform& Current() const { return current_; }
		std::size_t Count() const { return count_; }
		bool Contains(Key key) const { return Find(key) != count_; }
	private:
		static bool Finite(Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
		static Vec2 RotateScale(Vec2 p, float scale, float angle)
		{
			const float c = std::cos(angle)*scale, s = std::sin(angle)*scale;
			return {c*p.x-s*p.y,s*p.x+c*p.y};
		}
		std::size_t Find(Key key) const
		{
			for (std::size_t i=0;i<count_;++i) if (contacts_[i].key == key) return i;
			return count_;
		}
		void Rebase() { base_ = current_; for (std::size_t i=0;i<count_;++i) contacts_[i].reference = contacts_[i].point; }
		Transform Resolve() const
		{
			if (!count_) return current_;
			Vec2 from{},to{};
			for (std::size_t i=0;i<count_;++i) { from.x+=contacts_[i].reference.x; from.y+=contacts_[i].reference.y; to.x+=contacts_[i].point.x; to.y+=contacts_[i].point.y; }
			from.x/=count_; from.y/=count_; to.x/=count_; to.y/=count_;
			double oldSpan=0,newSpan=0,dot=0,cross=0;
			for (std::size_t i=0;i<count_;++i)
			{
				const Vec2 a{contacts_[i].reference.x-from.x,contacts_[i].reference.y-from.y};
				const Vec2 b{contacts_[i].point.x-to.x,contacts_[i].point.y-to.y};
				oldSpan+=a.x*a.x+a.y*a.y; newSpan+=b.x*b.x+b.y*b.y;
				dot+=a.x*b.x+a.y*b.y; cross+=a.x*b.y-a.y*b.x;
			}
			const float scale = oldSpan>1e-4 ? static_cast<float>(std::sqrt(newSpan/oldSpan)) : 1;
			const float angle = allowRotation_ && oldSpan>1e-4 && newSpan>1e-4 ? static_cast<float>(std::atan2(cross,dot)) : 0;
			// 缩到退化后仍保留真实接触，重新拉开可恢复；非法候选由共享几何拒绝。
			const Vec2 local{base_.pivot.x+base_.translation.x-from.x,base_.pivot.y+base_.translation.y-from.y};
			const auto moved=RotateScale(local,scale,angle);
			return {base_.pivot,{to.x+moved.x-base_.pivot.x,to.y+moved.y-base_.pivot.y},base_.scale*scale,base_.rotation+angle};
		}
		std::array<Contact,16> contacts_{};
		std::size_t count_=0;
		Transform base_{},current_{};
		bool allowRotation_=true;
	};
}
