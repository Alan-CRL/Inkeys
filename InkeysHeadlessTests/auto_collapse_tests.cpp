#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../Inkeys/IdtConfiguration.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.AutoCollapse.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

import Inkeys.Other.Config;

namespace
{
	constexpr const char* policyNames[]{ "Drawing", "Erasing", "Dragging", "OutsideClick" };
	constexpr int policyDefaults[]{ 1, 0, 0, 0 };

	IdtAtomic<int>& Policy(Inkeys::Config& config, int field)
	{
		auto& values = config.UI.Bar.AutoCollapse;
		switch (field)
		{
		case 0: return values.Drawing;
		case 1: return values.Erasing;
		case 2: return values.Dragging;
		default: return values.OutsideClick;
		}
	}

	Json::Value ReadJson(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		Json::Value result;
		Json::CharReaderBuilder reader;
		std::string errors;
		if (!Json::parseFromStream(reader, input, &result, &errors)) return {};
		return result;
	}

	bool WriteJson(const std::filesystem::path& path, const Json::Value& document)
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		Json::StreamWriterBuilder writer;
		output << Json::writeString(writer, document);
		return static_cast<bool>(output);
	}
}

int RunAutoCollapseTests()
{
	using namespace Inkeys::UI::Bar;
	namespace fs = std::filesystem;
	int failures = 0;
	auto check = [&](bool condition, const char* name)
	{
		if (condition) return;
		++failures;
		std::cerr << "[AutoCollapse] failed: " << name << '\n';
	};

	// 自由底栏与居中底栏都是同一竖向模式；纯策略不接收样式/横向模式，避免分离式专用门控。
	const BarBottomDockMode modes[]{ BarBottomDockMode::Floating,
		BarBottomDockMode::BottomDocked, BarBottomDockMode::BottomDocked };
	for (int policy = 0; policy <= 2; ++policy)
		for (int placement = 0; placement < 3; ++placement)
			check(ShouldAutoCollapse(policy, modes[placement])
				== (policy == 2 || (policy == 1 && placement == 0)),
				"three policies cover floating, free bottom dock and centered bottom dock");
	for (const int policy : { -1, 3, std::numeric_limits<int>::min(), std::numeric_limits<int>::max() })
		check(!ShouldAutoCollapse(policy, BarBottomDockMode::Floating)
			&& !ShouldAutoCollapse(policy, BarBottomDockMode::BottomDocked), "invalid policies never collapse");

	const fs::path root = fs::temp_directory_path()
		/ (L"inkeys-auto-collapse-" + std::to_wstring(GetCurrentProcessId())
			+ L"-" + std::to_wstring(GetTickCount64()));
	struct Restore final
	{
		fs::path root;
		std::wstring path = globalPath;
		bool brush = setlist.BrushRecover;
		bool eraser = setlist.RubberRecover;
		bool drag = setlist.regularSetting.moveRecover.load();
		bool outside = setlist.regularSetting.clickRecover.load();
		~Restore()
		{
			globalPath = path;
			setlist.BrushRecover = brush;
			setlist.RubberRecover = eraser;
			setlist.regularSetting.moveRecover = drag;
			setlist.regularSetting.clickRecover = outside;
			std::error_code ignored;
			fs::remove_all(root, ignored);
		}
	} restore{ root };
	const fs::path file = root / L"Inkeys" / L"Config" / L"main.json";
	const fs::path legacyFile = root / L"opt" / L"deploy.json";
	std::error_code error;
	fs::create_directories(file.parent_path(), error);
	if (error) return failures + 1;
	fs::create_directories(legacyFile.parent_path(), error);
	if (error) return failures + 1;
	globalPath = root.wstring() + L"\\";

	Inkeys::Config config;
	for (int field = 0; field < 4; ++field)
		check(Policy(config, field).load() == policyDefaults[field], "new schema uses independent defaults");
	const auto upload = config.GetUploadInfo();

	// 旧内存与 deploy 的全部布尔组合均不得初始化新字段，两种样式亦不改变默认值。
	for (int bits = 0; bits < 16; ++bits)
		for (int style = 0; style < 2; ++style)
		{
			setlist.BrushRecover = (bits & 1) != 0;
			setlist.RubberRecover = (bits & 2) != 0;
			setlist.regularSetting.moveRecover = (bits & 4) != 0;
			setlist.regularSetting.clickRecover = (bits & 8) != 0;
			Json::Value legacy(Json::objectValue);
			legacy["BrushRecover"] = setlist.BrushRecover;
			legacy["RubberRecover"] = setlist.RubberRecover;
			legacy["Regular"]["MoveRecover1"] = setlist.regularSetting.moveRecover.load();
			legacy["Regular"]["ClickRecover"] = setlist.regularSetting.clickRecover.load();
			Json::Value document(Json::objectValue);
			document["UI"]["Bar"]["SelectAndClickThroughMode"] = style;
			check(WriteJson(legacyFile, legacy) && WriteJson(file, document), "write private legacy/default fixture");
			check(config.ReadAll(), "read missing new policy group");
			for (int field = 0; field < 4; ++field)
				check(Policy(config, field).load() == policyDefaults[field], "legacy booleans and style do not override defaults");
			check(config.Write() && ReadJson(legacyFile) == legacy, "new config never writes or migrates legacy deploy");
		}

	for (int field = 0; field < 4; ++field)
	{
		const std::string path = std::string("UI.Bar.AutoCollapse.") + policyNames[field];
		auto& value = Policy(config, field);
		for (int candidate = 0; candidate <= 2; ++candidate)
		{
			value = candidate;
			check(config.Write() && ReadJson(file)["UI"]["Bar"]["AutoCollapse"][policyNames[field]].asInt() == candidate,
				"all legal values persist under the new policy path");
			Inkeys::Config restarted;
			check(restarted.ReadAll() && Policy(restarted, field).load() == candidate, "restart restores each policy value");
			value = (candidate + 1) % 3;
			config.UI.Bar.Zoom = 2.5;
			check(config.ReadMini({ path }) && value.load() == candidate && config.UI.Bar.Zoom.load() == 2.5,
				"ReadMini loads selected policy and preserves unrelated preference");
		}
		value = 2;
		Inkeys::Config frozen;
		frozen = config;
		value = 0;
		check(Policy(frozen, field).load() == 2 && frozen.Write()
			&& ReadJson(file)["UI"]["Bar"]["AutoCollapse"][policyNames[field]].asInt() == 2,
			"FIFO save snapshot independently retains each captured policy");

		Json::Value document(Json::objectValue);
		for (int sibling = 0; sibling < 4; ++sibling)
			if (sibling != field) document["UI"]["Bar"]["AutoCollapse"][policyNames[sibling]] = 2;
		check(WriteJson(file, document) && config.ReadAll() && value.load() == policyDefaults[field],
			"missing policy falls back independently");
		value = 2;
		config.UI.Bar.Zoom = 2.5;
		check(config.ReadMini({ path }) && value.load() == policyDefaults[field] && config.UI.Bar.Zoom.load() == 2.5,
			"ReadMini resets missing selected policy to its schema default");
		for (const Json::Value& invalid : { Json::Value(), Json::Value(-1), Json::Value(3),
			Json::Value(true), Json::Value(false), Json::Value("1"), Json::Value(1.0),
			Json::Value(Json::UInt64(4294967297ULL)), Json::Value(Json::UInt64(18446744073709551615ULL)),
			Json::Value(Json::Int64(-9223372036854775807LL)), Json::Value(Json::arrayValue), Json::Value(Json::objectValue) })
		{
			document["UI"]["Bar"]["AutoCollapse"][policyNames[field]] = invalid;
			check(WriteJson(file, document), "write invalid policy fixture");
			value = 2;
			check(config.ReadAll() && value.load() == policyDefaults[field], "ReadAll rejects invalid types, ranges and wide integers");
			for (int sibling = 0; sibling < 4; ++sibling)
				if (sibling != field) check(Policy(config, sibling).load() == 2, "invalid field does not reset valid sibling policies");
			value = 2;
			config.UI.Bar.Zoom = 2.5;
			check(config.ReadMini({ path }) && value.load() == policyDefaults[field] && config.UI.Bar.Zoom.load() == 2.5,
				"ReadMini uses the same strict codec and only selected policy default");
		}
	}

	for (const Json::Value& invalidGroup : { Json::Value(), Json::Value(true), Json::Value(2),
		Json::Value("invalid"), Json::Value(Json::arrayValue) })
	{
		Json::Value document(Json::objectValue);
		document["UI"]["Bar"]["AutoCollapse"] = invalidGroup;
		check(WriteJson(file, document) && config.ReadAll(), "malformed group safely falls back");
		for (int field = 0; field < 4; ++field)
			check(Policy(config, field).load() == policyDefaults[field], "malformed group restores all independent defaults");
	}

	for (int field = 0; field < 4; ++field) Policy(config, field) = 2;
	for (int style = 0; style < 2; ++style)
	{
		config.UI.Bar.SelectAndClickThroughMode = style;
		check(config.Write(), "persist style with auto-collapse preferences");
		Inkeys::Config restarted;
		check(restarted.ReadAll() && restarted.UI.Bar.SelectAndClickThroughMode.load() == style, "restart restores chosen style");
		for (int field = 0; field < 4; ++field)
			check(Policy(restarted, field).load() == 2, "changing style preserves all four policies");
	}
	check(config.GetUploadInfo() == upload, "all policies remain excluded from upload metadata");
	config.ResetToDefaults();
	for (int field = 0; field < 4; ++field)
		check(Policy(config, field).load() == policyDefaults[field], "reset uses the same independent schema defaults");
	std::cout << "Auto-collapse tests: " << failures << " failures\n";
	return failures;
}
