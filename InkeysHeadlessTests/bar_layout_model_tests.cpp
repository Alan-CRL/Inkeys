#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

import Inkeys.Other.Config;
import Inkeys.UI.Bar.LayoutModel;

namespace
{
	using namespace Inkeys;
	using namespace Inkeys::UI::Bar;
	namespace Id = BarButtonId;
	constexpr auto large = BarButtonSizeKind::TwoTwo;
	constexpr auto narrow = BarButtonSizeKind::OneTwo;
	int failures;
	void Check(bool ok, const char* message)
	{
		if (!ok) { ++failures; std::cerr << "[BarLayout] " << message << '\n'; }
	}
	BarLayoutCatalog Catalog()
	{
		BarLayoutCatalog catalog;
		const auto defaults = Reset();
		for (auto zone : { Segment::A1, Segment::A2 })
			for (const auto& e : defaults[zone]) catalog[e.Id] = { zone, e.Size, true, false };
		catalog[Id::UserDivider] = { Segment::A1, narrow, true, true };
		catalog[Id::MoreBoundary] = { Segment::B1, large, false, false };
		catalog[Id::Setting] = { Segment::B1, large, true, false };
		catalog["plugin.multi"] = { Segment::B1, narrow, true, true };
		catalog["plugin.one"] = { Segment::B1, large, true, false };
		return catalog;
	}
	void TestPlacementMatrix()
	{
		const auto catalog = Catalog();
		auto model = Reset();
		model[Segment::B1] = { { "plugin.multi", narrow, false }, { "plugin.one" },
			{ "plugin.absent" }, { Id::UserDivider, narrow }, { "plugin.multi", narrow } };
		const auto original = model;
		for (auto target : { Segment::A1, Segment::B1, Segment::More, Segment::A2 })
		{
			Position gap{ target, model[target].size() };
			for (auto zone : { Segment::A1, Segment::B1, Segment::More, Segment::A2 })
			{
				for (size_t i = 0; i < model[zone].size(); ++i)
				{
					const auto& entry = model[zone][i];
					bool divider = entry.Id == Id::UserDivider;
					bool component = IsExtensionBarButtonId(entry.Id);
					bool allowed = divider ? target != Segment::More :
						component || entry.Id == Id::Setting ? IsComponentSegment(target) : target == zone;
					Position source{ zone, i };
					auto moved = Move(model, source, gap, catalog);
					Check(moved.has_value() == allowed && CanPlace(model, source, gap, catalog) ==
						(allowed ? PlaceDecision::Allowed : PlaceDecision::WrongSegment), "move constraint matrix");
					if (moved) Check((*moved)[target].back() == entry, "move preserves occurrence at tail");
					Check(Remove(model, source, catalog).has_value() == (divider || component), "delete matrix");
					bool insertable = divider ? target != Segment::More :
						entry.Id == "plugin.multi" && IsComponentSegment(target);
					Check(Insert(model, entry.Id, gap, catalog).has_value() == insertable, "insert constraint matrix");
				}
			}
			for (auto id : { Id::Divider, Id::MoreBoundary, "Inkeys.Bar.More", "Inkeys.Bar.Pierce", "Inkeys.Bar.Redo" })
				Check(!Insert(model, id, gap, catalog), "runtime entities cannot be inserted");
		}
		Position first{ Segment::B1, 0 }, last{ Segment::B1, 4 };
		auto left = Move(model, last, first, catalog);
		Check(left && (*left)[Segment::B1].front().Visible && !(*left)[Segment::B1][1].Visible, "duplicate moved left by index");
		const auto same = Move(model, first, first, catalog);
		const auto adjacent = Move(model, first, { Segment::B1, 1 }, catalog);
		Check(same && *same == model && adjacent && *adjacent == model,
			"same position and adjacent right gap");
		auto right = Move(model, first, { Segment::B1, 3 }, catalog);
		Check(right && (*right)[Segment::B1][2] == model[Segment::B1][0], "pre-removal right gap");
		Position outside{ Segment::B1, 20 };
		Check(CanPlace(model, outside, first, catalog) == PlaceDecision::InvalidSource && !Move(model, outside, first, catalog)
			&& !Remove(model, outside, catalog), "invalid source");
		Check(CanPlace(model, first, outside, catalog) == PlaceDecision::InvalidIndex && !Move(model, first, outside, catalog)
			&& !Insert(model, "plugin.multi", outside, catalog), "invalid destination");
		Check(CanPlace(model, "plugin.one", first, catalog) == PlaceDecision::SingletonConflict &&
			CanPlace(model, "plugin.absent", first, catalog) == PlaceDecision::CannotInsert, "library rejection reasons");
		for (auto id : { Id::Divider, Id::MoreBoundary, "Inkeys.Bar.More", "Inkeys.Bar.Pierce", "Inkeys.Bar.Redo" })
		{
			auto invalid = model;
			invalid[Segment::B1].insert(invalid[Segment::B1].begin(), { id });
			for (auto zone : { Segment::A1, Segment::B1, Segment::More, Segment::A2 })
				Check(!Move(invalid, first, { zone, 0 }, catalog), "runtime source has no move target");
			Check(!Remove(invalid, first, catalog), "runtime source cannot be removed");
		}
		auto empty = Reset();
		Check(Insert(empty, "plugin.one", first, catalog).has_value() && !Move(empty, first, first, catalog),
			"empty segment accepts singleton but has no movable item");
		auto hiddenLine = model;
		hiddenLine[Segment::B1][3].Visible = false;
		auto fixedLine = Move(hiddenLine, { Segment::B1, 3 }, { Segment::A2, 1 }, catalog);
		Check(fixedLine && (*fixedLine)[Segment::A2][1] == BarExtensionButtonLayoutEntry{ Id::UserDivider, narrow, true },
			"user line entering fixed zone becomes visible");
		Check(model == original, "editing leaves source intact");
	}
	void TestFixedZones()
	{
		const auto catalog = Catalog();
		auto model = Reset();
		auto& a = model[Segment::A1];
		std::reverse(a.begin(), a.end());
		a.insert(a.begin(), { Id::UserDivider, narrow });
		a.insert(a.begin() + 3, 2, { Id::UserDivider, narrow });
		a.push_back({ Id::UserDivider, narrow });
		model[Segment::B1].push_back({ "plugin.absent" });
		auto values = ToConfig(model);
		values.a1.insert(values.a1.begin(), { Id::Divider });
		for (auto& entry : values.a1) entry.Size = BarButtonSizeKind::OneOne;
		Check(FromConfig(values, catalog) == model, "fixed order, divider runs and default sizes");
		for (const char* bad : { "", Id::Select, Id::Setting, Id::Freeze, "plugin.absent", "Inkeys.Bar.Unknown" })
		{
			auto invalid = values;
			if (*bad) invalid.a1.push_back({ bad }); else invalid.a1.erase(invalid.a1.begin() + 2);
			auto expected = model;
			expected[Segment::A1] = Reset()[Segment::A1];
			Check(FromConfig(invalid, catalog) == expected, "invalid A1 resets only A1");
		}
		auto oldSequence = ToConfig(Reset());
		std::swap(oldSequence.a1[2], oldSequence.a1[3]);
		Check(ToConfig(FromConfig(oldSequence, catalog)) == oldSequence, "new A1 explicitly using old order survives normalization");
		auto a2Lines = values;
		a2Lines.a2.insert(a2Lines.a2.begin() + 1, 2, { Id::UserDivider, narrow });
		Check(ToConfig(FromConfig(a2Lines, catalog)).a2 == a2Lines.a2, "A2 permits adjacent user lines in place");
		auto missingRegistration = catalog;
		missingRegistration.erase(Id::Draw);
		Check(FromConfig(values, missingRegistration)[Segment::A1] == Reset()[Segment::A1], "missing fixed registration");
		for (bool reverse : { false, true })
		{
			auto pair = values;
			pair.a2.pop_back();
			if (reverse) std::reverse(pair.a2.begin(), pair.a2.end());
			auto expected = model;
			if (reverse) std::swap(expected[Segment::A2][0], expected[Segment::A2][1]);
			Check(FromConfig(pair, catalog) == expected, "legacy A2 pair appends EndShow");
			pair.a2.push_back({ Id::UserDivider });
			Check(FromConfig(pair, catalog)[Segment::A2] == Reset()[Segment::A2], "new A2 cannot use legacy pair exception");
		}
	}
	void TestExtensionCodecModel()
	{
		auto values = ToConfig(Reset());
		values.extension = { { "plugin.multi", large, false }, { Id::UserDivider }, { Id::UserDivider },
			{ "plugin.one", narrow, false }, { Id::Setting, narrow, false }, { Id::Divider }, { Id::Draw },
			{ "Inkeys.Unknown" }, { "bad..id" }, { Id::MoreBoundary, narrow, false },
			{ Id::Setting }, { "plugin.one" }, { Id::UserDivider }, { "missing.plugin", narrow, false },
			{ Id::MoreBoundary }, { "missing.plugin", narrow }, { "plugin.multi" } };
		auto expected = Reset();
		expected[Segment::B1] = { { "plugin.multi", narrow, false }, { Id::UserDivider, narrow },
			{ Id::UserDivider, narrow }, { "plugin.one", large, false }, { Id::Setting } };
		expected[Segment::More] = { { "missing.plugin", large, false }, { "missing.plugin" }, { "plugin.multi", narrow } };
		auto parsed = FromConfig(values, Catalog());
		Check(parsed == expected, "extension parsing, visibility, missing plugins and singleton across boundary");
		Check(FromConfig(ToConfig(parsed), Catalog()) == parsed, "normalized roundtrip");
		values.extension.assign(4, { "plugin.multi" });
		parsed = FromConfig(values, Catalog());
		Check(parsed[Segment::B1] == BarLayoutEntries(4, { "plugin.multi", narrow }) &&
			parsed[Segment::More] == BarLayoutEntries{ { Id::Setting } }, "four explicit components without marker");
		auto saved = ToConfig(parsed);
		Check(saved.extension.size() == 6 && saved.extension[4] == BarExtensionButtonLayoutEntry{ Id::MoreBoundary }
			&& saved.extension[5].Id == Id::Setting, "missing boundary repaired at B1 tail");
	}
	void TestEditingAndReset()
	{
		auto catalog = Catalog();
		auto base = Reset();
		base[Segment::More].clear();
		base[Segment::B1] = { { Id::Setting }, { Id::UserDivider, narrow } };
		for (int count = 0; count <= 3; ++count)
		{
			auto result = AutoAdd(base, "plugin.multi", catalog);
			auto expected = base;
			expected[count < 2 ? Segment::B1 : Segment::More].push_back({ "plugin.multi", narrow });
			Check(result && *result == expected, "AutoAdd threshold counts only component occurrences");
			base[Segment::B1].push_back({ count % 2 ? "missing.plugin" : "plugin.multi", narrow, false });
		}
		auto third = Reset();
		third[Segment::B1].assign(2, { "plugin.multi", narrow });
		for (size_t i : { 2u, 3u })
		{
			auto added = Insert(third, "plugin.multi", { Segment::B1, i }, catalog);
			Check(added && (*added)[Segment::B1].size() == i + 1, "manual insert after threshold");
			if (added) third = *added;
		}
		base[Segment::More].push_back({ "plugin.multi", narrow });
		auto removed = Remove(base, { Segment::B1, 2 }, catalog);
		Check(removed && (*removed)[Segment::B1].size() + 1 == base[Segment::B1].size() &&
			(*removed)[Segment::B1][2].Id == "missing.plugin", "delete only selected occurrence");
		auto clean = RemoveAllOf(base, "plugin.multi", catalog);
		Check(clean[Segment::B1] == BarLayoutEntries{ { Id::Setting }, { Id::UserDivider, narrow },
			{ "missing.plugin", narrow, false }, { "missing.plugin", narrow, false } } && clean[Segment::More].empty(),
			"remove hidden and visible copies across segments");
		for (auto zone : { Segment::A1, Segment::A2 }) clean[zone].push_back({ Id::UserDivider, narrow });
		for (auto protectedId : { Id::Select, Id::Freeze, Id::Setting, Id::Divider, Id::UserDivider, "missing.other" })
			Check(RemoveAllOf(clean, protectedId, catalog) == clean, "bulk removal protects non-components and absent IDs");
		auto stripped = RemoveUserDividers(clean);
		Check(stripped[Segment::A1] == Reset()[Segment::A1] && stripped[Segment::A2] == Reset()[Segment::A2]
			&& stripped[Segment::B1].size() == 3 && stripped[Segment::B1].front().Id == Id::Setting, "remove only user lines");
		Check(!AutoAdd(base, Id::UserDivider, catalog) && !AutoAdd(base, "missing.plugin", catalog), "automatic additions require registered component");
		Config fresh;
		Check(ToConfig(Reset()) == fresh.GetBarLayout(), "reset equals fresh schema");
	}
	void TestLegacyImport()
	{
		const std::vector<std::string> ids = { BarComponentId::Explorer, BarComponentId::Taskmgr, BarComponentId::Control,
			BarComponentId::Desktop, BarComponentId::LockWorkStation, BarComponentId::Keyboardesc, BarComponentId::KeyboardAltF4,
			BarComponentId::IslandCaller1, BarComponentId::IslandCaller2, BarComponentId::SecRandom1, BarComponentId::SecRandom2,
			BarComponentId::SecRandom2Compat, BarComponentId::NamePicker, BarComponentId::ClassislandSettings,
			BarComponentId::ClassislandProfile, BarComponentId::ClassislandClassswap };
		for (const auto& id : ids)
			Check(ImportLegacy(Reset(), { id })[Segment::B1] == BarLayoutEntries{ { id } }, "individual legacy flags");
		for (const auto& enabled : { ids, std::vector<std::string>{ ids[4], ids[8], ids[15] }, std::vector<std::string>{} })
		{
			auto expected = Reset();
			for (size_t i = 0; i < enabled.size(); ++i)
				expected[i < 2 ? Segment::B1 : Segment::More].push_back({ enabled[i] });
			auto model = ImportLegacy(Reset(), enabled);
			Check(model == expected && ImportLegacy(model, enabled) == model, "registration-independent ordered idempotent import");
		}
		auto mixed = Reset();
		mixed[Segment::More] = { { ids[0], large, false }, { Id::Setting }, { ids[1], large, true }, { ids[0], large, false } };
		auto expected = mixed;
		expected[Segment::More][0].Visible = true;
		expected[Segment::More].push_back({ ids[2] });
		Check(ImportLegacy(mixed, { ids[0], ids[1], ids[2] }) == expected, "mixed flags keep position and enable first hidden copy");
	}
}

int RunBarLayoutModelTests()
{
	TestPlacementMatrix();
	TestFixedZones();
	TestExtensionCodecModel();
	TestEditingAndReset();
	TestLegacyImport();
	return failures;
}
