#include "../Inkeys/Inkeys/Business/ScrollFollowDebug.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cmath>
#include <limits>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
	namespace Debug = Inkeys::Business::ScrollFollow::Debug;
	namespace CV = Inkeys::CV;
	namespace Capture = Inkeys::Graphics::FreezeCapture;
	using namespace std::chrono_literals;
	int failures = 0;
	void Check(bool value, const char* name)
	{
		if (!value) { ++failures; std::cerr << "FAIL scroll follow debug model " << name << '\n'; }
	}
	struct Image
	{
		int width = 19, height = 13, left = 3, top = 2;
		std::size_t stride = (19 + 7) * 4;
		std::vector<std::uint8_t> pixels = std::vector<std::uint8_t>(stride * (13 + 4));
		CV::MotionImageView View()
		{
			return { pixels.data() + top * stride + left * 4, (height - 1) * stride + width * 4, stride, width, height, CV::MotionPixelFormat::Bgra32 };
		}
		static std::uint32_t Pixel(std::uint64_t sequence, int x, int y) { return 0xFF000000u | (static_cast<std::uint32_t>(sequence) << 10) | (y * 32 + x); }
		void Fill(std::uint64_t sequence)
		{
			for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
			{
				const auto pixel = Pixel(sequence, x, y); std::memcpy(pixels.data() + (top + y) * stride + (left + x) * 4, &pixel, 4);
			}
		}
	};
	Debug::FrameMetadata Metadata(std::uint64_t sequence)
	{
		Debug::FrameMetadata result;
		result.identity = { 0, 41, 72, sequence, 9 }; result.operation = Debug::Operation::Process;
		result.roi = { 3, 2, 22, 15 }; result.captureWidth = 26; result.captureHeight = 17; result.captureStrideBytes = 104;
		result.geometry.sourceRect = { 100, 200, 125, 217 }; result.geometry.displayGeneration = 9;
		result.sampledAt = Capture::TimePoint{} + std::chrono::milliseconds(1000 + sequence * 50); result.sampleInterval = 50ms;
		result.statistics.accepted = sequence; return result;
	}
	CV::MotionResult Motion(std::uint64_t sequence, bool accepted = true)
	{
		CV::MotionResult result; result.accepted = accepted; result.reason = accepted ? CV::MotionReason::None : CV::MotionReason::ForwardBackward;
		result.state = accepted ? CV::MotionState::Tracking : CV::MotionState::Paused;
		result.statistics.inputSequence = sequence; result.statistics.referenceSequence = accepted ? sequence : sequence - 1;
		result.total = { -4.25, -7.5 }; result.statistics.consecutiveRejects = accepted ? 0 : 3; return result;
	}
	bool Publish(const Debug::ChannelPtr& channel, Image& image, std::uint64_t sequence, bool accepted = true)
	{
		image.Fill(sequence); const auto metadata = Metadata(sequence); auto lease = channel->BeginFrame(metadata, image.View());
		if (!lease) return false;
		auto* tracker = lease.Tracker(); tracker->inputSequence = sequence; tracker->result = Motion(sequence, accepted);
		tracker->pointCount = 1; tracker->points[0].reference.x = static_cast<double>(sequence);
		channel->RecordMotion(metadata, tracker->result, 2.5);
		return lease.Publish(accepted, 2.5);
	}
	bool Matches(const Debug::FramePacket& packet)
	{
		const auto sequence = packet.metadata.identity.sequence;
		if (packet.width != 19 || packet.height != 13 || packet.strideBytes != 76 || packet.bgra.size() != 19 * 13 * 4
			|| packet.tracker.inputSequence != sequence || packet.tracker.result.statistics.inputSequence != sequence
			|| packet.tracker.points[0].reference.x != static_cast<double>(sequence)) return false;
		for (int y = 0; y < packet.height; ++y) for (int x = 0; x < packet.width; ++x)
		{
			std::uint32_t pixel = 0; std::memcpy(&pixel, packet.bgra.data() + y * packet.strideBytes + x * 4, 4);
			if (pixel != Image::Pixel(sequence, x, y)) return false;
		}
		return true;
	}
	void TestCopyFreezeAndPause()
	{
		auto channel = Debug::Activate(); Check(channel != nullptr, "activation"); if (!channel) return;
		Image image; Check(Publish(channel, image, 1) && channel->ConsumeLatest(), "first publish/consume");
		std::fill(image.pixels.begin(), image.pixels.end(), std::uint8_t{0});
		const auto* frozen = channel->Displayed(); Check(frozen && Matches(*frozen), "owns padded odd ROI after source reuse");
		auto clone = channel->CloneDisplayed(); Check(clone && Matches(*clone), "immutable export clone");
		for (std::uint64_t n = 2; n <= 35; ++n) Check(Publish(channel, image, n), "frozen producer continues");
		Check(channel->Displayed() == frozen && frozen->metadata.identity.sequence == 1 && Matches(*frozen), "front pinned while other slots exchange");
		const auto live = channel->ReadLive();
		Check(live.count == 30 && live.summaries.front().identity.sequence == 6 && live.summaries[29].identity.sequence == 35, "bounded recent 30 numeric summaries");
		Check(live.counters.framesOverwritten > 0 && live.counters.trackerCalls == 35, "latest-only no image FIFO");
		Check(channel->ConsumeLatest() && channel->Displayed()->metadata.identity.sequence == 35, "unfreeze consumes latest only");
		Check(channel->Displayed()->totalTrackerMs == live.summaries[29].totalTrackerMs && channel->Displayed()->copyMs == live.summaries[29].copyMs, "same packet/summary timing");
		Check(channel->Displayed()->metadata.sampleInterval == 50ms && channel->Displayed()->metadata.roi.left == 3, "sample interval and ROI identity");
		Check(Publish(channel, image, 36, false) && channel->ConsumeLatest(), "terminal failed packet published");
		Debug::LiveStatus status; status.state = Inkeys::Business::ScrollFollow::State::Paused; status.businessSession = 41; status.captureSession = 72;
		status.ownsCapture = true; status.capture.session = 72; status.capture.reason = Capture::StopReason::Requested;
		status.pauseCause = Debug::PauseCause::TrackerPaused;
		for (int n = 0; n < 5; ++n) channel->UpdateStatus(status);
		Check(channel->Displayed()->metadata.identity.sequence == 36 && !channel->Displayed()->tracker.result.accepted, "status never replaces terminal image");
		const auto paused = channel->ReadLive();
		Check(paused.counters.trackerRejected == 1 && paused.counters.trackerCalls == 36, "status does not fabricate CV rejections");
		Check(paused.count && !paused.summaries[paused.count - 1].hasMotion, "state event has no fake MotionResult");
		Check(clone && clone->metadata.identity.sequence == 1 && Matches(*clone), "export remains click-time immutable");
		auto invalid = image.View(); invalid.sizeBytes = 1;
		Check(!channel->BeginFrame(Metadata(37), invalid), "invalid layout drops only diagnostic frame");
		Debug::Deactivate(channel); Check(!Debug::Enabled() && !Debug::Current(), "disabled fast path");
	}
	void TestCaptureFailureHistory()
	{
		auto channel = Debug::Activate(); if (!channel) { Check(false, "capture history activation"); return; }
		Debug::LiveStatus status; status.state = Inkeys::Business::ScrollFollow::State::Tracking;
		status.businessSession = 41; status.captureSession = 72; status.ownsCapture = true; status.capture.session = 72;
		status.pauseCause = Debug::PauseCause::CaptureApi; status.capture.failureStage = Capture::FailureStage::Read;
		status.capture.nativeError = 123; status.capture.nativeErrorValid = true;
		status.capture.lastFailureAt = Capture::TimePoint{} + 1803ms;
		status.capture.statistics.apiFailures = status.capture.statistics.rejected = 1;
		channel->UpdateStatus(status);
		status.capture.reason = Capture::StopReason::Requested; channel->UpdateStatus(status);
		Check(channel->ReadLive().count == 1, "Requested reason-only update is not a new failure");
		status.capture.reason = Capture::StopReason::None; status.capture.nativeError = 124; channel->UpdateStatus(status);
		// 同一个时刻也必须按阶段/有效错误码区分；后续 End 不能覆盖最初 Read。
		status.state = Inkeys::Business::ScrollFollow::State::Paused;
		status.capture.failureStage = Capture::FailureStage::End; status.capture.reason = Capture::StopReason::BackendFailure;
		status.capture.nativeError = 0; status.capture.nativeErrorValid = false; channel->UpdateStatus(status);
		const auto live = channel->ReadLive();
		Check(live.count == 3 && live.summaries[0].captureFailure.valid
			&& live.summaries[0].captureFailure.stage == Capture::FailureStage::Read
			&& live.summaries[0].captureFailure.reason == Capture::StopReason::None
			&& live.summaries[0].captureFailure.at == status.capture.lastFailureAt
			&& live.summaries[0].captureFailure.nativeErrorValid && live.summaries[0].captureFailure.nativeError == 123,
			"Read failure details survive later End snapshot");
		Check(live.summaries[1].captureFailure.nativeErrorValid && live.summaries[1].captureFailure.nativeError == 124,
			"same-time native error change has an independent event");
		Check(live.summaries[2].captureFailure.valid && live.summaries[2].captureFailure.stage == Capture::FailureStage::End
			&& !live.summaries[2].captureFailure.nativeErrorValid && live.status.capture.failureStage == Capture::FailureStage::End
			&& live.status.pauseCause == Debug::PauseCause::CaptureApi && !live.counters.trackerCalls,
			"latest End and original pause cause remain separate without fake CV");
		Debug::Deactivate(channel);
	}
	void TestConcurrentLatest()
	{
		auto channel = Debug::Activate(); if (!channel) { Check(false, "stress activation"); return; }
		std::atomic_bool finished = false, invalid = false;
		std::thread producer([&]
		{
			Image image;
			for (std::uint64_t n = 1; n <= 400; ++n) if (!Publish(channel, image, n)) invalid = true;
			finished.store(true, std::memory_order_release);
		});
		std::uint64_t last = 0;
		while (!finished.load(std::memory_order_acquire))
		{
			if (channel->ConsumeLatest())
			{
				const auto* packet = channel->Displayed();
				Check(packet && Matches(*packet) && packet->metadata.identity.sequence > last, "concurrent packet/image/marker identity");
				last = packet->metadata.identity.sequence;
			}
			else std::this_thread::yield();
		}
		producer.join(); if (channel->ConsumeLatest()) last = channel->Displayed()->metadata.identity.sequence;
		Check(!invalid.load() && channel->Displayed() && Matches(*channel->Displayed()) && last == 400, "stress reaches final complete packet");
		const auto live = channel->ReadLive(); Check(live.count <= 30 && live.counters.trackerCalls == 400 && live.counters.framesPublished == 400, "bounded racing counters/history");
		Debug::Deactivate(channel);
	}
	void TestCloseDuringLease()
	{
		auto old = Debug::Activate(); if (!old) { Check(false, "close activation"); return; }
		Image image; image.Fill(1); auto lease = old->BeginFrame(Metadata(1), image.View());
		std::weak_ptr<Debug::Channel> weak = old; const auto oldGeneration = old->Generation(); Debug::Deactivate(old);
		auto current = Debug::Activate(); Check(current && current->Generation() != oldGeneration, "recreate uses new generation");
		Debug::Deactivate(old); Check(Debug::Enabled() && Debug::Current() == current, "old close cannot deactivate new window");
		old.reset(); Check(!weak.expired(), "in-flight lease keeps closed storage alive");
		Check(!lease.Publish(false, 1.0) && weak.expired(), "closed publication dropped and storage released");
		Check(current && Publish(current, image, 2) && current->ConsumeLatest() && Matches(*current->Displayed()), "new channel accepts same follow session");
		Debug::Deactivate(current);
		Debug::WriteLease empty; Check(!empty && !empty.Tracker(), "empty lease no snapshot allocation");
	}
	void TestPauseLatch()
	{
		for (int n = 0; n < 4; ++n)
		{
			std::atomic<Debug::PauseCause> cause = Debug::PauseCause::None; std::atomic_bool go = false;
			std::thread a([&] { while (!go.load()) std::this_thread::yield(); Debug::LatchPause(cause, Debug::PauseCause::CaptureQuality); });
			std::thread b([&] { while (!go.load()) std::this_thread::yield(); Debug::LatchPause(cause, Debug::PauseCause::CaptureApi); });
			go = true; a.join(); b.join(); const auto first = cause.load(); Debug::LatchPause(cause, Debug::PauseCause::CaptureUnavailable);
			Check((first == Debug::PauseCause::CaptureQuality || first == Debug::PauseCause::CaptureApi) && cause.load() == first, "first concurrent failure survives Requested/unavailable");
		}
		std::atomic<Debug::PauseCause> cause = Debug::PauseCause::None; Debug::LatchPause(cause, Debug::PauseCause::InvalidRoi);
		Debug::LatchPause(cause, Debug::PauseCause::TrackerPaused); Check(cause.load() == Debug::PauseCause::InvalidRoi, "ROI cause survives later tracker/status changes");
	}
	void TestRecordingPrefixAndBounds()
	{
		auto channel=Debug::Activate(); if (!channel) { Check(false,"record activation"); return; }
		Image image; image.Fill(1); auto metadata=Metadata(1); metadata.operation=Debug::Operation::Initialize;
		auto motion=Motion(1); CV::MotionFrame input{image.View(),.05,1,true};
		channel->RecordInput(metadata,input,&motion); Check(channel->ReadRecording().state==Debug::RecordingState::Off && !channel->TakeRecording(),"record default off");
		Debug::LiveStatus live; live.businessSession=41; live.state=Inkeys::Business::ScrollFollow::State::Tracking; channel->UpdateStatus(live);
		Check(channel->ArmRecording(),"arm next session"); channel->RecordInput(metadata,input,&motion);
		Check(channel->ReadRecording().state==Debug::RecordingState::Armed && !channel->ReadRecording().frames,"arm excludes current session Initialize");
		metadata.identity.businessSession=42; channel->RecordInput(metadata,input,&motion);
		metadata.identity.sequence=input.sequence=2; input.timestampSeconds=.1; metadata.operation=Debug::Operation::AwaitTracking;
		channel->RecordInput(metadata,input); // 不申请预览 lease、不消费 UI，也必须保留该输入。
		metadata.identity.sequence=input.sequence=3; input.timestampSeconds=.15; metadata.operation=Debug::Operation::Process; motion=Motion(3);
		input.inputLowInformation=true; metadata.quality.status=Capture::QualityStatus::LowInformation; channel->RecordInput(metadata,input,&motion);
		std::fill(image.pixels.begin(),image.pixels.end(),std::uint8_t{0}); channel->FinishRecording(); const auto saved=channel->TakeRecording();
		Check(saved && saved->info.completePrefix && saved->inputs.size()==3 && saved->info.pixelBytes==3*19*13,"all producer inputs form owned bounded prefix");
		if (saved)
		{
			Check(saved->inputs[0].gray.size()==19*13 && saved->inputs[0].width==19 && saved->inputs[0].height==13
				&& saved->inputs[0].originalStrideBytes==image.stride && saved->inputs[0].metadata.identity.windowGeneration==channel->Generation(),"full odd padded ROI recording geometry");
			Check(saved->inputs[1].metadata.operation==Debug::Operation::AwaitTracking && !saved->inputs[1].hasMotion
				&& saved->inputs[2].inputLowInformation && saved->inputs[2].metadata.quality.status==Capture::QualityStatus::LowInformation
				&& saved->inputs[2].motion.statistics.inputSequence==3,"operation original quality timestamp and result retained");
			Check(saved->inputs[2].timestampSeconds==.15 && std::any_of(saved->inputs[0].gray.begin(),saved->inputs[0].gray.end(),[](auto x){return x!=0;}),"borrowed input reused after copy");
		}
		image.Fill(7); auto fresh=[&] { (void)channel->ArmRecording(); metadata=Metadata(1); metadata.identity.businessSession=43; metadata.operation=Debug::Operation::Initialize; input={image.View(),0,1,true}; motion=Motion(1); channel->RecordInput(metadata,input,&motion); };
		fresh();
		for (std::uint64_t seq=2;seq<=65;++seq) { metadata.identity.sequence=input.sequence=seq; metadata.operation=Debug::Operation::Process; input.timestampSeconds=seq*.01; motion=Motion(seq); channel->RecordInput(metadata,input,&motion); }
		const auto capped=channel->TakeRecording(); Check(capped && capped->info.stop==Debug::RecordingStop::FrameLimit && capped->inputs.size()==64 && capped->info.completePrefix,"64-input cap preserves complete prefix");
		fresh(); metadata.identity.sequence=input.sequence=2; metadata.operation=Debug::Operation::Process; input.timestampSeconds=10; motion=Motion(2); channel->RecordInput(metadata,input,&motion);
		Check(channel->ReadRecording().stop==Debug::RecordingStop::TimeLimit && channel->ReadRecording().frames==1,"10-second limit stops before out-of-window input");
		fresh(); metadata.identity.sequence=input.sequence=3; metadata.operation=Debug::Operation::Process; input.timestampSeconds=.1; motion=Motion(3); channel->RecordInput(metadata,input,&motion);
		const auto gap=channel->TakeRecording(); Check(gap && !gap->info.completePrefix && gap->info.state==Debug::RecordingState::Failed && gap->info.stop==Debug::RecordingStop::InputGap,"missing input cannot claim replay-complete prefix");
		fresh(); metadata.identity.sequence=input.sequence=2; metadata.operation=Debug::Operation::Process; input.timestampSeconds=.1; input.image.height=(std::numeric_limits<int>::min)(); channel->RecordInput(metadata,input,&motion);
		Check(channel->ReadRecording().stop==Debug::RecordingStop::InvalidInput,"invalid extreme layout fails diagnostic recording safely");
		Check(saved && saved->inputs.size()==3 && saved->inputs[0].gray.size()==19*13,"sealed export survives explicit re-arm");
		Debug::Deactivate(channel);
	}
	void TestRecordingGrayReplayParity()
	{
		auto channel=Debug::Activate(); if (!channel) { Check(false,"record parity activation"); return; }
		Check(channel->ArmRecording(),"parity arm"); CV::VisualMotionTracker original;
		cv::Mat canvas(430,570,CV_8UC4,cv::Scalar(238,233,229,255)); cv::RNG rng(0x715a49);
		for (int y=8;y<400;y+=17) for (int x=8;x<540;x+=19)
		{
			const cv::Scalar color(rng.uniform(10,170),rng.uniform(10,170),rng.uniform(10,170),255);
			cv::rectangle(canvas,{x,y,rng.uniform(5,14),rng.uniform(5,13)},color,rng.uniform(1,3));
		}
		int acceptedMotion=0;
		for (int index=0;index<5;++index)
		{
			const int offset=index<2?0:index==2?16:24; cv::Mat pixels=canvas(cv::Rect(30,30+offset,401,303));
			if (index==4) pixels=cv::Mat(303,401,CV_8UC4,cv::Scalar(255,255,255,255));
			auto metadata=Metadata(index+1); metadata.operation=index==0?Debug::Operation::Initialize:index==1?Debug::Operation::AwaitTracking:Debug::Operation::Process;
			metadata.roi={30,30,431,333}; metadata.captureWidth=canvas.cols; metadata.captureHeight=canvas.rows; metadata.captureStrideBytes=canvas.step[0];
			CV::MotionFrame frame{{pixels.ptr<std::uint8_t>(),pixels.step[0]*(pixels.rows-1)+pixels.cols*4,pixels.step[0],pixels.cols,pixels.rows,CV::MotionPixelFormat::Bgra32},.05*(index+1),static_cast<std::uint64_t>(index+1),true,index==4};
			if (index==1) channel->RecordInput(metadata,frame);
			else { const auto result=index==0?original.Initialize(frame):original.Process(frame); if (index>1 && index<4 && result.accepted) ++acceptedMotion; channel->RecordInput(metadata,frame,&result); }
		}
		const auto recorded=channel->TakeRecording(); Check(recorded && recorded->info.completePrefix && recorded->inputs.size()==5,"BGRA recording prefix for actual tracker replay");
		if (recorded)
		{
			CV::VisualMotionTracker replay;
			for (const auto& saved:recorded->inputs)
			{
				if (saved.metadata.operation==Debug::Operation::AwaitTracking) continue;
				const CV::MotionFrame frame{{saved.gray.data(),saved.gray.size(),static_cast<std::size_t>(saved.width),saved.width,saved.height,CV::MotionPixelFormat::Gray8},saved.timestampSeconds,saved.metadata.identity.sequence,saved.captureValid,saved.inputLowInformation};
				const auto actual=saved.metadata.operation==Debug::Operation::Initialize?replay.Initialize(frame):replay.Process(frame); const auto& expected=saved.motion;
				const auto& a=actual.statistics; const auto& e=expected.statistics;
				Check(actual.accepted==expected.accepted && actual.state==expected.state && actual.reason==expected.reason
					&& std::hypot(actual.total.x-expected.total.x,actual.total.y-expected.total.y)<1e-9
					&& std::hypot(actual.delta.x-expected.delta.x,actual.delta.y-expected.delta.y)<1e-9 && actual.confidence==expected.confidence
					&& a.referenceSequence==e.referenceSequence && a.referenceGeneration==e.referenceGeneration && a.relayGeneration==e.relayGeneration
					&& a.attempted==e.attempted && a.forward==e.forward && a.fbValid==e.fbValid && a.inliers==e.inliers
					&& a.targetId==e.targetId && a.targetPhase==e.targetPhase && a.targetPoints==e.targetPoints && a.provisionalPoints==e.provisionalPoints
					&& a.matchedTemplates==e.matchedTemplates && a.independentBlocks==e.independentBlocks && a.candidateCount==e.candidateCount
					&& a.primaryReason==e.primaryReason && a.expectedBlocks==e.expectedBlocks && a.verifiedBlocks==e.verifiedBlocks
					&& a.identityTiles==e.identityTiles && a.identityContradictions==e.identityContradictions && a.identityScore==e.identityScore
					&& a.identityDisambiguated==e.identityDisambiguated,
					"production BGRA and recorded Gray8 preserve non-timing motion/target result");
			}
		}
		Check(acceptedMotion>0,"gray parity fixture includes accepted motion, not only equal rejects"); Debug::Deactivate(channel);
	}
	std::int64_t fakeMs = 1000;
	Capture::TimePoint FakeNow() noexcept { return Capture::TimePoint{} + std::chrono::milliseconds(fakeMs); }
	struct Backend final : Capture::Backend
	{
		std::vector<std::uint32_t> pixels;
		Capture::BackendResult begin{true}, update{true}, read{true}; bool cleaned = true;
		Capture::BackendResult Begin(const Capture::Geometry& g) noexcept override
		{
			pixels.resize(static_cast<std::size_t>(g.width) * g.height);
			for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = 0xFF000000u | static_cast<unsigned>(i % 256) * 0x010101u;
			return begin;
		}
		Capture::BackendResult Update(const Capture::Geometry&) noexcept override { return update; }
		Capture::BackendResult Read(const Capture::Geometry&, std::span<const std::uint32_t>& out) noexcept override { fakeMs += 3; out = pixels; return read; }
		bool End(bool) noexcept override { return cleaned; }
	};
	Capture::Geometry Geometry() { return {{0,0,64,48},{0,0,63,47},{0,0,64,40},9,64,47}; }
	void Due(Capture::Session& session, Backend& backend)
	{
		fakeMs += session.NextDelay().count(); session.Pump(backend);
	}
	void TestCaptureObservations()
	{
		fakeMs = 1000; Capture::Session session(nullptr, &FakeNow); Backend backend; Capture::TimePoint delivered{};
		session.SetReady(true); Check(session.Start(Geometry(), [&](const auto& f) { delivered = f.timestamp; }).status == Capture::StartStatus::Started, "capture observation start");
		session.Pump(backend); session.Pump(backend); Due(session, backend);
		auto first = session.GetSnapshot(); Check(first.lastReadStarted == Capture::TimePoint{} + 1800ms && first.lastReadCompleted == Capture::TimePoint{} + 1803ms
			&& first.lastAcceptedSample == delivered && first.lastSampleInterval == 0us && first.statistics.accepted == 1 && first.qualityObserved, "actual first Read times");
		Due(session, backend); Due(session, backend); const auto second = session.GetSnapshot();
		Check(second.lastSampleInterval == second.lastReadCompleted - first.lastReadCompleted && second.statistics.accepted == 2, "Read-completion sample interval");
		backend.read = {false, Capture::StopReason::BackendFailure, 123, true}; Due(session, backend); Due(session, backend);
		const auto api = session.GetSnapshot(); Check(api.failureStage == Capture::FailureStage::Read && api.nativeErrorValid && api.nativeError == 123 && !api.qualityObserved
			&& api.lastAcceptedSample == second.lastAcceptedSample && api.statistics.apiFailures == 1 && api.statistics.rejected == 1 && api.publishing, "API observation preserves original retry/acceptance");
		backend.read = {true}; std::fill(backend.pixels.begin(), backend.pixels.end(), 0xFFFFFFFFu); Due(session, backend); Due(session, backend);
		const auto lowInfo=session.GetSnapshot(); Check(lowInfo.quality.status==Capture::QualityStatus::LowInformation && lowInfo.statistics.accepted==3
			&& lowInfo.statistics.lowInformation==1 && lowInfo.statistics.rejected==1 && lowInfo.lastAcceptedSample==delivered,"low information is delivered without fake rejection");
		std::fill(backend.pixels.begin(), backend.pixels.end(), 0xFF000000u); Due(session, backend); Due(session, backend);
		const auto quality = session.GetSnapshot(); Check(quality.failureStage == Capture::FailureStage::Quality && !quality.nativeErrorValid && quality.qualityObserved
			&& quality.lastAcceptedSample == lowInfo.lastAcceptedSample && quality.statistics.rejected == 2 && quality.statistics.apiFailures == 1, "severe black quality retains last acceptance without stale native error");
		// 两次真实采样之间不读取Debug状态，晚到的唯一状态通知仍必须保留黑帧失败的原始质量。
		std::fill(backend.pixels.begin(),backend.pixels.end(),0xffffffffu); Due(session,backend); Due(session,backend);
		const auto recovered=session.GetSnapshot(); auto history=Debug::Activate(); Check(history!=nullptr,"delayed capture failure history activation");
		if (history)
		{
			Debug::LiveStatus status; status.state=Inkeys::Business::ScrollFollow::State::Tracking; status.businessSession=41;
			status.captureSession=recovered.session; status.ownsCapture=true; status.capture=recovered; history->UpdateStatus(status);
			const auto live=history->ReadLive();
			Check(live.count==1 && live.status.capture.quality.status==Capture::QualityStatus::LowInformation
				&& live.summaries[0].captureFailure.stage==Capture::FailureStage::Quality && live.summaries[0].captureFailure.qualityObserved
				&& live.summaries[0].captureFailure.quality.status==Capture::QualityStatus::NearBlack
				&& live.summaries[0].captureFailure.at==quality.lastFailureAt,"single late UpdateStatus preserves black failure while live sample is white");
			Debug::Deactivate(history);
		}
		session.Stop(); session.Drain(backend); Check(session.GetSnapshot().failureStage == Capture::FailureStage::Quality, "Requested stop preserves failure source");
		fakeMs = 1000; Capture::Session timeout(nullptr, &FakeNow); Backend timeoutBackend; timeout.SetReady(true);
		(void)timeout.Start(Geometry(), [](const auto&) {}); timeout.Pump(timeoutBackend); timeout.Pump(timeoutBackend); fakeMs = 6000; timeout.Supervise();
		Check(timeout.GetSnapshot().failureStage == Capture::FailureStage::Timeout && timeout.GetSnapshot().reason == Capture::StopReason::StartupTimeout, "timeout source without forged tracker"); timeout.Drain(timeoutBackend);
		for (const bool beginFailure : {true, false})
		{
			fakeMs = 1000; Capture::Session failing(nullptr, &FakeNow); Backend b; if (beginFailure) b.begin = {false,Capture::StopReason::BackendFailure,5,true}; else b.update = {false,Capture::StopReason::BackendFailure,6,true};
			failing.SetReady(true); (void)failing.Start(Geometry(), [](const auto&) {}); failing.Pump(b); if (!beginFailure) failing.Pump(b);
			Check(failing.GetSnapshot().failureStage == (beginFailure ? Capture::FailureStage::Begin : Capture::FailureStage::Update) && failing.GetSnapshot().nativeErrorValid, "Begin/Update native observation"); failing.Stop(); failing.Drain(b);
		}
		fakeMs = 1000; Capture::Session callback(nullptr, &FakeNow); Backend b; callback.SetReady(true);
		(void)callback.Start(Geometry(), [](const auto&) { throw std::runtime_error("callback"); }); callback.Pump(b); callback.Pump(b); Due(callback,b);
		Check(callback.GetSnapshot().failureStage == Capture::FailureStage::Callback && callback.GetSnapshot().reason == Capture::StopReason::CallbackFailure, "callback failure source");
		b.cleaned = false; callback.Drain(b); Check(callback.GetSnapshot().failureStage == Capture::FailureStage::End, "End failure source");
	}
}
int RunScrollFollowDebugModelTests()
{
	failures = 0; TestCopyFreezeAndPause(); TestCaptureFailureHistory(); TestConcurrentLatest(); TestCloseDuringLease(); TestPauseLatch(); TestRecordingPrefixAndBounds(); TestRecordingGrayReplayParity(); TestCaptureObservations();
	std::cout << (failures ? "FAIL" : "PASS") << " scroll follow debug model failures=" << failures << '\n'; return failures;
}
