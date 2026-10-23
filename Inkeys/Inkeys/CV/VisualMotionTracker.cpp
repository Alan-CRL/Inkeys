#include "VisualMotionTracker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#pragma comment(lib, "opencv_core4.lib")
#pragma comment(lib, "opencv_imgproc4.lib")
#pragma comment(lib, "opencv_video4.lib")

namespace Inkeys::CV
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr int Columns = 8, Rows = 6, CellCount = Columns * Rows;
		constexpr int MinimumPoints = 40;
		constexpr float Border = 12.0f;
		constexpr double ConsensusRadius = 1.5;
		constexpr double MinimumSpatialRatio = 0.65;
		struct Track
		{
			cv::Point2f point;
			MotionVector anchor;
			std::uint64_t birth = 0;
		};
		struct Match
		{
			std::size_t track = 0;
			cv::Point2f point, displacement;
			double fb = 0, weight = 0;
			int cell = 0;
		};
		double Milliseconds(Clock::time_point begin)
		{
			return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
		}
		double Length(double x, double y) { return std::hypot(x, y); }
		bool Finite(cv::Point2f p) { return std::isfinite(p.x) && std::isfinite(p.y); }
		bool Inside(cv::Point2f p, cv::Size size)
		{
			return Finite(p) && p.x >= Border && p.y >= Border
				&& p.x < size.width - Border && p.y < size.height - Border;
		}
		int Cell(cv::Point2f p, cv::Size size)
		{
			const int x = std::clamp(static_cast<int>(p.x * Columns / size.width), 0, Columns - 1);
			const int y = std::clamp(static_cast<int>(p.y * Rows / size.height), 0, Rows - 1);
			return y * Columns + x;
		}
		double Percentile(std::vector<double> values, double quantile)
		{
			if (values.empty()) return 0;
			std::sort(values.begin(), values.end());
			return values[static_cast<std::size_t>(quantile * (values.size() - 1))];
		}
		double WeightedMedian(std::vector<std::pair<double, double>> values)
		{
			std::stable_sort(values.begin(), values.end(), [](auto a, auto b) { return a.first < b.first; });
			double sum = 0;
			for (const auto& v : values) sum += v.second;
			double accumulated = 0;
			for (const auto& v : values)
			{
				accumulated += v.second;
				if (accumulated >= sum * 0.5) return v.first;
			}
			return values.empty() ? 0 : values.back().first;
		}
		MotionVector MedianFlow(const std::vector<Match>& matches, const std::vector<std::size_t>& indices)
		{
			std::vector<std::pair<double, double>> xs, ys;
			for (const auto i : indices)
			{
				xs.emplace_back(matches[i].displacement.x, matches[i].weight);
				ys.emplace_back(matches[i].displacement.y, matches[i].weight);
			}
			return { WeightedMedian(std::move(xs)), WeightedMedian(std::move(ys)) };
		}
		std::vector<std::size_t> Inliers(const std::vector<Match>& matches, MotionVector flow)
		{
			std::vector<std::size_t> indices;
			for (std::size_t i = 0; i < matches.size(); ++i)
				if (Length(matches[i].displacement.x - flow.x, matches[i].displacement.y - flow.y) <= ConsensusRadius)
					indices.push_back(i);
			return indices;
		}
		double Weight(const std::vector<Match>& matches, const std::vector<std::size_t>& indices)
		{
			double weight = 0;
			for (auto i : indices) weight += matches[i].weight;
			return weight;
		}
		// 歧义检查保留原单位格票，避免稀疏真实运动因拟合降权而被吞掉。
		double OriginalCellWeight(const std::vector<Match>& matches, const std::vector<std::size_t>& indices,
			const std::array<int, CellCount>& counts)
		{
			double weight = 0;
			for (const auto i : indices) weight += 1.0 / counts[matches[i].cell];
			return weight;
		}
		double Support(const std::vector<Match>& matches, MotionVector flow)
		{
			double weight = 0;
			for (const auto& m : matches)
			{
				const double x = m.displacement.x - flow.x, y = m.displacement.y - flow.y;
				if (x * x + y * y <= ConsensusRadius * ConsensusRadius) weight += m.weight;
			}
			return weight;
		}
		bool Coverage(const std::vector<Match>& matches, const std::vector<std::size_t>& indices,
			cv::Size size, MotionStatistics& stats)
		{
			std::array<bool, CellCount> cells{};
			std::array<bool, Columns> columns{};
			std::array<bool, Rows> rows{};
			double minX = size.width, minY = size.height, maxX = 0, maxY = 0;
			for (const auto i : indices)
			{
				const auto& m = matches[i];
				cells[m.cell] = true;
				columns[m.cell % Columns] = rows[m.cell / Columns] = true;
				minX = std::min(minX, static_cast<double>(m.point.x));
				minY = std::min(minY, static_cast<double>(m.point.y));
				maxX = std::max(maxX, static_cast<double>(m.point.x));
				maxY = std::max(maxY, static_cast<double>(m.point.y));
			}
			stats.inlierCells = static_cast<int>(std::count(cells.begin(), cells.end(), true));
			stats.interiorCells = 0;
			for (int y = 1; y < Rows - 1; ++y)
				for (int x = 1; x < Columns - 1; ++x)
					stats.interiorCells += cells[y * Columns + x] ? 1 : 0;
			stats.coverageX = std::max(0.0, maxX - minX) / size.width;
			stats.coverageY = std::max(0.0, maxY - minY) / size.height;
			return stats.inlierCells >= 16 && stats.interiorCells >= 8
				&& std::count(columns.begin(), columns.end(), true) >= 4
				&& std::count(rows.begin(), rows.end(), true) >= 3
				&& stats.coverageX >= 0.5 && stats.coverageY >= 0.4;
		}
		MotionReason Validate(const MotionFrame& frame)
		{
			if (!frame.captureValid) return MotionReason::InvalidCapture;
			if (!std::isfinite(frame.timestampSeconds)) return MotionReason::InvalidTimestamp;
			const auto& image = frame.image;
			if (!image.data || image.width < 160 || image.height < 120 || image.width > 16384 || image.height > 16384)
				return MotionReason::InvalidImage;
			std::size_t bytesPerPixel = 0;
			switch (image.format)
			{
			case MotionPixelFormat::Gray8: bytesPerPixel = 1; break;
			case MotionPixelFormat::Bgra32: bytesPerPixel = 4; break;
			default: return MotionReason::InvalidImage;
			}
			const auto rowBytes = static_cast<std::size_t>(image.width) * bytesPerPixel;
			const auto rowsBeforeLast = static_cast<std::size_t>(image.height - 1);
			// 先检查布局和乘加溢出，之后才允许 OpenCV 接触借用数据。
			if (image.strideBytes < rowBytes || image.strideBytes > (std::numeric_limits<std::size_t>::max() - rowBytes) / rowsBeforeLast
				|| rowsBeforeLast * image.strideBytes + rowBytes > image.sizeBytes)
				return MotionReason::InvalidImage;
			return MotionReason::None;
		}
		cv::Mat Gray(const MotionImageView& image)
		{
			cv::Mat borrowed(image.height, image.width,
				image.format == MotionPixelFormat::Gray8 ? CV_8UC1 : CV_8UC4,
				const_cast<std::uint8_t*>(image.data), image.strideBytes);
			cv::Mat fullGray, gray;
			if (image.format == MotionPixelFormat::Gray8) fullGray = borrowed;
			else cv::cvtColor(borrowed, fullGray, cv::COLOR_BGRA2GRAY);
			cv::resize(fullGray, gray, cv::Size(image.width / 2, image.height / 2), 0, 0, cv::INTER_AREA);
			return gray;
		}
		bool Black(const cv::Mat& gray)
		{
			double maximum = 0;
			cv::minMaxLoc(gray, nullptr, &maximum);
			return maximum <= 2; // 暗色纹理仍可用；只有几乎全零的画面立即关门。
		}
		int Refill(const cv::Mat& gray, std::vector<Track>& tracks, MotionVector total,
			double scaleX, double scaleY, std::uint64_t birth)
		{
			std::array<int, CellCount> counts{};
			cv::Mat available(gray.size(), CV_8UC1, cv::Scalar(255));
			available.rowRange(0, static_cast<int>(Border)).setTo(0);
			available.rowRange(gray.rows - static_cast<int>(Border), gray.rows).setTo(0);
			available.colRange(0, static_cast<int>(Border)).setTo(0);
			available.colRange(gray.cols - static_cast<int>(Border), gray.cols).setTo(0);
			for (const auto& t : tracks)
			{
				++counts[Cell(t.point, gray.size())];
				cv::circle(available, t.point, 8, cv::Scalar(0), -1);
			}
			const auto oldCount = tracks.size();
			for (int cell = 0; cell < CellCount; ++cell)
			{
				const int quota = 6 + (cell < 32 ? 1 : 0); // 48 格最多 320 点，工具栏不能靠点数垄断。
				if (counts[cell] >= quota) continue;
				const int x = cell % Columns, y = cell / Columns;
				const cv::Rect roi(x * gray.cols / Columns, y * gray.rows / Rows,
					(x + 1) * gray.cols / Columns - x * gray.cols / Columns,
					(y + 1) * gray.rows / Rows - y * gray.rows / Rows);
				cv::Mat mask = cv::Mat::zeros(gray.size(), CV_8UC1);
				available(roi).copyTo(mask(roi));
				// 仅检测当前格，保留梯度/局部极值的 halo，格线不能成为虚假的图像边界。
				const cv::Rect search = (roi + cv::Size(8, 8) - cv::Point(4, 4)) & cv::Rect(0, 0, gray.cols, gray.rows);
				std::vector<cv::Point2f> points;
				cv::goodFeaturesToTrack(gray(search), points, (quota - counts[cell]) * 2, 0.01, 8, mask(search), 3, false);
				for (auto& p : points) p += cv::Point2f(static_cast<float>(search.x), static_cast<float>(search.y));
				if (!points.empty()) cv::cornerSubPix(gray, points, cv::Size(3, 3), cv::Size(-1, -1),
					cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, 0.001));
				for (const auto p : points)
				{
					if (counts[cell] >= quota) break;
					if (!Inside(p, gray.size()) || Cell(p, gray.size()) != cell) continue;
					bool close = false;
					for (const auto& t : tracks) if (cv::norm(t.point - p) < 8) { close = true; break; }
					if (close) continue;
					tracks.push_back({ p, { p.x / scaleX - total.x, p.y / scaleY - total.y }, birth });
					++counts[cell];
				}
			}
			return static_cast<int>(tracks.size() - oldCount);
		}
		// 在平移裁剪前检查全位移场，仿射仅用于拒绝，不参与输出变换。
		bool NonTranslation(const std::vector<Match>& matches, cv::Size size, MotionVector translation,
			MotionStatistics& stats)
		{
			std::array<std::vector<double>, CellCount> xs, ys, px, py;
			for (const auto& m : matches)
			{
				xs[m.cell].push_back(m.displacement.x); ys[m.cell].push_back(m.displacement.y);
				px[m.cell].push_back(m.point.x); py[m.cell].push_back(m.point.y);
			}
			std::vector<cv::Vec3d> coordinates;
			std::vector<cv::Vec2d> flows;
			for (int cell = 0; cell < CellCount; ++cell)
				if (!xs[cell].empty())
				{
					coordinates.emplace_back(1, Percentile(px[cell], 0.5) / size.width - 0.5,
						Percentile(py[cell], 0.5) / size.height - 0.5);
					flows.emplace_back(Percentile(xs[cell], 0.5), Percentile(ys[cell], 0.5));
				}
			if (coordinates.size() < 16) return false;
			std::vector<double> weights(coordinates.size(), 1);
			cv::Matx<double, 3, 2> model{};
			for (int iteration = 0; iteration < 5; ++iteration)
			{
				cv::Matx33d lhs = cv::Matx33d::zeros();
				cv::Matx<double, 3, 2> rhs = cv::Matx<double, 3, 2>::zeros();
				for (std::size_t i = 0; i < coordinates.size(); ++i)
				{
					const auto& c = coordinates[i];
					for (int row = 0; row < 3; ++row)
					{
						for (int column = 0; column < 3; ++column) lhs(row, column) += weights[i] * c[row] * c[column];
						for (int column = 0; column < 2; ++column) rhs(row, column) += weights[i] * c[row] * flows[i][column];
					}
				}
				if (!cv::solve(lhs, rhs, model, cv::DECOMP_SVD)) return false;
				for (std::size_t i = 0; i < coordinates.size(); ++i)
				{
					const cv::Vec2d predicted = model.t() * coordinates[i];
					const double residual = cv::norm(predicted - flows[i]);
					weights[i] = residual <= 0.75 ? 1 : 0.75 / residual;
				}
			}
			std::vector<double> affineResiduals, translationResiduals;
			int supported = 0;
			for (std::size_t i = 0; i < coordinates.size(); ++i)
			{
				const double residual = cv::norm(model.t() * coordinates[i] - flows[i]);
				affineResiduals.push_back(residual);
				translationResiduals.push_back(Length(flows[i][0] - translation.x, flows[i][1] - translation.y));
				supported += residual <= 0.8 ? 1 : 0;
			}
			stats.affineResidual = Percentile(affineResiduals, 0.5);
			for (const double x : { -0.5, 0.5 }) for (const double y : { -0.5, 0.5 })
				stats.affineVariation = std::max(stats.affineVariation,
					Length(model(1, 0) * x + model(2, 0) * y, model(1, 1) * x + model(2, 1) * y));
			return stats.affineVariation > 1.0 && stats.affineResidual < 0.55 * Percentile(translationResiduals, 0.5)
				&& static_cast<double>(supported) / coordinates.size() >= MinimumSpatialRatio;
		}
	}

	struct VisualMotionTracker::Impl
	{
		cv::Mat reference;
		std::vector<Track> tracks;
		int width = 0, height = 0;
		MotionPixelFormat format = MotionPixelFormat::Gray8;
		double scaleX = 0, scaleY = 0, referenceTime = 0;
		MotionVector total{};
		MotionState state = MotionState::Uninitialized;
		std::uint64_t sequence = 0, generation = 0;
		int rejects = 0;

		MotionResult Result(MotionStatistics stats = {}) const
		{
			stats.referenceSequence = sequence;
			stats.referenceGeneration = generation;
			stats.relayGeneration = generation;
			stats.consecutiveRejects = rejects;
			MotionResult result;
			result.total = total; result.state = state; result.statistics = stats;
			return result;
		}
		MotionResult Reject(MotionReason reason, MotionStatistics stats, bool severe = false)
		{
			++rejects;
			if (severe || rejects >= 3) state = MotionState::Paused;
			auto result = Result(stats); result.reason = reason;
			return result;
		}
		MotionResult Initialize(const MotionFrame& frame)
		{
			MotionStatistics stats{};
			stats.inputSequence = frame.sequence;
			const auto valid = Validate(frame);
			if (valid != MotionReason::None) return Reject(valid, stats, true);
			const auto begin = Clock::now();
			auto gray = Gray(frame.image);
			stats.preprocessMs = Milliseconds(begin);
			if (Black(gray)) return Reject(MotionReason::BlackFrame, stats, true);
			const double sx = static_cast<double>(gray.cols) / frame.image.width;
			const double sy = static_cast<double>(gray.rows) / frame.image.height;
			std::vector<Track> initial;
			const auto refillBegin = Clock::now();
			stats.added = Refill(gray, initial, {}, sx, sy, 0);
			stats.refillMs = Milliseconds(refillBegin);
			if (initial.size() < MinimumPoints) return Reject(MotionReason::InsufficientFeatures, stats);
			std::vector<Match> locations;
			std::vector<std::size_t> indices;
			for (const auto& t : initial)
			{
				indices.push_back(locations.size());
				locations.push_back({ 0, t.point, {}, 0, 0, Cell(t.point, gray.size()) });
			}
			if (!Coverage(locations, indices, gray.size(), stats)) return Reject(MotionReason::InsufficientCoverage, stats);
			reference = std::move(gray); tracks = std::move(initial);
			width = frame.image.width; height = frame.image.height; format = frame.image.format;
			scaleX = sx; scaleY = sy; referenceTime = frame.timestampSeconds;
			total = {}; sequence = frame.sequence; generation = 1; rejects = 0; state = MotionState::Tracking;
			stats.initialSurvivors = static_cast<int>(tracks.size());
			auto result = Result(stats);
			result.accepted = true; result.confidence = 1; result.reason = MotionReason::Initialized;
			return result;
		}

		MotionResult Process(const MotionFrame& frame)
		{
			MotionStatistics stats{};
			stats.inputSequence = frame.sequence;
			if (state != MotionState::Tracking)
			{
				auto result = Result(stats);
				result.reason = state == MotionState::Paused ? MotionReason::Paused : MotionReason::NotInitialized;
				return result;
			}
			const auto valid = Validate(frame);
			if (valid != MotionReason::None) return Reject(valid, stats, true);
			if (frame.image.width != width || frame.image.height != height || frame.image.format != format)
				return Reject(MotionReason::GeometryChanged, stats, true);
			if (frame.timestampSeconds < referenceTime) return Reject(MotionReason::InvalidTimestamp, stats, true);
			const auto begin = Clock::now();
			auto gray = Gray(frame.image);
			stats.preprocessMs = Milliseconds(begin);
			if (Black(gray)) return Reject(MotionReason::BlackFrame, stats, true);
			const auto flowBegin = Clock::now();
			std::vector<cv::Point2f> p, q, back;
			for (const auto& t : tracks) p.push_back(t.point);
			stats.attempted = static_cast<int>(p.size());
			std::array<bool, CellCount> referenceCells{};
			for (const auto point : p) referenceCells[Cell(point, gray.size())] = true;
			stats.referenceCells = static_cast<int>(std::count(referenceCells.begin(), referenceCells.end(), true));
			std::vector<unsigned char> forwardStatus, backwardStatus;
			std::vector<float> errors;
			const cv::TermCriteria criteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, 0.01);
			// 大屏半分辨率仍需覆盖较大位移；小图由OpenCV自动限制可用金字塔层级。
			cv::calcOpticalFlowPyrLK(reference, gray, p, q, forwardStatus, errors, cv::Size(21, 21), 5, criteria, 0, 1e-4);
			std::vector<cv::Point2f> refined;
			std::vector<std::size_t> sourceIndices;
			for (std::size_t i = 0; i < p.size(); ++i)
				if (forwardStatus[i] && Inside(q[i], gray.size()))
				{
					++stats.forward; refined.push_back(q[i]); sourceIndices.push_back(i);
				}
			if (refined.size() < MinimumPoints)
			{
				stats.opticalFlowMs = Milliseconds(flowBegin);
				return Reject(MotionReason::InsufficientFeatures, stats);
			}
			cv::cornerSubPix(gray, refined, cv::Size(3, 3), cv::Size(-1, -1),
				cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, 0.001));
			// 细化后重新跑反向光流，不能沿用细化前的 FB 作为同一点凭据。
			cv::calcOpticalFlowPyrLK(gray, reference, refined, back, backwardStatus, errors, cv::Size(21, 21), 5, criteria, 0, 1e-4);
			std::vector<Match> matches;
			std::array<int, CellCount> counts{};
			std::vector<double> fbErrors;
			for (std::size_t j = 0; j < refined.size(); ++j)
			{
				const auto i = sourceIndices[j];
				if (!backwardStatus[j] || !Finite(back[j])) continue;
				++stats.backward;
				const double fb = cv::norm(back[j] - p[i]);
				if (!Inside(refined[j], gray.size()) || cv::norm(refined[j] - q[i]) > 1.0 || fb > 1.0) continue;
				const int cell = Cell(p[i], gray.size());
				matches.push_back({ i, refined[j], refined[j] - p[i], fb, 0, cell });
				++counts[cell]; fbErrors.push_back(fb);
			}
			stats.opticalFlowMs = Milliseconds(flowBegin);
			stats.fbValid = static_cast<int>(matches.size());
			stats.fbMedian = Percentile(fbErrors, 0.5); stats.fbP95 = Percentile(fbErrors, 0.95);
			stats.survivalRatio = static_cast<double>(matches.size()) / tracks.size();
			if (matches.size() < MinimumPoints) return Reject(MotionReason::ForwardBackward, stats);
			stats.occupiedCells = static_cast<int>(std::count_if(counts.begin(), counts.end(), [](int n) { return n > 0; }));
			double allFitWeight = 0;
			// 稀疏格不足3点时不足整票；每格至多一票，拟合与出生锚点使用同一权重。
			for (auto& m : matches)
			{
				m.weight = 1.0 / std::max(counts[m.cell], 3);
				allFitWeight += m.weight;
			}
			const auto fitBegin = Clock::now();
			const auto rejectFit = [&](MotionReason reason)
			{
				stats.fitMs = Milliseconds(fitBegin);
				return Reject(reason, stats);
			};
			MotionVector best{};
			double bestWeight = -1;
			// 确定性穷举，每个参考格至多一票，稀疏格降权；不先对全部点取中位数。
			for (const auto& candidate : matches)
			{
				const MotionVector flow{ candidate.displacement.x, candidate.displacement.y };
				const double weight = Support(matches, flow);
				if (weight > bestWeight) { bestWeight = weight; best = flow; }
			}
			auto indices = Inliers(matches, best);
			best = MedianFlow(matches, indices);
			indices = Inliers(matches, best);
			bestWeight = Weight(matches, indices);
			stats.inliers = static_cast<int>(indices.size());
			stats.pointInlierRatio = static_cast<double>(indices.size()) / matches.size();
			stats.spatialInlierRatio = bestWeight / allFitWeight;
			std::vector<double> residuals;
			for (const auto i : indices) residuals.push_back(Length(matches[i].displacement.x - best.x, matches[i].displacement.y - best.y));
			stats.residualMedian = Percentile(residuals, 0.5); stats.residualP95 = Percentile(residuals, 0.95);
			if (NonTranslation(matches, gray.size(), best, stats)) return rejectFit(MotionReason::NonTranslation);
			for (const auto& candidate : matches)
			{
				const double separation = Length(candidate.displacement.x - best.x, candidate.displacement.y - best.y);
				if (separation <= ConsensusRadius) continue;
				const auto other = Inliers(matches, { candidate.displacement.x, candidate.displacement.y });
				const double otherRatio = OriginalCellWeight(matches, other, counts) / stats.occupiedCells;
				if (separation > 2 * ConsensusRadius)
					stats.runnerUpRatio = std::max(stats.runnerUpRatio, otherRatio);
				// 零主模式也检查已分离但不足双半径的次流，避免把固定多数误当可靠静止。
				if (Length(best.x, best.y) < 0.3 && otherRatio > 0.15)
				{
					std::array<bool, CellCount> movingCells{};
					for (const auto i : other) movingCells[matches[i].cell] = true;
					int interior = 0;
					for (int y = 1; y < Rows - 1; ++y) for (int x = 1; x < Columns - 1; ++x)
						interior += movingCells[y * Columns + x] ? 1 : 0;
					if (interior >= 6)
					{
						stats.runnerUpRatio = std::max(stats.runnerUpRatio, otherRatio);
						return rejectFit(MotionReason::AmbiguousMotion);
					}
				}
			}
			if (stats.runnerUpRatio > 0.35) return rejectFit(MotionReason::AmbiguousMotion);
			if (indices.size() < MinimumPoints || stats.spatialInlierRatio < MinimumSpatialRatio || stats.survivalRatio < 0.35)
				return rejectFit(MotionReason::InconsistentTranslation);
			if (!Coverage(matches, indices, gray.size(), stats)) return rejectFit(MotionReason::InsufficientCoverage);
			if (Length(best.x / gray.cols, best.y / gray.rows) > 0.25)
				return rejectFit(MotionReason::JumpTooLarge);
			std::vector<std::pair<double, double>> totalsX, totalsY;
			for (const auto i : indices)
			{
				const auto& m = matches[i];
				const auto& anchor = tracks[m.track].anchor;
				totalsX.emplace_back(m.point.x / scaleX - anchor.x, m.weight);
				totalsY.emplace_back(m.point.y / scaleY - anchor.y, m.weight);
			}
			const MotionVector newTotal{ WeightedMedian(std::move(totalsX)), WeightedMedian(std::move(totalsY)) };
			const MotionVector delta{ newTotal.x - total.x, newTotal.y - total.y };
			if (Length(delta.x * scaleX - best.x, delta.y * scaleY - best.y) > ConsensusRadius)
				return rejectFit(MotionReason::InconsistentTranslation);
			stats.fitMs = Milliseconds(fitBegin);
			std::vector<Track> next;
			std::array<int, CellCount> retainedPerCell{};
			for (const auto i : indices)
			{
				auto track = tracks[matches[i].track];
				track.point = matches[i].point; // 只更新局部位置，出生内容锚点终身不变。
				const int cell = Cell(track.point, gray.size());
				// 跨格后也保持总点数上限；原有顺序优先保留出生更早的锚点。
				if (retainedPerCell[cell] >= 6 + (cell < 32 ? 1 : 0)) continue;
				++retainedPerCell[cell];
				next.push_back(track);
				stats.initialSurvivors += track.birth == 0 ? 1 : 0;
			}
			stats.retained = static_cast<int>(next.size());
			stats.retired = static_cast<int>(tracks.size() - next.size());
			const auto refillBegin = Clock::now();
			stats.added = Refill(gray, next, newTotal, scaleX, scaleY, generation);
			stats.refillMs = Milliseconds(refillBegin);
			// 所有可能失败的图像/拟合/补点均在临时对象中完成，最后一次提交可信事务。
			reference = std::move(gray); tracks = std::move(next); total = newTotal;
			sequence = frame.sequence; referenceTime = frame.timestampSeconds; ++generation; rejects = 0;
			auto result = Result(stats);
			result.accepted = true; result.reason = MotionReason::None; result.delta = delta;
			result.confidence = std::clamp(stats.spatialInlierRatio * std::min(1.0, stats.survivalRatio / 0.8)
				* std::min(1.0, stats.inlierCells / 32.0) / (1.0 + stats.fbMedian + stats.residualMedian), 0.0, 1.0);
			return result;
		}
	};

	VisualMotionTracker::VisualMotionTracker() : impl_(std::make_unique<Impl>()) {}
	VisualMotionTracker::~VisualMotionTracker() = default;
	void VisualMotionTracker::Reset() noexcept { *impl_ = Impl{}; }
	MotionResult VisualMotionTracker::Initialize(const MotionFrame& frame) noexcept
	{
		Reset();
		try { return impl_->Initialize(frame); }
		catch (const cv::Exception&)
		{
			MotionStatistics stats{}; stats.inputSequence = frame.sequence;
			return impl_->Reject(MotionReason::OpenCvException, stats, true);
		}
		catch (...)
		{
			MotionStatistics stats{}; stats.inputSequence = frame.sequence;
			return impl_->Reject(MotionReason::UnexpectedException, stats, true);
		}
	}
	MotionResult VisualMotionTracker::Process(const MotionFrame& frame) noexcept
	{
		try { return impl_->Process(frame); }
		catch (const cv::Exception&)
		{
			MotionStatistics stats{}; stats.inputSequence = frame.sequence;
			return impl_->Reject(MotionReason::OpenCvException, stats, true);
		}
		catch (...)
		{
			MotionStatistics stats{}; stats.inputSequence = frame.sequence;
			return impl_->Reject(MotionReason::UnexpectedException, stats, true);
		}
	}
	const char* MotionReasonName(MotionReason reason) noexcept
	{
		switch (reason)
		{
#define INKEYS_MOTION_REASON(name) case MotionReason::name: return #name
			INKEYS_MOTION_REASON(None); INKEYS_MOTION_REASON(Initialized); INKEYS_MOTION_REASON(NotInitialized);
			INKEYS_MOTION_REASON(InvalidImage); INKEYS_MOTION_REASON(InvalidTimestamp); INKEYS_MOTION_REASON(GeometryChanged);
			INKEYS_MOTION_REASON(InvalidCapture); INKEYS_MOTION_REASON(BlackFrame); INKEYS_MOTION_REASON(InsufficientFeatures);
			INKEYS_MOTION_REASON(ForwardBackward); INKEYS_MOTION_REASON(InconsistentTranslation); INKEYS_MOTION_REASON(InsufficientCoverage);
			INKEYS_MOTION_REASON(NonTranslation); INKEYS_MOTION_REASON(AmbiguousMotion); INKEYS_MOTION_REASON(JumpTooLarge);
			INKEYS_MOTION_REASON(Paused); INKEYS_MOTION_REASON(OpenCvException); INKEYS_MOTION_REASON(UnexpectedException);
#undef INKEYS_MOTION_REASON
		}
		return "Unknown";
	}
}
