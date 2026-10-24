#include "../Inkeys/Inkeys/CV/VisualMotionTracker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <utility>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace
{
	using namespace Inkeys::CV;
	constexpr int Width = 960, Height = 540;
	int casesRun = 0;
	std::string Environment(const char* name)
	{
		char* value = nullptr; std::size_t size = 0;
		if (_dupenv_s(&value, &size, name) != 0 || !value) return {};
		std::string result(value); std::free(value); return result;
	}
	bool SelectedCase(const std::string& name)
	{
		static const auto selected = Environment("INKEYS_TARGET_CASE");
		const bool run = selected.empty() || selected == name;
		if (run) ++casesRun; return run;
	}
	bool Detailed()
	{
		static const bool enabled = !Environment("INKEYS_TARGET_DETAIL").empty(); return enabled;
	}
	struct Sample { double camera = 0, time = 0; bool blank = false; };
	double Quantile(std::vector<double> values, double q)
	{
		if (values.empty()) return 0;
		std::sort(values.begin(), values.end());
		return values[static_cast<std::size_t>(q * (values.size() - 1))];
	}
	cv::Mat Document(int width, int height, bool periodic = false)
	{
		cv::Mat image(height, width, CV_8UC1, cv::Scalar(255));
		const char* paragraphs[] = {
			"The library opens early on weekdays.", "Read the next section before editing.",
			"A short note can explain the result.", "Tables show the measured differences.",
			"Please keep a copy of this document.", "The garden path turns near the lake.",
			"Each page contains ordinary sentences.", "Small changes should remain readable.",
			"Use the same example for comparison.", "The final chapter describes the method.",
			"A new paragraph starts below the title.", "Measurements are recorded separately."
		};
		for (int row = 0, y = 19; y < height - 8; ++row, y += 23)
		{
			std::string text = periodic ? "Repeated text repeated text repeated text" :
				std::to_string(row + 1) + ". " + paragraphs[(row * 7 + row / 11) % 12];
			cv::putText(image, text, { 10, y }, cv::FONT_HERSHEY_SIMPLEX, .42, cv::Scalar(25), 1, cv::LINE_AA);
		}
		return image;
	}
	cv::Mat Background()
	{
		cv::Mat image(Height, Width, CV_8UC1, cv::Scalar(225));
		for (int y = 18; y < Height; y += 21)
			for (int x = 6; x < Width; x += 115)
				cv::putText(image, "Menu Open Save", { x, y }, cv::FONT_HERSHEY_SIMPLEX,
					.36, cv::Scalar(65), 1, cv::LINE_AA);
		return image;
	}
	cv::Mat Image(const cv::Mat& document, const cv::Mat& background, cv::Rect pane, const Sample& sample)
	{
		auto image = background.clone();
		if (sample.blank) { image(pane).setTo(255); return image; }
		cv::Mat visible;
		// GT来自不可变正文的绝对相机位置；不递推warp，也不使用Tracker结果生成下一帧。
		cv::getRectSubPix(document, pane.size(), { (pane.width - 1) * .5f,
			static_cast<float>(sample.camera + (pane.height - 1) * .5) }, visible);
		visible.copyTo(image(pane));
		return image;
	}
	MotionFrame Frame(const cv::Mat& image, std::uint64_t sequence, double time)
	{
		return { { image.ptr<std::uint8_t>(), image.step[0] * image.rows, image.step[0],
			image.cols, image.rows, MotionPixelFormat::Gray8 }, time, sequence, true };
	}
	int RunCase(const std::string& name, cv::Rect pane, const std::vector<Sample>& samples,
		bool expectMotion = true, bool periodic = false)
	{
		if (!SelectedCase(name)) return 0;
		const auto background = Background();
		const auto document = Document(pane.width, pane.height * 14 + 200, periodic);
		VisualMotionTracker tracker;
		auto firstImage = Image(document, background, pane, samples.front());
		const auto initial = tracker.Initialize(Frame(firstImage, 1, samples.front().time));
		int correct = 0, accepted = 0, wrong = 0, held = 0, maxHeld = 0, paused = 0;
		std::vector<double> allErrors, acceptedErrors, durations;
		double lastCorrect = samples.front().time, maxGap = 0, finalError = 0;
		std::uint64_t hash = 1469598103934665603ULL;
		std::string firstFailure;
#ifndef INKEYS_VISUAL_TARGET_BASELINE
		std::unique_ptr<TrackerDebugSnapshot> debug;
		if (Detailed()) debug = std::make_unique<TrackerDebugSnapshot>();
#endif
		for (std::size_t i = 1; i < samples.size(); ++i)
		{
			auto image = Image(document, background, pane, samples[i]);
			// 完整像素指纹只用于证明A/B输入相同，绝不参与运动判断。
			for (int y = 0; y < image.rows; ++y) for (int x = 0; x < image.cols; ++x)
			{ hash ^= image.at<std::uint8_t>(y, x); hash *= 1099511628211ULL; }
			const auto begin = std::chrono::steady_clock::now();
#ifndef INKEYS_VISUAL_TARGET_BASELINE
			const auto result = tracker.Process(Frame(image, i + 1, samples[i].time), debug.get());
#else
			const auto result = tracker.Process(Frame(image, i + 1, samples[i].time));
#endif
			durations.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
			const double expected = samples.front().camera - samples[i].camera;
			const double error = std::hypot(result.total.x, result.total.y - expected);
			allErrors.push_back(error); finalError = error;
#ifndef INKEYS_VISUAL_TARGET_BASELINE
			if (Detailed()) std::cout << "TARGET_FRAME name=" << name << " sequence=" << i+1 << " time=" << samples[i].time
				<< " expected=" << expected << " total=" << result.total.y << " accepted=" << result.accepted << " reason=" << MotionReasonName(result.reason)
				<< " support=" << debug->targetSupport.minX << "," << debug->targetSupport.minY << "," << debug->targetSupport.maxX << "," << debug->targetSupport.maxY
				<< " identity_tiles=" << result.statistics.identityTiles << " contradictions=" << result.statistics.identityContradictions << " identity_score=" << result.statistics.identityScore
				<< " check=" << TrackerDebugCheckName(debug->firstFailure) << " margin=" << result.statistics.ambiguityMargin
				<< " primary=" << MotionReasonName(result.statistics.primaryReason) << " expected_blocks=" << result.statistics.expectedBlocks << " verified_blocks=" << result.statistics.verifiedBlocks
				<< " templates=" << result.statistics.templateCount << " matched=" << result.statistics.matchedTemplates << " independent=" << result.statistics.independentBlocks
				<< " mature=" << result.statistics.targetPoints << " provisional=" << result.statistics.provisionalPoints << " inliers=" << result.statistics.inliers
				<< " flow=" << debug->mainMotion.flow.x << "," << debug->mainMotion.flow.y << " unique=" << debug->mainMotion.cells
				<< " candidate0=" << debug->localCandidates[0].flow.x << "," << debug->localCandidates[0].flow.y << "," << debug->localCandidates[0].cells << "," << debug->localCandidates[0].points
				<< " candidate1=" << debug->localCandidates[1].flow.x << "," << debug->localCandidates[1].flow.y << "," << debug->localCandidates[1].cells << "," << debug->localCandidates[1].points
				<< " candidate2=" << debug->localCandidates[2].flow.x << "," << debug->localCandidates[2].flow.y << "," << debug->localCandidates[2].cells << "," << debug->localCandidates[2].points
				<< " keyframe=" << result.statistics.usedKeyframe << " coarse_ms=" << result.statistics.coarseMatchMs << " flow_ms=" << result.statistics.opticalFlowMs << '\n';
			if (Detailed() && !result.accepted && !Environment("INKEYS_TARGET_POINTS").empty())
				for (std::size_t pointIndex = 0; pointIndex < debug->pointCount; ++pointIndex)
				{
					const auto& point = debug->points[pointIndex];
					if (point.targetMember && point.fbValid) std::cout << "TARGET_POINT sequence=" << i + 1 << " point=" << pointIndex
						<< " reference=" << point.reference.x << "," << point.reference.y << " refined=" << point.refined.x << "," << point.refined.y
						<< " inlier=" << point.inlier << " weight=" << point.fitWeight << '\n';
				}
#endif
			if (result.accepted) { ++accepted; acceptedErrors.push_back(error); if (error > 5) ++wrong; }
			const bool tracked = error <= 2 && (result.accepted || samples[i].camera == samples[i - 1].camera);
			if (tracked) { ++correct; held = 0; lastCorrect = samples[i].time; }
			else { ++held; maxHeld = std::max(maxHeld, held); maxGap = std::max(maxGap, samples[i].time - lastCorrect); }
			paused += result.state == MotionState::Paused;
			if (!tracked && firstFailure.empty()) firstFailure = std::to_string(i + 1) + ":" + MotionReasonName(result.reason);
		}
		const auto count = samples.size() - 1;
		const double rate = static_cast<double>(correct) / count;
		const double maxAccepted = acceptedErrors.empty() ? 0 : *std::max_element(acceptedErrors.begin(), acceptedErrors.end());
		const bool passed = expectMotion ? initial.accepted && rate >= .95 && wrong == 0 && paused == 0 &&
			Quantile(acceptedErrors, .95) <= 2 && maxAccepted <= 5 && finalError <= 3 && maxHeld <= 2 && maxGap <= .350001
			: wrong == 0 && accepted == 0;
		std::cout << std::setprecision(10) << "TARGET_BENCH name=" << name << " frames=" << count
			<< " initialized=" << initial.accepted << " accepted=" << accepted << " correct=" << correct
			<< " wrong=" << wrong << " paused=" << paused << " all_p95=" << Quantile(allErrors, .95)
			<< " accepted_p95=" << Quantile(acceptedErrors, .95) << " accepted_max=" << maxAccepted
			<< " final_error=" << finalError << " max_held=" << maxHeld << " max_gap_s=" << maxGap
			<< " process_p50_ms=" << Quantile(durations, .5) << " process_p95_ms=" << Quantile(durations, .95)
			<< " pixel_fnv64=" << hash << " first=" << firstFailure << " pass=" << passed << '\n';
		return passed ? 0 : 1;
	}
	std::vector<Sample> Natural(int paneHeight, double speed, double hz)
	{
		std::vector<Sample> result{ { 120, 0 } };
		double camera = 120;
		const auto append = [&](double offset) { camera += offset; result.push_back({ camera, result.size() / hz }); };
		for (int i = 0; i < 6; ++i) append(paneHeight * speed / hz);
		for (int i = 0; i < 6; ++i) append(-paneHeight * speed / hz);
		for (double jitter : { 2., -3., 1., 5., -5., 0., 0., 0. }) append(jitter);
		return result;
	}
	int IdentityCases()
	{
		int failures=0;
		const auto background=Background();
		const cv::Rect target(70,135,480,270), neighbor(670,150,240,210);
		const auto content=Document(target.width,4000), animation=Document(neighbor.width,4000);
		const auto compose=[&](double main, double other) {
			auto canvas=Image(content,background,target,{120+main,0});
			return Image(animation,canvas,neighbor,{300+other,0});
		};
		if (SelectedCase("identity_animation"))
		{
			VisualMotionTracker tracker; auto first=compose(0,0); auto initial=tracker.Initialize(Frame(first,1,0));
			double main=0; unsigned correct=0, wrong=0; std::uint64_t identity=0;
			for(unsigned i=1;i<=24;++i)
			{
				// 先仅正文运动形成身份，再引入旁侧独立动画；停止/反向不能换目标。
				main+=i<=9?10:i<=13?0:-10;
				auto frame=compose(main,i<=4?0:(i-4)*13);
				const auto result=tracker.Process(Frame(frame,i+1,i*.1));
				const double error=std::hypot(result.total.x,result.total.y+main);
				correct+=error<=2 && result.accepted; wrong+=result.accepted && error>5;
#ifndef INKEYS_VISUAL_TARGET_BASELINE
				if (result.statistics.targetId) {if (!identity) identity=result.statistics.targetId; else if(identity!=result.statistics.targetId) ++wrong;}
#endif
			}
			const bool pass=initial.accepted && correct>=23 && wrong==0;
			std::cout<<"IDENTITY_BENCH name=identity_animation frames=24 correct="<<correct<<" wrong="<<wrong<<" pass="<<pass<<'\n'; failures+=!pass;
		}
		if (SelectedCase("two_equal_targets"))
		{
			const cv::Rect left(20,130,400,270), right(530,130,400,270); const auto page=Document(400,4000);
			const auto both=[&](int offset) {return Image(page,Image(page,background,left,{300.+offset,0}),right,{300.-offset,0});};
			VisualMotionTracker tracker; auto initial=both(0); (void)tracker.Initialize(Frame(initial,1,0)); unsigned wrong=0;
			for(unsigned i=1;i<=4;++i) {auto frame=both(i*16); const auto result=tracker.Process(Frame(frame,i+1,i*.1)); wrong+=result.accepted || std::hypot(result.delta.x,result.delta.y)>.001 || std::hypot(result.total.x,result.total.y)>.001;}
			std::cout<<"IDENTITY_BENCH name=two_equal_targets frames=4 wrong="<<wrong<<" pass="<<(wrong==0)<<'\n';failures+=wrong!=0;
		}
		if (SelectedCase("temporary_occlusion"))
		{
			VisualMotionTracker tracker; auto first=compose(0,0); (void)tracker.Initialize(Frame(first,1,0)); unsigned wrong=0, accepted=0; double held=0;
			for(unsigned i=1;i<=8;++i)
			{
				auto frame=compose(i*12,0); if(i==4) frame(target).setTo(255);
				const auto result=tracker.Process(Frame(frame,i+1,i*.1));
				if(i==4) wrong+=result.accepted || result.total.y!=held || result.delta.x!=0 || result.delta.y!=0;
				else { accepted+=result.accepted; wrong+=!result.accepted || std::hypot(result.total.x,result.total.y+i*12)>2; }
				held=result.total.y;
			}
			std::cout<<"IDENTITY_BENCH name=temporary_occlusion frames=8 accepted="<<accepted<<" wrong="<<wrong<<" pass="<<(wrong==0)<<'\n';failures+=wrong!=0;
		}
		for (const auto contrast : { 1., .18 })
		if (const std::string name = contrast == 1 ? "locked_periodic_new_page" : "locked_periodic_new_page_dark"; SelectedCase(name))
		{
			// 正负例使用相同亮度变换，检查弱纹理不能绕过重复段落身份保护。
			const auto scene = [&](double offset) { auto frame=compose(offset,0); if(contrast!=1) frame.convertTo(frame,CV_8UC1,contrast,3); return frame; };
			unsigned wrong=0;
			for (double jump : { 506., 529., 484. })
			{
				VisualMotionTracker tracker; auto first=scene(0); (void)tracker.Initialize(Frame(first,1,0));
				auto moved=scene(12); const auto locked=tracker.Process(Frame(moved,2,.1));
				auto covered=moved.clone(); covered(target).setTo(contrast == 1 ? 255 : 49); (void)tracker.Process(Frame(covered,3,.2));
				// 正文每22行相似，但行号不同；已无重叠的页面不能以正文别名重连。
				auto newPage=scene(12+jump); const auto rejected=tracker.Process(Frame(newPage,4,.3));
				wrong+=!locked.accepted || rejected.accepted || rejected.delta.x!=0 || rejected.delta.y!=0 ||
					rejected.total.x!=locked.total.x || rejected.total.y!=locked.total.y;
				auto restored=scene(24); const auto recovered=tracker.Process(Frame(restored,5,.4));
				wrong+=!recovered.accepted || std::hypot(recovered.total.x,recovered.total.y+24)>2;
			}
			std::cout<<"IDENTITY_BENCH name="<<name<<" cases=3 wrong="<<wrong<<" pass="<<(wrong==0)<<'\n'; failures+=wrong!=0;
		}
		if (SelectedCase("late_recovery_timeout"))
		{
			VisualMotionTracker tracker; auto first=compose(0,0); (void)tracker.Initialize(Frame(first,1,0));
			auto moved=compose(12,0); const auto locked=tracker.Process(Frame(moved,2,.1));
			auto covered=moved.clone(); covered(target).setTo(255); (void)tracker.Process(Frame(covered,3,.2));
			auto restored=compose(24,0); const auto expired=tracker.Process(Frame(restored,4,1.21));
			const bool pass=locked.accepted && !expired.accepted && expired.state==MotionState::Paused &&
				expired.total.x==locked.total.x && expired.total.y==locked.total.y && expired.delta.x==0 && expired.delta.y==0;
			std::cout<<"IDENTITY_BENCH name=late_recovery_timeout pass="<<pass<<'\n'; failures+=!pass;
		}
		if (SelectedCase("locked_target_deformation"))
		{
			unsigned wrong=0;
			for (const auto transform : { std::pair{1.02,0.}, std::pair{1.,1.} })
			{
				VisualMotionTracker tracker; auto first=compose(0,0); (void)tracker.Initialize(Frame(first,1,0));
				auto moved=compose(12,0); const auto locked=tracker.Process(Frame(moved,2,.1));
				auto changed=moved.clone(); cv::Mat deformed;
				const auto matrix=cv::getRotationMatrix2D(cv::Point2f(target.width*.5f,target.height*.5f),transform.second,transform.first);
				cv::warpAffine(moved(target),deformed,matrix,target.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101); deformed.copyTo(changed(target));
				const auto result=tracker.Process(Frame(changed,3,.2));
				wrong+=!locked.accepted || result.accepted || result.delta.x!=0 || result.delta.y!=0 ||
					result.total.x!=locked.total.x || result.total.y!=locked.total.y;
			}
			std::cout<<"IDENTITY_BENCH name=locked_target_deformation cases=2 wrong="<<wrong<<" pass="<<(wrong==0)<<'\n'; failures+=wrong!=0;
		}
		return failures;
	}

}

int RunVisualTargetTests()
{
	casesRun = 0;
	int failures = 0;
	for (int divisor : { 2, 4 })
	{
		const cv::Rect pane(Width / 4, Height / 4, Width / divisor, Height / divisor);
		for (double hz : { 6., 10., 20. }) for (double speed : { .25, .5, 1. })
			failures += RunCase("text_d" + std::to_string(divisor) + "_hz" + std::to_string(static_cast<int>(hz)) +
				"_speed" + std::to_string(static_cast<int>(speed * 100)), pane, Natural(pane.height, speed, hz));
	}
	const cv::Rect pane(Width / 4, Height / 4, Width / 2, Height / 2);
	std::vector<Sample> longPath{ { 120, 0 } };
	for (int i = 1; i <= 40; ++i) longPath.push_back({ 120. + i * pane.height / 4., i / 10. });
	for (int i = 1; i <= 40; ++i) longPath.push_back({ 120. + (40 - i) * pane.height / 4., (40 + i) / 10. });
	failures += RunCase("text_ten_viewports_return", pane, longPath);
	// 35%视口单步单列为丢帧压力，不混入常规速度矩阵。
	failures += RunCase("text_dropped_frame_35percent", pane, Natural(pane.height, 2.1, 6));
	failures += RunCase("periodic_ambiguous", pane, { { 120, 0 }, { 143, .15 }, { 166, .30 }, { 189, .45 } }, false, true);
	failures += IdentityCases();
	std::cout << "TARGET_BENCH_SUMMARY cases=" << casesRun << " failures=" << failures << '\n';
	return casesRun == 0 ? 2 : failures ? 1 : 0;
}

#ifdef INKEYS_VISUAL_TARGET_STANDALONE
int main() { return RunVisualTargetTests(); }
#endif
