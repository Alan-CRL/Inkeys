#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <json/json.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Inkeys.Drawing.Draw3.auto_save;
import draw3.uink_file;

namespace
{
	using namespace Inkeys::Drawing::Draw3;
	using namespace draw3::uink;
	namespace fs = std::filesystem;

	struct TestState
	{
		int failures = 0;

		void Check(bool condition, const char* expression, int line)
		{
			if (condition) return;
			++failures;
			std::cerr << "Desktop AutoSave FAILED line " << line << ": " <<
				expression << std::endl;
		}
	};

#define AUTOSAVE_CHECK(state, expression) \
	(state).Check(!!(expression), #expression, __LINE__)

	std::wstring WidenAscii(const std::string& value)
	{
		return std::wstring(value.begin(), value.end());
	}

	UInkGuid Guid(const char* value)
	{
		const std::optional<UInkGuid> parsed = ParseUInkGuid(value);
		return parsed ? *parsed : UInkGuid{};
	}

	class TempDirectory
	{
	public:
		TempDirectory()
		{
			const std::optional<UInkGuid> guid = CreateUInkGuid();
			if (!guid) return;
			path_ = fs::temp_directory_path() /
				(L"Inkeys-DesktopAutoSave-" + WidenAscii(FormatUInkGuid(*guid)));
			std::error_code error;
			valid_ = fs::create_directories(path_, error) && !error;
		}

		~TempDirectory()
		{
			// 测试只清理自己创建的唯一临时目录，不代表产品历史保留策略。
			std::error_code error;
			if (valid_) fs::remove_all(path_, error);
		}

		[[nodiscard]] bool IsValid() const noexcept { return valid_; }
		[[nodiscard]] fs::path Child(const wchar_t* name) const
		{
			return path_ / name;
		}

	private:
		fs::path path_;
		bool valid_ = false;
	};

	class FaultScope
	{
	public:
		explicit FaultScope(DesktopAutoSaveTestFaultInjection faults)
		{
			SetDesktopAutoSaveTestFaultInjection(faults);
		}
		~FaultScope() { ResetDesktopAutoSaveTestFaultInjection(); }
		FaultScope(const FaultScope&) = delete;
		FaultScope& operator=(const FaultScope&) = delete;
	};

	Draw3UInkExportSnapshot MakeSnapshot(const char* fileGuid, float seed)
	{
		Draw3UInkExportSnapshot snapshot;
		snapshot.fileGuid = Guid(fileGuid);
		snapshot.workspaceGuid = Guid("dddddddd-0000-4000-8000-000000000001");
		snapshot.workspaceName = "Desktop";
		snapshot.dpiScale = 1.25f;
		snapshot.assignedIndependentUndoGroups = true;

		Draw3UInkCanvasSnapshot canvas;
		canvas.pageGuid = Guid("eeeeeeee-0000-4000-8000-000000000001");
		canvas.pageIndex = 0;
		canvas.pageNumber = 1;
		canvas.viewport = { seed, seed + 1.0f, 1.0f };
		Draw3UInkStrokeSnapshot stroke;
		stroke.style = { Draw3UInkStrokeKind::Pen, 1.0f, 0x123456u, 0u };
		stroke.undoId = 0;
		stroke.points = {
			{ seed, seed + 2.0f, 3.0f },
			{ seed + 4.0f, seed + 6.0f, 3.5f },
		};
		canvas.strokes.push_back(std::move(stroke));
		snapshot.canvases.push_back(std::move(canvas));
		return snapshot;
	}

	DesktopAutoSaveTimestamp MakeTimestamp(
		std::string date, std::string fileTime)
	{
		DesktopAutoSaveTimestamp timestamp;
		timestamp.localDate = std::move(date);
		timestamp.localTimeForFile = std::move(fileTime);
		const std::string& time = timestamp.localTimeForFile;
		timestamp.createdAt = timestamp.localDate + "T" + time.substr(0, 2) + ":" +
			time.substr(2, 2) + ":" + time.substr(4, 2) + "." +
			time.substr(6, 3) + "+08:00";
		return timestamp;
	}

	DesktopAutoSaveRequest MakeRequest(const char* requestId,
		const char* sessionId, std::uint64_t sequence,
		const char* fileGuid, const char* date, const char* fileTime,
		float seed, DesktopAutoSaveTrigger trigger = DesktopAutoSaveTrigger::Clear)
	{
		DesktopAutoSaveRequest request;
		request.saveRequestId = requestId;
		request.sessionId = sessionId;
		request.sequenceInSession = sequence;
		request.trigger = trigger;
		request.timestamp = MakeTimestamp(date, fileTime);
		request.proposedFileName = BuildDesktopAutoSaveFileName(
			request.timestamp, request.saveRequestId);
		request.snapshot = MakeSnapshot(fileGuid, seed);
		request.estimatedSnapshotBytes =
			EstimateDesktopAutoSaveSnapshotBytes(request.snapshot);
		return request;
	}

	fs::path DateDirectory(const fs::path& root, const char* date)
	{
		return root / L"desktop" / WidenAscii(date);
	}

	bool ReadJson(const fs::path& path, Json::Value& root)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream) return false;
		Json::CharReaderBuilder builder;
		builder["collectComments"] = false;
		std::string errors;
		return Json::parseFromStream(builder, stream, &root, &errors);
	}

	std::vector<std::byte> ReadBytes(const fs::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream) return {};
		stream.seekg(0, std::ios::end);
		const std::streamoff length = stream.tellg();
		if (length < 0) return {};
		std::vector<std::byte> bytes(static_cast<std::size_t>(length));
		stream.seekg(0, std::ios::beg);
		if (!bytes.empty())
			stream.read(reinterpret_cast<char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		return stream || bytes.empty() ? bytes : std::vector<std::byte>{};
	}

	bool WriteBytes(const fs::path& path, const std::vector<std::byte>& bytes)
	{
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream) return false;
		if (!bytes.empty())
			stream.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		return !!stream;
	}

	bool WriteJson(const fs::path& path, const Json::Value& root)
	{
		Json::StreamWriterBuilder builder;
		builder["indentation"] = "  ";
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream) return false;
		stream << Json::writeString(builder, root);
		return !!stream;
	}

	bool WaitForIdle(DesktopAutoSaveService& service,
		std::chrono::milliseconds timeout = std::chrono::seconds(5))
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			if (service.Diagnostics().pending == 0) return true;
			Sleep(5);
		}
		return service.Diagnostics().pending == 0;
	}

	bool VerifyIndexedFiles(const fs::path& dateDirectory,
		const Json::Value& index, TestState& state)
	{
		if (!index["entries"].isArray()) return false;
		std::set<std::string> paths;
		std::uint64_t expectedSequence = 1;
		for (const Json::Value& entry : index["entries"])
		{
			AUTOSAVE_CHECK(state, entry["dailySequence"].asUInt64() == expectedSequence++);
			const std::string relative = entry["relativePath"].asString();
			AUTOSAVE_CHECK(state, paths.insert(relative).second);
			const fs::path filePath = dateDirectory / WidenAscii(relative);
			AUTOSAVE_CHECK(state, fs::is_regular_file(filePath));
			const UInkReadResult read = ReadUInkFile(filePath.wstring());
			AUTOSAVE_CHECK(state, read.status == UInkReadStatus::Complete);
			AUTOSAVE_CHECK(state, read.document &&
				FormatUInkGuid(read.document->header.guid) == entry["fileGuid"].asString());
		}
		return true;
	}

	void TestPolicyAndNaming(TestState& state)
	{
		DesktopAutoSavePolicy policy;
		AUTOSAVE_CHECK(state, policy.Eligibility() == DesktopAutoSaveEligibility::Eligible);
		AUTOSAVE_CHECK(state, policy.ShouldCapture(
			Bridge::Workspace::Desktop, true, true));
		AUTOSAVE_CHECK(state, !policy.ShouldCapture(
			Bridge::Workspace::Desktop, false, true));
		AUTOSAVE_CHECK(state, !policy.ShouldCapture(
			Bridge::Workspace::Desktop, true, false));
		AUTOSAVE_CHECK(state, !policy.ShouldCapture(
			Bridge::Workspace::Whiteboard, true, true));
		AUTOSAVE_CHECK(state, !policy.ShouldCapture(
			Bridge::Workspace::Presentation, true, true));
		// 场景画布已经独立，访问 PPT 不再污染 Desktop 的保存资格。
		AUTOSAVE_CHECK(state, policy.ShouldCapture(
			Bridge::Workspace::Desktop, true, true));
		policy.CompleteDesktopClear();
		AUTOSAVE_CHECK(state, policy.ShouldCapture(
			Bridge::Workspace::Desktop, true, true));

		const DesktopAutoSaveTimestamp timestamp =
			MakeTimestamp("2026-09-01", "123456789");
		const std::string request = "aaaaaaaa-0000-4000-8000-000000000001";
		AUTOSAVE_CHECK(state, BuildDesktopAutoSaveFileName(timestamp, request) ==
			L"123456789_aaaaaaaa.uink");
		AUTOSAVE_CHECK(state, BuildDesktopAutoSaveFileName(timestamp, request, 2) ==
			L"123456789_aaaaaaaa_2.uink");

		DesktopAutoSaveRequest invalid = MakeRequest(
			"aaaaaaaa-0000-4000-8000-000000000003",
			"bbbbbbbb-0000-4000-8000-000000000001", 1,
			"cccccccc-0000-4000-8000-000000000003",
			"2026-09-01", "123456789", 1.0f);
		invalid.trigger = static_cast<DesktopAutoSaveTrigger>(255);
		AUTOSAVE_CHECK(state, !IsValidDesktopAutoSaveRequest(invalid));
		invalid = MakeRequest(
			"aaaaaaaa-0000-4000-8000-000000000004",
			"bbbbbbbb-0000-4000-8000-000000000001", 2,
			"cccccccc-0000-4000-8000-000000000004",
			"2026-09-01", "123456789", 2.0f);
		invalid.timestamp.createdAt = "invalid";
		AUTOSAVE_CHECK(state, !IsValidDesktopAutoSaveRequest(invalid));
		invalid = MakeRequest(
			"aaaaaaaa-0000-4000-8000-000000000005",
			"bbbbbbbb-0000-4000-8000-000000000001", 3,
			"cccccccc-0000-4000-8000-000000000005",
			"2026-13-01", "123456789", 3.0f);
		AUTOSAVE_CHECK(state, !IsValidDesktopAutoSaveRequest(invalid));
		invalid = MakeRequest(
			"aaaaaaaa-0000-4000-8000-000000000006",
			"bbbbbbbb-0000-4000-8000-000000000001", 4,
			"cccccccc-0000-4000-8000-000000000006",
			"2026-09-01", "240000000", 4.0f);
		AUTOSAVE_CHECK(state, !IsValidDesktopAutoSaveRequest(invalid));
		invalid = MakeRequest(
			"aaaaaaaa-0000-4000-8000-000000000007",
			"bbbbbbbb-0000-4000-8000-000000000001", 5,
			"cccccccc-0000-4000-8000-000000000007",
			"2026-09-01", "123456789", 5.0f);
		invalid.timestamp.createdAt = "2026-09-01T12:34:56.788+08:00";
		AUTOSAVE_CHECK(state, !IsValidDesktopAutoSaveRequest(invalid));
	}

	void TestNoDirectoryBeforeRequest(TestState& state)
	{
		TempDirectory temporary;
		AUTOSAVE_CHECK(state, temporary.IsValid());
		if (!temporary.IsValid()) return;
		const fs::path root = temporary.Child(L"not-created");
		DesktopAutoSaveService service;
		AUTOSAVE_CHECK(state, service.Start(root.wstring()));
		service.CloseAndDrain();
		AUTOSAVE_CHECK(state, !fs::exists(root));
	}

	void TestIdempotencyAndFileCollision(TestState& state)
	{
		TempDirectory temporary;
		AUTOSAVE_CHECK(state, temporary.IsValid());
		if (!temporary.IsValid()) return;
		const fs::path root = temporary.Child(L"identity");
		DesktopAutoSaveService service;
		AUTOSAVE_CHECK(state, service.Start(root.wstring()));
		const DesktopAutoSaveRequest first = MakeRequest(
			"aaaaaaaa-0000-4000-8000-000000000001",
			"bbbbbbbb-0000-4000-8000-000000000001", 1,
			"cccccccc-0000-4000-8000-000000000001",
			"2026-09-01", "120000001", 1.0f);
		const DesktopAutoSaveRequest second = MakeRequest(
			"aaaaaaaa-0000-4000-8000-000000000002",
			"bbbbbbbb-0000-4000-8000-000000000001", 2,
			"cccccccc-0000-4000-8000-000000000002",
			"2026-09-01", "120000001", 2.0f, DesktopAutoSaveTrigger::Exit);
		AUTOSAVE_CHECK(state, IsValidDesktopAutoSaveRequest(first));
		AUTOSAVE_CHECK(state, service.SubmitPrepared(first) ==
			DesktopAutoSaveSubmitStatus::Accepted);
		AUTOSAVE_CHECK(state, service.SubmitPrepared(first) ==
			DesktopAutoSaveSubmitStatus::Existing);
		AUTOSAVE_CHECK(state, service.SubmitPrepared(second) ==
			DesktopAutoSaveSubmitStatus::Accepted);
		service.CloseAndDrain();

		const DesktopAutoSaveDiagnostics diagnostics = service.Diagnostics();
		AUTOSAVE_CHECK(state, diagnostics.accepted == 2);
		AUTOSAVE_CHECK(state, diagnostics.committed == 2);
		AUTOSAVE_CHECK(state, diagnostics.failed == 0);
		AUTOSAVE_CHECK(state, diagnostics.duplicate == 1);
		AUTOSAVE_CHECK(state, diagnostics.pending == 0);
		AUTOSAVE_CHECK(state, diagnostics.queuedSnapshotBytes == 0);

		Json::Value index;
		const fs::path dateDirectory = DateDirectory(root, "2026-09-01");
		AUTOSAVE_CHECK(state, ReadJson(dateDirectory / L"index.json", index));
		AUTOSAVE_CHECK(state, index["schemaVersion"].asInt() == 1);
		AUTOSAVE_CHECK(state, index["scenario"].asString() == "desktop");
		AUTOSAVE_CHECK(state, index["date"].asString() == "2026-09-01");
		AUTOSAVE_CHECK(state, index["entries"].size() == 2);
		AUTOSAVE_CHECK(state, VerifyIndexedFiles(dateDirectory, index, state));
		AUTOSAVE_CHECK(state,
			index["entries"][0]["relativePath"].asString() !=
			index["entries"][1]["relativePath"].asString());
		AUTOSAVE_CHECK(state, fs::is_regular_file(dateDirectory / L"index.json.bak"));
	}

	void TestConcurrentServices(TestState& state)
	{
		TempDirectory temporary;
		AUTOSAVE_CHECK(state, temporary.IsValid());
		if (!temporary.IsValid()) return;
		const fs::path root = temporary.Child(L"concurrent");
		DesktopAutoSaveService firstService;
		DesktopAutoSaveService secondService;
		AUTOSAVE_CHECK(state, firstService.Start(root.wstring()));
		AUTOSAVE_CHECK(state, secondService.Start(root.wstring()));
		const DesktopAutoSaveRequest first = MakeRequest(
			"abababab-0000-4000-8000-000000000001",
			"bcbcbcbc-0000-4000-8000-000000000001", 1,
			"cdcdcdcd-0000-4000-8000-000000000001",
			"2026-09-01", "130000001", 3.0f);
		const DesktopAutoSaveRequest second = MakeRequest(
			"abababab-0000-4000-8000-000000000002",
			"bcbcbcbc-0000-4000-8000-000000000002", 1,
			"cdcdcdcd-0000-4000-8000-000000000002",
			"2026-09-01", "130000001", 4.0f);
		FaultScope faults({ 80, false, false });
		AUTOSAVE_CHECK(state, firstService.SubmitPrepared(first) ==
			DesktopAutoSaveSubmitStatus::Accepted);
		AUTOSAVE_CHECK(state, secondService.SubmitPrepared(second) ==
			DesktopAutoSaveSubmitStatus::Accepted);
		std::thread firstCloser([&] { firstService.CloseAndDrain(); });
		std::thread secondCloser([&] { secondService.CloseAndDrain(); });
		firstCloser.join();
		secondCloser.join();
		AUTOSAVE_CHECK(state, firstService.Diagnostics().committed == 1);
		AUTOSAVE_CHECK(state, secondService.Diagnostics().committed == 1);

		Json::Value index;
		const fs::path dateDirectory = DateDirectory(root, "2026-09-01");
		AUTOSAVE_CHECK(state, ReadJson(dateDirectory / L"index.json", index));
		AUTOSAVE_CHECK(state, index["entries"].size() == 2);
		AUTOSAVE_CHECK(state, VerifyIndexedFiles(dateDirectory, index, state));
	}

	void TestCrossDateAndDrain(TestState& state)
	{
		TempDirectory temporary;
		AUTOSAVE_CHECK(state, temporary.IsValid());
		if (!temporary.IsValid()) return;
		const fs::path root = temporary.Child(L"drain");
		DesktopAutoSaveService service;
		AUTOSAVE_CHECK(state, service.Start(root.wstring()));
		const std::vector<DesktopAutoSaveRequest> requests = {
			MakeRequest("10000000-0000-4000-8000-000000000001",
				"20000000-0000-4000-8000-000000000001", 1,
				"30000000-0000-4000-8000-000000000001",
				"2026-09-01", "235959998", 5.0f),
			MakeRequest("10000001-0000-4000-8000-000000000001",
				"20000000-0000-4000-8000-000000000001", 2,
				"30000001-0000-4000-8000-000000000001",
				"2026-09-01", "235959999", 6.0f),
			MakeRequest("10000002-0000-4000-8000-000000000001",
				"20000000-0000-4000-8000-000000000001", 3,
				"30000002-0000-4000-8000-000000000001",
				"2026-09-02", "000000000", 7.0f),
			MakeRequest("10000003-0000-4000-8000-000000000001",
				"20000000-0000-4000-8000-000000000001", 4,
				"30000003-0000-4000-8000-000000000001",
				"2026-09-02", "000000001", 8.0f,
				DesktopAutoSaveTrigger::Exit),
		};
		FaultScope faults({ 80, false, false });
		for (const DesktopAutoSaveRequest& request : requests)
			AUTOSAVE_CHECK(state, service.SubmitPrepared(request) ==
				DesktopAutoSaveSubmitStatus::Accepted);
		const auto started = std::chrono::steady_clock::now();
		service.CloseAndDrain();
		const auto elapsed = std::chrono::steady_clock::now() - started;
		const DesktopAutoSaveDiagnostics diagnostics = service.Diagnostics();
		AUTOSAVE_CHECK(state, diagnostics.accepted == requests.size());
		AUTOSAVE_CHECK(state, diagnostics.committed == requests.size());
		AUTOSAVE_CHECK(state, diagnostics.pending == 0);
		AUTOSAVE_CHECK(state, diagnostics.queuedSnapshotBytes == 0);
		AUTOSAVE_CHECK(state, elapsed >= std::chrono::milliseconds(200));
		AUTOSAVE_CHECK(state, service.SubmitPrepared(requests.front()) ==
			DesktopAutoSaveSubmitStatus::Closed);

		for (const char* date : { "2026-09-01", "2026-09-02" })
		{
			Json::Value index;
			const fs::path dateDirectory = DateDirectory(root, date);
			AUTOSAVE_CHECK(state, ReadJson(dateDirectory / L"index.json", index));
			AUTOSAVE_CHECK(state, index["entries"].size() == 2);
			AUTOSAVE_CHECK(state, VerifyIndexedFiles(dateDirectory, index, state));
		}
	}

	void TestFailureIsolationAndRetry(TestState& state)
	{
		TempDirectory temporary;
		AUTOSAVE_CHECK(state, temporary.IsValid());
		if (!temporary.IsValid()) return;
		const fs::path root = temporary.Child(L"failures");
		const fs::path dateDirectory = DateDirectory(root, "2026-09-01");
		std::error_code error;
		fs::create_directories(dateDirectory, error);
		AUTOSAVE_CHECK(state, !error);
		const fs::path unknownFile = dateDirectory / L"user-untracked.uink";
		const std::vector<std::byte> sentinel = {
			std::byte{ 0x11 }, std::byte{ 0x22 }, std::byte{ 0x33 } };
		AUTOSAVE_CHECK(state, WriteBytes(unknownFile, sentinel));

		const DesktopAutoSaveRequest baseline = MakeRequest(
			"40000000-0000-4000-8000-000000000001",
			"50000000-0000-4000-8000-000000000001", 1,
			"60000000-0000-4000-8000-000000000001",
			"2026-09-01", "140000001", 9.0f);
		const DesktopAutoSaveRequest orphan = MakeRequest(
			"40000001-0000-4000-8000-000000000001",
			"50000000-0000-4000-8000-000000000001", 2,
			"60000001-0000-4000-8000-000000000001",
			"2026-09-01", "140000002", 10.0f);
		DesktopAutoSaveService service;
		AUTOSAVE_CHECK(state, service.Start(root.wstring()));
		AUTOSAVE_CHECK(state, service.SubmitPrepared(baseline) ==
			DesktopAutoSaveSubmitStatus::Accepted);
		AUTOSAVE_CHECK(state, WaitForIdle(service));
		const fs::path indexPath = dateDirectory / L"index.json";
		const std::vector<std::byte> indexBefore = ReadBytes(indexPath);
		{
			FaultScope faults({ 0, false, true });
			AUTOSAVE_CHECK(state, service.SubmitPrepared(orphan) ==
				DesktopAutoSaveSubmitStatus::Accepted);
			AUTOSAVE_CHECK(state, WaitForIdle(service));
		}
		service.CloseAndDrain();
		AUTOSAVE_CHECK(state, service.Diagnostics().committed == 1);
		AUTOSAVE_CHECK(state, service.Diagnostics().failed == 1);
		AUTOSAVE_CHECK(state, ReadBytes(indexPath) == indexBefore);
		const fs::path orphanPath = dateDirectory / orphan.proposedFileName;
		AUTOSAVE_CHECK(state, fs::is_regular_file(orphanPath));
		const std::vector<std::byte> orphanBeforeRetry = ReadBytes(orphanPath);
		AUTOSAVE_CHECK(state, ReadBytes(unknownFile) == sentinel);

		// 重新提交同一稳定请求时复用孤儿文件，只补齐索引，不生成第二份历史。
		DesktopAutoSaveService retryService;
		AUTOSAVE_CHECK(state, retryService.Start(root.wstring()));
		AUTOSAVE_CHECK(state, retryService.SubmitPrepared(orphan) ==
			DesktopAutoSaveSubmitStatus::Accepted);
		retryService.CloseAndDrain();
		AUTOSAVE_CHECK(state, retryService.Diagnostics().committed == 1);
		AUTOSAVE_CHECK(state, ReadBytes(orphanPath) == orphanBeforeRetry);
		Json::Value retriedIndex;
		AUTOSAVE_CHECK(state, ReadJson(indexPath, retriedIndex));
		AUTOSAVE_CHECK(state, retriedIndex["entries"].size() == 2);
		AUTOSAVE_CHECK(state, VerifyIndexedFiles(dateDirectory, retriedIndex, state));
		AUTOSAVE_CHECK(state, ReadBytes(unknownFile) == sentinel);

		const DesktopAutoSaveRequest failedUInk = MakeRequest(
			"40000002-0000-4000-8000-000000000001",
			"50000000-0000-4000-8000-000000000001", 3,
			"60000002-0000-4000-8000-000000000001",
			"2026-09-01", "140000003", 11.0f);
		DesktopAutoSaveService failedService;
		AUTOSAVE_CHECK(state, failedService.Start(root.wstring()));
		{
			FaultScope faults({ 0, true, false });
			AUTOSAVE_CHECK(state, failedService.SubmitPrepared(failedUInk) ==
				DesktopAutoSaveSubmitStatus::Accepted);
			failedService.CloseAndDrain();
		}
		AUTOSAVE_CHECK(state, failedService.Diagnostics().failed == 1);
		AUTOSAVE_CHECK(state, !fs::exists(dateDirectory / failedUInk.proposedFileName));
		AUTOSAVE_CHECK(state, ReadBytes(unknownFile) == sentinel);
	}

	void TestIndexBackupRecoveryAndIsolation(TestState& state)
	{
		TempDirectory temporary;
		AUTOSAVE_CHECK(state, temporary.IsValid());
		if (!temporary.IsValid()) return;
		const fs::path root = temporary.Child(L"index-recovery");
		const fs::path dateDirectory = DateDirectory(root, "2026-09-01");
		const fs::path indexPath = dateDirectory / L"index.json";
		const fs::path backupPath = dateDirectory / L"index.json.bak";
		const DesktopAutoSaveRequest first = MakeRequest(
			"81000000-0000-4000-8000-000000000001",
			"82000000-0000-4000-8000-000000000001", 1,
			"83000000-0000-4000-8000-000000000001",
			"2026-09-01", "160000001", 13.0f);
		const DesktopAutoSaveRequest second = MakeRequest(
			"81000000-0000-4000-8000-000000000002",
			"82000000-0000-4000-8000-000000000001", 2,
			"83000000-0000-4000-8000-000000000002",
			"2026-09-01", "160000002", 14.0f);
		const DesktopAutoSaveRequest recovered = MakeRequest(
			"81000000-0000-4000-8000-000000000003",
			"82000000-0000-4000-8000-000000000001", 3,
			"83000000-0000-4000-8000-000000000003",
			"2026-09-01", "160000003", 15.0f);
		const DesktopAutoSaveRequest isolated = MakeRequest(
			"81000000-0000-4000-8000-000000000004",
			"82000000-0000-4000-8000-000000000001", 4,
			"83000000-0000-4000-8000-000000000004",
			"2026-09-01", "160000004", 16.0f);

		DesktopAutoSaveService service;
		AUTOSAVE_CHECK(state, service.Start(root.wstring()));
		for (const DesktopAutoSaveRequest* request : { &first, &second })
		{
			AUTOSAVE_CHECK(state, service.SubmitPrepared(*request) ==
				DesktopAutoSaveSubmitStatus::Accepted);
			AUTOSAVE_CHECK(state, WaitForIdle(service));
		}
		AUTOSAVE_CHECK(state, fs::is_regular_file(indexPath));
		AUTOSAVE_CHECK(state, fs::is_regular_file(backupPath));

		const std::vector<std::byte> corruption = {
			std::byte{ 0xde }, std::byte{ 0xad }, std::byte{ 0xbe }, std::byte{ 0xef } };
		AUTOSAVE_CHECK(state, WriteBytes(indexPath, corruption));
		AUTOSAVE_CHECK(state, service.SubmitPrepared(recovered) ==
			DesktopAutoSaveSubmitStatus::Accepted);
		AUTOSAVE_CHECK(state, WaitForIdle(service));
		Json::Value recoveredIndex;
		AUTOSAVE_CHECK(state, ReadJson(indexPath, recoveredIndex));
		AUTOSAVE_CHECK(state, recoveredIndex["entries"].size() == 2);
		AUTOSAVE_CHECK(state,
			recoveredIndex["entries"][0]["saveRequestId"].asString() == first.saveRequestId);
		AUTOSAVE_CHECK(state,
			recoveredIndex["entries"][1]["saveRequestId"].asString() == recovered.saveRequestId);
		AUTOSAVE_CHECK(state, VerifyIndexedFiles(dateDirectory, recoveredIndex, state));

		// 主备均损坏时隔离索引写入；已 durable 的 UInk 保留为待恢复孤儿。
		AUTOSAVE_CHECK(state, WriteBytes(indexPath, corruption));
		AUTOSAVE_CHECK(state, WriteBytes(backupPath, corruption));
		AUTOSAVE_CHECK(state, service.SubmitPrepared(isolated) ==
			DesktopAutoSaveSubmitStatus::Accepted);
		service.CloseAndDrain();
		AUTOSAVE_CHECK(state, service.Diagnostics().committed == 3);
		AUTOSAVE_CHECK(state, service.Diagnostics().failed == 1);
		AUTOSAVE_CHECK(state, ReadBytes(indexPath) == corruption);
		AUTOSAVE_CHECK(state, ReadBytes(backupPath) == corruption);
		AUTOSAVE_CHECK(state, fs::is_regular_file(
			dateDirectory / isolated.proposedFileName));
	}

	void TestInvalidIndexReferenceIsIsolated(TestState& state)
	{
		TempDirectory temporary;
		AUTOSAVE_CHECK(state, temporary.IsValid());
		if (!temporary.IsValid()) return;
		const fs::path root = temporary.Child(L"invalid-index");
		const fs::path dateDirectory = DateDirectory(root, "2026-09-01");
		std::error_code error;
		fs::create_directories(dateDirectory, error);
		AUTOSAVE_CHECK(state, !error);
		// 索引必须引用完整普通文件；同名目录不能冒充已提交的 UInk。
		fs::create_directory(dateDirectory / L"missing.uink", error);
		AUTOSAVE_CHECK(state, !error);

		Json::Value invalid(Json::objectValue);
		invalid["schemaVersion"] = 1;
		invalid["scenario"] = "desktop";
		invalid["date"] = "2026-09-01";
		invalid["entries"] = Json::Value(Json::arrayValue);
		Json::Value missing(Json::objectValue);
		missing["dailySequence"] = Json::UInt64(1);
		missing["saveRequestId"] = "70000000-0000-4000-8000-000000000001";
		missing["sessionId"] = "71000000-0000-4000-8000-000000000001";
		missing["sequenceInSession"] = Json::UInt64(1);
		missing["trigger"] = "clear";
		missing["fileGuid"] = "72000000-0000-4000-8000-000000000001";
		missing["relativePath"] = "missing.uink";
		missing["createdAt"] = "2026-09-01T15:00:00.000+08:00";
		invalid["entries"].append(missing);
		const fs::path indexPath = dateDirectory / L"index.json";
		AUTOSAVE_CHECK(state, WriteJson(indexPath, invalid));
		const std::vector<std::byte> invalidBefore = ReadBytes(indexPath);

		const DesktopAutoSaveRequest request = MakeRequest(
			"73000000-0000-4000-8000-000000000001",
			"74000000-0000-4000-8000-000000000001", 1,
			"75000000-0000-4000-8000-000000000001",
			"2026-09-01", "150000001", 12.0f);
		DesktopAutoSaveService service;
		AUTOSAVE_CHECK(state, service.Start(root.wstring()));
		AUTOSAVE_CHECK(state, service.SubmitPrepared(request) ==
			DesktopAutoSaveSubmitStatus::Accepted);
		service.CloseAndDrain();
		AUTOSAVE_CHECK(state, service.Diagnostics().failed == 1);
		AUTOSAVE_CHECK(state, ReadBytes(indexPath) == invalidBefore);
		AUTOSAVE_CHECK(state, fs::is_regular_file(
			dateDirectory / request.proposedFileName));
	}

	void TestInvalidIndexTimestampIsIsolated(TestState& state)
	{
		TempDirectory temporary;
		AUTOSAVE_CHECK(state, temporary.IsValid());
		if (!temporary.IsValid()) return;
		const fs::path root = temporary.Child(L"invalid-index-time");
		const fs::path dateDirectory = DateDirectory(root, "2026-09-01");
		std::error_code error;
		fs::create_directories(dateDirectory, error);
		AUTOSAVE_CHECK(state, !error);
		const std::vector<std::byte> sentinel = { std::byte{ 0x55 } };
		AUTOSAVE_CHECK(state, WriteBytes(
			dateDirectory / L"existing.uink", sentinel));

		Json::Value invalid(Json::objectValue);
		invalid["schemaVersion"] = 1;
		invalid["scenario"] = "desktop";
		invalid["date"] = "2026-09-01";
		invalid["entries"] = Json::Value(Json::arrayValue);
		Json::Value entry(Json::objectValue);
		entry["dailySequence"] = Json::UInt64(1);
		entry["saveRequestId"] = "76000000-0000-4000-8000-000000000001";
		entry["sessionId"] = "77000000-0000-4000-8000-000000000001";
		entry["sequenceInSession"] = Json::UInt64(1);
		entry["trigger"] = "clear";
		entry["fileGuid"] = "78000000-0000-4000-8000-000000000001";
		entry["relativePath"] = "existing.uink";
		entry["createdAt"] = "2026-09-01T25:00:00.000+08:00";
		invalid["entries"].append(entry);
		const fs::path indexPath = dateDirectory / L"index.json";
		AUTOSAVE_CHECK(state, WriteJson(indexPath, invalid));
		const std::vector<std::byte> invalidBefore = ReadBytes(indexPath);

		const DesktopAutoSaveRequest request = MakeRequest(
			"79000000-0000-4000-8000-000000000001",
			"7a000000-0000-4000-8000-000000000001", 1,
			"7b000000-0000-4000-8000-000000000001",
			"2026-09-01", "151000001", 13.0f);
		DesktopAutoSaveService service;
		AUTOSAVE_CHECK(state, service.Start(root.wstring()));
		AUTOSAVE_CHECK(state, service.SubmitPrepared(request) ==
			DesktopAutoSaveSubmitStatus::Accepted);
		service.CloseAndDrain();
		AUTOSAVE_CHECK(state, service.Diagnostics().failed == 1);
		AUTOSAVE_CHECK(state, ReadBytes(indexPath) == invalidBefore);
		AUTOSAVE_CHECK(state, fs::is_regular_file(
			dateDirectory / request.proposedFileName));
	}
}

int RunDesktopAutoSaveTests()
{
	TestState state;
	ResetDesktopAutoSaveTestFaultInjection();
	TestPolicyAndNaming(state);
	TestNoDirectoryBeforeRequest(state);
	TestIdempotencyAndFileCollision(state);
	TestConcurrentServices(state);
	TestCrossDateAndDrain(state);
	TestFailureIsolationAndRetry(state);
	TestIndexBackupRecoveryAndIsolation(state);
	TestInvalidIndexReferenceIsIsolated(state);
	TestInvalidIndexTimestampIsIsolated(state);
	ResetDesktopAutoSaveTestFaultInjection();
	if (state.failures == 0)
		std::cout << "All Desktop UInk auto-save tests passed." << std::endl;
	return state.failures;
}
