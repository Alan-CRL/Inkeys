module;

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

export module Inkeys.UI.Bar.LayoutModel;

import Inkeys.Other.Config;

export namespace Inkeys::UI::Bar
{
	enum class Segment { A1, B1, More, A2 };
	struct Position { Segment segment; std::size_t index; };
	using ItemSource = std::variant<Position, std::string_view>;
	enum class PlaceDecision { Allowed, InvalidSource, InvalidIndex, WrongSegment, CannotInsert, SingletonConflict };
	struct BarLayoutDescriptor
	{
		Segment zone;
		BarButtonSizeKind size;
		bool entity, allowMultiple;
	};
	using BarLayoutCatalog = std::unordered_map<std::string, BarLayoutDescriptor>;
	using BarLayoutEntries = std::vector<BarExtensionButtonLayoutEntry>;
	struct BarLayoutModel
	{
		std::array<BarLayoutEntries, 4> segments;
		auto& operator[](Segment segment) { return segments.at(static_cast<std::size_t>(segment)); }
		const auto& operator[](Segment segment) const { return segments.at(static_cast<std::size_t>(segment)); }
		bool operator==(const BarLayoutModel& other) const
		{
			// 显式比较各段中的有序条目，避免 MSVC 在模块导入方删除默认比较运算符。
			for (std::size_t segment = 0; segment < segments.size(); ++segment)
			{
				const auto& left = segments[segment];
				const auto& right = other.segments[segment];
				if (left.size() != right.size()) return false;
				for (std::size_t i = 0; i < left.size(); ++i)
				{
					if (left[i].Id != right[i].Id || left[i].Size != right[i].Size ||
						left[i].Visible != right[i].Visible) return false;
				}
			}
			return true;
		}
	};

	inline bool IsComponentSegment(Segment segment)
	{
		return segment == Segment::B1 || segment == Segment::More;
	}

	inline BarLayoutModel Reset()
	{
		BarLayoutModel model;
		for (const auto& entry : MakeDefaultFixedButtonsA1().Snapshot())
			model[Segment::A1].push_back({ entry.Id, entry.Size });
		for (const auto& entry : MakeDefaultFixedButtonsA2().Snapshot())
			model[Segment::A2].push_back({ entry.Id, entry.Size });
		for (const auto& entry : MakeDefaultExtensionButtons().Snapshot())
			if (entry.Id != BarButtonId::MoreBoundary) model[Segment::More].push_back(entry);
		return model;
	}

	inline BarLayoutModel FromConfig(const BarLayoutConfigValues& configValues, const BarLayoutCatalog& catalog)
	{
		auto model = Reset();
		auto normalizeFixed = [&](Segment zone, const auto& configured)
		{
			BarLayoutEntries candidate;
			std::unordered_set<std::string> missing;
			for (const auto& entry : model[zone]) missing.insert(entry.Id);
			for (const auto& entry : configured)
			{
				if (entry.Id == BarButtonId::Divider) continue;
				auto descriptor = catalog.find(entry.Id);
				if (descriptor == catalog.end() || !descriptor->second.entity) return;
				// 可重复项保留原位置，仅必需项参与排列校验。
				if (entry.Id != BarButtonId::UserDivider &&
					(descriptor->second.zone != zone || !missing.erase(entry.Id))) return;
				candidate.push_back({ entry.Id, descriptor->second.size });
			}
			if (zone == Segment::A2 && candidate.size() == 2 && missing.size() == 1 &&
				missing.contains(BarButtonId::EndShow))
			{
				auto end = catalog.find(BarButtonId::EndShow);
				if (end == catalog.end() || !end->second.entity || end->second.zone != zone) return;
				candidate.push_back({ end->first, end->second.size });
				missing.clear();
			}
			if (missing.empty()) model[zone] = std::move(candidate);
		};
		normalizeFixed(Segment::A1, configValues.a1);
		normalizeFixed(Segment::A2, configValues.a2);
		model[Segment::More].clear();
		auto segment = Segment::B1;
		std::unordered_set<std::string> singletons;
		for (auto entry : configValues.extension)
		{
			if (entry.Id == BarButtonId::MoreBoundary)
			{
				segment = Segment::More;
				continue;
			}
			auto it = catalog.find(entry.Id);
			entry.Size = it == catalog.end() ? DefaultSizeForBarButtonId(entry.Id) : it->second.size;
			if (entry.Id == BarButtonId::Setting)
			{
				if (!singletons.insert(entry.Id).second) continue;
				entry.Visible = true;
			}
			else if (entry.Id == BarButtonId::UserDivider)
			{
				if (segment == Segment::More) continue;
			}
			else
			{
				if (!IsExtensionBarButtonId(entry.Id)) continue;
				if (it != catalog.end() && (!it->second.entity || it->second.zone != Segment::B1 ||
					(!it->second.allowMultiple && !singletons.insert(entry.Id).second))) continue;
			}
			model[segment].push_back(std::move(entry));
		}
		if (!singletons.contains(BarButtonId::Setting))
			model[Segment::More].push_back({ BarButtonId::Setting });
		return model;
	}

	inline BarLayoutConfigValues ToConfig(const BarLayoutModel& model)
	{
		BarLayoutConfigValues result;
		for (const auto& entry : model[Segment::A1]) result.a1.push_back({ entry.Id, entry.Size });
		for (const auto& entry : model[Segment::A2]) result.a2.push_back({ entry.Id, entry.Size });
		result.extension = model[Segment::B1];
		result.extension.push_back({ BarButtonId::MoreBoundary });
		const auto& more = model[Segment::More];
		result.extension.insert(result.extension.end(), more.begin(), more.end());
		return result;
	}

	inline PlaceDecision CanPlace(const BarLayoutModel& model, const ItemSource& source,
		Position gap, const BarLayoutCatalog& catalog)
	{
		if (static_cast<std::size_t>(gap.segment) >= model.segments.size() ||
			gap.index > model[gap.segment].size()) return PlaceDecision::InvalidIndex;
		const auto* from = std::get_if<Position>(&source);
		std::string id;
		if (from)
		{
			if (static_cast<std::size_t>(from->segment) >= model.segments.size() ||
				from->index >= model[from->segment].size()) return PlaceDecision::InvalidSource;
			id = model[from->segment][from->index].Id;
		}
		else id = std::get<std::string_view>(source);

		auto it = catalog.find(id);
		if (it != catalog.end() && !it->second.entity) return PlaceDecision::InvalidSource;
		bool allowed = false;
		if (id == BarButtonId::UserDivider)
		{
			if (!from && it == catalog.end()) return PlaceDecision::CannotInsert;
			allowed = gap.segment != Segment::More;
		}
		else if (IsOfficialFixedBarButtonId(id) || id == BarButtonId::Setting)
		{
			if (!from) return PlaceDecision::CannotInsert;
			allowed = id == BarButtonId::Setting ? IsComponentSegment(gap.segment) :
				gap.segment == (IsFixedButtonsA1Id(id) ? Segment::A1 : Segment::A2);
		}
		else if (IsExtensionBarButtonId(id))
		{
			if (!from && it == catalog.end()) return PlaceDecision::CannotInsert;
			allowed = IsComponentSegment(gap.segment);
			if (allowed && it != catalog.end() && !it->second.allowMultiple)
				for (auto zone : { Segment::B1, Segment::More })
					for (std::size_t i = 0; i < model[zone].size(); ++i)
						if (model[zone][i].Id == id && !(from && from->segment == zone && from->index == i))
							return PlaceDecision::SingletonConflict;
		}
		else return PlaceDecision::InvalidSource;
		return allowed ? PlaceDecision::Allowed : PlaceDecision::WrongSegment;
	}

	inline std::optional<BarLayoutModel> Move(const BarLayoutModel& model, Position from, Position gap,
		const BarLayoutCatalog& catalog)
	{
		if (CanPlace(model, from, gap, catalog) != PlaceDecision::Allowed) return {};
		auto result = model;
		auto entry = model[from.segment][from.index];
		result[from.segment].erase(result[from.segment].begin() + from.index);
		// 落点按删除前的缝隙编号；同段向右移动需抵消源项的宽度。
		if (gap.segment == from.segment && gap.index > from.index) --gap.index;
		if (!IsComponentSegment(gap.segment)) entry.Visible = true;
		result[gap.segment].insert(result[gap.segment].begin() + gap.index, std::move(entry));
		return result;
	}

	inline std::optional<BarLayoutModel> Insert(const BarLayoutModel& model, std::string_view id, Position gap,
		const BarLayoutCatalog& catalog)
	{
		if (CanPlace(model, id, gap, catalog) != PlaceDecision::Allowed) return {};
		auto result = model;
		const auto& [key, descriptor] = *catalog.find(std::string(id));
		result[gap.segment].insert(result[gap.segment].begin() + gap.index, { key, descriptor.size, true });
		return result;
	}

	inline std::optional<BarLayoutModel> Remove(const BarLayoutModel& model, Position at, const BarLayoutCatalog&)
	{
		if (static_cast<std::size_t>(at.segment) >= model.segments.size() || at.index >= model[at.segment].size()) return {};
		const auto& id = model[at.segment][at.index].Id;
		if (!IsExtensionBarButtonId(id) && id != BarButtonId::UserDivider) return {};
		auto result = model;
		result[at.segment].erase(result[at.segment].begin() + at.index);
		return result;
	}

	inline BarLayoutModel RemoveAllOf(const BarLayoutModel& input, std::string_view id, const BarLayoutCatalog&)
	{
		auto model = input;
		if (IsExtensionBarButtonId(id))
			for (auto zone : { Segment::B1, Segment::More })
				std::erase_if(model[zone], [&](const auto& entry) { return entry.Id == id; });
		return model;
	}

	inline BarLayoutModel RemoveUserDividers(const BarLayoutModel& input)
	{
		auto model = input;
		for (auto zone : { Segment::A1, Segment::B1, Segment::A2 })
			std::erase_if(model[zone], [](const auto& entry) { return entry.Id == BarButtonId::UserDivider; });
		return model;
	}

	inline std::optional<BarLayoutModel> AutoAdd(const BarLayoutModel& model, std::string_view id,
		const BarLayoutCatalog& catalog)
	{
		if (!IsExtensionBarButtonId(id)) return {};
		const auto& main = model[Segment::B1];
		auto count = std::count_if(main.begin(), main.end(), [](const auto& entry) { return IsExtensionBarButtonId(entry.Id); });
		auto zone = count < 2 ? Segment::B1 : Segment::More;
		return Insert(model, id, { zone, model[zone].size() }, catalog);
	}

	inline BarLayoutModel ImportLegacy(const BarLayoutModel& input, const std::vector<std::string>& enabled)
	{
		auto model = input;
		for (std::size_t i = 0; i < enabled.size(); ++i)
		{
			BarExtensionButtonLayoutEntry* first = nullptr;
			bool visible = false;
			for (auto zone : { Segment::B1, Segment::More })
				for (auto& entry : model[zone])
					if (entry.Id == enabled[i])
					{
						if (!first) first = &entry;
						visible |= entry.Visible;
					}
			if (!first) model[i < 2 ? Segment::B1 : Segment::More].push_back({ enabled[i], DefaultSizeForBarButtonId(enabled[i]) });
			else if (!visible) first->Visible = true;
		}
		return model;
	}
}
