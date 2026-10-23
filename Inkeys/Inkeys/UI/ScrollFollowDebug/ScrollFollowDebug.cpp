#include "ScrollFollowDebug.h"

#include "../../Business/ScrollFollowDebug.h"
#include "../../Graphics/Surface.hpp"

#include <shobjidl.h>
#include <windowsx.h>
#include <json/json.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

import Inkeys.Window;

namespace Inkeys::UI::ScrollFollowDebug
{
	namespace
	{
		namespace Model = Business::ScrollFollow::Debug;
		namespace Capture = Graphics::FreezeCapture;
		using CV::TrackerDebugStage;
		using CV::TrackerDebugCheckId;
		using Clock = std::chrono::steady_clock;
		constexpr UINT_PTR RefreshTimer = 0x5346;
		constexpr COLORREF Background = RGB(15, 19, 27), Header = RGB(28, 34, 46);
		constexpr COLORREF CanvasBackground = RGB(22, 28, 38), Foreground = RGB(224, 231, 240);
		constexpr COLORREF InlierColor = RGB(64, 221, 118), StationaryColor = RGB(158, 166, 179);
		constexpr COLORREF CompetitorColor = RGB(255, 168, 54), ExcludedColor = RGB(255, 91, 100);
		constexpr COLORREF AddedColor = RGB(61, 219, 239), ForwardColor = RGB(239, 222, 142);
		std::atomic<HWND> currentWindow = nullptr, exportPickerWindow = nullptr;
		std::mutex probeMutex;
		Probe lastProbe{};

		struct ViewOptions
		{
			bool points = true, flow = true, grid = true, bounds = true, frozen = false;
			int arrowScale = 1;
			double zoom = 1, panX = 0, panY = 0, splitRatio = .66;
		};
		struct ExportData
		{
			std::unique_ptr<Model::FramePacket> frame;
			Model::LiveSnapshot live{};
			ViewOptions view{};
			RECT previewRect{};
		};

		std::wstring Wide(const char* value)
		{
			return value ? std::wstring(value, value + std::strlen(value)) : L"N/A";
		}
		template<class T> std::wstring Text(const T& value)
		{
			std::wostringstream stream; stream << std::setprecision(7) << value; return stream.str();
		}
		std::wstring NumberText(double value)
		{
			if (std::isfinite(value)) return Text(value);
			return std::isnan(value) ? L"NaN" : value < 0 ? L"-Infinity" : L"+Infinity";
		}
		const char* StateName(Business::ScrollFollow::State state) noexcept
		{
			switch (state)
			{
			case Business::ScrollFollow::State::Off: return "Off";
			case Business::ScrollFollow::State::Initializing: return "Initializing";
			case Business::ScrollFollow::State::Tracking: return "Tracking";
			case Business::ScrollFollow::State::Paused: return "Paused";
			case Business::ScrollFollow::State::Stopping: return "Stopping";
			default: return "Unknown";
			}
		}
		const char* MotionStateName(CV::MotionState state) noexcept
		{
			switch (state) { case CV::MotionState::Uninitialized: return "Uninitialized";
			case CV::MotionState::Tracking: return "Tracking"; case CV::MotionState::Paused: return "Paused"; default: return "Unknown"; }
		}
		const char* QualityName(Capture::QualityStatus status) noexcept
		{
			switch (status) { case Capture::QualityStatus::Valid: return "Valid";
			case Capture::QualityStatus::InvalidBuffer: return "InvalidBuffer";
			case Capture::QualityStatus::NearBlack: return "NearBlack"; case Capture::QualityStatus::LowInformation: return "LowInformation"; default: return "Unknown"; }
		}
		const char* StopName(Capture::StopReason reason) noexcept
		{
			switch (reason)
			{
#define STOP_NAME(name) case Capture::StopReason::name: return #name
			STOP_NAME(None); STOP_NAME(Requested); STOP_NAME(FreezeActive); STOP_NAME(Unavailable);
			STOP_NAME(GeometryChanged); STOP_NAME(StartupTimeout); STOP_NAME(ResultTimeout);
			STOP_NAME(BackendFailure); STOP_NAME(CallbackFailure); STOP_NAME(Shutdown);
#undef STOP_NAME
			default: return "Unknown";
			}
		}
		const char* Comparison(CV::TrackerDebugComparison comparison) noexcept
		{
			switch (comparison) { case CV::TrackerDebugComparison::Equal: return "==";
			case CV::TrackerDebugComparison::NotEqual: return "!="; case CV::TrackerDebugComparison::Less: return "<";
			case CV::TrackerDebugComparison::LessEqual: return "<="; case CV::TrackerDebugComparison::Greater: return ">";
			case CV::TrackerDebugComparison::GreaterEqual: return ">="; default: return "?"; }
		}
		bool Completed(const CV::TrackerDebugSnapshot& frame, TrackerDebugStage stage) noexcept
		{
			return (frame.completedStages & (std::uint64_t{1} << static_cast<unsigned>(stage))) != 0;
		}
		bool Reached(const CV::TrackerDebugSnapshot& frame, TrackerDebugStage stage) noexcept
		{
			return (frame.reachedStages & (std::uint64_t{1} << static_cast<unsigned>(stage))) != 0;
		}
		bool Evaluated(const CV::TrackerDebugSnapshot& frame, TrackerDebugCheckId id) noexcept
		{
			return frame.checks[static_cast<std::size_t>(id)].evaluated;
		}
		double AgeMs(Capture::TimePoint value) noexcept
		{
			return value == Capture::TimePoint{} ? std::numeric_limits<double>::quiet_NaN()
				: std::chrono::duration<double, std::milli>(Clock::now() - value).count();
		}
		std::wstring AgeText(Capture::TimePoint value)
		{
			return value == Capture::TimePoint{} ? L"N/A" : NumberText(AgeMs(value))+L" ms";
		}
		std::wstring VectorText(CV::MotionVector value)
		{
			return L"(" + NumberText(value.x) + L", " + NumberText(value.y) + L")";
		}
		std::wstring RectText(const Capture::Rect& rect)
		{
			return L"(" + Text(rect.left) + L"," + Text(rect.top) + L"," + Text(rect.right) + L"," + Text(rect.bottom) + L")";
		}

		template<class T> Json::Value Numeric(T value)
		{
			if constexpr (std::is_integral_v<T>)
			{
				if constexpr (std::is_signed_v<T>) return Json::Value(static_cast<Json::Int64>(value));
				else return Json::Value(static_cast<Json::UInt64>(value));
			}
			else
			{
				if (std::isfinite(value)) return Json::Value(static_cast<double>(value));
				return Json::Value(std::isnan(value) ? "NaN" : value < 0 ? "-Infinity" : "+Infinity");
			}
		}
		Json::Value TimeJson(Capture::TimePoint value)
		{
			return value == Capture::TimePoint{} ? Json::Value() : Numeric(std::chrono::duration_cast<std::chrono::microseconds>(value.time_since_epoch()).count());
		}
		Json::Value VectorJson(CV::MotionVector value)
		{
			Json::Value result; result["x"] = Numeric(value.x); result["y"] = Numeric(value.y); return result;
		}
		Json::Value RectJson(const Capture::Rect& rect)
		{
			Json::Value result; result["left"] = rect.left; result["top"] = rect.top;
			result["right"] = rect.right; result["bottom"] = rect.bottom; return result;
		}
		Json::Value IdentityJson(const Model::FrameIdentity& identity)
		{
			Json::Value result;
#define ID_FIELD(name) result[#name] = Numeric(identity.name)
			ID_FIELD(windowGeneration); ID_FIELD(businessSession); ID_FIELD(captureSession); ID_FIELD(sequence); ID_FIELD(displayGeneration);
#undef ID_FIELD
			return result;
		}
		Json::Value GeometryJson(const Capture::Geometry& geometry)
		{
			Json::Value result; result["primaryBounds"] = RectJson(geometry.primaryBounds); result["sourceRect"] = RectJson(geometry.sourceRect);
			result["workArea"] = RectJson(geometry.workArea); result["displayGeneration"] = Numeric(geometry.displayGeneration);
			result["width"] = geometry.width; result["height"] = geometry.height; return result;
		}
		Json::Value QualityJson(const Capture::Quality& quality)
		{
			Json::Value result; result["status"] = QualityName(quality.status); result["meanLuma"] = Numeric(quality.meanLuma);
			result["variance"] = Numeric(quality.variance); result["blackFraction"] = Numeric(quality.blackFraction); result["samples"] = quality.samples; return result;
		}
		Json::Value CaptureStatisticsJson(const Capture::Statistics& statistics)
		{
			Json::Value result;
#define CAPTURE_FIELD(name) result[#name] = Numeric(statistics.name)
			CAPTURE_FIELD(attempts); CAPTURE_FIELD(accepted); CAPTURE_FIELD(rejected); CAPTURE_FIELD(apiFailures); CAPTURE_FIELD(skipped); CAPTURE_FIELD(lateResults);
#undef CAPTURE_FIELD
			result["lastReadDurationUs"] = Numeric(statistics.lastReadDuration.count()); result["maxReadDurationUs"] = Numeric(statistics.maxReadDuration.count()); return result;
		}
		struct StatEntry { const char* name; Json::Value value; bool valid; };
		std::vector<StatEntry> Statistics(const CV::MotionStatistics& statistics, const CV::TrackerDebugSnapshot* debug)
		{
			const bool normal = !debug || debug->exception == CV::MotionReason::None;
			const auto done = [&](TrackerDebugStage stage) { return normal && (!debug || Completed(*debug, stage)); };
			const auto reached = [&](TrackerDebugStage stage) { return normal && (!debug || Reached(*debug, stage)); };
			const auto checked = [&](TrackerDebugCheckId id) { return normal && (!debug || Evaluated(*debug, id)); };
			std::vector<StatEntry> entries; entries.reserve(40);
#define STAT(name, validity) entries.push_back({ #name, Numeric(statistics.name), (validity) })
			STAT(inputSequence, true); STAT(referenceSequence, true); STAT(referenceGeneration, true); STAT(relayGeneration, true);
			STAT(attempted, reached(TrackerDebugStage::Forward)); STAT(forward, done(TrackerDebugStage::Forward));
			STAT(backward, done(TrackerDebugStage::Backward)); STAT(fbValid, checked(TrackerDebugCheckId::FbPointsLow));
			STAT(inliers, done(TrackerDebugStage::Consensus)); STAT(referenceCells, done(TrackerDebugStage::Forward));
			STAT(occupiedCells, reached(TrackerDebugStage::Consensus)); STAT(inlierCells, checked(TrackerDebugCheckId::CoverageCells));
			STAT(interiorCells, checked(TrackerDebugCheckId::CoverageCells)); STAT(added, done(TrackerDebugStage::Refill));
			STAT(retained, done(TrackerDebugStage::Retention)); STAT(retired, done(TrackerDebugStage::Retention));
			STAT(initialSurvivors, done(TrackerDebugStage::Retention) || (debug && debug->initialization && done(TrackerDebugStage::Commit)));
			STAT(consecutiveRejects, true); STAT(pointInlierRatio, done(TrackerDebugStage::Consensus)); STAT(spatialInlierRatio, done(TrackerDebugStage::Consensus));
			STAT(survivalRatio, checked(TrackerDebugCheckId::FbPointsLow)); STAT(coverageX, checked(TrackerDebugCheckId::CoverageCells));
			STAT(coverageY, checked(TrackerDebugCheckId::CoverageCells)); STAT(runnerUpRatio, reached(TrackerDebugStage::Ambiguity));
			STAT(fbMedian, checked(TrackerDebugCheckId::FbPointsLow)); STAT(fbP95, checked(TrackerDebugCheckId::FbPointsLow));
			STAT(residualMedian, done(TrackerDebugStage::Consensus)); STAT(residualP95, done(TrackerDebugStage::Consensus));
			STAT(affineVariation, normal && (!debug || debug->affineModelValid)); STAT(affineResidual, normal && (!debug || debug->affineModelValid));
			STAT(preprocessMs, done(TrackerDebugStage::Preprocess));
			STAT(opticalFlowMs, done(TrackerDebugStage::Backward) || (checked(TrackerDebugCheckId::ForwardPointsLow) && debug && debug->checks[static_cast<std::size_t>(TrackerDebugCheckId::ForwardPointsLow)].condition));
			STAT(fitMs, reached(TrackerDebugStage::Consensus)); STAT(refillMs, done(TrackerDebugStage::Refill));
#undef STAT
			return entries;
		}
		Json::Value MotionJson(const CV::MotionResult& motion, const CV::TrackerDebugSnapshot* debug = nullptr)
		{
			Json::Value result; result["accepted"] = motion.accepted; result["state"] = MotionStateName(motion.state);
			result["reason"] = CV::MotionReasonName(motion.reason); result["delta"] = VectorJson(motion.delta); result["total"] = VectorJson(motion.total);
			result["confidence"] = Numeric(motion.confidence);
			for (const auto& entry : Statistics(motion.statistics, debug))
			{
				result["statistics"][entry.name] = entry.value;
				result["statisticsValidity"][entry.name] = debug ? Json::Value(entry.valid) : Json::Value();
			}
			result["statisticsNote"] = debug ? "Raw returned values; false validity means not computed for this result."
				: "Numeric summary only; stage validity is available on the selected full tracker snapshot.";
			return result;
		}
		Json::Value BoundsJson(const CV::TrackerDebugBounds& bounds)
		{
			Json::Value result; result["valid"] = bounds.valid;
			result["minX"] = bounds.valid ? Numeric(bounds.minX) : Json::Value(); result["minY"] = bounds.valid ? Numeric(bounds.minY) : Json::Value();
			result["maxX"] = bounds.valid ? Numeric(bounds.maxX) : Json::Value(); result["maxY"] = bounds.valid ? Numeric(bounds.maxY) : Json::Value(); return result;
		}
		Json::Value CandidateJson(const CV::TrackerDebugCandidate& candidate,bool competitor = true)
		{
			Json::Value result; result["valid"] = candidate.valid; result["flow"] = candidate.valid ? VectorJson(candidate.flow) : Json::Value();
#define CANDIDATE_VALUE(name) result[#name] = candidate.valid ? Numeric(candidate.name) : Json::Value()
			CANDIDATE_VALUE(fitWeight); CANDIDATE_VALUE(points); CANDIDATE_VALUE(cells); CANDIDATE_VALUE(interiorCells);
#undef CANDIDATE_VALUE
			result["competitorOnlyValuesValid"] = candidate.valid && competitor;
			result["originalCellRatio"] = candidate.valid && competitor ? Numeric(candidate.originalCellRatio) : Json::Value();
			result["separation"] = candidate.valid && competitor ? Numeric(candidate.separation) : Json::Value();
			result["referenceBounds"] = BoundsJson(candidate.referenceBounds); result["currentBounds"] = BoundsJson(candidate.currentBounds);
#define CANDIDATE_BOOL(name) result[#name] = candidate.name
			CANDIDATE_BOOL(doubleRadiusEvaluated); CANDIDATE_BOOL(beyondDoubleRadius); CANDIDATE_BOOL(zeroLengthEvaluated); CANDIDATE_BOOL(zeroLength);
			CANDIDATE_BOOL(zeroRatioEvaluated); CANDIDATE_BOOL(zeroRatio); CANDIDATE_BOOL(interiorEvaluated); CANDIDATE_BOOL(enoughInterior);
#undef CANDIDATE_BOOL
			return result;
		}
		struct CellValidity
		{
			bool reference, forward, backward, consensus, ambiguity, weights, retention, refill;
		};
		CellValidity ValidCells(const CV::TrackerDebugSnapshot& debug) noexcept
		{
			return {debug.referenceBeforeValid,Completed(debug,TrackerDebugStage::Forward),Completed(debug,TrackerDebugStage::Backward),
				Completed(debug,TrackerDebugStage::Consensus),Reached(debug,TrackerDebugStage::Ambiguity),Reached(debug,TrackerDebugStage::Consensus),
				Completed(debug,TrackerDebugStage::Retention),Completed(debug,TrackerDebugStage::Refill)};
		}
		Json::Value TrackerJson(const CV::TrackerDebugSnapshot& debug)
		{
			Json::Value result;
#define TRACK_VALUE(name) result[#name] = Numeric(debug.name)
			TRACK_VALUE(inputSequence); TRACK_VALUE(referenceSequenceBefore); TRACK_VALUE(referenceSequenceAfter); TRACK_VALUE(referenceGenerationBefore); TRACK_VALUE(referenceGenerationAfter);
			TRACK_VALUE(inputWidth); TRACK_VALUE(inputHeight); TRACK_VALUE(processingWidth); TRACK_VALUE(processingHeight); TRACK_VALUE(referenceProcessingWidth); TRACK_VALUE(referenceProcessingHeight);
			TRACK_VALUE(inputStrideBytes); TRACK_VALUE(inputSizeBytes); TRACK_VALUE(timestampSeconds); TRACK_VALUE(scaleX); TRACK_VALUE(scaleY);
			TRACK_VALUE(pointBorder); TRACK_VALUE(consensusRadius); TRACK_VALUE(refinementDistanceLimit); TRACK_VALUE(fbErrorLimit); TRACK_VALUE(nearStationaryLimit); TRACK_VALUE(zeroModeRatioThreshold);
			TRACK_VALUE(reachedStages); TRACK_VALUE(completedStages); TRACK_VALUE(pointCount); TRACK_VALUE(addedPointCount); TRACK_VALUE(consensusCandidatesEvaluated); TRACK_VALUE(competitorsEvaluated);
#undef TRACK_VALUE
#define TRACK_BOOL(name) result[#name] = debug.name
			TRACK_BOOL(initialization); TRACK_BOOL(referenceBeforeValid); TRACK_BOOL(referenceAfterValid); TRACK_BOOL(captureValid); TRACK_BOOL(truncated);
			TRACK_BOOL(affineModelValid); TRACK_BOOL(ambiguityComplete); TRACK_BOOL(candidateTotalValid);
#undef TRACK_BOOL
			result["inputFormat"] = debug.inputFormat == CV::MotionPixelFormat::Bgra32 ? "BGRA32" : "Gray8";
			result["firstFailure"] = debug.firstFailure == TrackerDebugCheckId::Count ? Json::Value() : Json::Value(CV::TrackerDebugCheckName(debug.firstFailure));
			result["exception"] = CV::MotionReasonName(debug.exception);
			for (std::size_t index = 0; index < debug.checks.size(); ++index)
			{
				const auto& check = debug.checks[index]; Json::Value item; item["id"] = CV::TrackerDebugCheckName(static_cast<TrackerDebugCheckId>(index));
				item["evaluated"] = check.evaluated; item["condition"] = check.evaluated ? Json::Value(check.condition) : Json::Value();
				item["measured"] = check.evaluated ? Numeric(check.measured) : Json::Value(); item["threshold"] = check.evaluated ? Numeric(check.threshold) : Json::Value();
				item["comparison"] = check.evaluated ? Json::Value(Comparison(check.comparison)) : Json::Value(); result["checks"].append(std::move(item));
			}
			for (unsigned index = 0; index < static_cast<unsigned>(TrackerDebugStage::Count); ++index)
			{
				const auto stage = static_cast<TrackerDebugStage>(index); Json::Value item; item["name"] = CV::TrackerDebugStageName(stage);
				item["reached"] = Reached(debug, stage); item["completed"] = Completed(debug, stage); result["stages"].append(std::move(item));
			}
			for (std::size_t index = 0; index < (std::min)(debug.pointCount, debug.points.size()); ++index)
			{
				const auto& point = debug.points[index]; Json::Value item;
				item["reference"] = VectorJson(point.reference); item["forward"] = point.forwardComputed ? VectorJson(point.forward) : Json::Value();
				item["refined"] = point.refinedComputed ? VectorJson(point.refined) : Json::Value(); item["backward"] = point.backwardComputed ? VectorJson(point.backward) : Json::Value();
				item["anchor"] = VectorJson(point.anchor); item["birthGeneration"] = Numeric(point.birthGeneration); item["referenceCell"] = point.referenceCell; item["currentCell"] = point.currentCell;
				item["fbError"] = point.fbComputed ? Numeric(point.fbError) : Json::Value(); item["refinementDistance"] = point.refinementDistanceEvaluated ? Numeric(point.refinementDistance) : Json::Value();
				item["fitWeight"] = point.fbValid && Reached(debug, TrackerDebugStage::Consensus) ? Numeric(point.fitWeight) : Json::Value(); item["residual"] = point.residualComputed ? Numeric(point.residual) : Json::Value();
#define POINT_BOOL(name) item[#name] = point.name
				POINT_BOOL(forwardComputed); POINT_BOOL(forwardStatus); POINT_BOOL(forwardInsideEvaluated); POINT_BOOL(forwardInside); POINT_BOOL(refinedComputed);
				POINT_BOOL(backwardComputed); POINT_BOOL(backwardStatus); POINT_BOOL(backwardFiniteEvaluated); POINT_BOOL(backwardFinite); POINT_BOOL(fbComputed);
				POINT_BOOL(refinedInsideEvaluated); POINT_BOOL(refinedInside); POINT_BOOL(refinementDistanceEvaluated); POINT_BOOL(fbThresholdEvaluated); POINT_BOOL(fbValid);
				POINT_BOOL(consensusEvaluated); POINT_BOOL(inlier); POINT_BOOL(residualComputed); POINT_BOOL(nearStationary); POINT_BOOL(competitor); POINT_BOOL(retentionEvaluated); POINT_BOOL(retained); POINT_BOOL(retired);
#undef POINT_BOOL
				result["points"].append(std::move(item));
			}
			for (std::size_t index = 0; index < (std::min)(debug.addedPointCount, debug.addedPoints.size()); ++index)
			{
				const auto& point = debug.addedPoints[index]; Json::Value item; item["point"] = VectorJson(point.point); item["anchor"] = VectorJson(point.anchor);
				item["birthGeneration"] = Numeric(point.birthGeneration); item["cell"] = point.cell; item["committed"] = point.committed; result["addedPoints"].append(std::move(item));
			}
			const auto valid = ValidCells(debug);
			for (const auto& cell : debug.cells)
			{
				Json::Value item;
#define CELL_VALUE(name, computed) item[#name] = (computed) ? Numeric(cell.name) : Json::Value(); item["validity"][#name] = (computed)
				CELL_VALUE(reference,valid.reference); CELL_VALUE(forward,valid.forward); CELL_VALUE(fbValid,valid.backward); CELL_VALUE(inliers,valid.consensus);
				CELL_VALUE(competitor,valid.ambiguity); CELL_VALUE(nearStationary,valid.backward); CELL_VALUE(retained,valid.retention); CELL_VALUE(added,valid.refill);
				CELL_VALUE(fitWeight,valid.weights); CELL_VALUE(inlierWeight,valid.consensus); CELL_VALUE(originalCellWeight,valid.weights);
#undef CELL_VALUE
				item["competitorScanComplete"] = debug.ambiguityComplete;
				item["affineSampleValid"] = cell.affineSampleValid; item["affinePosition"] = cell.affineSampleValid ? VectorJson(cell.affinePosition) : Json::Value();
				item["affineFlow"] = cell.affineSampleValid ? VectorJson(cell.affineFlow) : Json::Value(); result["cells"].append(std::move(item));
			}
			result["mainMotion"] = CandidateJson(debug.mainMotion,false); result["strongestCompetitor"] = CandidateJson(debug.strongestCompetitor);
			result["eligibleRunnerUp"] = CandidateJson(debug.eligibleRunnerUp); result["rejectingCompetitor"] = CandidateJson(debug.rejectingCompetitor);
			result["totalBefore"] = VectorJson(debug.totalBefore); result["candidateDelta"] = debug.candidateTotalValid ? VectorJson(debug.candidateDelta) : Json::Value();
			result["candidateTotal"] = debug.candidateTotalValid ? VectorJson(debug.candidateTotal) : Json::Value(); result["result"] = MotionJson(debug.result, &debug); return result;
		}

		Json::Value CaptureSnapshotJson(const Capture::Snapshot& capture)
		{
			Json::Value result; result["session"] = Numeric(capture.session); result["sequence"] = Numeric(capture.sequence);
			result["geometry"] = GeometryJson(capture.geometry); result["statistics"] = CaptureStatisticsJson(capture.statistics);
			const bool qualityObserved=capture.qualityObserved, readObserved=capture.lastReadCompleted!=Capture::TimePoint{};
			result["qualityObserved"] = qualityObserved; result["quality"] = qualityObserved ? QualityJson(capture.quality) : Json::Value();
			result["readObserved"] = readObserved;
			if (!readObserved) { result["statistics"]["lastReadDurationUs"] = Json::Value(); result["statistics"]["maxReadDurationUs"] = Json::Value(); }
			result["reason"] = StopName(capture.reason); result["ready"] = capture.ready; result["publishing"] = capture.publishing;
			result["occupied"] = capture.occupied; result["inFlight"] = capture.inFlight;
			result["lastReadStartedUs"] = TimeJson(capture.lastReadStarted); result["lastReadCompletedUs"] = TimeJson(capture.lastReadCompleted);
			result["lastAcceptedSampleUs"] = TimeJson(capture.lastAcceptedSample); result["lastFailureAtUs"] = TimeJson(capture.lastFailureAt);
			result["lastSampleIntervalUs"] = Numeric(capture.lastSampleInterval.count()); result["lastSampleIntervalValid"] = capture.lastSampleInterval.count() > 0;
			result["failureStage"] = Capture::FailureStageName(capture.failureStage); result["nativeErrorValid"] = capture.nativeErrorValid;
			result["nativeError"] = capture.nativeErrorValid ? Numeric(capture.nativeError) : Json::Value(); return result;
		}
		Json::Value CaptureFailureJson(const Model::CaptureFailureInfo& failure)
		{
			if (!failure.valid) return Json::Value();
			Json::Value result; result["valid"] = true; result["stage"] = Capture::FailureStageName(failure.stage); result["reason"] = StopName(failure.reason);
			result["atUs"] = TimeJson(failure.at); result["qualityValid"] = failure.stage==Capture::FailureStage::Quality;
			result["quality"] = failure.stage==Capture::FailureStage::Quality ? QualityJson(failure.quality) : Json::Value();
			result["nativeErrorValid"] = failure.nativeErrorValid; result["nativeError"] = failure.nativeErrorValid ? Numeric(failure.nativeError) : Json::Value(); return result;
		}
		std::wstring CaptureFailureText(const Model::CaptureFailureInfo& failure)
		{
			if (!failure.valid) return L"N/A (no captured failure detail)";
			std::wstring result=Wide(Capture::FailureStageName(failure.stage))+L" / "+Wide(StopName(failure.reason));
			result+=L" native="+(failure.nativeErrorValid?Text(failure.nativeError):L"N/A");
			if (failure.stage==Capture::FailureStage::Quality) result+=L" quality="+Wide(QualityName(failure.quality.status));
			return result;
		}
		Json::Value LiveJson(const Model::LiveSnapshot& live)
		{
			Json::Value result; const auto& status = live.status; auto& current = result["status"];
			current["state"] = StateName(status.state); current["businessSession"] = Numeric(status.businessSession); current["captureSession"] = Numeric(status.captureSession);
			current["ownsCapture"] = status.ownsCapture; current["awaitTracking"] = status.awaitTracking; current["pauseCause"] = Model::PauseCauseName(status.pauseCause);
			current["captureObserved"] = status.ownsCapture; current["capture"] = status.ownsCapture ? CaptureSnapshotJson(status.capture) : Json::Value(); current["ownerLatestSequence"] = Numeric(status.ownerLatestSequence);
			current["ownerAppliedSequence"] = Numeric(status.ownerAppliedSequence); current["viewportX"] = Numeric(status.viewportX); current["viewportY"] = Numeric(status.viewportY);
			current["viewportClamped"] = status.viewportClamped; current["observedAtUs"] = TimeJson(status.observedAt);
#define COUNTER(name) result["counters"][#name] = Numeric(live.counters.name)
			COUNTER(framesPublished); COUNTER(framesOverwritten); COUNTER(framesConsumed); COUNTER(frameDrops); COUNTER(summaryDrops);
			COUNTER(trackerCalls); COUNTER(trackerAccepted); COUNTER(trackerRejected);
#undef COUNTER
			result["summaryCount"] = Numeric(live.count);
			for (std::size_t index = 0; index < (std::min)(live.count, live.summaries.size()); ++index)
			{
				const auto& summary = live.summaries[index]; Json::Value item;
				item["kind"] = summary.kind == Model::SummaryKind::Motion ? "Motion" : summary.kind == Model::SummaryKind::State ? "State" : "CaptureFailure";
				item["identity"] = IdentityJson(summary.identity); item["sampledAtUs"] = TimeJson(summary.sampledAt); item["observedAtUs"] = TimeJson(summary.observedAt);
				item["hasMotion"] = summary.hasMotion; item["motion"] = summary.hasMotion ? MotionJson(summary.motion) : Json::Value();
				item["state"] = StateName(summary.state); item["pauseCause"] = Model::PauseCauseName(summary.pauseCause);
				item["captureFailure"] = CaptureFailureJson(summary.captureFailure);
				item["captureAccepted"] = Numeric(summary.captureAccepted); item["captureRejected"] = Numeric(summary.captureRejected); item["captureApiFailures"] = Numeric(summary.captureApiFailures);
				item["ownerAppliedSequence"] = Numeric(summary.ownerAppliedSequence); item["viewportX"] = Numeric(summary.viewportX); item["viewportY"] = Numeric(summary.viewportY);
				item["copyMs"] = summary.hasMotion ? Numeric(summary.copyMs) : Json::Value(); item["totalTrackerMs"] = summary.hasMotion ? Numeric(summary.totalTrackerMs) : Json::Value();
				result["summaries"].append(std::move(item));
			}
			return result;
		}
		Json::Value ExportJson(const ExportData& data)
		{
			const auto& frame = *data.frame; const auto& metadata = frame.metadata; Json::Value result;
			result["schema"] = "Inkeys.ScrollFollowDebug.v1"; result["rawImage"] = "raw.png"; result["annotatedImage"] = "preview.png";
			result["numericEncoding"] = "NaN/+Infinity/-Infinity are explicit strings; unevaluated values are null. Raw MotionStatistics have separate validity flags.";
			result["timeSemantics"] = "steady-clock microseconds; Capture Read excludes Analyze; includes backend work, not isolated PrintWindow; sample time is backend return, not compositor freshness";
			result["pointUnits"] = "reference/forward/refined/backward/flow/bounds/cells: processing pixels; divide by actual scaleX/scaleY once";
			result["worldUnits"] = "anchors, candidate/result delta and total: original ROI/content pixels; never multiply DPI";
			result["supportSemantics"] = "candidate support bounding boxes are evidence, not automatic ROI; main consensus is not whole-frame acceptance";
			result["gridSemantics"] = "reference voting grid; retained/added counts use current candidate cells";
			result["checkSemantics"] = "condition is original expression truth; only firstFailure is actual rejection; unexecuted short-circuits are not failures";
			result["jointGates"]["NonTranslation"] = "AffineVariationHigh AND AffineResidualLow AND AffineSupportHigh";
			result["jointGates"]["Coverage"] = "CoverageCells AND CoverageInterior AND CoverageColumns AND CoverageRows AND CoverageX AND CoverageY";
			result["jointGates"]["ConsistencyReject"] = "InliersLow OR SpatialRatioLow OR SurvivalRatioLow";
			result["jointGates"]["ZeroModeAmbiguity"] = "evaluated candidate zero-length AND separation AND original-cell ratio AND interior support predicates";
			auto& meta = result["frame"]["metadata"]; meta["identity"] = IdentityJson(metadata.identity);
			meta["operation"] = metadata.operation == Model::Operation::Initialize ? "Initialize" : "Process";
			meta["geometry"] = GeometryJson(metadata.geometry); meta["quality"] = QualityJson(metadata.quality); meta["statistics"] = CaptureStatisticsJson(metadata.statistics);
			meta["roiInCapture"] = RectJson(metadata.roi); meta["captureWidth"] = metadata.captureWidth; meta["captureHeight"] = metadata.captureHeight;
			meta["captureStrideBytes"] = Numeric(metadata.captureStrideBytes); meta["sourcePixelsPerPixelX"] = Numeric(metadata.sourcePixelsPerPixelX);
			meta["sourcePixelsPerPixelY"] = Numeric(metadata.sourcePixelsPerPixelY); meta["sampledAtUs"] = TimeJson(metadata.sampledAt);
			meta["sampleIntervalUs"] = Numeric(metadata.sampleInterval.count()); meta["sampleIntervalValid"] = metadata.sampleInterval.count() > 0;
			result["frame"]["width"] = frame.width; result["frame"]["height"] = frame.height; result["frame"]["strideBytes"] = Numeric(frame.strideBytes);
			result["frame"]["copyMs"] = Numeric(frame.copyMs); result["frame"]["totalTrackerMs"] = Numeric(frame.totalTrackerMs); result["frame"]["targetPublished"] = frame.targetPublished;
			result["frame"]["tracker"] = TrackerJson(frame.tracker); result["liveAtExportClick"] = LiveJson(data.live);
			auto& view = result["displayOptions"]; view["points"] = data.view.points; view["flow"] = data.view.flow; view["grid"] = data.view.grid; view["bounds"] = data.view.bounds;
			view["frozen"] = data.view.frozen; view["arrowScale"] = data.view.arrowScale; view["zoom"] = Numeric(data.view.zoom);
			view["panX"] = Numeric(data.view.panX); view["panY"] = Numeric(data.view.panY); view["splitRatio"] = Numeric(data.view.splitRatio);
			view["previewClientRect"] = RectJson({data.previewRect.left,data.previewRect.top,data.previewRect.right,data.previewRect.bottom});
			view["exportMapping"] = "complete ROI at 1:1 original pixels; marker switches/arrow scale retained; interactive view zoom/pan are recorded, not cropped into PNG";
			result["legend"]["green"] = "main consensus inlier, not frame accepted"; result["legend"]["gray"] = "near-stationary FB-valid";
			result["legend"]["orange"] = "production-evaluated competitor"; result["legend"]["red"] = "failed/excluded"; result["legend"]["cyan"] = "new point; committed flag separate";
			return result;
		}

		struct DcScope
		{
			HDC dc; int saved; explicit DcScope(HDC value) noexcept : dc(value), saved(SaveDC(value)) {}
			~DcScope() { if (saved) RestoreDC(dc,saved); }
		};
		void Fill(HDC dc, const RECT& bounds, COLORREF color) noexcept
		{
			const HBRUSH brush = CreateSolidBrush(color); if (brush) { FillRect(dc, &bounds, brush); DeleteObject(brush); }
		}
		void DrawLine(HDC dc, POINT from, POINT to, COLORREF color, int width = 1, int style = PS_SOLID) noexcept
		{
			const HPEN pen = CreatePen(style, width, color); if (!pen) return;
			const auto old = SelectObject(dc, pen); MoveToEx(dc, from.x, from.y, nullptr); LineTo(dc, to.x, to.y); SelectObject(dc, old); DeleteObject(pen);
		}
		void DrawArrow(HDC dc, POINT from, POINT to, COLORREF color, int width = 1) noexcept
		{
			DrawLine(dc, from, to, color, width);
			const double dx = static_cast<double>(to.x) - from.x, dy = static_cast<double>(to.y) - from.y, length = std::hypot(dx, dy);
			if (length < 2) return; const double unitX = dx / length, unitY = dy / length;
			const double head = (std::min)(8.0, length * .45);
			const POINT left{static_cast<LONG>(std::lround(to.x-head*unitX+head*.5*unitY)),static_cast<LONG>(std::lround(to.y-head*unitY-head*.5*unitX))};
			const POINT right{static_cast<LONG>(std::lround(to.x-head*unitX-head*.5*unitY)),static_cast<LONG>(std::lround(to.y-head*unitY+head*.5*unitX))};
			DrawLine(dc, to, left, color, width); DrawLine(dc, to, right, color, width);
		}
		void Label(HDC dc, const RECT& bounds, const std::wstring& text, COLORREF color = Foreground, UINT flags = DT_LEFT | DT_SINGLELINE | DT_NOPREFIX) noexcept
		{
			RECT rectangle = bounds; SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color); DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rectangle, flags);
		}
		struct Mapping
		{
			double scale = 1, x = 0, y = 0;
			POINT Original(double px, double py) const noexcept
			{
				const auto clamp = [](double value) { return static_cast<LONG>(std::lround((std::clamp)(value, -1000000.0, 1000000.0))); };
				return {clamp(x+px*scale),clamp(y+py*scale)};
			}
			POINT Processing(CV::MotionVector point, const CV::TrackerDebugSnapshot& debug) const noexcept
			{
				return Original(point.x/debug.scaleX,point.y/debug.scaleY);
			}
		};
		bool Finite(CV::MotionVector point) noexcept { return std::isfinite(point.x) && std::isfinite(point.y); }
		bool PixelsValid(const Model::FramePacket& frame) noexcept
		{
			if (frame.width <= 0 || frame.height <= 0 || frame.width > 16384 || frame.height > 16384) return false;
			if (static_cast<std::size_t>(frame.width)*frame.height > 64u*1024u*1024u) return false;
			const auto row = static_cast<std::size_t>(frame.width) * 4;
			return frame.strideBytes == row && frame.strideBytes <= (std::numeric_limits<std::size_t>::max)() / static_cast<std::size_t>(frame.height)
				&& frame.bgra.size() >= frame.strideBytes * static_cast<std::size_t>(frame.height);
		}
		Mapping FitMapping(const Model::FramePacket& frame, const RECT& area, const ViewOptions& view) noexcept
		{
			const double fit = (std::min)(static_cast<double>((std::max)(1L, area.right-area.left))/frame.width,
				static_cast<double>((std::max)(1L, area.bottom-area.top))/frame.height);
			const double scale = fit*view.zoom;
			return {scale,area.left+(area.right-area.left-frame.width*scale)/2+view.panX,area.top+(area.bottom-area.top-frame.height*scale)/2+view.panY};
		}
		void PointShape(HDC dc, POINT point, COLORREF color, int shape) noexcept
		{
			const HPEN pen = CreatePen(PS_SOLID, 1, color); if (!pen) return;
			const auto oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
			if (shape == 0) { MoveToEx(dc,point.x-3,point.y-3,nullptr); LineTo(dc,point.x+4,point.y+4); MoveToEx(dc,point.x-3,point.y+3,nullptr); LineTo(dc,point.x+4,point.y-4); }
			else if (shape == 1) Rectangle(dc,point.x-3,point.y-3,point.x+4,point.y+4);
			else if (shape == 2) { const POINT triangle[]{{point.x,point.y-4},{point.x-4,point.y+3},{point.x+4,point.y+3}}; Polygon(dc,triangle,3); }
			else if (shape == 4) { MoveToEx(dc,point.x-4,point.y,nullptr); LineTo(dc,point.x+5,point.y); MoveToEx(dc,point.x,point.y-4,nullptr); LineTo(dc,point.x,point.y+5); }
			else Ellipse(dc,point.x-3,point.y-3,point.x+4,point.y+4);
			SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(pen);
		}
		void CandidateMarks(HDC dc, const CV::TrackerDebugCandidate& candidate, const CV::TrackerDebugSnapshot& debug,
			const Mapping& mapping, const ViewOptions& view, COLORREF color, const wchar_t* name) noexcept
		{
			if (!candidate.valid || !Finite(candidate.flow)) return;
			for (int current = 0; current < 2; ++current)
			{
				const auto& bounds = current ? candidate.currentBounds : candidate.referenceBounds;
				if (!bounds.valid || !std::isfinite(bounds.minX) || !std::isfinite(bounds.maxX) || !std::isfinite(bounds.minY) || !std::isfinite(bounds.maxY)) continue;
				const POINT left = mapping.Processing({bounds.minX,bounds.minY},debug), right = mapping.Processing({bounds.maxX,bounds.maxY},debug);
				if (view.bounds)
				{
					DrawLine(dc,left,{right.x,left.y},color,1,current ? PS_SOLID : PS_DASH); DrawLine(dc,{right.x,left.y},right,color,1,current ? PS_SOLID : PS_DASH);
					DrawLine(dc,right,{left.x,right.y},color,1,current ? PS_SOLID : PS_DASH); DrawLine(dc,{left.x,right.y},left,color,1,current ? PS_SOLID : PS_DASH);
				}
				if (!current && view.flow)
				{
					const CV::MotionVector center{(bounds.minX+bounds.maxX)/2,(bounds.minY+bounds.maxY)/2};
					DrawArrow(dc,mapping.Processing(center,debug),mapping.Processing({center.x+candidate.flow.x*view.arrowScale,center.y+candidate.flow.y*view.arrowScale},debug),color,2);
				}
				if (current && view.bounds) Label(dc,{left.x+2,left.y+2,left.x+230,left.y+18},name,color);
			}
		}
		void DrawAnnotations(HDC dc, const Model::FramePacket& frame, const Mapping& mapping, const ViewOptions& view)
		{
			const auto& debug = frame.tracker;
			// 验证/几何早退没有处理尺寸时只保留真实图像，不能用旧尺寸或默认二倍补出标记。
			if (debug.processingWidth <= 0 || debug.processingHeight <= 0 || !std::isfinite(debug.scaleX) || !std::isfinite(debug.scaleY) || debug.scaleX <= 0 || debug.scaleY <= 0) return;
			if (view.grid)
			{
				const int width = debug.referenceBeforeValid ? debug.referenceProcessingWidth : debug.processingWidth;
				const int height = debug.referenceBeforeValid ? debug.referenceProcessingHeight : debug.processingHeight;
				for (int column = 0; column <= 8; ++column)
					DrawLine(dc,mapping.Processing({width*column/8.0,0},debug),mapping.Processing({width*column/8.0,static_cast<double>(height)},debug),RGB(83,110,139),1,PS_DOT);
				for (int row = 0; row <= 6; ++row)
					DrawLine(dc,mapping.Processing({0,height*row/6.0},debug),mapping.Processing({static_cast<double>(width),height*row/6.0},debug),RGB(83,110,139),1,PS_DOT);
				for (int row = 0; row < 6; ++row) for (int column = 0; column < 8; ++column)
				{
					const auto& cell = debug.cells[static_cast<std::size_t>(row)*8+column]; const POINT point = mapping.Processing({width*column/8.0,height*row/6.0},debug);
					const auto valid=ValidCells(debug);
					Label(dc,{point.x+3,point.y+3,point.x+130,point.y+18},L"R"+(valid.reference?Text(cell.reference):L"-")+L" F"+(valid.forward?Text(cell.forward):L"-")+L" FB"+(valid.backward?Text(cell.fbValid):L"-"),RGB(171,194,213));
					Label(dc,{point.x+3,point.y+18,point.x+130,point.y+33},L"I"+(valid.consensus?Text(cell.inliers):L"-")+L" C"+(valid.ambiguity?Text(cell.competitor):L"-"),RGB(171,194,213));
				}
			}
			for (std::size_t index = 0; index < (std::min)(debug.pointCount, debug.points.size()); ++index)
			{
				const auto& point = debug.points[index]; if (!Finite(point.reference)) continue;
				CV::MotionVector position = point.reference; int shape = 0; COLORREF color = ExcludedColor;
				if (point.forwardComputed && point.forwardStatus && Finite(point.forward)) { position=point.forward; shape=1; color=ForwardColor; }
				if (point.refinedComputed && Finite(point.refined)) { position=point.refined; shape=2; }
				if (point.fbValid) { shape=3; color=point.nearStationary?StationaryColor:point.inlier?InlierColor:ExcludedColor; }
				else if (point.backwardComputed) color=ExcludedColor;
				if (point.competitor) color=CompetitorColor;
				if (view.flow && point.forwardComputed && point.forwardStatus && Finite(position))
					DrawArrow(dc,mapping.Processing(point.reference,debug),mapping.Processing({point.reference.x+(position.x-point.reference.x)*view.arrowScale,point.reference.y+(position.y-point.reference.y)*view.arrowScale},debug),color);
				if (view.points) PointShape(dc,mapping.Processing(position,debug),color,shape);
			}
			if (view.points) for (std::size_t index = 0; index < (std::min)(debug.addedPointCount, debug.addedPoints.size()); ++index)
			{
				const auto& point = debug.addedPoints[index]; if (Finite(point.point)) PointShape(dc,mapping.Processing(point.point,debug),AddedColor,point.committed?4:2);
			}
			CandidateMarks(dc,debug.mainMotion,debug,mapping,view,InlierColor,L"Main Motion Support Bounds");
			CandidateMarks(dc,debug.strongestCompetitor,debug,mapping,view,CompetitorColor,L"Strongest evaluated competitor");
			if (debug.rejectingCompetitor.valid && (!debug.strongestCompetitor.valid || debug.rejectingCompetitor.flow.x!=debug.strongestCompetitor.flow.x || debug.rejectingCompetitor.flow.y!=debug.strongestCompetitor.flow.y))
				CandidateMarks(dc,debug.rejectingCompetitor,debug,mapping,view,RGB(255,118,63),L"Actual rejecting competitor");
		}
		bool WriteArtifacts(const ExportData& data, const std::filesystem::path& directory, const std::atomic_bool* cancelled, std::wstring& status)
		{
			const auto isCancelled = [&] { return cancelled && cancelled->load(std::memory_order_acquire); };
			if (isCancelled()) { status=L"Export cancelled"; return false; }
			if (!data.frame || !PixelsValid(*data.frame)) { status=L"Export failed: invalid owned input layout"; return false; }
			const auto& frame = *data.frame; Graphics::DibSurface raw, preview;
			if (!raw.resize(frame.width,frame.height)) { status=L"Export failed: raw DIB allocation"; return false; }
			for (int row=0;row<frame.height;++row) std::memcpy(raw.pixels().data()+static_cast<std::size_t>(row)*frame.width,frame.bgra.data()+static_cast<std::size_t>(row)*frame.strideBytes,static_cast<std::size_t>(frame.width)*4);
			preview=raw; DrawAnnotations(preview.dc(),frame,{1,0,0},data.view); GdiFlush();
			// GDI 标记写 RGB 时不会维护 DIB alpha；标记 PNG 必须显式成为不透明预览。
			for (auto& pixel:preview.pixels()) pixel|=0xff000000u;
			if (isCancelled()) { status=L"Export cancelled"; return false; }
			if (!raw.savePng((directory/L"raw.png").native())) { status=L"Export failed: raw.png"; return false; }
			if (isCancelled()) { status=L"Export cancelled (partial directory retained)"; return false; }
			if (!preview.savePng((directory/L"preview.png").native())) { status=L"Export failed: preview.png (partial directory retained)"; return false; }
			if (isCancelled()) { status=L"Export cancelled (partial directory retained)"; return false; }
			Json::StreamWriterBuilder builder; builder["indentation"]="  "; const std::string json=Json::writeString(builder,ExportJson(data));
			const HANDLE file=CreateFileW((directory/L"snapshot.json").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
			if (file==INVALID_HANDLE_VALUE) { status=L"Export failed: create snapshot.json error "+Text(GetLastError()); return false; }
			DWORD written=0; const bool writtenAll=json.size()<MAXDWORD && WriteFile(file,json.data(),static_cast<DWORD>(json.size()),&written,nullptr) && written==json.size();
			const DWORD error=writtenAll?0:GetLastError(); CloseHandle(file);
			if (!writtenAll) { status=L"Export failed: write snapshot.json error "+Text(error); return false; }
			status=L"Saved: "+directory.native(); return true;
		}

		struct ExportJob
		{
			std::unique_ptr<ExportData> data;
			std::uint64_t generation = 0;
			std::atomic_bool cancelled = false, done = false;
			std::mutex mutex;
			std::wstring status = L"Choosing export directory...";
			void Status(std::wstring value) { std::scoped_lock lock(mutex); status=std::move(value); }
			std::wstring Status() { std::scoped_lock lock(mutex); return status; }
		};
		struct StaApartment
		{
			HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
			~StaApartment() { if (SUCCEEDED(initialized)) CoUninitialize(); }
		};
		struct PickerContext { IFileDialog* dialog = nullptr; ExportJob* job = nullptr; bool showing = false; };
		void ObservePicker(const PickerContext& context) noexcept
		{
			HWND hwnd=nullptr; IOleWindow* window=nullptr;
			// 初次文件夹事件已有 HWND；定时器仅作补查，不将窗口类名/owner 当成排除依据。
			if (context.showing && !context.job->cancelled.load(std::memory_order_acquire) && SUCCEEDED(context.dialog->QueryInterface(IID_PPV_ARGS(&window))))
			{
				if (FAILED(window->GetWindow(&hwnd))) hwnd=nullptr;
				window->Release();
			}
			DWORD process=0; const DWORD thread=hwnd?GetWindowThreadProcessId(hwnd,&process):0;
			if (!hwnd || !IsWindow(hwnd) || process!=GetCurrentProcessId() || thread!=GetCurrentThreadId() || (GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_CHILD)) hwnd=nullptr;
			exportPickerWindow.store(hwnd,std::memory_order_release);
		}
		class PickerEvents final : public IFileDialogEvents
		{
		public:
			explicit PickerEvents(PickerContext& context) noexcept : context_(context) {}
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** result) override
			{
				if (!result) return E_POINTER; *result=nullptr;
				if (IsEqualIID(iid,IID_IUnknown) || IsEqualIID(iid,IID_IFileDialogEvents)) { *result=static_cast<IFileDialogEvents*>(this); AddRef(); return S_OK; }
				return E_NOINTERFACE;
			}
			ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
			ULONG STDMETHODCALLTYPE Release() override { const ULONG remaining=--references_; if (!remaining) delete this; return remaining; }
			HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog*) override { ObservePicker(context_); return S_OK; }
			HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog*,IShellItem*) override { ObservePicker(context_); return S_OK; }
			HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog*) override { ObservePicker(context_); return S_OK; }
			HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog*) override { ObservePicker(context_); return S_OK; }
			HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog*,IShellItem*,FDE_SHAREVIOLATION_RESPONSE* response) override { if (response) *response=FDESVR_DEFAULT; return S_OK; }
			HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog*) override { ObservePicker(context_); return S_OK; }
			HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog*,IShellItem*,FDE_OVERWRITE_RESPONSE* response) override { if (response) *response=FDEOR_DEFAULT; return S_OK; }
		private:
			std::atomic<ULONG> references_=1; PickerContext& context_;
		};
		LRESULT CALLBACK PickerCancelProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			if (message == WM_NCCREATE) SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
			const auto* context=reinterpret_cast<PickerContext*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
			if (message==WM_TIMER && context)
			{
				ObservePicker(*context);
				if (context->job->cancelled.load(std::memory_order_acquire))
				{
					(void)context->dialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED)); exportPickerWindow.store(nullptr,std::memory_order_release);
				}
				return 0;
			}
			return DefWindowProcW(hwnd,message,wParam,lParam);
		}
		bool PickDirectory(ExportJob& job, std::filesystem::path& directory, std::wstring& status)
		{
			if (job.cancelled.load(std::memory_order_acquire)) { status=L"Export cancelled"; return false; }
			PickerContext context;
			struct Resources
			{
				IFileOpenDialog* dialog=nullptr; PickerEvents* events=nullptr; DWORD cookie=0; bool advised=false;
				HWND cancelWindow=nullptr; bool registered=false;
				std::wstring className=L"Inkeys.ScrollFollowDebug.ExportCancellation";
				~Resources()
				{
					exportPickerWindow.store(nullptr,std::memory_order_release);
					if (cancelWindow) { KillTimer(cancelWindow,1); DestroyWindow(cancelWindow); }
					if (registered) UnregisterClassW(className.c_str(),GetModuleHandleW(nullptr));
					if (advised) (void)dialog->Unadvise(cookie);
					if (events) events->Release(); if (dialog) dialog->Release();
				}
			} resources;
			HRESULT hr=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&resources.dialog));
			if (FAILED(hr)) { status=L"Export failed: directory dialog "+Text(static_cast<unsigned long>(hr)); return false; }
			context.dialog=resources.dialog; context.job=&job;
			resources.events=new PickerEvents(context); hr=resources.dialog->Advise(resources.events,&resources.cookie);
			if (FAILED(hr)) { status=L"Export failed: directory observation "+Text(static_cast<unsigned long>(hr)); return false; } resources.advised=true;
			FILEOPENDIALOGOPTIONS options{}; hr=resources.dialog->GetOptions(&options);
			if (SUCCEEDED(hr)) hr=resources.dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_DONTADDTORECENT|FOS_NOCHANGEDIR);
			if (FAILED(hr)) { status=L"Export failed: directory options "+Text(static_cast<unsigned long>(hr)); return false; }
			(void)resources.dialog->SetTitle(L"Choose a directory for a new Scroll Follow snapshot folder");
			WNDCLASSW windowClass{}; windowClass.hInstance=GetModuleHandleW(nullptr); windowClass.lpfnWndProc=PickerCancelProc; windowClass.lpszClassName=resources.className.c_str();
			resources.registered=RegisterClassW(&windowClass)!=0;
			if (!resources.registered) { status=L"Export failed: cancellation window class"; return false; }
			resources.cancelWindow=CreateWindowExW(0,resources.className.c_str(),L"",0,0,0,0,0,HWND_MESSAGE,nullptr,windowClass.hInstance,&context);
			if (!resources.cancelWindow || !SetTimer(resources.cancelWindow,1,100,nullptr)) { status=L"Export failed: cancellation timer"; return false; }
			if (job.cancelled.load(std::memory_order_acquire)) { status=L"Export cancelled"; return false; }
			// 对话框只泵自己的 STA；取消也在该 apartment 调 Close，不使用已关闭的产品 HWND。
			context.showing=true; exportPickerWindow.store(nullptr,std::memory_order_release); hr=resources.dialog->Show(nullptr);
			context.showing=false; exportPickerWindow.store(nullptr,std::memory_order_release);
			if (hr==HRESULT_FROM_WIN32(ERROR_CANCELLED) || job.cancelled.load(std::memory_order_acquire)) { status=L"Export cancelled"; return false; }
			if (FAILED(hr)) { status=L"Export failed: directory selection "+Text(static_cast<unsigned long>(hr)); return false; }
			IShellItem* rawItem=nullptr; hr=resources.dialog->GetResult(&rawItem);
			if (FAILED(hr)) { status=L"Export failed: selected directory "+Text(static_cast<unsigned long>(hr)); return false; }
			const auto releaseItem=[](IShellItem* item) { if (item) item->Release(); };
			std::unique_ptr<IShellItem,decltype(releaseItem)> item(rawItem,releaseItem);
			PWSTR rawName=nullptr; hr=item->GetDisplayName(SIGDN_FILESYSPATH,&rawName);
			std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> name(rawName,&CoTaskMemFree);
			if (FAILED(hr) || !rawName) { status=L"Export failed: filesystem directory "+Text(static_cast<unsigned long>(hr)); return false; }
			directory=std::filesystem::path(rawName); return true;
		}
		class ExportManager
		{
		public:
			~ExportManager() { Shutdown(); }
			std::shared_ptr<ExportJob> Current() { std::scoped_lock lock(mutex_); return job_; }
			bool Start(std::unique_ptr<ExportData> data)
			{
				std::scoped_lock lock(mutex_);
				if (shuttingDown_ || (job_ && !job_->done.load(std::memory_order_acquire))) return false;
				if (worker_.joinable()) worker_.join(); // done 只在 COM/GDI/数据清理完成后发布。
				auto job=std::make_shared<ExportJob>(); job->generation=data->frame->metadata.identity.windowGeneration; job->data=std::move(data);
				worker_=std::thread([job]
				{
					try
					{
						StaApartment apartment;
						if (FAILED(apartment.initialized)) throw std::runtime_error("export COM apartment");
						std::filesystem::path parent; std::wstring status;
						if (PickDirectory(*job,parent,status))
						{
							const auto& identity=job->data->frame->metadata.identity;
							const std::wstring stem=L"scroll-follow-"+Text(identity.businessSession)+L"-"+Text(identity.captureSession)+L"-"+Text(identity.sequence)+L"-"+Text(GetTickCount64());
							std::filesystem::path directory; bool made=false;
							for (int attempt=0;attempt<8 && !made && !job->cancelled.load(std::memory_order_acquire);++attempt)
							{
								directory=parent/(stem+L"-"+Text(attempt));
								if (directory.native().size()>=MAX_PATH-32) { status=L"Export failed: directory path too long"; break; }
								made=CreateDirectoryW(directory.c_str(),nullptr)!=FALSE;
								if (!made && GetLastError()!=ERROR_ALREADY_EXISTS) { status=L"Export failed: create snapshot directory"; break; }
							}
							if (made) { job->Status(L"Writing immutable snapshot..."); (void)WriteArtifacts(*job->data,directory,&job->cancelled,status); }
							else if (job->cancelled.load(std::memory_order_acquire)) status=L"Export cancelled";
							else if (status.empty()) status=L"Export failed: no unique snapshot directory";
						}
						job->Status(std::move(status));
					}
					catch (...) { try { job->Status(L"Export failed: allocation or file exception (partial directory may remain)"); } catch (...) {} }
					job->data.reset(); job->done.store(true,std::memory_order_release);
				});
				job_=std::move(job); return true;
			}
			void Cancel(std::uint64_t generation) noexcept
			{
				std::scoped_lock lock(mutex_); if (job_ && job_->generation==generation) job_->cancelled.store(true,std::memory_order_release);
			}
			void Shutdown() noexcept
			{
				std::thread worker;
				{ std::scoped_lock lock(mutex_); shuttingDown_=true; if (job_) job_->cancelled.store(true,std::memory_order_release); worker=std::move(worker_); }
				if (worker.joinable()) worker.join();
			}
		private:
			std::mutex mutex_; std::thread worker_; std::shared_ptr<ExportJob> job_; bool shuttingDown_=false;
		};
		ExportManager exports;

		struct UiState
		{
			HWND hwnd=nullptr; Model::ChannelPtr channel; Model::LiveSnapshot live{};
			ViewOptions view{}; Graphics::DibSurface backBuffer; HFONT font=nullptr, mono=nullptr;
			RECT client{}, preview{}, stats{}, history{}, buttons[10]{};
			int contentTop=0, split=0, statsScroll=0, historyScroll=0, statsHeight=0;
			bool panning=false, resizingSplit=false, scrollingStats=false;
			POINT lastMouse{}; std::wstring notice;
			std::uint64_t refreshes=0, consumed=0, paints=0, rateBaseline=0;
			double previewHz=0; Clock::time_point rateAt=Clock::now();
			~UiState()
			{
				if (channel) { exports.Cancel(channel->Generation()); Model::Deactivate(channel); }
				if (font) DeleteObject(font); if (mono) DeleteObject(mono);
			}
		};
		UiState* State(HWND hwnd) noexcept { return reinterpret_cast<UiState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA)); }
		void Layout(UiState& state) noexcept
		{
			GetClientRect(state.hwnd,&state.client); const int width=state.client.right, height=state.client.bottom;
			constexpr int widths[]{76,66,60,60,76,36,36,36,76,88}; int x=12,y=74;
			for (int index=0;index<10;++index)
			{
				if (x+widths[index]>width-12) { x=12; y+=30; }
				state.buttons[index]={x,y,x+widths[index],y+24}; x+=widths[index]+6;
			}
			state.contentTop=y+36; const int historyHeight=(std::clamp)(height/4,128,220), historyTop=(std::max)(state.contentTop+120,height-historyHeight);
			state.split=(std::clamp)(static_cast<int>(width*state.view.splitRatio),240,(std::max)(240,width-270));
			state.preview={12,state.contentTop+24,state.split-8,(std::max)(state.contentTop+30,historyTop-46)};
			state.stats={state.split+8,state.contentTop+24,width-14,historyTop-10};
			state.history={12,historyTop+24,width-12,height-6};
		}
		void Refresh(UiState& state)
		{
			++state.refreshes; state.live=state.channel->ReadLive();
			if (!state.view.frozen && state.channel->ConsumeLatest()) ++state.consumed;
			const auto now=Clock::now(); const double seconds=std::chrono::duration<double>(now-state.rateAt).count();
			if (seconds>=1) { state.previewHz=(state.consumed-state.rateBaseline)/seconds; state.rateBaseline=state.consumed; state.rateAt=now; }
			if (const auto job=exports.Current()) state.notice=job->Status();
			InvalidateRect(state.hwnd,nullptr,FALSE);
		}
		std::vector<std::wstring> DetailLines(const UiState& state)
		{
			std::vector<std::wstring> lines; const auto* packet=state.channel->Displayed(); const auto& live=state.live.status;
			const auto add=[&](std::wstring value) { lines.push_back(std::move(value)); };
			add(L"LIVE (separate from selected frame)");
			add(L"State "+Wide(StateName(live.state))+L" | cause "+Wide(Model::PauseCauseName(live.pauseCause)));
			add(L"Business/capture "+Text(live.businessSession)+L" / "+Text(live.captureSession));
			add(L"ownsCapture="+Text(live.ownsCapture)+L" awaitTracking="+Text(live.awaitTracking));
			add(L"Owner latest/applied "+Text(live.ownerLatestSequence)+L" / "+Text(live.ownerAppliedSequence));
			add(L"Applied viewport "+VectorText({live.viewportX,live.viewportY})+L" clamp="+Text(live.viewportClamped));
			add(L"Live status age "+AgeText(live.observedAt));
			if (live.ownsCapture)
			{
				const bool qualityObserved=live.capture.qualityObserved, readObserved=live.capture.lastReadCompleted!=Capture::TimePoint{};
				add(L"Capture ready/publishing/occupied/inFlight "+Text(live.capture.ready)+L"/"+Text(live.capture.publishing)+L"/"+Text(live.capture.occupied)+L"/"+Text(live.capture.inFlight));
				add(L"Capture stop "+Wide(StopName(live.capture.reason))+L" quality "+(qualityObserved?Wide(QualityName(live.capture.quality.status)):L"N/A (no analyzed sample)"));
				add(L"Capture failure stage "+Wide(Capture::FailureStageName(live.capture.failureStage)));
				add(L"Native error "+(live.capture.nativeErrorValid?Text(live.capture.nativeError):L"N/A (no native error code)"));
				add(L"Read completion / accepted age: "+AgeText(live.capture.lastReadCompleted)+L" / "+AgeText(live.capture.lastAcceptedSample));
				add(L"Last failure age "+AgeText(live.capture.lastFailureAt));
				add(L"Capture Read "+(readObserved?NumberText(live.capture.statistics.lastReadDuration.count()/1000.0)+L" ms":L"N/A")+L" (backend; excludes Analyze)");
				add(L"Capture max Read "+(readObserved?NumberText(live.capture.statistics.maxReadDuration.count()/1000.0)+L" ms":L"N/A"));
				add(L"Return interval "+(live.capture.lastSampleInterval.count()>0?NumberText(live.capture.lastSampleInterval.count()/1000.0)+L" ms / "+NumberText(1000000.0/live.capture.lastSampleInterval.count())+L" Hz":L"N/A"));
				add(L"Read time does not prove compositor freshness.");
				add(L"Capture session counts (not Tracker counts):");
				add(L"attempt/accept/reject/API "+Text(live.capture.statistics.attempts)+L"/"+Text(live.capture.statistics.accepted)+L"/"+Text(live.capture.statistics.rejected)+L"/"+Text(live.capture.statistics.apiFailures));
				add(L"skipped/late "+Text(live.capture.statistics.skipped)+L"/"+Text(live.capture.statistics.lateResults));
			}
			else add(L"Capture: N/A (no currently owned/matching capture session)");
			add(L"Window-observed Tracker call/A/R "+Text(state.live.counters.trackerCalls)+L"/"+Text(state.live.counters.trackerAccepted)+L"/"+Text(state.live.counters.trackerRejected));
			add(L"Preview pub/consume/overwrite/drop "+Text(state.live.counters.framesPublished)+L"/"+Text(state.live.counters.framesConsumed)+L"/"+Text(state.live.counters.framesOverwritten)+L"/"+Text(state.live.counters.frameDrops));
			add(L"Summary drops "+Text(state.live.counters.summaryDrops)+L" | UI refresh/paint "+Text(state.refreshes)+L"/"+Text(state.paints));
			add(L"Capture failure/AwaitTracking do not imply a CV call.");
			add(L"ORIGINAL CAPTURE FAILURES (recent events; independent of latest status):");
			bool failureFound=false;
			for (std::size_t index=0;index<(std::min)(state.live.count,state.live.summaries.size());++index)
			{
				const auto& event=state.live.summaries[index]; if (!event.captureFailure.valid) continue; failureFound=true;
				add(L"b/c "+Text(event.identity.businessSession)+L"/"+Text(event.identity.captureSession)+L" "+CaptureFailureText(event.captureFailure));
				add(L"  event age "+AgeText(event.captureFailure.at));
				if (event.captureFailure.stage==Capture::FailureStage::Quality)
				{
					const auto& quality=event.captureFailure.quality;
					add(L"  mean/variance/black/samples "+NumberText(quality.meanLuma)+L"/"+NumberText(quality.variance)+L"/"+NumberText(quality.blackFraction)+L"/"+Text(quality.samples));
				}
			}
			if (!failureFound) add(L"N/A (no captured failure event)"); add(L"");
			if (!packet) { add(L"SELECTED FRAME: none (N/A)"); return lines; }
			const auto& frame=packet->tracker; const auto& metadata=packet->metadata; const auto& result=frame.result;
			add(L"SELECTED FRAME (fixed while Frozen)");
			add(L"Window/business/capture/frame "+Text(metadata.identity.windowGeneration)+L"/"+Text(metadata.identity.businessSession)+L"/"+Text(metadata.identity.captureSession)+L"/"+Text(metadata.identity.sequence));
			add(L"Display generation "+Text(metadata.identity.displayGeneration)+L" | sample age "+NumberText(AgeMs(metadata.sampledAt))+L" ms");
			add(L"Operation "+std::wstring(metadata.operation==Model::Operation::Initialize?L"Initialize":L"Process"));
			add(L"Reference seq "+Text(frame.referenceSequenceBefore)+L" -> "+Text(frame.referenceSequenceAfter));
			add(L"Reference generation "+Text(frame.referenceGenerationBefore)+L" -> "+Text(frame.referenceGenerationAfter));
			add(L"Reference valid before/after "+Text(frame.referenceBeforeValid)+L"/"+Text(frame.referenceAfterValid));
			add(L"Production "+std::wstring(result.accepted?L"ACCEPT":L"REJECT")+L" "+Wide(MotionStateName(result.state))+L" / "+Wide(CV::MotionReasonName(result.reason)));
			add(L"Actual first failure "+(frame.firstFailure==TrackerDebugCheckId::Count?L"none":Wide(CV::TrackerDebugCheckName(frame.firstFailure))));
			add(L"Exception "+Wide(CV::MotionReasonName(frame.exception))+L" | confidence "+NumberText(result.confidence)+L" (quality score)");
			add(L"Production delta "+VectorText(result.delta)+L" | total "+VectorText(result.total)+L" original px");
			add(L"Estimated candidate delta "+(frame.candidateTotalValid?VectorText(frame.candidateDelta):L"N/A"));
			add(L"Estimated candidate total "+(frame.candidateTotalValid?VectorText(frame.candidateTotal):L"N/A"));
			add(L"Total before "+VectorText(frame.totalBefore)+L" original/world px");
			add(L"Target actually published="+Text(packet->targetPublished)+L" (not owner applied)");
			add(L"Primary "+RectText(metadata.geometry.primaryBounds)); add(L"Capture source "+RectText(metadata.geometry.sourceRect)); add(L"Work area "+RectText(metadata.geometry.workArea));
			add(L"ROI in capture "+RectText(metadata.roi));
			add(L"Desktop ROI origin "+Text(metadata.geometry.sourceRect.left+metadata.roi.left)+L","+Text(metadata.geometry.sourceRect.top+metadata.roi.top));
			add(L"Capture "+Text(metadata.captureWidth)+L"x"+Text(metadata.captureHeight)+L" stride "+Text(metadata.captureStrideBytes));
			add(L"Owned ROI "+Text(packet->width)+L"x"+Text(packet->height)+L" stride "+Text(packet->strideBytes));
			add(L"Tracker input "+Text(frame.inputWidth)+L"x"+Text(frame.inputHeight)+L" stride/bytes "+Text(frame.inputStrideBytes)+L"/"+Text(frame.inputSizeBytes));
			add(L"Processing "+Text(frame.processingWidth)+L"x"+Text(frame.processingHeight)+L" reference "+Text(frame.referenceProcessingWidth)+L"x"+Text(frame.referenceProcessingHeight));
			add(L"Actual scale X/Y "+NumberText(frame.scaleX)+L" / "+NumberText(frame.scaleY));
			add(L"Capture px/px X/Y "+NumberText(metadata.sourcePixelsPerPixelX)+L" / "+NumberText(metadata.sourcePixelsPerPixelY));
			add(L"Tracker timestamp "+NumberText(frame.timestampSeconds)+L" s | captureValid="+Text(frame.captureValid));
			add(L"Quality "+Wide(QualityName(metadata.quality.status))+L" mean/variance "+NumberText(metadata.quality.meanLuma)+L" / "+NumberText(metadata.quality.variance));
			add(L"Black fraction "+NumberText(metadata.quality.blackFraction)+L" samples "+Text(metadata.quality.samples));
			add(L"Frame Capture Read "+NumberText(metadata.statistics.lastReadDuration.count()/1000.0)+L" ms (excludes Analyze)");
			add(L"Frame Capture attempts/A/R/API "+Text(metadata.statistics.attempts)+L"/"+Text(metadata.statistics.accepted)+L"/"+Text(metadata.statistics.rejected)+L"/"+Text(metadata.statistics.apiFailures));
			add(L"Frame Capture skipped/late/maxReadUs "+Text(metadata.statistics.skipped)+L"/"+Text(metadata.statistics.lateResults)+L"/"+Text(metadata.statistics.maxReadDuration.count()));
			add(L"Frame return interval "+(metadata.sampleInterval.count()>0?NumberText(metadata.sampleInterval.count()/1000.0)+L" ms":L"N/A"));
			add(L"ROI copy "+NumberText(packet->copyMs)+L" ms | total Tracker "+NumberText(packet->totalTrackerMs)+L" ms");
			add(L"Total Tracker is measured wall time, not component sum."); add(L"");
			add(L"ALL RETURNED MotionStatistics (N/A = not computed):");
			for (const auto& entry:Statistics(result.statistics,&frame)) add(Wide(entry.name)+L" = "+(entry.valid?Wide(entry.value.asString().c_str()):L"N/A"));
			add(L""); add(L"STAGE COMPLETENESS");
			for (unsigned index=0;index<static_cast<unsigned>(TrackerDebugStage::Count);++index)
			{
				const auto stage=static_cast<TrackerDebugStage>(index); add(Wide(CV::TrackerDebugStageName(stage))+L": "+(Completed(frame,stage)?L"completed":Reached(frame,stage)?L"reached / incomplete":L"N/A (not executed)"));
			}
			add(L""); add(L"CHECKS: original expression truth, not PASS/FAIL");
			add(L"Only firstFailure is the actual rejection gate.");
			add(L"NonTranslation = variation AND residual AND support");
			add(L"Coverage = cells AND interior AND columns AND rows AND X AND Y");
			add(L"Consistency reject = inliers OR spatial OR survival");
			for (std::size_t index=0;index<frame.checks.size();++index)
			{
				const auto& check=frame.checks[index]; const auto id=static_cast<TrackerDebugCheckId>(index);
				add((id==frame.firstFailure?L">>> ":L"    ")+Wide(CV::TrackerDebugCheckName(id)));
				add(check.evaluated?L"  "+NumberText(check.measured)+L" "+Wide(Comparison(check.comparison))+L" "+NumberText(check.threshold)+L" => "+(check.condition?L"true":L"false"):L"  N/A (short-circuited or not reached)");
			}
			add(L"Limits (processing px): border="+NumberText(frame.pointBorder)+L" consensus="+NumberText(frame.consensusRadius));
			add(L"Refine/FB/stationary "+NumberText(frame.refinementDistanceLimit)+L"/"+NumberText(frame.fbErrorLimit)+L"/"+NumberText(frame.nearStationaryLimit));
			add(L"Zero-mode ratio threshold "+NumberText(frame.zeroModeRatioThreshold));
			add(L"Candidate scan "+Text(frame.consensusCandidatesEvaluated)+L" / competitors checked "+Text(frame.competitorsEvaluated));
			add(L"Ambiguity complete="+Text(frame.ambiguityComplete)+L" (false = partial scan)");
			const auto candidate=[&](const wchar_t* name,const CV::TrackerDebugCandidate& value,bool competitor=true)
			{
				add(name); if (!value.valid) { add(L"  N/A (no evaluated candidate)"); return; }
				add(L"  flow "+VectorText(value.flow)+L" processing px; separation "+(competitor?NumberText(value.separation):L"N/A (main)"));
				add(L"  points/cells/interior "+Text(value.points)+L"/"+Text(value.cells)+L"/"+Text(value.interiorCells));
				add(L"  fit weight "+NumberText(value.fitWeight)+L" original-cell ratio "+(competitor?NumberText(value.originalCellRatio):L"N/A (main)"));
				const auto box=[&](const wchar_t* label,const CV::TrackerDebugBounds& bounds) { add(std::wstring(L"  ")+label+(bounds.valid?L" ("+NumberText(bounds.minX)+L","+NumberText(bounds.minY)+L")..("+NumberText(bounds.maxX)+L","+NumberText(bounds.maxY)+L")":L" N/A")); };
				box(L"Reference support",value.referenceBounds); box(L"Current support",value.currentBounds);
				const auto truth=[](bool evaluated,bool condition) { return evaluated?(condition?L"true":L"false"):L"N/A"; };
				add(L"  beyond2R="+std::wstring(truth(value.doubleRadiusEvaluated,value.beyondDoubleRadius))+L" zeroLength="+truth(value.zeroLengthEvaluated,value.zeroLength));
				add(L"  zeroRatio="+std::wstring(truth(value.zeroRatioEvaluated,value.zeroRatio))+L" enoughInterior="+truth(value.interiorEvaluated,value.enoughInterior));
			};
			candidate(L"MAIN CONSENSUS (not frame acceptance)",frame.mainMotion,false); candidate(L"STRONGEST ACTUALLY EVALUATED COMPETITOR",frame.strongestCompetitor);
			candidate(L"ELIGIBLE RUNNER-UP",frame.eligibleRunnerUp); candidate(L"ACTUAL REJECTING COMPETITOR",frame.rejectingCompetitor);
			add(L"Support boxes are evidence, not automatic ROI; main support is not whole-frame acceptance.");
			add(L"POINT LEGEND: green main consensus; gray near-static FB-valid; orange evaluated competitor; red failed/excluded; cyan newly added; pale yellow Forward before FB.");
			add(L"Color priority: evaluated competitor, then near-static, then main inlier; raw point flags remain authoritative.");
			add(L"Shapes: x failed; square Forward; triangle refine/backward intermediate; circle FB-valid; plus committed refill. Uncommitted refill is triangle.");
			add(L""); add(L"8x6 GRID: reference voting; retained/added current cells");
			const auto valid=ValidCells(frame); const auto observed=[](auto value,bool computed) { return computed?Text(value):L"N/A"; };
			for (std::size_t index=0;index<frame.cells.size();++index)
			{
				const auto& cell=frame.cells[index]; add(L"["+Text(index%8)+L","+Text(index/8)+L"] ref/F/FB/I/C/static="+observed(cell.reference,valid.reference)+L"/"+observed(cell.forward,valid.forward)+L"/"+observed(cell.fbValid,valid.backward)+L"/"+observed(cell.inliers,valid.consensus)+L"/"+observed(cell.competitor,valid.ambiguity)+L"/"+observed(cell.nearStationary,valid.backward));
				add(L"  fit/inlier/original W "+observed(cell.fitWeight,valid.weights)+L"/"+observed(cell.inlierWeight,valid.consensus)+L"/"+observed(cell.originalCellWeight,valid.weights)+L" retained/added "+observed(cell.retained,valid.retention)+L"/"+observed(cell.added,valid.refill));
				if (cell.affineSampleValid) add(L"  affine sample "+VectorText(cell.affinePosition)+L" flow "+VectorText(cell.affineFlow));
			}
			add(L"Points "+Text(frame.pointCount)+L" added "+Text(frame.addedPointCount)+L" truncated="+Text(frame.truncated));
			add(L"Commit completed="+Text(Completed(frame,TrackerDebugStage::Commit))+L"; new points/retention before it are proposals.");
			return lines;
		}

		bool LiveCaptureFailure(const Model::LiveStatus& live) noexcept
		{
			if (live.ownsCapture && live.capture.lastFailureAt!=Capture::TimePoint{} && live.capture.failureStage!=Capture::FailureStage::None
				&& (live.capture.lastAcceptedSample==Capture::TimePoint{} || live.capture.lastFailureAt>=live.capture.lastAcceptedSample)) return true;
			if (live.state!=Business::ScrollFollow::State::Paused) return false;
			switch (live.pauseCause)
			{
			case Model::PauseCause::CaptureQuality: case Model::PauseCause::CaptureApi: case Model::PauseCause::CaptureStartupTimeout:
			case Model::PauseCause::CaptureResultTimeout: case Model::PauseCause::CaptureUnavailable: return true;
			default: return false;
			}
		}
		void PublishProbe(const UiState& state)
		{
			Probe probe; probe.open=true; probe.frozen=state.view.frozen; probe.points=state.view.points; probe.flow=state.view.flow;
			probe.grid=state.view.grid; probe.bounds=state.view.bounds; probe.arrowScale=state.view.arrowScale; probe.zoom=state.view.zoom; probe.panX=state.view.panX; probe.panY=state.view.panY;
			probe.generation=state.channel->Generation(); probe.refreshes=state.refreshes; probe.consumed=state.consumed; probe.paints=state.paints; probe.previewHz=state.previewHz;
			probe.ownerAppliedSequence=state.live.status.ownerAppliedSequence; probe.viewportX=state.live.status.viewportX; probe.viewportY=state.live.status.viewportY;
			probe.previewRect=state.preview; probe.freezeButton=state.buttons[0]; probe.exportButton=state.buttons[9]; probe.resetButton=state.buttons[8];
			for (int index=0;index<10;++index) probe.buttons[index]=state.buttons[index];
			probe.paintSamplePoint={state.client.right-12,12};
			if (const auto job=exports.Current()) probe.exportBusy=!job->done.load(std::memory_order_acquire);
			if (const auto* frame=state.channel->Displayed())
			{
				probe.hasFrame=true; probe.businessSession=frame->metadata.identity.businessSession; probe.captureSession=frame->metadata.identity.captureSession;
				probe.sequence=frame->metadata.identity.sequence; probe.referenceSequence=frame->tracker.referenceSequenceBefore;
				probe.displayAgeMs=AgeMs(frame->metadata.sampledAt); probe.copyMs=frame->copyMs; probe.totalTrackerMs=frame->totalTrackerMs;
				if (PixelsValid(*frame)) { const auto mapping=FitMapping(*frame,state.preview,state.view); const auto left=mapping.Original(0,0),right=mapping.Original(frame->width,frame->height); probe.imageRect={left.x,left.y,right.x,right.y}; }
			}
			std::scoped_lock lock(probeMutex); lastProbe=probe;
		}
		void StartExport(UiState& state)
		{
			if (const auto active=exports.Current(); active && !active->done.load(std::memory_order_acquire)) { state.notice=L"An export is already active (including a closing window's task)."; return; }
			auto data=std::make_unique<ExportData>(); data->frame=state.channel->CloneDisplayed();
			if (!data->frame) { state.notice=L"No displayed frame to export, or snapshot allocation failed."; return; }
			// 点击时完整深拷贝，目录选择期间仍可消费新帧；worker 不持 Channel/front/HWND。
			data->live=state.channel->ReadLive(); data->view=state.view; data->previewRect=state.preview;
			state.notice=exports.Start(std::move(data))?L"Choosing export directory...":L"Export could not start.";
		}
		void Button(UiState& state,int index)
		{
			switch (index)
			{
			case 0: state.view.frozen=!state.view.frozen; if (!state.view.frozen && state.channel->ConsumeLatest()) ++state.consumed; break;
			case 1: state.view.points=!state.view.points; break; case 2: state.view.flow=!state.view.flow; break;
			case 3: state.view.grid=!state.view.grid; break; case 4: state.view.bounds=!state.view.bounds; break;
			case 5: state.view.arrowScale=1; break; case 6: state.view.arrowScale=2; break; case 7: state.view.arrowScale=4; break;
			case 8: state.view.zoom=1; state.view.panX=state.view.panY=0; break; case 9: StartExport(state); break;
			default: return;
			}
			InvalidateRect(state.hwnd,nullptr,FALSE);
		}
		void Paint(UiState& state,HDC destination)
		{
			Layout(state); const int width=state.client.right,height=state.client.bottom;
			if (width<=0 || height<=0 || !state.backBuffer.resize(width,height)) { Fill(destination,state.client,Background); return; }
			const HDC dc=state.backBuffer.dc(); const DcScope saved(dc); SelectObject(dc,state.font?reinterpret_cast<HGDIOBJ>(state.font):GetStockObject(DEFAULT_GUI_FONT));
			Fill(dc,state.client,Background); Fill(dc,{0,0,width,66},Header);
			const auto* frame=state.channel->Displayed(); const auto& live=state.live.status; const double age=frame?AgeMs(frame->metadata.sampledAt):0;
			const bool stale=frame && (!std::isfinite(age) || age>1000 || frame->metadata.identity.businessSession!=live.businessSession || !live.ownsCapture || !live.capture.publishing);
			std::wstring stateLabel=L"LIVE "+Wide(StateName(live.state))+L" b/c="+Text(live.businessSession)+L"/"+Text(live.captureSession);
			if (state.view.frozen) stateLabel+=L"  FROZEN (selected frame fixed)";
			if (stale) stateLabel+=L"  STALE";
			if (LiveCaptureFailure(live)) stateLabel+=L"  CAPTURE FAILURE";
			if (live.pauseCause!=Model::PauseCause::None) stateLabel+=L"  cause="+Wide(Model::PauseCauseName(live.pauseCause));
			Label(dc,{12,5,width-28,24},stateLabel,live.state==Business::ScrollFollow::State::Paused?CompetitorColor:Foreground);
			Label(dc,{12,25,width-28,44},frame?L"DISPLAY b/c/frame/ref="+Text(frame->metadata.identity.businessSession)+L"/"+Text(frame->metadata.identity.captureSession)+L"/"+Text(frame->metadata.identity.sequence)+L"/"+Text(frame->tracker.referenceSequenceBefore)+L"  age="+NumberText(age)+L" ms  "+Wide(CV::MotionReasonName(frame->tracker.result.reason)):L"DISPLAY: no actual Tracker frame (Capture-only/AwaitTracking do not synthesize results)");
			Label(dc,{12,45,width-28,64},(live.ownsCapture?L"Capture A/R/API="+Text(live.capture.statistics.accepted)+L"/"+Text(live.capture.statistics.rejected)+L"/"+Text(live.capture.statistics.apiFailures):L"Capture: N/A (no owned session)")+L"  Tracker(window) A/R="+Text(state.live.counters.trackerAccepted)+L"/"+Text(state.live.counters.trackerRejected)+L"  Preview="+NumberText(state.previewHz)+L" Hz / "+Text(state.consumed),RGB(171,194,213));
			const wchar_t* names[]{state.view.frozen?L"Resume":L"Freeze",L"Points",L"Flow",L"Grid",L"Bounds",L"1x",L"2x",L"4x",L"Reset view",L"Export..."};
			const bool active[]{state.view.frozen,state.view.points,state.view.flow,state.view.grid,state.view.bounds,state.view.arrowScale==1,state.view.arrowScale==2,state.view.arrowScale==4,false,false};
			for (int index=0;index<10;++index)
			{
				Fill(dc,state.buttons[index],active[index]?RGB(47,91,118):Header);
				Label(dc,{state.buttons[index].left,state.buttons[index].top+4,state.buttons[index].right,state.buttons[index].bottom},names[index],Foreground,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
			}
			Label(dc,{12,state.contentTop,state.split-8,state.contentTop+20},L"ROI input + observed features  | wheel zoom, drag pan, double-click reset",RGB(171,194,213));
			Fill(dc,state.preview,CanvasBackground); const int clipped=SaveDC(dc); IntersectClipRect(dc,state.preview.left,state.preview.top,state.preview.right,state.preview.bottom);
			if (frame && PixelsValid(*frame))
			{
				const auto mapping=FitMapping(*frame,state.preview,state.view); const auto left=mapping.Original(0,0),right=mapping.Original(frame->width,frame->height);
				BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=frame->width; info.bmiHeader.biHeight=-frame->height;
				info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
				SetStretchBltMode(dc,COLORONCOLOR); StretchDIBits(dc,left.x,left.y,right.x-left.x,right.y-left.y,0,0,frame->width,frame->height,frame->bgra.data(),&info,DIB_RGB_COLORS,SRCCOPY);
				DrawAnnotations(dc,*frame,mapping,state.view);
			}
			else Label(dc,{state.preview.left+12,state.preview.top+10,state.preview.right-12,state.preview.top+30},L"Waiting for actual Tracker input; no diagnostic frame yet.",RGB(171,194,213));
			Label(dc,{state.preview.left+8,state.preview.bottom-20,state.preview.right-8,state.preview.bottom},L"Zoom "+NumberText(state.view.zoom)+L"x | arrows "+Text(state.view.arrowScale)+L"x (display only)",RGB(171,194,213));
			RestoreDC(dc,clipped);
			const int legendY=state.preview.bottom+7;
			const int legendWidth=(state.split-20)/3;
			Label(dc,{12,legendY,12+legendWidth,legendY+18},L"Green: consensus",InlierColor);
			Label(dc,{12+legendWidth,legendY,12+legendWidth*2,legendY+18},L"Gray: near-static",StationaryColor);
			Label(dc,{12+legendWidth*2,legendY,state.split-8,legendY+18},L"Orange: competitor",CompetitorColor);
			Label(dc,{12,legendY+18,12+legendWidth,legendY+36},L"Red: failed/excluded",ExcludedColor);
			Label(dc,{12+legendWidth,legendY+18,12+legendWidth*2,legendY+36},L"Cyan: new point",AddedColor);
			Label(dc,{12+legendWidth*2,legendY+18,state.split-8,legendY+36},L"Shapes: see Details",Foreground);
			Fill(dc,{state.split-2,state.contentTop,state.split+3,state.history.top-24},RGB(67,80,99));
			Label(dc,{state.stats.left,state.contentTop,state.stats.right,state.contentTop+20},L"Details (wheel / PageUp / PageDown / scrollbar)",RGB(171,194,213));
			SelectObject(dc,state.mono?reinterpret_cast<HGDIOBJ>(state.mono):GetStockObject(SYSTEM_FIXED_FONT)); const auto details=DetailLines(state);
			std::vector<int> heights; heights.reserve(details.size()); state.statsHeight=0;
			for (const auto& line:details)
			{
				RECT measure{0,0,(std::max)(1L,state.stats.right-state.stats.left-16),0}; DrawTextW(dc,line.empty()?L" ":line.c_str(),-1,&measure,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
				const int row=(std::max)(18L,measure.bottom-measure.top+3); heights.push_back(row); state.statsHeight+=row;
			}
			const int visible=state.stats.bottom-state.stats.top; state.statsScroll=(std::clamp)(state.statsScroll,0,(std::max)(0,state.statsHeight-visible));
			const int statsClip=SaveDC(dc); IntersectClipRect(dc,state.stats.left,state.stats.top,state.stats.right-12,state.stats.bottom); int y=state.stats.top-state.statsScroll;
			for (std::size_t index=0;index<details.size();++index)
			{
				if (y+heights[index]>=state.stats.top && y<state.stats.bottom)
					Label(dc,{state.stats.left,y,state.stats.right-16,y+heights[index]},details[index],details[index].starts_with(L">>>")?ExcludedColor:Foreground,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
				y+=heights[index];
			}
			RestoreDC(dc,statsClip);
			if (state.statsHeight>visible && visible>0)
			{
				Fill(dc,{state.stats.right-8,state.stats.top,state.stats.right,state.stats.bottom},Header);
				const int thumb=(std::max)(20,visible*visible/state.statsHeight), top=state.stats.top+state.statsScroll*(visible-thumb)/(state.statsHeight-visible);
				Fill(dc,{state.stats.right-8,top,state.stats.right,top+thumb},RGB(105,130,153));
			}
			const int historyTop=state.history.top-24; Fill(dc,{0,historyTop,width,height},Header);
			Label(dc,{12,historyTop+3,width-28,historyTop+22},L"LIVE numeric events (last 30; wheel to older) | "+state.notice,RGB(171,194,213));
			const int rows=(std::max)(1L,(state.history.bottom-state.history.top)/18); const int count=static_cast<int>((std::min)(state.live.count,state.live.summaries.size()));
			state.historyScroll=(std::clamp)(state.historyScroll,0,(std::max)(0,count-rows)); const int end=count-state.historyScroll, first=(std::max)(0,end-rows);
			for (int index=first;index<end;++index)
			{
				const auto& event=state.live.summaries[index]; const auto& identity=event.identity;
				std::wstring line=event.hasMotion?L"M ":event.kind==Model::SummaryKind::CaptureFailure?L"C ":L"S ";
				line+=Text(identity.businessSession)+L"/"+Text(identity.captureSession)+L"/"+Text(identity.sequence)+L" "+Wide(StateName(event.state));
				if (event.hasMotion) line+=L" "+std::wstring(event.motion.accepted?L"A ":L"R ")+Wide(CV::MotionReasonName(event.motion.reason))+L" d="+VectorText(event.motion.delta)+L" t="+VectorText(event.motion.total)+L" ms="+NumberText(event.totalTrackerMs);
				else if (event.captureFailure.valid) line+=L" "+CaptureFailureText(event.captureFailure);
				else line+=L" cause="+Wide(Model::PauseCauseName(event.pauseCause))+L" (no Tracker result)";
				line+=L" cap="+Text(event.captureAccepted)+L"/"+Text(event.captureRejected)+L"/"+Text(event.captureApiFailures)+L" applied="+Text(event.ownerAppliedSequence);
				Label(dc,{state.history.left,state.history.top+(index-first)*18,state.history.right,state.history.top+(index-first+1)*18},line,event.hasMotion&&!event.motion.accepted?ExcludedColor:Foreground);
			}
			BitBlt(destination,0,0,width,height,dc,0,0,SRCCOPY); ++state.paints; PublishProbe(state);
		}
		void Zoom(UiState& state,POINT point,double steps)
		{
			const auto* frame=state.channel->Displayed(); if (!frame || !PixelsValid(*frame)) return;
			const auto before=FitMapping(*frame,state.preview,state.view); const double x=(point.x-before.x)/before.scale,y=(point.y-before.y)/before.scale;
			state.view.zoom=(std::clamp)(state.view.zoom*std::pow(1.18,steps),.125,16.0);
			const double oldPanX=state.view.panX,oldPanY=state.view.panY; state.view.panX=state.view.panY=0;
			const auto after=FitMapping(*frame,state.preview,state.view); state.view.panX=point.x-after.x-x*after.scale; state.view.panY=point.y-after.y-y*after.scale;
			if (!std::isfinite(state.view.panX) || !std::isfinite(state.view.panY)) { state.view.panX=oldPanX; state.view.panY=oldPanY; }
		}
		void ScrollAt(UiState& state,int y) noexcept
		{
			const int visible=state.stats.bottom-state.stats.top;
			if (visible>0) state.statsScroll=(std::clamp)(static_cast<int>((y-state.stats.top)*static_cast<double>(state.statsHeight-visible)/visible),0,(std::max)(0,state.statsHeight-visible));
		}
		LRESULT CALLBACK DebugWindowProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam)
		{
			if (message==WM_CLOSE) { (void)Window::GetService().Destroy(Window::WindowRole::ScrollFollowDebug); return 0; }
			if (message==WM_GETMINMAXINFO) { reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize={760,520}; return 0; }
			auto* state=State(hwnd);
			if (message==WM_NCDESTROY)
			{
				KillTimer(hwnd,RefreshTimer); if (GetCapture()==hwnd) ReleaseCapture(); SetWindowLongPtrW(hwnd,GWLP_USERDATA,0); delete state;
				return DefWindowProcW(hwnd,message,wParam,lParam);
			}
			if (message==WM_ERASEBKGND) return 1;
			if (message==WM_PAINT)
			{
				PAINTSTRUCT paint{}; const HDC dc=BeginPaint(hwnd,&paint);
				try { if (state) Paint(*state,dc); else { RECT client{}; GetClientRect(hwnd,&client); Fill(dc,client,Background); } }
				catch (...) { Fill(dc,paint.rcPaint,Background); const wchar_t* failure=L"Diagnostic UI allocation/render failure"; TextOutW(dc,12,12,failure,lstrlenW(failure)); }
				EndPaint(hwnd,&paint); return 0;
			}
			if (!state) return DefWindowProcW(hwnd,message,wParam,lParam);
			try
			{
				switch (message)
				{
				case WM_TIMER: if (wParam==RefreshTimer) { Refresh(*state); return 0; } break;
				case WM_SIZE: Layout(*state); InvalidateRect(hwnd,nullptr,FALSE); return 0;
				case WM_LBUTTONDOWN:
				{
					const POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)}; SetFocus(hwnd);
					for (int index=0;index<10;++index) if (PtInRect(&state->buttons[index],point)) { Button(*state,index); return 0; }
					state->lastMouse=point; state->resizingSplit=std::abs(point.x-state->split)<=5 && point.y>=state->contentTop && point.y<state->history.top-24;
					state->scrollingStats=PtInRect(&state->stats,point) && point.x>=state->stats.right-12;
					state->panning=PtInRect(&state->preview,point);
					if (state->resizingSplit || state->panning || state->scrollingStats) { SetCapture(hwnd); if (state->scrollingStats) ScrollAt(*state,point.y); }
					InvalidateRect(hwnd,nullptr,FALSE); return 0;
				}
				case WM_MOUSEMOVE:
				{
					const POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
					if (GetCapture()==hwnd)
					{
						if (state->resizingSplit) { state->view.splitRatio=(std::clamp)(static_cast<double>(point.x)/(std::max)(1L,state->client.right),.35,.80); Layout(*state); }
						if (state->panning) { state->view.panX+=point.x-state->lastMouse.x; state->view.panY+=point.y-state->lastMouse.y; }
						if (state->scrollingStats) ScrollAt(*state,point.y);
						state->lastMouse=point; InvalidateRect(hwnd,nullptr,FALSE);
					}
					return 0;
				}
				case WM_LBUTTONUP: if (GetCapture()==hwnd) ReleaseCapture(); [[fallthrough]];
				case WM_CAPTURECHANGED: state->panning=state->resizingSplit=state->scrollingStats=false; return 0;
				case WM_CANCELMODE: if (GetCapture()==hwnd) ReleaseCapture(); state->panning=state->resizingSplit=state->scrollingStats=false; return 0;
				case WM_LBUTTONDBLCLK: { const POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)}; if (PtInRect(&state->preview,point)) Button(*state,8); return 0; }
				case WM_MOUSEWHEEL:
				{
					POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)}; ScreenToClient(hwnd,&point); const double steps=GET_WHEEL_DELTA_WPARAM(wParam)/120.0;
					if (PtInRect(&state->preview,point)) Zoom(*state,point,steps);
					else if (PtInRect(&state->stats,point)) state->statsScroll-=(static_cast<int>(std::lround(steps*54)));
					else if (point.y>=state->history.top-24) state->historyScroll+=static_cast<int>(std::lround(steps*3));
					InvalidateRect(hwnd,nullptr,FALSE); return 0;
				}
				case WM_KEYDOWN:
					if (wParam==VK_HOME) state->statsScroll=0;
					else if (wParam==VK_END) state->statsScroll=state->statsHeight;
					else if (wParam==VK_PRIOR) state->statsScroll-=state->stats.bottom-state->stats.top;
					else if (wParam==VK_NEXT) state->statsScroll+=state->stats.bottom-state->stats.top;
					else if (wParam==VK_UP) state->statsScroll-=18;
					else if (wParam==VK_DOWN) state->statsScroll+=18;
					InvalidateRect(hwnd,nullptr,FALSE); return 0;
				case WM_SETCURSOR:
					if (LOWORD(lParam)==HTCLIENT) { POINT point{}; GetCursorPos(&point); ScreenToClient(hwnd,&point); SetCursor(LoadCursorW(nullptr,std::abs(point.x-state->split)<=5 && point.y>=state->contentTop?IDC_SIZEWE:PtInRect(&state->preview,point)?IDC_SIZEALL:IDC_ARROW)); return TRUE; }
					break;
				default: break;
				}
			}
			catch (...) { /* 诊断界面失败不越过 WndProc，也不影响生产 Capture/Tracker。 */ }
			return DefWindowProcW(hwnd,message,wParam,lParam);
		}
		void Created(HWND hwnd)
		{
			auto state=std::make_unique<UiState>(); state->hwnd=hwnd; state->channel=Model::Activate();
			if (!state->channel) throw std::runtime_error("diagnostic channel allocation");
			state->font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
			state->mono=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
			Layout(*state); SetLastError(ERROR_SUCCESS);
			if (!SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state.get())) && GetLastError()!=ERROR_SUCCESS) throw std::runtime_error("diagnostic window state binding");
			state.release();
			if (!SetTimer(hwnd,RefreshTimer,100,nullptr)) throw std::runtime_error("diagnostic refresh timer");
			currentWindow.store(hwnd,std::memory_order_release); Refresh(*State(hwnd));
		}
		Window::WindowSpec MakeSpec(HICON icon,const RECT* initialBounds)
		{
			Window::WindowSpec spec; spec.role=Window::WindowRole::ScrollFollowDebug; spec.className=L"Inkeys.ScrollFollowDebug";
			spec.title=L"Inkeys Scroll Follow Diagnostics"; spec.x=CW_USEDEFAULT; spec.y=CW_USEDEFAULT; spec.width=1160; spec.height=780;
			if (initialBounds) { spec.x=initialBounds->left; spec.y=initialBounds->top; spec.width=initialBounds->right-initialBounds->left; spec.height=initialBounds->bottom-initialBounds->top; }
			spec.style=WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN; spec.exStyle=WS_EX_APPWINDOW|WS_EX_TOPMOST; spec.classStyle|=CS_DBLCLKS;
			spec.windowProc=DebugWindowProc; spec.largeIcon=icon; spec.smallIcon=icon; spec.bindMessages=false; spec.optional=true;
			spec.created=Created; spec.destroyed=Destroyed; return spec;
		}
	}

	HWND Handle() noexcept { return currentWindow.load(std::memory_order_acquire); }
	HWND ExportPickerHandle() noexcept { return exportPickerWindow.load(std::memory_order_acquire); }
	WNDPROC WindowProc() noexcept { return DebugWindowProc; }
	Probe ReadProbe() noexcept { try { std::scoped_lock lock(probeMutex); return lastProbe; } catch (...) { return {}; } }
	void Destroyed() noexcept { currentWindow.store(nullptr,std::memory_order_release); std::scoped_lock lock(probeMutex); lastProbe={}; }
	void ShutdownExports() noexcept { exports.Shutdown(); }
	bool Open(HICON icon,const RECT* initialBounds) noexcept
	{
		try
		{
			auto& service=Window::GetService(); if (service.Handle(Window::WindowRole::ScrollFollowDebug)) return true;
			if (!service.Create(MakeSpec(icon,initialBounds))) return false;
			if (service.Show(Window::WindowRole::ScrollFollowDebug)) return true;
			(void)service.Destroy(Window::WindowRole::ScrollFollowDebug);
		}
		catch (...) {} // 可选诊断失败不能阻断正常产品启动。
		return false;
	}

#ifdef INKEYS_MESSAGE_BOX_TESTING
	int RunExportTests() noexcept
	{
		int failures=0;
		const auto check=[&](bool condition,const char* name) { if (!condition) { std::fprintf(stderr,"FAIL scroll follow export: %s\n",name); ++failures; } };
		try
		{
			const auto root=std::filesystem::current_path();
			if (GetFileAttributesW((root/L"InkeysRepo.sln").c_str())==INVALID_FILE_ATTRIBUTES) throw std::runtime_error("export test requires repository cwd");
			const auto parent=root/L"Build"/L"ScrollFollow";
			(void)CreateDirectoryW(parent.c_str(),nullptr);
			auto checked=parent.root_path();
			for (const auto& component:parent.relative_path())
			{
				checked/=component; const DWORD attributes=GetFileAttributesW(checked.c_str());
				if (attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("unsafe export test root");
			}
			const auto directory=parent/(L"debug-export-test-"+Text(GetCurrentProcessId())+L"-"+Text(GetTickCount64()));
			if (!CreateDirectoryW(directory.c_str(),nullptr)) throw std::runtime_error("fresh export test directory");
			auto original=std::make_unique<Model::FramePacket>(); original->width=401; original->height=303; original->strideBytes=401*4;
			original->bgra.resize(original->strideBytes*303);
			for (std::size_t index=0;index<original->bgra.size();index+=4) { original->bgra[index]=110; original->bgra[index+1]=70; original->bgra[index+2]=40; original->bgra[index+3]=255; }
			original->metadata.identity={7,11,13,17,19}; original->metadata.roi={3,5,404,308}; original->metadata.captureWidth=410; original->metadata.captureHeight=310;
			original->metadata.captureStrideBytes=410*4; original->metadata.sampledAt=Clock::now(); original->metadata.sampleInterval=std::chrono::microseconds(50000);
			original->copyMs=.25; original->totalTrackerMs=2.5; original->targetPublished=true;
			auto& debug=original->tracker; debug.inputWidth=401; debug.inputHeight=303; debug.processingWidth=debug.referenceProcessingWidth=200;
			debug.processingHeight=debug.referenceProcessingHeight=151; debug.scaleX=200.0/401; debug.scaleY=151.0/303;
			debug.referenceBeforeValid=true; debug.referenceAfterValid=true; debug.referenceSequenceBefore=16; debug.referenceSequenceAfter=17;
			debug.result.accepted=true; debug.result.state=CV::MotionState::Tracking; debug.result.reason=CV::MotionReason::None; debug.result.delta={5.25,-7.5}; debug.result.total={100.5,-50.25};
			debug.completedStages=(std::uint64_t{1}<<static_cast<unsigned>(TrackerDebugStage::Preprocess))|(std::uint64_t{1}<<static_cast<unsigned>(TrackerDebugStage::Consensus));
			debug.reachedStages=debug.completedStages; debug.pointCount=1; auto& point=debug.points[0]; point.reference={50,50}; point.forward=point.refined={60,50};
			point.forwardComputed=point.forwardStatus=point.refinedComputed=point.fbValid=point.consensusEvaluated=point.inlier=true;
			debug.addedPointCount=1; debug.addedPoints[0].point={100,75.5}; debug.addedPoints[0].committed=true;
			debug.mainMotion.valid=true; debug.mainMotion.flow={10,0}; debug.mainMotion.referenceBounds={true,10,20,30,40}; debug.mainMotion.currentBounds={true,20,20,40,40};
			ExportData data; data.frame=std::make_unique<Model::FramePacket>(*original); data.view.grid=false; data.view.arrowScale=4; data.view.zoom=2; data.view.panX=37; data.view.panY=-21;
			data.live.count=1; data.live.summaries[0].hasMotion=false; data.live.summaries[0].kind=Model::SummaryKind::CaptureFailure; data.live.status.pauseCause=Model::PauseCause::CaptureApi;
			auto& failure=data.live.summaries[0].captureFailure; failure.valid=true; failure.stage=Capture::FailureStage::Read;
			failure.reason=Capture::StopReason::BackendFailure; failure.at=Clock::now(); failure.nativeError=5; failure.nativeErrorValid=true;
			std::fill(original->bgra.begin(),original->bgra.end(),std::uint8_t{0}); // 克隆后原数据复用，writer 只能读取点击时快照。
			std::wstring status; check(WriteArtifacts(data,directory,nullptr,status),"shared immutable PNG/JSON writer");
			Graphics::DibSurface raw,preview;
			check(raw.loadFromFile((directory/L"raw.png").native()) && raw.width()==401 && raw.height()==303,"odd raw PNG dimensions");
			check(preview.loadFromFile((directory/L"preview.png").native()) && preview.width()==401 && preview.height()==303,"odd annotated PNG dimensions");
			if (raw.width()==401 && raw.height()==303)
				check(std::all_of(raw.pixels().begin(),raw.pixels().end(),[](auto pixel) { return pixel==0xff28466eu; }),"raw PNG preserves complete cloned pixels");
			if (preview.width()==401 && preview.height()==303)
			{
				// 独立原图 GT：处理(100,75.5)对应原图(200.5,151.5)，GDI 标记中心舍入到(201,152)。
				check(preview.pixels()[152*401+204]==0xff3ddbefu,"cyan refill at odd-axis original pixel GT (not times two or DPI)");
				check(preview.pixels()[100*401+170]==0xff40dd76u,"four-times display arrow at original pixel GT");
				check(preview.pixels()[70*401+40]==0xff40dd76u,"current support at original pixel GT");
				check(preview.pixels()[280*401+380]==0xff28466eu,"unmarked preview pixels preserved");
			}
			Json::Value json; Json::CharReaderBuilder reader; std::string errors; std::ifstream input(directory/L"snapshot.json",std::ios::binary);
			check(Json::parseFromStream(reader,input,&json,&errors),"export JSON parses");
			check(json["frame"]["metadata"]["identity"]["windowGeneration"].asUInt64()==7 && json["frame"]["metadata"]["identity"]["sequence"].asUInt64()==17,"selected identity preserved");
			check(json["frame"]["tracker"]["result"]["delta"]["x"].asDouble()==5.25 && json["displayOptions"]["arrowScale"].asInt()==4,"display magnification does not change production result");
			check(json["frame"]["tracker"]["checks"][static_cast<Json::ArrayIndex>(TrackerDebugCheckId::JumpHigh)]["measured"].isNull(),"unevaluated check is null");
			check(!json["frame"]["tracker"]["result"]["statisticsValidity"]["fbMedian"].asBool(),"FB summary validity requires actual completed checks");
			check(json["liveAtExportClick"]["summaries"][0]["motion"].isNull(),"Capture-only summary does not fabricate rejection");
			check(json["frame"]["tracker"]["mainMotion"]["originalCellRatio"].isNull() && json["frame"]["tracker"]["mainMotion"]["separation"].isNull(),"main support does not fabricate competitor-only values");
			check(json["frame"]["tracker"]["cells"][0]["forward"].isNull() && json["frame"]["tracker"]["cells"][0]["retained"].isNull()
				&& !json["frame"]["tracker"]["cells"][0]["validity"]["added"].asBool(),"unevaluated cell groups have explicit validity and null values");
			const auto& event=json["liveAtExportClick"]["summaries"][0]["captureFailure"];
			check(event["stage"].asString()=="Read" && event["nativeError"].asUInt()==5 && event["quality"].isNull(),"original API failure detail excludes unobserved quality");
			check(json["liveAtExportClick"]["status"]["capture"].isNull(),"no owned capture is not a default InvalidBuffer observation");
			Capture::Snapshot noSample; noSample.session=13; const auto noSampleJson=CaptureSnapshotJson(noSample);
			check(noSampleJson["quality"].isNull() && noSampleJson["statistics"]["lastReadDurationUs"].isNull(),"not-yet-sampled capture has no fabricated quality or Read duration");
			noSample.statistics.rejected=1; noSample.statistics.apiFailures=1; noSample.failureStage=Capture::FailureStage::Read; noSample.lastReadCompleted=Clock::now();
			check(CaptureSnapshotJson(noSample)["quality"].isNull(),"Read API rejection without Analyze has no fabricated InvalidBuffer quality");
			noSample.qualityObserved=true; noSample.quality.status=Capture::QualityStatus::NearBlack;
			check(CaptureSnapshotJson(noSample)["quality"]["status"].asString()=="NearBlack","actual analyzed quality stays observable");
			Model::CaptureFailureInfo qualityFailure; qualityFailure.valid=true; qualityFailure.stage=Capture::FailureStage::Quality; qualityFailure.quality.status=Capture::QualityStatus::NearBlack;
			const auto qualityJson=CaptureFailureJson(qualityFailure);
			check(qualityJson["quality"]["status"].asString()=="NearBlack" && qualityJson["nativeError"].isNull(),"quality event keeps its actual observation without inventing native code");
			data.frame->tracker.timestampSeconds=std::numeric_limits<double>::quiet_NaN(); data.frame->tracker.checks[0].evaluated=true;
			data.frame->tracker.checks[0].measured=std::numeric_limits<double>::infinity();
			const std::string nonfinite=Json::writeString(Json::StreamWriterBuilder{},ExportJson(data)); std::istringstream text(nonfinite); Json::Value parsed;
			check(Json::parseFromStream(reader,text,&parsed,&errors) && parsed["frame"]["tracker"]["timestampSeconds"].asString()=="NaN"
				&& parsed["frame"]["tracker"]["checks"][0]["measured"].asString()=="+Infinity","nonfinite data stays valid JSON with explicit encoding");
			const auto cancelledDirectory=directory/L"cancelled"; check(CreateDirectoryW(cancelledDirectory.c_str(),nullptr)!=FALSE,"cancel test private directory");
			std::atomic_bool cancelled=true; check(!WriteArtifacts(data,cancelledDirectory,&cancelled,status)
				&& GetFileAttributesW((cancelledDirectory/L"raw.png").c_str())==INVALID_FILE_ATTRIBUTES,"cancel before writer creates no image");
			std::fprintf(stdout,"Scroll follow export writer test directory: %ls\n",directory.c_str());
		}
		catch (const std::exception& exception) { std::fprintf(stderr,"FAIL scroll follow export exception: %s\n",exception.what()); ++failures; }
		catch (...) { std::fputs("FAIL scroll follow export exception\n",stderr); ++failures; }
		if (!failures) std::fputs("PASS scroll follow shared export writer / odd mapping (no GUI)\n",stdout);
		return failures;
	}
#endif
}
