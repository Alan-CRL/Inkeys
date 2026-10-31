#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "Draw3.Presentation.h"
#include <array>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

import Inkeys.Drawing.Draw3.presentation_auto_save;
import draw3.uink_file;

#include "presentation-load-gate.generated.hpp"

namespace
{
    using namespace Inkeys::Drawing::Draw3;
    using namespace draw3::uink;
    namespace fs = std::filesystem;

    int failures = 0;
    void Check(bool condition, const char* label)
    {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    }
    void Require(bool condition, const char* label)
    {
        Check(condition, label);
        if (!condition) throw std::runtime_error(label);
    }

    struct Handle
    {
        HANDLE value = INVALID_HANDLE_VALUE;
        ~Handle() { Close(); }
        void Close()
        {
            if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
            value = INVALID_HANDLE_VALUE;
        }
    };

    Bridge::PresentationTarget MakeTarget()
    {
        Bridge::PresentationTarget target;
        for (std::size_t index = 0; index < target.key.bytes.size(); ++index)
            target.key.bytes[index] = static_cast<std::uint8_t>(index + 1);
        target.bindingMode = Bridge::SlideBindingMode::StableSlideId;
        target.sourceIdentity = "path:c:\\lessons\\sharing-probe.pptx";
        target.presentationName = "sharing-probe.pptx";
        target.provider = "PowerPoint";
        target.bindingToken = "PowerPoint:probe:1";
        target.slideIds = { 101 };
        target.slideId = 101;
        target.totalPages = 1;
        target.bindingRevision = 1;
        target.sessionRevision = 1;
        target.targetRevision = 1;
        return target;
    }

    // 与现有 presentation_autosave_tests.cpp 的合法最小快照形状一致。
    Draw3UInkExportSnapshot MakeSnapshot(const Bridge::PresentationTarget& target)
    {
        Draw3UInkExportSnapshot snapshot;
        snapshot.fileGuid = *ParseUInkGuid("11111111-1111-4111-8111-111111111111");
        snapshot.workspaceGuid = *ParseUInkGuid("22222222-2222-4222-8222-222222222222");
        snapshot.workspaceName = target.presentationName;
        snapshot.workspaceType = 2;
        snapshot.hostId = FormatPresentationKey(target.key);
        snapshot.currentPageIndex = 0;
        snapshot.workspaceExtra = MakeInkeysBindingExtra(Draw3UInkImportBindingMode::StableSlideId);
        snapshot.assignedIndependentUndoGroups = true;
        Draw3UInkCanvasSnapshot canvas;
        canvas.pageGuid = *ParseUInkGuid("33333333-3333-4333-8333-333333333333");
        canvas.pageNumber = 1;
        canvas.slideId = 101;
        canvas.extra = MakeInkeysBindingExtra(Draw3UInkImportBindingMode::StableSlideId);
        Draw3UInkStrokeSnapshot stroke;
        stroke.style.kind = Draw3UInkStrokeKind::Pen;
        stroke.style.fallbackRgb = 0x123456;
        stroke.points = { { 10.0f, 20.0f, 4.0f }, { 30.0f, 40.0f, 4.0f } };
        canvas.strokes.push_back(std::move(stroke));
        snapshot.canvases.push_back(std::move(canvas));
        return snapshot;
    }

    PresentationPersistenceCompletion TakeOne(PresentationAutoSaveService& service)
    {
        PresentationPersistenceCompletion result;
        Require(service.TryTakeCompletion(result), "accepted work has a completion");
        PresentationPersistenceCompletion extra;
        Require(!service.TryTakeCompletion(extra), "exactly one completion");
        return result;
    }

    PresentationPersistenceCompletion Load(PresentationAutoSaveService& service,
        const fs::path& root, const Bridge::PresentationTarget& target,
        std::uint64_t generation)
    {
        Require(service.Start(root.wstring()), "real service Start for Current load");
        PresentationLoadRequest request;
        request.target = target;
        request.slotGeneration = generation;
        Require(service.SubmitLoad(std::move(request)) ==
            PresentationPersistenceSubmitStatus::Accepted, "real Current load accepted");
        service.CloseAndDrain();
        return TakeOne(service);
    }
}

int wmain(int argc, wchar_t** argv)
{
    // 根目录由调用者放在授权 Build 调查目录下；使用随机子目录保留证据，不自动递归删除。
    if (argc != 2) { std::fputs("usage: probe.exe <existing-probe-output-directory>\n", stderr); return 2; }
    try
    {
        const auto id = CreateUInkGuid();
        Require(id.has_value(), "unique fixture identity");
        const auto guid = FormatUInkGuid(*id);
        const auto root = fs::absolute(fs::path(argv[1])) /
            (L"presentation-sharing-" + std::wstring(guid.begin(), guid.end()));
        fs::create_directories(root);
        const auto rootUtf8 = root.u8string();
        std::printf("fixture_root=%s\n", reinterpret_cast<const char*>(rootUtf8.c_str()));
        auto target = MakeTarget();
        PresentationAutoSaveService service;
        ResetPresentationAutoSaveTestFaultInjection();
        Require(service.Start(root.wstring()), "real service Start for save");
        PresentationSaveRequest save;
        save.target = target;
        save.mutationRevision = 1;
        save.slotGeneration = 7;
        save.snapshot = MakeSnapshot(target);
        Require(service.SubmitSave(std::move(save)) ==
            PresentationPersistenceSubmitStatus::Accepted, "real save accepted");
        service.CloseAndDrain();
        const auto committed = TakeOne(service);
        Require(committed.status == PresentationPersistenceStatus::Committed,
            "fixture durable UInk and index committed");
        Check(ShouldEvictPresentationSlot(true, 1, 1, 1, false),
            "clean saved slot is eligible for production cold eviction");

        fs::path file;
        std::size_t count = 0;
        for (const auto& entry : fs::recursive_directory_iterator(root))
            if (entry.is_regular_file() && entry.path().extension() == L".uink")
            { file = entry.path(); ++count; }
        Require(count == 1, "fixture contains exactly one versioned UInk");
        const auto fileUtf8 = file.u8string();
        std::printf("fixture_uink=%s\n", reinterpret_cast<const char*>(fileUtf8.c_str()));
        const auto before = ReadUInkFile(file.wstring());
        Require(before.status == UInkReadStatus::Complete && before.sourceRevision,
            "uncontended real UInk read is Complete");
        const auto initial = Load(service, root, target, 8);
        Require(initial.status == PresentationPersistenceStatus::Loaded && initial.loadedSnapshot,
            "baseline same-session Current load is Loaded");

        Handle exclusive;
        exclusive.value = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(exclusive.value != INVALID_HANDLE_VALUE,
            "external equivalent FileShare.None handle acquired");
        const auto blockedRead = ReadUInkFile(file.wstring());
        std::printf("blocked_uink_status=%u\n", static_cast<unsigned>(blockedRead.status));
        Require(blockedRead.status == UInkReadStatus::IoError,
            "real UInk read with share conflict is IoError");
        const auto failed = Load(service, root, target, 9);
        std::printf("blocked_current_status=%u\n", static_cast<unsigned>(failed.status));
        Require(failed.status == PresentationPersistenceStatus::SourceChanged,
            "real Current load maps share conflict to SourceChanged");
        Require(failed.slotGeneration == 9 && failed.target == target,
            "failure completion preserves matching active slot identity");

        ProbeOriginal::PresentationCurrentLoadRetry retry;
        bool pending = true;
        bool initialized = false;
        const std::optional<Bridge::PresentationTarget> activeTarget = target;
        ProbeOriginal::ApplyOriginalCurrentLoadFailure(failed, pending, initialized,
            activeTarget, 9, retry);
        Check(!pending && !initialized, "original failure branch leaves unresolved slot");
        Check(Bridge::PresentationInputSuppressed(Bridge::Workspace::Presentation,
            pending || ProbeOriginal::PresentationLoadUnresolved(
                Bridge::Workspace::Presentation, initialized, 0)),
            "original production input predicate continues to suppress drawing");

        exclusive.Close();
        const auto after = ReadUInkFile(file.wstring());
        Require(after.status == UInkReadStatus::Complete && after.sourceRevision,
            "same file reads Complete immediately after exclusive handle release");
        Check(after.sourceRevision == before.sourceRevision,
            "fixture source revision and SHA256 remained unchanged");
        const auto now = GetTickCount64() + 24ull * 60 * 60 * 1000;
        int retried = 0;
        const auto submit = [&](PresentationLoadRequest&&) { ++retried; return true; };
        Check(!retry.RemainingWaitMilliseconds(target, 9, now),
            "original retry gate has no future retry deadline");
        Check(!retry.TrySubmit(target, 9, now, pending, initialized, submit) && retried == 0,
            "original retry gate still refuses automatic submit after simulated 24h");

        Bridge::StateBridge bridge;
        bool changed = false;
        const auto revision = bridge.PublishPresentationTarget(target, &changed);
        Require(revision && changed, "actual bridge first target has a revision");
        changed = true;
        const auto repeated = bridge.PublishPresentationTarget(target, &changed);
        Require(repeated == revision && !changed,
            "actual bridge same target publication keeps old target revision");
        auto state = bridge.Snapshot();
        state.tool = Bridge::Tool::Highlighter;
        bridge.PublishState(state);
        Check(!ProbeOriginal::OriginalHostWouldQueueTarget(bridge.Snapshot(), *revision, false),
            "original Host target condition ignores tool publication for same target");

        // 手工新请求是对照，证明后台与文件已恢复；不把它当成产品已提供的 UI 重试。
        const auto manual = Load(service, root, target, 9);
        Check(manual.status == PresentationPersistenceStatus::Loaded,
            "explicit new Current load after release recovers without process restart");
        Check(ProbeOriginal::PresentationLoadedForLane(manual),
            "recovered completion passes original lane validation");
        target.bindingToken += ":new-binding";
        changed = false;
        const auto newRevision = bridge.PublishPresentationTarget(target, &changed);
        Check(newRevision && *newRevision != *revision && changed &&
            ProbeOriginal::OriginalHostWouldQueueTarget(bridge.Snapshot(), *revision, false),
            "new scene identity can cause a fresh target command");
        std::printf("RESULT failures=%d scope=real_worker_plus_original_gate_fragments no_hwnd=1\n", failures);
        return failures ? 1 : 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "PROBE_ABORT %s failures=%d\n", error.what(), failures);
        return 3;
    }
}
