#include "Other.Config.ViewAdjustTest.h"

#if defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS

#include "../../IdtMain.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

import Inkeys.Other.Config;

namespace Inkeys::Tests
{
	namespace
	{
		Json::Value Parse(const std::string& text)
		{
			Json::CharReaderBuilder reader;
			Json::Value result;
			std::string errors;
			std::istringstream input(text);
			if (!Json::parseFromStream(reader, input, &result, &errors))
				throw std::runtime_error("invalid config fixture");
			return result;
		}

		std::string ReadBytes(const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input) throw std::runtime_error("cannot read private config");
			return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
		}

		std::string WriteFixture(const std::filesystem::path& path, const Json::Value& root)
		{
			Json::StreamWriterBuilder writer;
			const std::string bytes = Json::writeString(writer, root);
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
			if (!output.good()) throw std::runtime_error("cannot write private config");
			return bytes;
		}

		Json::Value BaseRoot()
		{
			return Parse(R"({"Config":{"AutoClean":false},"Unknown":{"keep":[1,2]},"UI":{"Bar":{
				"FixedButtonsA1":[{"Id":"Inkeys.Bar.Draw","Size":"oneOne","Extra":"A1"},
					{"Id":"Inkeys.Bar.Select","Size":"twoTwo"}],
				"FixedButtonsA2":[{"Id":"Inkeys.Bar.Freeze","Size":"twoOne"},
					{"Id":"Inkeys.Bar.Whiteboard","Size":"twoOne","Extra":"A2"}]
			}}})");
		}

		bool RuntimeMatches(const Inkeys::Config& config, const Json::Value& expected)
		{
			const auto entries = config.UI.Bar.ExtensionButtons.Snapshot();
			if (entries.size() != expected.size()) return false;
			for (std::size_t index = 0; index < entries.size(); ++index)
			{
				const auto& item = expected[static_cast<Json::ArrayIndex>(index)];
				const char* size = entries[index].Size == Inkeys::BarButtonSizeKind::OneOne ? "oneOne"
					: entries[index].Size == Inkeys::BarButtonSizeKind::OneTwo ? "oneTwo"
					: entries[index].Size == Inkeys::BarButtonSizeKind::TwoOne ? "twoOne" : "twoTwo";
				if (entries[index].Id != item["Id"].asString() || size != item["Size"].asString()
					|| entries[index].Visible != item["Visible"].asBool()) return false;
			}
			return true;
		}

		struct RestoreGlobalPath
		{
			std::wstring previous = globalPath;
			~RestoreGlobalPath() { globalPath = std::move(previous); }
		};
	}

	int RunBarViewAdjustConfigTests() noexcept
	{
		try
		{
			// 只接受仓库工作目录；全部文件留在私有 Build/ScrollFollow 下，不接触用户配置。
			const auto repo = std::filesystem::current_path();
			if (!std::filesystem::is_regular_file(repo / "InkeysRepo.sln")) return 2;
			const auto root = repo / "Build" / "ScrollFollow" /
				("view-adjust-config-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
			if (!std::filesystem::create_directories(root / "Inkeys" / "Config")) return 2;
			RestoreGlobalPath restore;
			globalPath = root.wstring() + L"\\";
			const auto file = root / "Inkeys" / "Config" / "main.json";
			int failures = 0;
			const auto check = [&](bool ok, const char* name)
			{
				if (!ok) { ++failures; std::fprintf(stderr, "[BarViewAdjustConfig] FAIL: %s\n", name); }
			};
			const auto defaultEntries = Parse(R"([{"Id":"Inkeys.Bar.MoreBoundary","Size":"twoTwo","Visible":true},
				{"Id":"Inkeys.Bar.ViewAdjust","Size":"twoTwo","Visible":true},
				{"Id":"Inkeys.Bar.Setting","Size":"twoTwo","Visible":true}])");
			Inkeys::Config defaults;
			check(RuntimeMatches(defaults, defaultEntries), "default ViewAdjust precedes Setting");

			struct Fixture { const char* name; const char* source; const char* expected; bool unchanged; };
			const Fixture fixtures[] = {
				{ "mixed order, hidden entries and unknown plugin fields",
					R"([{"Id":"Plugin.Uninstalled","Size":"oneOne","Visible":false,"Options":{"keep":42}},
					{"Id":"Inkeys.Bar.MoreBoundary","Size":"oneTwo","Visible":false},
					{"Id":"Inkeys.Bar.Setting","Size":"twoOne","Visible":false},
					{"Id":"Plugin.Later","Size":"oneTwo","Visible":true}])",
					R"([{"Id":"Plugin.Uninstalled","Size":"oneOne","Visible":false,"Options":{"keep":42}},
					{"Id":"Inkeys.Bar.MoreBoundary","Size":"oneTwo","Visible":false},
					{"Id":"Inkeys.Bar.ViewAdjust","Size":"twoTwo","Visible":true},
					{"Id":"Inkeys.Bar.Setting","Size":"twoOne","Visible":false},
					{"Id":"Plugin.Later","Size":"oneTwo","Visible":true}])", false },
				{ "existing hidden ViewAdjust keeps its position, size and fields",
					R"([{"Id":"Inkeys.Bar.Setting","Size":"twoOne","Visible":false},
					{"Id":"Plugin.Unknown","Size":"oneTwo","Visible":true},
					{"Id":"Inkeys.Bar.ViewAdjust","Size":"oneOne","Visible":false,"Keep":"value"}])", nullptr, true },
				{ "missing Setting appends only ViewAdjust",
					R"([{"Id":"Plugin.Alpha","Size":"oneOne","Visible":false}])",
					R"([{"Id":"Plugin.Alpha","Size":"oneOne","Visible":false},
					{"Id":"Inkeys.Bar.ViewAdjust","Size":"twoTwo","Visible":true}])", false },
				{ "empty B gets only ViewAdjust", "[]",
					R"([{"Id":"Inkeys.Bar.ViewAdjust","Size":"twoTwo","Visible":true}])", false },
				{ "duplicate Setting keeps existing entries and inserts once",
					R"([{"Id":"Inkeys.Bar.Setting","Size":"twoTwo","Visible":true},
					{"Id":"Inkeys.Bar.Setting","Size":"oneOne","Visible":false}])",
					R"([{"Id":"Inkeys.Bar.ViewAdjust","Size":"twoTwo","Visible":true},
					{"Id":"Inkeys.Bar.Setting","Size":"twoTwo","Visible":true},
					{"Id":"Inkeys.Bar.Setting","Size":"oneOne","Visible":false}])", false }
			};
			for (const bool mini : { false, true })
				for (const auto& fixture : fixtures)
				{
					auto source = BaseRoot();
					source["UI"]["Bar"]["ExtensionButtons"] = Parse(fixture.source);
					auto expected = source;
					const auto entries = Parse(fixture.expected ? fixture.expected : fixture.source);
					expected["UI"]["Bar"]["ExtensionButtons"] = entries;
					const auto originalBytes = WriteFixture(file, source);
					Inkeys::Config config;
					check(mini ? config.ReadMini({ "UI.Bar.ExtensionButtons" }) : config.ReadAll(), fixture.name);
					check(Parse(ReadBytes(file)) == expected && RuntimeMatches(config, entries), fixture.name);
					// 模拟启动冻结快照后没有再触发 Write；补缺必须已经真实落盘。
					Inkeys::Config frozen;
					frozen = config;
					check(RuntimeMatches(frozen, entries) && Parse(ReadBytes(file)) == expected, "frozen copy cannot hide an unpersisted migration");
					const auto migratedBytes = ReadBytes(file);
					if (fixture.unchanged) check(migratedBytes == originalBytes, "existing ViewAdjust causes no file rewrite");
					check(config.ReadAll() && ReadBytes(file) == migratedBytes, "repeated reads are idempotent");
				}

			auto partial = BaseRoot();
			partial["UI"]["Bar"]["ExtensionButtons"] = Parse(fixtures[0].source);
			WriteFixture(file, partial);
			Inkeys::Config mini;
			mini.UI.Bar.ExtensionButtons.Replace({ { "Runtime.Sentinel", Inkeys::BarButtonSizeKind::OneOne, false } });
			check(mini.ReadMini({ "Config.AutoClean" }) && mini.UI.Bar.ExtensionButtons.Snapshot().front().Id == "Runtime.Sentinel",
				"unrelated ReadMini preserves unselected runtime fields");
			partial["UI"]["Bar"]["ExtensionButtons"] = Parse(fixtures[0].expected);
			check(Parse(ReadBytes(file)) == partial, "unrelated ReadMini persists only the document migration");

			for (const bool useMini : { false, true })
			{
				auto legacy = BaseRoot();
				legacy["UI"]["Bar"].removeMember("FixedButtonsA1");
				legacy["UI"]["Bar"].removeMember("FixedButtonsA2");
				legacy["UI"]["Bar"]["ButtonLayout"] = Parse(R"([{"Id":"Inkeys.Bar.Draw"},
					{"Id":"Plugin.Legacy","Visible":false},{"Id":"Inkeys.Bar.Setting","Visible":false},
					{"Id":"Inkeys.Bar.Divider"},{"Id":"Inkeys.Bar.Freeze"},{"Id":"Inkeys.Bar.Pierce"}])");
				auto expected = legacy;
				expected["UI"]["Bar"].removeMember("ButtonLayout");
				expected["UI"]["Bar"]["FixedButtonsA1"] = Parse(R"([{"Id":"Inkeys.Bar.Draw","Size":"twoTwo"}])");
				expected["UI"]["Bar"]["FixedButtonsA2"] = Parse(R"([{"Id":"Inkeys.Bar.Freeze","Size":"twoOne"}])");
				const auto entries = Parse(R"([{"Id":"Plugin.Legacy","Size":"twoTwo","Visible":false},
					{"Id":"Inkeys.Bar.ViewAdjust","Size":"twoTwo","Visible":true},
					{"Id":"Inkeys.Bar.Setting","Size":"twoTwo","Visible":false}])");
				expected["UI"]["Bar"]["ExtensionButtons"] = entries;
				WriteFixture(file, legacy);
				Inkeys::Config config;
				check(useMini ? config.ReadMini({ "UI.Bar.ExtensionButtons" }) : config.ReadAll(), "legacy layout parses");
				check(Parse(ReadBytes(file)) == expected && RuntimeMatches(config, entries), "legacy split precedes the single-entry migration");
			}

			const auto noLayout = BaseRoot();
			const auto noLayoutBytes = WriteFixture(file, noLayout);
			Inkeys::Config missing;
			check(missing.ReadAll() && RuntimeMatches(missing, defaultEntries) && ReadBytes(file) == noLayoutBytes,
				"absent B uses new schema defaults without rewriting other fields");
			check(missing.Write() && Parse(ReadBytes(file))["UI"]["Bar"]["ExtensionButtons"] == defaultEntries,
				"normal Write persists the new default B");
			auto invalid = BaseRoot();
			invalid["UI"]["Bar"]["ExtensionButtons"] = Parse(R"([{"Id":"Plugin.Invalid","Visible":"false"}])");
			const auto invalidBytes = WriteFixture(file, invalid);
			Inkeys::Config rejected;
			check(rejected.ReadAll() && RuntimeMatches(rejected, defaultEntries) && ReadBytes(file) == invalidBytes,
				"invalid B retains the existing codec fallback and original document");
			std::fprintf(stderr, "[BarViewAdjustConfig] %s: real ReadAll/ReadMini, preserved layouts, legacy split and frozen copy (%d failures)\n",
				failures ? "FAIL" : "PASS", failures);
			return failures ? 1 : 0;
		}
		catch (...)
		{
			std::fputs("[BarViewAdjustConfig] FAIL: private configuration test exception\n", stderr);
			return 1;
		}
	}
}

#else

int Inkeys::Tests::RunBarViewAdjustConfigTests() noexcept { return 2; }

#endif
