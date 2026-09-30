#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <json/json.h>

#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Presentation.h"

#include <filesystem>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

import Inkeys.Drawing.Draw3.presentation_auto_save;
import draw3.uink_codec;
import draw3.uink_file;

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

	UInkGuid RetainedPageGuid()
	{
		return *ParseUInkGuid("99999999-9999-4999-8999-999999999999");
	}

	Draw3UInkExportSnapshot MakeRetainedSnapshot(
		const Bridge::PresentationTarget& target, bool endScreen)
	{
		auto snapshot = MakeSnapshot(target, true);
		snapshot.activeCanvases = std::move(snapshot.canvases);
		if (endScreen)
		{
			Draw3UInkCanvasSnapshot end;
			end.pageGuid = *ParseUInkGuid("77777777-7777-4777-8777-777777777777");
			end.pageIndex = target.totalPages;
			end.pageNumber = target.totalPages + 1;
			end.extra = MakeInkeysEndScreenExtra(
				Draw3UInkImportBindingMode::StableSlideId);
			auto stroke = snapshot.activeCanvases.front().strokes.front();
			stroke.style.fallbackRgb = 0xeeeeee;
			end.strokes.push_back(std::move(stroke));
			snapshot.activeCanvases.push_back(std::move(end));
			snapshot.currentPageIndex = target.pageIndex;
		}
		Draw3UInkCanvasSnapshot retained;
		retained.pageGuid = RetainedPageGuid();
		retained.pageIndex = static_cast<std::uint32_t>(snapshot.activeCanvases.size());
		retained.pageNumber = retained.pageIndex + 1;
		retained.slideId = 202;
		retained.retained = true;
		UInkExtra extra = MakeInkeysBindingExtra(
			Draw3UInkImportBindingMode::StableSlideId);
		UInkMessagePackValue key; key.value = std::string("inkeysPageState");
		UInkMessagePackValue value; value.value = std::string("retained");
		extra.emplace_back(std::move(key), std::move(value));
		retained.extra = std::move(extra);
		auto stroke = snapshot.activeCanvases.front().strokes.front();
		stroke.style.fallbackRgb = 0x202020;
		retained.strokes.push_back(std::move(stroke));
		snapshot.retainedCanvases.push_back(std::move(retained));
		snapshot.canvases = snapshot.activeCanvases;
		return snapshot;
	}

	Draw3UInkExportSnapshot MakeTwoPageTrackSnapshot(
		const Bridge::PresentationTarget& target, bool independentIdentity)
	{
		auto snapshot = MakeSnapshot(target, false);
		if (independentIdentity)
		{
			snapshot.fileGuid = *ParseUInkGuid(
				"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
			snapshot.workspaceGuid = *ParseUInkGuid(
				"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
		}
		snapshot.canvases.clear();
		for (std::uint32_t index = 0; index < 2; ++index)
		{
			Draw3UInkCanvasSnapshot canvas;
			canvas.pageGuid = *ParseUInkGuid(independentIdentity
				? (index == 0 ? "cccccccc-cccc-4ccc-8ccc-cccccccccccc"
					: "dddddddd-dddd-4ddd-8ddd-dddddddddddd")
				: (index == 0 ? "33333333-3333-4333-8333-333333333333"
					: "44444444-4444-4444-8444-444444444444"));
			canvas.pageIndex = index;
			canvas.pageNumber = index + 1;
			if (target.bindingMode == Bridge::SlideBindingMode::StableSlideId)
				canvas.slideId = target.slideIds[index];
			canvas.extra = MakeInkeysBindingExtra(
				target.bindingMode == Bridge::SlideBindingMode::StableSlideId
					? Draw3UInkImportBindingMode::StableSlideId
					: Draw3UInkImportBindingMode::PageIndexFallback);
			Draw3UInkStrokeSnapshot stroke;
			stroke.style.kind = Draw3UInkStrokeKind::Pen;
			stroke.style.fallbackRgb = independentIdentity
				? (index == 0 ? 0xaaaaaa : 0xbbbbbb)
				: (index == 0 ? 0x111111 : 0x222222);
			stroke.points = { { 10.0f + index, 20.0f, 4.0f },
				{ 30.0f + index, 40.0f, 4.0f } };
			canvas.strokes.push_back(std::move(stroke));
			snapshot.canvases.push_back(std::move(canvas));
		}
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

	std::optional<std::string> ReadFileBytes(const fs::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream) return std::nullopt;
		return std::string(std::istreambuf_iterator<char>(stream),
			std::istreambuf_iterator<char>());
	}

	bool ReadIndexJson(const fs::path& path, Json::Value& document)
	{
		const auto bytes = ReadFileBytes(path);
		if (!bytes) return false;
		Json::CharReaderBuilder builder;
		std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
		std::string errors;
		return reader && reader->parse(bytes->data(), bytes->data() + bytes->size(),
			&document, &errors);
	}

	bool WriteIndexJson(const fs::path& path, const Json::Value& document)
	{
		Json::StreamWriterBuilder builder;
		builder["indentation"] = "  ";
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output << Json::writeString(builder, document);
		return output.good();
	}

	bool IndexKnowsSlides(const fs::path& path,
		std::set<std::int32_t> expected)
	{
		Json::Value document;
		if (!ReadIndexJson(path, document) || !document["entries"].isArray() ||
			document["entries"].size() != 1 ||
			!document["entries"][0]["slideIds"].isArray()) return false;
		std::set<std::int32_t> actual;
		for (const auto& id : document["entries"][0]["slideIds"])
		{
			if (!id.isInt()) return false;
			actual.insert(id.asInt());
		}
		return actual == expected;
	}

	bool LoadedRetainedPage(const PresentationPersistenceCompletion& completion,
		bool endScreen)
	{
		if (completion.status != PresentationPersistenceStatus::Loaded ||
			!completion.loadedSnapshot) return false;
		const auto& snapshot = *completion.loadedSnapshot;
		if (snapshot.activeCanvases.size() != (endScreen ? 2u : 1u) ||
			snapshot.retainedCanvases.size() != 1 ||
			snapshot.currentPageIndex != (endScreen ? 1u : 0u) ||
			snapshot.activeCanvases[0].slideId != 101 ||
			snapshot.activeCanvases[0].pageGuid !=
				*ParseUInkGuid("33333333-3333-4333-8333-333333333333")) return false;
		const auto& retained = snapshot.retainedCanvases[0];
		if (retained.pageGuid != RetainedPageGuid() || retained.slideId != 202 ||
			!retained.retained || retained.strokes.size() != 1 ||
			retained.strokes.front().style.fallbackRgb != 0x202020 ||
			!HasInkeysPageStateExtra(retained.extra, true)) return false;
		if (!endScreen) return true;
		const auto& end = snapshot.activeCanvases[1];
		return !end.slideId && end.pageGuid ==
			*ParseUInkGuid("77777777-7777-4777-8777-777777777777") &&
			InkeysPageKind(end.extra) == UInkInkeysPageKind::EndScreen &&
			end.strokes.size() == 1 &&
			end.strokes.front().style.fallbackRgb == 0xeeeeee;
	}

	std::string UpperAscii(std::string value)
	{
		for (char& character : value)
			if (character >= 'a' && character <= 'z')
				character = static_cast<char>(character - 'a' + 'A');
		return value;
	}

	std::optional<fs::path> SingleUInkFile(const fs::path& root)
	{
		const fs::path files = root / L"presentation" / L"files";
		std::error_code error;
		if (!fs::is_directory(files, error)) return std::nullopt;
		std::optional<fs::path> found;
		for (const auto& entry : fs::directory_iterator(files, error))
		{
			if (!entry.is_regular_file() || entry.path().extension() != L".uink")
				continue;
			if (found) return std::nullopt;
			found = entry.path();
		}
		return error ? std::nullopt : found;
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

		// 恢复后清空会推进 mutation；即使当前无内容，也提交同一逻辑文件的新物理版本。
		PRESENTATION_CHECK(state, ShouldQueuePresentationSave(2, 1));
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 2,
			MakeSnapshot(target, false) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		PRESENTATION_CHECK(state, CountUInkFiles(root) == 2);

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

		// 新物理版本提交而 index 发布失败后，下一次仍能从 self-written revision 收敛。
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
		PRESENTATION_CHECK(state, CountUInkFiles(root) == 4);
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
		const auto uink = SingleUInkFile(root);
		const auto originalUInk = uink ? ReadFileBytes(*uink) : std::nullopt;
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
		PRESENTATION_CHECK(state, uink && originalUInk &&
			ReadFileBytes(*uink) == originalUInk);
		fs::remove_all(root, error);
		error.clear();
		PRESENTATION_CHECK(state, !fs::exists(root, error) && !error);
	}

	void TestInterruptedSecondCommitKeepsPreviousVersion(TestState& state)
	{
		const fs::path root = MakeRoot();
		const auto target = MakeTarget();
		constexpr const char* session = "efefefef-efef-4fef-8fef-efefefefefef";
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PresentationAutoSaveService seed;
		PRESENTATION_CHECK(state, seed.Start(root.wstring()));
		PRESENTATION_CHECK(state, seed.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		seed.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(seed).status ==
			PresentationPersistenceStatus::Committed);
		const fs::path index = root / L"presentation" / L"index.json";
		const auto previousIndex = ReadFileBytes(index);
		const auto previousPath = SingleUInkFile(root);
		const auto previousUInk = previousPath
			? ReadFileBytes(*previousPath) : std::nullopt;
		PRESENTATION_CHECK(state, previousIndex && previousUInk);

		HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HANDLE resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		PRESENTATION_CHECK(state, ready && resume);
		PresentationAutoSaveTestFaultInjection faults;
		faults.failIndexCommit = true;
		faults.sessionIdOverride = session;
		faults.afterUInkCommittedEvent = ready;
		faults.continueIndexCommitEvent = resume;
		SetPresentationAutoSaveTestFaultInjection(faults);
		PresentationAutoSaveService interrupted;
		PRESENTATION_CHECK(state, interrupted.Start(root.wstring()));
		PRESENTATION_CHECK(state, interrupted.SubmitSave({ target, 2,
			MakeSnapshot(target, false) }) == PresentationPersistenceSubmitStatus::Accepted);
		const DWORD wait = ready ? WaitForSingleObject(ready, 5000) : WAIT_FAILED;
		PRESENTATION_CHECK(state, wait == WAIT_OBJECT_0);
		if (resume) SetEvent(resume);
		interrupted.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(interrupted).status ==
			PresentationPersistenceStatus::IoError);
		if (ready) CloseHandle(ready);
		if (resume) CloseHandle(resume);
		PRESENTATION_CHECK(state, previousIndex &&
			ReadFileBytes(index) == previousIndex);
		PRESENTATION_CHECK(state, previousPath && previousUInk &&
			ReadFileBytes(*previousPath) == previousUInk);

		// 故障后的新 Service 不持有 pendingIndexEntries，必须严格读回旧提交。
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PresentationAutoSaveService fresh;
		PRESENTATION_CHECK(state, fresh.Start(root.wstring()));
		PRESENTATION_CHECK(state, fresh.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		fresh.CloseAndDrain();
		const auto loaded = TakeCompletion(fresh);
		PRESENTATION_CHECK(state, loaded.status ==
			PresentationPersistenceStatus::Loaded && loaded.loadedSnapshot &&
			loaded.loadedSnapshot->canvases.front().strokes.size() == 1);
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestSecondCommitBackupStillMatchesPreviousUInk(TestState& state)
	{
		const fs::path root = MakeRoot();
		const auto target = MakeTarget();
		constexpr const char* session = "fefefefe-fefe-4efe-8efe-fefefefefefe";
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PresentationAutoSaveService service;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		const auto previousPath = SingleUInkFile(root);
		const auto previousUInk = previousPath
			? ReadFileBytes(*previousPath) : std::nullopt;
		PRESENTATION_CHECK(state, previousUInk && !previousUInk->empty());
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 2,
			MakeSnapshot(target, false) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		const fs::path presentationRoot = root / L"presentation";
		const fs::path index = presentationRoot / L"index.json";
		const fs::path backup = presentationRoot / L"index.json.bak";
		PRESENTATION_CHECK(state, fs::exists(backup));
		PRESENTATION_CHECK(state, previousPath && previousUInk &&
			ReadFileBytes(*previousPath) == previousUInk);
		{
			std::ofstream corrupt(index, std::ios::binary | std::ios::trunc);
			corrupt << "{invalid-primary";
			PRESENTATION_CHECK(state, corrupt.good());
		}
		PresentationAutoSaveService fresh;
		PRESENTATION_CHECK(state, fresh.Start(root.wstring()));
		PRESENTATION_CHECK(state, fresh.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		fresh.CloseAndDrain();
		const auto loaded = TakeCompletion(fresh);
		PRESENTATION_CHECK(state, loaded.status ==
			PresentationPersistenceStatus::Loaded && loaded.loadedSnapshot &&
			loaded.loadedSnapshot->canvases.front().strokes.size() == 1);
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestLegacyIndexMigratesWithoutReplacingLegacyFile(TestState& state)
	{
		const fs::path root = MakeRoot();
		const auto target = MakeTarget();
		constexpr const char* session = "cacacaca-caca-4aca-8aca-cacacacacaca";
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PresentationAutoSaveService service;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		const fs::path presentationRoot = root / L"presentation";
		const fs::path index = presentationRoot / L"index.json";
		const auto firstVersion = SingleUInkFile(root);
		const fs::path legacy = presentationRoot / L"files" /
			L"11111111-1111-4111-8111-111111111111.uink";
		Json::Value legacyIndex;
		const bool parsed = ReadIndexJson(index, legacyIndex);
		PRESENTATION_CHECK(state, parsed && firstVersion &&
			legacyIndex["schemaVersion"].asInt() == 2);
		if (!parsed || !firstVersion)
		{
			std::error_code cleanup;
			fs::remove_all(root, cleanup);
			return;
		}
		std::error_code error;
		fs::rename(*firstVersion, legacy, error);
		PRESENTATION_CHECK(state, !error);
		if (error)
		{
			fs::remove_all(root, error);
			return;
		}
		const auto legacyBytes = ReadFileBytes(legacy);
		legacyIndex["schemaVersion"] = 1;
		legacyIndex["entries"][0]["relativePath"] =
			"files/11111111-1111-4111-8111-111111111111.uink";
		PRESENTATION_CHECK(state, WriteIndexJson(index, legacyIndex));
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto oldLoaded = TakeCompletion(service);
		PRESENTATION_CHECK(state, oldLoaded.status ==
			PresentationPersistenceStatus::Loaded && oldLoaded.loadedSnapshot &&
			oldLoaded.loadedSnapshot->canvases.front().strokes.size() == 1);
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 2,
			MakeSnapshot(target, false) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		Json::Value current;
		Json::Value backup;
		PRESENTATION_CHECK(state, ReadIndexJson(index, current) &&
			current["schemaVersion"].asInt() == 2 &&
			current["entries"][0]["relativePath"].asString() !=
			legacyIndex["entries"][0]["relativePath"].asString());
		PRESENTATION_CHECK(state, ReadIndexJson(presentationRoot /
			L"index.json.bak", backup) && backup["schemaVersion"].asInt() == 1);
		PRESENTATION_CHECK(state, legacyBytes && ReadFileBytes(legacy) == legacyBytes);
		{
			std::ofstream corrupt(index, std::ios::binary | std::ios::trunc);
			corrupt << "{invalid-primary";
		}
		PresentationAutoSaveService fresh;
		PRESENTATION_CHECK(state, fresh.Start(root.wstring()));
		PRESENTATION_CHECK(state, fresh.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		fresh.CloseAndDrain();
		const auto backupLoaded = TakeCompletion(fresh);
		PRESENTATION_CHECK(state, backupLoaded.status ==
			PresentationPersistenceStatus::Loaded && backupLoaded.loadedSnapshot &&
			backupLoaded.loadedSnapshot->canvases.front().strokes.size() == 1);
		fs::remove_all(root, error);
	}

	void TestVersionPathsRejectUntrustedVariants(TestState& state)
	{
		const fs::path root = MakeRoot();
		const auto target = MakeTarget();
		constexpr const char* session = "cdcdcdcd-cdcd-4dcd-8dcd-cdcdcdcdcdcd";
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PresentationAutoSaveService seed;
		PRESENTATION_CHECK(state, seed.Start(root.wstring()));
		PRESENTATION_CHECK(state, seed.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		seed.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(seed).status ==
			PresentationPersistenceStatus::Committed);
		const fs::path index = root / L"presentation" / L"index.json";
		const auto indexBytes = ReadFileBytes(index);
		const auto uink = SingleUInkFile(root);
		const auto uinkBytes = uink ? ReadFileBytes(*uink) : std::nullopt;
		Json::Value original;
		const bool parsed = ReadIndexJson(index, original);
		PRESENTATION_CHECK(state, parsed &&
			original["schemaVersion"].asInt() == 2 && uinkBytes);
		if (!parsed || !uinkBytes || !indexBytes)
		{
			std::error_code cleanup;
			fs::remove_all(root, cleanup);
			return;
		}
		const std::string guid = FormatUInkGuid(MakeSnapshot(target, true).fileGuid);
		const std::string valid = original["entries"][0]["relativePath"].asString();
		const std::vector<std::pair<int, std::string>> invalid = {
			{ 2, "files/../outside.uink" },
			{ 2, "files/" + guid + "_not-a-guid.uink" },
			{ 2, "files/44444444-4444-4444-8444-444444444444_" +
				valid.substr(valid.size() - 41) },
			{ 2, "files\\" + guid + ".uink" },
			{ 1, valid },
		};
		for (const auto& [schema, path] : invalid)
		{
			Json::Value malformed = original;
			malformed["schemaVersion"] = schema;
			malformed["entries"][0]["relativePath"] = path;
			PRESENTATION_CHECK(state, WriteIndexJson(index, malformed));
			PresentationAutoSaveService fresh;
			PRESENTATION_CHECK(state, fresh.Start(root.wstring()));
			PRESENTATION_CHECK(state, fresh.SubmitLoad({ target }) ==
				PresentationPersistenceSubmitStatus::Accepted);
			fresh.CloseAndDrain();
			PRESENTATION_CHECK(state, TakeCompletion(fresh).status ==
				PresentationPersistenceStatus::IoError);
		}
		if (indexBytes)
		{
			std::ofstream restore(index, std::ios::binary | std::ios::trunc);
			restore.write(indexBytes->data(), indexBytes->size());
			PRESENTATION_CHECK(state, restore.good());
		}
		PresentationAutoSaveService fresh;
		PRESENTATION_CHECK(state, fresh.Start(root.wstring()));
		PRESENTATION_CHECK(state, fresh.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		fresh.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(fresh).status ==
			PresentationPersistenceStatus::Loaded);
		PRESENTATION_CHECK(state, uink && uinkBytes && ReadFileBytes(*uink) == uinkBytes);
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestIndexRejectsCaseAliasedFileIdentities(TestState& state)
	{
		const fs::path root = MakeRoot();
		const auto target = MakeTarget();
		constexpr const char* session = "acacacac-acac-4cac-8cac-acacacacacac";
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		auto snapshot = MakeSnapshot(target, true);
		snapshot.fileGuid = *ParseUInkGuid(
			"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
		PresentationAutoSaveService seed;
		PRESENTATION_CHECK(state, seed.Start(root.wstring()));
		PRESENTATION_CHECK(state, seed.SubmitSave({ target, 1,
			std::move(snapshot) }) == PresentationPersistenceSubmitStatus::Accepted);
		seed.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(seed).status ==
			PresentationPersistenceStatus::Committed);
		const fs::path index = root / L"presentation" / L"index.json";
		Json::Value original;
		const auto firstVersion = SingleUInkFile(root);
		const bool parsed = ReadIndexJson(index, original);
		PRESENTATION_CHECK(state, parsed && firstVersion &&
			original["schemaVersion"].asInt() == 2);
		if (!parsed || !firstVersion)
		{
			std::error_code error;
			fs::remove_all(root, error);
			return;
		}
		const std::string guid = original["entries"][0]["fileGuid"].asString();
		const std::string path = original["entries"][0]["relativePath"].asString();
		PRESENTATION_CHECK(state, path.starts_with("files/" + guid + "_"));
		if (!path.starts_with("files/" + guid + "_"))
		{
			std::error_code error;
			fs::remove_all(root, error);
			return;
		}
		Json::Value v2Alias = original;
		Json::Value duplicate = original["entries"][0];
		duplicate["sourceIdentity"] = "path:c:\\lessons\\case-alias.pptx";
		duplicate["presentationKey"] =
			"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
		duplicate["fileGuid"] = UpperAscii(guid);
		duplicate["relativePath"] = "files/" + UpperAscii(guid) +
			path.substr(std::string("files/").size() + guid.size());
		v2Alias["entries"].append(duplicate);
		PRESENTATION_CHECK(state, WriteIndexJson(index, v2Alias));
		PresentationAutoSaveService reader;
		PRESENTATION_CHECK(state, reader.Start(root.wstring()));
		PRESENTATION_CHECK(state, reader.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		reader.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(reader).status ==
			PresentationPersistenceStatus::IoError);

		// v1 允许大写 GUID 的单条历史 entry，但同一物理路径的大小写别名必须拒绝。
		std::error_code error;
		const fs::path legacy = root / L"presentation" / L"files" /
			L"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa.uink";
		fs::rename(*firstVersion, legacy, error);
		PRESENTATION_CHECK(state, !error);
		if (error)
		{
			fs::remove_all(root, error);
			return;
		}
		const auto legacyBytes = ReadFileBytes(legacy);
		Json::Value v1 = original;
		v1["schemaVersion"] = 1;
		v1["entries"][0]["relativePath"] =
			"files/aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa.uink";
		PRESENTATION_CHECK(state, WriteIndexJson(index, v1));
		PRESENTATION_CHECK(state, reader.Start(root.wstring()));
		PRESENTATION_CHECK(state, reader.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		reader.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(reader).status ==
			PresentationPersistenceStatus::Loaded);
		Json::Value uppercaseV1 = v1;
		uppercaseV1["entries"][0]["fileGuid"] = UpperAscii(guid);
		uppercaseV1["entries"][0]["relativePath"] =
			"files/" + UpperAscii(guid) + ".uink";
		PRESENTATION_CHECK(state, WriteIndexJson(index, uppercaseV1));
		PRESENTATION_CHECK(state, reader.Start(root.wstring()));
		PRESENTATION_CHECK(state, reader.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		reader.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(reader).status ==
			PresentationPersistenceStatus::Loaded);
		Json::Value v1Alias = v1;
		duplicate = v1["entries"][0];
		duplicate["sourceIdentity"] = "path:c:\\lessons\\case-alias.pptx";
		duplicate["presentationKey"] =
			"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
		duplicate["fileGuid"] = UpperAscii(guid);
		duplicate["relativePath"] =
			"files/" + UpperAscii(guid) + ".uink";
		v1Alias["entries"].append(duplicate);
		PRESENTATION_CHECK(state, WriteIndexJson(index, v1Alias));
		PRESENTATION_CHECK(state, reader.Start(root.wstring()));
		PRESENTATION_CHECK(state, reader.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		reader.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(reader).status ==
			PresentationPersistenceStatus::IoError);
		PRESENTATION_CHECK(state, legacyBytes &&
			ReadFileBytes(legacy) == legacyBytes);
		fs::remove_all(root, error);
	}

	void TestUppercaseLegacyGuidCanSaveCanonicalVersion(TestState& state,
		bool uppercaseWorkspace, bool failIndexOnSave)
	{
		const fs::path root = MakeRoot();
		const auto target = MakeTarget();
		const UInkGuid fileGuid = *ParseUInkGuid(
			"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
		const UInkGuid workspaceGuid = *ParseUInkGuid(
			"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
		const std::string canonicalFile = FormatUInkGuid(fileGuid);
		const std::string canonicalWorkspace = FormatUInkGuid(workspaceGuid);
		constexpr const char* session = "82828282-8282-4282-8282-828282828282";
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		auto seedSnapshot = MakeSnapshot(target, true);
		seedSnapshot.fileGuid = fileGuid;
		seedSnapshot.workspaceGuid = workspaceGuid;
		PresentationSaveRequest first{ target, 1, std::move(seedSnapshot) };
		first.slotGeneration = 21;
		PresentationAutoSaveService seed;
		PRESENTATION_CHECK(state, seed.Start(root.wstring()));
		PRESENTATION_CHECK(state, seed.SubmitSave(std::move(first)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		seed.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(seed).status ==
			PresentationPersistenceStatus::Committed);
		const fs::path index = root / L"presentation" / L"index.json";
		const fs::path backup = root / L"presentation" / L"index.json.bak";
		const auto firstVersion = SingleUInkFile(root);
		Json::Value original;
		const bool parsed = ReadIndexJson(index, original);
		PRESENTATION_CHECK(state, parsed && firstVersion);
		if (!parsed || !firstVersion)
		{
			std::error_code error;
			fs::remove_all(root, error);
			return;
		}
		const fs::path legacy = root / L"presentation" / L"files" /
			L"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa.uink";
		std::error_code error;
		fs::rename(*firstVersion, legacy, error);
		PRESENTATION_CHECK(state, !error);
		if (error)
		{
			fs::remove_all(root, error);
			return;
		}
		Json::Value oldIndex = original;
		oldIndex["schemaVersion"] = 1;
		const std::string indexedFile = uppercaseWorkspace
			? canonicalFile : UpperAscii(canonicalFile);
		oldIndex["entries"][0]["fileGuid"] = indexedFile;
		oldIndex["entries"][0]["workspaceGuid"] = uppercaseWorkspace
			? UpperAscii(canonicalWorkspace) : canonicalWorkspace;
		oldIndex["entries"][0]["relativePath"] =
			"files/" + indexedFile + ".uink";
		PRESENTATION_CHECK(state, WriteIndexJson(index, oldIndex));
		const auto legacyBytes = ReadFileBytes(legacy);
		const auto oldIndexBytes = ReadFileBytes(index);
		PRESENTATION_CHECK(state, legacyBytes && oldIndexBytes &&
			!fs::exists(backup));

		PresentationAutoSaveService reader;
		PresentationLoadRequest oldLoad{ target };
		oldLoad.slotGeneration = 21;
		PRESENTATION_CHECK(state, reader.Start(root.wstring()));
		PRESENTATION_CHECK(state, reader.SubmitLoad(std::move(oldLoad)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		reader.CloseAndDrain();
		const auto oldLoaded = TakeCompletion(reader);
		PRESENTATION_CHECK(state, oldLoaded.status ==
			PresentationPersistenceStatus::Loaded &&
			oldLoaded.fileGuid == fileGuid && oldLoaded.loadedSnapshot &&
			oldLoaded.loadedSnapshot->canvases.front().strokes.size() == 1);
		if (oldLoaded.status != PresentationPersistenceStatus::Loaded)
		{
			fs::remove_all(root, error);
			return;
		}

		auto changed = MakeSnapshot(target, false);
		changed.fileGuid = fileGuid;
		changed.workspaceGuid = workspaceGuid;
		PresentationSaveRequest next{ target, 2, std::move(changed) };
		next.slotGeneration = 21;
		SetPresentationAutoSaveTestFaultInjection({ failIndexOnSave, 0, session });
		PRESENTATION_CHECK(state, reader.Start(root.wstring()));
		PRESENTATION_CHECK(state, reader.SubmitSave(std::move(next)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		reader.CloseAndDrain();
		const auto saved = TakeCompletion(reader);
		if (failIndexOnSave)
		{
			PRESENTATION_CHECK(state, saved.status ==
				PresentationPersistenceStatus::IoError &&
				saved.storageTrack == PresentationStorageTrack::Base &&
				saved.slotGeneration == 21 && saved.fileGuid == fileGuid);
			PRESENTATION_CHECK(state, oldIndexBytes && legacyBytes &&
				ReadFileBytes(index) == oldIndexBytes &&
				ReadFileBytes(legacy) == legacyBytes &&
				!fs::exists(backup) && CountUInkFiles(root) == 2);
			SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
			PresentationLoadRequest pendingLoad{ target };
			pendingLoad.slotGeneration = 21;
			PRESENTATION_CHECK(state, reader.Start(root.wstring()));
			PRESENTATION_CHECK(state, reader.SubmitLoad(std::move(pendingLoad)) ==
				PresentationPersistenceSubmitStatus::Accepted);
			reader.CloseAndDrain();
			const auto pendingLoaded = TakeCompletion(reader);
			PRESENTATION_CHECK(state, pendingLoaded.status ==
				PresentationPersistenceStatus::Loaded &&
				pendingLoaded.slotGeneration == 21 &&
				pendingLoaded.fileGuid == fileGuid &&
				pendingLoaded.loadedSnapshot &&
				pendingLoaded.loadedSnapshot->canvases.front().strokes.empty());
			PresentationAutoSaveService freshOld;
			PRESENTATION_CHECK(state, freshOld.Start(root.wstring()));
			PRESENTATION_CHECK(state, freshOld.SubmitLoad({ target }) ==
				PresentationPersistenceSubmitStatus::Accepted);
			freshOld.CloseAndDrain();
			const auto committedOld = TakeCompletion(freshOld);
			PRESENTATION_CHECK(state, committedOld.status ==
				PresentationPersistenceStatus::Loaded &&
				committedOld.loadedSnapshot &&
				committedOld.loadedSnapshot->canvases.front().strokes.size() == 1);
			fs::remove_all(root, error);
			return;
		}
		PRESENTATION_CHECK(state, saved.status ==
			PresentationPersistenceStatus::Committed &&
			saved.storageTrack == PresentationStorageTrack::Base &&
			saved.slotGeneration == 21 && saved.fileGuid == fileGuid);
		if (saved.status != PresentationPersistenceStatus::Committed)
		{
			fs::remove_all(root, error);
			return;
		}
		Json::Value published;
		const bool publishedParsed = ReadIndexJson(index, published) &&
			published["schemaVersion"].asInt() == 2 &&
			published["entries"].isArray() && published["entries"].size() == 1;
		PRESENTATION_CHECK(state, publishedParsed);
		if (publishedParsed)
		{
			const auto& entry = published["entries"][0];
			const std::string path = entry["relativePath"].asString();
			PRESENTATION_CHECK(state, entry["fileGuid"].asString() ==
				canonicalFile && entry["workspaceGuid"].asString() ==
				canonicalWorkspace && path.starts_with(
					"files/" + canonicalFile + "_") &&
				path.ends_with(".uink") &&
				path != "files/" + UpperAscii(canonicalFile) + ".uink" &&
				fs::exists(root / L"presentation" / fs::path(path)));
		}
		PRESENTATION_CHECK(state, legacyBytes && oldIndexBytes &&
			ReadFileBytes(legacy) == legacyBytes &&
			ReadFileBytes(backup) == oldIndexBytes &&
			CountUInkFiles(root) == 2);

		PresentationAutoSaveService fresh;
		PresentationLoadRequest newLoad{ target };
		newLoad.slotGeneration = 21;
		PRESENTATION_CHECK(state, fresh.Start(root.wstring()));
		PRESENTATION_CHECK(state, fresh.SubmitLoad(std::move(newLoad)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		fresh.CloseAndDrain();
		const auto newLoaded = TakeCompletion(fresh);
		PRESENTATION_CHECK(state, newLoaded.status ==
			PresentationPersistenceStatus::Loaded &&
			newLoaded.fileGuid == fileGuid && newLoaded.loadedSnapshot &&
			newLoaded.loadedSnapshot->canvases.front().strokes.empty());
		fs::remove_all(root, error);
	}

	void TestPublishedVersionsAndUnknownFileAreRetained(TestState& state)
	{
		const fs::path root = MakeRoot();
		const auto target = MakeTarget();
		SetPresentationAutoSaveTestFaultInjection({ false, 0,
			"dededede-dede-4ede-8ede-dededededede" });
		PresentationAutoSaveService service;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		const auto retired = SingleUInkFile(root);
		const auto retiredBytes = retired ? ReadFileBytes(*retired) : std::nullopt;
		const fs::path unknown = root / L"presentation" / L"files" /
			L"11111111-1111-4111-8111-111111111111_ffffffff-ffff-4fff-8fff-ffffffffffff.uink";
		{
			std::ofstream file(unknown, std::ios::binary);
			file << "foreign-file-must-stay";
			PRESENTATION_CHECK(state, file.good());
		}
		for (std::uint64_t revision = 2; revision <= 3; ++revision)
		{
			PRESENTATION_CHECK(state, service.Start(root.wstring()));
			PRESENTATION_CHECK(state, service.SubmitSave({ target, revision,
				MakeSnapshot(target, revision == 3) }) ==
				PresentationPersistenceSubmitStatus::Accepted);
			service.CloseAndDrain();
			PRESENTATION_CHECK(state, TakeCompletion(service).status ==
				PresentationPersistenceStatus::Committed);
		}
		PRESENTATION_CHECK(state, retired && retiredBytes &&
			ReadFileBytes(*retired) == retiredBytes);
		PRESENTATION_CHECK(state, ReadFileBytes(unknown) ==
			std::optional<std::string>("foreign-file-must-stay"));
		PRESENTATION_CHECK(state, CountUInkFiles(root) == 4);
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestProcessKillBeforeIndexSwitchKeepsPreviousVersion(TestState& state)
	{
		const fs::path root = MakeRoot();
		const auto target = MakeTarget();
		constexpr const char* session = "edededed-eded-4ded-8ded-edededededed";
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PresentationAutoSaveService seed;
		PRESENTATION_CHECK(state, seed.Start(root.wstring()));
		PRESENTATION_CHECK(state, seed.SubmitSave({ target, 1,
			MakeSnapshot(target, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		seed.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(seed).status ==
			PresentationPersistenceStatus::Committed);
		const fs::path index = root / L"presentation" / L"index.json";
		const auto previousIndex = ReadFileBytes(index);
		const auto previousPath = SingleUInkFile(root);
		const auto previousUInk = previousPath
			? ReadFileBytes(*previousPath) : std::nullopt;
		const auto token = CreateUInkGuid();
		PRESENTATION_CHECK(state, previousIndex && previousUInk && token);
		if (!previousIndex || !previousUInk || !token)
		{
			std::error_code error;
			fs::remove_all(root, error);
			return;
		}
		const std::string tokenText = FormatUInkGuid(*token);
		const std::wstring suffix(tokenText.begin(), tokenText.end());
		const std::wstring readyName = L"Local\\InkeysPresentationAtomic_" +
			suffix + L"_ready";
		const std::wstring resumeName = L"Local\\InkeysPresentationAtomic_" +
			suffix + L"_resume";
		HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str());
		HANDLE resume = CreateEventW(nullptr, TRUE, FALSE, resumeName.c_str());
		PRESENTATION_CHECK(state, ready && resume);
		std::wstring executable(32768, L'\0');
		const DWORD pathLength = GetModuleFileNameW(nullptr, executable.data(),
			static_cast<DWORD>(executable.size()));
		PRESENTATION_CHECK(state, pathLength != 0 && pathLength < executable.size());
		bool childStopped = true;
		if (ready && resume && pathLength != 0 && pathLength < executable.size())
		{
			executable.resize(pathLength);
			// 只启动本测试 EXE 的显式子命令；强杀仅使用本次 CreateProcess 返回的 HANDLE。
			std::wstring command = L"\"" + executable +
				L"\" --presentation-atomic-child \"" + root.wstring() +
				L"\" \"" + readyName + L"\" \"" + resumeName + L"\"";
			STARTUPINFOW startup = {};
			startup.cb = sizeof(startup);
			PROCESS_INFORMATION process = {};
			const BOOL launched = CreateProcessW(executable.c_str(), command.data(),
				nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
				&startup, &process);
			PRESENTATION_CHECK(state, launched);
			if (launched)
			{
				HANDLE waits[] = { ready, process.hProcess };
				const DWORD reached = WaitForMultipleObjects(2, waits, FALSE, 10000);
				PRESENTATION_CHECK(state, reached == WAIT_OBJECT_0);
				const BOOL killed = TerminateProcess(process.hProcess, 0xE044);
				PRESENTATION_CHECK(state, killed);
				childStopped = WaitForSingleObject(process.hProcess, 10000) ==
					WAIT_OBJECT_0;
				PRESENTATION_CHECK(state, childStopped);
				DWORD exitCode = 0;
				PRESENTATION_CHECK(state, GetExitCodeProcess(process.hProcess,
					&exitCode) && exitCode == 0xE044);
				CloseHandle(process.hThread);
				CloseHandle(process.hProcess);
			}
		}
		if (ready) CloseHandle(ready);
		if (resume) CloseHandle(resume);
		if (!childStopped) return;
		PRESENTATION_CHECK(state, ReadFileBytes(index) == previousIndex);
		PRESENTATION_CHECK(state, previousPath &&
			ReadFileBytes(*previousPath) == previousUInk);
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PresentationAutoSaveService fresh;
		PRESENTATION_CHECK(state, fresh.Start(root.wstring()));
		PRESENTATION_CHECK(state, fresh.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		fresh.CloseAndDrain();
		const auto loaded = TakeCompletion(fresh);
		PRESENTATION_CHECK(state, loaded.status ==
			PresentationPersistenceStatus::Loaded && loaded.loadedSnapshot &&
			loaded.loadedSnapshot->canvases.front().strokes.size() == 1);
		if (childStopped)
		{
			std::error_code error;
			fs::remove_all(root, error);
		}
	}

	void TestFirstRetainedCommitKeepsKnownSlideAndEndScreen(TestState& state)
	{
		const fs::path root = MakeRoot();
		auto target = MakeTarget();
		target.pageKind = Bridge::PresentationPageKind::EndScreen;
		target.pageIndex = target.totalPages;
		target.slideId.reset();
		constexpr const char* session = "12121212-1212-4212-8212-121212121212";
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PresentationAutoSaveService service;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 1,
			MakeRetainedSnapshot(target, true) }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		const fs::path presentationRoot = root / L"presentation";
		const fs::path index = presentationRoot / L"index.json";
		PRESENTATION_CHECK(state, IndexKnowsSlides(index, { 101, 202 }));
		PresentationAutoSaveService fresh;
		PRESENTATION_CHECK(state, fresh.Start(root.wstring()));
		PRESENTATION_CHECK(state, fresh.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		fresh.CloseAndDrain();
		const auto loaded = TakeCompletion(fresh);
		const bool readable = LoadedRetainedPage(loaded, true);
		PRESENTATION_CHECK(state, readable);
		if (!readable)
		{
			std::error_code error;
			fs::remove_all(root, error);
			return;
		}
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 2,
			MakeRetainedSnapshot(target, true) }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		PRESENTATION_CHECK(state, IndexKnowsSlides(index, { 101, 202 }) &&
			IndexKnowsSlides(presentationRoot / L"index.json.bak", { 101, 202 }));
		{
			std::ofstream corrupt(index, std::ios::binary | std::ios::trunc);
			corrupt << "{invalid-primary";
			PRESENTATION_CHECK(state, corrupt.good());
		}
		PresentationAutoSaveService backupReader;
		PRESENTATION_CHECK(state, backupReader.Start(root.wstring()));
		PRESENTATION_CHECK(state, backupReader.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		backupReader.CloseAndDrain();
		PRESENTATION_CHECK(state, LoadedRetainedPage(
			TakeCompletion(backupReader), true));
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestPendingFirstCommitRetainsDeletedSlideOnRetry(TestState& state)
	{
		const fs::path root = MakeRoot();
		auto twoPages = MakeTarget();
		twoPages.totalPages = 2;
		twoPages.slideIds = { 101, 202 };
		auto first = MakeSnapshot(twoPages, true);
		auto removed = first.canvases.front();
		removed.pageGuid = RetainedPageGuid();
		removed.pageIndex = 1;
		removed.pageNumber = 2;
		removed.slideId = 202;
		removed.strokes.front().style.fallbackRgb = 0x202020;
		first.canvases.push_back(std::move(removed));
		constexpr const char* session = "34343434-3434-4434-8434-343434343434";
		SetPresentationAutoSaveTestFaultInjection({ true, 0, session });
		PresentationAutoSaveService service;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ twoPages, 1,
			std::move(first) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::IoError);
		const fs::path index = root / L"presentation" / L"index.json";
		PRESENTATION_CHECK(state, !fs::exists(index) && CountUInkFiles(root) == 1);
		auto onePage = MakeTarget();
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ onePage, 2,
			MakeRetainedSnapshot(onePage, false) }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		PRESENTATION_CHECK(state, IndexKnowsSlides(index, { 101, 202 }));
		PresentationAutoSaveService fresh;
		PRESENTATION_CHECK(state, fresh.Start(root.wstring()));
		PRESENTATION_CHECK(state, fresh.SubmitLoad({ onePage }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		fresh.CloseAndDrain();
		PRESENTATION_CHECK(state, LoadedRetainedPage(
			TakeCompletion(fresh), false));
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestFallbackAndStableUseIndependentStorageTracks(TestState& state)
	{
		const fs::path root = MakeRoot();
		auto fallback = MakeFallbackTarget();
		fallback.totalPages = 2;
		auto stable = fallback;
		stable.bindingMode = Bridge::SlideBindingMode::StableSlideId;
		stable.slideIds = { 202, 101 }; // 旧 ordinal 与新 SlideID 不能互相猜测。
		stable.slideId = 202;
		stable.bindingRevision = 2;
		constexpr const char* session = "56565656-5656-4656-8656-565656565656";
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PresentationAutoSaveService service;
		for (std::uint64_t revision = 1; revision <= 2; ++revision)
		{
			PRESENTATION_CHECK(state, service.Start(root.wstring()));
			PRESENTATION_CHECK(state, service.SubmitSave({ fallback, revision,
				MakeTwoPageTrackSnapshot(fallback, false) }) ==
				PresentationPersistenceSubmitStatus::Accepted);
			service.CloseAndDrain();
			PRESENTATION_CHECK(state, TakeCompletion(service).status ==
				PresentationPersistenceStatus::Committed);
		}
		const fs::path base = root / L"presentation";
		const fs::path baseIndex = base / L"index.json";
		const fs::path baseBackup = base / L"index.json.bak";
		Json::Value mainDocument;
		Json::Value backupDocument;
		const bool indexesReadable = ReadIndexJson(baseIndex, mainDocument) &&
			ReadIndexJson(baseBackup, backupDocument);
		PRESENTATION_CHECK(state, indexesReadable);
		if (!indexesReadable)
		{
			std::error_code error;
			fs::remove_all(root, error);
			return;
		}
		const fs::path mainUInk = base /
			fs::path(mainDocument["entries"][0]["relativePath"].asString());
		const fs::path backupUInk = base /
			fs::path(backupDocument["entries"][0]["relativePath"].asString());
		const auto mainIndexBytes = ReadFileBytes(baseIndex);
		const auto backupIndexBytes = ReadFileBytes(baseBackup);
		const auto mainUInkBytes = ReadFileBytes(mainUInk);
		const auto backupUInkBytes = ReadFileBytes(backupUInk);
		PRESENTATION_CHECK(state, mainIndexBytes && backupIndexBytes &&
			mainUInkBytes && backupUInkBytes);

		PresentationLoadRequest stableMissing{ stable };
		stableMissing.slotGeneration = 41;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad(std::move(stableMissing)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto missing = TakeCompletion(service);
		PRESENTATION_CHECK(state, missing.status ==
			PresentationPersistenceStatus::NotFound &&
			missing.storageTrack == PresentationStorageTrack::SlideIdSidecar &&
			missing.slotGeneration == 41 && !missing.fileGuid);
		// 与旧轨撞 file/workspace GUID 必须拒绝，不能借旧 UInk 原位升级。
		PresentationSaveRequest collision{ stable, 1,
			MakeTwoPageTrackSnapshot(stable, false) };
		collision.slotGeneration = 41;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave(std::move(collision)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto rejected = TakeCompletion(service);
		PRESENTATION_CHECK(state, rejected.status ==
			PresentationPersistenceStatus::SourceChanged &&
			rejected.storageTrack == PresentationStorageTrack::SlideIdSidecar &&
			rejected.slotGeneration == 41 && rejected.fileGuid ==
				*ParseUInkGuid("11111111-1111-4111-8111-111111111111"));
		PRESENTATION_CHECK(state, ReadFileBytes(baseIndex) == mainIndexBytes &&
			ReadFileBytes(baseBackup) == backupIndexBytes &&
			ReadFileBytes(mainUInk) == mainUInkBytes &&
			ReadFileBytes(backupUInk) == backupUInkBytes);

		PresentationSaveRequest stableSave{ stable, 1,
			MakeTwoPageTrackSnapshot(stable, true) };
		stableSave.slotGeneration = 41;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave(std::move(stableSave)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto committed = TakeCompletion(service);
		PRESENTATION_CHECK(state, committed.status ==
			PresentationPersistenceStatus::Committed &&
			committed.storageTrack == PresentationStorageTrack::SlideIdSidecar &&
			committed.slotGeneration == 41 && committed.fileGuid ==
				*ParseUInkGuid("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"));
		const fs::path stableIndex = base / L"slide-id" / L"index.json";
		PRESENTATION_CHECK(state, fs::exists(stableIndex) &&
			IndexKnowsSlides(stableIndex, { 101, 202 }));
		PRESENTATION_CHECK(state, ReadFileBytes(baseIndex) == mainIndexBytes &&
			ReadFileBytes(baseBackup) == backupIndexBytes &&
			ReadFileBytes(mainUInk) == mainUInkBytes &&
			ReadFileBytes(backupUInk) == backupUInkBytes);
		PresentationAutoSaveService fresh;
		PRESENTATION_CHECK(state, fresh.Start(root.wstring()));
		PresentationLoadRequest stableLoad{ stable };
		stableLoad.slotGeneration = 41;
		PRESENTATION_CHECK(state, fresh.SubmitLoad(std::move(stableLoad)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		PresentationLoadRequest fallbackLoad{ fallback };
		fallbackLoad.slotGeneration = 31;
		PRESENTATION_CHECK(state, fresh.SubmitLoad(std::move(fallbackLoad)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		fresh.CloseAndDrain();
		PresentationPersistenceCompletion loaded;
		int stableLoaded = 0;
		int fallbackLoaded = 0;
		while (fresh.TryTakeCompletion(loaded))
		{
			const bool isStable = loaded.target.bindingMode ==
				Bridge::SlideBindingMode::StableSlideId;
			const auto& expected = isStable ? stable : fallback;
			const auto firstGuid = *ParseUInkGuid(isStable
				? "cccccccc-cccc-4ccc-8ccc-cccccccccccc"
				: "33333333-3333-4333-8333-333333333333");
			const auto secondGuid = *ParseUInkGuid(isStable
				? "dddddddd-dddd-4ddd-8ddd-dddddddddddd"
				: "44444444-4444-4444-8444-444444444444");
			const bool matched = loaded.status == PresentationPersistenceStatus::Loaded &&
				loaded.storageTrack == (isStable
					? PresentationStorageTrack::SlideIdSidecar
					: PresentationStorageTrack::Base) &&
				loaded.slotGeneration == (isStable ? 41u : 31u) &&
				loaded.fileGuid == *ParseUInkGuid(isStable
					? "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
					: "11111111-1111-4111-8111-111111111111") &&
				loaded.loadedSnapshot && loaded.loadedSnapshot->activeCanvases.size() == 2 &&
				loaded.loadedSnapshot->retainedCanvases.empty() &&
				loaded.loadedSnapshot->activeCanvases[0].pageGuid == firstGuid &&
				loaded.loadedSnapshot->activeCanvases[1].pageGuid == secondGuid &&
				loaded.loadedSnapshot->activeCanvases[0].slideId == expected.slideId &&
				loaded.loadedSnapshot->activeCanvases[0].strokes.size() == 1 &&
				loaded.loadedSnapshot->activeCanvases[1].strokes.size() == 1 &&
				loaded.loadedSnapshot->activeCanvases[0].strokes[0].style.fallbackRgb ==
					(isStable ? 0xaaaaaa : 0x111111) &&
				loaded.loadedSnapshot->activeCanvases[1].strokes[0].style.fallbackRgb ==
					(isStable ? 0xbbbbbb : 0x222222);
			PRESENTATION_CHECK(state, matched);
			if (isStable) ++stableLoaded;
			else ++fallbackLoaded;
		}
		PRESENTATION_CHECK(state, stableLoaded == 1 && fallbackLoaded == 1);
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestCrossTrackQueuedSavesDoNotReplaceEachOther(TestState& state)
	{
		const fs::path root = MakeRoot();
		auto fallback = MakeFallbackTarget();
		auto stable = fallback;
		stable.bindingMode = Bridge::SlideBindingMode::StableSlideId;
		stable.slideIds = { 101 };
		stable.slideId = 101;
		const auto blocker = MakeSecondTarget();
		constexpr const char* session = "67676767-6767-4767-8767-676767676767";
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PresentationAutoSaveService service;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ fallback, 1,
			MakeSnapshot(fallback, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HANDLE resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		PRESENTATION_CHECK(state, ready && resume);
		PresentationAutoSaveTestFaultInjection faults;
		faults.sessionIdOverride = session;
		faults.afterUInkCommittedEvent = ready;
		faults.continueIndexCommitEvent = resume;
		SetPresentationAutoSaveTestFaultInjection(faults);
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		auto blockerSnapshot = MakeSnapshot(blocker, false);
		blockerSnapshot.fileGuid = *ParseUInkGuid(
			"77777777-7777-4777-8777-777777777777");
		blockerSnapshot.workspaceGuid = *ParseUInkGuid(
			"88888888-8888-4888-8888-888888888888");
		blockerSnapshot.canvases.front().pageGuid = *ParseUInkGuid(
			"99999999-9999-4999-8999-999999999999");
		PRESENTATION_CHECK(state, service.SubmitSave({ blocker, 1,
			std::move(blockerSnapshot) }) == PresentationPersistenceSubmitStatus::Accepted);
		PRESENTATION_CHECK(state, ready &&
			WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0);
		PresentationSaveRequest fallbackTail{ fallback, 2,
			MakeSnapshot(fallback, false) };
		fallbackTail.slotGeneration = 31;
		PresentationSaveRequest stableTail{ stable, 1,
			MakeSecondSnapshot(stable, true) };
		stableTail.slotGeneration = 41;
		PRESENTATION_CHECK(state, service.SubmitSave(std::move(fallbackTail)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		PRESENTATION_CHECK(state, service.SubmitSave(std::move(stableTail)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		if (resume) SetEvent(resume);
		service.CloseAndDrain();
		if (ready) CloseHandle(ready);
		if (resume) CloseHandle(resume);
		int blockerCommitted = 0;
		int fallbackCommitted = 0;
		int stableCommitted = 0;
		PresentationPersistenceCompletion completion;
		while (service.TryTakeCompletion(completion))
		{
			if (completion.target.key == blocker.key)
				blockerCommitted += completion.status == PresentationPersistenceStatus::Committed;
			else if (completion.target.bindingMode == Bridge::SlideBindingMode::StableSlideId)
				stableCommitted += completion.status == PresentationPersistenceStatus::Committed &&
					completion.storageTrack == PresentationStorageTrack::SlideIdSidecar &&
					completion.slotGeneration == 41;
			else fallbackCommitted += completion.status ==
				PresentationPersistenceStatus::Committed &&
				completion.storageTrack == PresentationStorageTrack::Base &&
				completion.slotGeneration == 31;
		}
		PRESENTATION_CHECK(state, blockerCommitted == 1 &&
			fallbackCommitted == 1 && stableCommitted == 1);
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestSidecarPendingAndForeignSessionsStayIsolated(TestState& state)
	{
		const fs::path root = MakeRoot();
		auto fallback = MakeFallbackTarget();
		auto stable = fallback;
		stable.bindingMode = Bridge::SlideBindingMode::StableSlideId;
		stable.slideIds = { 101 };
		stable.slideId = 101;
		constexpr const char* session = "78787878-7878-4787-8787-787878787878";
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PresentationAutoSaveService service;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ fallback, 1,
			MakeSnapshot(fallback, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		const fs::path baseIndex = root / L"presentation" / L"index.json";
		const auto baseBytes = ReadFileBytes(baseIndex);
		SetPresentationAutoSaveTestFaultInjection({ true, 0, session });
		PresentationSaveRequest failed{ stable, 1, MakeSecondSnapshot(stable, true) };
		failed.slotGeneration = 41;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave(std::move(failed)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto failedCompletion = TakeCompletion(service);
		PRESENTATION_CHECK(state, failedCompletion.status ==
			PresentationPersistenceStatus::IoError &&
			failedCompletion.storageTrack == PresentationStorageTrack::SlideIdSidecar &&
			failedCompletion.slotGeneration == 41 && failedCompletion.fileGuid ==
				*ParseUInkGuid("44444444-4444-4444-8444-444444444444") &&
			ReadFileBytes(baseIndex) == baseBytes);
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PresentationLoadRequest pendingLoad{ stable };
		pendingLoad.slotGeneration = 41;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad(std::move(pendingLoad)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto sameService = TakeCompletion(service);
		PRESENTATION_CHECK(state, sameService.status ==
			PresentationPersistenceStatus::Loaded &&
			sameService.storageTrack == PresentationStorageTrack::SlideIdSidecar &&
			sameService.slotGeneration == 41 && sameService.fileGuid ==
				*ParseUInkGuid("44444444-4444-4444-8444-444444444444"));
		PresentationAutoSaveService fresh;
		PRESENTATION_CHECK(state, fresh.Start(root.wstring()));
		PRESENTATION_CHECK(state, fresh.SubmitLoad({ stable }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		fresh.CloseAndDrain();
		const auto freshUncommitted = TakeCompletion(fresh);
		PRESENTATION_CHECK(state, freshUncommitted.status ==
			PresentationPersistenceStatus::NotFound &&
			freshUncommitted.storageTrack == PresentationStorageTrack::SlideIdSidecar);
		PresentationSaveRequest oldTrack{ fallback, 2,
			MakeSnapshot(fallback, false) };
		oldTrack.slotGeneration = 31;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave(std::move(oldTrack)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto oldCommitted = TakeCompletion(service);
		PRESENTATION_CHECK(state, oldCommitted.status ==
			PresentationPersistenceStatus::Committed &&
			oldCommitted.storageTrack == PresentationStorageTrack::Base &&
			oldCommitted.slotGeneration == 31);
		PresentationSaveRequest retry{ stable, 2,
			MakeSecondSnapshot(stable, false) };
		retry.slotGeneration = 41;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave(std::move(retry)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto sidecarCommitted = TakeCompletion(service);
		PRESENTATION_CHECK(state, sidecarCommitted.status ==
			PresentationPersistenceStatus::Committed &&
			sidecarCommitted.storageTrack == PresentationStorageTrack::SlideIdSidecar &&
			sidecarCommitted.slotGeneration == 41 &&
			fs::exists(root / L"presentation" / L"slide-id" / L"index.json"));
		PresentationAutoSaveService foreign;
		SetPresentationAutoSaveTestFaultInjection({ false, 0,
			"89898989-8989-4989-8989-898989898989" });
		PRESENTATION_CHECK(state, foreign.Start(root.wstring()));
		PRESENTATION_CHECK(state, foreign.SubmitLoad({ fallback }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		PRESENTATION_CHECK(state, foreign.SubmitLoad({ stable }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		foreign.CloseAndDrain();
		int rejected = 0;
		PresentationPersistenceCompletion completion;
		while (foreign.TryTakeCompletion(completion))
			rejected += completion.status ==
				PresentationPersistenceStatus::CrossProcessConflictDeferred &&
				!completion.fileGuid &&
				completion.storageTrack == (completion.target.bindingMode ==
					Bridge::SlideBindingMode::StableSlideId
						? PresentationStorageTrack::SlideIdSidecar
						: PresentationStorageTrack::Base);
		PRESENTATION_CHECK(state, rejected == 2);
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestPendingGenerationCannotReuseIdentity(TestState& state)
	{
		const fs::path root = MakeRoot();
		const auto target = MakeTarget();
		constexpr const char* session = "81818181-8181-4181-8181-818181818181";
		PresentationAutoSaveService service;
		SetPresentationAutoSaveTestFaultInjection({ true, 0, session });
		PresentationSaveRequest first{ target, 1, MakeSnapshot(target, true) };
		first.slotGeneration = 11;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave(std::move(first)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto firstFailure = TakeCompletion(service);
		const fs::path index = root / L"presentation" / L"index.json";
		const auto pendingFile = SingleUInkFile(root);
		const auto pendingBytes = pendingFile
			? ReadFileBytes(*pendingFile) : std::nullopt;
		PRESENTATION_CHECK(state, firstFailure.status ==
			PresentationPersistenceStatus::IoError &&
			firstFailure.storageTrack == PresentationStorageTrack::Base &&
			firstFailure.slotGeneration == 11 &&
			!fs::exists(index) && pendingFile && pendingBytes);

		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PresentationSaveRequest second{ target, 2, MakeSnapshot(target, false) };
		second.slotGeneration = 12;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave(std::move(second)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto rejected = TakeCompletion(service);
		PRESENTATION_CHECK(state, rejected.status ==
			PresentationPersistenceStatus::SourceChanged &&
			rejected.storageTrack == PresentationStorageTrack::Base &&
			rejected.slotGeneration == 12);
		PRESENTATION_CHECK(state, !fs::exists(index) &&
			CountUInkFiles(root) == 1 && pendingFile && pendingBytes &&
			ReadFileBytes(*pendingFile) == pendingBytes);

		PresentationLoadRequest pendingLoad{ target };
		pendingLoad.slotGeneration = 11;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad(std::move(pendingLoad)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto oldPending = TakeCompletion(service);
		PRESENTATION_CHECK(state, oldPending.status ==
			PresentationPersistenceStatus::Loaded &&
			oldPending.slotGeneration == 11 && oldPending.loadedSnapshot &&
			oldPending.loadedSnapshot->canvases.size() == 1 &&
			oldPending.loadedSnapshot->canvases.front().strokes.size() == 1);
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestExistingStableBaseKeepsPriorityAndDuplicateTrackFailsClosed(TestState& state)
	{
		const fs::path root = MakeRoot();
		const auto stable = MakeTarget();
		auto fallback = stable;
		fallback.bindingMode = Bridge::SlideBindingMode::PageIndexFallback;
		fallback.slideIds.clear();
		fallback.slideId.reset();
		constexpr const char* session = "90909090-9090-4090-8090-909090909090";
		SetPresentationAutoSaveTestFaultInjection({ false, 0, session });
		PresentationAutoSaveService service;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ stable, 1,
			MakeSnapshot(stable, true) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		const fs::path baseIndex = root / L"presentation" / L"index.json";
		const auto baseBytes = ReadFileBytes(baseIndex);
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ stable }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		PRESENTATION_CHECK(state, service.SubmitLoad({ fallback }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PresentationPersistenceCompletion completion;
		int stableBase = 0;
		int fallbackMissing = 0;
		while (service.TryTakeCompletion(completion))
		{
			if (completion.target.bindingMode == Bridge::SlideBindingMode::StableSlideId)
				stableBase += completion.status == PresentationPersistenceStatus::Loaded &&
					completion.storageTrack == PresentationStorageTrack::Base &&
					completion.loadedSnapshot &&
					completion.loadedSnapshot->canvases.front().strokes.size() == 1;
			else fallbackMissing += completion.status ==
				PresentationPersistenceStatus::NotFound &&
				completion.storageTrack == PresentationStorageTrack::PageIndexSidecar;
		}
		PRESENTATION_CHECK(state, stableBase == 1 && fallbackMissing == 1);
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ fallback, 1,
			MakeSecondSnapshot(fallback, false) }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto saved = TakeCompletion(service);
		PRESENTATION_CHECK(state, saved.status ==
			PresentationPersistenceStatus::Committed &&
			saved.storageTrack == PresentationStorageTrack::PageIndexSidecar &&
			ReadFileBytes(baseIndex) == baseBytes &&
			fs::exists(root / L"presentation" / L"page-index" / L"index.json"));
		PresentationAutoSaveService fresh;
		PRESENTATION_CHECK(state, fresh.Start(root.wstring()));
		PRESENTATION_CHECK(state, fresh.SubmitLoad({ stable }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		PRESENTATION_CHECK(state, fresh.SubmitLoad({ fallback }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		fresh.CloseAndDrain();
		int loaded = 0;
		while (fresh.TryTakeCompletion(completion))
			loaded += completion.status == PresentationPersistenceStatus::Loaded &&
				completion.storageTrack == (completion.target.bindingMode ==
					Bridge::SlideBindingMode::StableSlideId
						? PresentationStorageTrack::Base
						: PresentationStorageTrack::PageIndexSidecar);
		PRESENTATION_CHECK(state, loaded == 2);
		// 同 source+mode 在 base 与 sidecar 各出现一次时，不猜测权威版本。
		Json::Value duplicate;
		PRESENTATION_CHECK(state, ReadIndexJson(baseIndex, duplicate));
		const fs::path duplicateIndex = root / L"presentation" /
			L"slide-id" / L"index.json";
		std::error_code error;
		fs::create_directories(duplicateIndex.parent_path(), error);
		PRESENTATION_CHECK(state, !error && WriteIndexJson(duplicateIndex, duplicate));
		PRESENTATION_CHECK(state, fresh.Start(root.wstring()));
		PRESENTATION_CHECK(state, fresh.SubmitLoad({ stable }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		fresh.CloseAndDrain();
		const auto ambiguous = TakeCompletion(fresh);
		PRESENTATION_CHECK(state, ambiguous.status ==
			PresentationPersistenceStatus::IoError &&
			ambiguous.storageTrack == PresentationStorageTrack::Unresolved &&
			!ambiguous.fileGuid);
		fs::remove_all(root, error);
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

		// 同路径重新取得 SlideID 时新轨保持独立，不能沿旧页序显示 fallback 墨迹。
		Bridge::PresentationTarget stable = otherBinding;
		stable.bindingMode = Bridge::SlideBindingMode::StableSlideId;
		stable.slideIds = { 101 };
		stable.slideId = 101;
		const fs::path baseIndex = root / L"presentation" / L"index.json";
		const auto previousBaseIndex = ReadFileBytes(baseIndex);
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ stable }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::NotFound);
		auto stableSnapshot = MakeSecondSnapshot(stable, false);
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ stable, 3,
			std::move(stableSnapshot) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, TakeCompletion(service).status ==
			PresentationPersistenceStatus::Committed);
		PRESENTATION_CHECK(state, previousBaseIndex &&
			ReadFileBytes(baseIndex) == previousBaseIndex &&
			CountUInkFiles(root) == 2 &&
			fs::exists(root / L"presentation" / L"slide-id" / L"index.json"));
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ stable }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto independent = TakeCompletion(service);
		PRESENTATION_CHECK(state, independent.status ==
			PresentationPersistenceStatus::Loaded && independent.loadedSnapshot &&
			independent.loadedSnapshot->workspaceType == 2 &&
			independent.loadedSnapshot->canvases.front().slideId == 101 &&
			independent.loadedSnapshot->canvases.front().pageGuid ==
				*ParseUInkGuid("66666666-6666-4666-8666-666666666666"));

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
		PRESENTATION_CHECK(state, CountUInkFiles(root) == 2);
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

	void TestEndScreenUInkRoundTrip(TestState& state)
	{
		const auto target = MakeTarget();
		auto snapshot = MakeSnapshot(target, true);
		snapshot.currentPageIndex = 1;
		snapshot.activeCanvases = std::move(snapshot.canvases);
		Draw3UInkCanvasSnapshot end;
		end.pageGuid = *ParseUInkGuid("77777777-7777-4777-8777-777777777777");
		end.pageIndex = 1;
		end.pageNumber = 2;
		end.extra = MakeInkeysEndScreenExtra(
			Draw3UInkImportBindingMode::StableSlideId);
		Draw3UInkStrokeSnapshot first;
		first.style.kind = Draw3UInkStrokeKind::Pen;
		first.style.fallbackRgb = 0xabcdef;
		first.points = { { 11.0f, 12.0f, 4.0f }, { 13.0f, 14.0f, 4.0f } };
		Draw3UInkClearSnapshot clear;
		clear.undoId = 1;
		Draw3UInkStrokeSnapshot last = first;
		last.undoId = 2;
		last.style.fallbackRgb = 0x987654;
		end.operations = { first, clear, last };
		snapshot.activeCanvases.push_back(end);

		const auto exported = ExportDraw3SnapshotToUInk(snapshot);
		PRESENTATION_CHECK(state, exported.document &&
			exported.document->canvases.size() == 2);
		if (!exported.document) return;
		const auto encoded = EncodeUInkDocument(*exported.document);
		PRESENTATION_CHECK(state, encoded.status == UInkEncodeStatus::Success);
		if (encoded.status != UInkEncodeStatus::Success) return;
		const auto decoded = DecodeUInk(encoded.bytes);
		PRESENTATION_CHECK(state, decoded.status == UInkReadStatus::Complete &&
			decoded.document && !decoded.provenance.usedTemporaryIdentity);
		if (!decoded.document) return;
		Draw3UInkImportExpectation expected;
		expected.fileGuid = snapshot.fileGuid;
		expected.hostId = FormatPresentationKey(target.key);
		expected.bindingMode = Draw3UInkImportBindingMode::StableSlideId;
		expected.slideIds = target.slideIds;
		expected.pageCount = target.totalPages;
		expected.allowEndScreen = true;
		const auto imported = ImportApplicationOwnedPresentation(*decoded.document, expected);
		PRESENTATION_CHECK(state, imported.status == Draw3UInkImportStatus::Success &&
			imported.snapshot && imported.snapshot->activeCanvases.size() == 2);
		if (!imported.snapshot || imported.snapshot->activeCanvases.size() != 2) return;
		PRESENTATION_CHECK(state, imported.snapshot->currentPageIndex == 1 &&
			imported.snapshot->activeCanvases[0].pageGuid !=
				imported.snapshot->activeCanvases[1].pageGuid &&
			imported.snapshot->activeCanvases[0].strokes.size() == 1 &&
			imported.snapshot->activeCanvases[1].pageGuid == end.pageGuid &&
			imported.snapshot->activeCanvases[1].strokes.size() == 1 &&
			imported.snapshot->activeCanvases[1].strokes[0].style.fallbackRgb == 0x987654 &&
			imported.snapshot->activeCanvases[1].operations.size() == 3 &&
			InkeysPageKind(imported.snapshot->activeCanvases[1].extra) ==
				UInkInkeysPageKind::EndScreen);

		// 增页后仍按真实 SlideID 投影，结束页 pageGuid 移到新的内部 N。
		auto grown = expected;
		grown.slideIds = { 202, 101 };
		grown.pageCount = 2;
		const auto remapped = ImportApplicationOwnedPresentation(*decoded.document, grown);
		PRESENTATION_CHECK(state, remapped.status == Draw3UInkImportStatus::Success &&
			remapped.snapshot && remapped.snapshot->currentPageIndex == 2 &&
			remapped.snapshot->activeCanvases.size() == 2 &&
			remapped.snapshot->activeCanvases.back().pageIndex == 2 &&
			remapped.snapshot->activeCanvases.back().pageGuid == end.pageGuid);

		auto beforeDeletion = snapshot;
		Draw3UInkCanvasSnapshot removed = beforeDeletion.activeCanvases.front();
		removed.pageGuid = *ParseUInkGuid("99999999-9999-4999-8999-999999999999");
		removed.pageIndex = 1;
		removed.pageNumber = 2;
		removed.slideId = 202;
		removed.strokes.front().style.fallbackRgb = 0x202020;
		beforeDeletion.activeCanvases.insert(beforeDeletion.activeCanvases.begin() + 1, removed);
		beforeDeletion.currentPageIndex = 2;
		const auto deletedExport = ExportDraw3SnapshotToUInk(beforeDeletion);
		PRESENTATION_CHECK(state, deletedExport.document.has_value());
		if (deletedExport.document)
		{
			const auto deletedBytes = EncodeUInkDocument(*deletedExport.document);
			const auto deletedRead = DecodeUInk(deletedBytes.bytes);
			PRESENTATION_CHECK(state, deletedBytes.status == UInkEncodeStatus::Success &&
				deletedRead.document.has_value());
			if (deletedRead.document)
			{
				auto shrunk = expected;
				shrunk.knownSlideIds = { 101, 202 };
				const auto restored = ImportApplicationOwnedPresentation(*deletedRead.document, shrunk);
				PRESENTATION_CHECK(state, restored.status == Draw3UInkImportStatus::Success &&
					restored.snapshot && restored.snapshot->currentPageIndex == 1 &&
					restored.snapshot->activeCanvases.size() == 2 &&
					restored.snapshot->activeCanvases.back().pageGuid == end.pageGuid &&
					restored.snapshot->retainedCanvases.size() == 1 &&
					restored.snapshot->retainedCanvases.front().pageGuid == removed.pageGuid &&
					restored.snapshot->retainedCanvases.front().slideId == 202 &&
					restored.snapshot->retainedCanvases.front().strokes.front().style.fallbackRgb ==
						0x202020);
				auto untrusted = shrunk;
				untrusted.knownSlideIds = { 101 };
				PRESENTATION_CHECK(state, ImportApplicationOwnedPresentation(
					*deletedRead.document, untrusted).status == Draw3UInkImportStatus::TopologyMismatch);
			}
		}

		auto legacy = MakeSnapshot(target, true);
		const auto legacyExport = ExportDraw3SnapshotToUInk(legacy);
		PRESENTATION_CHECK(state, legacyExport.document.has_value());
		if (legacyExport.document)
		{
			const auto legacyBytes = EncodeUInkDocument(*legacyExport.document);
			const auto legacyRead = DecodeUInk(legacyBytes.bytes);
			PRESENTATION_CHECK(state, legacyBytes.status == UInkEncodeStatus::Success &&
				legacyRead.status == UInkReadStatus::Complete && legacyRead.document &&
				ImportApplicationOwnedPresentation(*legacyRead.document, expected).status ==
					Draw3UInkImportStatus::Success);
		}
		auto disallowed = expected;
		disallowed.allowEndScreen = false;
		PRESENTATION_CHECK(state, ImportApplicationOwnedPresentation(
			*decoded.document, disallowed).status == Draw3UInkImportStatus::TopologyMismatch);

		auto unmarked = *decoded.document;
		unmarked.canvases[1].extra = MakeInkeysBindingExtra(
			Draw3UInkImportBindingMode::StableSlideId);
		PRESENTATION_CHECK(state, EncodeUInkDocument(unmarked).status ==
			UInkEncodeStatus::InvalidModel);
		PRESENTATION_CHECK(state, ImportApplicationOwnedPresentation(unmarked, expected).status ==
			Draw3UInkImportStatus::TopologyMismatch);
		auto duplicateMarker = *decoded.document;
		duplicateMarker.canvases[1].extra->push_back(
			(*duplicateMarker.canvases[1].extra)[1]);
		PRESENTATION_CHECK(state, EncodeUInkDocument(duplicateMarker).status ==
			UInkEncodeStatus::InvalidModel);
		PRESENTATION_CHECK(state, ImportApplicationOwnedPresentation(
			duplicateMarker, expected).status == Draw3UInkImportStatus::TopologyMismatch);
		auto duplicate = *decoded.document;
		duplicate.canvases.push_back(duplicate.canvases[1]);
		duplicate.canvases.back().pageGuid =
			*ParseUInkGuid("88888888-8888-4888-8888-888888888888");
		duplicate.canvases.back().pageIndex = 2;
		duplicate.canvases.back().pageNumber = 3;
		duplicate.header.pageNum = 3;
		PRESENTATION_CHECK(state, ImportApplicationOwnedPresentation(duplicate, expected).status ==
			Draw3UInkImportStatus::TopologyMismatch);
		auto forged = *decoded.document;
		forged.canvases[1].slideId = 202;
		PRESENTATION_CHECK(state, EncodeUInkDocument(forged).status ==
			UInkEncodeStatus::InvalidModel);
		PRESENTATION_CHECK(state, ImportApplicationOwnedPresentation(forged, expected).status ==
			Draw3UInkImportStatus::TopologyMismatch);

		const auto fallbackTarget = MakeFallbackTarget();
		auto fallback = MakeSnapshot(fallbackTarget, true);
		fallback.currentPageIndex = 1;
		fallback.activeCanvases = std::move(fallback.canvases);
		Draw3UInkCanvasSnapshot fallbackEnd = end;
		fallbackEnd.extra = MakeInkeysEndScreenExtra(
			Draw3UInkImportBindingMode::PageIndexFallback);
		fallback.activeCanvases.push_back(std::move(fallbackEnd));
		const auto fallbackExport = ExportDraw3SnapshotToUInk(fallback);
		PRESENTATION_CHECK(state, fallbackExport.document.has_value());
		if (fallbackExport.document)
		{
			const auto fallbackBytes = EncodeUInkDocument(*fallbackExport.document);
			const auto fallbackRead = DecodeUInk(fallbackBytes.bytes);
			Draw3UInkImportExpectation fallbackExpected;
			fallbackExpected.fileGuid = fallback.fileGuid;
			fallbackExpected.hostId = FormatPresentationKey(fallbackTarget.key);
			fallbackExpected.bindingMode = Draw3UInkImportBindingMode::PageIndexFallback;
			fallbackExpected.pageCount = 1;
			fallbackExpected.allowEndScreen = true;
			PRESENTATION_CHECK(state, fallbackBytes.status == UInkEncodeStatus::Success &&
				fallbackRead.document && ImportApplicationOwnedPresentation(
					*fallbackRead.document, fallbackExpected).status ==
						Draw3UInkImportStatus::Success);
		}
	}

	void TestWorkerExceptionBecomesCompletion(TestState& state)
	{
		const fs::path root = MakeRoot();
		const auto target = MakeTarget();
		PresentationAutoSaveService service;
		SetPresentationAutoSaveTestFaultInjection({ false, 0,
			"77777777-7777-4777-8777-777777777777", true });
		PresentationSaveRequest save{ target, 1, MakeSnapshot(target, true) };
		save.slotGeneration = 77;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave(std::move(save)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto completion = TakeCompletion(service);
		PRESENTATION_CHECK(state, completion.operation ==
			PresentationPersistenceOperation::Save &&
			completion.status == PresentationPersistenceStatus::IoError &&
			completion.mutationRevision == 1 && completion.slotGeneration == 77 &&
			completion.storageTrack == PresentationStorageTrack::Unresolved &&
			completion.fileGuid ==
				*ParseUInkGuid("11111111-1111-4111-8111-111111111111"));
		PresentationLoadRequest load{ target };
		load.slotGeneration = 88;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad(std::move(load)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto loadFailure = TakeCompletion(service);
		PRESENTATION_CHECK(state, loadFailure.operation ==
			PresentationPersistenceOperation::Load &&
			loadFailure.status == PresentationPersistenceStatus::IoError &&
			loadFailure.slotGeneration == 88 &&
			loadFailure.storageTrack == PresentationStorageTrack::Unresolved &&
			!loadFailure.fileGuid);
		const UInkGuid pageGuid = *ParseUInkGuid(
			"33333333-3333-4333-8333-333333333333");
		PresentationLoadRequest interval{ target,
			PresentationLoadKind::PreviousInterval, pageGuid, 7 };
		interval.slotGeneration = 89;
		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad(std::move(interval)) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		const auto intervalFailure = TakeCompletion(service);
		PRESENTATION_CHECK(state, intervalFailure.operation ==
			PresentationPersistenceOperation::Load &&
			intervalFailure.status == PresentationPersistenceStatus::IoError &&
			intervalFailure.loadKind == PresentationLoadKind::PreviousInterval &&
			intervalFailure.pageGuid == pageGuid &&
			intervalFailure.intervalOrdinal == 7 &&
			intervalFailure.slotGeneration == 89 &&
			intervalFailure.storageTrack == PresentationStorageTrack::Unresolved &&
			!intervalFailure.fileGuid);
		std::error_code error;
		fs::remove_all(root, error);
	}

	void TestClearIntervalsPersistAndLoad(TestState& state)
	{
		const fs::path root = MakeRoot();
		const auto target = MakeTarget();
		PresentationAutoSaveService service;
		SetPresentationAutoSaveTestFaultInjection({ false, 0,
			"89898989-8989-4989-8989-898989898989" });
		auto first = MakeSnapshot(target, true);
		const UInkGuid pageGuid = first.canvases.front().pageGuid;
		auto second = MakeSnapshot(target, true);
		second.canvases.front().intervalOrdinal = 1;
		second.canvases.front().strokes.front().style.fallbackRgb = 0x223344;
		auto current = MakeSnapshot(target, true);
		current.canvases.front().intervalOrdinal = 2;
		current.canvases.front().strokes.front().style.fallbackRgb = 0x334455;

		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 1,
			std::move(first), pageGuid, 0 }) == PresentationPersistenceSubmitStatus::Accepted);
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 2,
			std::move(second), pageGuid, 1 }) == PresentationPersistenceSubmitStatus::Accepted);
		PRESENTATION_CHECK(state, service.SubmitSave({ target, 3,
			std::move(current) }) == PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		PRESENTATION_CHECK(state, service.Diagnostics().committed == 3);
		PRESENTATION_CHECK(state, CountUInkFiles(root) == 3);

		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ target }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		auto loaded = TakeCompletion(service);
		PRESENTATION_CHECK(state, loaded.status == PresentationPersistenceStatus::Loaded &&
			loaded.loadedSnapshot && loaded.loadedSnapshot->canvases.front().intervalOrdinal == 2 &&
			loaded.loadedSnapshot->canvases.front().strokes.front().style.fallbackRgb == 0x334455);

		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ target,
			PresentationLoadKind::PreviousInterval, pageGuid, 1 }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		loaded = TakeCompletion(service);
		PRESENTATION_CHECK(state, loaded.status == PresentationPersistenceStatus::Loaded &&
			loaded.loadKind == PresentationLoadKind::PreviousInterval &&
			loaded.intervalOrdinal == 1 && loaded.loadedSnapshot &&
			loaded.loadedSnapshot->canvases.front().strokes.front().style.fallbackRgb == 0x223344);

		PRESENTATION_CHECK(state, service.Start(root.wstring()));
		PRESENTATION_CHECK(state, service.SubmitLoad({ target,
			PresentationLoadKind::PreviousInterval, pageGuid, 0 }) ==
			PresentationPersistenceSubmitStatus::Accepted);
		service.CloseAndDrain();
		loaded = TakeCompletion(service);
		PRESENTATION_CHECK(state, loaded.status == PresentationPersistenceStatus::Loaded &&
			loaded.intervalOrdinal == 0 && loaded.loadedSnapshot &&
			loaded.loadedSnapshot->canvases.front().strokes.front().style.fallbackRgb == 0x123456);
		std::error_code error;
		fs::remove_all(root, error);
	}
}

int RunPresentationAutoSaveAtomicChild(const wchar_t* root,
	const wchar_t* readyEventName, const wchar_t* resumeEventName)
{
	if (!root || !readyEventName || !resumeEventName) return 2;
	HANDLE ready = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE,
		FALSE, readyEventName);
	HANDLE resume = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE,
		FALSE, resumeEventName);
	if (!ready || !resume)
	{
		if (ready) CloseHandle(ready);
		if (resume) CloseHandle(resume);
		return 2;
	}
	PresentationAutoSaveTestFaultInjection faults;
	faults.sessionIdOverride = "edededed-eded-4ded-8ded-edededededed";
	faults.afterUInkCommittedEvent = ready;
	faults.continueIndexCommitEvent = resume;
	SetPresentationAutoSaveTestFaultInjection(faults);
	PresentationAutoSaveService service;
	const auto target = MakeTarget();
	const bool started = service.Start(root);
	const auto submitted = started ? service.SubmitSave({ target, 2,
		MakeSnapshot(target, false) }) : PresentationPersistenceSubmitStatus::Closed;
	service.CloseAndDrain();
	const auto completion = TakeCompletion(service);
	CloseHandle(ready);
	CloseHandle(resume);
	ResetPresentationAutoSaveTestFaultInjection();
	return started && submitted == PresentationPersistenceSubmitStatus::Accepted &&
		completion.status == PresentationPersistenceStatus::Committed ? 0 : 2;
}

int RunPresentationUInkRoundTripTests()
{
	TestState state;
	TestStrictPresentationImporter(state);
	TestEndScreenUInkRoundTrip(state);
	return state.failures;
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
	PRESENTATION_CHECK(state, ShouldReleasePresentationClearFallback(2, 1));
	PRESENTATION_CHECK(state, !ShouldReleasePresentationClearFallback(2, 0));
	PRESENTATION_CHECK(state, !ShouldReleasePresentationClearFallback(
		UINT32_MAX, UINT32_MAX));
	TestSaveLoadAndClearOverwrite(state);
	TestStrictPresentationImporter(state);
	TestEndScreenUInkRoundTrip(state);
	TestIndexFailureRecoveryAndForeignConflict(state);
	TestCorruptIndexFallbackAndIsolation(state);
	TestInterruptedSecondCommitKeepsPreviousVersion(state);
	TestSecondCommitBackupStillMatchesPreviousUInk(state);
	TestLegacyIndexMigratesWithoutReplacingLegacyFile(state);
	TestVersionPathsRejectUntrustedVariants(state);
	TestIndexRejectsCaseAliasedFileIdentities(state);
	TestUppercaseLegacyGuidCanSaveCanonicalVersion(state, false, false);
	TestUppercaseLegacyGuidCanSaveCanonicalVersion(state, true, false);
	TestUppercaseLegacyGuidCanSaveCanonicalVersion(state, false, true);
	TestUppercaseLegacyGuidCanSaveCanonicalVersion(state, true, true);
	TestPublishedVersionsAndUnknownFileAreRetained(state);
	TestProcessKillBeforeIndexSwitchKeepsPreviousVersion(state);
	TestFirstRetainedCommitKeepsKnownSlideAndEndScreen(state);
	TestPendingFirstCommitRetainsDeletedSlideOnRetry(state);
	TestFallbackAndStableUseIndependentStorageTracks(state);
	TestCrossTrackQueuedSavesDoNotReplaceEachOther(state);
	TestSidecarPendingAndForeignSessionsStayIsolated(state);
	TestPendingGenerationCannotReuseIdentity(state);
	TestExistingStableBaseKeepsPriorityAndDuplicateTrackFailsClosed(state);
	TestRootChangeDropsPendingIndexState(state);
	TestPageIndexFallbackOverwrite(state);
	TestRestartDropsStaleCompletions(state);
	TestMultiplePresentationsStayIsolated(state);
	TestLatestWinsPendingSave(state);
	TestConcurrentWritersShareCaseFoldedMutex(state);
	TestWorkerExceptionBecomesCompletion(state);
	TestClearIntervalsPersistAndLoad(state);
	ResetPresentationAutoSaveTestFaultInjection();
	return state.failures;
}
