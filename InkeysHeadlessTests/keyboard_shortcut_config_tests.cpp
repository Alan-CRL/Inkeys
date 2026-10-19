#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../Inkeys/IdtConfiguration.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>

import Inkeys.Other.Config;

int RunKeyboardShortcutConfigTests()
{
	namespace fs = std::filesystem;
	int failures = 0;
	auto check = [&](bool value, const char* name)
	{
		if (!value) { ++failures; std::cerr << "[KeyboardShortcutConfig] " << name << '\n'; }
	};
	const auto root = fs::temp_directory_path() / (L"inkeys-shortcuts-" + std::to_wstring(GetCurrentProcessId())
		+ L"-" + std::to_wstring(GetTickCount64()));
	struct Restore final
	{
		fs::path root;
		std::wstring oldPath = globalPath;
		~Restore()
		{
			globalPath = oldPath;
			std::error_code ignored;
			fs::remove_all(root, ignored);
		}
	} restore{ root };
	const auto file = root / L"Inkeys" / L"Config" / L"main.json";
	std::error_code error;
	fs::create_directories(file.parent_path(), error);
	if (error) return 1;
	globalPath = root.wstring() + L"\\";
	auto fields = [](Inkeys::Config& config)
	{
		auto& s = config.Input.Shortcuts;
		return std::array<IdtAtomic<bool>*, 6>{ &s.ToggleAnnotation, &s.Undo, &s.RedoY, &s.RedoShiftZ, &s.Clear, &s.Freeze };
	};
	const char* names[]{ "ToggleAnnotation", "Undo", "RedoY", "RedoShiftZ", "Clear", "Freeze" };
	auto write = [&](const Json::Value& value)
	{
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		Json::StreamWriterBuilder writer;
		stream << Json::writeString(writer, value);
		return static_cast<bool>(stream);
	};
	Inkeys::Config config;
	const auto upload = config.GetUploadInfo();
	for (auto field : fields(config)) check(field->load(), "all six defaults enabled");
	// 每个组合独立持久化；两个重做组合不能共用一个开关。
	for (unsigned mask = 0; mask < 64; ++mask)
	{
		auto values = fields(config);
		for (unsigned i = 0; i < values.size(); ++i) *values[i] = (mask & (1u << i)) != 0;
		check(config.Write(), "write combination mask");
		Inkeys::Config restarted;
		check(restarted.ReadAll(), "read production config");
		auto actual = fields(restarted);
		for (unsigned i = 0; i < actual.size(); ++i)
			check(actual[i]->load() == ((mask & (1u << i)) != 0), "independent restart values");
		check(config.GetUploadInfo() == upload, "shortcut preferences excluded from upload");
	}
	for (unsigned i = 0; i < 6; ++i)
	{
		const auto path = std::string("Input.Shortcuts.") + names[i];
		auto values = fields(config);
		*values[i] = false;
		Inkeys::Config frozen;
		frozen = config;
		*values[i] = true;
		check(!fields(frozen)[i]->load() && frozen.Write(), "frozen FIFO snapshot retains false");
		config.UI.Bar.Zoom2 = 2.5;
		check(config.ReadMini({ path }) && !values[i]->load() && config.UI.Bar.Zoom2.load() == 2.5,
			"ReadMini loads selected bool without changing unrelated values");
		for (const auto& invalid : { Json::Value(), Json::Value(0), Json::Value(1), Json::Value("false"),
			Json::Value(1.0), Json::Value(Json::arrayValue), Json::Value(Json::objectValue) })
		{
			Json::Value document(Json::objectValue);
			for (unsigned j = 0; j < 6; ++j) document["Input"]["Shortcuts"][names[j]] = false;
			document["Input"]["Shortcuts"][names[i]] = invalid;
			check(write(document) && config.ReadAll() && values[i]->load(), "non-bool falls back to enabled");
			for (unsigned j = 0; j < 6; ++j) if (i != j)
				check(!values[j]->load(), "invalid field does not reset valid sibling");
			*values[i] = false;
			check(config.ReadMini({ path }) && values[i]->load(), "ReadMini uses strict bool codec");
		}
		Json::Value missing(Json::objectValue);
		missing["HotKey"][names[i]] = false; // 旧路径不能覆盖新的独立默认值。
		check(write(missing) && config.ReadAll() && values[i]->load(), "missing new field ignores legacy paths");
	}
	for (auto field : fields(config)) *field = false;
	config.ResetToDefaults();
	for (auto field : fields(config)) check(field->load(), "reset restores all defaults");
	std::cout << "Keyboard shortcut config tests: " << failures << " failures\n";
	return failures;
}
