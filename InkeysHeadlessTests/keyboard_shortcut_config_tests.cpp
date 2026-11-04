#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../Inkeys/IdtConfiguration.h"
#include "../Inkeys/Inkeys/Business/DesktopShortcut.h"
#include <thread>
#include <atomic>

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

int RunDesktopShortcutConfigTests()
{
	using namespace Inkeys::Business::DesktopShortcut;
	namespace fs = std::filesystem;
	int failures = 0;
	auto check = [&](bool value, const char* name) { if (!value) { ++failures; std::cerr << "[DesktopShortcutConfig] " << name << '\n'; } };
	for (const auto* invalid : { L"", L" ", L"\u3000", L"CON", L"con.txt", L"COM¹", L"LPT².log", L"nul", L"../file", L"file.", L"file ", L"a:b", L"x?", L"a\nb" })
		check(!IsValidName(invalid), "invalid Windows filenames are rejected without rewriting");
	for (const auto* valid : { L"Inkeys", L"Inkeys(批注)", L"批注 α", L"COM10", L".Inkeys", L"图画😀" })
		check(IsValidName(valid), "valid Unicode filenames remain unchanged");
	check(IsValidName(std::wstring(251, L'a')) && !IsValidName(std::wstring(252, L'a'))
		&& IsValidName(L"a", 253) && !IsValidName(L"ab", 253), "extension/component and actual path limits are checked");
	const auto portableName=std::wstring(200,L'a');
	check(IsValidName(portableName) && NormalizePreset(Custom,portableName,true,100)==Default,
		"generic-valid restored custom name falls back on a longer actual desktop path");
	NameOptions chinese; chinese.simplifiedChinese = true; chinese.presets[2] = L"Inkeys(批注)";
	NameOptions english;
	check(ResolveName(Default, L"", chinese) == L"Inkeys" && ResolveName(Default, L"", english) == L"Inkeys", "all languages default to Inkeys");
	check(ResolveName(Brand,L"",chinese) == L"智绘教Inkeys" && ResolveName(Brand,L"",english) == L"Inkeys", "brand is available only in simplified Chinese");
	check(ResolveName(Annotation,L"",chinese) == L"Inkeys(批注)" && ResolveName(Custom,L"原名 α",english) == L"原名 α", "presets translate while custom names retain their text");
	for (bool locale : { false, true }) for (int index=0; index<(locale ? 5 : 4); ++index)
		check(IndexForPreset(PresetForIndex(index,locale),locale) == index, "persistent preset identity is independent of combo index");

	NameOptions boardChinese(Kind::Whiteboard); boardChinese.simplifiedChinese = true; boardChinese.presets[0] = L"Inkeys白板";
	NameOptions boardEnglish(Kind::Whiteboard);
	check(ResolveName(Default,L"",boardChinese)==L"Inkeys白板" && ResolveName(Default,L"",boardEnglish)==L"Inkeys Whiteboard",
		"whiteboard has a localized distinct default name");
	check(ResolveName(Brand,L"",boardChinese)==L"智绘教Inkeys白板" && ResolveName(Brand,L"",boardEnglish)==L"Inkeys Whiteboard",
		"whiteboard brand falls back outside simplified Chinese");
	check(ResolveName(WhiteboardCustom,L"我的白板",boardChinese)==L"我的白板"
		&& NormalizePreset(4,L"valid",true,0,Kind::Whiteboard)==Default, "whiteboard uses its own three persistent IDs");
	for (bool locale : {false,true}) for (int index=0; index<PresetCount(Kind::Whiteboard,locale); ++index)
		check(IndexForPreset(PresetForIndex(index,locale,Kind::Whiteboard),locale,Kind::Whiteboard)==index,
			"whiteboard combo order retains the independent preset identity");
	check(NamesCollide(L"Inkeys",L"inkeys") && NamesCollide(L"Ä",L"ä") && !NamesCollide(L"Inkeys",L"Inkeys白板"),
		"cross-category collision uses Windows ordinal case-insensitive comparison");

	const auto root = fs::temp_directory_path() / (L"inkeys-desktop-shortcuts-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	struct Restore { fs::path root; std::wstring previous = globalPath; ~Restore() { globalPath=previous; std::error_code ec; fs::remove_all(root,ec); } } restore{root};
	const auto file = root / L"Inkeys" / L"Config" / L"main.json";
	std::error_code ec; fs::create_directories(file.parent_path(),ec); if (ec) return failures+1;
	globalPath=root.wstring()+L"\\";
	auto write = [&](const Json::Value& value) { std::ofstream stream(file,std::ios::binary|std::ios::trunc); Json::StreamWriterBuilder writer; stream<<Json::writeString(writer,value); return static_cast<bool>(stream); };
	Inkeys::Config config;
	check(config.GetShortcutNamePreference().first == Default && config.Application.Shortcut.CorrectLnk && !config.Application.Shortcut.CreateLnk, "new shortcut defaults are independent of legacy settings");
	Json::Value legacy(Json::objectValue); legacy["PlugIn"]["ShortcutAssistant"]["CorrectLnk"]=false; legacy["PlugIn"]["ShortcutAssistant"]["CreateLnk"]=true;
	check(write(legacy) && config.ReadAll() && config.Application.Shortcut.CorrectLnk && !config.Application.Shortcut.CreateLnk, "legacy values are not migrated");
	for (int preset=Default; preset<=Custom; ++preset)
	{
		config.SetShortcutNamePreference(preset,L"我的批注 α");
		for (bool correct : {false,true}) for (bool create : {false,true})
		{
			config.Application.Shortcut.CorrectLnk=correct; config.Application.Shortcut.CreateLnk=create;
			check(config.Write(), "valid shortcut preference writes");
			Inkeys::Config restored; check(restored.ReadAll() && restored.GetShortcutNamePreference()==config.GetShortcutNamePreference()
				&& restored.Application.Shortcut.CorrectLnk==correct && restored.Application.Shortcut.CreateLnk==create, "name and independent switches round-trip");
		}
	}
	const auto good=config.GetShortcutNamePreference();
	check(!config.SetShortcutNamePreference(Custom,L"CON") && config.GetShortcutNamePreference()==good, "invalid edit retains last valid preference");
	for (const auto& invalid : { Json::Value(-1), Json::Value(99), Json::Value("4"), Json::Value(true), Json::Value(1.5) })
	{
		Json::Value document(Json::objectValue); document["Application"]["Shortcut"]["NamePreset"]=invalid;
		check(write(document) && config.ReadAll() && config.GetShortcutNamePreference().first==Default, "invalid preset falls back to Inkeys");
	}
	Json::Value invalidCustom(Json::objectValue); invalidCustom["Application"]["Shortcut"]["NamePreset"]=Custom; invalidCustom["Application"]["Shortcut"]["CustomName"]="CON";
	check(write(invalidCustom) && config.ReadMini({"Application.Shortcut"}) && config.GetShortcutNamePreference().first==Default, "invalid custom disk value normalizes in ReadMini");
	config.SetShortcutNamePreference(Custom,L"frozen"); Inkeys::Config frozen; frozen=config; config.SetShortcutNamePreference(Custom,L"later");
	check(frozen.GetShortcutNamePreference().second==L"frozen", "FIFO config snapshot owns the old string");
	std::atomic<bool> finished{false};
	std::thread producer([&] { for (int i=0;i<500;++i) config.SetShortcutNamePreference(Custom,L"name"+std::to_wstring(i)); finished=true; });
	while (!finished.load()) { Inkeys::Config snapshot; snapshot=config; const auto name=snapshot.GetShortcutNamePreference(); check(name.first==Custom && IsValidName(name.second), "name pair stays valid during concurrent snapshots"); }
	producer.join();
	config.ResetToDefaults();
	check(config.GetWhiteboardShortcutNamePreference()==std::pair<int,std::wstring>{Default,L""}
		&& config.Application.WhiteboardShortcut.CorrectLnk && !config.Application.WhiteboardShortcut.CreateLnk,
		"whiteboard defaults are independent and require no migration");
	config.SetShortcutNamePreference(Custom,L"Application frozen");
	config.Application.Shortcut.CorrectLnk=false; config.Application.Shortcut.CreateLnk=true;
	const auto appPreference=config.GetShortcutNamePreference(); const auto upload=config.GetUploadInfo();
	for (int preset=Default; preset<=WhiteboardCustom; ++preset)
	{
		config.SetWhiteboardShortcutNamePreference(preset,L"我的白板 α");
		for (bool correct : {false,true}) for (bool create : {false,true})
		{
			config.Application.WhiteboardShortcut.CorrectLnk=correct; config.Application.WhiteboardShortcut.CreateLnk=create;
			check(config.Write(),"write whiteboard preference");
			Inkeys::Config restored;
			check(restored.ReadAll() && restored.GetWhiteboardShortcutNamePreference()==config.GetWhiteboardShortcutNamePreference()
				&& restored.Application.WhiteboardShortcut.CorrectLnk==correct && restored.Application.WhiteboardShortcut.CreateLnk==create,
				"whiteboard IDs, text and two switches round-trip");
			check(restored.GetShortcutNamePreference()==appPreference && !restored.Application.Shortcut.CorrectLnk
				&& restored.Application.Shortcut.CreateLnk && config.GetUploadInfo()==upload,"whiteboard does not change application preference or upload payload");
		}
	}
	const auto validBoard=config.GetWhiteboardShortcutNamePreference();
	check(!config.SetWhiteboardShortcutNamePreference(WhiteboardCustom,L"CON")
		&& !config.SetWhiteboardShortcutNamePreference(Custom,L"valid") && config.GetWhiteboardShortcutNamePreference()==validBoard,
		"invalid whiteboard text or preset cannot replace a valid pair");
	for (const auto& invalid : {Json::Value(-1),Json::Value(3),Json::Value(true),Json::Value("2"),Json::Value(2.0),
		Json::Value(static_cast<Json::UInt64>(0x100000002ULL))})
	{
		Json::Value document(Json::objectValue); document["Application"]["WhiteboardShortcut"]["NamePreset"]=invalid;
		check(write(document) && config.ReadMini({"Application.WhiteboardShortcut"})
			&& config.GetWhiteboardShortcutNamePreference().first==Default && config.GetShortcutNamePreference()==appPreference,
			"invalid or wide whiteboard ID falls back without changing application fields");
	}
	Json::Value invalidBoardCustom(Json::objectValue);
	invalidBoardCustom["Application"]["WhiteboardShortcut"]["NamePreset"]=WhiteboardCustom;
	invalidBoardCustom["Application"]["WhiteboardShortcut"]["CustomName"]="CON";
	invalidBoardCustom["Application"]["WhiteboardShortcut"]["CorrectLnk"]="false";
	invalidBoardCustom["Application"]["WhiteboardShortcut"]["CreateLnk"]=1;
	check(write(invalidBoardCustom) && config.ReadMini({"Application.WhiteboardShortcut"})
		&& config.GetWhiteboardShortcutNamePreference().first==Default && config.Application.WhiteboardShortcut.CorrectLnk
		&& !config.Application.WhiteboardShortcut.CreateLnk,"invalid whiteboard text/bools use schema defaults");
	config.SetWhiteboardShortcutNamePreference(WhiteboardCustom,L"frozen whiteboard"); frozen=config;
	config.SetWhiteboardShortcutNamePreference(WhiteboardCustom,L"later whiteboard");
	check(frozen.GetWhiteboardShortcutNamePreference().second==L"frozen whiteboard","whiteboard FIFO snapshot owns its text");
	finished=false;
	std::thread boardProducer([&] { for (int i=0;i<500;++i) config.SetWhiteboardShortcutNamePreference(WhiteboardCustom,L"board"+std::to_wstring(i)); finished=true; });
	while (!finished.load()) { Inkeys::Config snapshot; snapshot=config; const auto name=snapshot.GetWhiteboardShortcutNamePreference();
		check(name.first==WhiteboardCustom && IsValidName(name.second),"whiteboard pair remains valid during concurrent config capture"); }
	boardProducer.join();
	std::cout<<"Desktop shortcut config tests: "<<failures<<" failures\n";
	return failures;
}
