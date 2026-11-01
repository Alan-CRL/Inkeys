#include "../Inkeys/Inkeys/Business/PptSession.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.A2.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h"
#include "../Inkeys/PptNavigationDiagnostics.h"
#include "../Inkeys/ShutdownProgressTrace.h"
#include "../Inkeys/PptWaitingDiagnostics.h"

#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
#include <array>
#include <thread>

namespace
{
	using PerfEvent = Inkeys::Business::PptPerf::Event;
	std::vector<PerfEvent>* perfEvents = nullptr;
	std::int64_t perfClock = 100;
	int perfClockCalls = 0;
	unsigned long perfThread = 11;
	std::int64_t PerfNow() { ++perfClockCalls; return perfClock++; }
	std::int64_t PerfFrequency() { return 1000; }
	unsigned long PerfProcess() { return 7; }
	unsigned long PerfThread() { return perfThread; }
	void PerfCapture(const PerfEvent& value) { perfEvents->push_back(value); }
	void PerfThrow(const PerfEvent&) { throw std::runtime_error("diagnostic sink"); }
	std::int64_t ShutdownNow() { SetLastError(801); return PerfNow(); }
	void ShutdownCapture(const PerfEvent& value) { SetLastError(802); PerfCapture(value); }
	void ShutdownThrow(const PerfEvent&) { SetLastError(803); throw std::runtime_error("shutdown sink"); }
	bool WaitOutputThrows(const char*,std::size_t) { SetLastError(910);throw std::runtime_error("wait output"); }
	std::int64_t WaitInvalidClock() { return 0; }

}

int RunPptSessionTests()
{
	using namespace Inkeys::Business;
	using namespace Inkeys::UI::Bar;
	int failures = 0;
	auto check = [&](bool ok, const char* text)
	{
		if (!ok) { ++failures; std::cerr << "FAIL " << text << '\n'; }
	};
	{
		namespace Wait=Inkeys::Business::PptWaitTrace;
		Wait::Record slots[4];const PptPerf::Clock clock{ShutdownNow,PerfFrequency,PerfProcess,PerfThread};
		Wait::Recorder disabled(nullptr,0,clock);perfClockCalls=0;SetLastError(901);
		Wait::Fields fields;fields.session=7;fields.epoch=11;fields.flags=Wait::Waiting;
		disabled.Capture(Wait::Stage::Publish,fields,Wait::Result::Accepted);
		{Wait::DrawScope scope(fields,&disabled);Wait::DrawStarted();Wait::DotBrush(true);Wait::DotIssued();}
		check(perfClockCalls==0&&disabled.AfterProducersJoined().seen==0&&GetLastError()==901,
			"disabled waiting capture performs no sampling or slot writes and preserves LastError");
		Wait::Recorder recorder(slots,4,clock);
		{
			Wait::DrawScope scope(fields,&recorder);Wait::DrawStarted();Wait::DotBrush(true);Wait::DotIssued();
			{Wait::DrawScope muted(fields,nullptr);Wait::DotBrush(false);Wait::DotIssued();}
			Wait::DotIssued();Wait::DotIssued();
			scope.Pixels(Wait::Result::Submitted,true,23,S_OK,0);
		}
		auto counts=recorder.AfterProducersJoined();
		check(counts.seen==3&&counts.retained==3&&counts.dropped==0&&counts.invalid==0
			&&slots[1].stage==Wait::Stage::Draw&&slots[1].fields.dots==3&&slots[1].fields.branches==1
			&&slots[2].fields.attempt==slots[1].fields.attempt&&slots[2].fields.pixelRevision==23
			&&(slots[2].fields.flags&Wait::PixelsSubmitted)&&GetLastError()==901&&Wait::drawSample==nullptr,
			"draw TLS restores nested muted scopes and keeps real dots and successful pixels on the same attempt");
		recorder.Capture(Wait::Stage::NativeReceipt,fields,Wait::Result::ReceiptRejected);
		recorder.Capture(Wait::Stage::NativeReceipt,fields,Wait::Result::ReceiptCommitted);
		counts=recorder.AfterProducersJoined();
		check(counts.seen==5&&counts.retained==4&&counts.dropped==1&&slots[3].result==Wait::Result::ReceiptRejected,
			"full waiting buffer retains the prefix and reports loss instead of overwriting a failure with recovery");
		SetLastError(902);const auto errors=Wait::FlushAfterProducersJoined(recorder,WaitOutputThrows);
		check(errors==5&&GetLastError()==902,"waiting export failures cannot escape or overwrite caller LastError");
		char text[1024];const int n=Wait::Format(text,sizeof(text),slots[2],3);
		check(n>0&&std::string(text).find("pixel_revision=23")!=std::string::npos
			&&std::string(text).find("stage=pixels-result result=submitted")!=std::string::npos,
			"waiting records export immutable numeric identity and exact software boundary");
		Wait::Record invalidSlot[1];Wait::Recorder invalid(invalidSlot,1,{WaitInvalidClock,PerfFrequency,PerfProcess,PerfThread});
		invalid.Capture(Wait::Stage::Publish,fields,Wait::Result::Accepted);
		check(invalid.AfterProducersJoined().retained==1&&invalid.AfterProducersJoined().invalid==1,
			"invalid waiting clocks remain represented in the retained denominator");
		std::array<Wait::Record,512> concurrentSlots;
		Wait::Recorder concurrent(concurrentSlots.data(),concurrentSlots.size());
		std::array<std::thread,4> producers;
		for(std::size_t worker=0;worker<producers.size();++worker)producers[worker]=std::thread([&,worker]{
			for(unsigned item=0;item<100;++item){auto value=fields;value.epoch=worker*100+item+1;
				concurrent.Capture(Wait::Stage::Model,value,Wait::Result::Accepted);}
		});
		for(auto& producer:producers)producer.join();
		const auto concurrentCounts=concurrent.AfterProducersJoined();std::array<bool,401> seen{};bool unique=true;
		for(std::size_t i=0;i<concurrentCounts.retained;++i){const auto epoch=concurrentSlots[i].fields.epoch;
			if(!epoch||epoch>=seen.size()||seen[epoch])unique=false;else seen[epoch]=true;}
		check(concurrentCounts.seen==400&&concurrentCounts.retained==400&&concurrentCounts.dropped==0
			&&concurrentCounts.invalid==0&&unique,"joined multi-producer waiting capture publishes each independent slot exactly once");
	}
	{
		using namespace Inkeys::Business::PptPerf;
		const Clock clock{ PerfNow, PerfFrequency, PerfProcess, PerfThread };
		std::vector<Event> events; perfEvents = &events;
		perfClockCalls = 0;
		Recorder disabled(false, PerfCapture, clock);
		{
			Scope trace(disabled, "native-business", { 0, 17, 23, "Previous" });
			disabled.Instant("native-discard", {}, "outstanding");
		}
		check(events.empty() && perfClockCalls == 0, "disabled native diagnostics never sample the clock or write a record");
		Recorder recorder(true, PerfCapture, clock);
		const auto request = recorder.Capture("native-request", { 0, 17, 23, "Previous" });
		perfClock += 1000; // 模拟入队前的生产者锁等待，队列计时必须从成功接纳开始。
		const auto queued = recorder.Capture("native-queue", { request.span, 17, 23, "Previous" });
		// 消费者可先写出 end；时间戳仍是原始边界，而不是logger实际写入时刻。
		perfThread = 29;
		const auto dequeued = recorder.CaptureEnd(queued, "dequeued");
		check(queued.qpc - request.qpc == 1001 && dequeued.stamp.qpc - queued.qpc == 1,
			"producer admission delay stays outside exact queue residence timing");
		recorder.Emit(dequeued);
		recorder.Begin(request); recorder.Begin(queued);
		check(events.size() == 3 && events[0].stamp.qpc > queued.qpc &&
			events[0].stamp.span == queued.span && events[0].stamp.thread == 29 && queued.thread == 11,
			"queue telemetry preserves its cross-thread identity and captured time despite out-of-order sinks");
		{
			Scope resumed(recorder, request);
			resumed.Result("native-completed");
			Scope business(recorder, "native-business", { request.span, 17, 23, "Previous" });
			{
				ContextScope context({ business.Id(), 17, 23, "Previous" });
				check(current.parent == business.Id() && current.epoch == 23,
					"native request context carries only the matching operation and local identity");
				try
				{
					Scope call(recorder, "native-com", current);
					throw std::runtime_error("original business error");
				}
				catch (const std::runtime_error& error)
				{
					check(std::string(error.what()) == "original business error", "diagnostic scopes preserve the original exception");
				}
			}
			check(current.parent == 0 && current.epoch == 0, "request context cannot leak into the next business command");
		}
		check(events.size() == 8 && std::string(events[5].event) == "end" && std::string(events[5].result) == "exception"
			&& events[5].stamp.context.parent == events[3].stamp.span &&
			events.back().stamp.span == request.span && std::string(events.back().event) == "end",
			"exceptional COM span closes before its parents and the deferred root is completed exactly once");
		char formatted[512]{};
		const auto sample = events.empty() ? Event{} : events.front();
		const int count = Format(formatted, sizeof(formatted), sample);
		const std::string line(formatted);
		check(count > 0 && line.find("[PptPerf] v=1 source=native pid=7 tid=29") == 0
			&& line.find("event=end stage=native-queue op=Previous local_session=17 epoch=23") != std::string::npos,
			"native numeric trace protocol contains cross-layer clock/thread and distinct local request identity");
		char truncatedBuffer[8]{};
		check(Format(truncatedBuffer, sizeof(truncatedBuffer), sample) == 0, "truncated native telemetry is dropped rather than parsed as a complete record");
		Recorder throwing(true, PerfThrow, clock);
		int businessSteps = 0;
		{
			Scope trace(throwing, "native-focus", { request.span, 17, 23, "Previous" });
			++businessSteps;
			trace.Result("denied");
		}
		check(businessSteps == 1, "throwing diagnostic sink cannot skip business work or escape its cleanup");
		perfEvents = nullptr; perfThread = 11;
	}
	{
		namespace Trace = Inkeys::Shutdown::ProgressTrace;
		// 首次读取环境/初始化 Recorder 也必须保住紧邻 Win32 调用的错误码。
		SetLastError(701);
		(void)Trace::Enabled();
		check(GetLastError() == 701, "first shutdown flag read preserves LastError");
		auto& defaultRecorder = Trace::DefaultRecorder();
		check(GetLastError() == 701, "first shutdown recorder initialization preserves LastError");
		Trace::WarmUp();
		check(Trace::PreparedRecorder() == &defaultRecorder && GetLastError() == 701,
			"shutdown warm-up publishes the shared recorder without changing LastError");

		const Trace::Clock clock{ ShutdownNow, PerfFrequency, PerfProcess, PerfThread };
		std::vector<Trace::Event> events; perfEvents = &events;
		perfClockCalls = 0;
		Trace::Recorder disabled(false, ShutdownCapture, clock);
		{
			Trace::Scope trace(disabled, "test-disabled");
			const auto stamp = Trace::Capture(disabled, "test-disabled-buffer");
			Trace::Emit(disabled, Trace::CaptureEnd(disabled, stamp));
			Trace::Begin(disabled, stamp);
		}
		const auto absent = Trace::Capture(static_cast<Trace::Recorder*>(nullptr), "test-unprepared");
		(void)Trace::CaptureEnd(static_cast<Trace::Recorder*>(nullptr), absent);
		check(events.empty() && perfClockCalls == 0 && GetLastError() == 701,
			"disabled and unprepared shutdown diagnostics never sample or emit and preserve LastError");

		Trace::Recorder recorder(true, ShutdownCapture, clock);
		const auto buffered = Trace::Capture(&recorder, "test-buffered");
		const auto bufferedEnd = Trace::CaptureEnd(&recorder, buffered, "returned", 42);
		check(events.empty() && perfClockCalls == 2 && GetLastError() == 701,
			"pre-Arm shutdown capture stores only values and preserves LastError");
		Trace::Begin(recorder, buffered); Trace::Emit(recorder, bufferedEnd);
		check(events.size() == 2 && events[0].stamp.span == events[1].stamp.span
			&& events[0].stamp.qpc < events[1].stamp.qpc && GetLastError() == 701,
			"buffered shutdown boundaries keep one identity after deferred emission");
		char formatted[512]{};
		const int count = Trace::Format(formatted, sizeof(formatted), bufferedEnd);
		const std::string line(formatted);
		char truncated[8]{};
		check(count > 0 && line.find("[ShutdownTrace] v=1 source=native pid=7 tid=11") == 0
			&& line.find("event=end stage=test-buffered result=returned error=42") != std::string::npos
			&& Trace::Format(truncated, sizeof(truncated), bufferedEnd) == 0 && GetLastError() == 701,
			"shutdown protocol is distinct, numeric and drops truncated records while preserving LastError");

		events.clear();
		{
			Trace::Scope parent(recorder, "test-parent");
			{
				Trace::Scope child(recorder, "test-child");
				SetLastError(702); // 诊断析构必须保留业务调用刚产生的错误，而不是旧值。
			}
			check(GetLastError() == 702, "nested shutdown scope preserves the latest business LastError");
		}
		check(events.size() == 4 && events[0].stamp.span == events[3].stamp.span
			&& events[1].stamp.span == events[2].stamp.span && events[0].stamp.span != events[1].stamp.span
			&& std::string(events[0].event) == "begin" && std::string(events[2].event) == "end"
			&& GetLastError() == 702, "nested shutdown spans pair without affecting business errors");
		try
		{
			Trace::Scope trace(recorder, "test-exception");
			throw std::runtime_error("original shutdown error");
		}
		catch (const std::runtime_error& error)
		{
			check(std::string(error.what()) == "original shutdown error", "shutdown scopes preserve original exceptions");
		}
		check(events.size() == 6 && std::string(events.back().result) == "exception",
			"exceptional shutdown scopes close their original span");

		Trace::Recorder failing(true, ShutdownThrow, clock);
		SetLastError(703);
		int businessSteps = 0;
		{
			Trace::Scope trace(failing, "test-sink-failure");
			check(GetLastError() == 703, "failed shutdown begin output preserves LastError");
			++businessSteps;
		}
		check(businessSteps == 1 && GetLastError() == 703,
			"shutdown output failure cannot escape, skip business work or change LastError");
		// 不创建窗口/控制台；临时无效 stderr 验证原始输出失败后安全返回。
		const HANDLE originalError = GetStdHandle(STD_ERROR_HANDLE);
		const BOOL redirected = SetStdHandle(STD_ERROR_HANDLE, INVALID_HANDLE_VALUE);
		bool rawPreserved = false;
		if (redirected)
		{
			SetLastError(704);
			Trace::WriteRaw(bufferedEnd);
			rawPreserved = GetLastError() == 704;
			(void)SetStdHandle(STD_ERROR_HANDLE, originalError);
		}
		check(redirected && rawPreserved, "missing shutdown stderr is harmless and preserves LastError");
		perfEvents = nullptr;
	}
	const std::wstring descriptor = LR"({"schemaVersion":1,"provider":"PowerPoint","status":"StableSlideIds","fullName":"C:\\slides\\a.pptx","presentationName":"a.pptx","applicationProcessId":12,"slideShowHwnd":34,"currentPage":2,"totalPage":3,"currentSlideId":202,"slideIds":[101,202,303],"bindingRevision":9})";
	const auto envelope = [&](std::wstring lifecycle, std::wstring status,
		std::wstring nested = L"")
	{
		return L"{\"schemaVersion\":1,\"stateRevision\":10,\"showSessionRevision\":2,\"bindingRevision\":9,\"lifecycle\":\""
			+ lifecycle + L"\",\"pageStatus\":\"" + status + L"\",\"descriptor\":"
			+ (nested.empty() ? descriptor : nested) + L"}";
	};
	const auto valid = ParsePptSessionSnapshot(envelope(L"Active", L"Valid"));
	check(valid.snapshot && valid.snapshot->showSessionRevision == 2
		&& valid.snapshot->descriptor.currentSlideId == 202,
		"session parser delegates stable identity validation to real descriptor parser");
	check(!ParsePptSessionSnapshot(envelope(L"Inactive", L"Valid")).snapshot,
		"inactive snapshot cannot claim a writable valid page");
	check(!ParsePptSessionSnapshot(envelope(L"Active", L"GuessEnd")).snapshot,
		"unknown status cannot masquerade as an end screen");
	std::wstring mismatched = envelope(L"Active", L"Valid");
	mismatched.replace(mismatched.find(L"\"bindingRevision\":9"),
		std::wstring(L"\"bindingRevision\":9").size(), L"\"bindingRevision\":8");
	check(!ParsePptSessionSnapshot(mismatched).snapshot,
		"session and descriptor must belong to same binding");
	const std::wstring unavailable = LR"({"schemaVersion":1,"provider":"PowerPoint","status":"Unavailable","fullName":"C:\\slides\\a.pptx","presentationName":"a.pptx","applicationProcessId":12,"slideShowHwnd":34,"currentPage":0,"totalPage":3,"currentSlideId":null,"slideIds":[],"bindingRevision":9})";
	check(ParsePptSessionSnapshot(envelope(L"Active", L"EndScreen", unavailable)).snapshot.has_value(),
		"explicit end screen preserves v1 descriptor without fabricating slide identity");
	const std::wstring endTopology = LR"({"schemaVersion":1,"provider":"PowerPoint","status":"StableSlideIds","fullName":"C:\\slides\\a.pptx","presentationName":"a.pptx","applicationProcessId":12,"slideShowHwnd":34,"currentPage":0,"totalPage":3,"currentSlideId":null,"slideIds":[101,202,303],"bindingRevision":9})";
	check(ParsePptSessionSnapshot(envelope(L"Active", L"EndScreen", endTopology)).snapshot.has_value() &&
		!ParsePptSessionSnapshot(envelope(L"Active", L"Valid", endTopology)).snapshot,
		"only EndScreen envelope can authorize a zero-current-page stable topology");
	const auto stableEnd = ParsePptSessionSnapshot(envelope(L"Active", L"EndScreen", endTopology));
	check(stableEnd.snapshot && Inkeys::Drawing::Draw3::ResolveEndScreenTarget(stableEnd.snapshot->descriptor)
		&& ResolvePptControlPage(*stableEnd.snapshot, -1, 3).known
		&& !ResolvePptControlPage(*stableEnd.snapshot, 3, 3).known
		&& !ResolvePptControlPage(*stableEnd.snapshot, -1, 4).known,
		"stable end topology still requires matching raw end state and total before writable admission");
	check(ParsePptSessionSnapshot(envelope(L"Unknown", L"Unknown", unavailable)).snapshot.has_value(),
		"read failures stay unknown instead of exiting the workspace");
	check(!ParsePptSessionSnapshot(envelope(L"Active", L"Valid", unavailable)).snapshot,
		"unavailable descriptor cannot open input through valid status");
	const std::wstring scalarOnly = LR"({"schemaVersion":1,"provider":"PowerPoint","status":"Unavailable","fullName":"","presentationName":"","applicationProcessId":0,"slideShowHwnd":0,"currentPage":2,"totalPage":3,"currentSlideId":null,"slideIds":[],"bindingRevision":9})";
	const auto parsedBasic = ParsePptSessionSnapshot(envelope(L"Active", L"Unknown", scalarOnly));
	check(parsedBasic.snapshot && ResolvePptControlPage(*parsedBasic.snapshot, 2, 3).known
		&& !Inkeys::Drawing::Draw3::ResolvePresentationTarget(parsedBasic.snapshot->descriptor),
		"real wire parser accepts fresh scalar controls while refusing an ink identity");
	if (valid.snapshot)
	{
		auto windowless = *valid.snapshot;
		windowless.descriptor.slideShowHwnd = 0;
		windowless.descriptor.applicationProcessId = 0;
		const auto basic = ResolvePptControlPage(windowless, 2, 3);
		check(CanStartPptControlSession(windowless) && basic.known
			&& basic.currentPage == 2 && basic.totalPage == 3,
			"fresh owner page keeps basic controls without a typed HWND");
		check(!ResolvePptControlPage(windowless, 1, 3).known
			&& !ResolvePptControlPage(windowless, 2, 4).known,
			"raw and owner page disagreement cannot publish fresh basic numbers");
		windowless.descriptor.status =
			Inkeys::Drawing::Draw3::PresentationDescriptorStatus::PageIndexFallback;
		windowless.descriptor.slideIds.clear();
		windowless.descriptor.currentSlideId.reset();
		windowless.descriptor.fullName.clear();
		windowless.descriptor.presentationName.clear();
		check(ResolvePptControlPage(windowless, 2, 3).known,
			"missing metadata and stable IDs do not block basic page controls");
		windowless.pageStatus = PptPageStatus::Unknown;
		windowless.descriptor.status =
			Inkeys::Drawing::Draw3::PresentationDescriptorStatus::Unavailable;
		check(ResolvePptControlPage(windowless, 2, 3).known,
			"fresh scalar page evidence survives a metadata-unavailable descriptor");
		windowless.descriptor.status =
			Inkeys::Drawing::Draw3::PresentationDescriptorStatus::TransientBusy;
		check(CanStartPptControlSession(windowless)
			&& !ResolvePptControlPage(windowless, 2, 3).known,
			"busy preserves an active scene without making an old raw page fresh");
		windowless.lifecycle = PptLifecycle::Unknown;
		check(!CanStartPptControlSession(windowless),
			"a failed activity probe cannot create a basic control session");
	}
	const auto basicEnd = ParsePptSessionSnapshot(envelope(L"Active", L"EndScreen", unavailable));
	check(basicEnd.snapshot && ResolvePptControlPage(*basicEnd.snapshot, -1, 3).known
		&& !ResolvePptControlPage(*basicEnd.snapshot, 3, 3).known
		&& !Inkeys::Drawing::Draw3::ResolveEndScreenTarget(basicEnd.snapshot->descriptor),
		"explicit end state can publish basic controls without fabricating topology");
	// Done/Count 是本次证据；缺拓扑和 HWND 时也只发布基础结束控制，不生成墨迹目标。
	const std::wstring scalarEnd = LR"({"schemaVersion":1,"provider":"Wps","status":"Unavailable","fullName":"C:\\Lessons\\Deck.pptx","presentationName":"Deck.pptx","applicationProcessId":0,"slideShowHwnd":0,"currentPage":0,"totalPage":3,"currentSlideId":null,"slideIds":[],"bindingRevision":9})";
	const auto parsedBasicEnd = ParsePptSessionSnapshot(envelope(L"Active", L"EndScreen", scalarEnd));
	check(parsedBasicEnd.snapshot
		&& ResolvePptControlPage(*parsedBasicEnd.snapshot, -1, 3).known
		&& ResolvePptControlPage(*parsedBasicEnd.snapshot, -1, 3).currentPage == -1
		&& ResolvePptControlPage(*parsedBasicEnd.snapshot, -1, 3).totalPage == 3
		&& !ResolvePptControlPage(*parsedBasicEnd.snapshot, -1, 4).known
		&& !Inkeys::Drawing::Draw3::ResolvePresentationTarget(parsedBasicEnd.snapshot->descriptor)
		&& !Inkeys::Drawing::Draw3::ResolveEndScreenTarget(parsedBasicEnd.snapshot->descriptor),
		"fresh windowless Done wire keeps basic end controls while refusing all ink targets");

	// 执行生产缓存/Bridge/回执谓词；不创建 HWND，也不模拟 Office 已经呈现。
	{
		namespace D3 = Inkeys::Drawing::Draw3;
		auto desc = valid.snapshot->descriptor;
		desc.currentPage = 1; desc.currentSlideId = 101;
		PptTargetCache cache;
		D3::Bridge::StateBridge bridge;
		check(cache.Update(true, &desc, true, false, 1) && cache.Current(), "initial target resolves");
		auto first = bridge.PublishPresentationTarget(*cache.target);
		cache.target->targetRevision = first.value_or(0);
		desc.currentPage = 2; desc.currentSlideId = 202;
		check(!cache.Update(true, &desc, false, false, 1) && !cache.Current(),
			"raw A with descriptor B retains pending application and rejects stale A");
		check(cache.Update(false, &desc, true, false, 1) && cache.Current()
			&& cache.target->pageIndex == 1 && cache.target->slideId == 202,
			"raw catches up without a new descriptor revision and resolves B");
		const auto second = bridge.PublishPresentationTarget(*cache.target);
		cache.target->targetRevision = second.value_or(0);
		check(second && first && *second != *first
			&& MatchesPptPageUi(*cache.target, 1, *second, 2, 3)
			&& !MatchesPptPageUi(*cache.target, 1, *first, 1, 3),
			"new page accepts only its own UI acknowledgement");
		for (int tick = 0; tick != 10000; ++tick)
			check(!cache.Update(false, &desc, true, false, 1) && cache.Current()
				&& bridge.PublishPresentationTarget(*cache.target) == second,
				"unchanged heartbeat preserves converged target identity");
		// raw 先到时仍等待 descriptor；随后多次 descriptor 变化只应用最后可信目标。
		check(!cache.Update(false, &desc, false, false, 1), "raw-first mismatch does not resolve");
		desc.currentPage = 3; desc.currentSlideId = 303;
		check(!cache.Update(true, &desc, false, false, 1), "rapid page C waits for raw");
		desc.currentPage = 1; desc.currentSlideId = 101;
		check(cache.Update(true, &desc, true, false, 1) && cache.target->pageIndex == 0,
			"rapid changes converge to latest trusted descriptor");
		desc.status = D3::PresentationDescriptorStatus::Unavailable;
		check(!cache.Update(true, &desc, true, false, 1) && !cache.Current(),
			"failed resolution cannot publish previous target");
		desc.status = D3::PresentationDescriptorStatus::StableSlideIds;
		check(cache.Update(false, &desc, true, false, 1), "failed resolution remains retryable");
		desc.currentPage = 0; desc.currentSlideId.reset();
		check(!cache.Update(true, &desc, false, false, 1) &&
			cache.Update(false, &desc, false, true, 1) && cache.target->pageIndex == 3,
			"end screen pending update converges without another envelope");
		cache.target->targetRevision = 9;
		check(MatchesPptPageUi(*cache.target, 1, 9, -1, 3)
			&& !MatchesPptPageUi(*cache.target, 1, 9, 3, 3), "end screen requires distinct UI identity");
		desc.currentPage = 2; desc.currentSlideId = 202;
		check(cache.Update(true, &desc, true, false, 2) && cache.target->sessionRevision == 2
			&& !MatchesPptPageUi(*cache.target, 1, 9, 2, 3), "new session rejects previous UI ack");
		desc.slideIds = {303,202,101};
		check(cache.Update(true, &desc, true, false, 2) && cache.target->slideIds == desc.slideIds,
			"same page topology change replaces full target identity");
		cache.target.reset();
		check(cache.Update(false, &desc, true, false, 2), "cleared session cache resolves even without notification");

		// UI 没看到暂停占位帧时，原目标曾经 ack 的状态不能吞掉恢复回执。
		const auto before = bridge.Snapshot().presentationTarget;
		const auto identity = D3::Bridge::ReadyIdentityFor(*before);
		for (int tick = 0; tick != 1000; ++tick)
			check(bridge.RequirePresentationTargetRefresh(identity)
				&& bridge.Snapshot().presentationTarget->targetRevision == identity.targetRevision,
				"repeated suspension is idempotent until trusted publication");
		const auto resumed = bridge.PublishPresentationTarget(*before);
		check(resumed && *resumed != identity.targetRevision,
			"same-page resume creates new identity for Present and UI ack");
		check(!bridge.RequirePresentationTargetRefresh(identity)
			&& bridge.PublishPresentationTarget(*before) == resumed,
			"late suspension cannot invalidate resumed page and heartbeat stays stable");
		const auto resumedTarget = bridge.Snapshot().presentationTarget;
		check(!MatchesPptPageUi(*resumedTarget, 1, identity.targetRevision, 2, 3)
			&& MatchesPptPageUi(*resumedTarget, 1, *resumed, 2, 3),
			"old same-page UI acknowledgement is rejected after resume");
		const auto resumedIdentity = D3::Bridge::ReadyIdentityFor(*resumedTarget);
		check(bridge.RequirePresentationTargetRefresh(resumedIdentity), "arm refresh before rejected publication");
		auto invalid = *resumedTarget;
		invalid.totalPages = 0;
		check(!bridge.PublishPresentationTarget(invalid)
			&& bridge.Snapshot().presentationTarget->targetRevision == *resumed,
			"invalid publication preserves current target");
		const auto afterRejected = bridge.PublishPresentationTarget(*resumedTarget);
		check(afterRejected && *afterRejected != *resumed,
			"rejected publication retains refresh obligation for valid retry");

		auto beforeClear = bridge.Snapshot().presentationTarget;
		const auto clearIdentity = D3::Bridge::ReadyIdentityFor(*beforeClear);
		check(bridge.RequirePresentationTargetRefresh(clearIdentity) && bridge.ClearPresentationTarget()
			&& !bridge.RequirePresentationTargetRefresh(clearIdentity), "cleared target rejects late refresh");
		const auto afterClear = bridge.PublishPresentationTarget(*beforeClear);
		check(afterClear && *afterClear != clearIdentity.targetRevision
			&& !bridge.RequirePresentationTargetRefresh(clearIdentity)
			&& bridge.PublishPresentationTarget(*beforeClear) == afterClear,
			"republication after clear has new stable identity unaffected by old refresh");

		auto beforeReset = bridge.Snapshot().presentationTarget;
		const auto resetIdentity = D3::Bridge::ReadyIdentityFor(*beforeReset);
		check(bridge.RequirePresentationTargetRefresh(resetIdentity), "arm refresh before Bridge reset");
		bridge.Reset();
		check(!bridge.RequirePresentationTargetRefresh(resetIdentity), "Bridge reset rejects old refresh identity");
		const auto afterReset = bridge.PublishPresentationTarget(*beforeReset);
		check(afterReset && *afterReset != resetIdentity.targetRevision
			&& !bridge.RequirePresentationTargetRefresh(resetIdentity)
			&& bridge.PublishPresentationTarget(*beforeReset) == afterReset,
			"Bridge reset preserves revision uniqueness and clears pending refresh");

		const auto previousSession = bridge.Snapshot().presentationTarget;
		const auto previousIdentity = D3::Bridge::ReadyIdentityFor(*previousSession);
		check(bridge.RequirePresentationTargetRefresh(previousIdentity), "arm refresh before new session");
		auto newSession = *previousSession;
		++newSession.sessionRevision;
		const auto newRevision = bridge.PublishPresentationTarget(newSession);
		check(newRevision && *newRevision != previousIdentity.targetRevision
			&& !bridge.RequirePresentationTargetRefresh(previousIdentity)
			&& bridge.PublishPresentationTarget(newSession) == newRevision,
			"new session rejects previous-session refresh and retains stable revision");
	}

	PptSessionToken original{ 1, 2, 3, 4, 5, 6, true, true };
	check(MatchesPptSession(original, original), "current request matches exact session");
	auto reopened = original;
	++reopened.showSessionRevision;
	check(!MatchesPptSession(original, reopened), "same HWND and binding reentry rejects old confirmation");
	reopened = original; ++reopened.serviceGeneration;
	check(!MatchesPptSession(original, reopened), "new service instance rejects old command");
	reopened = original; reopened.active = false;
	check(!MatchesPptSession(original, reopened), "ended show rejects queued command");
	auto noWindow = original;
	noWindow.showWindow = 0; noWindow.processId = 0;
	check(!IsPptWindowConfirmedEnded(noWindow, false, 0)
		&& MatchesPptSession(noWindow, noWindow),
		"HWND zero is an unknown window, not proof of COM show exit");
	check(IsPptWindowConfirmedEnded(original, false, 0)
		&& IsPptWindowConfirmedEnded(original, true, original.processId + 1)
		&& !IsPptWindowConfirmedEnded(original, true, 0),
		"only a destroyed window or successfully observed different owner ends the show");
	original.processStartTime = 100;
	check(IsPptWindowConfirmedEnded(original, true, original.processId, 200)
		&& !IsPptWindowConfirmedEnded(original, true, original.processId, 0),
		"known process creation identity detects PID reuse without guessing on query failure");
	if (valid.snapshot)
	{
		auto sameShow = *valid.snapshot;
		auto matching = noWindow;
		matching.showSessionRevision = sameShow.showSessionRevision;
		matching.bindingRevision = sameShow.bindingRevision;
		check(MatchesPptShowSession(matching, matching.serviceGeneration, sameShow),
			"basic scene identity is independent of a window and drawing readiness");
		sameShow.lifecycle = PptLifecycle::Unknown;
		check(MatchesPptShowSession(matching, matching.serviceGeneration, sameShow),
			"same show read failure may retain the previous suspended display");
		++sameShow.bindingRevision;
		check(!MatchesPptShowSession(matching, matching.serviceGeneration, sameShow),
			"new document cannot borrow an old scene or ink after busy");
		--sameShow.bindingRevision; ++sameShow.showSessionRevision;
		check(!MatchesPptShowSession(matching, matching.serviceGeneration, sameShow),
			"windowless reentry still requires a new control session");
	}

	// 执行生产焦点策略和离场交接，全部使用纯窗口身份，不调用 Win32 激活 API。
	{
		const PptFocusForeground external{ 1001, 1002 };
		check(IsFirstPptWindowAcquisition(noWindow, original.showWindow)
			&& !IsFirstPptWindowAcquisition(original, original.showWindow)
			&& !IsFirstPptWindowAcquisition(noWindow, 0),
			"only first trusted HWND acquisition may focus, not known-anchor recovery or repetition");
		const PptFocusRequest request{ original, external };
		PptFocusState state{ original, external, true, true, false, true, true,
			original.processId, original.processStartTime };
		check(CanFocusPptSession(request, state),
			"explicit noactivate click can hand off unchanged Explorer foreground");
		for (int repeat = 0; repeat != 16; ++repeat)
			check(CanFocusPptSession(request, state),
				"up move and longpress input do not invalidate unchanged foreground");
		auto changed = state;
		changed.foreground.window += 1;
		check(!CanFocusPptSession(request, changed), "different foreground window rejects queued focus");
		changed = state; changed.foreground.processId += 1;
		check(!CanFocusPptSession(request, changed), "reused foreground HWND rejects queued focus");
		changed = state; changed.foreground = { original.showWindow, original.processId };
		check(CanFocusPptSession(request, changed), "already focused show needs no activation");
		changed = state; ++changed.session.localSession;
		check(!CanFocusPptSession(request, changed), "same show HWND cannot borrow a newer session");
		changed = state; changed.session.active = false;
		check(!CanFocusPptSession(request, changed), "ended show cannot regain focus");
		changed = state; changed.modalActive = true;
		check(!CanFocusPptSession(request, changed), "settings and modal dialogs block focus");
		changed = state; changed.workspaceAllowed = false;
		check(!CanFocusPptSession(request, changed), "background whiteboard and its transaction block focus");
		changed = state; changed.controlAvailable = false;
		check(!CanFocusPptSession(request, changed), "untrusted show window cannot focus");
		changed = state; changed.targetExists = false;
		check(!CanFocusPptSession(request, changed), "destroyed show HWND cannot focus");
		changed = state; changed.targetVisible = false;
		check(!CanFocusPptSession(request, changed), "hidden show window cannot focus");
		changed = state; ++changed.targetProcessId;
		check(!CanFocusPptSession(request, changed), "reused show HWND cannot focus another process");
		changed = state; ++changed.targetProcessStartTime;
		check(!CanFocusPptSession(request, changed), "reused Office PID cannot inherit focus");
		changed = state; changed.session = noWindow;
		check(!CanFocusPptSession({ noWindow, external }, changed), "HWND zero basic controls cannot focus");

		PptFocusHandoff handoff;
		const PptFocusForeground whiteboard{ 2001, 2002 };
		handoff.Begin({ original, whiteboard });
		handoff.RebaseAfterWindowTransition(whiteboard, external, true);
		const auto returned = handoff.Take();
		check(returned && MatchesPptSession(returned->session, original)
			&& returned->foreground.window == external.window
			&& CanFocusPptSession(*returned, state) && !handoff.Take(),
			"controlled whiteboard Hide fallback retains old token and queues one valid handoff");
		handoff.Begin({ original, whiteboard });
		handoff.RebaseAfterWindowTransition(external, external);
		check(!handoff.Take(), "user switching away before whiteboard Hide cancels handoff");
		const PptFocusForeground anotherExternal{ 3001, 3002 };
		handoff.Begin(request);
		handoff.RebaseAfterWindowTransition(external, anotherExternal);
		check(!handoff.Take(), "external A to B during return is not a controlled Hide fallback");
		handoff.Begin(request);
		handoff.RebaseAfterWindowTransition(external, { external.window, external.processId + 1 });
		check(!handoff.Take(), "external HWND PID reuse during return cancels handoff");
		handoff.Begin({ original, whiteboard });
		handoff.RebaseAfterWindowTransition(whiteboard, external, true);
		handoff.RebaseAfterWindowTransition(external, anotherExternal);
		check(!handoff.Take(), "whiteboard fallback cannot authorize a later external foreground switch");
		handoff.Begin({ original, whiteboard });
		handoff.RebaseAfterWindowTransition(whiteboard, external, true);
		handoff.RebaseAfterWindowTransition(external, external);
		check(handoff.Take().has_value(), "unchanged external foreground preserves controlled fallback intent");
		changed = state; changed.foreground.window += 1;
		check(returned && !CanFocusPptSession(*returned, changed),
			"user switching after Hide but before dequeue still cancels focus");
		changed = state; ++changed.session.localSession;
		check(returned && !CanFocusPptSession(*returned, changed),
			"new show during whiteboard exit cannot claim old return intent");
		handoff.Begin({ noWindow, whiteboard });
		check(!handoff.Take(), "windowless whiteboard return has no focus request");
		handoff.Begin(request); handoff.Reset();
		check(!handoff.Take(), "whiteboard reentry cancels pending return focus");
	}

	BarA2CallbackDispatcher dispatcher;
	std::uint64_t first = 0, second = 0;
	dispatcher.SetStamped([&](std::uint64_t id) { first = id; });
	check(dispatcher.Dispatch() && !dispatcher.Dispatch(), "main exit single-flight spans pending dialog");
	dispatcher.Complete(first);
	dispatcher.SetStamped([&](std::uint64_t id) { second = id; });
	check(dispatcher.Dispatch() && second != first, "cancel allows a new request with different identity");
	dispatcher.Complete(first);
	check(!dispatcher.Dispatch(), "late completion cannot unlock a newer confirmation");
	dispatcher.Complete(second);
	dispatcher.SetStamped([](std::uint64_t) { throw 1; });
	check(!dispatcher.Dispatch(), "failed business dispatch releases its request");
	dispatcher.SetStamped([&](std::uint64_t id) { second = id; });
	check(dispatcher.Dispatch(), "dispatch works after failure");
	dispatcher.Complete(second);

	BarPptSceneState scene;
	check(scene.Publish(1, true) && scene.Take() == std::optional<bool>(true),
		"first live show creates exactly one enter transition");
	check(!scene.Publish(1, true) && !scene.Take(), "page heartbeat does not replay enter");
	check(scene.Publish(1, false) && scene.Take() == std::optional<bool>(false),
		"true exit creates exactly one exit transition");
	check(!scene.Publish(1, false), "duplicate exit is idempotent");
	BarPptSceneState rapidReentry;
	check(rapidReentry.Publish(1, true) && rapidReentry.Publish(1, false)
		&& rapidReentry.Publish(2, true), "native same-iteration exit and reentry are accepted");
	const auto reentryTransition = rapidReentry.Take();
	check(reentryTransition == std::optional<bool>(false) && rapidReentry.active
		&& rapidReentry.session == 2 && !rapidReentry.Take(),
		"pending exit obligation survives newer entry without replaying two animations");
	check(ShouldPlaceBarWorkspaceDock(false, BarBottomDockMode::BottomDocked),
		"expanded dock follows scene bottom without changing centering mode");
	check(!ShouldPlaceBarWorkspaceDock(true, BarBottomDockMode::BottomDocked) &&
		!ShouldPlaceBarWorkspaceDock(false, BarBottomDockMode::Floating) &&
		!ShouldPlaceBarWorkspaceDock(true, BarBottomDockMode::Floating),
		"folded and floating bars preserve their geometry on scene entry");
	check(ResolveBarWorkspaceBottomInsetDip(false, true, false, false) == 5.0
		&& ResolveBarWorkspaceBottomInsetDip(false, true, false, true) == 0.0
		&& ResolveBarWorkspaceBottomInsetDip(false, false, true, false) == 5.0
		&& ResolveBarWorkspaceBottomInsetDip(true, true, false, false) == 0.0,
		"whiteboard minimize switches to work-area dock without ending scene");
	BarWorkspaceSceneState workspace;
	check(workspace.Update(false, true) == BarWorkspaceTransition::Enter,
		"desktop to presentation captures baseline");
	check(workspace.Update(true, true) == BarWorkspaceTransition::Switch,
		"presentation to whiteboard preserves baseline");
	check(workspace.Update(true, false) == BarWorkspaceTransition::Keep,
		"background presentation exit cannot restore desktop under whiteboard");
	check(workspace.Update(false, false) == BarWorkspaceTransition::Leave,
		"last workspace exit restores desktop baseline");
	check(workspace.Update(true, false) == BarWorkspaceTransition::Enter,
		"desktop to whiteboard captures a fresh baseline");
	check(workspace.Update(false, true) == BarWorkspaceTransition::Switch,
		"return to a live show preserves baseline");
	check(workspace.Update(false, true) == BarWorkspaceTransition::Keep,
		"heartbeat and minimizing do not replay entry");
	check(workspace.Update(false, false) == BarWorkspaceTransition::Leave &&
		workspace.Update(false, false) == BarWorkspaceTransition::Keep,
		"desktop restoration occurs exactly once");
	return failures;
}
