#include "../Inkeys/Inkeys/CV/VisualMotionTracker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
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
		auto result = tracker.Initialize(Frame(initial, 0));
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
			const auto processBegin = std::chrono::steady_clock::now();
			result = tracker.Process(Frame(image, i));
			const double processMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - processBegin).count();
			const MotionVector gt{ cameras.front().x - cameras[i].x, cameras.front().y - cameras[i].y };
			path += Distance(gt, previousGt);
			const double error = Distance(result.total, gt);
			const double limit = stationary ? 0.5 : std::max(3.0, path * 0.002);
			if (!result.accepted || error > limit)
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
			Expect(result.accepted && result.state==MotionState::Tracking,std::string(name)+" native every frame accepted");
			Expect(error<=limit,std::string(name)+" native strict prefix error");
			if (!delta.x && !delta.y)
			{
				maxStoppedDrift=std::max(maxStoppedDrift,Distance(result.total,previous));
				Expect(Distance(result.total,previous)<=.5,std::string(name)+" native sudden stop drift");
			}
			if (!result.accepted || error>limit) Dump(name,n,result,error,limit);
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
				Expect((result.state==MotionState::Paused)==(n==3),std::string(name)+" native third reject pauses");
			}
			Expect(tracker.Process(Frame(initial,4)).reason==MotionReason::Paused,std::string(name)+" native pause stays latched");
			std::cout<<"motion native rejection="<<name<<" reason="<<MotionReasonName(result.reason)<<" inliers="<<result.statistics.inliers
				<<" fb_valid="<<result.statistics.fbValid<<" spatial="<<result.statistics.spatialInlierRatio<<" runner_up="<<result.statistics.runnerUpRatio<<'\n';
		};
		// 复用原多数静止/中部次流及仿射负例，只把几何换成可建到第5层的大ROI。
		auto majorityFixed=initial.clone(); const auto moved=NativeTextureFrame({0,4});
		const cv::Rect middle(NativeWidth/4,NativeHeight/3,NativeWidth/2,NativeHeight/3);
		moved(middle).copyTo(majorityFixed(middle)); reject("native-majority-fixed-middle4",majorityFixed);
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
				Expect((bad.state == MotionState::Paused) == (i == 3), "third reject latches");
			}
			const auto restored = Render(canvas, { start.x + 2, start.y + (badCount + 1) * 5 });
			const auto good = tracker.Process(Frame(restored, badCount + 1));
			if (badCount < 3)
			{
				Expect(good.accepted && good.statistics.consecutiveRejects == 0, "short rejection recovers");
				Expect(Distance(good.total, { -2, -(badCount + 1) * 5.0 }) <= 3 && Distance(good.delta, good.total) < 1e-9,
					"recovery includes entire rejected gap");
			}
			else Expect(!good.accepted && good.reason == MotionReason::Paused && Distance(good.total) == 0, "pause never auto recovers");
			std::cout << "motion rejection count=" << badCount << " recovered=" << good.accepted << " total="
				<< good.total.x << ',' << good.total.y << " ref=" << good.statistics.referenceSequence << '\n';
			tracker.Reset();
			Expect(tracker.Process(Frame(initial, 9)).reason == MotionReason::NotInitialized, "reset removes session");
			Expect(tracker.Initialize(Frame(restored, 10)).accepted, "explicit initialize restarts");
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
				if (value <= 1 || i == 3) Expect(result.state == MotionState::Paused, "black immediate / constant third pause");
			}
			Expect(!tracker.Process(Frame(initial, 4)).accepted, "hard/third pause sticky");
		}
		for (const bool strip : { false, true })
		{
			cv::Mat sparse(Height, Width, CV_8UC1, cv::Scalar(127));
			if (strip) initial(cv::Rect(0, Height / 2, Width, 35)).copyTo(sparse(cv::Rect(0, Height / 2, Width, 35)));
			else for (int i = 0; i < 3; ++i) cv::rectangle(sparse, cv::Rect(100 + i * 200, 200, 8, 8), cv::Scalar(255), -1);
			VisualMotionTracker tracker;
			const auto result = tracker.Initialize(Frame(sparse, 0));
			Expect(!result.accepted, strip ? "high point count thin strip lacks coverage" : "few isolated corners");
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
		// 多数固定区域下，独立改变中部位移，检查清楚分离的次流不能被零主模式吞掉。
		for (const double movement : { 1.0, 2.0, 4.0, 10.0 })
		{
			VisualMotionTracker tracker; (void)tracker.Initialize(Frame(initial, 0));
			auto majorityFixed = initial.clone();
			const auto moved = Render(canvas, { 500, 600 + movement });
			const cv::Rect middle(Width / 4, Height / 3, Width / 2, Height / 3);
			moved(middle).copyTo(majorityFixed(middle));
			const auto ambiguous = tracker.Process(Frame(majorityFixed, 1));
			if (movement >= 4)
				Expect(!ambiguous.accepted && ambiguous.reason == MotionReason::AmbiguousMotion && Distance(ambiguous.total) == 0
					&& Distance(ambiguous.delta) == 0 && ambiguous.statistics.referenceSequence == 0, "fixed majority with clear interior motion refuses zero");
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
		VisualMotionTracker tracker; const auto initialized = tracker.Initialize(Frame(initial, 0));
		Expect(initialized.accepted && initialized.statistics.inlierCells >= 16, std::string(name) + " full spatial initialize");
		if (!initialized.accepted) { Dump(name, 0, initialized, Distance(initialized.total), 0); return; }
		for (int n = 1; n <= 3; ++n)
		{
			const auto result = tracker.Process(Frame(changed, n)); const auto& s = result.statistics;
			Expect(!result.accepted && Distance(result.total) == 0 && Distance(result.delta) == 0, std::string(name) + " refuses both zero and any local flow");
			Expect(s.referenceSequence == 0 && s.referenceGeneration == initialized.statistics.referenceGeneration && s.consecutiveRejects == n,
				std::string(name) + " original trusted reference transaction");
			Expect((result.state == MotionState::Paused) == (n == 3), std::string(name) + " exactly third rejection pauses");
			// 实际FB/生存/fit须先通过；下方明确的目标reason证明NonTranslation联合门已过，不能单独以variation代替它。
			Expect(s.fbValid >= 40 && s.inliers >= 40 && s.survivalRatio >= .35 && s.spatialInlierRatio >= .65,
				std::string(name) + " actual FB survival and fit prerequisites");
			if (scene == DensityScene::LocalCoverage)
				Expect(result.reason == MotionReason::InsufficientCoverage && s.occupiedCells <= 8 && s.inlierCells < 16,
					std::string(name) + " dense few cells reaches Coverage rejection");
			else if (scene == DensityScene::SparseMinority)
				Expect(result.reason == MotionReason::AmbiguousMotion && s.runnerUpRatio > .15 && s.runnerUpRatio <= .35,
					std::string(name) + " original zero-mode unit-cell minority and interior gate");
			else
				Expect(result.reason == MotionReason::AmbiguousMotion && s.runnerUpRatio > .35,
					std::string(name) + " opposite streams original unit-cell runner-up gate");
			Dump(name, n, result, Distance(result.total), 0);
		}
		const auto latched = tracker.Process(Frame(initial, 4));
		Expect(!latched.accepted && latched.reason == MotionReason::Paused && latched.state == MotionState::Paused && Distance(latched.total) == 0 &&
			Distance(latched.delta) == 0 && latched.statistics.referenceSequence == 0, std::string(name) + " no automatic resume");
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
