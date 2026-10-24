#include "../Inkeys/Inkeys/CV/VisualMotionTracker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <type_traits>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace
{
	using namespace Inkeys::CV;
	int failures = 0;
	constexpr int Width = 960, Height = 540;
	constexpr std::uint64_t Seed = 0x571AD19;
	void Expect(bool condition, const std::string& message)
	{
		if (!condition) { ++failures; std::cerr << "FAIL visual motion " << message << '\n'; }
	}
	double Distance(MotionVector a, MotionVector b = {}) { return std::hypot(a.x - b.x, a.y - b.y); }
	double Quantile(std::vector<double> data, double q)
	{
		if (data.empty()) return 0;
		std::sort(data.begin(), data.end());
		return data[static_cast<std::size_t>(q * (data.size() - 1))];
	}
	cv::Mat Canvas()
	{
		cv::Mat canvas(Height * 16 + 2400, Width * 4, CV_8UC1);
		cv::RNG rng(Seed);
		rng.fill(canvas, cv::RNG::UNIFORM, 100, 155);
		// 不可变、非周期长画布。所有帧只按绝对 camera 从它取样，绝不递推 warp 前帧。
		for (int i = 0; i < 42000; ++i)
		{
			const cv::Point p(rng.uniform(0, canvas.cols - 40), rng.uniform(0, canvas.rows - 40));
			const int shade = rng.uniform(15, 240);
			if (i % 3 == 0) cv::circle(canvas, p + cv::Point(15, 15), rng.uniform(3, 15), cv::Scalar(shade), 2);
			else cv::rectangle(canvas, cv::Rect(p.x, p.y, rng.uniform(5, 35), rng.uniform(5, 28)), cv::Scalar(shade), i % 2 ? -1 : 2);
		}
		cv::GaussianBlur(canvas, canvas, cv::Size(5, 5), 0.8);
		return canvas;
	}
	cv::Mat DocumentCanvas()
	{
		cv::Mat canvas(Height * 4, Width * 2, CV_8UC1, cv::Scalar(238));
		cv::RNG rng(Seed ^ 0x13579BDF);
		for (int y = 15; y < canvas.rows - 30; y += 27)
			for (int x = 15; x < canvas.cols - 30; x += rng.uniform(15, 38))
			{
				if (rng.uniform(0, 5) == 0) continue;
				const int width = rng.uniform(5, 13), height = rng.uniform(7, 16), shade = rng.uniform(15, 95);
				cv::line(canvas, { x, y }, { x, y + height }, cv::Scalar(shade), 2);
				cv::line(canvas, { x, y + height }, { x + width, y + height }, cv::Scalar(shade), 2);
				if (rng.uniform(0, 2)) cv::line(canvas, { x, y + height / 2 }, { x + width, y + height / 2 }, cv::Scalar(shade), 2);
			}
		cv::GaussianBlur(canvas, canvas, cv::Size(3, 3), 0.6);
		return canvas;
	}
	cv::Mat Render(const cv::Mat& canvas, MotionVector camera, int width = Width, int height = Height)
	{
		cv::Mat image;
		cv::getRectSubPix(canvas, cv::Size(width, height),
			cv::Point2f(static_cast<float>(camera.x + (width - 1) * 0.5),
				static_cast<float>(camera.y + (height - 1) * 0.5)), image);
		return image;
	}
	MotionFrame Frame(const cv::Mat& image, std::uint64_t sequence)
	{
		return { { image.ptr<std::uint8_t>(), image.step[0] * static_cast<std::size_t>(image.rows), image.step[0],
			image.cols, image.rows, image.channels() == 1 ? MotionPixelFormat::Gray8 : MotionPixelFormat::Bgra32 },
			sequence * 0.05, sequence, true };
	}
	void Dump(const char* name, std::size_t frame, const MotionResult& result, double error, double limit)
	{
		const auto& s = result.statistics;
		std::cerr << "  " << name << " frame=" << frame << " accepted=" << result.accepted
			<< " reason=" << MotionReasonName(result.reason) << " state=" << static_cast<int>(result.state)
			<< " total=" << result.total.x << ',' << result.total.y << " error=" << error << " limit=" << limit
			<< " points=" << s.attempted << '/' << s.forward << '/' << s.fbValid << '/' << s.inliers
			<< " spatial=" << s.spatialInlierRatio << " coverage=" << s.inlierCells << '/' << s.interiorCells
			<< " FB=" << s.fbMedian << '/' << s.fbP95 << " residual=" << s.residualMedian << '/' << s.residualP95
			<< " affine=" << s.affineVariation << '/' << s.affineResidual << " runnerUp=" << s.runnerUpRatio
			<< " ref=" << s.referenceSequence << '/' << s.referenceGeneration << '\n';
	}
	enum class Decoration { None, Noise, FixedUi, Dynamic, Bgra, Dark };
	void Decorate(cv::Mat& image, Decoration decoration, std::size_t frame)
	{
		if (decoration == Decoration::Noise)
		{
			cv::Mat noise(image.size(), CV_16SC1), expanded;
			cv::RNG rng(Seed + frame * 191);
			rng.fill(noise, cv::RNG::UNIFORM, -1, 2);
			image.convertTo(expanded, CV_16SC1); expanded += noise; expanded.convertTo(image, CV_8UC1);
		}
		if (decoration == Decoration::FixedUi)
		{
			const int band = image.rows / 6;
			for (int y = 0; y < image.rows; ++y) for (int x = 0; x < image.cols; ++x)
				if (y < band || x >= image.cols * 7 / 8)
				{
					std::uint32_t h = static_cast<std::uint32_t>(x / 3) * 0x9E3779B9u ^ static_cast<std::uint32_t>(y / 3) * 0x85EBCA6Bu;
					h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15;
					image.at<std::uint8_t>(y, x) = static_cast<std::uint8_t>(10 + h % 235);
				}
		}
		if (decoration == Decoration::Dynamic)
		{
			const cv::Rect rect(image.cols / 3, image.rows / 3, image.cols / 4, image.rows / 3);
			cv::RNG rng(Seed + frame * 331); rng.fill(image(rect), cv::RNG::UNIFORM, 0, 255);
		}
		if (decoration == Decoration::Dark) image.convertTo(image, CV_8UC1, 0.18, 3);
		if (decoration == Decoration::Bgra) cv::cvtColor(image, image, cv::COLOR_GRAY2BGRA);
	}
	void Sequence(const char* name, const cv::Mat& canvas, const std::vector<MotionVector>& cameras,
		Decoration decoration = Decoration::None, int width = Width, int height = Height,
		bool stationary = false, bool relayRequired = false)
	{
		VisualMotionTracker tracker;
		auto initial = Render(canvas, cameras.front(), width, height);
		Decorate(initial, decoration, 0);
		const auto initialFrame = Frame(initial, 0);
		auto result = tracker.Initialize(initialFrame);
		bool targetLocked = result.statistics.targetId != 0;
		Expect(result.accepted, std::string(name) + " initialize");
		int accepted = 0, rejected = 0, paused = 0, added = 0, reported = 0, minCells = 48, maxPoints = 0;
		double path = 0, maximum = 0, finalError = 0, maxStep = 0, maxRatio = 0, minSpatial = 1, maxFb = 0, maxResidual = 0;
		double maxAxisX = 0, maxAxisY = 0;
		std::size_t worst = 0;
		std::map<std::string, int> reasons;
		std::vector<double> durations, preprocess, flow, fit, refill;
		MotionVector previousGt{};
		for (std::size_t i = 1; i < cameras.size(); ++i)
		{
			auto image = Render(canvas, cameras[i], width, height);
			Decorate(image, decoration, i);
			const auto input = Frame(image, i); const auto previousTotal = result.total;
			const auto processBegin = std::chrono::steady_clock::now();
			result = tracker.Process(input);
			const double processMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - processBegin).count();
			const MotionVector gt{ cameras.front().x - cameras[i].x, cameras.front().y - cameras[i].y };
			path += Distance(gt, previousGt);
			const double error = Distance(result.total, gt);
			const double limit = stationary ? 0.5 : std::max(3.0, path * 0.002);
			// 首次亚像素运动可以等待可辨证据；只放行前两帧且不超过350ms，锁定后不再豁免。
			const bool discoveryWait = !result.accepted && !targetLocked && result.reason == MotionReason::AwaitingMotion
				&& result.state == MotionState::Tracking && result.statistics.targetId == 0
				&& result.statistics.targetPhase == MotionTargetPhase::Discovering
				&& Distance(result.delta) == 0 && Distance(result.total, previousTotal) == 0 && Distance(result.total) == 0;
			const bool discoveringStill = stationary && discoveryWait;
			const double discoverySeconds = input.timestampSeconds - initialFrame.timestampSeconds;
			const bool initialDiscoveryWait = !stationary && i <= 2 && discoverySeconds >= 0 && discoverySeconds <= .350 && discoveryWait;
			targetLocked = targetLocked || result.statistics.targetId != 0;
			if ((!result.accepted && !discoveringStill && !initialDiscoveryWait) || error > limit)
			{
				++failures;
				if (++reported <= 4) Dump(name, i, result, error, limit);
			}
			accepted += result.accepted ? 1 : 0; rejected += result.accepted ? 0 : 1;
			paused += result.state == MotionState::Paused ? 1 : 0;
			if (error > maximum) { maximum = error; worst = i; }
			finalError = error; maxRatio = std::max(maxRatio, error / limit);
			maxAxisX = std::max(maxAxisX, std::abs(result.total.x - gt.x)); maxAxisY = std::max(maxAxisY, std::abs(result.total.y - gt.y));
			maxStep = std::max(maxStep, Distance(result.delta, { gt.x - previousGt.x, gt.y - previousGt.y }));
			const auto& s = result.statistics;
			minSpatial = std::min(minSpatial, s.spatialInlierRatio); minCells = std::min(minCells, s.inlierCells);
			maxFb = std::max(maxFb, s.fbP95); maxResidual = std::max(maxResidual, s.residualP95);
			added += s.added;
			maxPoints = std::max(maxPoints, std::max(s.attempted, s.retained + s.added));
			preprocess.push_back(s.preprocessMs); flow.push_back(s.opticalFlowMs); fit.push_back(s.fitMs); refill.push_back(s.refillMs);
			durations.push_back(processMs);
			++reasons[MotionReasonName(result.reason)]; previousGt = gt;
		}
		if (relayRequired) Expect(result.statistics.initialSurvivors == 0 && added > 320,
			std::string(name) + " all initial points left and new points relayed");
		Expect(maxPoints <= 320, std::string(name) + " feature budget remains bounded across grid crossings");
		std::cout << std::fixed << std::setprecision(5) << "motion sequence=" << name << " seed=" << Seed
			<< " size=" << width << 'x' << height << " frames=" << cameras.size() - 1 << " path=" << path
			<< " net=" << previousGt.x << ',' << previousGt.y << " accepted=" << accepted << " rejected=" << rejected << " paused=" << paused
			<< " max=" << maximum << " final=" << finalError << " axesMax=" << maxAxisX << ',' << maxAxisY << " worst=" << worst << " ratio=" << maxRatio << " step=" << maxStep
			<< " spatialMin=" << minSpatial << " cellsMin=" << minCells << " fbP95Max=" << maxFb << " residualP95Max=" << maxResidual
			<< " added=" << added << " retained=" << result.statistics.retained << " initial=" << result.statistics.initialSurvivors << " pointsMax=" << maxPoints
			<< " relay=" << result.statistics.relayGeneration << " ref=" << result.statistics.referenceSequence
			<< " msMedian/P95=" << Quantile(durations, 0.5) << '/' << Quantile(durations, 0.95)
			<< " stagesP95=" << Quantile(preprocess, 0.95) << '/' << Quantile(flow, 0.95) << '/' << Quantile(fit, 0.95) << '/' << Quantile(refill, 0.95);
		for (const auto& [reason, count] : reasons) std::cout << ' ' << reason << '=' << count;
		std::cout << '\n';
	}
	std::vector<MotionVector> Line(int count, MotionVector step, MotionVector start = { 500, 600 })
	{
		std::vector<MotionVector> path;
		for (int i = 0; i <= count; ++i) path.push_back({ start.x + step.x * i, start.y + step.y * i });
		return path;
	}
	void TestValidSequences(const cv::Mat& canvas)
	{
		Sequence("stationary300", canvas, Line(300, {}), Decoration::None, Width, Height, true);
		Sequence("stationaryNoise300", canvas, Line(300, {}), Decoration::Noise, Width, Height, true);
		Sequence("integer12screens", canvas, Line(540, { 0, 12 }), Decoration::None, Width, Height, false, true);
		Sequence("subpixel12screens", canvas, Line(760, { 0.375, 8.625 }), Decoration::None, Width, Height, false, true);
		Sequence("quarterPixel2048", canvas, Line(2048, { 0, 0.25 }));
		Sequence("tiny2d", canvas, Line(500, { 0.125, 0.1875 }));
		std::vector<MotionVector> reverse{ { 600, 1400 } };
		for (const MotionVector step : { MotionVector{ 3.25, 2.5 }, MotionVector{ -3.25, -2.5 }, MotionVector{ -1.75, 3.125 }, MotionVector{ 1.75, -3.125 } })
			for (int i = 0; i < 160; ++i) reverse.push_back({ reverse.back().x + step.x, reverse.back().y + step.y });
		Sequence("reversal2d", canvas, reverse);
		cv::Mat horizontal; cv::transpose(canvas, horizontal);
		Sequence("horizontal10screens", horizontal, Line(650, { 14.875, 0.125 }, { 100, 600 }), Decoration::None, Width, Height, false, true);
		const auto document = DocumentCanvas();
		auto uiImage = Render(document, { 200, 400 }); Decorate(uiImage, Decoration::FixedUi, 0);
		std::vector<cv::Point2f> rawCorners;
		cv::goodFeaturesToTrack(uiImage, rawCorners, 10000, 0.01, 3);
		int uiCorners = 0;
		for (const auto p : rawCorners) uiCorners += p.y < Height / 6 || p.x >= Width * 7 / 8 ? 1 : 0;
		const double uiRatio = static_cast<double>(uiCorners) / rawCorners.size();
		Expect(uiRatio > 0.65, "fixed UI has >65% unbalanced raw corners");
		std::cout << "motion fixedUi raw=" << rawCorners.size() << " ui=" << uiCorners << " ratio=" << uiRatio << '\n';
		Sequence("fixedDenseUi", document, Line(240, { 0, 3.25 }, { 200, 400 }), Decoration::FixedUi);
		Sequence("sparseDocument", document, Line(300, { 0.375, 1.25 }, { 200, 400 }));
		Sequence("dynamicPatch", canvas, Line(160, { 0.5, 4.25 }), Decoration::Dynamic);
		Sequence("oddDimensions", canvas, Line(240, { 0.25, 3.375 }), Decoration::None, 961, 541);
		Sequence("bgra", canvas, Line(120, { 1.25, 4.5 }), Decoration::Bgra);
		Sequence("darkTexture", canvas, Line(120, { 0, 2.25 }), Decoration::Dark);
	}

	// 原实屏专用内容的不可变世界公式；固定native ROI只测试大图能力，不要求小图接受同一跳变。
	std::uint32_t NativeTexturePixel(int x, int y) noexcept
	{
		// 世界坐标的散列纹理不随帧重置；4px细节和不同位置的L角点避免周期棋盘的多解。
		std::uint32_t h = static_cast<std::uint32_t>(x / 4) * 0x9e3779b9u ^
			static_cast<std::uint32_t>(y / 4) * 0x85ebca6bu ^ 0x317bc4edu;
		h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15;
		const auto g = 30u + h % 196u;
		std::uint32_t a = static_cast<std::uint32_t>(x / 96) * 0x45d9f3bu ^
			static_cast<std::uint32_t>(y / 96) * 0x27d4eb2du;
		a ^= a >> 16;
		const int u = x % 96 - (8 + static_cast<int>(a % 40));
		const int v = y % 96 - (8 + static_cast<int>((a >> 8) % 40));
		const auto gray = ((u >= 0 && u < 3 && v >= 0 && v < 26) ||
			(v >= 0 && v < 3 && u >= 0 && u < 26)) ? 8u : g;
		return 0xff000000u | gray * 0x010101u;
	}
	constexpr int NativeWidth = 2880, NativeHeight = 1808;
	cv::Mat NativeTextureFrame(cv::Point camera)
	{
		cv::Mat image(NativeHeight, NativeWidth, CV_8UC4);
		for (int y=0;y<image.rows;++y)
		{
			auto* row=image.ptr<std::uint32_t>(y);
			for (int x=0;x<image.cols;++x) row[x]=NativeTexturePixel(x+256+camera.x,y+512+48+camera.y);
		}
		return image;
	}
	void NativeRoiCase(const char* name, const std::vector<cv::Point>& cameras, bool relayRequired = false, bool stationary = false)
	{
		VisualMotionTracker tracker;
		auto image=NativeTextureFrame(cameras.front());
		auto result=tracker.Initialize(Frame(image,0));
		Expect(result.accepted,std::string(name)+" native initialize");
		std::size_t accepted=0, rejected=0, firstRetired=0, acceptedAfterRetired=0;
		double path=0, maximum=0, maxStep=0, maxStoppedDrift=0;
		int added=0; std::vector<double> durations;
		MotionVector previous{};
		for (std::size_t n=1;n<cameras.size();++n)
		{
			const cv::Point delta=cameras[n]-cameras[n-1];
			path+=std::hypot(static_cast<double>(delta.x),static_cast<double>(delta.y));
			image=NativeTextureFrame(cameras[n]);
			const auto started=std::chrono::steady_clock::now();
			result=tracker.Process(Frame(image,n));
			durations.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
			const MotionVector expected{static_cast<double>(cameras.front().x-cameras[n].x),static_cast<double>(cameras.front().y-cameras[n].y)};
			const double error=Distance(result.total,expected), limit=stationary?.5:std::max(3.0,path*.002);
			maximum=std::max(maximum,error); maxStep=std::max(maxStep,Distance(result.delta,{-static_cast<double>(delta.x),-static_cast<double>(delta.y)}));
			accepted+=result.accepted; rejected+=!result.accepted;
			const bool discoveringStill=stationary && result.reason==MotionReason::AwaitingMotion && !result.statistics.targetId && Distance(result.delta)==0;
			Expect((result.accepted || discoveringStill) && result.state==MotionState::Tracking,std::string(name)+" native motion accepted or safe discovery still");
			Expect(error<=limit,std::string(name)+" native strict prefix error");
			if (!delta.x && !delta.y)
			{
				maxStoppedDrift=std::max(maxStoppedDrift,Distance(result.total,previous));
				Expect(Distance(result.total,previous)<=.5,std::string(name)+" native sudden stop drift");
			}
			if ((!result.accepted && !discoveringStill) || error>limit) Dump(name,n,result,error,limit);
			if (result.accepted && result.statistics.initialSurvivors==0)
			{
				if (!firstRetired) firstRetired=n;
				else ++acceptedAfterRetired;
			}
			added+=result.statistics.added; previous=result.total;
		}
		if (relayRequired) Expect(firstRetired>0 && acceptedAfterRetired>0 && added>320,std::string(name)+" native birth points retired with continued relay");
		std::cout<<"motion native sequence="<<name<<" size="<<NativeWidth<<'x'<<NativeHeight<<" frames="<<cameras.size()-1
			<<" accepted="<<accepted<<" rejected="<<rejected<<" path="<<path<<" max="<<maximum<<" final="<<Distance(result.total)
			<<" step="<<maxStep<<" stopped_drift="<<maxStoppedDrift<<" initial="<<result.statistics.initialSurvivors
			<<" first_retired_frame="<<firstRetired<<" accepted_after_retired="<<acceptedAfterRetired<<" added="<<added
			<<" msMedian/P95="<<Quantile(durations,.5)<<'/'<<Quantile(durations,.95)<<'\n';
	}
	void TestNativeRoiRejections()
	{
		const auto initial=NativeTextureFrame({0,0});
		const auto reject=[&](const char* name,const cv::Mat& changed)
		{
			VisualMotionTracker tracker; const auto initialized=tracker.Initialize(Frame(initial,0));
			Expect(initialized.accepted,std::string(name)+" native reject initialize");
			MotionResult result;
			for (int n=1;n<=3;++n)
			{
				result=tracker.Process(Frame(changed,n));
				Expect(!result.accepted && Distance(result.total)==0 && Distance(result.delta)==0 && result.statistics.referenceSequence==0 &&
					result.statistics.referenceGeneration==initialized.statistics.referenceGeneration,std::string(name)+" native reject keeps transaction");
				Expect(result.state==MotionState::Tracking,std::string(name)+" native short rejection remains recoverable");
			}
			auto expired=Frame(changed,4); expired.timestampSeconds=1.10; result=tracker.Process(expired);
			Expect(!result.accepted && result.state==MotionState::Paused && Distance(result.total)==0,std::string(name)+" native elapsed recovery budget pauses");
			auto after=Frame(initial,5); after.timestampSeconds=1.15;
			Expect(tracker.Process(after).reason==MotionReason::Paused,std::string(name)+" native expired pause stays latched");
			std::cout<<"motion native rejection="<<name<<" reason="<<MotionReasonName(result.reason)<<" inliers="<<result.statistics.inliers
				<<" fb_valid="<<result.statistics.fbValid<<" spatial="<<result.statistics.spatialInlierRatio<<" runner_up="<<result.statistics.runnerUpRatio<<'\n';
		};
		// 大ROI中的唯一局部运动应成为目标；仿射/无重叠仍保护可信事务。
		auto majorityFixed=initial.clone(); const auto moved=NativeTextureFrame({0,4});
		const cv::Rect middle(NativeWidth/4,NativeHeight/3,NativeWidth/2,NativeHeight/3);
		moved(middle).copyTo(majorityFixed(middle));
		VisualMotionTracker local; (void)local.Initialize(Frame(initial,0)); const auto followed=local.Process(Frame(majorityFixed,1));
		Expect(followed.accepted && followed.statistics.targetId && Distance(followed.total,{0,-4})<=2,"native fixed majority does not swallow unique interior target");
		reject("native-new-page-no-overlap",NativeTextureFrame({4096,8192}));
		for (const auto transform:std::vector<std::pair<double,double>>{{1.02,0},{1,1}})
		{
			const auto matrix=cv::getRotationMatrix2D(cv::Point2f(NativeWidth*.5f,NativeHeight*.5f),transform.second,transform.first);
			cv::Mat changed; cv::warpAffine(initial,changed,matrix,initial.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
			reject(transform.first==1?"native-rotation1":"native-scale1.02",changed);
		}
	}

	void TestNativeRoiLargeSteps()
	{
		NativeRoiCase("native-stationary300",std::vector<cv::Point>(301,{0,0}),false,true);
		for (const int amplitude:{16,32,64,128,192}) for (int axis=0;axis<2;++axis) for (const int sign:{-1,1})
		{
			const cv::Point step(axis?0:sign*amplitude,axis?sign*amplitude:0);
			const auto name="native-step-"+std::to_string(amplitude)+(axis?"-y":"-x")+(sign<0?"-negative":"-positive");
			// 先停止再反向，每次真实位移不超过本case幅度；不使用GT给光流初值。
			NativeRoiCase(name.c_str(),{{0,0},step,step,{0,0},-step,-step,{0,0}});
		}
		std::vector<cv::Point> relay{{0,0}};
		const int count=(NativeHeight*10+127)/128;
		for (int n=1;n<=count;++n) relay.push_back({0,n*128});
		for (int n=count-1;n>=0;--n) relay.push_back({0,n*128});
		NativeRoiCase("native-ten-heights-128-roundtrip",relay,true);
	}

	void TestRejectionTransactions(const cv::Mat& canvas)
	{
		const MotionVector start{ 500, 600 };
		const auto initial = Render(canvas, start);
		for (const int badCount : { 1, 2, 3 })
		{
			VisualMotionTracker tracker;
			const auto initialized = tracker.Initialize(Frame(initial, 0));
			Expect(initialized.accepted, "reject initialize");
			for (int i = 1; i <= badCount; ++i)
			{
				auto disturbed = Render(canvas, { start.x, start.y + i * 8 });
				const auto opposite = Render(canvas, { start.x, start.y - i * 8 });
				opposite(cv::Rect(Width / 2, 0, Width / 2, Height)).copyTo(disturbed(cv::Rect(Width / 2, 0, Width / 2, Height)));
				const auto bad = tracker.Process(Frame(disturbed, i));
				Expect(!bad.accepted && Distance(bad.total) == 0 && Distance(bad.delta) == 0, "reject keeps trusted total");
				Expect(bad.statistics.referenceGeneration == initialized.statistics.referenceGeneration
					&& bad.statistics.referenceSequence == 0, "reject keeps reference");
				Expect(bad.statistics.consecutiveRejects == i, "reject count");
				Expect(bad.state == MotionState::Tracking && bad.statistics.targetPhase == MotionTargetPhase::Recovering, "three short rejects remain recoverable");
			}
			const auto restored = Render(canvas, { start.x + 2, start.y + (badCount + 1) * 5 });
			const auto good = tracker.Process(Frame(restored, badCount + 1));
			Expect(good.accepted && good.statistics.consecutiveRejects == 0, "short rejection recovers");
			Expect(Distance(good.total, { -2, -(badCount + 1) * 5.0 }) <= 3 && Distance(good.delta, good.total) < 1e-9,
				"recovery includes entire rejected gap");
			std::cout << "motion rejection count=" << badCount << " recovered=" << good.accepted << " total="
				<< good.total.x << ',' << good.total.y << " ref=" << good.statistics.referenceSequence << '\n';
			tracker.Reset();
			Expect(tracker.Process(Frame(initial, 9)).reason == MotionReason::NotInitialized, "reset removes session");
			Expect(tracker.Initialize(Frame(restored, 10)).accepted, "explicit initialize restarts");
		}
	}
	void TestRecoveryBudgets(const cv::Mat& canvas)
	{
		const auto initial=Render(canvas,{500,600}); const cv::Mat blank(Height,Width,CV_8UC1,cv::Scalar(255));
		for (const bool frameLimit:{false,true})
		{
			VisualMotionTracker tracker; (void)tracker.Initialize(Frame(initial,0)); const int count=frameLimit?24:2;
			for (int n=1;n<=count;++n)
			{
				auto frame=Frame(blank,n); frame.timestampSeconds=frameLimit?n*.005:(n==1?.5:1.5); frame.inputLowInformation=true;
				const auto result=tracker.Process(frame);
				Expect(!result.accepted && Distance(result.total)==0 && Distance(result.delta)==0 && result.statistics.referenceSequence==0,"bounded recovery never commits low information");
				Expect((result.state==MotionState::Paused)==(n==count),frameLimit?"24-input recovery cap":"one-second recovery cap");
				if (n==count) Expect(result.reason==MotionReason::RecoveryExpired,"explicit recovery budget reason");
			}
			auto restored=Frame(initial,count+1); restored.timestampSeconds=2;
			Expect(tracker.Process(restored).reason==MotionReason::Paused,"exhausted budget requires explicit restart");
		}
	}

	void TestInvalidAndOwnership(const cv::Mat& canvas)
	{
		auto initial = Render(canvas, { 500, 600 });
		const auto goodFrame = Frame(initial, 0);
		std::vector<MotionFrame> invalid;
		auto bad = goodFrame; bad.image.data = nullptr; invalid.push_back(bad);
		bad = goodFrame; bad.image.width = -1; invalid.push_back(bad);
		bad = goodFrame; bad.image.height = 0; invalid.push_back(bad);
		bad = goodFrame; bad.image.width = 16385; invalid.push_back(bad);
		bad = goodFrame; bad.image.strideBytes = Width - 1; invalid.push_back(bad);
		bad = goodFrame; --bad.image.sizeBytes; invalid.push_back(bad);
		bad = goodFrame; bad.image.strideBytes = std::numeric_limits<std::size_t>::max(); invalid.push_back(bad);
		bad = goodFrame; bad.image.format = static_cast<MotionPixelFormat>(255); invalid.push_back(bad);
		bad = goodFrame; bad.captureValid = false; invalid.push_back(bad);
		bad = goodFrame; bad.timestampSeconds = std::numeric_limits<double>::quiet_NaN(); invalid.push_back(bad);
		for (const auto& frame : invalid)
		{
			VisualMotionTracker tracker;
			Expect(tracker.Initialize(goodFrame).accepted, "layout initialize");
			const auto result = tracker.Process(frame);
			Expect(!result.accepted && result.state == MotionState::Paused && Distance(result.total) == 0, "invalid layout immediately pauses");
		}
		VisualMotionTracker tracker;
		Expect(tracker.Initialize(goodFrame).accepted, "ownership initialize");
		initial.setTo(0); initial.release(); // 返回后调用者可立即销毁/复用原缓冲。
		auto next = Render(canvas, { 502, 605 });
		const auto moved = tracker.Process(Frame(next, 1));
		Expect(moved.accepted && Distance(moved.total, { -2, -5 }) < 3, "owns reference after borrowed lifetime");
		auto changed = Render(canvas, { 502, 605 }, Width + 1, Height);
		Expect(tracker.Process(Frame(changed, 2)).reason == MotionReason::GeometryChanged, "geometry stops");
		for (const bool bgra : { false, true })
		{
			auto pixels = Render(canvas, { 500, 600 });
			if (bgra) cv::cvtColor(pixels, pixels, cv::COLOR_GRAY2BGRA);
			cv::Mat padded(pixels.rows, pixels.cols + 17, pixels.type(), cv::Scalar::all(0));
			pixels.copyTo(padded(cv::Rect(0, 0, pixels.cols, pixels.rows)));
			auto roi = padded(cv::Rect(0, 0, pixels.cols, pixels.rows));
			auto view = Frame(roi, 0);
			view.image.sizeBytes = (pixels.rows - 1) * roi.step[0] + pixels.cols * pixels.elemSize();
			Expect(tracker.Initialize(view).accepted, "valid padded minimal size");
			auto movedPixels = Render(canvas, { 500.25, 603.5 });
			if (bgra)
			{
				cv::cvtColor(movedPixels, movedPixels, cv::COLOR_GRAY2BGRA);
				std::vector<cv::Mat> channels; cv::split(movedPixels, channels); channels[3].setTo(0); cv::merge(channels, movedPixels);
			}
			movedPixels.copyTo(roi);
			view = Frame(roi, 1); view.image.sizeBytes = (pixels.rows - 1) * roi.step[0] + pixels.cols * pixels.elemSize();
			const auto paddedResult = tracker.Process(view);
			Expect(paddedResult.accepted && Distance(paddedResult.total, { -0.25, -3.5 }) < 3, "padded gray/BGRA motion and alpha=0");
		}
		Expect(tracker.Initialize(Frame(next, 10)).accepted, "timestamp initialize");
		Expect(tracker.Process(Frame(next, 9)).reason == MotionReason::InvalidTimestamp, "backwards time stops");
	}
	void TestUntrackable(const cv::Mat& canvas)
	{
		const auto initial = Render(canvas, { 500, 600 });
		for (const int value : { 0, 1, 127, 255 })
		{
			VisualMotionTracker tracker;
			Expect(tracker.Initialize(Frame(initial, 0)).accepted, "constant initialize");
			const cv::Mat constant(Height, Width, CV_8UC1, cv::Scalar(value));
			for (int i = 1; i <= 3; ++i)
			{
				const auto result = tracker.Process(Frame(constant, i));
				Expect(!result.accepted && Distance(result.total) == 0, "constant is never reliable zero");
				Expect(result.state==(value<=1?MotionState::Paused:MotionState::Tracking), "black pauses immediately; low information remains in bounded recovery");
			}
			const auto restored=Render(canvas,{502,605}); const auto result=tracker.Process(Frame(restored,4));
			if (value<=1) Expect(!result.accepted && result.reason==MotionReason::Paused,"hard black pause sticky");
			else Expect(result.accepted && Distance(result.total,{-2,-5})<=3,"three low-information inputs recover against trusted reference");
		}
		for (const bool strip : { false, true })
		{
			cv::Mat sparse(Height, Width, CV_8UC1, cv::Scalar(127));
			if (strip) initial(cv::Rect(0, Height / 2, Width, 35)).copyTo(sparse(cv::Rect(0, Height / 2, Width, 35)));
			else for (int i = 0; i < 3; ++i) cv::rectangle(sparse, cv::Rect(100 + i * 200, 200, 8, 8), cv::Scalar(255), -1);
			VisualMotionTracker tracker;
			const auto result = tracker.Initialize(Frame(sparse, 0));
			if (strip) Expect(result.accepted && result.statistics.targetPhase==MotionTargetPhase::Discovering && !result.statistics.targetId,"local strip initializes discovery without global coverage gate");
			else Expect(!result.accepted && result.reason==MotionReason::InsufficientFeatures,"few isolated unresolved corners still cannot initialize");
			std::cout << "motion sparse strip=" << strip << " reason=" << MotionReasonName(result.reason) << " added=" << result.statistics.added
				<< " cells=" << result.statistics.inlierCells << '\n';
		}
	}
	void TestDeformations(const cv::Mat& canvas)
	{
		const auto initial = Render(canvas, { 500, 600 });
		for (const double scale : { 0.98, 1.02, 1.005 }) for (const double angle : { 0.0, 1.0, -1.0 })
		{
			VisualMotionTracker tracker;
			Expect(tracker.Initialize(Frame(initial, 0)).accepted, "deformation initialize");
			auto affine = cv::getRotationMatrix2D(cv::Point2f(Width * 0.5f, Height * 0.5f), angle, scale);
			affine.at<double>(0, 2) += 3.25; affine.at<double>(1, 2) -= 4.5;
			cv::Mat changed; cv::warpAffine(initial, changed, affine, initial.size(), cv::INTER_LINEAR, cv::BORDER_REFLECT_101);
			const auto result = tracker.Process(Frame(changed, 1));
			Expect(!result.accepted && Distance(result.total) == 0 && result.statistics.referenceSequence == 0, "zoom/rotation refuses transaction");
			std::cout << "motion deformation scale=" << scale << " angle=" << angle << " reason=" << MotionReasonName(result.reason)
				<< " variation=" << result.statistics.affineVariation << " residual=" << result.statistics.affineResidual << '\n';
		}
		for (const double angle : { -1.0, 1.0, -0.25, 0.25 })
		{
			VisualMotionTracker tracker; (void)tracker.Initialize(Frame(initial, 0));
			const auto affine = cv::getRotationMatrix2D(cv::Point2f(Width * 0.5f, Height * 0.5f), angle, 1);
			cv::Mat changed; cv::warpAffine(initial, changed, affine, initial.size(), cv::INTER_LINEAR, cv::BORDER_REFLECT_101);
			const auto result = tracker.Process(Frame(changed, 1));
			Expect(!result.accepted && Distance(result.total) == 0, "rotation including 0.25deg refuses");
			std::cout << "motion rotation angle=" << angle << " reason=" << MotionReasonName(result.reason) << " variation=" << result.statistics.affineVariation << '\n';
		}
		for (const bool rearrange : { false, true })
		{
			VisualMotionTracker tracker; (void)tracker.Initialize(Frame(initial, 0));
			auto changed = Render(canvas, { 1800, 4500 });
			if (rearrange)
			{
				const auto a = Render(canvas, { 508, 600 }), b = Render(canvas, { 492, 600 });
				a(cv::Rect(0, 0, Width / 2, Height)).copyTo(changed(cv::Rect(0, 0, Width / 2, Height)));
				b(cv::Rect(Width / 2, 0, Width / 2, Height)).copyTo(changed(cv::Rect(Width / 2, 0, Width / 2, Height)));
			}
			const auto result = tracker.Process(Frame(changed, 1));
			Expect(!result.accepted && Distance(result.total) == 0 && result.statistics.referenceSequence == 0, "rearrangement/no overlap rejected");
			std::cout << "motion layout rearrange=" << rearrange << " reason=" << MotionReasonName(result.reason) << " ratio="
				<< result.statistics.spatialInlierRatio << " runnerUp=" << result.statistics.runnerUpRatio << '\n';
		}
		// 原负例的安全意图是不能误发零位移；唯一中部运动现在应识别为目标。
		for (const double movement : { 1.0, 2.0, 4.0, 10.0 })
		{
			VisualMotionTracker tracker; (void)tracker.Initialize(Frame(initial, 0));
			auto majorityFixed = initial.clone();
			const auto moved = Render(canvas, { 500, 600 + movement });
			const cv::Rect middle(Width / 4, Height / 3, Width / 2, Height / 3);
			moved(middle).copyTo(majorityFixed(middle));
			const auto ambiguous = tracker.Process(Frame(majorityFixed, 1));
			if (movement >= 4)
				Expect(ambiguous.accepted && ambiguous.statistics.targetId && Distance(ambiguous.total,{0,-movement})<=2,
					"fixed majority follows unique interior motion instead of zero");
			else Expect(!ambiguous.accepted || Distance(ambiguous.total,{0,-movement})<=1,"small interior motion never publishes wrong displacement");
			const auto& stats = ambiguous.statistics;
			std::cout << "motion majorityFixed movement=" << movement << " accepted=" << ambiguous.accepted
				<< " reason=" << MotionReasonName(ambiguous.reason) << " total=" << ambiguous.total.x << ',' << ambiguous.total.y
				<< " support=" << stats.spatialInlierRatio << " runnerUp=" << stats.runnerUpRatio
				<< " residual=" << stats.residualMedian << '/' << stats.residualP95 << " fb=" << stats.fbMedian << '/' << stats.fbP95
				<< " cells=" << stats.inlierCells << '/' << stats.interiorCells << " inliers=" << stats.inliers << '/' << stats.fbValid << '\n';
		}
	}
	enum class DensityScene { SparseMinority, OppositeStreams, LocalCoverage };
	bool DenseCell(DensityScene scene, int x, int y, bool swap)
	{
		if (scene == DensityScene::SparseMinority) return !(x >= 2 && x < 6 && y >= 1 && y < 4);
		if (scene == DensityScene::LocalCoverage) return x >= 2 && x < 6 && y >= 2 && y < 4;
		return ((x + y) % 2 == 0) != swap;
	}
	cv::Mat DensityImage(const cv::Mat& canvas, DensityScene scene, bool swap, bool changed)
	{
		cv::Mat image(Height, Width, CV_8UC1, cv::Scalar(238));
		for (int y = 0; y < 6; ++y) for (int x = 0; x < 8; ++x)
		{
			const bool dense = DenseCell(scene, x, y, swap);
			if (changed && scene == DensityScene::LocalCoverage && !dense) continue;
			const int cellX = x * Width / 8, cellY = y * Height / 6;
			const int cellWidth = (x + 1) * Width / 8 - cellX, cellHeight = (y + 1) * Height / 6 - cellY;
			const int w = dense ? cellWidth - 32 : 8, h = dense ? cellHeight - 32 : 8;
			int shiftY = 0;
			if (changed)
			{
				if (scene == DensityScene::SparseMinority) shiftY = dense ? 0 : 8;
				else if (scene == DensityScene::OppositeStreams) shiftY = (x + y) % 2 ? -4 : 4;
				else shiftY = -8;
			}
			// 每帧从同一不可变Canvas取同一patch，再按独立GT平移整个patch（包含边界）。
			// 格间留白，避免固定裁切边界制造额外静止角点；单8px patch最多少量独立角点。
			const auto patch = Render(canvas, {500.0 + x * 140, 600.0 + y * 110}, w, h);
			const cv::Rect destination(cellX + (cellWidth - w) / 2, cellY + (cellHeight - h) / 2 + shiftY, w, h);
			patch.copyTo(image(destination));
		}
		return image;
	}
	void DensitySanity(const char* name, const cv::Mat& initial, DensityScene scene, bool swap)
	{
		cv::Mat gray; cv::resize(initial, gray, {Width / 2, Height / 2}, 0, 0, cv::INTER_AREA);
		std::vector<cv::Point2f> points; cv::goodFeaturesToTrack(gray, points, 10000, .01, 8);
		int counts[48]{};
		for (const auto p : points)
		{
			const int x = std::clamp(static_cast<int>(p.x * 8 / gray.cols), 0, 7);
			const int y = std::clamp(static_cast<int>(p.y * 6 / gray.rows), 0, 5);
			++counts[y * 8 + x];
		}
		int sparsePoints = 0, densePoints = 0, sparseCells = 0, denseCells = 0;
		for (int y = 0; y < 6; ++y) for (int x = 0; x < 8; ++x)
		{
			const int n = counts[y * 8 + x]; const bool dense = DenseCell(scene, x, y, swap);
			Expect(dense ? n >= 6 : n >= 1 && n <= 2, std::string(name) + " real GFTT density cell " + std::to_string(y * 8 + x));
			if (dense) { densePoints += n; ++denseCells; } else { sparsePoints += n; ++sparseCells; }
		}
		std::cout << "density input=" << name << " dense_cells=" << denseCells << " dense_raw_corners=" << densePoints
			<< " sparse_cells=" << sparseCells << " sparse_raw_corners=" << sparsePoints << " pixel_shift_gt=" << (scene == DensityScene::OppositeStreams ? 4 : 8) << " raw_corner_sanity_not_FB_match_count=1\n";
	}
	void DensityReject(const char* name, const cv::Mat& initial, const cv::Mat& changed, DensityScene scene)
	{
		VisualMotionTracker tracker; const auto initialized=tracker.Initialize(Frame(initial,0));
		Expect(initialized.accepted && !initialized.statistics.targetId,std::string(name)+" initialize local discovery");
		if (!initialized.accepted) { Dump(name,0,initialized,Distance(initialized.total),0); return; }
		for (int n=1;n<=3;++n)
		{
			const auto result=tracker.Process(Frame(changed,n)); const auto& stats=result.statistics;
			if (scene==DensityScene::OppositeStreams)
			{
				Expect(!result.accepted && Distance(result.total)==0 && Distance(result.delta)==0,std::string(name)+" independent opposite targets refuse arbitrary selection");
				Expect(stats.referenceSequence==0 && stats.referenceGeneration==initialized.statistics.referenceGeneration && stats.consecutiveRejects==n,
					std::string(name)+" ambiguous inputs preserve trusted reference");
				Expect(result.state==MotionState::Tracking,std::string(name)+" ambiguity remains recoverable within budget");
			}
			else
			{
				const MotionVector expected{0,scene==DensityScene::SparseMinority?8.0:-8.0};
				// 12 个4px处理块可能无法形成唯一模板；安全拒绝允许，但接受必须符合独立GT，不能发布零主模式。
				if (scene==DensityScene::LocalCoverage || result.accepted)
					Expect(result.accepted && result.statistics.targetId && Distance(result.total,expected)<=2,std::string(name)+" unique local target survives unbalanced global density");
				else Expect(Distance(result.total)==0 && Distance(result.delta)==0 && stats.referenceSequence==0 && result.state==MotionState::Tracking,std::string(name)+" insufficient tiny patch evidence preserves origin");
				Expect(!result.accepted || Distance(result.delta,n==1?expected:MotionVector{})<=2,std::string(name)+" repeated unchanged target has no accumulated drift");
			}
			std::cout<<"motion density="<<name<<" frame="<<n<<" accepted="<<result.accepted<<" reason="<<MotionReasonName(result.reason)
				<<" total="<<result.total.x<<','<<result.total.y<<" target="<<stats.targetId<<" blocks="<<stats.independentBlocks<<'\n';
		}
	}

	void TestDensityGuards(const cv::Mat& canvas)
	{
		for (const auto scene : {DensityScene::SparseMinority, DensityScene::OppositeStreams, DensityScene::LocalCoverage})
			for (const bool swap : {false, true})
			{
				if (swap && scene != DensityScene::OppositeStreams) continue;
				const char* name = scene == DensityScene::SparseMinority ? "sparse12-moving-dense36-fixed" : scene == DensityScene::LocalCoverage ? "dense8-only-coverage" :
					(swap ? "opposite24x24-swap-density" : "opposite24x24-original-density");
				const auto initial = DensityImage(canvas, scene, swap, false), changed = DensityImage(canvas, scene, swap, true);
				DensitySanity(name, initial, scene, swap); DensityReject(name, initial, changed, scene);
			}
	}

	bool DebugStageReached(const TrackerDebugSnapshot& debug, TrackerDebugStage stage)
	{
		return (debug.reachedStages & (std::uint64_t{ 1 } << static_cast<unsigned>(stage))) != 0;
	}
	const TrackerDebugCheck& DebugCheck(const TrackerDebugSnapshot& debug, TrackerDebugCheckId id)
	{
		return debug.checks[static_cast<std::size_t>(id)];
	}
	void SameMotion(const MotionResult& a, const MotionResult& b, const std::string& name)
	{
		// 显式列出全部非计时字段，避免 padding/memcmp 或忽略新阶段的可信事务差异。
#define SAME(field) Expect(a.field == b.field, name + " exact " #field)
		SAME(accepted); SAME(state); SAME(reason); SAME(delta.x); SAME(delta.y); SAME(total.x); SAME(total.y); SAME(confidence);
		SAME(statistics.inputSequence); SAME(statistics.referenceSequence); SAME(statistics.referenceGeneration); SAME(statistics.relayGeneration);
		SAME(statistics.attempted); SAME(statistics.forward); SAME(statistics.backward); SAME(statistics.fbValid); SAME(statistics.inliers);
		SAME(statistics.referenceCells); SAME(statistics.occupiedCells); SAME(statistics.inlierCells); SAME(statistics.interiorCells);
		SAME(statistics.added); SAME(statistics.retained); SAME(statistics.retired); SAME(statistics.initialSurvivors); SAME(statistics.consecutiveRejects);
		SAME(statistics.pointInlierRatio); SAME(statistics.spatialInlierRatio); SAME(statistics.survivalRatio);
		SAME(statistics.coverageX); SAME(statistics.coverageY); SAME(statistics.runnerUpRatio);
		SAME(statistics.fbMedian); SAME(statistics.fbP95); SAME(statistics.residualMedian); SAME(statistics.residualP95);
		SAME(statistics.affineVariation); SAME(statistics.affineResidual);
		SAME(statistics.targetId); SAME(statistics.targetPhase); SAME(statistics.targetPoints); SAME(statistics.provisionalPoints);
		SAME(statistics.templateCount); SAME(statistics.matchedTemplates); SAME(statistics.independentBlocks); SAME(statistics.candidateCount);
		SAME(statistics.referenceAgeSeconds); SAME(statistics.recoverySeconds); SAME(statistics.ambiguityMargin); SAME(statistics.usedKeyframe); SAME(statistics.lowPointFallback); SAME(statistics.primaryReason); SAME(statistics.expectedBlocks); SAME(statistics.verifiedBlocks);
		SAME(statistics.identityTiles); SAME(statistics.identityContradictions); SAME(statistics.identityScore); SAME(statistics.identityDisambiguated);
#undef SAME
	}
	struct DebugPair
	{
		VisualMotionTracker plain, observed;
		std::unique_ptr<TrackerDebugSnapshot> debug = std::make_unique<TrackerDebugSnapshot>();
		MotionResult last{};
		std::string name;
		explicit DebugPair(std::string label) : name(std::move(label)) {}
		MotionResult Call(const MotionFrame& frame, bool initialize = false)
		{
			const auto previous = last;
			const auto a = initialize ? plain.Initialize(frame) : plain.Process(frame);
			const auto b = initialize ? observed.Initialize(frame, debug.get()) : observed.Process(frame, debug.get());
			SameMotion(a, b, name); SameMotion(b, debug->result, name + " snapshot result");
			Expect(debug->result.statistics.preprocessMs == b.statistics.preprocessMs && debug->result.statistics.opticalFlowMs == b.statistics.opticalFlowMs
				&& debug->result.statistics.fitMs == b.statistics.fitMs && debug->result.statistics.refillMs == b.statistics.refillMs
				&& debug->result.statistics.coarseMatchMs==b.statistics.coarseMatchMs && debug->result.statistics.verificationMs==b.statistics.verificationMs, name + " snapshot copies original timings");
			Expect(debug->inputSequence == frame.sequence && debug->referenceSequenceAfter == b.statistics.referenceSequence
				&& debug->referenceGenerationAfter == b.statistics.referenceGeneration, name + " input/reference after identity");
			if (!initialize)
				Expect((debug->referenceSequenceBefore == previous.statistics.referenceSequence && debug->referenceGenerationBefore == previous.statistics.referenceGeneration)
					|| (b.statistics.usedKeyframe && debug->referenceSequenceBefore<previous.statistics.referenceSequence && debug->referenceGenerationBefore<previous.statistics.referenceGeneration),
					name + " reference before preserved independently of after");
			Expect(debug->pointCount <= 320 && debug->addedPointCount <= 320 && debug->patchCount<=24 && debug->localCandidateCount<=3 && !debug->truncated, name + " bounded arrays");
			// 当前参考与旧关键帧共用一次Process的精查预算，恢复不能重新获得三次额度。
			Expect(debug->consensusCandidatesEvaluated >= 0 && debug->consensusCandidatesEvaluated <= 3, name + " Process shares at most three pixel verifications across references");
			Expect((debug->completedStages & ~debug->reachedStages) == 0, name + " stage completeness subset");
			if (debug->ambiguityComplete && b.statistics.runnerUpRatio>0)
				Expect(debug->strongestCompetitor.valid && debug->strongestCompetitor.originalCellRatio==b.statistics.runnerUpRatio,name+" actual strongest local competitor matches production ratio");
			if (b.accepted)
			{
				Expect(debug->firstFailure == TrackerDebugCheckId::Count, name + " accepted has no failed gate");
				Expect(DebugStageReached(*debug, TrackerDebugStage::Commit), name + " accepted commit reached");
				if (!initialize)
					Expect(debug->candidateTotalValid && debug->candidateTotal.x == b.total.x && debug->candidateTotal.y == b.total.y
						&& debug->candidateDelta.x == b.delta.x && debug->candidateDelta.y == b.delta.y, name + " estimated and applied distinguished");
			}
			else if (b.reason==MotionReason::AwaitingMotion)
				Expect(debug->firstFailure==TrackerDebugCheckId::Count && Distance(b.delta)==0 && !b.statistics.targetId,name+" discovery wait is not a failed gate or published motion");
			else if (debug->exception == MotionReason::None)
				Expect(debug->firstFailure != TrackerDebugCheckId::Count, name + " rejection has actual gate");
			if (DebugStageReached(*debug, TrackerDebugStage::Forward))
			{
				int forward = 0, backward = 0, valid = 0, visibleValid = 0, inliers = 0, retained = 0, retired = 0;
				for (std::size_t i = 0; i < debug->pointCount; ++i)
				{
					const auto& p = debug->points[i];
					forward += p.forwardStatus && p.forwardInsideEvaluated && p.forwardInside;
					backward += p.backwardStatus && p.backwardFiniteEvaluated && p.backwardFinite;
					visibleValid += p.fbValid; valid += p.fbValid && (!previous.statistics.targetId || p.targetMember); inliers += p.inlier; retained += p.retained; retired += p.retired;
					Expect(!p.fbValid || (p.refinedComputed && p.fbComputed && p.refinementDistanceEvaluated && p.fbThresholdEvaluated), name + " FB validity follows real stages");
					Expect(!p.inlier || p.consensusEvaluated, name + " inliers only after consensus");
				}
				Expect(static_cast<int>(debug->pointCount) == b.statistics.attempted && forward == b.statistics.forward
					&& backward == b.statistics.backward && valid == b.statistics.fbValid && inliers == b.statistics.inliers, name + " point counts match production");
				Expect(retained == b.statistics.retained && retired == b.statistics.retired, name + " retention matches production");
				int cellReference = 0, cellForward = 0, cellFb = 0, cellInliers = 0;
				for (const auto& c : debug->cells) { cellReference += c.reference; cellForward += c.forward; cellFb += c.fbValid; cellInliers += c.inliers; }
				Expect(cellReference == b.statistics.attempted && cellForward == forward && cellFb == visibleValid && cellInliers == inliers, name + " reference grid evidence counts");
			}
			last = b;
			return b;
		}
	};
	void TestDebugExtremeAspect()
	{
		// 独立世界坐标直接生成原图；最窄竖图取合法的160px宽，覆盖粗图只有少数行/列的边界。
		for (const cv::Size size : { cv::Size(6400,160), cv::Size(160,6400), cv::Size(16384,120), cv::Size(160,16384) })
		{
			const std::string name="debug extreme aspect "+std::to_string(size.width)+"x"+std::to_string(size.height);
			const auto render=[&](cv::Point camera) {
				cv::Mat image(size,CV_8UC1);
				for (int y=0;y<image.rows;++y)
				{
					auto* row=image.ptr<std::uint8_t>(y);
					for (int x=0;x<image.cols;++x)
						row[x]=static_cast<std::uint8_t>(NativeTexturePixel(x+256+camera.x,y+512+camera.y)&255u);
				}
				return image;
			};
			DebugPair pair(name); const auto initial=render({});
			const auto initialized=pair.Call(Frame(initial,0),true);
			Expect(initialized.accepted && initialized.state==MotionState::Tracking,name+" valid texture initializes");
			if (!initialized.accepted) continue;
			const cv::Point camera=size.width>size.height?cv::Point(8,0):cv::Point(0,8);
			const auto changed=render(camera); const auto result=pair.Call(Frame(changed,1));
			const MotionVector expected{-static_cast<double>(camera.x),-static_cast<double>(camera.y)};
			Expect(pair.debug->exception==MotionReason::None && result.reason!=MotionReason::OpenCvException
				&& result.reason!=MotionReason::UnexpectedException,name+" coarse geometry stays inside real matrices");
			Expect(DebugStageReached(*pair.debug,TrackerDebugStage::CoarseMatch),name+" exercises coarse matching");
			if (result.accepted)
				Expect(result.state==MotionState::Tracking && result.statistics.targetId
					&& Distance(result.total,expected)<=2 && Distance(result.delta,expected)<=2
					&& result.statistics.referenceSequence==1,name+" accepted motion matches independent original-pixel GT");
			else
			{
				const bool evidenceHold=result.reason==MotionReason::InsufficientOverlap || result.reason==MotionReason::InsufficientFeatures
					|| result.reason==MotionReason::MatchAmbiguous || result.reason==MotionReason::TargetAmbiguous
					|| result.reason==MotionReason::ForwardBackward || result.reason==MotionReason::InconsistentTranslation
					|| result.reason==MotionReason::NonTranslation || result.reason==MotionReason::AwaitingMotion;
				Expect(evidenceHold && result.state==MotionState::Tracking && Distance(result.delta)==0
					&& Distance(result.total,initialized.total)==0 && result.statistics.referenceSequence==initialized.statistics.referenceSequence
					&& result.statistics.referenceGeneration==initialized.statistics.referenceGeneration,name+" insufficient evidence explicitly holds trusted transaction");
			}
			std::cout<<name<<" accepted="<<result.accepted<<" reason="<<MotionReasonName(result.reason)
				<<" total="<<result.total.x<<','<<result.total.y<<" candidates="<<pair.debug->consensusCandidatesEvaluated<<'\n';
		}
	}
	void TestDebugValidAndRelay(const cv::Mat& canvas)
	{
		DebugPair pair("debug odd padded BGRA relay");
		const int width = Width + 1, height = Height + 1;
		cv::Mat storage(height + 4, width + 17, CV_8UC4, cv::Scalar::all(0));
		auto roi = storage(cv::Rect(3, 2, width, height));
		const auto submit = [&](MotionVector camera, std::uint64_t sequence) {
			auto pixels = Render(canvas, camera, width, height); cv::cvtColor(pixels, pixels, cv::COLOR_GRAY2BGRA);
			pixels.copyTo(roi);
			auto frame = Frame(roi, sequence); frame.image.sizeBytes = (height - 1) * roi.step[0] + width * roi.elemSize();
			const auto result = pair.Call(frame, sequence == 0);
			Expect(result.accepted, "short debug valid/relay accepted");
			Expect(pair.debug->processingWidth == width / 2 && pair.debug->processingHeight == height / 2
				&& pair.debug->scaleX == static_cast<double>(width / 2) / width && pair.debug->scaleY == static_cast<double>(height / 2) / height,
				"actual odd processing ratio");
			storage.setTo(0); // 下一调用仍须使用 Tracker 自有参考，而非上次借用缓冲。
			return result;
		};
		(void)submit({ 500, 600 }, 0);
		(void)submit({ 500.25, 606.5 }, 1); (void)submit({ 498.5, 603.25 }, 2); (void)submit({ 500, 600 }, 3);
		Expect(!DebugCheck(*pair.debug, TrackerDebugCheckId::AffineVariationHigh).condition
			&& !DebugCheck(*pair.debug, TrackerDebugCheckId::AffineResidualLow).evaluated
			&& !DebugCheck(*pair.debug, TrackerDebugCheckId::AffineSupportHigh).evaluated, "normal affine short-circuit leaves later operands unexecuted");
		bool sawAdded = false;
		for (std::uint64_t n = 4; n <= 25; ++n)
		{
			const auto result = submit({ 500, 600 + (n - 3) * 32.0 }, n); sawAdded = sawAdded || result.statistics.added > 0;
		}
		Expect(sawAdded && pair.last.statistics.initialSurvivors == 0, "short relay retires all initial anchors");
		for (std::size_t i = 0; i < pair.debug->addedPointCount; ++i)
		{
			const auto& p = pair.debug->addedPoints[i];
			Expect(p.committed && p.birthGeneration + 1 == pair.last.statistics.referenceGeneration, "refill born at actual generation");
		}
	}
	void TestDebugRejections(const cv::Mat& canvas)
	{
		const auto initial=Render(canvas,{500,600});
		for (int badCount:{1,3})
		{
			DebugPair pair("debug bounded recovery reference"); (void)pair.Call(Frame(initial,0),true);
			for (int n=1;n<=badCount;++n)
			{
				auto changed=Render(canvas,{500,600+n*8.0}); const auto opposite=Render(canvas,{500,600-n*8.0});
				opposite(cv::Rect(Width/2,0,Width/2,Height)).copyTo(changed(cv::Rect(Width/2,0,Width/2,Height)));
				const auto result=pair.Call(Frame(changed,n));
				Expect(!result.accepted && result.state==MotionState::Tracking,"short mixed motion refused without premature permanent pause");
				Expect(pair.debug->referenceSequenceBefore==0 && pair.debug->referenceSequenceAfter==0,"rejected references remain original");
			}
			const auto restored=Render(canvas,{502,620}); const auto result=pair.Call(Frame(restored,badCount+1));
			Expect(result.accepted && pair.debug->referenceSequenceBefore==0 && pair.debug->referenceSequenceAfter==static_cast<std::uint64_t>(badCount+1)
				&& Distance(result.delta,result.total)==0,"recovery snapshot contains entire skipped-reference delta");
		}
		for (const auto scene:{DensityScene::SparseMinority,DensityScene::OppositeStreams,DensityScene::LocalCoverage})
		{
			DebugPair pair("debug local target evidence"); const auto start=DensityImage(canvas,scene,false,false),changed=DensityImage(canvas,scene,false,true);
			(void)pair.Call(Frame(start,0),true); const auto result=pair.Call(Frame(changed,1));
			Expect(DebugStageReached(*pair.debug,TrackerDebugStage::CoarseMatch) && !DebugCheck(*pair.debug,TrackerDebugCheckId::CoverageCells).evaluated
				&& !DebugCheck(*pair.debug,TrackerDebugCheckId::JumpHigh).evaluated,"local discovery reports actual coarse work without old global gates");
			if (scene==DensityScene::OppositeStreams)
			{
				Expect(!result.accepted && Distance(result.total)==0 && !pair.debug->candidateTotalValid,"competing targets cannot publish arbitrary displacement");
				if (result.reason==MotionReason::TargetAmbiguous)
				{
					const auto gate = pair.debug->firstFailure; const auto& other = pair.debug->rejectingCompetitor;
					const bool actualGate = gate == TrackerDebugCheckId::TargetConflict || gate == TrackerDebugCheckId::IndependentMotion;
					const MotionVector positive{0, 4 * pair.debug->scaleY}, negative{0, -4 * pair.debug->scaleY};
					const auto primary = pair.debug->mainMotion.flow;
					const bool oppositeGt = (Distance(primary, positive) <= .5 && Distance(other.flow, negative) <= .5)
						|| (Distance(primary, negative) <= .5 && Distance(other.flow, positive) <= .5);
					Expect(actualGate && DebugCheck(*pair.debug,gate).evaluated && DebugCheck(*pair.debug,gate).condition
						&& pair.debug->mainMotion.valid && other.valid && other.points >= 3 && other.referenceBounds.valid
						&& Distance(primary, other.flow) > 2 * pair.debug->consensusRadius && oppositeGt,"actual target-conflict evidence retained");
					if (gate == TrackerDebugCheckId::IndependentMotion)
					{
						// 独立反证允许在目标块外；必须有真实闭环点及二维间隔，不能仅凭另一个失败门通过。
						std::vector<MotionVector> independent; bool evidence = true;
						for (std::size_t i = 0; i < pair.debug->pointCount; ++i)
						{
							const auto& point = pair.debug->points[i]; if (!point.competitor) continue;
							const MotionVector flow{point.refined.x - point.reference.x, point.refined.y - point.reference.y};
							evidence = evidence && point.backwardStatus && point.backwardFinite && point.refinedComputed && point.refinedInside
								&& point.fbComputed && point.fbError <= pair.debug->fbErrorLimit && point.refinementDistanceEvaluated
								&& point.refinementDistance <= pair.debug->refinementDistanceLimit && Distance(flow, other.flow) <= .5;
							for (const auto position : independent) evidence = evidence && Distance(position, point.reference) >= 12;
							independent.push_back(point.reference);
						}
						const auto& check = DebugCheck(*pair.debug,gate);
						Expect(evidence && independent.size() >= 3 && independent.size() == static_cast<std::size_t>(other.points)
							&& check.comparison == TrackerDebugComparison::GreaterEqual && check.threshold == 3 && check.measured == other.points
							&& other.referenceBounds.maxX - other.referenceBounds.minX >= 12 && other.referenceBounds.maxY - other.referenceBounds.minY >= 12,
							"independent opposite motion exposes actual separated FB evidence");
					}
				}
			}
			else if (scene==DensityScene::LocalCoverage || result.accepted)
			{
				const MotionVector expected{0,scene==DensityScene::SparseMinority?8.0:-8.0};
				Expect(result.accepted && result.statistics.targetId && Distance(result.total,expected)<=2,"local accepted evidence follows known motion");
				Expect(pair.debug->targetSupport.valid && pair.debug->mainMotion.valid && DebugStageReached(*pair.debug,TrackerDebugStage::PatchVerification),"accepted target exposes its actual support and independent blocks");
			}
			else Expect(Distance(result.total)==0 && Distance(result.delta)==0 && result.statistics.referenceSequence==0,"tiny ambiguous sparse patches preserve trusted origin");
		}
		DebugPair pair("debug information failure"); (void)pair.Call(Frame(initial,0),true);
		const cv::Mat flat(Height,Width,CV_8UC1,cv::Scalar(127)); const auto empty=pair.Call(Frame(flat,1));
		Expect(!empty.accepted && Distance(empty.total)==0 && DebugStageReached(*pair.debug,TrackerDebugStage::CoarseMatch),"flat reaches actual local information refusal");
		Expect(!pair.debug->mainMotion.valid && !DebugStageReached(*pair.debug,TrackerDebugStage::Forward)
			&& !DebugCheck(*pair.debug,TrackerDebugCheckId::NonTranslation).evaluated,"no local hypothesis means no fabricated LK or affine work");
		auto expired=Frame(flat,2); expired.timestampSeconds=1.10; const auto paused=pair.Call(expired);
		Expect(!paused.accepted && paused.state==MotionState::Paused && DebugCheck(*pair.debug,TrackerDebugCheckId::RecoveryTimeout).condition,"actual time budget exposed");
		auto after=Frame(initial,3); after.timestampSeconds=1.15; const auto latched=pair.Call(after);
		Expect(latched.reason==MotionReason::Paused && pair.debug->reachedStages==0 && !pair.debug->mainMotion.valid
			&& !DebugCheck(*pair.debug,TrackerDebugCheckId::BlackMaximum).evaluated,"paused has no fabricated CV stages");
		DebugPair affine("debug affine safety"); (void)affine.Call(Frame(initial,0),true);
		const auto matrix=cv::getRotationMatrix2D(cv::Point2f(Width*.5f,Height*.5f),0,1.02);
		cv::Mat deformed; cv::warpAffine(initial,deformed,matrix,initial.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
		const auto nonTranslation=affine.Call(Frame(deformed,1));
		Expect(!nonTranslation.accepted && Distance(nonTranslation.total)==0 && nonTranslation.statistics.referenceSequence==0,"affine input never commits a local approximation as global translation");
		if (nonTranslation.reason==MotionReason::NonTranslation)
			Expect(affine.debug->firstFailure==TrackerDebugCheckId::NonTranslation && DebugCheck(*affine.debug,TrackerDebugCheckId::AffineVariationHigh).condition
				&& DebugCheck(*affine.debug,TrackerDebugCheckId::AffineResidualLow).condition && DebugCheck(*affine.debug,TrackerDebugCheckId::AffineSupportHigh).condition,"actual affine combined rejection exposes all evaluated operands");
	}

	void TestDebugMicroIdentity(const cv::Mat& canvas)
	{
		DebugPair pair("debug cumulative micro identity");
		const auto scene = [&](MotionVector camera, std::uint64_t sequence)
		{ auto image = Render(canvas, camera); Decorate(image, Decoration::Dynamic, sequence); return image; };
		MotionVector camera{500, 600}; auto image = scene(camera, 0); (void)pair.Call(Frame(image, 0), true);
		camera.y += 8; image = scene(camera, 1); const auto locked = pair.Call(Frame(image, 1));
		Expect(locked.accepted && locked.statistics.targetId != 0, "micro relay first locks unique visual target");
		MotionResult last = locked;
		// 累计关键帧认证不能抹平动画孔洞；微动、停止、反向都保持同一目标。
		for (std::uint64_t i = 2; i <= 49; ++i)
		{
			const double direction = i <= 25 ? 1 : i <= 31 ? 0 : -1;
			camera.x += direction * .125; camera.y += direction * .1875;
			image = scene(camera, i); last = pair.Call(Frame(image, i));
			Expect(last.accepted && Distance(last.total, {500 - camera.x, 600 - camera.y}) <= 2
				&& last.statistics.targetId == locked.statistics.targetId, "micro identity survives independent animated hole");
		}
		image = scene({2000, 5000}, 50); const auto rejected = pair.Call(Frame(image, 50));
		Expect(!rejected.accepted && Distance(rejected.delta) == 0 && Distance(rejected.total, last.total) == 0
			&& rejected.statistics.referenceSequence == last.statistics.referenceSequence, "micro-maintained identity rejects no-overlap new page transactionally");
		image = scene(camera, 51); const auto restored = pair.Call(Frame(image, 51));
		Expect(restored.accepted && Distance(restored.total, {500 - camera.x, 600 - camera.y}) <= 2
			&& restored.statistics.targetId == locked.statistics.targetId, "micro-maintained target recovers directly to trusted content");
	}

	void TestDebugInvalid(const cv::Mat& canvas)
	{
		const auto image = Render(canvas, { 500, 600 }); const auto valid = Frame(image, 0);
		std::vector<MotionFrame> invalid;
		auto frame = valid; frame.image.data = nullptr; invalid.push_back(frame);
		frame = valid; frame.image.width = 159; invalid.push_back(frame);
		frame = valid; frame.image.strideBytes = Width - 1; invalid.push_back(frame);
		frame = valid; frame.image.strideBytes = std::numeric_limits<std::size_t>::max(); invalid.push_back(frame);
		frame = valid; --frame.image.sizeBytes; invalid.push_back(frame);
		frame = valid; frame.image.format = static_cast<MotionPixelFormat>(255); invalid.push_back(frame);
		frame = valid; frame.captureValid = false; invalid.push_back(frame);
		frame = valid; frame.timestampSeconds = std::numeric_limits<double>::quiet_NaN(); invalid.push_back(frame);
		for (const auto& bad : invalid)
		{
			DebugPair pair("debug validation early return"); const auto result = pair.Call(bad, true);
			Expect(!result.accepted && result.state == MotionState::Paused && !DebugStageReached(*pair.debug, TrackerDebugStage::Preprocess), "bad input never reaches gray conversion");
			if (!bad.image.data)
				Expect(!DebugCheck(*pair.debug, TrackerDebugCheckId::WidthTooSmall).evaluated && !DebugCheck(*pair.debug, TrackerDebugCheckId::BufferTooSmall).evaluated,
					"invalid image OR short-circuit preserved");
		}
		DebugPair pair("debug invalid processing"); (void)pair.Call(valid, true);
		frame = Frame(image, 1); frame.timestampSeconds = -1; const auto time = pair.Call(frame);
		Expect(time.reason == MotionReason::InvalidTimestamp && pair.debug->firstFailure == TrackerDebugCheckId::TimestampReversed, "reference timestamp condition source");
		(void)pair.Call(valid, true);
		const auto resized = Render(canvas, { 500, 600 }, Width + 1, Height); const auto geometry = pair.Call(Frame(resized, 1));
		Expect(geometry.reason == MotionReason::GeometryChanged && pair.debug->firstFailure == TrackerDebugCheckId::WidthChanged
			&& !DebugCheck(*pair.debug, TrackerDebugCheckId::HeightChanged).evaluated, "geometry short-circuit source");
		const cv::Mat black(Height, Width, CV_8UC1, cv::Scalar(0)); (void)pair.Call(valid, true);
		const auto dark = pair.Call(Frame(black, 1)); Expect(dark.reason == MotionReason::BlackFrame && pair.debug->firstFailure == TrackerDebugCheckId::BlackMaximum, "actual black maximum");
		const cv::Mat blank(Height, Width, CV_8UC1, cv::Scalar(127)); const auto noPoints = pair.Call(Frame(blank, 0), true);
		// 少角点会继续尝试局部模板；平坦图应由实际执行的模板门拒绝。
		Expect(!noPoints.accepted && noPoints.reason == MotionReason::InsufficientFeatures
			&& DebugCheck(*pair.debug, TrackerDebugCheckId::InitialPointsLow).evaluated
			&& DebugCheck(*pair.debug, TrackerDebugCheckId::InitialPointsLow).condition
			&& pair.debug->firstFailure == TrackerDebugCheckId::TemplatesLow
			&& DebugCheck(*pair.debug, TrackerDebugCheckId::TemplatesLow).evaluated
			&& DebugCheck(*pair.debug, TrackerDebugCheckId::TemplatesLow).condition
			&& !DebugCheck(*pair.debug, TrackerDebugCheckId::CoverageCells).evaluated, "initial local templates early return");
	}

}

int RunVisualMotionTrackerTests()
{
	const auto outputFlags = std::cout.flags();
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;
	failures = 0;
	const auto canvas = Canvas();
	TestValidSequences(canvas);
	TestRejectionTransactions(canvas);
	TestRecoveryBudgets(canvas);
	TestInvalidAndOwnership(canvas);
	TestUntrackable(canvas);
	TestDeformations(canvas);
	TestNativeRoiLargeSteps();
	TestNativeRoiRejections();
	TestDensityGuards(canvas);
	std::cout << (failures ? "FAIL" : "PASS") << " visual motion tracker failures=" << failures << '\n';
	std::cout.flags(outputFlags);
	return failures;
}

int RunScrollFollowDebugTests()
{
	static_assert(std::is_trivially_copyable_v<TrackerDebugSnapshot> && std::is_nothrow_default_constructible_v<TrackerDebugSnapshot>);
	failures = 0;
	const auto begin = std::chrono::steady_clock::now(); const auto canvas = Canvas();
	TestDebugValidAndRelay(canvas); TestDebugRejections(canvas); TestDebugInvalid(canvas); TestRecoveryBudgets(canvas); TestDebugExtremeAspect(); TestDebugMicroIdentity(canvas);
	std::cout << (failures ? "FAIL" : "PASS") << " scroll follow debug short failures=" << failures
		<< " snapshot_bytes=" << sizeof(TrackerDebugSnapshot) << " seconds="
		<< std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() << '\n';
	return failures;
}
