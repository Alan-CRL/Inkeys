#include "../Inkeys/IdtConfiguration.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <type_traits>
#include <utility>

import Inkeys.Other.Config;
import Inkeys.Conv.Text;

// 仅提供生产 schema 的来源存储，不编译传统配置写入器。
SetListStruct setlist;
std::shared_mutex setlistUpdateMutex;

namespace
{
	bool Expect(bool condition, const char* name)
	{
		if (!condition) std::cerr << "[ConfigMetadata] failed: " << name << '\n';
		return condition;
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
}

int RunConfigMetadataTests()
{
	using Inkeys::ConfigDetail::ConfigReadOnlyValue;
	using Inkeys::ConfigDetail::BindConfigSource;
	namespace fs = std::filesystem;
	static_assert(!std::is_assignable_v<ConfigReadOnlyValue<std::wstring>&, std::wstring>);
	static_assert(std::is_same_v<decltype(Inkeys::Config{}.UI.Bar.UseTaskbarHeightAsScaleBase.load()), int>);
	int failures = 0;
	auto check = [&](bool condition, const char* name)
	{
		if (!Expect(condition, name)) ++failures;
	};

	for (const bool writeFile : { false, true })
		for (const bool writeReferer : { false, true })
		{
			Json::Value parent(Json::objectValue);
			parent["Known"] = "stale";
			parent["Unknown"] = "retain";
			std::string upload = "existing=keep;";
			Inkeys::ConfigDetail::WriteHiddenConfigValue(parent, "Known", Json::Value("live"), writeFile);
			Inkeys::ConfigDetail::AppendConfigUploadValue(upload, "key", "live", writeReferer);
			check(parent.isMember("Known") == writeFile
				&& (!writeFile || parent["Known"].asString() == "live")
				&& parent["Unknown"].asString() == "retain"
				&& upload == (writeReferer ? "existing=keep;key=live;" : "existing=keep;"),
				"four independent output switch combinations use production sinks");
		}

	const fs::path root = fs::temp_directory_path()
		/ (L"inkeys-config-metadata-" + std::to_wstring(GetCurrentProcessId())
			+ L"-" + std::to_wstring(GetTickCount64()));
	struct Restore final
	{
		fs::path root;
		std::wstring path = globalPath;
		std::wstring id = userId;
		std::wstring version = editionVersion;
		std::wstring date = editionDate;
		std::wstring architecture = programArchitecture;
		std::wstring target = targetArchitecture;
		std::wstring windows = windowsEdition;
		std::string channel;
		bool defaultLargeScreen = Inkeys::ConfigDetail::GetDefaultLargeScreenDevice();
		int paintDevice = setlist.paintDevice.load();
		~Restore()
		{
			globalPath = path;
			Inkeys::ConfigDetail::SetDefaultLargeScreenDevice(defaultLargeScreen);
			setlist.paintDevice = paintDevice;
			userId = id;
			editionVersion = version;
			editionDate = date;
			programArchitecture = architecture;
			targetArchitecture = target;
			windowsEdition = windows;
			{ std::unique_lock lock(setlistUpdateMutex); setlist.UpdateChannel = channel; }
			std::error_code ignored;
			fs::remove_all(root, ignored);
		}
	} restore{ root };
	{ std::shared_lock lock(setlistUpdateMutex); restore.channel = setlist.UpdateChannel; }
	const fs::path file = root / L"Inkeys" / L"Config" / L"main.json";
	std::error_code error;
	fs::create_directories(file.parent_path(), error);
	if (error) return 1;
	globalPath = root.wstring() + L"\\";

	Json::Value initial(Json::objectValue);
	initial["$Info"]["UserId"] = "untrusted-file-id";
	initial["$Info"]["EditionVersion"] = "untrusted-file-version";
	initial["$Info"]["UpdateChannel"] = "untrusted-file-channel";
	initial["$Info"]["Unknown"] = "retain";
	initial["UI"]["Bar"]["Zoom2"] = 1.25;
	{
		std::ofstream output(file, std::ios::binary);
		Json::StreamWriterBuilder writer;
		output << Json::writeString(writer, initial);
		if (!output) return 1;
	}

	Inkeys::Config config;
	// 在 Config 构造后改变来源，输出必须实时读取，不能依赖 ReadAll 刷新 H。
	userId = L"{01234567-89ab-cdef-0123-456789abcdef}";
	editionVersion = L"test-version";
	editionDate = L"test-date";
	programArchitecture = L"arm64";
	targetArchitecture = L"win64";
	windowsEdition = L"test-windows";
	const std::string channel = "Beta ;=";
	{ std::unique_lock lock(setlistUpdateMutex); setlist.UpdateChannel = channel; }
	const std::string expected = "id=01234567-89ab-cdef-0123-456789abcdef;ed=test-date;pa=arm64;we=test-windows;ch="
		+ StringToUrlencode(channel) + ";";
	check(config.GetUploadInfo() == expected, "live sources, exact keys/order and GUID without braces");
	check(config.ReadAll() && config.UI.Bar.Zoom2.load() == 1.25, "X reads normally");
	check(userId == L"{01234567-89ab-cdef-0123-456789abcdef}"
		&& editionVersion == L"test-version" && setlist.UpdateChannel == channel,
		"ReadAll cannot overwrite H sources");
	check(config.ReadMini({ "Info", "$Info", "UI.Bar.Zoom2" })
		&& config.GetUploadInfo() == expected, "ReadMini leaves H live and ignores malicious H document");
	check(config.Write(), "write initial metadata");
	auto persisted = ReadJson(file);
	check(persisted["$Info"]["UserId"].asString() == "{01234567-89ab-cdef-0123-456789abcdef}"
		&& persisted["$Info"]["EditionVersion"].asString() == "test-version"
		&& persisted["$Info"]["TargetArchitecture"].asString() == "win64"
		&& persisted["$Info"]["UpdateChannel"].asString() == channel
		&& persisted["$Info"]["Unknown"].asString() == "retain",
		"file keeps braced id, ev, channel and unknown keys independently of Referer flags");

	editionDate = L"new-date";
	editionVersion = L"new-version";
	{ std::unique_lock lock(setlistUpdateMutex); setlist.UpdateChannel = "Release"; }
	check(config.GetUploadInfo().find("ed=new-date;") != std::string::npos
		&& config.GetUploadInfo().ends_with("ch=Release;"), "next output sees updated sources without read");
	check(config.Write(), "write latest channel without read");
	persisted = ReadJson(file);
	check(persisted["$Info"]["UpdateChannel"].asString() == "Release"
		&& persisted["$Info"]["EditionVersion"].asString() == "new-version",
		"file sees latest bound values");
	config.ResetToDefaults();
	check(config.UI.Bar.Zoom2.load() == 1.0 && editionVersion == L"new-version"
		&& setlist.UpdateChannel == "Release", "reset restores X only");
	config.UI.Bar.Zoom2 = 1.75;
	config.UI.Bar.ExtensionButtons.Replace({ { "test.extension", Inkeys::BarButtonSizeKind::OneOne, false } });
	Inkeys::Config copy;
	copy = config;
	const auto copiedButtons = copy.UI.Bar.ExtensionButtons.Snapshot();
	check(copy.UI.Bar.Zoom2.load() == 1.75
		&& copiedButtons.size() == 1 && copiedButtons.front().Id == "test.extension"
		&& copy.GetUploadInfo() == config.GetUploadInfo(), "copy preserves X scalars/sequences and H binding");
	config.UI.Bar.Zoom2 = 2.0;
	editionDate = L"after-copy";
	check(copy.UI.Bar.Zoom2.load() == 1.75
		&& copy.GetUploadInfo().find("ed=after-copy;") != std::string::npos,
		"copied X is independent while copied H stays live");

	ConfigReadOnlyValue<std::wstring> fixed{ L"fixed" };
	std::wstring value = L"first";
	ConfigReadOnlyValue<std::wstring> live{ &value };
	value = L"second";
	check(fixed.Snapshot() == L"fixed" && live.Snapshot() == L"second", "fixed versus live typed source");
	bool rejectedNull = false;
	try { ConfigReadOnlyValue<std::wstring> invalid{ static_cast<const std::wstring*>(nullptr) }; }
	catch (const std::invalid_argument&) { rejectedNull = true; }
	check(rejectedNull, "null source rejected");
	config.Info.EditionDate = fixed;
	copy = config;
	editionDate = L"external-change";
	copy.ResetToDefaults();
	check(copy.GetUploadInfo().find("ed=fixed;") != std::string::npos
		&& editionDate == L"external-change", "fixed descriptor copy/reset never changes external source");

	std::thread writer([]
	{
		for (int i = 0; i < 200; ++i)
		{
			std::unique_lock lock(setlistUpdateMutex);
			setlist.UpdateChannel = i % 2 ? "Channel-A" : "Channel-B";
		}
	});
	for (int i = 0; i < 200; ++i)
	{
		const auto upload = config.GetUploadInfo();
		check(upload.ends_with("ch=Release;") || upload.ends_with("ch=Channel-A;")
			|| upload.ends_with("ch=Channel-B;"), "locked channel yields a complete snapshot");
	}
	writer.join();
	{ std::unique_lock lock(setlistUpdateMutex); setlist.UpdateChannel = "Final"; }
	check(config.GetUploadInfo().ends_with("ch=Final;"), "final synchronized channel value");

	// 在同一私有目录使用真实配置读写，覆盖模式默认值、严格校验和冻结写盘副本。
	Inkeys::Config modeConfig;
	check(modeConfig.UI.Bar.SelectAndClickThroughMode.load() == 0, "interaction mode defaults to combined");
	const std::string modeUpload = modeConfig.GetUploadInfo();
	for (const int mode : { 0, 1 })
	{
		modeConfig.UI.Bar.SelectAndClickThroughMode = mode;
		check(modeConfig.Write(), "write both interaction modes");
		const auto document = ReadJson(file);
		check(document["UI"]["Bar"]["SelectAndClickThroughMode"].asInt() == mode,
			"mode is persisted under UI.Bar");
		Inkeys::Config restarted;
		check(restarted.ReadAll() && restarted.UI.Bar.SelectAndClickThroughMode.load() == mode,
			"restart restores persisted interaction mode");
		check(modeConfig.GetUploadInfo() == modeUpload, "mode is excluded from upload metadata");
	}
	modeConfig.UI.Bar.SelectAndClickThroughMode = 1;
	Inkeys::Config frozenMode;
	frozenMode = modeConfig;
	modeConfig.UI.Bar.SelectAndClickThroughMode = 0;
	check(frozenMode.UI.Bar.SelectAndClickThroughMode.load() == 1 && frozenMode.Write()
		&& ReadJson(file)["UI"]["Bar"]["SelectAndClickThroughMode"].asInt() == 1,
		"frozen configuration copy retains mode independently");
	modeConfig.UI.Bar.SelectAndClickThroughMode = 1;
	modeConfig.ResetToDefaults();
	check(modeConfig.UI.Bar.SelectAndClickThroughMode.load() == 0, "reset returns to combined");

	auto writeModeDocument = [&](const Json::Value& document)
	{
		std::ofstream output(file, std::ios::binary | std::ios::trunc);
		Json::StreamWriterBuilder builder;
		output << Json::writeString(builder, document);
		check(static_cast<bool>(output), "write mode input fixture");
	};
	// 使用真实 schema 和写盘链验证字体实验偏好，不接受数值/字符串伪装成 bool。
	{
		constexpr const char* fontPath = "Experimental.Inkeys3.UI3.AllowFractionalFontSize";
		Inkeys::Config fonts;
		auto& preference = fonts.Experimental.Inkeys3.UI3.AllowFractionalFontSize;
		check(!preference.load(), "fractional fonts default off");
		const auto upload = fonts.GetUploadInfo();
		for (const bool allowed : { true, false })
		{
			preference = allowed;
			check(fonts.Write()
				&& ReadJson(file)["Experimental"]["Inkeys3"]["UI3"]["AllowFractionalFontSize"].asBool() == allowed,
				"fractional font policy persists as a JSON bool");
			Inkeys::Config restarted;
			check(restarted.ReadAll() && restarted.Experimental.Inkeys3.UI3.AllowFractionalFontSize.load() == allowed,
				"new process configuration restores fractional font policy");
			check(fonts.GetUploadInfo() == upload, "fractional font preference is not uploaded");
			preference = !allowed; fonts.UI.Bar.Zoom2 = 1.25;
			check(fonts.ReadMini({fontPath}) && preference.load() == allowed && fonts.UI.Bar.Zoom2.load() == 1.25,
				"font Mini read updates only the selected preference");
		}
		preference = true;
		Inkeys::Config frozen; frozen = fonts; preference = false;
		check(frozen.Write() && ReadJson(file)["Experimental"]["Inkeys3"]["UI3"]["AllowFractionalFontSize"].asBool(),
			"frozen font configuration retains its captured preference");
		preference = true; fonts.ResetToDefaults();
		check(!preference.load(), "font preference reset restores default off");
		writeModeDocument(Json::Value(Json::objectValue)); preference = true;
		check(fonts.ReadAll() && !preference.load(), "old configuration missing font preference defaults off");
		preference = true; fonts.UI.Bar.Zoom2 = 1.25;
		check(fonts.ReadMini({fontPath}) && !preference.load() && fonts.UI.Bar.Zoom2.load() == 1.25,
			"Mini missing font preference resets only the selected field");
		for (const Json::Value& invalid : {Json::Value(), Json::Value(0), Json::Value(1), Json::Value(1.0),
			Json::Value("true"), Json::Value(Json::arrayValue), Json::Value(Json::objectValue)})
		{
			Json::Value document(Json::objectValue);
			document["Experimental"]["Inkeys3"]["UI3"]["AllowFractionalFontSize"] = invalid;
			writeModeDocument(document); preference = true;
			check(fonts.ReadAll() && !preference.load(), "invalid font preference type defaults off");
			preference = true; fonts.UI.Bar.Zoom2 = 1.25;
			check(fonts.ReadMini({fontPath}) && !preference.load() && fonts.UI.Bar.Zoom2.load() == 1.25,
				"Mini rejects invalid font preference without unrelated changes");
		}
	}

	// 使用真实配置验证两个类型独立恢复，退役高亮键不能重建可选边缘策略。
	Inkeys::Config cursorConfig;
	const auto cursorUpload = cursorConfig.GetUploadInfo();
	auto& cursor = cursorConfig.Drawing.Cursor;
	check(cursor.MouseStyle.load() == 0 && cursor.PenStyle.load() == 0,
		"cursor defaults use the system mouse and the original circular pen");
	for (const int mouseStyle : { 0, 1 })
	{
		for (const int penStyle : { 0, 1 })
		{
			cursor.MouseStyle = mouseStyle;
			cursor.PenStyle = penStyle;
			check(cursorConfig.Write(), "write each independent mouse and pen combination");
			const auto document = ReadJson(file);
			check(document["Drawing"]["Cursor"]["MouseStyle"].asInt() == mouseStyle
				&& document["Drawing"]["Cursor"]["PenStyle"].asInt() == penStyle,
				"both cursor types preserve their own selected value");
			Inkeys::Config restarted;
			check(restarted.ReadAll() && restarted.Drawing.Cursor.MouseStyle.load() == mouseStyle
				&& restarted.Drawing.Cursor.PenStyle.load() == penStyle,
				"restart restores all four independent cursor type combinations");
			check(!document["Drawing"]["Cursor"].isMember("HideSystemTouchFeedback"),
				"fixed touch cursor hiding has no persisted user toggle");
		}
	}
	const std::array<IdtAtomic<int>*, 2> typePreferences{ &cursor.MouseStyle, &cursor.PenStyle };
	const std::array<const char*, 2> typeNames{ "MouseStyle", "PenStyle" };
	for (size_t index = 0; index < typePreferences.size(); ++index)
	{
		const std::string path = std::string("Drawing.Cursor.") + typeNames[index];
		for (const Json::Value& invalid : { Json::Value(), Json::Value(-1), Json::Value(2), Json::Value(true),
			Json::Value("1"), Json::Value(1.0), Json::Value(Json::UInt64(4294967297ULL)), Json::Value(Json::arrayValue) })
		{
			Json::Value cursorDocument(Json::objectValue);
			cursorDocument["Drawing"]["Cursor"][typeNames[index]] = invalid;
			writeModeDocument(cursorDocument);
			*typePreferences[index] = 1;
			*typePreferences[1 - index] = 1;
			cursorConfig.UI.Bar.Zoom2 = 2.5;
			check(cursorConfig.ReadMini({ path }) && typePreferences[index]->load() == 0
				&& typePreferences[1 - index]->load() == 1 && cursorConfig.UI.Bar.Zoom2.load() == 2.5,
				"invalid type restores its own default without coercion or changing the other input");
		}
	}
	writeModeDocument(Json::Value(Json::objectValue));
	cursor.MouseStyle = 1;
	cursor.PenStyle = 1;
	check(cursorConfig.ReadAll() && cursor.MouseStyle.load() == 0 && cursor.PenStyle.load() == 0,
		"missing cursor fields restore the system mouse and circular pen defaults");
	Json::Value retiredCursorDocument(Json::objectValue);
	retiredCursorDocument["Config"]["AutoClean"] = false;
	retiredCursorDocument["Drawing"]["Cursor"]["MouseStyle"] = 1;
	retiredCursorDocument["Drawing"]["Cursor"]["PenStyle"] = 0;
	retiredCursorDocument["Drawing"]["Cursor"]["MouseHighlightEdge"] = false;
	retiredCursorDocument["Drawing"]["Cursor"]["ScreenPenHighlightEdge"] = true;
	retiredCursorDocument["Drawing"]["Cursor"]["DigitizerHighlightEdge"] = "false";
	retiredCursorDocument["Drawing"]["Cursor"]["FutureCursorSetting"] = 37;
	writeModeDocument(retiredCursorDocument);
	check(cursorConfig.ReadAll() && cursor.MouseStyle.load() == 1 && cursor.PenStyle.load() == 0,
		"retired edge values cannot change the selected mouse or pen type");
	check(cursorConfig.Write(), "write cursor types while preserving unknown configuration");
	const auto cleanedCursor = ReadJson(file)["Drawing"]["Cursor"];
	check(!cleanedCursor.isMember("MouseHighlightEdge") && !cleanedCursor.isMember("ScreenPenHighlightEdge")
		&& !cleanedCursor.isMember("DigitizerHighlightEdge") && cleanedCursor["FutureCursorSetting"].asInt() == 37,
		"saving removes exactly the three retired edge switches even with AutoClean disabled");
	Inkeys::Config frozenCursor;
	frozenCursor = cursorConfig;
	cursor.MouseStyle = 0;
	cursor.PenStyle = 1;
	check(frozenCursor.Write() && ReadJson(file)["Drawing"]["Cursor"]["MouseStyle"].asInt() == 1
		&& ReadJson(file)["Drawing"]["Cursor"]["PenStyle"].asInt() == 0,
		"frozen cursor snapshot keeps both captured types independently");
	check(cursorConfig.GetUploadInfo() == cursorUpload, "cursor type preferences are not uploaded");

	Json::Value modeDocument(Json::objectValue);
	modeDocument["UI"]["Bar"]["Zoom2"] = 1.25;
	modeConfig.UI.Bar.SelectAndClickThroughMode = 1;
	writeModeDocument(modeDocument);
	check(modeConfig.ReadAll() && modeConfig.UI.Bar.SelectAndClickThroughMode.load() == 0,
		"missing mode falls back to combined on ReadAll");
	modeConfig.UI.Bar.Zoom2 = 2.5;
	modeConfig.UI.Bar.SelectAndClickThroughMode = 1;
	check(modeConfig.ReadMini({ "UI.Bar.SelectAndClickThroughMode" })
		&& modeConfig.UI.Bar.SelectAndClickThroughMode.load() == 0 && modeConfig.UI.Bar.Zoom2.load() == 2.5,
		"ReadMini missing mode resets only the selected field");
	for (const Json::Value& invalid : { Json::Value(), Json::Value(-1), Json::Value(2),
		Json::Value(true), Json::Value("1"), Json::Value(1.0), Json::Value(Json::UInt64(4294967297ULL)),
		Json::Value(Json::UInt64(18446744073709551615ULL)) })
	{
		modeDocument["UI"]["Bar"]["SelectAndClickThroughMode"] = invalid;
		writeModeDocument(modeDocument);
		modeConfig.UI.Bar.SelectAndClickThroughMode = 1;
		check(modeConfig.ReadAll() && modeConfig.UI.Bar.SelectAndClickThroughMode.load() == 0,
			"ReadAll rejects illegal mode type/value without integer truncation");
		modeConfig.UI.Bar.Zoom2 = 2.5;
		modeConfig.UI.Bar.SelectAndClickThroughMode = 1;
		check(modeConfig.ReadMini({ "UI.Bar.SelectAndClickThroughMode" })
			&& modeConfig.UI.Bar.SelectAndClickThroughMode.load() == 0 && modeConfig.UI.Bar.Zoom2.load() == 2.5,
			"ReadMini normalizes invalid mode without changing unrelated fields");
	}
	modeDocument["UI"]["Bar"]["SelectAndClickThroughMode"] = 1;
	writeModeDocument(modeDocument);
	check(modeConfig.ReadMini({ "UI.Bar.SelectAndClickThroughMode" })
		&& modeConfig.UI.Bar.SelectAndClickThroughMode.load() == 1 && modeConfig.UI.Bar.Zoom2.load() == 2.5,
		"ReadMini loads separate mode without changing unrelated fields");

	// 主栏两项使用生产 schema，覆盖所有合法值、严格类型校验及异步保存所用的冻结副本。
	for (int field = 0; field < 2; ++field)
	{
		const char* name = field == 0 ? "IconOrder" : "ExpandDirectionAdjustment";
		const std::string path = std::string("UI.Bar.") + name;
		const int defaultValue = field == 0 ? 0 : 1;
		const int maxValue = field == 0 ? 3 : 1;
		const int nonDefaultValue = field == 0 ? 3 : 0;
		Inkeys::Config toolbarConfig;
		auto& preference = field == 0 ? toolbarConfig.UI.Bar.IconOrder : toolbarConfig.UI.Bar.ExpandDirectionAdjustment;
		check(preference.load() == defaultValue, "toolbar preference schema default");
		const auto toolbarUpload = toolbarConfig.GetUploadInfo();
		for (int preferenceValue = 0; preferenceValue <= maxValue; ++preferenceValue)
		{
			preference = preferenceValue;
			check(toolbarConfig.Write() && ReadJson(file)["UI"]["Bar"][name].asInt() == preferenceValue,
				"all toolbar preference values persist under UI.Bar");
			Inkeys::Config restarted;
			check(restarted.ReadAll()
				&& (field == 0 ? restarted.UI.Bar.IconOrder.load() : restarted.UI.Bar.ExpandDirectionAdjustment.load()) == preferenceValue,
				"restart restores each toolbar preference value");
			check(toolbarConfig.GetUploadInfo() == toolbarUpload, "toolbar preferences are not uploaded");
			preference = defaultValue;
			toolbarConfig.UI.Bar.Zoom2 = 2.5;
			check(toolbarConfig.ReadMini({ path }) && preference.load() == preferenceValue && toolbarConfig.UI.Bar.Zoom2.load() == 2.5,
				"ReadMini loads each toolbar value without altering unrelated fields");
		}
		preference = maxValue;
		Inkeys::Config frozenToolbar;
		frozenToolbar = toolbarConfig;
		preference = 0;
		check(frozenToolbar.Write() && ReadJson(file)["UI"]["Bar"][name].asInt() == maxValue,
			"frozen toolbar configuration retains captured preference");
		preference = nonDefaultValue;
		toolbarConfig.ResetToDefaults();
		check(preference.load() == defaultValue, "toolbar preference reset restores schema default");
		Json::Value toolbarDocument(Json::objectValue);
		toolbarDocument["UI"]["Bar"]["Zoom2"] = 1.25;
		preference = nonDefaultValue;
		writeModeDocument(toolbarDocument);
		check(toolbarConfig.ReadAll() && preference.load() == defaultValue,
			"ReadAll missing toolbar field restores default for existing configurations");
		preference = nonDefaultValue;
		toolbarConfig.UI.Bar.Zoom2 = 2.5;
		check(toolbarConfig.ReadMini({ path }) && preference.load() == defaultValue && toolbarConfig.UI.Bar.Zoom2.load() == 2.5,
			"ReadMini missing toolbar field resets selected preference only");
		for (const Json::Value& invalid : { Json::Value(), Json::Value(-1), Json::Value(maxValue + 1),
			Json::Value(true), Json::Value("1"), Json::Value(1.0), Json::Value(Json::UInt64(4294967297ULL)),
			Json::Value(Json::UInt64(18446744073709551615ULL)), Json::Value(Json::arrayValue), Json::Value(Json::objectValue) })
		{
			toolbarDocument["UI"]["Bar"][name] = invalid;
			writeModeDocument(toolbarDocument);
			preference = nonDefaultValue;
			check(toolbarConfig.ReadAll() && preference.load() == defaultValue,
				"ReadAll rejects invalid toolbar type/value without truncation");
			preference = nonDefaultValue;
			toolbarConfig.UI.Bar.Zoom2 = 2.5;
			check(toolbarConfig.ReadMini({ path }) && preference.load() == defaultValue && toolbarConfig.UI.Bar.Zoom2.load() == 2.5,
				"ReadMini normalizes invalid toolbar preference without unrelated changes");
		}
	}

	using Inkeys::ConfigDetail::ResolveDefaultLargeScreenDevice;
	struct DeviceDefaultCase final
	{
		int digitizerStatus;
		bool sizeAvailable;
		int widthCm;
		int heightCm;
		bool largeScreen;
	};
	const DeviceDefaultCase deviceCases[] = {
		{ 0, false, 0, 0, false },
		{ NID_READY, false, 0, 0, false },
		{ NID_INTEGRATED_TOUCH, true, 100, 100, false },
		{ NID_READY | NID_INTEGRATED_PEN, false, 0, 0, false },
		{ NID_READY | NID_INTEGRATED_TOUCH, false, 0, 0, true },
		{ NID_READY | NID_EXTERNAL_TOUCH, false, 0, 0, true },
		{ NID_READY | NID_INTEGRATED_TOUCH, true, 20, 59, false },
		{ NID_READY | NID_INTEGRATED_TOUCH, true, 20, 60, true },
		{ NID_READY | NID_EXTERNAL_TOUCH, true, 20, 61, true },
	};
	for (const auto& test : deviceCases)
		check(ResolveDefaultLargeScreenDevice(test.digitizerStatus, test.sizeAvailable,
			test.widthCm, test.heightCm) == test.largeScreen,
			"production startup classifier retains touch readiness, unknown size and inclusive area threshold");
	for (const bool defaultLargeScreen : { false, true })
	{
		const int defaultMode = defaultLargeScreen ? 1 : 0;
		// 用户交互模式与硬件缺省相反时，也不能影响 schema 的设备条件缺省。
		setlist.paintDevice = defaultLargeScreen ? 1 : 0;
		Inkeys::ConfigDetail::SetDefaultLargeScreenDevice(defaultLargeScreen);
		// 改名刻意不导入旧倍率；走真实文件和生产 schema，避免测试复制迁移逻辑。
		Inkeys::Config scaleConfig;
		check(scaleConfig.UI.Bar.Zoom2.load() == 1.0
			&& scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase.load() == defaultMode, "scale schema uses 100 percent and frozen device default");
		std::error_code removeError;
		fs::remove(file, removeError);
		scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase = 2;
		check(!scaleConfig.ReadAll()
			&& scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase.load() == defaultMode,
			"missing file ReadAll still applies frozen device default");
		scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase = 2;
		scaleConfig.UI.Bar.Zoom2 = 1.5;
		check(!scaleConfig.ReadMini({ "UI.Bar.UseTaskbarHeightAsScaleBase" })
			&& scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase.load() == defaultMode
			&& scaleConfig.UI.Bar.Zoom2.load() == 1.5,
			"missing file Mini applies only selected device default");
		Json::Value scaleDocument(Json::objectValue);
		scaleDocument["UI"]["Bar"]["Zoom"] = 1.75;
		writeModeDocument(scaleDocument);
		scaleConfig.UI.Bar.Zoom2 = 2.0;
		scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase = 2;
		check(scaleConfig.ReadAll() && scaleConfig.UI.Bar.Zoom2.load() == 1.0
			&& scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase.load() == defaultMode, "ReadAll ignores old Zoom and supplies new defaults");
		scaleConfig.UI.Bar.Zoom2 = 2.0;
		scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase = 2;
		check(scaleConfig.ReadMini({ "UI.Bar.Zoom2", "UI.Bar.UseTaskbarHeightAsScaleBase" })
			&& scaleConfig.UI.Bar.Zoom2.load() == 1.0 && scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase.load() == defaultMode,
			"startup Mini ignores old Zoom and resets selected scale fields");
		check(scaleConfig.Write(), "write scale defaults alongside unknown old Zoom");
		const auto defaultScaleDocument = ReadJson(file);
		check(defaultScaleDocument["UI"]["Bar"]["Zoom"].asDouble() == 1.75
			&& defaultScaleDocument["UI"]["Bar"]["Zoom2"].asDouble() == 1.0
			&& defaultScaleDocument["UI"]["Bar"]["UseTaskbarHeightAsScaleBase"].isInt()
			&& defaultScaleDocument["UI"]["Bar"]["UseTaskbarHeightAsScaleBase"].asInt() == defaultMode,
			"unknown old Zoom is retained without migrating it to Zoom2");
		scaleDocument["UI"]["Bar"]["Zoom2"] = 1.25;
		const std::pair<Json::Value, int> modeCases[] = {
			{ Json::Value(false), 0 }, { Json::Value(true), 2 },
			{ Json::Value(0), 0 }, { Json::Value(1), 1 }, { Json::Value(2), 2 },
			{ Json::Value(Json::UInt(1)), 1 }, { Json::Value(Json::UInt(2)), 2 }
		};
		for (const auto& [modeValue, expectedMode] : modeCases)
		{
			scaleDocument["UI"]["Bar"]["UseTaskbarHeightAsScaleBase"] = modeValue;
			writeModeDocument(scaleDocument);
			check(scaleConfig.ReadAll() && scaleConfig.UI.Bar.Zoom2.load() == 1.25
				&& scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase.load() == expectedMode,
				"ReadAll maps legacy bool to 0/2 and honors all three integer modes");
			scaleConfig.UI.Bar.Zoom2 = 1.5;
			const int otherMode = (expectedMode + 1) % 3;
			scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase = otherMode;
			check(scaleConfig.ReadMini({ "UI.Bar.UseTaskbarHeightAsScaleBase" })
				&& scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase.load() == expectedMode
				&& scaleConfig.UI.Bar.Zoom2.load() == 1.5, "Mini taskbar mode leaves zoom untouched");
			check(scaleConfig.ReadMini({ "UI.Bar.Zoom2" }) && scaleConfig.UI.Bar.Zoom2.load() == 1.25
				&& scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase.load() == expectedMode,
				"Mini new zoom does not read old key or alter taskbar mode");
			check(scaleConfig.Write(), "write normalized taskbar mode");
			const auto normalized = ReadJson(file)["UI"]["Bar"]["UseTaskbarHeightAsScaleBase"];
			check(normalized.isInt() && normalized.asInt() == expectedMode,
				"legacy bool and integer modes serialize as their exact integer mode");
			const auto scaleUpload = scaleConfig.GetUploadInfo();
			scaleConfig.UI.Bar.Zoom2 = 1.5;
			scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase = otherMode;
			check(scaleConfig.GetUploadInfo() == scaleUpload, "zoom and taskbar mode are NoUpload");
			Inkeys::Config frozenScale;
			frozenScale = scaleConfig;
			scaleConfig.UI.Bar.Zoom2 = 2.0;
			scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase = expectedMode;
			check(frozenScale.UI.Bar.Zoom2.load() == 1.5
				&& frozenScale.UI.Bar.UseTaskbarHeightAsScaleBase.load() == otherMode && frozenScale.Write(),
				"frozen scale payload retains both independent captured values");
			Inkeys::Config restarted;
			check(restarted.ReadAll() && restarted.UI.Bar.Zoom2.load() == 1.5
				&& restarted.UI.Bar.UseTaskbarHeightAsScaleBase.load() == otherMode,
				"all scale modes round-trip through frozen write and restart");
		}
		for (const Json::Value& invalid : { Json::Value(), Json::Value(-1), Json::Value(3),
			Json::Value(1.0), Json::Value(2.0), Json::Value("1"), Json::Value("false"),
			Json::Value(Json::UInt64(4294967297ULL)), Json::Value(Json::Int64(-4294967296LL)),
			Json::Value(Json::arrayValue), Json::Value(Json::objectValue) })
		{
			scaleDocument["UI"]["Bar"]["UseTaskbarHeightAsScaleBase"] = invalid;
			writeModeDocument(scaleDocument);
			scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase = 2;
			check(scaleConfig.ReadAll() && scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase.load() == defaultMode,
				"ReadAll rejects invalid taskbar mode and restores device default");
			scaleConfig.UI.Bar.Zoom2 = 1.5;
			scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase = 2;
			check(scaleConfig.ReadMini({ "UI.Bar.UseTaskbarHeightAsScaleBase" })
				&& scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase.load() == defaultMode && scaleConfig.UI.Bar.Zoom2.load() == 1.5,
				"Mini rejects invalid taskbar mode without unrelated changes");
		}
		for (const Json::Value& invalid : { Json::Value(), Json::Value(true), Json::Value("1.5"),
			Json::Value(Json::arrayValue), Json::Value(Json::objectValue) })
		{
			scaleDocument["UI"]["Bar"]["Zoom2"] = invalid;
			writeModeDocument(scaleDocument);
			scaleConfig.UI.Bar.Zoom2 = 2.0;
			check(scaleConfig.ReadAll() && scaleConfig.UI.Bar.Zoom2.load() == 1.0,
				"ReadAll invalid Zoom2 defaults instead of falling back to valid old Zoom");
			scaleConfig.UI.Bar.Zoom2 = 2.0;
			check(scaleConfig.ReadMini({ "UI.Bar.Zoom2" }) && scaleConfig.UI.Bar.Zoom2.load() == 1.0,
				"Mini invalid Zoom2 defaults instead of falling back to valid old Zoom");
		}
		scaleConfig.UI.Bar.Zoom2 = 1.5;
		scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase = 2;
		scaleConfig.ResetToDefaults();
		check(scaleConfig.UI.Bar.Zoom2.load() == 1.0 && scaleConfig.UI.Bar.UseTaskbarHeightAsScaleBase.load() == defaultMode,
			"reset restores both new scale defaults");
	}
	{
		using Inkeys::WhiteboardThemePreference;
		const auto writeThemeDocument=[&](const Json::Value& document){
			std::ofstream output(file,std::ios::binary|std::ios::trunc);Json::StreamWriterBuilder themeWriter;
			output<<Json::writeString(themeWriter,document);check(output.good(),"theme fixture writes isolated main.json");};
		Inkeys::Config themes;const WhiteboardThemePreference defaultTheme{};
		writeThemeDocument(Json::Value(Json::objectValue));
		check(themes.ReadAll()&&themes.Drawing.Whiteboard.DefaultTheme.load()==defaultTheme,"missing theme keeps green blank on an existing configuration");
		for(const auto rgb:{0x123B32u,0x172A46u,0x000000u,0xFFFFFFu,0xE5E7EBu,0xF5F1E6u,0xABCDEFu})
		for(const std::uint32_t pattern:{0u,1u,2u}){
			(void)themes.SetWhiteboardThemeDefault(rgb,pattern);
			check(!themes.SetWhiteboardThemeDefault(rgb,pattern),"unchanged theme preference does not request a write");
			const auto upload=themes.GetUploadInfo();Inkeys::Config frozenTheme;frozenTheme=themes;
			(void)themes.SetWhiteboardThemeDefault(rgb^0xFFFFFF,pattern==2?0:pattern+1);
			check(themes.GetUploadInfo()==upload&&frozenTheme.Drawing.Whiteboard.DefaultTheme.load()==WhiteboardThemePreference{rgb,pattern}
				&&frozenTheme.Write(),"frozen FIFO payload owns both NoUpload theme fields");
			Inkeys::Config restarted;
			check(restarted.ReadAll()&&restarted.Drawing.Whiteboard.DefaultTheme.load()==WhiteboardThemePreference{rgb,pattern},
				"new Config instance reloads the complete last selected background and pattern");
			const auto json=ReadJson(file)["Drawing"]["Whiteboard"]["DefaultTheme"];
			check(json["BackgroundRgb"].isUInt()&&json["BackgroundRgb"].asUInt()==rgb&&json["Pattern"].isUInt()&&json["Pattern"].asUInt()==pattern,
				"theme serializes as two exact RGB and pattern integers");
		}
		Json::Value validTheme(Json::objectValue);validTheme["BackgroundRgb"]=0xFFFFFF;validTheme["Pattern"]=2;
		for(const char* field:{"BackgroundRgb","Pattern"}){
			std::vector<Json::Value> invalids{Json::Value(),Json::Value(true),Json::Value(-1),Json::Value(1.0),Json::Value("1"),
				Json::Value(Json::arrayValue),Json::Value(Json::objectValue),Json::Value(Json::UInt64(0x100000000ULL))};
			invalids.emplace_back(std::string_view(field)=="Pattern"?3:0x1000000);
			for(const auto& invalidTheme:invalids){
				auto document=Json::Value(Json::objectValue);auto theme=validTheme;theme[field]=invalidTheme;
				document["Drawing"]["Whiteboard"]["DefaultTheme"]=theme;writeThemeDocument(document);
				check(themes.ReadAll()&&themes.Drawing.Whiteboard.DefaultTheme.load()==defaultTheme,"invalid theme component defaults the whole pair without narrowing or coercion");
				(void)themes.SetWhiteboardThemeDefault(0xFFFFFF,2);themes.UI.Bar.Zoom2=1.25;
				check(themes.ReadMini({"Drawing.Whiteboard.DefaultTheme"})&&themes.Drawing.Whiteboard.DefaultTheme.load()==defaultTheme
					&&themes.UI.Bar.Zoom2.load()==1.25,"Mini theme validation keeps unrelated settings untouched");
			}
			auto incomplete=validTheme;incomplete.removeMember(field);Json::Value document(Json::objectValue);
			document["Drawing"]["Whiteboard"]["DefaultTheme"]=incomplete;writeThemeDocument(document);
			check(themes.ReadAll()&&themes.Drawing.Whiteboard.DefaultTheme.load()==defaultTheme,"partial theme never combines disk and default components");
		}
		(void)themes.SetWhiteboardThemeDefault(0xFFFFFF,2);
		check(!themes.SetWhiteboardThemeDefault(0x1000000,0)&&!themes.SetWhiteboardThemeDefault(0,3)
			&&themes.Drawing.Whiteboard.DefaultTheme.load()==WhiteboardThemePreference{0xFFFFFF,2},"invalid runtime preference cannot replace the last successful choice");
		themes.ResetToDefaults();check(themes.Drawing.Whiteboard.DefaultTheme.load()==defaultTheme,"reset restores the green blank theme default");
	}
	std::cout << "Config metadata tests: " << failures << " failures\n";
	return failures;
}
