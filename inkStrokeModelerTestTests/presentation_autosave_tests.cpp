#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Presentation.h"

#include <filesystem>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

import Inkeys.Drawing.Draw3.presentation_auto_save;

namespace
{
	namespace fs = std::filesystem;
	using namespace Inkeys::Drawing::Draw3;
	using namespace draw3::uink;

	struct TestState
	{
		int failures = 0;
	};

#define PRESENTATION_CHECK(state, expression) \
	do { if (!(expression)) { ++(state).failures; \
	std::cerr << "[PresentationAutoSave] failed: " #expression << '\n'; } } while (false)

	Bridge::PresentationTarget MakeTarget()
	{
		Bridge::PresentationTarget target;
		for (std::size_t index = 0; index < target.key.bytes.size(); ++index)
			target.key.bytes[index] = static_cast<std::uint8_t>(index + 1);
		target.bindingMode = Bridge::SlideBindingMode::StableSlideId;
		target.sourceIdentity = "path:c:\\lessons\\deck.pptx";
		target.presentationName = "deck.pptx";
		target.provider = "PowerPoint";
		target.bindingToken = "PowerPoint:12:34:1";
		target.slideIds = { 101 };
		target.slideId = 101;
		target.totalPages = 1;
		target.bindingRevision = 1;
		target.targetRevision = 1;
		return target;
	}

	Bridge::PresentationTarget MakeFallbackTarget()
	{
		Bridge::PresentationTarget target = MakeTarget();
		target.key.bytes[0] = 0x55;
		target.bindingMode = Bridge::SlideBindingMode::PageIndexFallback;
		target.sourceIdentity = "path:c:\\lessons\\fallback.pptx";
		target.presentationName = "fallback.pptx";
		target.slideIds.clear();
		target.slideId.reset();
		return target;
	}

	Bridge::PresentationTarget MakeSecondTarget()
	{
		Bridge::PresentationTarget target = MakeTarget();
		target.key.bytes[0] = 0x77;
		target.sourceIdentity = "path:c:\\lessons\\second.pptx";
		target.presentationName = "second.pptx";
		return target;
	}

	Draw3UInkExportSnapshot MakeSnapshot(const Bridge::PresentationTarget& target,
		bool withContent)
	{
		Draw3UInkExportSnapshot snapshot;
		snapshot.fileGuid = *ParseUInkGuid("11111111-1111-4111-8111-111111111111");
		snapshot.workspaceGuid = *ParseUInkGuid("22222222-2222-4222-8222-222222222222");
		snapshot.workspaceName = target.presentationName;
		const bool stable = target.bindingMode ==
			Bridge::SlideBindingMode::StableSlideId;
		const auto mode = stable ? Draw3UInkImportBindingMode::StableSlideId
			: Draw3UInkImportBindingMode::PageIndexFallback;
		snapshot.workspaceType = stable ? 2 : kInkeysPageIndexWorkspaceType;
		snapshot.hostId = FormatPresentationKey(target.key);
		snapshot.currentPageIndex = 0;
		snapshot.workspaceExtra = MakeInkeysBindingExtra(mode);
		snapshot.assignedIndependentUndoGroups = true;
		Draw3UInkCanvasSnapshot canvas;
		canvas.pageGuid = *ParseUInkGuid("33333333-3333-4333-8333-333333333333");
		canvas.pageNumber = 1;
		if (stable) canvas.slideId = 101;
		canvas.extra = MakeInkeysBindingExtra(mode);
		if (withContent)
		{
			Draw3UInkStrokeSnapshot stroke;
			stroke.style.kind = Draw3UInkStrokeKind::Pen;
			stroke.style.fallbackRgb = 0x123456;
			stroke.points = { { 10.0f, 20.0f, 4.0f }, { 30.0f, 40.0f, 4.0f } };
			canvas.strokes.push_back(std::move(stroke));
		}
		snapshot.canvases.push_back(std::move(canvas));
		return snapshot;
	}

	Draw3UInkExportSnapshot MakeSecondSnapshot(
		const Bridge::PresentationTarget& target, bool withContent)
	{
		auto snapshot = MakeSnapshot(target, withContent);
		snapshot.fileGuid = *ParseUInkGuid("44444444-4444-4444-8444-444444444444");
		snapshot.workspaceGuid = *ParseUInkGuid("55555555-5555-4555-8555-555555555555");
		snapshot.canvases.front().pageGuid =
			*ParseUInkGuid("66666666-6666-4666-8666-666666666666");
		return snapshot;
	}

	PresentationPersistenceCompletion TakeCompletion(
		PresentationAutoSaveService& service)
	{
		PresentationPersistenceCompletion result;
		PresentationPersistenceCompletion current;
		while (service.TryTakeCompletion(current)) result = std::move(current);
		return result;
	}

	fs::path MakeRoot()
	{
		wchar_t temporary[MAX_PATH] = {};
		GetTempPathW(MAX_PATH, temporary);
		const auto guid = CreateUInkGuid();
		const std::string text = FormatUInkGuid(*guid);
		return fs::path(temporary) /
			(L"InkeysPresentationAutoSave_" + std::wstring(text.begin(), text.end()));
	}

	std::size_t CountUInkFiles(const fs::path& root)
	{
		std::size_t count = 0;
		std::error_code error;
		const fs::path files = root / L"presentation" / L"files";
		if (!fs::is_directory(files, error)) return 0;
		for (const auto& entry : fs::directory_iterator(files, error))
			if (entry.is_regular_file() && entry.path().extension() == L".uink") ++count;
		return count;
	}

	void TestSaveLoadAndClearOverwrite(TestState& state)
	{
		const fs::path root = MakeRoot();
		const Bridge::PresentationTarget target = MakeTarget();
		PresentationAutoSaveService service;
		SetPresentationAutoSaveTestFaultInjection({ false, 0,
			"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa" });
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		PRESENTATION_CHECK(state, CountUInkFiles(root) == 1);

		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		auto loaded = TakeCompletion(service);
		PRESENTATION_CHECK(state, loaded.status == PresentationPersistenceStatus::Loaded);
		PRESENTATION_CHECK(state, loaded.loadedSnapshot &&
			loaded.loadedSnapshot->canvases.front().strokes.size() == 1);

		// 恢复后清空会推进 mutation；即使当前无内容，也覆盖同一逻辑文件。
		PRESENTATION_CHECK(state, ShouldQueuePresentationSave(2, 1));
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 2,
			MakeSnapshot(target, false) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		PRESENTATION_CHECK(state, CountUInkFiles(root) == 1);

		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		loaded = TakeCompletion(service);
		PRESENTATION_CHECK(state, loaded.status == PresentationPersistenceStatus::Loaded);
		PRESENTATION_CHECK(state, loaded.loadedSnapshot &&
			loaded.loadedSnapshot->canvases.front().strokes.empty());
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestIndexFailureRecoveryAndForeignConflict(TestState& state)
	{
		const fs::path root = MakeRoot();
		const Bridge::PresentationTarget target = MakeTarget();
		PresentationAutoSaveService service;
		SetPresentationAutoSaveTestFaultInjection({ true, 0,
			"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb" });
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::IoError);
		PRESENTATION_CHECK(state, CountUInkFiles(root) == 1);

		SetPresentationAutoSaveTestFaultInjection({ false, 0,
			"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb" });
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto firstPendingLoaded = TakeCompletion(service);
		PRESENTATION_CHECK(state, firstPendingLoaded.status ==
			PresentationPersistenceStatus::Loaded && firstPendingLoaded.loadedSnapshot &&
			firstPendingLoaded.loadedSnapshot->canvases.front().strokes.size() == 1);
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 2,
			MakeSnapshot(target, false) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);

		// 文件覆盖成功而 index 发布失败后，下一次仍能从 self-written revision 收敛。
		SetPresentationAutoSaveTestFaultInjection({ true, 0,
			"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb" });
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 3,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::IoError);
		SetPresentationAutoSaveTestFaultInjection({ false, 0,
			"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb" });
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto pendingLoaded = TakeCompletion(service);
		PRESENTATION_CHECK(state, pendingLoaded.status ==
			PresentationPersistenceStatus::Loaded && pendingLoaded.loadedSnapshot &&
			pendingLoaded.loadedSnapshot->canvases.front().strokes.size() == 1);
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 4,
			MakeSnapshot(target, false) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);

		PresentationAutoSaveService foreign;
		SetPresentationAutoSaveTestFaultInjection({ false, 0,
			"cccccccc-cccc-4ccc-8ccc-cccccccccccc" });
		PRESENTATION_CHECK(state, foreign.Start(root.wstring()));
		PRESENTATION_CHECK(state, foreign.SubmitSave({ target, 3,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		foreign.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(foreign).status ==
			PresentationPersistenceStatus::CrossProcessConflictDeferred);
		PRESENTATION_CHECK(state, CountUInkFiles(root) == 1);
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestCorruptIndexFallbackAndIsolation(TestState& state)
	{
		const fs::path root = MakeRoot();
		const Bridge::PresentationTarget target = MakeTarget();
		PresentationAutoSaveService service;
		SetPresentationAutoSaveTestFaultInjection({ false, 0,
			"bcbcbcbc-bcbc-4bcb-8bcb-bcbcbcbcbcbc" });
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);

		const fs::path presentationRoot = root / L"presentation";
		const fs::path index = presentationRoot / L"index.json";
		const fs::path backup = presentationRoot / L"index.json.bak";
		const fs::path uink = presentationRoot / L"files" /
			L"11111111-1111-4111-8111-111111111111.uink";
		auto readBytes = [](const fs::path& path) -> std::optional<std::string>
			{
				std::ifstream stream(path, std::ios::binary);
				if (!stream) return std::nullopt;
				return std::string(std::istreambuf_iterator<char>(stream),
					std::istreambuf_iterator<char>());
			};
		const auto originalUInk = readBytes(uink);
		PRESENTATION_CHECK(state, originalUInk && !originalUInk->empty());
		std::error_code error;
		PRESENTATION_CHECK(state, fs::copy_file(index, backup,
			fs::copy_options::overwrite_existing, error));
		{
			std::ofstream corrupt(index, std::ios::binary | std::ios::trunc);
			corrupt << "{invalid-primary";
			PRESENTATION_CHECK(state, corrupt.good());
		}
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto backupLoaded = TakeCompletion(service);
		PRESENTATION_CHECK(state, backupLoaded.status ==
			PresentationPersistenceStatus::Loaded && backupLoaded.loadedSnapshot &&
			backupLoaded.loadedSnapshot->canvases.front().strokes.size() == 1);

		{
			std::ofstream corrupt(backup, std::ios::binary | std::ios::trunc);
			corrupt << "{invalid-backup";
			PRESENTATION_CHECK(state, corrupt.good());
		}
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::IoError);
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 2,
			MakeSnapshot(target, false) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::IoError);
		PRESENTATION_CHECK(state, CountUInkFiles(root) == 1);
		PRESENTATION_CHECK(state, originalUInk && readBytes(uink) == originalUInk);
		fs::remove_all(root, error);
		error.clear();
		PRESENTATION_CHECK(state, !fs::exists(root, error) && !error);
	}

	void TestRootChangeDropsPendingIndexState(TestState& state)
	{
		const fs::path firstRoot = MakeRoot();
		const fs::path secondRoot = MakeRoot();
		const Bridge::PresentationTarget target = MakeTarget();
		constexpr const char* session = "abababab-abab-4bab-8bab-abababababab";

		PresentationAutoSaveService seed;
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PRESENTATION_CHECK(state, seed.Start(secondRoot.wstring()));
		PRESENTATION_CHECK(state, seed.SubmitSave({ target, 1,
			MakeSnapshot(target, false) }) == PresentationPersistenceSubmitStatus::Accepted);
		seed.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(seed).status ==
			PresentationPersistenceStatus::Committed);

		PresentationAutoSaveService service;
		PRESENTATION_CHECK(state, service.Start(firstRoot.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 1,
			MakeSnapshot(target, false) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		SetPresentationAutoSaveTestFaultInjection({ true, 0, session });
		PRESENTATION_CHECK(state, service.Start(firstRoot.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 2,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::IoError);

		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PRESENTATION_CHECK(state, service.Start(secondRoot.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto loaded = TakeCompletion(service);
		PRESENTATION_CHECK(state, loaded.status == PresentationPersistenceStatus::Loaded &&
			loaded.loadedSnapshot && loaded.loadedSnapshot->canvases.front().strokes.empty());

		std::error_code error;
		fs::remove_all(firstRoot, error);
		fs::remove_all(secondRoot, error);
	}

	void TestPageIndexFallbackOverwrite(TestState& state)
	{
		const fs::path root = MakeRoot();
		const Bridge::PresentationTarget target = MakeFallbackTarget();
		PresentationAutoSaveService service;
		SetPresentationAutoSaveTestFaultInjection({ false, 0,
			"dddddddd-dddd-4ddd-8ddd-dddddddddddd" });
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 2,
			MakeSnapshot(target, false) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto loaded = TakeCompletion(service);
		PRESENTATION_CHECK(state, loaded.status == PresentationPersistenceStatus::Loaded);
		PRESENTATION_CHECK(state, loaded.loadedSnapshot &&
			loaded.loadedSnapshot->workspaceType == kInkeysPageIndexWorkspaceType &&
			!loaded.loadedSnapshot->canvases.front().slideId &&
			loaded.loadedSnapshot->canvases.front().strokes.empty());

		Bridge::PresentationTarget otherBinding = target;
		otherBinding.bindingRevision = 2;
		otherBinding.bindingToken += ":new-window";
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ otherBinding }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Loaded);

		// 同一路径文稿重开放映后取得完整 SlideID，仍按 ordinal 原位升级同一逻辑文件。
		Bridge::PresentationTarget stable = otherBinding;
		stable.bindingMode = Bridge::SlideBindingMode::StableSlideId;
		stable.slideIds = { 101 };
		stable.slideId = 101;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ stable }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Loaded);
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ stable, 3,
			MakeSnapshot(stable, false) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		PRESENTATION_CHECK(state, CountUInkFiles(root) == 1);
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ stable }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto upgraded = TakeCompletion(service);
		PRESENTATION_CHECK(state, upgraded.status ==
			PresentationPersistenceStatus::Loaded && upgraded.loadedSnapshot &&
			upgraded.loadedSnapshot->workspaceType == 2 &&
			upgraded.loadedSnapshot->canvases.front().slideId == 101);

		const fs::path localRoot = MakeRoot();
		Bridge::PresentationTarget processLocal = target;
		processLocal.key.bytes[0] ^= 0x40;
		processLocal.sourceIdentity = "process:current:wps:12:34:1:fallback";
		processLocal.processLocalIdentity = true;
		PRESENTATION_CHECK(state, service.Start(localRoot.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ processLocal, 1,
			MakeSnapshot(processLocal, false) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		Bridge::PresentationTarget otherLocalBinding = processLocal;
		otherLocalBinding.bindingRevision += 1;
		otherLocalBinding.bindingToken += ":new-window";
		PRESENTATION_CHECK(state, service.Start(localRoot.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ otherLocalBinding }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::CrossProcessConflictDeferred);
		std::error_code error;
		fs::remove_all(root, error);
		fs::remove_all(localRoot, error);
	}

	void TestRestartDropsStaleCompletions(TestState& state)
	{
		const fs::path root = MakeRoot();
		const Bridge::PresentationTarget target = MakeTarget();
		PresentationAutoSaveService service;
		SetPresentationAutoSaveTestFaultInjection({ false, 0,
			"eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee" });
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		// 模拟 Host 未消费上代回调就重启；Start 必须先丢弃旧 completion。
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		int completionCount = 0;
		PresentationPersistenceCompletion completion;
		while (service.TryTakeCompletion(completion))
		{
			++completionCount;
			PRESENTATION_CHECK(state, completion.operation ==
				PresentationPersistenceOperation::Load &&
				completion.status == PresentationPersistenceStatus::Loaded);
		}
		PRESENTATION_CHECK(state, completionCount == 1);
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestMultiplePresentationsStayIsolated(TestState& state)
	{
		const fs::path root = MakeRoot();
		const auto first = MakeTarget();
		const auto second = MakeSecondTarget();
		PresentationAutoSaveService service;
		SetPresentationAutoSaveTestFaultInjection({ false, 0,
			"ffffffff-ffff-4fff-8fff-ffffffffffff" });
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ first, 1,
			MakeSnapshot(first, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		PRESENTATION_CHECK(state, service.SubmitSave({ second, 1,
			MakeSecondSnapshot(second, false) }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		int committed = 0;
		PresentationPersistenceCompletion completion;
		while (service.TryTakeCompletion(completion))
			if (completion.status == PresentationPersistenceStatus::Committed) ++committed;
		PRESENTATION_CHECK(state, committed == 2 && CountUInkFiles(root) == 2);

		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ second }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		PRESENTATION_CHECK(state, service.SubmitLoad({ first }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		bool sawFirstInk = false;
		bool sawSecondEmpty = false;
		while (service.TryTakeCompletion(completion))
		{
			if (!completion.loadedSnapshot) continue;
			if (completion.target.key == first.key)
				sawFirstInk = completion.loadedSnapshot->canvases.front().strokes.size() == 1;
			if (completion.target.key == second.key)
				sawSecondEmpty = completion.loadedSnapshot->canvases.front().strokes.empty();
		}
		PRESENTATION_CHECK(state, sawFirstInk && sawSecondEmpty);
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestLatestWinsPendingSave(TestState& state)
	{
		const fs::path root = MakeRoot();
		const auto target = MakeTarget();
		PresentationAutoSaveService service;
		SetPresentationAutoSaveTestFaultInjection({ false, 50,
			"99999999-9999-4999-8999-999999999999" });
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		const auto second = service.SubmitSave({ target, 2,
			MakeSnapshot(target, false) });
		const auto third = service.SubmitSave({ target, 3,
			MakeSnapshot(target, true) });
		PRESENTATION_CHECK(state, second == PresentationPersistenceSubmitStatus::Accepted ||
			second == PresentationPersistenceSubmitStatus::ReplacedPending);
		PRESENTATION_CHECK(state, third == PresentationPersistenceSubmitStatus::Accepted ||
			third == PresentationPersistenceSubmitStatus::ReplacedPending);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, service.Diagnostics().replacedPending >= 1);
		SetPresentationAutoSaveTestFaultInjection({ false, 0,
			"99999999-9999-4999-8999-999999999999" });
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto loaded = TakeCompletion(service);
		PRESENTATION_CHECK(state, loaded.status == PresentationPersistenceStatus::Loaded &&
			loaded.mutationRevision == 3 && loaded.loadedSnapshot &&
			loaded.loadedSnapshot->canvases.front().strokes.size() == 1);
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestConcurrentWritersShareCaseFoldedMutex(TestState& state)
	{
		const fs::path root = MakeRoot();
		std::wstring alternateRoot = root.wstring();
		if (!alternateRoot.empty() && alternateRoot[0] >= L'A' &&
			alternateRoot[0] <= L'Z')
			alternateRoot[0] = static_cast<wchar_t>(alternateRoot[0] - L'A' + L'a');
		const auto target = MakeTarget();
		PresentationAutoSaveService first;
		PresentationAutoSaveService second;
		SetPresentationAutoSaveTestFaultInjection({ false, 20,
			"88888888-8888-4888-8888-888888888888" });
		PRESENTATION_CHECK(state, first.Start(root.wstring()));
		PRESENTATION_CHECK(state, second.Start(alternateRoot));
		PRESENTATION_CHECK(state, first.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		PRESENTATION_CHECK(state, second.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		first.CloseAndDrain();
		second.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(first).status ==
			PresentationPersistenceStatus::Committed);
		PRESENTATION_CHECK(state, TakeCompletion(second).status ==
			PresentationPersistenceStatus::Committed);
		PRESENTATION_CHECK(state, CountUInkFiles(root) == 1);
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestStrictPresentationImporter(TestState& state)
	{
		const auto target = MakeTarget();
		const auto snapshot = MakeSnapshot(target, true);
		const auto exported = ExportDraw3SnapshotToUInk(snapshot);
		PRESENTATION_CHECK(state, exported.document.has_value());
		if (!exported.document) return;
		Draw3UInkImportExpectation expectation;
		expectation.fileGuid = snapshot.fileGuid;
		expectation.hostId = FormatPresentationKey(target.key);
		expectation.bindingMode = Draw3UInkImportBindingMode::StableSlideId;
		expectation.slideIds = target.slideIds;
		expectation.pageCount = target.totalPages;
		PRESENTATION_CHECK(state, ImportApplicationOwnedPresentation(
			*exported.document, expectation).status == Draw3UInkImportStatus::Success);

		Bridge::PresentationTarget fourPageTarget = target;
		fourPageTarget.slideIds = { 101, 202, 303, 404 };
		fourPageTarget.slideId = 202;
		fourPageTarget.pageIndex = 1;
		fourPageTarget.totalPages = 4;
		auto fourPageSnapshot = MakeSnapshot(fourPageTarget, false);
		fourPageSnapshot.currentPageIndex = 1;
		fourPageSnapshot.canvases.clear();
		const UInkGuid pageGuids[] = {
			*ParseUInkGuid("33333333-3333-4333-8333-333333333331"),
			*ParseUInkGuid("33333333-3333-4333-8333-333333333332"),
			*ParseUInkGuid("33333333-3333-4333-8333-333333333333"),
			*ParseUInkGuid("33333333-3333-4333-8333-333333333334")
		};
		for (std::size_t pageIndex = 0; pageIndex < fourPageTarget.slideIds.size();
			++pageIndex)
		{
			Draw3UInkCanvasSnapshot canvas;
			canvas.pageGuid = pageGuids[pageIndex];
			canvas.pageIndex = static_cast<std::uint32_t>(pageIndex);
			canvas.pageNumber = static_cast<std::uint32_t>(pageIndex + 1);
			canvas.slideId = fourPageTarget.slideIds[pageIndex];
			canvas.extra = MakeInkeysBindingExtra(
				Draw3UInkImportBindingMode::StableSlideId);
			Draw3UInkStrokeSnapshot stroke;
			stroke.style.kind = Draw3UInkStrokeKind::Pen;
			stroke.style.fallbackRgb = static_cast<std::uint32_t>(*canvas.slideId);
			stroke.points = { { 10.0f, 20.0f, 4.0f }, { 30.0f, 40.0f, 4.0f } };
			canvas.strokes.push_back(std::move(stroke));
			fourPageSnapshot.canvases.push_back(std::move(canvas));
		}
		const auto fourPageExport = ExportDraw3SnapshotToUInk(fourPageSnapshot);
		PRESENTATION_CHECK(state, fourPageExport.document.has_value());
		if (fourPageExport.document)
		{
			Draw3UInkImportExpectation reorderedExpectation;
			reorderedExpectation.fileGuid = fourPageSnapshot.fileGuid;
			reorderedExpectation.hostId = FormatPresentationKey(fourPageTarget.key);
			reorderedExpectation.bindingMode =
				Draw3UInkImportBindingMode::StableSlideId;
			reorderedExpectation.slideIds = { 101, 303, 202, 404 };
			reorderedExpectation.knownSlideIds = fourPageTarget.slideIds;
			reorderedExpectation.pageCount = 4;
			const auto reordered = ImportApplicationOwnedPresentation(
				*fourPageExport.document, reorderedExpectation);
			bool followsSlideIds = reordered.snapshot &&
				reordered.snapshot->activeCanvases.size() == 4;
			if (followsSlideIds)
				for (std::size_t pageIndex = 0; pageIndex < 4; ++pageIndex)
				{
					const auto& canvas = reordered.snapshot->activeCanvases[pageIndex];
					followsSlideIds = canvas.slideId ==
						reorderedExpectation.slideIds[pageIndex] &&
						canvas.strokes.size() == 1 && canvas.strokes.front().style.fallbackRgb ==
						static_cast<std::uint32_t>(reorderedExpectation.slideIds[pageIndex]);
					if (!followsSlideIds) break;
				}
			PRESENTATION_CHECK(state, followsSlideIds);
		}

		UInkDocument unexpectedExtension = *exported.document;
		unexpectedExtension.headerExtension->name = "unexpected";
		PRESENTATION_CHECK(state, ImportApplicationOwnedPresentation(
			unexpectedExtension, expectation).status ==
			Draw3UInkImportStatus::IdentityMismatch);

		UInkDocument unboundCanvas = *exported.document;
		unboundCanvas.canvases.front().presentationUnbound = true;
		PRESENTATION_CHECK(state, ImportApplicationOwnedPresentation(
			unboundCanvas, expectation).status ==
			Draw3UInkImportStatus::TopologyMismatch);

		UInkDocument unsupportedMedia = *exported.document;
		UInkMedia media;
		media.contentId = static_cast<std::uint32_t>(
			unsupportedMedia.canvases.front().content.size());
		media.undoId = media.contentId;
		media.path = "media/image.png";
		media.mimeType = "image/png";
		unsupportedMedia.canvases.front().content.push_back(std::move(media));
		PRESENTATION_CHECK(state, ImportApplicationOwnedPresentation(
			unsupportedMedia, expectation).status ==
			Draw3UInkImportStatus::UnsupportedContent);
	}

	void TestWorkerExceptionBecomesCompletion(TestState& state)
	{
		const fs::path root = MakeRoot();
		const auto target = MakeTarget();
		PresentationAutoSaveService service;
		SetPresentationAutoSaveTestFaultInjection({ false, 0,
			"77777777-7777-4777-8777-777777777777", true });
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto completion = TakeCompletion(service);
		PRESENTATION_CHECK(state, completion.operation ==
			PresentationPersistenceOperation::Save &&
			completion.status == PresentationPersistenceStatus::IoError &&
			completion.mutationRevision == 1);
		std::error_code error;
		fs::remove_all(root, error);
	}
}

int RunPresentationAutoSaveTests()
{
	TestState state;
	{
		PresentationAutoSaveService validationService;
		auto target = MakeTarget();
		auto mismatched = MakeSnapshot(target, true);
		mismatched.workspaceType = 0;
		PRESENTATION_CHECK(state, validationService.SubmitSave({ target, 1,
			std::move(mismatched) }) == PresentationPersistenceSubmitStatus::Invalid);
		target.totalPages = Bridge::kMaximumPresentationPages + 1;
		PRESENTATION_CHECK(state, validationService.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Invalid);
	}
	PRESENTATION_CHECK(state, !ShouldQueuePresentationSave(1, 1));
	const std::uint64_t restoredRevision = 7;
	const std::uint64_t clearedRevision = PresentationClearAdvancesMutation(true, 1)
		? AdvancePresentationMutationRevision(restoredRevision) : restoredRevision;
	PRESENTATION_CHECK(state, clearedRevision == 8 &&
		ShouldQueuePresentationSave(clearedRevision, restoredRevision));
	PRESENTATION_CHECK(state, !PresentationClearAdvancesMutation(true, 0));
	PRESENTATION_CHECK(state, ShouldEvictPresentationSlot(true, 4, 4, 4, false));
	PRESENTATION_CHECK(state, !ShouldEvictPresentationSlot(true, 5, 4, 4, false));
	PRESENTATION_CHECK(state, !ShouldEvictPresentationSlot(true, 4, 4, 3, false));
	PRESENTATION_CHECK(state, !ShouldEvictPresentationSlot(true, 4, 4, 4, true));
	PRESENTATION_CHECK(state, !ShouldEvictPresentationSlot(false, 4, 4, 4, false));
	PRESENTATION_CHECK(state, ShouldPersistLoadedPresentationBindingMigration(
		Bridge::SlideBindingMode::StableSlideId, kInkeysPageIndexWorkspaceType));
	PRESENTATION_CHECK(state, !ShouldPersistLoadedPresentationBindingMigration(
		Bridge::SlideBindingMode::StableSlideId, 2));
	PRESENTATION_CHECK(state, !ShouldPersistLoadedPresentationBindingMigration(
		Bridge::SlideBindingMode::PageIndexFallback, kInkeysPageIndexWorkspaceType));
	TestSaveLoadAndClearOverwrite(state);
	TestStrictPresentationImporter(state);
	TestIndexFailureRecoveryAndForeignConflict(state);
	TestCorruptIndexFallbackAndIsolation(state);
	TestRootChangeDropsPendingIndexState(state);
	TestPageIndexFallbackOverwrite(state);
	TestRestartDropsStaleCompletions(state);
	TestMultiplePresentationsStayIsolated(state);
	TestLatestWinsPendingSave(state);
	TestConcurrentWritersShareCaseFoldedMutex(state);
	TestWorkerExceptionBecomesCompletion(state);
	ResetPresentationAutoSaveTestFaultInjection();
	return state.failures;
}
