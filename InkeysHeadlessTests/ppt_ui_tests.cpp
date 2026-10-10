#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cmath>
#include <iostream>
#include <string_view>
#include <fstream>
#include "../Inkeys/PptSettingsPersistence.h"

import Inkeys.UI.Ppt;

namespace
{
	using namespace Inkeys::UI::Ppt;

	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL " << name << '\n';
	}

	bool SameRect(const RECT& lhs, const RECT& rhs)
	{
		return lhs.left == rhs.left && lhs.top == rhs.top &&
			lhs.right == rhs.right && lhs.bottom == rhs.bottom;
	}

	void TestLayoutAndDpi()
	{
		constexpr RECT monitor{ -1920, 0, 0, 1080 };
		LayoutConfiguration config;
		config.bottomPairWidth = 100.0F;
		config.middlePairWidth = 80.0F;
		config.showMiddlePair = true;
		const auto bottomLeft = ResolveControlLayout(
			Control::BottomLeft, monitor, config, true);
		const auto bottomRight = ResolveControlLayout(
			Control::BottomRight, monitor, config, true);
		Check(bottomLeft.expanded.left - monitor.left ==
			monitor.right - bottomRight.expanded.right,
			"bottom pair mirrors around monitor center");
		Check(bottomLeft.backing.cx == 165 && bottomLeft.backing.cy == 43,
			"bottom backing uses compact bar dimensions");

		const auto middleLeft = ResolveControlLayout(
			Control::MiddleLeft, monitor, config, false);
		const auto middleRight = ResolveControlLayout(
			Control::MiddleRight, monitor, config, false);
		Check(middleLeft.hidden.right < monitor.left &&
			middleRight.hidden.left > monitor.right,
			"middle controls hide on their respective sides");
		Check(SameRect(bottomLeft.expanded, bottomLeft.hidden),
			"bottom controls fade at their target position");

		const auto scaled = ResolveControlLayout(
			Control::BottomLeft, monitor, config, true, 1.5F);
		Check(scaled.backing.cx == 248 && scaled.backing.cy == 64,
			"DPI changes physical backing size");
		auto movedConfig = config;
		movedConfig.bottomPairWidth += 40.0F;
		const auto moved = ResolveControlLayout(
			Control::BottomLeft, monitor, movedConfig, true);
		Check(moved.backing.cx == bottomLeft.backing.cx &&
			moved.backing.cy == bottomLeft.backing.cy &&
			moved.expanded.left != bottomLeft.expanded.left,
			"position change reuses backing size");
	}

	void TestAnimationAndDrag()
	{
		const float first = AdvanceLegacyValue(0.0F, 15.0F);
		Check(first > 0.0F && first < 15.0F,
			"legacy advancement converges monotonically");
		Check(AdvanceLegacyValue(14.95F, 15.0F) == 15.0F,
			"legacy advancement snaps near target");

		constexpr RECT monitor{ 0, 0, 1000, 800 };
		LayoutConfiguration config;
		config.bottomPairWidth = 10000.0F;
		config.bottomPairHeight = -10.0F;
		auto clamped = ClampPptDrag(Control::BottomLeft, monitor, config);
		Check(clamped.bottomPairWidth == 330.0F && clamped.bottomPairHeight == 0.0F,
			"bottom drag clamps to monitor boundaries");

		config.showBottomPair = true;
		config.showMiddlePair = true;
		config.bottomPairWidth = 0.0F;
		config.bottomPairHeight = 0.0F;
		config.middlePairHeight = 0.0F;
		Check(!PptDragCollides(Control::BottomLeft, monitor, config),
			"separated visible groups do not collide");
		config.middlePairHeight = -312.5F;
		Check(PptDragCollides(Control::BottomLeft, monitor, config),
			"overlapping visible groups collide");
		config.showMiddlePair = false;
		Check(!PptDragCollides(Control::BottomLeft, monitor, config),
			"hidden groups are ignored by collision checks");
	}

	void TestDamageTransactions()
	{
		constexpr SIZE backing{ 100, 60 };
		Check(SameRect(ResolvePptDamage(backing, {}, {}, true),
			RECT{ 0, 0, 100, 60 }), "full damage covers backing");
		Check(SameRect(ResolvePptDamage(backing,
			RECT{ -5, 4, 20, 30 }, RECT{ 15, 10, 120, 50 }, false),
			RECT{ 0, 4, 100, 50 }), "local damage unions and clips visuals");

		const auto active = ResolvePptDiagnosticDamage(backing,
			RECT{ 10, 10, 30, 30 }, {}, false, true, true, false);
		Check(active.drawActive && !active.drawFinal && active.keepFinalFrame &&
			SameRect(active.damage, RECT{ 10, 10, 30, 30 }),
			"active diagnostic draws red local damage");
		const auto final = ResolvePptDiagnosticDamage(backing,
			{}, active.damage, false, true, false, active.keepFinalFrame);
		Check(!final.drawActive && final.drawFinal && !final.keepFinalFrame &&
			SameRect(final.damage, active.damage),
			"idle transition draws one green final damage");
	}

	void TestVisualGeometryAndPageText()
	{
		const auto bottomLeft = ResolveControlVisualGeometry(Control::BottomLeft);
		const auto bottomRight = ResolveControlVisualGeometry(Control::BottomRight);
		const auto middleLeft = ResolveControlVisualGeometry(Control::MiddleLeft);
		Check(bottomLeft.dragHandle.x1 == 10.0F && bottomLeft.dragHandle.x2 == 10.0F &&
			bottomLeft.dragHandle.y1 == 11.25F && bottomLeft.dragHandle.y2 == 31.25F,
			"bottom-left uses compact vertical drag handle");
		Check(bottomRight.dragHandle.x1 == 155.0F &&
			bottomRight.dragHandle.x2 == 155.0F,
			"bottom-right drag handle mirrors at trailing edge");
		Check(middleLeft.dragHandle.y1 == 10.0F && middleLeft.dragHandle.y2 == 10.0F &&
			middleLeft.dragHandle.x1 == 11.25F && middleLeft.dragHandle.x2 == 31.25F,
			"middle controls use compact horizontal drag handle");

		Check(middleLeft.previous.top == 15.0F && middleLeft.previous.bottom == 47.5F &&
			middleLeft.next.top == 127.5F && middleLeft.next.bottom == 160.0F,
			"middle buttons keep balanced outer spacing");
		Check(middleLeft.currentPage.top == 52.5F &&
			middleLeft.currentPage.bottom == 87.5F &&
			middleLeft.totalPage.top == 87.5F &&
			middleLeft.totalPage.bottom == 122.5F,
			"middle page text uses compact centered bounds");
		Check(IsInPageHitArea(Control::BottomLeft, 52.5F, 0.0F) &&
			IsInPageHitArea(Control::BottomLeft, 122.5F, 42.5F) &&
			!IsInPageHitArea(Control::BottomLeft, 52.0F, 30.0F),
			"bottom page hit area keeps full control height");
		Check(IsInPageHitArea(Control::MiddleLeft, 0.0F, 52.5F) &&
			IsInPageHitArea(Control::MiddleLeft, 42.5F, 122.5F) &&
			!IsInPageHitArea(Control::MiddleLeft, 30.0F, 123.0F),
			"middle page hit area keeps full control width");

		const auto unknown = ResolvePageText(Control::BottomLeft, -1, -1);
		Check(unknown.current == L"-" && unknown.total == L"/-",
			"unknown page values use placeholders");
		const auto bottomLimit = ResolvePageText(Control::BottomRight, 12345, 56789);
		Check(bottomLimit.current == L"9999" && bottomLimit.total == L"/9999",
			"bottom page values retain four-digit cap");
		const auto middleLimit = ResolvePageText(Control::MiddleRight, 1234, 5678);
		Check(middleLimit.current == L"999" && middleLimit.total == L"/999",
			"middle page values retain three-digit cap");
	}

	void TestPageStatePublication()
	{
		const auto ready = ResolvePageStateForPublication(7, 12, 7, 12);
		Check(ready.currentPage == 7 && ready.totalPage == 12,
			"valid COM page publishes the Draw3-ready state");

		const auto endPage = ResolvePageStateForPublication(-1, -1, -1, 12);
		Check(endPage.currentPage == -1 && endPage.totalPage == 12,
			"end page preserves the observed total only at the UI boundary");

		const auto unknown = ResolvePageStateForPublication(7, 12, -1, -1);
		const auto zeroPage = ResolvePageStateForPublication(7, 12, 0, 12);
		Check(unknown.currentPage == -1 && unknown.totalPage == -1
			&& zeroPage.currentPage == -1 && zeroPage.totalPage == -1,
			"unknown and zero-page COM states do not publish EndShow");

		const auto recoveryNotReady = ResolvePageStateForPublication(
			-1, -1, 8, 12);
		Check(recoveryNotReady.currentPage == -1
			&& recoveryNotReady.totalPage == -1,
			"valid COM recovery waits for Draw3-ready page data");

		const auto recoveryReady = ResolvePageStateForPublication(8, 12, 8, 12);
		Check(recoveryReady.currentPage == 8 && recoveryReady.totalPage == 12,
			"Draw3-ready recovery publishes the valid page tuple");
	}

	void TestDisplayReflowAndTransition()
	{
		const RECT monitor{ -800, 0, 0, 600 };
		LayoutConfiguration config;
		config.bottomPairWidth = 10000.0F;
		config.bottomPairHeight = -100.0F;
		config.middlePairWidth = 10000.0F;
		config.middlePairHeight = 10000.0F;
		config.showMiddlePair = true;
		config.bottomPairScale = 2.0F;
		config.middlePairScale = 2.0F;
		const auto runtime = ResolveRuntimeLayoutConfiguration(
			monitor, config, 1.5F);
		for (const auto control : { Control::BottomLeft, Control::BottomRight,
			Control::MiddleLeft, Control::MiddleRight })
		{
			const auto layout = ResolveControlLayout(control, monitor,
				runtime.configuration, true, runtime.dpiScale);
			Check(layout.expanded.left >= monitor.left &&
				layout.expanded.top >= monitor.top &&
				layout.expanded.right <= monitor.right &&
				layout.expanded.bottom <= monitor.bottom,
				"display reflow keeps enabled PPT control on screen");
		}
		Check(EasePptDisplayTransition(0.25F) == 0.0625F &&
			EasePptDisplayTransition(0.5F) == 0.5F,
			"PPT display transition uses ease-in-out cubic");
		const float midpoint = InterpolatePptDisplayValue(100.0F, 300.0F, 0.5F);
		Check(midpoint == 200.0F &&
			InterpolatePptDisplayValue(midpoint, 400.0F, 0.0F) == midpoint,
			"PPT display retarget starts from current rendered value");

		const RECT tinyMonitor{ -180, -60, 0, 60 };
		LayoutConfiguration extreme;
		extreme.bottomPairWidth = 10000.0F;
		extreme.bottomPairHeight = 10000.0F;
		extreme.middlePairWidth = 10000.0F;
		extreme.middlePairHeight = 10000.0F;
		extreme.bottomPairScale = 3.0F;
		extreme.middlePairScale = 3.0F;
		extreme.showMiddlePair = true;
		const auto original = extreme;
		const auto fitted = ResolveRuntimeLayoutConfiguration(
			tinyMonitor, extreme, 1.5F);
		for (const auto control : { Control::BottomLeft, Control::BottomRight,
			Control::MiddleLeft, Control::MiddleRight })
		{
			const auto layout = ResolveControlLayout(control, tinyMonitor,
				fitted.configuration, true, fitted.dpiScale);
			Check(layout.expanded.left >= tinyMonitor.left &&
				layout.expanded.top >= tinyMonitor.top &&
				layout.expanded.right <= tinyMonitor.right &&
				layout.expanded.bottom <= tinyMonitor.bottom,
				"extreme monitor uses runtime-only group fitting");
		}
		Check(!PptDragCollides(Control::MiddleLeft, tinyMonitor,
				fitted.configuration, fitted.dpiScale),
			"lower-priority side group is corrected after collisions");
		Check(extreme.bottomPairWidth == original.bottomPairWidth &&
			extreme.middlePairScale == original.middlePairScale,
			"runtime correction does not mutate persisted configuration input");
	}
	void TestPositionSessionsAndPersistence()
	{
		using namespace Inkeys::PptSettings;
		PositionState state;
		WriteJournal journal;
		Json::Value original;
		original["MemoryWidgetPosition"] = true;
		SetPositions(original, { 0, 400, 0, 0 });
		journal.Initialize(original);
		auto SavedLayout = [&]
		{
			const auto saved = journal.Saved();
			LayoutConfiguration layout;
			layout.bottomPairWidth = saved.bottomX;
			layout.bottomPairHeight = saved.bottomY;
			layout.middlePairWidth = saved.middleX;
			layout.middlePairHeight = saved.middleY;
			return layout;
		};
		auto Save = [&]
		{
			const auto& layout = state.configuration;
			return journal.CapturePositions({ layout.bottomPairWidth, layout.bottomPairHeight,
				layout.middlePairWidth, layout.middlePairHeight }, layout.rememberPosition);
		};
		int writes = 0;
		auto Success = [&](const std::string& content)
		{
			++writes;
			Check(content.find("_InkeysWriteRevision") == std::string::npos,
				"queue revision is not persisted as a settings key");
			return true;
		};
		Check(state.BeginSession(1, SavedLayout()), "new show restores the saved baseline");
		Check(state.CommitPair(1, state.epoch, 0, 10, 20, 500)
			&& journal.Saved().bottomY == 400 && writes == 0,
			"drag commits current position without writing or changing saved baseline");
		const auto epoch = state.epoch;
		Check(state.CommitPair(1, epoch, 1, 11, 30, 70)
			&& !state.CommitPair(1, epoch, 0, 9, 99, 99)
			&& state.configuration.bottomPairHeight == 500,
			"interleaved pairs and stale commits cannot overwrite the other pair");
		const auto oldSettings = journal.CaptureSettings(original);
		Check(state.SetRemember(false), "turning memory off is a save edge");
		const auto turnOff = Save();
		Check(journal.Commit(oldSettings, Success) && writes == 0,
			"late settings JSON is superseded by the newer position save");
		Check(!journal.Commit(turnOff, [](const std::string&) { return false; })
			&& journal.Saved().bottomY == 400 && journal.Retry() == turnOff,
			"failed write keeps successful disk baseline and the exact frozen retry payload");
		Check(state.CommitPair(1, epoch, 0, 12, 0, 0), "memory off allows further runtime dragging");
		Check(journal.Commit(journal.Retry(), Success) && journal.Saved().bottomY == 500
			&& journal.Saved().middleY == 70,
			"retry saves toggle-time position instead of collecting later temporary drag");
		Check(!state.EndSession(1), "ending a memory-off show does not request a save");
		Check(state.BeginSession(2, SavedLayout()) && state.configuration.bottomPairHeight == 500,
			"reentry restores the top position saved when memory was disabled");
		Check(state.SetRemember(true) && journal.Commit(Save(), Success),
			"turning memory on also saves the current position");
		Check(state.CommitPair(2, state.epoch, 0, 20, 80, 120), "second show accepts its own commit");
		const auto beforeEnd = Save();
		Check(state.EndSession(2) && journal.Commit(beforeEnd, Success)
			&& journal.Saved().bottomY == 120 && !state.EndSession(2),
			"remembered show end saves once and duplicate lifecycle edges are inert");
		Check(state.BeginSession(3, SavedLayout()), "third show enters");
		const auto beforeResetEpoch = state.epoch;
		(void)state.SetRemember(false);
		state.RestorePositions({});
		Check(!state.CommitPair(3, beforeResetEpoch, 1, 100, 40, 60)
			&& journal.Commit(Save(), Success) && journal.Saved().bottomY == 0,
			"reset saves defaults with memory off and rejects the old gesture epoch");
		Check(!state.CommitPair(2, state.epoch, 0, 200, 50, 90),
			"late old-session commit cannot mutate the current show");

		// 模拟写盘途中又有新请求：旧成功只推进真实磁盘基线，不清除新请求。
		const auto older = journal.CapturePositions({ 1, 2, 3, 4 }, false);
		WriteJournal::PreparedWrite prepared;
		Check(journal.Prepare(older, prepared), "prepare old immutable write");
		const auto newer = journal.CapturePositions({ 5, 6, 7, 8 }, false);
		journal.Complete(prepared, true);
		Check(journal.Saved().bottomY == 2
			&& !journal.Commit(newer, [](const std::string&) { return false; })
			&& journal.Retry() == newer,
			"older completion preserves the newer failed save for retry");
	}

	void TestRapidReentryBeforeSaveCompletion()
	{
		using namespace Inkeys::PptSettings;
		WriteJournal journal;
		Json::Value disk;
		SetPositions(disk, { 0, 100, 0, 0 });
		journal.Initialize(disk);
		PositionState state;
		auto Enter = [&](std::uint64_t session)
		{
			const auto frozen = journal.RestoreBaseline();
			LayoutConfiguration restore;
			restore.bottomPairHeight = frozen.bottomY;
			return state.BeginSession(session, restore);
		};
		Check(Enter(1) && state.CommitPair(1, state.epoch, 0, 10, 0, 500),
			"rapid reentry setup accepts the current runtime position");
		const auto ending = journal.CapturePositions({ 0, 500, 0, 0 }, true);
		Check(state.EndSession(1) && Enter(2) && state.configuration.bottomPairHeight == 500
			&& journal.Saved().bottomY == 100 && journal.SavedRevision() == 0,
			"rapid remembered reentry uses the frozen requested baseline without claiming disk success");
		Check(state.SetRemember(false), "rapid reentry can turn memory off");
		const auto disabled = journal.CapturePositions({ 0, 500, 0, 0 }, false);
		Check(state.CommitPair(2, state.epoch, 0, 20, 0, 0) && !state.EndSession(2)
			&& Enter(3) && state.configuration.bottomPairHeight == 500,
			"immediate memory-off reentry preserves toggle-time top position while queue is delayed");
		Check(!journal.Commit(disabled, [](const std::string&) { return false; })
			&& journal.RestoreBaseline().bottomY == 500 && journal.Saved().bottomY == 100
			&& journal.Retry() == disabled,
			"failed delayed save retains usable in-process baseline and exact retry distinct from disk");
		Check(journal.Commit(ending, [](const std::string&) { return false; })
			&& journal.Retry() == disabled, "old ending save cannot displace newer failed toggle request");
		WriteJournal restarted;
		restarted.Initialize(disk);
		Check(restarted.RestoreBaseline().bottomY == 100,
			"a new process restores actual disk data and never invents success of a failed prior save");
	}

	void TestAtomicPositionFile()
	{
		using namespace Inkeys::PptSettings;
		wchar_t temporary[MAX_PATH]{};
		Check(GetTempPathW(MAX_PATH, temporary) != 0, "temporary directory available");
		const auto directory = std::filesystem::path(temporary)
			/ (L"Inkeys-ppt-settings-" + std::to_wstring(GetCurrentProcessId()));
		const auto path = directory / L"positions.json";
		Check(WriteAtomically(path, "old baseline"), "atomic writer creates initial file");
		const HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		Check(held != INVALID_HANDLE_VALUE, "test locks old destination against replacement");
		Check(!WriteAtomically(path, "new baseline"), "locked destination makes replacement fail");
		if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
		{
			std::ifstream file(path, std::ios::binary);
			const std::string actual((std::istreambuf_iterator<char>(file)), {});
			Check(actual == "old baseline", "failed atomic replacement preserves old bytes");
		}
		Check(WriteAtomically(path, "new baseline"), "same file can be retried after failure");
		{
			std::ifstream file(path, std::ios::binary);
			const std::string actual((std::istreambuf_iterator<char>(file)), {});
			Check(actual == "new baseline", "successful retry changes complete file contents");
		}
		std::error_code error;
		std::filesystem::remove(path, error);
		std::filesystem::remove(directory, error);
	}

}

int RunPptUiTests()
{
	TestPositionSessionsAndPersistence();
	TestRapidReentryBeforeSaveCompletion();
	TestAtomicPositionFile();
	TestLayoutAndDpi();
	TestAnimationAndDrag();
	TestDamageTransactions();
	TestVisualGeometryAndPageText();
	TestPageStatePublication();
	TestDisplayReflowAndTransition();
	return failureCount;
}
