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
		constexpr double RefinementDistanceLimit = 1.0, FbErrorLimit = 1.0, NearStationaryLimit = 0.3;
		constexpr double ZeroModeRatioThreshold = 0.15;
		void Stage(TrackerDebugSnapshot* debug, TrackerDebugStage stage, bool complete = false) noexcept
		{
			if (!debug) return;
			const auto mask = std::uint64_t{ 1 } << static_cast<unsigned>(stage);
			debug->reachedStages |= mask;
			if (complete) debug->completedStages |= mask;
		}
		bool Check(TrackerDebugSnapshot* debug, TrackerDebugCheckId id, bool condition,
			double measured, TrackerDebugComparison comparison, double threshold) noexcept
		{
			if (debug) debug->checks[static_cast<std::size_t>(id)] = { true, condition, measured, threshold, comparison };
			return condition;
		}
		bool RejectWhen(TrackerDebugSnapshot* debug, TrackerDebugCheckId id, bool condition,
			double measured, TrackerDebugComparison comparison, double threshold) noexcept
		{
			if (Check(debug, id, condition, measured, comparison, threshold) && debug) debug->firstFailure = id;
			return condition;
		}
		bool Require(TrackerDebugSnapshot* debug, TrackerDebugCheckId id, bool condition,
			double measured, TrackerDebugComparison comparison, double threshold) noexcept
		{
			if (!Check(debug, id, condition, measured, comparison, threshold) && debug) debug->firstFailure = id;
			return condition;
		}
		using Comparison = TrackerDebugComparison;
		using CheckId = TrackerDebugCheckId;
		void Extend(TrackerDebugBounds& bounds, cv::Point2f point) noexcept
		{
			if (!bounds.valid)
			{
				bounds = { true, point.x, point.y, point.x, point.y };
				return;
			}
			bounds.minX = std::min(bounds.minX, static_cast<double>(point.x));
			bounds.minY = std::min(bounds.minY, static_cast<double>(point.y));
			bounds.maxX = std::max(bounds.maxX, static_cast<double>(point.x));
			bounds.maxY = std::max(bounds.maxY, static_cast<double>(point.y));
		}
		MotionVector Position(cv::Point2f point) noexcept { return { point.x, point.y }; }
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
		void RecordTracks(TrackerDebugSnapshot* debug, const std::vector<Track>& tracks, cv::Size size) noexcept
		{
			if (!debug) return;
			debug->pointCount = std::min(tracks.size(), debug->points.size());
			debug->truncated = tracks.size() > debug->pointCount;
			for (std::size_t i = 0; i < debug->pointCount; ++i)
			{
				auto& point = debug->points[i]; const auto& track = tracks[i];
				point.reference = Position(track.point); point.anchor = track.anchor; point.birthGeneration = track.birth;
				const int x = std::clamp(static_cast<int>(track.point.x * Columns / size.width), 0, Columns - 1);
				const int y = std::clamp(static_cast<int>(track.point.y * Rows / size.height), 0, Rows - 1);
				point.referenceCell = y * Columns + x;
				++debug->cells[point.referenceCell].reference;
			}
		}
		void Candidate(TrackerDebugCandidate& output, const std::vector<Match>& matches,
			const std::vector<std::size_t>& indices, const std::vector<Track>& tracks, MotionVector flow) noexcept
		{
			output = {}; output.valid = true; output.flow = flow; output.points = static_cast<int>(indices.size());
			std::array<bool, CellCount> cells{};
			for (const auto i : indices)
			{
				const auto& match = matches[i]; cells[match.cell] = true;
				output.fitWeight += match.weight;
				Extend(output.referenceBounds, tracks[match.track].point); Extend(output.currentBounds, match.point);
			}
			output.cells = static_cast<int>(std::count(cells.begin(), cells.end(), true));
			for (int y = 1; y < Rows - 1; ++y) for (int x = 1; x < Columns - 1; ++x)
				output.interiorCells += cells[y * Columns + x] ? 1 : 0;
		}
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
			cv::Size size, MotionStatistics& stats, TrackerDebugSnapshot* debug)
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
			// 逐项沿用原 && 短路：后续未执行项不能被解释成失败。
			const bool covered = Require(debug, CheckId::CoverageCells, stats.inlierCells >= 16, stats.inlierCells, Comparison::GreaterEqual, 16)
				&& Require(debug, CheckId::CoverageInterior, stats.interiorCells >= 8, stats.interiorCells, Comparison::GreaterEqual, 8)
				&& [&] {
					const auto count = std::count(columns.begin(), columns.end(), true);
					return Require(debug, CheckId::CoverageColumns, count >= 4, static_cast<double>(count), Comparison::GreaterEqual, 4);
				}()
				&& [&] {
					const auto count = std::count(rows.begin(), rows.end(), true);
					return Require(debug, CheckId::CoverageRows, count >= 3, static_cast<double>(count), Comparison::GreaterEqual, 3);
				}()
				&& Require(debug, CheckId::CoverageX, stats.coverageX >= 0.5, stats.coverageX, Comparison::GreaterEqual, 0.5)
				&& Require(debug, CheckId::CoverageY, stats.coverageY >= 0.4, stats.coverageY, Comparison::GreaterEqual, 0.4);
			Stage(debug, TrackerDebugStage::Coverage, true);
			return covered;
		}
		MotionReason Validate(const MotionFrame& frame, TrackerDebugSnapshot* debug)
		{
			Stage(debug, TrackerDebugStage::Validation);
			if (RejectWhen(debug, CheckId::CaptureInvalid, !frame.captureValid, frame.captureValid, Comparison::Equal, 0)) return MotionReason::InvalidCapture;
			const bool timestampNonFinite = !std::isfinite(frame.timestampSeconds);
			if (RejectWhen(debug, CheckId::TimestampNonFinite, timestampNonFinite, timestampNonFinite, Comparison::Equal, 1)) return MotionReason::InvalidTimestamp;
			const auto& image = frame.image;
			if (RejectWhen(debug, CheckId::ImageDataMissing, !image.data, image.data != nullptr, Comparison::Equal, 0)
				|| RejectWhen(debug, CheckId::WidthTooSmall, image.width < 160, image.width, Comparison::Less, 160)
				|| RejectWhen(debug, CheckId::HeightTooSmall, image.height < 120, image.height, Comparison::Less, 120)
				|| RejectWhen(debug, CheckId::WidthTooLarge, image.width > 16384, image.width, Comparison::Greater, 16384)
				|| RejectWhen(debug, CheckId::HeightTooLarge, image.height > 16384, image.height, Comparison::Greater, 16384))
				return MotionReason::InvalidImage;
			std::size_t bytesPerPixel = 0;
			switch (image.format)
			{
			case MotionPixelFormat::Gray8: bytesPerPixel = 1; break;
			case MotionPixelFormat::Bgra32: bytesPerPixel = 4; break;
			default:
				RejectWhen(debug, CheckId::PixelFormatInvalid, true, 1, Comparison::Equal, 1);
				return MotionReason::InvalidImage;
			}
			Check(debug, CheckId::PixelFormatInvalid, false, 0, Comparison::Equal, 1);
			const auto rowBytes = static_cast<std::size_t>(image.width) * bytesPerPixel;
			const auto rowsBeforeLast = static_cast<std::size_t>(image.height - 1);
			// 先检查布局和乘加溢出，之后才允许 OpenCV 接触借用数据。
			if (RejectWhen(debug, CheckId::StrideTooSmall, image.strideBytes < rowBytes, static_cast<double>(image.strideBytes), Comparison::Less, static_cast<double>(rowBytes))
				|| RejectWhen(debug, CheckId::LayoutOverflow, image.strideBytes > (std::numeric_limits<std::size_t>::max() - rowBytes) / rowsBeforeLast,
					static_cast<double>(image.strideBytes), Comparison::Greater, static_cast<double>((std::numeric_limits<std::size_t>::max() - rowBytes) / rowsBeforeLast))
				|| RejectWhen(debug, CheckId::BufferTooSmall, rowsBeforeLast * image.strideBytes + rowBytes > image.sizeBytes,
					static_cast<double>(rowsBeforeLast * image.strideBytes + rowBytes), Comparison::Greater, static_cast<double>(image.sizeBytes)))
				return MotionReason::InvalidImage;
			Stage(debug, TrackerDebugStage::Validation, true);
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
		void RecordGeometry(TrackerDebugSnapshot* debug, const cv::Mat& gray, const MotionFrame& frame) noexcept
		{
			if (!debug) return;
			debug->processingWidth = gray.cols; debug->processingHeight = gray.rows;
			debug->scaleX = static_cast<double>(gray.cols) / frame.image.width;
			debug->scaleY = static_cast<double>(gray.rows) / frame.image.height;
		}
		bool Black(const cv::Mat& gray, TrackerDebugSnapshot* debug)
		{
			double maximum = 0;
			cv::minMaxLoc(gray, nullptr, &maximum);
			return RejectWhen(debug, CheckId::BlackMaximum, maximum <= 2, maximum, Comparison::LessEqual, 2); // 仅近黑立即关门。
		}
		int Refill(const cv::Mat& gray, std::vector<Track>& tracks, MotionVector total,
			double scaleX, double scaleY, std::uint64_t birth, TrackerDebugSnapshot* debug)
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
					if (debug)
					{
						if (debug->addedPointCount < debug->addedPoints.size())
							debug->addedPoints[debug->addedPointCount++] = { Position(p), tracks.back().anchor, birth, cell, false };
						else debug->truncated = true;
						++debug->cells[cell].added;
					}
					++counts[cell];
				}
			}
			return static_cast<int>(tracks.size() - oldCount);
		}
		// 在平移裁剪前检查全位移场，仿射仅用于拒绝，不参与输出变换。
		bool NonTranslation(const std::vector<Match>& matches, cv::Size size, MotionVector translation,
			MotionStatistics& stats, TrackerDebugSnapshot* debug)
		{
			Stage(debug, TrackerDebugStage::NonTranslation);
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
					if (debug)
					{
						debug->cells[cell].affineSampleValid = true;
						debug->cells[cell].affinePosition = { (coordinates.back()[1] + 0.5) * size.width, (coordinates.back()[2] + 0.5) * size.height };
						debug->cells[cell].affineFlow = { flows.back()[0], flows.back()[1] };
					}
				}
			if (Check(debug, CheckId::AffineCellsLow, coordinates.size() < 16, static_cast<double>(coordinates.size()), Comparison::Less, 16))
			{
				Stage(debug, TrackerDebugStage::NonTranslation, true);
				return false;
			}
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
				const bool solved = cv::solve(lhs, rhs, model, cv::DECOMP_SVD);
				if (Check(debug, CheckId::AffineSolveFailed, !solved, solved, Comparison::Equal, 0))
				{
					Stage(debug, TrackerDebugStage::NonTranslation, true);
					return false;
				}
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
			if (debug) debug->affineModelValid = true;
			// 仿射拒绝是三项联合条件，未被短路到的项不补算。
			const bool nonTranslation = Check(debug, CheckId::AffineVariationHigh, stats.affineVariation > 1.0, stats.affineVariation, Comparison::Greater, 1.0)
				&& [&] {
					const double threshold = 0.55 * Percentile(translationResiduals, 0.5);
					return Check(debug, CheckId::AffineResidualLow, stats.affineResidual < threshold, stats.affineResidual, Comparison::Less, threshold);
				}()
				&& Check(debug, CheckId::AffineSupportHigh, static_cast<double>(supported) / coordinates.size() >= MinimumSpatialRatio,
					static_cast<double>(supported) / coordinates.size(), Comparison::GreaterEqual, MinimumSpatialRatio);
			Stage(debug, TrackerDebugStage::NonTranslation, true);
			return nonTranslation;
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
		void BeginDebug(const MotionFrame& frame, TrackerDebugSnapshot* debug, bool initialization) const noexcept
		{
			if (!debug) return;
			// 直接原位清空调用方存储，关闭诊断时栈上不创建大型快照。
			std::destroy_at(debug); std::construct_at(debug);
			debug->initialization = initialization; debug->inputSequence = frame.sequence;
			debug->referenceSequenceBefore = sequence; debug->referenceGenerationBefore = generation;
			debug->referenceBeforeValid = !reference.empty(); debug->totalBefore = total;
			debug->referenceProcessingWidth = reference.cols; debug->referenceProcessingHeight = reference.rows;
			debug->inputWidth = frame.image.width; debug->inputHeight = frame.image.height;
			debug->inputStrideBytes = frame.image.strideBytes; debug->inputSizeBytes = frame.image.sizeBytes;
			debug->inputFormat = frame.image.format; debug->timestampSeconds = frame.timestampSeconds; debug->captureValid = frame.captureValid;
			debug->pointBorder = Border; debug->consensusRadius = ConsensusRadius; debug->refinementDistanceLimit = RefinementDistanceLimit;
			debug->fbErrorLimit = FbErrorLimit; debug->nearStationaryLimit = NearStationaryLimit;
			debug->zeroModeRatioThreshold = ZeroModeRatioThreshold;
			if (!reference.empty()) RecordTracks(debug, tracks, reference.size());
		}
		MotionResult EndDebug(MotionResult result, TrackerDebugSnapshot* debug) const noexcept
		{
			if (debug)
			{
				debug->result = result; debug->referenceSequenceAfter = sequence; debug->referenceGenerationAfter = generation;
				debug->referenceAfterValid = !reference.empty();
				if (result.accepted)
					for (std::size_t i = 0; i < debug->addedPointCount; ++i) debug->addedPoints[i].committed = true;
			}
			return result;
		}
		MotionResult Initialize(const MotionFrame& frame, TrackerDebugSnapshot* debug)
		{
			MotionStatistics stats{};
			stats.inputSequence = frame.sequence;
			const auto valid = Validate(frame, debug);
			if (valid != MotionReason::None) return Reject(valid, stats, true);
			Stage(debug, TrackerDebugStage::Preprocess);
			const auto begin = Clock::now();
			auto gray = Gray(frame.image);
			stats.preprocessMs = Milliseconds(begin);
			RecordGeometry(debug, gray, frame); Stage(debug, TrackerDebugStage::Preprocess, true);
			if (Black(gray, debug)) return Reject(MotionReason::BlackFrame, stats, true);
			const double sx = static_cast<double>(gray.cols) / frame.image.width;
			const double sy = static_cast<double>(gray.rows) / frame.image.height;
			std::vector<Track> initial;
			const auto refillBegin = Clock::now();
			Stage(debug, TrackerDebugStage::Refill);
			stats.added = Refill(gray, initial, {}, sx, sy, 0, debug);
			stats.refillMs = Milliseconds(refillBegin); Stage(debug, TrackerDebugStage::Refill, true);
			if (RejectWhen(debug, CheckId::InitialPointsLow, initial.size() < MinimumPoints, static_cast<double>(initial.size()), Comparison::Less, MinimumPoints)) return Reject(MotionReason::InsufficientFeatures, stats);
			std::vector<Match> locations;
			std::vector<std::size_t> indices;
			for (const auto& t : initial)
			{
				indices.push_back(locations.size());
				locations.push_back({ 0, t.point, {}, 0, 0, Cell(t.point, gray.size()) });
			}
			Stage(debug, TrackerDebugStage::Coverage);
			if (!Coverage(locations, indices, gray.size(), stats, debug)) return Reject(MotionReason::InsufficientCoverage, stats);
			Stage(debug, TrackerDebugStage::Commit);
			reference = std::move(gray); tracks = std::move(initial);
			width = frame.image.width; height = frame.image.height; format = frame.image.format;
			scaleX = sx; scaleY = sy; referenceTime = frame.timestampSeconds;
			total = {}; sequence = frame.sequence; generation = 1; rejects = 0; state = MotionState::Tracking;
			stats.initialSurvivors = static_cast<int>(tracks.size());
			auto result = Result(stats);
			result.accepted = true; result.confidence = 1; result.reason = MotionReason::Initialized;
			Stage(debug, TrackerDebugStage::Commit, true);
			return result;
		}

		MotionResult Process(const MotionFrame& frame, TrackerDebugSnapshot* debug)
		{
			MotionStatistics stats{};
			stats.inputSequence = frame.sequence;
			if (RejectWhen(debug, CheckId::TrackingState, state != MotionState::Tracking, static_cast<int>(state), Comparison::NotEqual, static_cast<int>(MotionState::Tracking)))
			{
				auto result = Result(stats);
				result.reason = state == MotionState::Paused ? MotionReason::Paused : MotionReason::NotInitialized;
				return result;
			}
			const auto valid = Validate(frame, debug);
			if (valid != MotionReason::None) return Reject(valid, stats, true);
			if (RejectWhen(debug, CheckId::WidthChanged, frame.image.width != width, frame.image.width, Comparison::NotEqual, width)
				|| RejectWhen(debug, CheckId::HeightChanged, frame.image.height != height, frame.image.height, Comparison::NotEqual, height)
				|| RejectWhen(debug, CheckId::FormatChanged, frame.image.format != format, static_cast<int>(frame.image.format), Comparison::NotEqual, static_cast<int>(format)))
				return Reject(MotionReason::GeometryChanged, stats, true);
			if (RejectWhen(debug, CheckId::TimestampReversed, frame.timestampSeconds < referenceTime, frame.timestampSeconds, Comparison::Less, referenceTime)) return Reject(MotionReason::InvalidTimestamp, stats, true);
			Stage(debug, TrackerDebugStage::Preprocess);
			const auto begin = Clock::now();
			auto gray = Gray(frame.image);
			stats.preprocessMs = Milliseconds(begin);
			RecordGeometry(debug, gray, frame); Stage(debug, TrackerDebugStage::Preprocess, true);
			if (Black(gray, debug)) return Reject(MotionReason::BlackFrame, stats, true);
			Stage(debug, TrackerDebugStage::Forward);
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
			{
				auto* point = debug && i < debug->pointCount ? &debug->points[i] : nullptr;
				if (point) { point->forwardComputed = true; point->forward = Position(q[i]); point->forwardStatus = forwardStatus[i] != 0; }
				if (forwardStatus[i] && [&] {
					const bool inside = Inside(q[i], gray.size());
					if (point) { point->forwardInsideEvaluated = true; point->forwardInside = inside; }
					return inside;
				}())
				{
					++stats.forward; refined.push_back(q[i]); sourceIndices.push_back(i);
					if (point) ++debug->cells[point->referenceCell].forward;
				}
			}
			Stage(debug, TrackerDebugStage::Forward, true);
			if (RejectWhen(debug, CheckId::ForwardPointsLow, refined.size() < MinimumPoints, static_cast<double>(refined.size()), Comparison::Less, MinimumPoints))
			{
				stats.opticalFlowMs = Milliseconds(flowBegin);
				return Reject(MotionReason::InsufficientFeatures, stats);
			}
			Stage(debug, TrackerDebugStage::Refinement);
			cv::cornerSubPix(gray, refined, cv::Size(3, 3), cv::Size(-1, -1),
				cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, 0.001));
			if (debug) for (std::size_t j = 0; j < refined.size(); ++j)
				if (sourceIndices[j] < debug->pointCount)
				{
					auto& point = debug->points[sourceIndices[j]]; point.refinedComputed = true; point.refined = Position(refined[j]);
				}
			Stage(debug, TrackerDebugStage::Refinement, true); Stage(debug, TrackerDebugStage::Backward);
			// 细化后重新跑反向光流，不能沿用细化前的 FB 作为同一点凭据。
			cv::calcOpticalFlowPyrLK(gray, reference, refined, back, backwardStatus, errors, cv::Size(21, 21), 5, criteria, 0, 1e-4);
			std::vector<Match> matches;
			std::array<int, CellCount> counts{};
			std::vector<double> fbErrors;
			for (std::size_t j = 0; j < refined.size(); ++j)
			{
				const auto i = sourceIndices[j];
				auto* point = debug && i < debug->pointCount ? &debug->points[i] : nullptr;
				if (point) { point->backwardComputed = true; point->backward = Position(back[j]); point->backwardStatus = backwardStatus[j] != 0; }
				if (!backwardStatus[j] || ![&] {
					const bool finite = Finite(back[j]);
					if (point) { point->backwardFiniteEvaluated = true; point->backwardFinite = finite; }
					return finite;
				}()) continue;
				++stats.backward;
				const double fb = cv::norm(back[j] - p[i]);
				if (point) { point->fbComputed = true; point->fbError = fb; }
				if (![&] {
					const bool inside = Inside(refined[j], gray.size());
					if (point) { point->refinedInsideEvaluated = true; point->refinedInside = inside; }
					return inside;
				}() || [&] {
					const double distance = cv::norm(refined[j] - q[i]);
					if (point) { point->refinementDistanceEvaluated = true; point->refinementDistance = distance; }
					return distance > RefinementDistanceLimit;
				}() || [&] { if (point) point->fbThresholdEvaluated = true; return fb > FbErrorLimit; }()) continue;
				const int cell = Cell(p[i], gray.size());
				matches.push_back({ i, refined[j], refined[j] - p[i], fb, 0, cell });
				++counts[cell]; fbErrors.push_back(fb);
				if (point)
				{
					point->fbValid = true; point->currentCell = Cell(refined[j], gray.size()); ++debug->cells[cell].fbValid;
					point->nearStationary = Length(refined[j].x - p[i].x, refined[j].y - p[i].y) < NearStationaryLimit;
					if (point->nearStationary) ++debug->cells[cell].nearStationary;
				}
			}
			stats.opticalFlowMs = Milliseconds(flowBegin); Stage(debug, TrackerDebugStage::Backward, true);
			stats.fbValid = static_cast<int>(matches.size());
			stats.fbMedian = Percentile(fbErrors, 0.5); stats.fbP95 = Percentile(fbErrors, 0.95);
			stats.survivalRatio = static_cast<double>(matches.size()) / tracks.size();
			if (RejectWhen(debug, CheckId::FbPointsLow, matches.size() < MinimumPoints, static_cast<double>(matches.size()), Comparison::Less, MinimumPoints)) return Reject(MotionReason::ForwardBackward, stats);
			stats.occupiedCells = static_cast<int>(std::count_if(counts.begin(), counts.end(), [](int n) { return n > 0; }));
			double allFitWeight = 0;
			// 稀疏格不足3点时不足整票；每格至多一票，拟合与出生锚点使用同一权重。
			for (auto& m : matches)
			{
				m.weight = 1.0 / std::max(counts[m.cell], 3);
				allFitWeight += m.weight;
				if (debug)
				{
					debug->cells[m.cell].fitWeight += m.weight; debug->cells[m.cell].originalCellWeight += 1.0 / counts[m.cell];
					if (m.track < debug->pointCount) debug->points[m.track].fitWeight = m.weight;
				}
			}
			Stage(debug, TrackerDebugStage::Consensus);
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
				if (debug) ++debug->consensusCandidatesEvaluated;
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
			for (const auto i : indices)
			{
				residuals.push_back(Length(matches[i].displacement.x - best.x, matches[i].displacement.y - best.y));
				if (debug && matches[i].track < debug->pointCount)
				{
					auto& point = debug->points[matches[i].track]; point.residualComputed = true; point.residual = residuals.back();
				}
			}
			stats.residualMedian = Percentile(residuals, 0.5); stats.residualP95 = Percentile(residuals, 0.95);
			if (debug)
			{
				Candidate(debug->mainMotion, matches, indices, tracks, best);
				for (const auto& match : matches) if (match.track < debug->pointCount) debug->points[match.track].consensusEvaluated = true;
				for (const auto i : indices)
				{
					++debug->cells[matches[i].cell].inliers; debug->cells[matches[i].cell].inlierWeight += matches[i].weight;
					if (matches[i].track < debug->pointCount) debug->points[matches[i].track].inlier = true;
				}
			}
			Stage(debug, TrackerDebugStage::Consensus, true);
			const bool nonTranslation = NonTranslation(matches, gray.size(), best, stats, debug);
			if (RejectWhen(debug, CheckId::NonTranslation, nonTranslation, nonTranslation, Comparison::Equal, 1)) return rejectFit(MotionReason::NonTranslation);
			Stage(debug, TrackerDebugStage::Ambiguity);
			for (const auto& candidate : matches)
			{
				const double separation = Length(candidate.displacement.x - best.x, candidate.displacement.y - best.y);
				if (separation <= ConsensusRadius) continue;
				const auto other = Inliers(matches, { candidate.displacement.x, candidate.displacement.y });
				const double otherRatio = OriginalCellWeight(matches, other, counts) / stats.occupiedCells;
				TrackerDebugCandidate* observed = nullptr;
				TrackerDebugCandidate* eligible = nullptr;
				if (debug)
				{
					++debug->competitorsEvaluated;
					if (!debug->strongestCompetitor.valid || otherRatio > debug->strongestCompetitor.originalCellRatio)
					{
						observed = &debug->strongestCompetitor;
						Candidate(*observed, matches, other, tracks, { candidate.displacement.x, candidate.displacement.y });
						observed->originalCellRatio = otherRatio; observed->separation = separation;
						for (std::size_t i = 0; i < debug->pointCount; ++i) debug->points[i].competitor = false;
						for (auto& cell : debug->cells) cell.competitor = 0;
						for (const auto i : other)
						{
							++debug->cells[matches[i].cell].competitor;
							if (matches[i].track < debug->pointCount) debug->points[matches[i].track].competitor = true;
						}
					}
				}
				if ([&] {
					const bool beyond = separation > 2 * ConsensusRadius;
					if (observed) { observed->doubleRadiusEvaluated = true; observed->beyondDoubleRadius = beyond; }
					return beyond;
				}())
				{
					// 普通次流只接纳 > 双半径的候选，不能用更强但不够分离的候选冒充拒绝来源。
					if (debug && otherRatio > stats.runnerUpRatio)
					{
						eligible = &debug->eligibleRunnerUp;
						Candidate(*eligible, matches, other, tracks, { candidate.displacement.x, candidate.displacement.y });
						eligible->originalCellRatio = otherRatio; eligible->separation = separation;
						eligible->doubleRadiusEvaluated = eligible->beyondDoubleRadius = true;
					}
					stats.runnerUpRatio = std::max(stats.runnerUpRatio, otherRatio);
				}
				// 零主模式也检查已分离但不足双半径的次流，避免把固定多数误当可靠静止。
				if ([&] {
					const bool zero = Length(best.x, best.y) < NearStationaryLimit;
					if (observed) { observed->zeroLengthEvaluated = true; observed->zeroLength = zero; }
					if (eligible) { eligible->zeroLengthEvaluated = true; eligible->zeroLength = zero; }
					return zero;
				}() && [&] {
					const bool ratio = otherRatio > ZeroModeRatioThreshold;
					if (observed) { observed->zeroRatioEvaluated = true; observed->zeroRatio = ratio; }
					if (eligible) { eligible->zeroRatioEvaluated = true; eligible->zeroRatio = ratio; }
					return ratio;
				}())
				{
					std::array<bool, CellCount> movingCells{};
					for (const auto i : other) movingCells[matches[i].cell] = true;
					int interior = 0;
					for (int y = 1; y < Rows - 1; ++y) for (int x = 1; x < Columns - 1; ++x)
						interior += movingCells[y * Columns + x] ? 1 : 0;
					if (observed) { observed->interiorEvaluated = true; observed->enoughInterior = interior >= 6; }
					if (eligible) { eligible->interiorEvaluated = true; eligible->enoughInterior = interior >= 6; }
					if (RejectWhen(debug, CheckId::ZeroModeAmbiguity, interior >= 6, interior, Comparison::GreaterEqual, 6))
					{
						stats.runnerUpRatio = std::max(stats.runnerUpRatio, otherRatio);
						if (debug)
						{
							auto& rejecting = debug->rejectingCompetitor;
							Candidate(rejecting, matches, other, tracks, { candidate.displacement.x, candidate.displacement.y });
							rejecting.originalCellRatio = otherRatio; rejecting.separation = separation;
							rejecting.doubleRadiusEvaluated = true; rejecting.beyondDoubleRadius = separation > 2 * ConsensusRadius;
							rejecting.zeroLengthEvaluated = rejecting.zeroLength = rejecting.zeroRatioEvaluated = rejecting.zeroRatio = true;
							rejecting.interiorEvaluated = rejecting.enoughInterior = true;
						}
						return rejectFit(MotionReason::AmbiguousMotion);
					}
				}
			}
			if (debug) debug->ambiguityComplete = true;
			Stage(debug, TrackerDebugStage::Ambiguity, true);
			if (RejectWhen(debug, CheckId::RunnerUpHigh, stats.runnerUpRatio > 0.35, stats.runnerUpRatio, Comparison::Greater, 0.35))
			{
				if (debug) debug->rejectingCompetitor = debug->eligibleRunnerUp;
				return rejectFit(MotionReason::AmbiguousMotion);
			}
			Stage(debug, TrackerDebugStage::Consistency);
			if (RejectWhen(debug, CheckId::InliersLow, indices.size() < MinimumPoints, static_cast<double>(indices.size()), Comparison::Less, MinimumPoints)
				|| RejectWhen(debug, CheckId::SpatialRatioLow, stats.spatialInlierRatio < MinimumSpatialRatio, stats.spatialInlierRatio, Comparison::Less, MinimumSpatialRatio)
				|| RejectWhen(debug, CheckId::SurvivalRatioLow, stats.survivalRatio < 0.35, stats.survivalRatio, Comparison::Less, 0.35))
				return rejectFit(MotionReason::InconsistentTranslation);
			Stage(debug, TrackerDebugStage::Consistency, true); Stage(debug, TrackerDebugStage::Coverage);
			if (!Coverage(matches, indices, gray.size(), stats, debug)) return rejectFit(MotionReason::InsufficientCoverage);
			Stage(debug, TrackerDebugStage::Jump);
			const double normalizedJump = Length(best.x / gray.cols, best.y / gray.rows);
			if (RejectWhen(debug, CheckId::JumpHigh, normalizedJump > 0.25, normalizedJump, Comparison::Greater, 0.25))
				return rejectFit(MotionReason::JumpTooLarge);
			Stage(debug, TrackerDebugStage::Jump, true); Stage(debug, TrackerDebugStage::Anchor);
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
			if (debug) { debug->candidateTotalValid = true; debug->candidateTotal = newTotal; debug->candidateDelta = delta; }
			const double disagreement = Length(delta.x * scaleX - best.x, delta.y * scaleY - best.y);
			if (RejectWhen(debug, CheckId::AnchorDisagreement, disagreement > ConsensusRadius, disagreement, Comparison::Greater, ConsensusRadius))
				return rejectFit(MotionReason::InconsistentTranslation);
			Stage(debug, TrackerDebugStage::Anchor, true);
			stats.fitMs = Milliseconds(fitBegin);
			Stage(debug, TrackerDebugStage::Retention);
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
				if (debug)
				{
					++debug->cells[cell].retained;
					if (matches[i].track < debug->pointCount) debug->points[matches[i].track].retained = true;
				}
				stats.initialSurvivors += track.birth == 0 ? 1 : 0;
			}
			if (debug) for (std::size_t i = 0; i < debug->pointCount; ++i)
			{
				auto& point = debug->points[i]; point.retentionEvaluated = true; point.retired = !point.retained;
			}
			Stage(debug, TrackerDebugStage::Retention, true);
			stats.retained = static_cast<int>(next.size());
			stats.retired = static_cast<int>(tracks.size() - next.size());
			const auto refillBegin = Clock::now();
			Stage(debug, TrackerDebugStage::Refill);
			stats.added = Refill(gray, next, newTotal, scaleX, scaleY, generation, debug);
			stats.refillMs = Milliseconds(refillBegin); Stage(debug, TrackerDebugStage::Refill, true);
			// 所有可能失败的图像/拟合/补点均在临时对象中完成，最后一次提交可信事务。
			Stage(debug, TrackerDebugStage::Commit);
			reference = std::move(gray); tracks = std::move(next); total = newTotal;
			sequence = frame.sequence; referenceTime = frame.timestampSeconds; ++generation; rejects = 0;
			auto result = Result(stats);
			result.accepted = true; result.reason = MotionReason::None; result.delta = delta;
			Stage(debug, TrackerDebugStage::Commit, true);
			result.confidence = std::clamp(stats.spatialInlierRatio * std::min(1.0, stats.survivalRatio / 0.8)
				* std::min(1.0, stats.inlierCells / 32.0) / (1.0 + stats.fbMedian + stats.residualMedian), 0.0, 1.0);
			return result;
		}
	};

	VisualMotionTracker::VisualMotionTracker() : impl_(std::make_unique<Impl>()) {}
	VisualMotionTracker::~VisualMotionTracker() = default;
	void VisualMotionTracker::Reset() noexcept { *impl_ = Impl{}; }
	MotionResult VisualMotionTracker::Initialize(const MotionFrame& frame, TrackerDebugSnapshot* debug) noexcept
	{
		Reset(); impl_->BeginDebug(frame, debug, true);
		try { return impl_->EndDebug(impl_->Initialize(frame, debug), debug); }
		catch (const cv::Exception&)
		{
			MotionStatistics stats{}; stats.inputSequence = frame.sequence;
			if (debug) debug->exception = MotionReason::OpenCvException;
			return impl_->EndDebug(impl_->Reject(MotionReason::OpenCvException, stats, true), debug);
		}
		catch (...)
		{
			MotionStatistics stats{}; stats.inputSequence = frame.sequence;
			if (debug) debug->exception = MotionReason::UnexpectedException;
			return impl_->EndDebug(impl_->Reject(MotionReason::UnexpectedException, stats, true), debug);
		}
	}
	MotionResult VisualMotionTracker::Process(const MotionFrame& frame, TrackerDebugSnapshot* debug) noexcept
	{
		impl_->BeginDebug(frame, debug, false);
		try { return impl_->EndDebug(impl_->Process(frame, debug), debug); }
		catch (const cv::Exception&)
		{
			MotionStatistics stats{}; stats.inputSequence = frame.sequence;
			if (debug) debug->exception = MotionReason::OpenCvException;
			return impl_->EndDebug(impl_->Reject(MotionReason::OpenCvException, stats, true), debug);
		}
		catch (...)
		{
			MotionStatistics stats{}; stats.inputSequence = frame.sequence;
			if (debug) debug->exception = MotionReason::UnexpectedException;
			return impl_->EndDebug(impl_->Reject(MotionReason::UnexpectedException, stats, true), debug);
		}
	}
	const char* TrackerDebugStageName(TrackerDebugStage stage) noexcept
	{
		switch (stage)
		{
#define INKEYS_DEBUG_STAGE(name) case TrackerDebugStage::name: return #name
			INKEYS_DEBUG_STAGE(Validation); INKEYS_DEBUG_STAGE(Preprocess); INKEYS_DEBUG_STAGE(Forward);
			INKEYS_DEBUG_STAGE(Refinement); INKEYS_DEBUG_STAGE(Backward); INKEYS_DEBUG_STAGE(Consensus);
			INKEYS_DEBUG_STAGE(NonTranslation); INKEYS_DEBUG_STAGE(Ambiguity); INKEYS_DEBUG_STAGE(Consistency);
			INKEYS_DEBUG_STAGE(Coverage); INKEYS_DEBUG_STAGE(Jump); INKEYS_DEBUG_STAGE(Anchor);
			INKEYS_DEBUG_STAGE(Retention); INKEYS_DEBUG_STAGE(Refill); INKEYS_DEBUG_STAGE(Commit);
#undef INKEYS_DEBUG_STAGE
		default: return "Not executed";
		}
	}
	const char* TrackerDebugCheckName(TrackerDebugCheckId id) noexcept
	{
		switch (id)
		{
#define INKEYS_DEBUG_CHECK(name, text) case TrackerDebugCheckId::name: return text
			INKEYS_DEBUG_CHECK(TrackingState, "state != Tracking"); INKEYS_DEBUG_CHECK(CaptureInvalid, "captureValid == false");
			INKEYS_DEBUG_CHECK(TimestampNonFinite, "timestamp is non-finite"); INKEYS_DEBUG_CHECK(ImageDataMissing, "image.data == null");
			INKEYS_DEBUG_CHECK(WidthTooSmall, "width < 160"); INKEYS_DEBUG_CHECK(HeightTooSmall, "height < 120");
			INKEYS_DEBUG_CHECK(WidthTooLarge, "width > 16384"); INKEYS_DEBUG_CHECK(HeightTooLarge, "height > 16384");
			INKEYS_DEBUG_CHECK(PixelFormatInvalid, "unsupported pixel format"); INKEYS_DEBUG_CHECK(StrideTooSmall, "stride < row bytes");
			INKEYS_DEBUG_CHECK(LayoutOverflow, "stride exceeds safe layout bound"); INKEYS_DEBUG_CHECK(BufferTooSmall, "required bytes > buffer size");
			INKEYS_DEBUG_CHECK(WidthChanged, "width != reference width"); INKEYS_DEBUG_CHECK(HeightChanged, "height != reference height");
			INKEYS_DEBUG_CHECK(FormatChanged, "format != reference format"); INKEYS_DEBUG_CHECK(TimestampReversed, "timestamp < reference time");
			INKEYS_DEBUG_CHECK(BlackMaximum, "maximum gray <= 2"); INKEYS_DEBUG_CHECK(InitialPointsLow, "initial points < 40");
			INKEYS_DEBUG_CHECK(ForwardPointsLow, "Forward valid points < 40"); INKEYS_DEBUG_CHECK(FbPointsLow, "FB valid points < 40");
			INKEYS_DEBUG_CHECK(AffineCellsLow, "affine cells < 16 (skip diagnostic fit)");
			INKEYS_DEBUG_CHECK(AffineSolveFailed, "affine solve failed (skip diagnostic fit)");
			INKEYS_DEBUG_CHECK(AffineVariationHigh, "nontranslation operand: variation > 1");
			INKEYS_DEBUG_CHECK(AffineResidualLow, "nontranslation operand: affine residual < .55 * translation residual");
			INKEYS_DEBUG_CHECK(AffineSupportHigh, "nontranslation operand: supported cells / cells >= .65");
			INKEYS_DEBUG_CHECK(NonTranslation, "NonTranslation: variation AND residual AND support");
			INKEYS_DEBUG_CHECK(ZeroModeAmbiguity, "zero-main AND other ratio > .15 AND interior >= 6 (last evaluated candidate)");
			INKEYS_DEBUG_CHECK(RunnerUpHigh, "runner-up original-cell ratio > .35"); INKEYS_DEBUG_CHECK(InliersLow, "inliers < 40");
			INKEYS_DEBUG_CHECK(SpatialRatioLow, "spatial fit ratio < .65"); INKEYS_DEBUG_CHECK(SurvivalRatioLow, "survival ratio < .35");
			INKEYS_DEBUG_CHECK(CoverageCells, "coverage AND: inlier cells >= 16"); INKEYS_DEBUG_CHECK(CoverageInterior, "coverage AND: interior cells >= 8");
			INKEYS_DEBUG_CHECK(CoverageColumns, "coverage AND: columns >= 4"); INKEYS_DEBUG_CHECK(CoverageRows, "coverage AND: rows >= 3");
			INKEYS_DEBUG_CHECK(CoverageX, "coverage AND: X span >= .5"); INKEYS_DEBUG_CHECK(CoverageY, "coverage AND: Y span >= .4");
			INKEYS_DEBUG_CHECK(JumpHigh, "normalized jump > .25"); INKEYS_DEBUG_CHECK(AnchorDisagreement, "anchor delta vs consensus distance > 1.5");
#undef INKEYS_DEBUG_CHECK
		default: return "None";
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
