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
		constexpr int MinimumPoints = 8;
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
			bool mature = false;
			MotionVector birthTotal{};
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
				point.provisional = !track.mature; point.targetMember = track.mature;
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
			TrackerDebugBounds bounds;
			for (const auto i : indices) { cells[matches[i].cell] = true; Extend(bounds, matches[i].point); }
			stats.inlierCells = static_cast<int>(std::count(cells.begin(), cells.end(), true));
			for (int y = 1; y < Rows - 1; ++y) for (int x = 1; x < Columns - 1; ++x)
				stats.interiorCells += cells[y * Columns + x] ? 1 : 0;
			if (bounds.valid) { stats.coverageX = (bounds.maxX - bounds.minX) / size.width; stats.coverageY = (bounds.maxY - bounds.minY) / size.height; }
			// 网格只保留为诊断；小内容区不能再被整个屏幕的覆盖比例否决。
			Stage(debug, TrackerDebugStage::Coverage, true);
			return true;
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
			double scaleX, double scaleY, std::uint64_t birth, TrackerDebugSnapshot* debug, cv::Rect support = {})
		{
			std::array<int, CellCount> counts{};
			cv::Mat available(gray.size(), CV_8UC1, cv::Scalar(support.empty() ? 255 : 0));
			if (!support.empty()) available(support & cv::Rect(0, 0, gray.cols, gray.rows)).setTo(255);
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
				const int quota = support.empty() ? 6 + (cell < 32 ? 1 : 0) : std::max(8, 320 / std::max(1, (support.width * Columns / gray.cols + 1) * (support.height * Rows / gray.rows + 1)));
				if (counts[cell] >= quota) continue;
				const int x = cell % Columns, y = cell / Columns;
				const cv::Rect roi(x * gray.cols / Columns, y * gray.rows / Rows,
					(x + 1) * gray.cols / Columns - x * gray.cols / Columns,
					(y + 1) * gray.rows / Rows - y * gray.rows / Rows);
				if (!cv::countNonZero(available(roi))) continue;
				// 仅检测当前格，保留梯度/局部极值的 halo，格线不能成为虚假的图像边界。
				const cv::Rect search = (roi + cv::Size(8, 8) - cv::Point(4, 4)) & cv::Rect(0, 0, gray.cols, gray.rows);
				// 遮罩只需覆盖检测邻域，内容与原全图遮罩的同一区域完全相同。
				cv::Mat mask = cv::Mat::zeros(search.size(), CV_8UC1);
				available(roi).copyTo(mask(roi - search.tl()));
				std::vector<cv::Point2f> points;
				cv::goodFeaturesToTrack(gray(search), points, (quota - counts[cell]) * 2, 0.01, 8, mask, 3, false);
				for (auto& p : points) p += cv::Point2f(static_cast<float>(search.x), static_cast<float>(search.y));
				if (!points.empty()) cv::cornerSubPix(gray, points, cv::Size(3, 3), cv::Size(-1, -1),
					cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, 0.001));
				for (const auto p : points)
				{
					if (counts[cell] >= quota || tracks.size() >= 320) break;
					if (!Inside(p, gray.size()) || Cell(p, gray.size()) != cell) continue;
					bool close = false;
					for (const auto& t : tracks) if (cv::norm(t.point - p) < 8) { close = true; break; }
					if (close) continue;
					tracks.push_back({ p, { p.x / scaleX - total.x, p.y / scaleY - total.y }, birth, false, total });
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
		// 仅用已选目标的对应点诊断形变；固定背景不再进入同一个仿射模型。
		bool NonTranslation(const std::vector<Match>& matches, cv::Size, MotionVector translation,
			MotionStatistics& stats, TrackerDebugSnapshot* debug)
		{
			Stage(debug, TrackerDebugStage::NonTranslation);
			if (matches.size() < MinimumPoints) { Stage(debug, TrackerDebugStage::NonTranslation, true); return false; }
			TrackerDebugBounds bounds;
			for (const auto& m : matches) Extend(bounds, m.point - m.displacement);
			const double width = bounds.maxX - bounds.minX, height = bounds.maxY - bounds.minY;
			if (width < 8 || height < 8) { Stage(debug, TrackerDebugStage::NonTranslation, true); return false; }
			std::vector<cv::Vec3d> coordinates;
			std::vector<cv::Vec2d> flows;
			for (const auto& m : matches)
			{
				const auto p = m.point - m.displacement;
				coordinates.emplace_back(1, (p.x - bounds.minX) / width - .5, (p.y - bounds.minY) / height - .5);
				flows.emplace_back(m.displacement.x, m.displacement.y);
			}
			std::vector<double> weights(matches.size(), 1);
			cv::Matx<double, 3, 2> model{};
			for (int iteration = 0; iteration < 5; ++iteration)
			{
				cv::Matx33d lhs = cv::Matx33d::zeros();
				cv::Matx<double, 3, 2> rhs = cv::Matx<double, 3, 2>::zeros();
				for (std::size_t i = 0; i < coordinates.size(); ++i) for (int row = 0; row < 3; ++row)
				{
					for (int column = 0; column < 3; ++column) lhs(row, column) += weights[i] * coordinates[i][row] * coordinates[i][column];
					for (int column = 0; column < 2; ++column) rhs(row, column) += weights[i] * coordinates[i][row] * flows[i][column];
				}
				if (!cv::solve(lhs, rhs, model, cv::DECOMP_SVD)) { Stage(debug, TrackerDebugStage::NonTranslation, true); return false; }
				for (std::size_t i = 0; i < coordinates.size(); ++i)
				{
					const double residual = cv::norm(model.t() * coordinates[i] - flows[i]);
					weights[i] = residual <= .75 ? 1 : .75 / residual;
				}
			}
			std::vector<double> affineResiduals, translationResiduals;
			int supported = 0;
			for (std::size_t i = 0; i < coordinates.size(); ++i)
			{
				const double residual = cv::norm(model.t() * coordinates[i] - flows[i]);
				affineResiduals.push_back(residual);
				translationResiduals.push_back(Length(flows[i][0] - translation.x, flows[i][1] - translation.y));
				supported += residual <= .8 ? 1 : 0;
			}
			stats.affineResidual = Percentile(affineResiduals, .5);
			for (const double x : { -.5, .5 }) for (const double y : { -.5, .5 })
				stats.affineVariation = std::max(stats.affineVariation, Length(model(1, 0) * x + model(2, 0) * y, model(1, 1) * x + model(2, 1) * y));
			if (debug) debug->affineModelValid = true;
			const bool nonTranslation = Check(debug, CheckId::AffineVariationHigh, stats.affineVariation > .7, stats.affineVariation, Comparison::Greater, .7)
				&& Check(debug, CheckId::AffineResidualLow, stats.affineResidual < .55 * Percentile(translationResiduals, .5), stats.affineResidual, Comparison::Less, .55 * Percentile(translationResiduals, .5))
				&& Check(debug, CheckId::AffineSupportHigh, static_cast<double>(supported) / matches.size() >= .75, static_cast<double>(supported) / matches.size(), Comparison::GreaterEqual, .75);
			Stage(debug, TrackerDebugStage::NonTranslation, true);
			return nonTranslation;
		}
		TrackerDebugBounds Bounds(cv::Rect rect)
		{
			return { !rect.empty(), static_cast<double>(rect.x), static_cast<double>(rect.y), static_cast<double>(rect.x + rect.width), static_cast<double>(rect.y + rect.height) };
		}
		bool Contains(cv::Rect rect, cv::Point2f p)
		{
			return p.x >= rect.x && p.y >= rect.y && p.x < rect.x + rect.width && p.y < rect.y + rect.height;
		}
		struct LocalPatch
		{
			cv::Rect rect;
			std::array<TrackerDebugPatchPeak, 3> peaks{};
			int count = 0;
			double information = 0, change = 0;
			bool member = false;
		};
		struct IdentitySample
		{
			cv::Rect rect;
			double correlation = 0;
		};
		struct IdentityPattern
		{
			cv::Rect rect;
			double information = 0;
		};
		struct LocalMotion
		{
			MotionVector flow{};
			std::vector<std::size_t> patches;
			double score = 0, margin = 0, meanScore = 0;
			int unique = 0, members = 0, expectedBlocks = 0, verifiedBlocks = 0, identityTiles = 0, identityContradictions = 0;
			double identityScore = 0;
			bool spread = false, identityDisambiguated = false;
			int unverifiedAlternatives = 0, unverifiedConflicts = 0;
			std::vector<IdentitySample> identitySamples;
			cv::Rect sourceBounds, currentBounds;
		};
		bool IdentityMember(const cv::Mat& membership, const cv::Mat& reference, cv::Rect tile);
		std::vector<LocalPatch> GatherPatches(const cv::Mat& reference, const cv::Mat& gray,
			const std::vector<Track>& tracks, cv::Rect support, bool locked, const cv::Mat& membership = {})
		{
			if (locked)
			{
				if (membership.empty()) return {};
				support &= cv::boundingRect(membership);
				if (support.empty()) return {};
			}
			int patchWidth = std::clamp(reference.cols / 8, 32, 96), patchHeight = std::clamp(reference.rows / 12, 20, 48);
			// 小目标沿用全屏块尺寸会只剩两行/列，轻微滚动即被误判无重叠。
			if (locked && !support.empty() && (support.width < 2 * patchWidth || support.height < 4 * patchHeight))
			{
				patchWidth = std::min(patchWidth, std::max(24, support.width / 3));
				patchHeight = std::min(patchHeight, std::max(16, support.height / 3));
			}
			cv::Mat difference;
			cv::absdiff(reference, gray, difference);
			std::vector<LocalPatch> candidates;
			double maximumChange = 0;
			std::vector<cv::Point2f> centers;
			for (const auto& track : tracks) centers.push_back(track.point);
			// 少角点图像仍可拥有可辨纹理；格点仅提供模板位置，不直接授予内容锚点。
			if (tracks.size() < MinimumPoints)
			{
				const auto area = support.empty() ? cv::Rect(0, 0, reference.cols, reference.rows) : support;
				for (int y = area.y + patchHeight / 2; y < area.y + area.height; y += patchHeight)
					for (int x = area.x + patchWidth / 2; x < area.x + area.width; x += patchWidth)
						centers.emplace_back(static_cast<float>(x), static_cast<float>(y));
			}
			for (const auto center : centers)
			{
				if (locked && !Contains(support, center)) continue;
				cv::Rect rect(std::clamp(cvRound(center.x) - patchWidth / 2, 0, reference.cols - patchWidth),
					std::clamp(cvRound(center.y) - patchHeight / 2, 0, reference.rows - patchHeight), patchWidth, patchHeight);
				if (locked && support.width >= patchWidth && support.height >= patchHeight)
				{ rect.x = std::clamp(rect.x, support.x, support.x + support.width - patchWidth); rect.y = std::clamp(rect.y, support.y, support.y + support.height - patchHeight); }
				cv::Scalar mean, deviation;
				cv::meanStdDev(reference(rect), mean, deviation);
				if (deviation[0] < 4) continue;
				LocalPatch patch;
				patch.rect = rect; patch.information = deviation[0]; patch.change = cv::mean(difference(rect))[0];
				for (const auto& known : tracks) if (known.mature && Contains(rect, known.point)) { patch.member = true; break; }
				maximumChange = std::max(maximumChange, patch.change);
				candidates.push_back(patch);
			}
			// 变化只用于分配发现预算，不用像素差推断位移；静态工具栏不能耗尽所有模板。
			if (!locked && maximumChange > 1)
				candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const auto& patch) { return patch.change < std::max(.5, maximumChange * .04); }), candidates.end());
			std::stable_sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b)
			{
				const double aScore = a.information * (locked ? (a.member ? 4 : 1) : 1 + a.change);
				const double bScore = b.information * (locked ? (b.member ? 4 : 1) : 1 + b.change);
				return aScore > bScore;
			});
			std::vector<LocalPatch> patches;
			std::array<bool, 24> buckets{};
			const std::size_t limit = locked ? 12 : 24;
			for (int pass = 0; pass < 2; ++pass) for (const auto& patch : candidates)
			{
				if (patches.size() >= limit) break;
				const int bucket = std::clamp((patch.rect.y + patchHeight / 2) * 4 / reference.rows, 0, 3) * 6
					+ std::clamp((patch.rect.x + patchWidth / 2) * 6 / reference.cols, 0, 5);
				if (pass == 0 && buckets[bucket]) continue;
				// 身份资格先于预算/NMS，避免静态边框或动画块占满名额后才被后级全部丢弃。
				if (locked && !IdentityMember(membership, reference, patch.rect)) continue;
				bool overlap = false;
				for (const auto& other : patches) if ((patch.rect & other.rect).area() > patch.rect.area() * .20) { overlap = true; break; }
				if (overlap) continue;
				patches.push_back(patch); buckets[bucket] = true;
			}
			return patches;
		}
		double PeakOffset(const cv::Mat& scores, cv::Point p, bool horizontal)
		{
			const int x = horizontal ? 1 : 0, y = horizontal ? 0 : 1;
			if (p.x < x || p.y < y || p.x + x >= scores.cols || p.y + y >= scores.rows) return 0;
			const double left = scores.at<float>(p.y - y, p.x - x), middle = scores.at<float>(p.y, p.x), right = scores.at<float>(p.y + y, p.x + x);
			const double curvature = left + right - 2 * middle;
			return curvature < -1e-6 ? std::clamp(.5 * (left - right) / curvature, -.5, .5) : 0;
		}
		TrackerDebugPatchPeak RefinePeak(const cv::Mat& reference, const cv::Mat& gray, cv::Rect rect, cv::Point2f predicted, int radius)
		{
			TrackerDebugPatchPeak peak;
			const int left = std::max(0, cvRound(predicted.x) - radius), top = std::max(0, cvRound(predicted.y) - radius);
			const int right = std::min(gray.cols - rect.width, cvRound(predicted.x) + radius), bottom = std::min(gray.rows - rect.height, cvRound(predicted.y) + radius);
			if (left > right || top > bottom) return peak;
			cv::Mat scores;
			cv::matchTemplate(gray(cv::Rect(left, top, right - left + rect.width, bottom - top + rect.height)), reference(rect), scores, cv::TM_CCOEFF_NORMED);
			cv::Point point;
			cv::minMaxLoc(scores, nullptr, &peak.score, nullptr, &point);
			if (!std::isfinite(peak.score) || peak.score < .60) return peak;
			peak.valid = true;
			peak.flow = { left + point.x + PeakOffset(scores, point, true) - rect.x, top + point.y + PeakOffset(scores, point, false) - rect.y };
			return peak;
		}
		void MatchPatches(const cv::Mat& reference, const cv::Mat& gray, std::vector<LocalPatch>& patches, cv::Rect support, MotionStatistics& stats, TrackerDebugSnapshot* debug)
		{
			const double ratio = std::min(1.0, 640.0 / std::max(gray.cols, gray.rows));
			const cv::Size coarseSize(std::max(1, cvRound(gray.cols * ratio)), std::max(1, cvRound(gray.rows * ratio)));
			cv::Mat coarseReference, coarseCurrent;
			cv::resize(reference, coarseReference, coarseSize, 0, 0, cv::INTER_AREA);
			cv::resize(gray, coarseCurrent, coarseSize, 0, 0, cv::INTER_AREA);
			const double sx = static_cast<double>(coarseSize.width) / gray.cols, sy = static_cast<double>(coarseSize.height) / gray.rows;
			cv::Rect search(0, 0, coarseSize.width, coarseSize.height);
			if (!support.empty())
			{
				const int padX = std::max(32, gray.cols / 8), padY = std::max(24, gray.rows / 12);
				const auto expanded = (support + cv::Size(padX * 2, padY * 2) - cv::Point(padX, padY)) & cv::Rect(0, 0, gray.cols, gray.rows);
				search = cv::Rect(cvFloor(expanded.x * sx), cvFloor(expanded.y * sy), cvFloor(expanded.width * sx), cvFloor(expanded.height * sy));
			}
			for (std::size_t i = 0; i < patches.size(); ++i)
			{
				auto& patch = patches[i];
				if (debug)
				{
					auto& out = debug->patches[i]; out.referenceBounds = Bounds(patch.rect);
					out.information = patch.information; out.change = patch.change; out.targetMember = patch.member;
				}
				// 极端长宽比的粗图短边可能不足四像素；模板必须落在真实粗图内，预测继续用实际起点。
				const cv::Size smallSize(std::min(coarseReference.cols, std::max(4, cvFloor(patch.rect.width * sx))),
					std::min(coarseReference.rows, std::max(4, cvFloor(patch.rect.height * sy))));
				const cv::Rect small(std::clamp(cvFloor(patch.rect.x * sx), 0, coarseReference.cols - smallSize.width),
					std::clamp(cvFloor(patch.rect.y * sy), 0, coarseReference.rows - smallSize.height), smallSize.width, smallSize.height);
				if (search.width < small.width || search.height < small.height) continue;
				cv::Mat scores;
				cv::matchTemplate(coarseCurrent(search), coarseReference(small), scores, cv::TM_CCOEFF_NORMED);
				std::vector<TrackerDebugPatchPeak> peaks;
				for (int p = 0; p < 3; ++p)
				{
					double score = 0; cv::Point point;
					cv::minMaxLoc(scores, nullptr, &score, nullptr, &point);
					if (!std::isfinite(score) || score < .50) break;
					const cv::Point2f prediction(static_cast<float>(patch.rect.x + (point.x + search.x - small.x) / sx), static_cast<float>(patch.rect.y + (point.y + search.y - small.y) / sy));
					const auto verificationBegin = Clock::now(); Stage(debug, TrackerDebugStage::PatchVerification);
					const auto peak = RefinePeak(reference, gray, patch.rect, prediction, static_cast<int>(std::ceil(2 / std::min(sx, sy))) + 1);
					stats.verificationMs += Milliseconds(verificationBegin);
					if (peak.valid) peaks.push_back(peak);
					cv::rectangle(scores, cv::Rect(point.x - 3, point.y - 3, 7, 7) & cv::Rect(0, 0, scores.cols, scores.rows), cv::Scalar(-2), -1);
				}
				// 零位移也必须走同一真实像素核验，不由固定区域的点数投票得出。
				const auto zeroBegin = Clock::now();
				const auto zero = RefinePeak(reference, gray, patch.rect, cv::Point2f(static_cast<float>(patch.rect.x), static_cast<float>(patch.rect.y)), 1);
				stats.verificationMs += Milliseconds(zeroBegin);
				if (zero.valid) peaks.push_back(zero);
				std::stable_sort(peaks.begin(), peaks.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
				for (const auto& peak : peaks)
				{
					bool duplicate = false;
					for (int j = 0; j < patch.count; ++j) if (Length(peak.flow.x - patch.peaks[j].flow.x, peak.flow.y - patch.peaks[j].flow.y) < 2) { duplicate = true; break; }
					if (!duplicate && patch.count < 3) patch.peaks[patch.count++] = peak;
				}
				if (debug)
				{
					auto& out = debug->patches[i]; out.peaks = patch.peaks; out.peakCount = patch.count;
				}
			}
			if (debug) debug->patchCount = patches.size();
		}
		const TrackerDebugPatchPeak* MatchingPeak(const LocalPatch& patch, MotionVector flow)
		{
			const TrackerDebugPatchPeak* result = nullptr;
			for (int i = 0; i < patch.count; ++i)
				if (Length(patch.peaks[i].flow.x - flow.x, patch.peaks[i].flow.y - flow.y) <= ConsensusRadius
					&& (!result || patch.peaks[i].score > result->score)) result = &patch.peaks[i];
			return result;
		}
		LocalMotion EvaluateMotion(const std::vector<LocalPatch>& patches, MotionVector flow)
		{
			LocalMotion motion; motion.flow = flow;
			std::vector<std::pair<double, double>> xs, ys;
			for (std::size_t i = 0; i < patches.size(); ++i)
			{
				const auto& patch = patches[i]; const auto* peak = MatchingPeak(patch, flow);
				if (!peak || peak->score < .82) continue;
				double other = 0;
				for (int j = 0; j < patch.count; ++j)
					if (Length(patch.peaks[j].flow.x - flow.x, patch.peaks[j].flow.y - flow.y) > 2 * ConsensusRadius) other = std::max(other, patch.peaks[j].score);
				motion.unique += peak->score - other >= .035 ? 1 : 0;
				motion.margin += peak->score - other; motion.meanScore += peak->score;
				motion.score += (peak->score - .75) * (patch.member ? 1.5 : 1.0);
				motion.members += patch.member ? 1 : 0; motion.patches.push_back(i);
				xs.emplace_back(peak->flow.x, peak->score); ys.emplace_back(peak->flow.y, peak->score);
				motion.sourceBounds = motion.sourceBounds.empty() ? patch.rect : motion.sourceBounds | patch.rect;
				const cv::Rect destination(patch.rect.x + cvRound(peak->flow.x), patch.rect.y + cvRound(peak->flow.y), patch.rect.width, patch.rect.height);
				motion.currentBounds = motion.currentBounds.empty() ? destination : motion.currentBounds | destination;
			}
			if (!motion.patches.empty())
			{
				motion.flow = { WeightedMedian(std::move(xs)), WeightedMedian(std::move(ys)) };
				motion.margin /= motion.patches.size(); motion.meanScore /= motion.patches.size();
				const auto& patch = patches[motion.patches.front()];
				motion.spread = motion.sourceBounds.width >= patch.rect.width * 1.2 && motion.sourceBounds.height >= patch.rect.height * 1.2;
			}
			return motion;
		}
		std::vector<LocalMotion> FindMotions(const std::vector<LocalPatch>& patches)
		{
			std::vector<LocalMotion> motions;
			for (const auto& patch : patches) for (int p = 0; p < patch.count; ++p)
			{
				auto motion = EvaluateMotion(patches, patch.peaks[p].flow);
				if (motion.patches.size() < 3 || !motion.spread) continue;
				bool duplicate = false;
				for (auto& previous : motions) if (Length(previous.flow.x - motion.flow.x, previous.flow.y - motion.flow.y) <= 2 * ConsensusRadius)
				{
					if (motion.score > previous.score) previous = std::move(motion);
					duplicate = true; break;
				}
				if (!duplicate) motions.push_back(std::move(motion));
			}
			std::stable_sort(motions.begin(), motions.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
			if (motions.size() > 9) motions.resize(9);
			return motions;
		}
		double IdentityInformation(const cv::Mat& pattern, cv::Mat* energy = nullptr)
		{
			cv::Mat gx, gy;
			cv::Sobel(pattern, gx, CV_32F, 1, 0, 3, 1.0 / 2040); cv::Sobel(pattern, gy, CV_32F, 0, 1, 3, 1.0 / 2040);
			const auto xx = gx.mul(gx), yy = gy.mul(gy);
			const double a = cv::mean(xx)[0], b = cv::mean(gx.mul(gy))[0], c = cv::mean(yy)[0];
			if (energy) *energy = xx + yy;
			return .5 * (a + c - std::sqrt((a - c) * (a - c) + 4 * b * b));
		}
		bool IdentityMember(const cv::Mat& membership, const cv::Mat& reference, cv::Rect tile)
		{
			if (membership.empty() || (tile & cv::Rect(0, 0, membership.cols, membership.rows)) != tile) return false;
			cv::Mat energy; IdentityInformation(reference(tile), &energy);
			const double sum = cv::sum(energy)[0];
			const int pixels = cv::countNonZero(membership(tile));
			// 白底没有反证能力，不能按面积票数吞掉少量区别纹理；动画的有信息像素仍必须有历史身份。
			return sum > 1e-8 ? cv::mean(energy, membership(tile))[0] * pixels >= .85 * sum : pixels >= tile.area() * .85;
		}
		std::vector<IdentityPattern> IdentityPatterns(const cv::Mat& reference, cv::Rect support, const cv::Mat& membership,
			const std::vector<Track>& tracks)
		{
			std::vector<IdentityPattern> patterns;
			if (membership.empty()) return patterns;
			const auto area = support & cv::boundingRect(membership) & cv::Rect(0, 0, reference.cols, reference.rows);
			if (area.width < 12 || area.height < 12) return patterns;
			const int columns = std::min(6, area.width / 12), rows = std::min(6, area.height / 12);
			for (int y = 0; y < rows; ++y) for (int x = 0; x < columns; ++x)
			{
				const int left = area.x + x * area.width / columns, right = area.x + (x + 1) * area.width / columns;
				const int top = area.y + y * area.height / rows, bottom = area.y + (y + 1) * area.height / rows;
				IdentityPattern best;
				// 每层最多九个源图位置，只保留一个；左右边缘固定席位保住局部区别纹理。
				const int samplesX = x == 0 || x + 1 == columns || right - left == 12 ? 1 : 3;
				const int samplesY = bottom - top == 12 ? 1 : 3;
				for (int sy = 0; sy < samplesY; ++sy) for (int sx = 0; sx < samplesX; ++sx)
				{
					const int px = x == 0 ? left : x + 1 == columns ? right - 12 : left + sx * (right - left - 12) / 2;
					const int py = top + sy * (bottom - top - 12) / 2;
					const cv::Rect tile(px, py, 12, 12);
					if (!IdentityMember(membership, reference, tile)) continue;
					cv::Scalar mean, deviation; cv::meanStdDev(reference(tile), mean, deviation);
					if (deviation[0] < 6) continue;
					const double value = IdentityInformation(reference(tile));
					if (value > best.information) best = { tile, value };
				}
				if (best.rect.empty())
				{
					// 大图的规则位置可能全落在行间；只给空层补可信源角点，不替换已有边缘反证。
					const cv::Rect region(left, top, right - left, bottom - top);
					int examined = 0;
					for (const auto& track : tracks)
					{
						if (!track.mature || !Contains(region, track.point)) continue;
						if (++examined > 9) break;
						const cv::Rect tile(std::clamp(cvRound(track.point.x) - 6, left, right - 12),
							std::clamp(cvRound(track.point.y) - 6, top, bottom - 12), 12, 12);
						if (!IdentityMember(membership, reference, tile)) continue;
						cv::Scalar mean, deviation; cv::meanStdDev(reference(tile), mean, deviation);
						if (deviation[0] < 6) continue;
						const double value = IdentityInformation(reference(tile));
						if (value > best.information) best = { tile, value };
					}
				}
				if (!best.rect.empty()) patterns.push_back(best);
			}
			return patterns;
		}
		double IdentityThreshold(const std::vector<IdentityPattern>& patterns)
		{
			std::vector<double> information;
			for (const auto& pattern : patterns) information.push_back(pattern.information);
			// 对比度一致缩放不改变身份；只从历史成员估计尺度，不按当前候选的好坏调门。
			return std::max(1e-6, .15 * Percentile(information, .75));
		}
		bool VerifyTargetBlocks(const cv::Mat& reference, const cv::Mat& gray, const std::vector<LocalPatch>& patches,
			MotionVector flow, cv::Rect support, const cv::Mat& membership, int& expected, int& verified)
		{
			// 目标身份由所有预计仍可见的可信块约束；少量相似句不能绕过其它块的反证。
			const auto overlap = support & (support - cv::Point(cvRound(flow.x), cvRound(flow.y))) & cv::Rect(0, 0, gray.cols, gray.rows);
			for (const auto& patch : patches)
			{
				if ((patch.rect & overlap).area() != patch.rect.area()) continue;
				if (!IdentityMember(membership, reference, patch.rect)
					|| !IdentityMember(membership, reference, patch.rect + cv::Point(cvRound(flow.x), cvRound(flow.y)))) continue;
				// 静态同像素块不能凭周期别名成为非零目标票；新模板仍需独立的跨帧像素证据。
				if (Length(flow.x, flow.y) > .12 && cv::norm(reference(patch.rect), gray(patch.rect), cv::NORM_INF) <= 1) continue;
				++expected;
				cv::Mat current, score;
				cv::getRectSubPix(gray, patch.rect.size(), cv::Point2f(static_cast<float>(patch.rect.x + (patch.rect.width - 1) * .5 + flow.x),
					static_cast<float>(patch.rect.y + (patch.rect.height - 1) * .5 + flow.y)), current);
				cv::matchTemplate(current, reference(patch.rect), score, cv::TM_CCOEFF_NORMED);
				if (score.at<float>(0, 0) >= .86) ++verified;
			}
			return expected >= 3 && verified * 4 >= expected * 3;
		}
		void VerifyMotion(const cv::Mat& reference, const cv::Mat& gray, const std::vector<LocalPatch>& patches, LocalMotion& motion, cv::Rect support, const cv::Mat& membership)
		{
			LocalMotion verified; verified.flow = motion.flow;
			const cv::Rect image(0, 0, gray.cols, gray.rows);
			const auto area = support.empty() ? motion.sourceBounds | motion.currentBounds : support;
			const auto overlap = area & (area - cv::Point(cvRound(motion.flow.x), cvRound(motion.flow.y))) & image;
			if (!support.empty() && !VerifyTargetBlocks(reference, gray, patches, motion.flow, support, membership, verified.expectedBlocks, verified.verifiedBlocks))
			{ motion = std::move(verified); return; }
			for (const auto i : motion.patches)
			{
				const auto& patch = patches[i]; const auto* peak = MatchingPeak(patch, motion.flow);
				const auto context = (patch.rect + cv::Size(patch.rect.width, patch.rect.height * 2) - cv::Point(patch.rect.width / 2, patch.rect.height)) & overlap;
				if (!peak || context.area() < patch.rect.area() || context.width < patch.rect.width || context.height < patch.rect.height) continue;
				if (context.x + motion.flow.x < 0 || context.y + motion.flow.y < 0 || context.x + context.width + motion.flow.x > gray.cols || context.y + context.height + motion.flow.y > gray.rows) continue;
				cv::Mat current, score;
				cv::getRectSubPix(gray, context.size(), cv::Point2f(static_cast<float>(context.x + (context.width - 1) * .5 + motion.flow.x),
					static_cast<float>(context.y + (context.height - 1) * .5 + motion.flow.y)), current);
				cv::matchTemplate(current, reference(context), score, cv::TM_CCOEFF_NORMED);
				const double correlation = score.at<float>(0, 0);
				if (!std::isfinite(correlation) || correlation < .86) continue;
				double other = 0;
				for (int j = 0; j < patch.count; ++j) if (Length(patch.peaks[j].flow.x - motion.flow.x, patch.peaks[j].flow.y - motion.flow.y) > 2 * ConsensusRadius) other = std::max(other, patch.peaks[j].score);
				verified.patches.push_back(i); verified.unique += peak->score - other >= .035 ? 1 : 0;
				verified.members += patch.member ? 1 : 0; verified.margin += peak->score - other; verified.meanScore += correlation;
				verified.score += (correlation - .75) * (patch.member ? 1.5 : 1);
				verified.sourceBounds = verified.sourceBounds.empty() ? patch.rect : verified.sourceBounds | patch.rect;
				const cv::Rect destination(patch.rect.x + cvRound(motion.flow.x), patch.rect.y + cvRound(motion.flow.y), patch.rect.width, patch.rect.height);
				verified.currentBounds = verified.currentBounds.empty() ? destination : verified.currentBounds | destination;
			}
			if (!verified.patches.empty())
			{
				verified.margin /= verified.patches.size(); verified.meanScore /= verified.patches.size();
				const auto& patch = patches[verified.patches.front()];
				verified.spread = verified.sourceBounds.width >= patch.rect.width * 1.2 && verified.sourceBounds.height >= patch.rect.height * 1.2;
			}
			motion = std::move(verified);
		}
		void VerifyIdentity(const cv::Mat& reference, const cv::Mat& gray, cv::Rect support, const cv::Mat& membership,
			const std::vector<IdentityPattern>& patterns, double informationThreshold, LocalMotion& motion)
		{
			// 细小独有纹理不能被大块均值淹没；固定上限网格覆盖目标边缘与内部，不解释字符语义。
			const auto overlap = support & (support - cv::Point(cvRound(motion.flow.x), cvRound(motion.flow.y))) & cv::Rect(0, 0, gray.cols, gray.rows);
			constexpr int tileSize = 12;
			if (overlap.width < tileSize || overlap.height < tileSize) return;
			for (const auto& pattern : patterns)
			{
				const auto& tile = pattern.rect;
				if ((tile & overlap) != tile || pattern.information < informationThreshold) continue;
				// 支持框内部可有固定动画孔洞；成员证据来自上次提交，不能按本次匹配好坏删除反证。
				if (!IdentityMember(membership, reference, tile + cv::Point(cvRound(motion.flow.x), cvRound(motion.flow.y)))) continue;
				if (Length(motion.flow.x, motion.flow.y) > .12 && cv::norm(reference(tile), gray(tile), cv::NORM_INF) <= 1) continue;
				cv::Mat current, score;
				cv::getRectSubPix(gray, tile.size(), cv::Point2f(static_cast<float>(tile.x + (tileSize - 1) * .5 + motion.flow.x),
					static_cast<float>(tile.y + (tileSize - 1) * .5 + motion.flow.y)), current);
				cv::matchTemplate(current, reference(tile), score, cv::TM_CCOEFF_NORMED);
				const double correlation = score.at<float>(0, 0);
				motion.identitySamples.push_back({ tile, correlation });
				++motion.identityTiles; motion.identityScore += correlation;
				if (!std::isfinite(correlation) || correlation < .72) ++motion.identityContradictions;
			}
			if (motion.identityTiles) motion.identityScore /= motion.identityTiles;
		}
		struct LocalMoments
		{
			cv::Mat image, mean, variance;
			explicit LocalMoments(const cv::Mat& source)
			{
				source.convertTo(image, CV_32F);
				cv::boxFilter(image, mean, CV_32F, cv::Size(11, 11));
				cv::boxFilter(image.mul(image), variance, CV_32F, cv::Size(11, 11));
				variance -= mean.mul(mean);
			}
		};
		cv::Mat LocalCorrelations(const LocalMoments& first, const cv::Mat& second)
		{
			const LocalMoments current(second);
			cv::Mat covariance;
			cv::boxFilter(first.image.mul(current.image), covariance, CV_32F, cv::Size(11, 11));
			covariance -= first.mean.mul(current.mean);
			for (int y = 0; y < covariance.rows; ++y)
			{
				auto* correlation = covariance.ptr<float>(y);
				const auto* va = first.variance.ptr<float>(y); const auto* vb = current.variance.ptr<float>(y);
				for (int x = 0; x < covariance.cols; ++x)
					correlation[x] = va[x] >= 16 && vb[x] >= 16 ? correlation[x] / std::sqrt(va[x] * vb[x]) : -1;
			}
			return covariance;
		}
		cv::Mat CertifyIdentity(const cv::Mat& reference, const cv::Mat& gray, MotionVector flow,
			cv::Rect support, const cv::Mat& previous)
		{
			cv::Mat sourceMembers = cv::Mat::zeros(gray.size(), CV_8U);
			const auto area = support & cv::Rect(0, 0, gray.cols, gray.rows);
			if (area.empty()) return sourceMembers;
			// 局部相关图线性计算成员区域，保留支持框中的孔洞；仅成功事务才保存此候选。
			const cv::Matx23d transform(1, 0, flow.x, 0, 1, flow.y);
			cv::Mat aligned;
			cv::warpAffine(gray, aligned, transform, gray.size(), cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
			// 窗口 halo 从真实图像取，support 的边缘不是图像边界，不能人为清空左右五列。
			const auto context = (area + cv::Size(10, 10) - cv::Point(5, 5)) & cv::Rect(0, 0, gray.cols, gray.rows);
			// 运动与静止核验使用同一参考窗口，复用均值/方差而不改变相关性阈值。
			const LocalMoments source(reference(context));
			const auto correlated = LocalCorrelations(source, aligned(context));
			const auto stationary = LocalCorrelations(source, gray(context));
			const double distance = Length(flow.x, flow.y);
			// 亚像素 NCC 的零流差异近似随位移平方变化；仍要求正优势，静止不能增加身份。
			const double advantage = std::max(.001, .035 * std::min(1.0, distance * distance));
			for (int y = area.y; y < area.y + area.height; ++y)
			{
				auto* members = sourceMembers.ptr<unsigned char>(y);
				const auto* score = correlated.ptr<float>(y - context.y); const auto* zero = stationary.ptr<float>(y - context.y);
				const auto* known = previous.empty() ? nullptr : previous.ptr<unsigned char>(y);
				for (int x = area.x; x < area.x + area.width; ++x)
				{
					if (x < 5 || y < 5 || x >= gray.cols - 5 || y >= gray.rows - 5
						|| x + flow.x < 5 || y + flow.y < 5 || x + flow.x >= gray.cols - 5 || y + flow.y >= gray.rows - 5
						|| !Contains(area, cv::Point2f(static_cast<float>(x + flow.x), static_cast<float>(y + flow.y)))) continue;
					const int localX = x - context.x;
					if (score[localX] >= .86 && ((known && known[x]) || (distance > .12 && score[localX] - zero[localX] >= advantage))) members[x] = 255;
				}
			}
			cv::Mat currentMembers, result;
			cv::warpAffine(sourceMembers, currentMembers, transform, gray.size(), cv::INTER_NEAREST, cv::BORDER_CONSTANT);
			// 屏幕可见支持不随内容逐步移出窗口：已验证的源/目标位置共同认证，下次仍检查两端。
			cv::bitwise_or(sourceMembers, currentMembers, result);
			return result;
		}
		bool IdentityDisambiguates(const cv::Mat& reference, const cv::Mat& gray, cv::Rect support, const cv::Mat& membership,
			const std::vector<LocalMotion>& motions, std::size_t selected)
		{
			const auto& motion = motions[selected];
			if (motion.identityTiles < 6 || motion.identityContradictions >= 2 || motion.patches.size() < 3 || !motion.spread) return false;
			bool distinguished = false;
			for (std::size_t i = 0; i < motions.size(); ++i)
			{
				if (i == selected) continue;
				const auto& other = motions[i];
				int contradictions = 0;
				// 必须在同一位置证明“这一细格匹配、另一位移不匹配”，不能拿其它区域的坏分数补唯一性。
				for (const auto& sample : motion.identitySamples)
				{
					if (sample.correlation < .86 || !std::isfinite(sample.correlation)) continue;
					const auto& tile = sample.rect;
					if (tile.x + other.flow.x < support.x || tile.y + other.flow.y < support.y
						|| tile.x + tile.width + other.flow.x > support.x + support.width
						|| tile.y + tile.height + other.flow.y > support.y + support.height) continue;
					if (!IdentityMember(membership, reference, tile + cv::Point(cvRound(other.flow.x), cvRound(other.flow.y)))) continue;
					cv::Mat current, score;
					cv::getRectSubPix(gray, tile.size(), cv::Point2f(static_cast<float>(tile.x + (tile.width - 1) * .5 + other.flow.x),
						static_cast<float>(tile.y + (tile.height - 1) * .5 + other.flow.y)), current);
					cv::matchTemplate(current, reference(tile), score, cv::TM_CCOEFF_NORMED);
					const double correlation = score.at<float>(0, 0);
					if (std::isfinite(correlation) && correlation < .72 && ++contradictions >= 2) break;
				}
				if (contradictions >= 2) distinguished = true;
				else if (other.patches.size() >= 3 && other.spread && other.identityContradictions < 2) return false;
			}
			return distinguished;
		}
		bool DistinctPointMotion(const cv::Mat& source, const cv::Mat& gray, cv::Point2f p, cv::Point2f q, MotionVector selected)
		{
			const auto flow = q - p;
			if (cv::norm(flow) <= .12 || Length(flow.x - selected.x, flow.y - selected.y) <= 2 * ConsensusRadius) return false;
			const cv::Point2f predicted(static_cast<float>(p.x + selected.x), static_cast<float>(p.y + selected.y));
			if (!Inside(predicted, gray.size())) return false;
			const cv::Rect local(cvRound(p.x) - 4, cvRound(p.y) - 4, 9, 9);
			if (cv::norm(source(local), gray(local), cv::NORM_INF) <= 1) return false;
			cv::Mat pattern, current, chosen, score;
			cv::getRectSubPix(source, { 9, 9 }, p, pattern);
			cv::getRectSubPix(gray, { 9, 9 }, q, current);
			cv::getRectSubPix(gray, { 9, 9 }, predicted, chosen);
			cv::matchTemplate(current, pattern, score, cv::TM_CCOEFF_NORMED); const double correlation = score.at<float>(0, 0);
			cv::matchTemplate(chosen, pattern, score, cv::TM_CCOEFF_NORMED); const double selectedCorrelation = score.at<float>(0, 0);
			return std::isfinite(correlation) && std::isfinite(selectedCorrelation) && correlation >= .90 && correlation - selectedCorrelation >= .035;
		}
		std::vector<std::size_t> IndependentPointMotion(const std::vector<Match>& matches)
		{
			for (const auto& seed : matches)
			{
				const auto group = Inliers(matches, Position(seed.displacement));
				std::vector<std::size_t> independent; TrackerDebugBounds bounds;
				for (const auto i : group)
				{
					const auto p = matches[i].point - matches[i].displacement;
					bool separate = true;
					for (const auto j : independent)
						if (cv::norm(p - (matches[j].point - matches[j].displacement)) < 12) { separate = false; break; }
					if (!separate) continue;
					independent.push_back(i); Extend(bounds, p);
				}
				if (independent.size() >= 3 && bounds.maxX - bounds.minX >= 12 && bounds.maxY - bounds.minY >= 12) return independent;
			}
			return {};
		}
		cv::Rect SupportedRegion(const cv::Mat& reference, const cv::Mat& gray, MotionVector flow, cv::Rect trusted, cv::Rect initial)
		{
			const cv::Rect image(0, 0, gray.cols, gray.rows);
			auto base = (trusted.empty() ? initial : trusted) & image;
			if (base.empty() || Length(flow.x, flow.y) <= .12) return base;
			// 邻近变化只能提出区域扩展；必须按同一位移跨帧匹配，不能把旁侧动画直接并入目标。
			const auto search = (base + cv::Size(24, 24) - cv::Point(12, 12)) & image;
			cv::Mat difference, changed;
			cv::absdiff(reference(search), gray(search), difference); cv::threshold(difference, changed, 2, 255, cv::THRESH_BINARY);
			if (trusted.empty())
			{
				// 初始粗块可含静态外框；仅在原块内裁边，不能由一个稀疏扩展格替代整个初始范围。
				const auto pixels = cv::boundingRect(changed(base - search.tl()));
				if (!pixels.empty()) base &= (pixels + base.tl() + cv::Size(2, 2) - cv::Point(1, 1));
			}
			auto observed = cv::boundingRect(changed);
			constexpr int tileSize = 12;
			if (observed.width < tileSize || observed.height < tileSize) return base;
			observed += search.tl();
			const int columns = std::min(6, observed.width / tileSize), rows = std::min(6, observed.height / tileSize);
			cv::Rect certified;
			for (int y = 0; y < rows; ++y) for (int x = 0; x < columns; ++x)
			{
				const cv::Rect tile(observed.x + (columns > 1 ? x * (observed.width - tileSize) / (columns - 1) : 0),
					observed.y + (rows > 1 ? y * (observed.height - tileSize) / (rows - 1) : 0), tileSize, tileSize);
				if (tile.x + flow.x < 0 || tile.y + flow.y < 0 || tile.x + tile.width + flow.x > gray.cols || tile.y + tile.height + flow.y > gray.rows) continue;
				cv::Scalar mean, deviation; cv::meanStdDev(reference(tile), mean, deviation);
				if (deviation[0] < 4) continue;
				cv::Mat current, score;
				cv::getRectSubPix(gray, tile.size(), cv::Point2f(static_cast<float>(tile.x + (tileSize - 1) * .5 + flow.x),
					static_cast<float>(tile.y + (tileSize - 1) * .5 + flow.y)), current);
				cv::matchTemplate(current, reference(tile), score, cv::TM_CCOEFF_NORMED); const double correlation = score.at<float>(0, 0);
				cv::matchTemplate(gray(tile), reference(tile), score, cv::TM_CCOEFF_NORMED); const double stationary = score.at<float>(0, 0);
				if (!std::isfinite(correlation) || !std::isfinite(stationary) || correlation < .86 || correlation - stationary < .035) continue;
				const cv::Rect destination(tile.x + cvRound(flow.x), tile.y + cvRound(flow.y), tile.width, tile.height);
				for (const auto region : { tile & search, destination & search })
				{
					if (region.empty()) continue;
					auto pixels = cv::boundingRect(changed(region - search.tl()));
					if (pixels.empty()) continue;
					pixels += region.tl(); certified = certified.empty() ? pixels : certified | pixels;
				}
			}
			if (certified.empty()) return base;
			certified = (certified + cv::Size(2, 2) - cv::Point(1, 1)) & search;
			// 稀疏扩展采样不能缩掉已由运动块验证的初始范围，成员身份仍由局部历史证据判定。
			return base | certified;
		}
		bool SeparateRegions(const LocalMotion& a, const LocalMotion& b)
		{
			const auto& first = a.sourceBounds; const auto& second = b.sourceBounds;
			return !first.empty() && !second.empty() && (first & second).area() < .20 * std::min(first.area(), second.area());
		}
		void RecordMotion(TrackerDebugCandidate& output, const LocalMotion& motion)
		{
			output.valid = true; output.flow = motion.flow; output.fitWeight = motion.score;
			output.points = static_cast<int>(motion.patches.size()); output.cells = motion.unique;
			output.referenceBounds = Bounds(motion.sourceBounds); output.currentBounds = Bounds(motion.currentBounds);
		}
		bool SupportedPoint(const std::vector<LocalPatch>& patches, const LocalMotion& motion, cv::Point2f point)
		{
			for (const auto i : motion.patches) if (Contains(patches[i].rect, point)) return true;
			return false;
		}
	}

	struct VisualMotionTracker::Impl
	{
		cv::Mat reference, keyframe, identityMask, keyframeIdentityMask;
		std::vector<Track> tracks, keyframeTracks;
		cv::Rect support, keyframeSupport;
		int width = 0, height = 0;
		MotionPixelFormat format = MotionPixelFormat::Gray8;
		double scaleX = 0, scaleY = 0, referenceTime = 0, keyframeTime = 0, inputTime = 0, recoveryStartTime = 0;
		MotionVector total{}, keyframeTotal{}, identityRefreshTotal{};
		MotionState state = MotionState::Uninitialized;
		MotionTargetPhase phase = MotionTargetPhase::Discovering;
		std::uint64_t sequence = 0, generation = 0, keyframeSequence = 0, keyframeGeneration = 0, targetId = 0;
		int rejects = 0;
		struct Estimate
		{
			std::vector<Track> tracks;
			cv::Mat identityMask;
			MotionVector total{};
			cv::Rect support;
			bool locksTarget = false;
			double confidence = 0;
		};

		MotionResult Result(MotionStatistics stats = {}) const
		{
			stats.referenceSequence = sequence; stats.referenceGeneration = generation; stats.relayGeneration = generation;
			stats.consecutiveRejects = rejects; stats.targetId = targetId; stats.targetPhase = phase;
			stats.targetPoints = static_cast<int>(std::count_if(tracks.begin(), tracks.end(), [](const auto& track) { return track.mature; }));
			stats.provisionalPoints = static_cast<int>(tracks.size()) - stats.targetPoints;
			MotionResult result; result.total = total; result.state = state; result.statistics = stats;
			return result;
		}
		MotionResult Reject(MotionReason reason, MotionStatistics stats, bool severe = false)
		{
			// 恢复预算从第一次普通失配开始，静态发现期不会预先消耗恢复时间。
			if (!rejects) recoveryStartTime = inputTime;
			++rejects;
			stats.recoverySeconds = reference.empty() ? 0 : std::max(0.0, inputTime - recoveryStartTime);
			const bool expired = !reference.empty() && (stats.recoverySeconds >= 1.0 || rejects >= 24);
			if (severe || expired) state = MotionState::Paused;
			if (!reference.empty()) phase = MotionTargetPhase::Recovering;
			auto result = Result(stats); result.reason = expired && !severe ? MotionReason::RecoveryExpired : reason;
			return result;
		}
		void BeginDebug(const MotionFrame& frame, TrackerDebugSnapshot* debug, bool initialization) const noexcept
		{
			if (!debug) return;
			std::destroy_at(debug); std::construct_at(debug);
			debug->initialization = initialization; debug->inputSequence = frame.sequence;
			debug->referenceSequenceBefore = sequence; debug->referenceGenerationBefore = generation;
			debug->referenceBeforeValid = !reference.empty(); debug->totalBefore = total;
			debug->referenceProcessingWidth = reference.cols; debug->referenceProcessingHeight = reference.rows;
			debug->inputWidth = frame.image.width; debug->inputHeight = frame.image.height;
			debug->inputStrideBytes = frame.image.strideBytes; debug->inputSizeBytes = frame.image.sizeBytes;
			debug->inputFormat = frame.image.format; debug->timestampSeconds = frame.timestampSeconds; debug->captureValid = frame.captureValid;
			debug->pointBorder = Border; debug->consensusRadius = ConsensusRadius; debug->refinementDistanceLimit = RefinementDistanceLimit;
			debug->fbErrorLimit = FbErrorLimit; debug->nearStationaryLimit = NearStationaryLimit; debug->zeroModeRatioThreshold = ZeroModeRatioThreshold;
			debug->targetPhaseBefore = phase; debug->targetId = targetId; debug->targetSupport = Bounds(support);
			if (!reference.empty()) RecordTracks(debug, tracks, reference.size());
		}
		MotionResult EndDebug(MotionResult result, TrackerDebugSnapshot* debug) const noexcept
		{
			if (debug)
			{
				debug->result = result; debug->referenceSequenceAfter = sequence; debug->referenceGenerationAfter = generation;
				debug->referenceAfterValid = !reference.empty(); debug->targetPhaseAfter = phase;
				debug->targetId = targetId; debug->targetSupport = Bounds(support);
				if (result.accepted) for (std::size_t i = 0; i < debug->addedPointCount; ++i) debug->addedPoints[i].committed = true;
			}
			return result;
		}
		MotionResult Initialize(const MotionFrame& frame, TrackerDebugSnapshot* debug)
		{
			MotionStatistics stats{}; stats.inputSequence = frame.sequence; inputTime = frame.timestampSeconds;
			const auto valid = Validate(frame, debug);
			if (valid != MotionReason::None) return Reject(valid, stats, true);
			Stage(debug, TrackerDebugStage::Preprocess);
			const auto begin = Clock::now(); auto gray = Gray(frame.image); stats.preprocessMs = Milliseconds(begin);
			RecordGeometry(debug, gray, frame); Stage(debug, TrackerDebugStage::Preprocess, true);
			if (Black(gray, debug)) return Reject(MotionReason::BlackFrame, stats, true);
			const double sx = static_cast<double>(gray.cols) / frame.image.width, sy = static_cast<double>(gray.rows) / frame.image.height;
			std::vector<Track> initial;
			const auto refillBegin = Clock::now(); Stage(debug, TrackerDebugStage::Refill);
			stats.added = Refill(gray, initial, {}, sx, sy, 0, debug); stats.refillMs = Milliseconds(refillBegin);
			Stage(debug, TrackerDebugStage::Refill, true);
			const bool fewPoints = initial.size() < MinimumPoints;
			Check(debug, CheckId::InitialPointsLow, fewPoints, static_cast<double>(initial.size()), Comparison::Less, MinimumPoints);
			if (fewPoints)
			{
				const auto patches = GatherPatches(gray, gray, initial, {}, false);
				cv::Rect bounds;
				for (const auto& patch : patches) bounds = bounds.empty() ? patch.rect : bounds | patch.rect;
				const bool enough = patches.size() >= 3 && bounds.width >= patches.front().rect.width * 1.2 && bounds.height >= patches.front().rect.height * 1.2;
				if (RejectWhen(debug, CheckId::TemplatesLow, !enough, static_cast<double>(patches.size()), Comparison::Less, 3))
					return Reject(MotionReason::InsufficientFeatures, stats);
			}
			// 初始化只要求存在局部信息；目标在真实运动出现后才获得身份。
			Stage(debug, TrackerDebugStage::Commit);
			reference = std::move(gray); tracks = std::move(initial); width = frame.image.width; height = frame.image.height; format = frame.image.format;
			scaleX = sx; scaleY = sy; referenceTime = inputTime; total = {}; sequence = frame.sequence; generation = 1; rejects = 0;
			state = MotionState::Tracking; phase = MotionTargetPhase::Discovering;
			stats.initialSurvivors = static_cast<int>(tracks.size());
			auto result = Result(stats); result.accepted = true; result.confidence = 1; result.reason = MotionReason::Initialized;
			Stage(debug, TrackerDebugStage::Commit, true); return result;
		}
		MotionReason Measure(const cv::Mat& source, const std::vector<Track>& sourceTracks, MotionVector sourceTotal,
			cv::Rect sourceSupport, const cv::Mat& sourceIdentity, const cv::Mat& gray, int& verificationBudget, int reserve,
			MotionStatistics& stats, TrackerDebugSnapshot* debug, Estimate& estimate)
		{
			const bool locked = targetId != 0;
			// 完全相同的发现帧没有新运动证据；严格相等早退不改变参考或提交零位移。
			if (!locked && cv::norm(source, gray, cv::NORM_INF) == 0) return MotionReason::AwaitingMotion;
			const auto coarseBegin = Clock::now(); Stage(debug, TrackerDebugStage::CoarseMatch);
			auto patches = GatherPatches(source, gray, sourceTracks, sourceSupport, locked, sourceIdentity);
			stats.templateCount = static_cast<int>(patches.size());
			if (RejectWhen(debug, CheckId::TemplatesLow, patches.size() < 3, static_cast<double>(patches.size()), Comparison::Less, 3))
			{ stats.coarseMatchMs = Milliseconds(coarseBegin); return MotionReason::InsufficientOverlap; }
			MatchPatches(source, gray, patches, locked ? sourceSupport : cv::Rect{}, stats, debug);
			auto motions = FindMotions(patches);
			if (locked)
			{
				// 纯几何预筛只用既有块位置；完整可见块少于三的假设本来就不能通过后级。
				motions.erase(std::remove_if(motions.begin(), motions.end(), [&](const auto& motion)
				{
					const auto overlap = sourceSupport & (sourceSupport - cv::Point(cvRound(motion.flow.x), cvRound(motion.flow.y)));
					int visible = 0;
					for (const auto& patch : patches) visible += (patch.rect & overlap) == patch.rect ? 1 : 0;
					return visible < 3;
				}), motions.end());
			}
			const auto candidateLimit = static_cast<std::size_t>(std::max(0, verificationBudget - reserve));
			if (!locked && motions.size() > candidateLimit && candidateLimit >= 2)
			{
				// 精查前保留独立区域席位，不能让同一区域的重复纹理占满预算。
				const auto separate = std::find_if(motions.begin() + 1, motions.end(), [&](const auto& other) { return SeparateRegions(motions.front(), other); });
				if (separate != motions.end()) std::iter_swap(motions.begin() + 1, separate);
			}
			std::vector<LocalMotion> deferred;
			if (motions.size() > candidateLimit)
			{
				deferred.assign(motions.begin() + candidateLimit, motions.end()); motions.resize(candidateLimit);
			}
			const auto coarseMotions = motions;
			verificationBudget -= static_cast<int>(motions.size());
			const auto contextBegin = Clock::now();
			const auto identityPatterns = locked ? IdentityPatterns(source, sourceSupport, sourceIdentity, sourceTracks) : std::vector<IdentityPattern>{};
			const double identityThreshold = IdentityThreshold(identityPatterns);
			for (std::size_t i = 0; i < motions.size(); ++i)
			{
				auto& motion = motions[i]; const auto& coarse = coarseMotions[i];
				VerifyMotion(source, gray, patches, motion, locked ? sourceSupport : cv::Rect{}, sourceIdentity);
				if (locked) VerifyIdentity(source, gray, sourceSupport, sourceIdentity, identityPatterns, identityThreshold, motion);
				// 未展开的强竞争只保留元数据，不能再读当前像素或凭未核验就宣告已排除。
				for (const auto& other : deferred)
				{
					if ((!locked && Length(other.flow.x, other.flow.y) <= .12) || (locked && other.members < 2)) continue;
					const double ratio = other.score / std::max(coarse.score, 1e-9);
					const bool separate = !locked && SeparateRegions(coarse, other);
					const bool independent = separate && other.unique >= 2 && other.meanScore >= std::max(.90, coarse.meanScore - .03);
					if (ratio >= .55 || independent) ++motion.unverifiedAlternatives;
					if (independent || (ratio >= .55 && (other.unique >= 2 || separate))) ++motion.unverifiedConflicts;
				}
			}
			for (std::size_t i = 0; i < motions.size(); ++i)
				if (locked && motions[i].unique < 2 && !motions[i].unverifiedAlternatives)
					motions[i].identityDisambiguated = IdentityDisambiguates(source, gray, sourceSupport, sourceIdentity, motions, i);
			// 保留最佳原始假设的核验计数，即使它被淘汰也能看到真实失败证据。
			if (!motions.empty()) { const auto& first = motions.front(); stats.expectedBlocks = first.expectedBlocks; stats.verifiedBlocks = first.verifiedBlocks;
				stats.identityTiles = first.identityTiles; stats.identityContradictions = first.identityContradictions; stats.identityScore = first.identityScore; stats.identityDisambiguated = first.identityDisambiguated; }
			stats.verificationMs += Milliseconds(contextBegin); Stage(debug, TrackerDebugStage::PatchVerification, true);
			motions.erase(std::remove_if(motions.begin(), motions.end(), [](const auto& motion) { return motion.patches.size() < 3 || !motion.spread || motion.identityContradictions >= 2; }), motions.end());
			std::stable_sort(motions.begin(), motions.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
			stats.coarseMatchMs = std::max(0.0, Milliseconds(coarseBegin) - stats.verificationMs); stats.candidateCount = static_cast<int>(motions.size());
			Stage(debug, TrackerDebugStage::CoarseMatch, true); Stage(debug, TrackerDebugStage::TargetSelection);
			if (debug)
			{
				debug->localCandidateCount = motions.size(); debug->consensusCandidatesEvaluated = 3 - verificationBudget;
				for (std::size_t i = 0; i < motions.size(); ++i) RecordMotion(debug->localCandidates[i], motions[i]);
			}
			if (motions.empty() && RejectWhen(debug, CheckId::IdentityContradictions, stats.identityContradictions >= 2, stats.identityContradictions, Comparison::GreaterEqual, 2)) return MotionReason::MatchAmbiguous;
			if (RejectWhen(debug, CheckId::IndependentBlocksLow, motions.empty(), motions.empty() ? 0 : static_cast<double>(motions.front().patches.size()), Comparison::Less, 3))
				return MotionReason::InsufficientOverlap;
			std::size_t selected = 0;
			if (!locked)
			{
				// 静态背景不是待选滚动目标；没有唯一运动时保持发现状态。
				for (std::size_t i = 0; i < motions.size(); ++i)
					if (Length(motions[i].flow.x, motions[i].flow.y) > .12) { selected = i; break; }
			}
			const auto& motion = motions[selected];
			if (!locked && Length(motion.flow.x, motion.flow.y) <= .12) return MotionReason::AwaitingMotion;
			stats.matchedTemplates = stats.independentBlocks = static_cast<int>(motion.patches.size()); stats.ambiguityMargin = motion.margin;
			stats.expectedBlocks = motion.expectedBlocks; stats.verifiedBlocks = motion.verifiedBlocks;
			stats.identityTiles = motion.identityTiles; stats.identityContradictions = motion.identityContradictions; stats.identityScore = motion.identityScore; stats.identityDisambiguated = motion.identityDisambiguated;
			if (debug) RecordMotion(debug->mainMotion, motion);
			// 成熟坐标也可能与重复正文别名自洽，不能豁免独立历史身份支撑。
			if (locked && RejectWhen(debug, CheckId::IdentitySupportLow, motion.identityTiles < 3, motion.identityTiles, Comparison::Less, 3))
				return MotionReason::InsufficientOverlap;
			const int unresolved = motion.unique < 2 ? motion.unverifiedAlternatives : motion.unverifiedConflicts;
			if (RejectWhen(debug, CheckId::VerificationBudget, unresolved > 0, unresolved, Comparison::Greater, 0))
				return MotionReason::MatchAmbiguous;
			if (motion.unique < 2 && !motion.identityDisambiguated)
			{
				if (!locked && Length(motion.flow.x, motion.flow.y) <= .12 && motion.meanScore >= .98) return MotionReason::AwaitingMotion;
				RejectWhen(debug, CheckId::MatchUniquenessLow, true, motion.unique, Comparison::Less, 2); return MotionReason::MatchAmbiguous;
			}
			Check(debug, CheckId::MatchUniquenessLow, motion.unique < 2, motion.unique, Comparison::Less, 2);
			for (std::size_t i = 0; i < motions.size(); ++i)
			{
				if (i == selected) continue;
				const auto& other = motions[i];
				if (!locked && Length(other.flow.x, other.flow.y) <= .12) continue;
				if (locked && other.members < 2) continue;
				const double ratio = other.score / std::max(motion.score, 1e-9);
				if (debug)
				{
					++debug->competitorsEvaluated;
					if (!debug->strongestCompetitor.valid || ratio > stats.runnerUpRatio)
					{ RecordMotion(debug->strongestCompetitor, other); debug->strongestCompetitor.originalCellRatio = ratio; }
				}
				stats.runnerUpRatio = std::max(stats.runnerUpRatio, ratio);
				// 发现期两个独立区域的单块质量相当时，采样块数略少不能证明第二目标不存在。
				const double comparableQuality = std::max(.90, motion.meanScore - .03);
				const bool independentTarget = !locked && SeparateRegions(motion, other) && other.patches.size() >= 3
					&& other.unique >= 2 && other.meanScore >= comparableQuality;
				if (independentTarget || (ratio >= .55 && other.patches.size() >= 3 && (other.unique >= 2 || other.identityDisambiguated || (!locked && SeparateRegions(motion, other)))))
				{
					RejectWhen(debug, CheckId::TargetConflict, true, independentTarget ? other.meanScore : ratio, Comparison::GreaterEqual, independentTarget ? comparableQuality : .55);
					if (debug) RecordMotion(debug->rejectingCompetitor, other);
					return MotionReason::TargetAmbiguous;
				}
			}
			if (debug) debug->ambiguityComplete = true;
			Stage(debug, TrackerDebugStage::TargetSelection, true);
			const auto regionBegin = Clock::now();
			const auto candidateSupport = SupportedRegion(source, gray, motion.flow, locked ? sourceSupport | support : cv::Rect{}, motion.sourceBounds | motion.currentBounds);
			stats.verificationMs += Milliseconds(regionBegin);

			for (const auto i : motion.patches)
			{
				const auto& patch = patches[i]; const auto* peak = MatchingPeak(patch, motion.flow);
				if (!peak) continue;
				if (debug)
				{
					auto& output = debug->patches[i]; output.selected = output.independent = true; output.selectedScore = peak->score;
					output.currentBounds = Bounds(cv::Rect(patch.rect.x + cvRound(peak->flow.x), patch.rect.y + cvRound(peak->flow.y), patch.rect.width, patch.rect.height));
					double otherScore = 0;
					for (int j = 0; j < patch.count; ++j) if (Length(patch.peaks[j].flow.x - motion.flow.x, patch.peaks[j].flow.y - motion.flow.y) > 2 * ConsensusRadius) otherScore = std::max(otherScore, patch.peaks[j].score);
					output.uniqueness = peak->score - otherScore;
				}
			}
			Stage(debug, TrackerDebugStage::PatchVerification, true);
			Stage(debug, TrackerDebugStage::Forward); const auto flowBegin = Clock::now();
			std::vector<cv::Point2f> p, q, back, refined;
			std::vector<std::size_t> sourceIndices;
			std::vector<unsigned char> forwardStatus, backwardStatus;
			std::vector<float> errors;
			for (const auto& track : sourceTracks)
			{
				p.push_back(track.point); q.emplace_back(static_cast<float>(track.point.x + motion.flow.x), static_cast<float>(track.point.y + motion.flow.y));
			}
			stats.attempted = static_cast<int>(p.size());
			const cv::TermCriteria criteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, .01);
			if (!p.empty()) cv::calcOpticalFlowPyrLK(source, gray, p, q, forwardStatus, errors, cv::Size(21, 21), 1, criteria, cv::OPTFLOW_USE_INITIAL_FLOW, 1e-4);
			for (std::size_t i = 0; i < p.size(); ++i)
			{
				auto* point = debug && i < debug->pointCount ? &debug->points[i] : nullptr;
				if (point) { point->forwardComputed = true; point->forward = Position(q[i]); point->forwardStatus = forwardStatus[i] != 0; point->forwardInsideEvaluated = forwardStatus[i] != 0; point->forwardInside = Inside(q[i], gray.size()); }
				if (forwardStatus[i] && Inside(q[i], gray.size()))
				{
					++stats.forward; refined.push_back(q[i]); sourceIndices.push_back(i); back.push_back(p[i]);
					if (point) ++debug->cells[point->referenceCell].forward;
				}
			}
			Stage(debug, TrackerDebugStage::Forward, true); Stage(debug, TrackerDebugStage::Refinement);
			if (!refined.empty()) cv::cornerSubPix(gray, refined, cv::Size(3, 3), cv::Size(-1, -1), cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, .001));
			Stage(debug, TrackerDebugStage::Refinement, true); Stage(debug, TrackerDebugStage::Backward);
			if (!refined.empty()) cv::calcOpticalFlowPyrLK(gray, source, refined, back, backwardStatus, errors, cv::Size(21, 21), 1, criteria, cv::OPTFLOW_USE_INITIAL_FLOW, 1e-4);
			std::vector<Match> matches, allMatches, competingMatches;
			std::vector<double> fbErrors;
			std::array<int, CellCount> counts{};
			std::array<bool, CellCount> referenceCells{};
			for (const auto& track : sourceTracks) if (!locked || track.mature) referenceCells[Cell(track.point, gray.size())] = true;
			stats.referenceCells = static_cast<int>(std::count(referenceCells.begin(), referenceCells.end(), true));
			for (std::size_t j = 0; j < refined.size(); ++j)
			{
				const auto i = sourceIndices[j]; auto* point = debug && i < debug->pointCount ? &debug->points[i] : nullptr;
				const double fb = cv::norm(back[j] - p[i]), refinement = cv::norm(refined[j] - q[i]);
				if (point)
				{
					point->refinedComputed = point->backwardComputed = true; point->refined = Position(refined[j]); point->backward = Position(back[j]);
					point->backwardStatus = backwardStatus[j] != 0; point->backwardFiniteEvaluated = backwardStatus[j] != 0; point->backwardFinite = Finite(back[j]);
					point->fbComputed = point->refinementDistanceEvaluated = point->fbThresholdEvaluated = point->refinedInsideEvaluated = true;
					point->fbError = fb; point->refinementDistance = refinement; point->refinedInside = Inside(refined[j], gray.size());
				}
				if (!backwardStatus[j] || !Finite(back[j])) continue;
				++stats.backward;
				if (!Inside(refined[j], gray.size()) || refinement > RefinementDistanceLimit || fb > FbErrorLimit) continue;
				// 大上下文可能淹没另一稀疏运动；原始 FB 点只提供独立像素反证，不能据此提交坐标。
				if (!locked && DistinctPointMotion(source, gray, p[i], refined[j], motion.flow))
					competingMatches.push_back({ i, refined[j], refined[j] - p[i], fb, 1, Cell(p[i], gray.size()) });
				const bool belongs = locked ? sourceTracks[i].mature || Contains(sourceSupport, p[i]) : SupportedPoint(patches, motion, p[i]);
				if (!belongs) continue;
				// 已滚出目标的内容即使在固定 UI 上闭合，也不能进入目标一致性分母或获得接力身份。
				const cv::Point2f predicted(static_cast<float>(p[i].x + motion.flow.x), static_cast<float>(p[i].y + motion.flow.y));
				if (locked && (!Contains(candidateSupport, p[i]) || !Contains(candidateSupport, predicted) || !Contains(candidateSupport, refined[j]))) continue;
				// 光流可在重复工具栏上往返闭合；局部像素未变时不能授予非零运动身份。
				if (Length(motion.flow.x, motion.flow.y) > .12)
				{
					const cv::Rect local(cvRound(p[i].x) - 4, cvRound(p[i].y) - 4, 9, 9);
					if (cv::norm(source(local), gray(local), cv::NORM_INF) <= 1) continue;
				}
				const int cell = Cell(p[i], gray.size());
				Match match{ i, refined[j], refined[j] - p[i], fb, 1, cell };
				allMatches.push_back(match);
				if (!locked || sourceTracks[i].mature) { matches.push_back(match); ++counts[cell]; fbErrors.push_back(fb); }
				if (point) { point->fbValid = true; point->currentCell = Cell(refined[j], gray.size()); ++debug->cells[cell].fbValid; point->nearStationary = cv::norm(match.displacement) < NearStationaryLimit; if (point->nearStationary) ++debug->cells[cell].nearStationary; }
			}
			stats.opticalFlowMs = Milliseconds(flowBegin); Stage(debug, TrackerDebugStage::Backward, true);
			stats.fbValid = static_cast<int>(matches.size()); stats.fbMedian = Percentile(fbErrors, .5); stats.fbP95 = Percentile(fbErrors, .95);
			if (!locked)
			{
				Stage(debug, TrackerDebugStage::Ambiguity);
				const auto independent = IndependentPointMotion(competingMatches);
				Stage(debug, TrackerDebugStage::Ambiguity, true);
				if (RejectWhen(debug, CheckId::IndependentMotion, !independent.empty(), static_cast<double>(independent.size()), Comparison::GreaterEqual, 3))
				{
					if (debug)
					{
						const auto alternate = MedianFlow(competingMatches, independent);
						Candidate(debug->rejectingCompetitor, competingMatches, independent, sourceTracks, alternate);
						++debug->competitorsEvaluated;
						for (const auto i : independent) debug->points[competingMatches[i].track].competitor = true;
					}
					return MotionReason::TargetAmbiguous;
				}
			}
			stats.occupiedCells = static_cast<int>(std::count_if(counts.begin(), counts.end(), [](int n) { return n > 0; }));
			const int expected = locked ? static_cast<int>(std::count_if(sourceTracks.begin(), sourceTracks.end(), [](const auto& track) { return track.mature; })) : static_cast<int>(sourceTracks.size());
			stats.survivalRatio = static_cast<double>(matches.size()) / std::max(1, expected);
			for (auto& match : matches)
			{
				match.weight = 1.0 / std::max(counts[match.cell], 3);
				if (debug) { debug->cells[match.cell].fitWeight += match.weight; debug->cells[match.cell].originalCellWeight += 1.0 / counts[match.cell]; debug->points[match.track].fitWeight = match.weight; }
			}
			Stage(debug, TrackerDebugStage::Consensus); const auto fitBegin = Clock::now();
			auto indices = Inliers(matches, motion.flow);
			MotionVector flow = indices.empty() ? motion.flow : MedianFlow(matches, indices);
			indices = Inliers(matches, flow);
			const bool nonTranslation = NonTranslation(matches, gray.size(), flow, stats, debug);
			if (RejectWhen(debug, CheckId::NonTranslation, nonTranslation, nonTranslation, Comparison::Equal, 1)) { stats.fitMs = Milliseconds(fitBegin); return MotionReason::NonTranslation; }
			stats.inliers = static_cast<int>(indices.size()); stats.pointInlierRatio = static_cast<double>(indices.size()) / std::max<std::size_t>(1, matches.size());
			stats.spatialInlierRatio = Weight(matches, indices) / std::max(1e-9, [&] { double sum = 0; for (const auto& match : matches) sum += match.weight; return sum; }());
			std::vector<double> residuals;
			for (const auto i : indices)
			{
				const auto& match = matches[i]; const double residual = Length(match.displacement.x - flow.x, match.displacement.y - flow.y); residuals.push_back(residual);
				if (debug) { auto& point = debug->points[match.track]; point.residualComputed = point.consensusEvaluated = point.inlier = true; point.residual = residual; ++debug->cells[match.cell].inliers; debug->cells[match.cell].inlierWeight += match.weight; }
			}
			stats.residualMedian = Percentile(residuals, .5); stats.residualP95 = Percentile(residuals, .95);
			Coverage(matches, indices, gray.size(), stats, debug); Stage(debug, TrackerDebugStage::Consensus, true); Stage(debug, TrackerDebugStage::Anchor);
			MotionVector newTotal;
			if (indices.size() >= MinimumPoints)
			{
				if (RejectWhen(debug, CheckId::SpatialRatioLow, stats.spatialInlierRatio < .60, stats.spatialInlierRatio, Comparison::Less, .60)) { stats.fitMs = Milliseconds(fitBegin); return MotionReason::InconsistentTranslation; }
				std::vector<std::pair<double, double>> xs, ys;
				for (const auto i : indices) { const auto& match = matches[i]; const auto& anchor = sourceTracks[match.track].anchor; xs.emplace_back(match.point.x / scaleX - anchor.x, match.weight); ys.emplace_back(match.point.y / scaleY - anchor.y, match.weight); }
				newTotal = { WeightedMedian(std::move(xs)), WeightedMedian(std::move(ys)) };
			}
			else
			{
				// 少点路径仍需三个独立、唯一的图像块；不是降低角点阈值后直接提交。
				if (RejectWhen(debug, CheckId::IndependentBlocksLow, motion.unique < 3, motion.unique, Comparison::Less, 3)
					|| RejectWhen(debug, CheckId::MatchScoreLow, motion.meanScore < .90, motion.meanScore, Comparison::Less, .90))
				{ stats.fitMs = Milliseconds(fitBegin); return MotionReason::InsufficientFeatures; }
				newTotal = { sourceTotal.x + motion.flow.x / scaleX, sourceTotal.y + motion.flow.y / scaleY };
				flow = motion.flow; stats.lowPointFallback = true;
			}
			const double disagreement = Length((newTotal.x - sourceTotal.x) * scaleX - motion.flow.x, (newTotal.y - sourceTotal.y) * scaleY - motion.flow.y);
			if (debug) { debug->candidateTotalValid = true; debug->candidateTotal = newTotal; debug->candidateDelta = { newTotal.x - total.x, newTotal.y - total.y }; }
			if (RejectWhen(debug, CheckId::AnchorDisagreement, disagreement > ConsensusRadius, disagreement, Comparison::Greater, ConsensusRadius)) { stats.fitMs = Milliseconds(fitBegin); return MotionReason::InconsistentTranslation; }
			Stage(debug, TrackerDebugStage::Anchor, true); stats.fitMs = Milliseconds(fitBegin);
			estimate.total = newTotal; estimate.locksTarget = locked || Length(flow.x, flow.y) > .12;
			estimate.support = estimate.locksTarget ? candidateSupport : cv::Rect{};
			const auto identityBegin = Clock::now();
			estimate.identityMask = CertifyIdentity(source, gray, flow, estimate.support, sourceIdentity);
			stats.verificationMs += Milliseconds(identityBegin);
			Stage(debug, TrackerDebugStage::Retention);
			for (const auto& match : allMatches)
			{
				auto track = sourceTracks[match.track];
				if (Length(match.displacement.x - flow.x, match.displacement.y - flow.y) > ConsensusRadius) continue;
				const double anchorError = Length((match.point.x / scaleX - track.anchor.x - newTotal.x) * scaleX,
					(match.point.y / scaleY - track.anchor.y - newTotal.y) * scaleY);
				if (anchorError > ConsensusRadius) continue;
				bool certified = false;
				if (estimate.locksTarget && !track.mature)
				{
					// 新生点必须跨帧随目标产生可辨运动；累计出生位移也支持很小的连续滚动。
					const double evidence = Length((newTotal.x - track.birthTotal.x) * scaleX, (newTotal.y - track.birthTotal.y) * scaleY);
					const bool localMember = estimate.identityMask.at<unsigned char>(cvRound(match.point.y), cvRound(match.point.x)) != 0;
					certified = localMember && (!locked || (evidence > .75 && anchorError < .35));
					if (certified) track.mature = true;
				}
				track.point = match.point; estimate.tracks.push_back(track);
				stats.initialSurvivors += track.birth == 0 ? 1 : 0;
				if (debug) { auto& point = debug->points[match.track]; point.retained = true; point.certified = certified; ++debug->cells[Cell(track.point, gray.size())].retained; }
			}
			if (debug) for (std::size_t i = 0; i < debug->pointCount; ++i) { debug->points[i].retentionEvaluated = true; debug->points[i].retired = !debug->points[i].retained; }
			stats.retained = static_cast<int>(estimate.tracks.size()); stats.retired = static_cast<int>(sourceTracks.size()) - stats.retained;
			Stage(debug, TrackerDebugStage::Retention, true); Stage(debug, TrackerDebugStage::Refill);
			const auto refillBegin = Clock::now();
			stats.added = Refill(gray, estimate.tracks, newTotal, scaleX, scaleY, generation, debug, estimate.support);
			stats.refillMs = Milliseconds(refillBegin); Stage(debug, TrackerDebugStage::Refill, true);
			estimate.confidence = std::clamp(motion.meanScore * std::min(1.0, std::max(motion.unique / 4.0, motion.identityDisambiguated ? .75 : 0.0)) / (1 + stats.fbMedian + stats.residualMedian), 0.0, 1.0);
			return MotionReason::None;
		}
		MotionResult Process(const MotionFrame& frame, TrackerDebugSnapshot* debug)
		{
			MotionStatistics stats{}; stats.inputSequence = frame.sequence; inputTime = frame.timestampSeconds;
			if (RejectWhen(debug, CheckId::TrackingState, state != MotionState::Tracking, static_cast<int>(state), Comparison::NotEqual, static_cast<int>(MotionState::Tracking)))
			{ auto result = Result(stats); result.reason = state == MotionState::Paused ? MotionReason::Paused : MotionReason::NotInitialized; return result; }
			const auto valid = Validate(frame, debug);
			if (valid != MotionReason::None) return Reject(valid, stats, true);
			if (RejectWhen(debug, CheckId::WidthChanged, frame.image.width != width, frame.image.width, Comparison::NotEqual, width)
				|| RejectWhen(debug, CheckId::HeightChanged, frame.image.height != height, frame.image.height, Comparison::NotEqual, height)
				|| RejectWhen(debug, CheckId::FormatChanged, frame.image.format != format, static_cast<int>(frame.image.format), Comparison::NotEqual, static_cast<int>(format))) return Reject(MotionReason::GeometryChanged, stats, true);
			if (RejectWhen(debug, CheckId::TimestampReversed, inputTime < referenceTime, inputTime, Comparison::Less, referenceTime)) return Reject(MotionReason::InvalidTimestamp, stats, true);
			if (rejects)
			{
				// 恢复截止在观测之前执行；晚到的好帧不能越过既定预算再提交。
				const double age = std::max(0.0, inputTime - recoveryStartTime);
				if (RejectWhen(debug, CheckId::RecoveryTimeout, age >= 1.0, age, Comparison::GreaterEqual, 1.0)
					|| RejectWhen(debug, CheckId::RecoveryFramesLimit, rejects + 1 >= 24, rejects + 1, Comparison::GreaterEqual, 24))
				{ Stage(debug, TrackerDebugStage::Recovery, true); return Reject(MotionReason::RecoveryExpired, stats); }
			}
			Stage(debug, TrackerDebugStage::Preprocess); const auto begin = Clock::now(); auto gray = Gray(frame.image);
			stats.preprocessMs = Milliseconds(begin); RecordGeometry(debug, gray, frame); Stage(debug, TrackerDebugStage::Preprocess, true);
			if (Black(gray, debug)) return Reject(MotionReason::BlackFrame, stats, true);
			stats.referenceAgeSeconds = std::max(0.0, inputTime - referenceTime);
			Estimate estimate;
			// 同一输入共享三次候选精查；有旧关键帧时为恢复保留一次，不能在第二参考重新开预算。
			int verificationBudget = 3;
			const bool canRecover = !keyframe.empty() && keyframeSequence != sequence;
			auto reason = Measure(reference, tracks, total, support, identityMask, gray, verificationBudget, canRecover ? 1 : 0, stats, debug, estimate);
			stats.primaryReason = reason;
			if (reason != MotionReason::None && reason != MotionReason::AwaitingMotion && canRecover && verificationBudget > 0)
			{
				// 只回到有可信内容坐标的关键帧；拒绝图像永远不能成为新的世界原点。
				const MotionStatistics previousStats = stats;
				const auto preprocess = stats.preprocessMs;
				stats = {}; stats.inputSequence = frame.sequence; stats.preprocessMs = preprocess; stats.usedKeyframe = true; stats.primaryReason = reason;
				stats.referenceAgeSeconds = std::max(0.0, inputTime - keyframeTime);
				if (debug)
				{
					BeginDebug(frame, debug, false); RecordGeometry(debug, gray, frame);
					debug->referenceSequenceBefore = keyframeSequence; debug->referenceGenerationBefore = keyframeGeneration; debug->pointCount = 0; debug->cells = {};
					RecordTracks(debug, keyframeTracks, keyframe.size());
					Stage(debug, TrackerDebugStage::Validation, true); Stage(debug, TrackerDebugStage::Preprocess, true); Stage(debug, TrackerDebugStage::Recovery);
				}
				estimate = {};
				reason = Measure(keyframe, keyframeTracks, keyframeTotal, keyframeSupport, keyframeIdentityMask, gray, verificationBudget, 0, stats, debug, estimate);
				if (reason == MotionReason::None)
				{
					// 旧关键帧给出的世界坐标还需与最近可信参考闭环，不能凭相似段落切换身份。
					const auto verifyBegin = Clock::now();
					const auto latestPatches = GatherPatches(reference, gray, tracks, support, true, identityMask);
					const MotionVector latestFlow{ (estimate.total.x - total.x) * scaleX, (estimate.total.y - total.y) * scaleY };
					int expected = 0, verified = 0;
					const bool visibleBlocks = VerifyTargetBlocks(reference, gray, latestPatches, latestFlow, support, identityMask, expected, verified);
					if (!visibleBlocks)
					{
						RejectWhen(debug, CheckId::TargetConflict, true, static_cast<double>(verified) / std::max(3, expected), Comparison::Less, .75);
						stats.expectedBlocks = expected; stats.verifiedBlocks = verified; reason = MotionReason::TargetAmbiguous;
					}
					LocalMotion latestMotion; latestMotion.flow = latestFlow;
					const auto latestPatterns = IdentityPatterns(reference, support, identityMask, tracks);
					VerifyIdentity(reference, gray, support, identityMask, latestPatterns, IdentityThreshold(latestPatterns), latestMotion);
					if (RejectWhen(debug, CheckId::IdentityContradictions, latestMotion.identityContradictions >= 2,
						latestMotion.identityContradictions, Comparison::GreaterEqual, 2)) reason = MotionReason::MatchAmbiguous;
					if (RejectWhen(debug, CheckId::IdentitySupportLow, latestMotion.identityTiles < 3,
						latestMotion.identityTiles, Comparison::Less, 3)) reason = MotionReason::InsufficientOverlap;
					const int identityCount = stats.identityTiles + latestMotion.identityTiles;
					if (identityCount) stats.identityScore = (stats.identityScore * stats.identityTiles + latestMotion.identityScore * latestMotion.identityTiles) / identityCount;
					stats.identityTiles = identityCount; stats.identityContradictions += latestMotion.identityContradictions;
					if (reason == MotionReason::None) estimate.identityMask = CertifyIdentity(reference, gray, latestFlow, estimate.support, identityMask);
					stats.verificationMs += Milliseconds(verifyBegin);
				}
				stats.coarseMatchMs += previousStats.coarseMatchMs; stats.opticalFlowMs += previousStats.opticalFlowMs;
				stats.verificationMs += previousStats.verificationMs; stats.fitMs += previousStats.fitMs; stats.refillMs += previousStats.refillMs;
				Stage(debug, TrackerDebugStage::Recovery, true);
			}
			if (debug) debug->consensusCandidatesEvaluated = 3 - verificationBudget;
			if (reason == MotionReason::AwaitingMotion)
			{
				// 尚未识别目标时允许静态/周期内容等待；没有提交零位移来掩盖歧义。
				auto result = Result(stats); result.reason = reason; return result;
			}
			if (reason != MotionReason::None)
			{
				Stage(debug, TrackerDebugStage::Recovery);
				const double recoveryAge = rejects ? std::max(0.0, inputTime - recoveryStartTime) : 0;
				Check(debug, CheckId::RecoveryTimeout, recoveryAge >= 1.0, recoveryAge, Comparison::GreaterEqual, 1.0);
				Stage(debug, TrackerDebugStage::Recovery, true); return Reject(reason, stats);
			}
			const MotionVector delta{ estimate.total.x - total.x, estimate.total.y - total.y };
			bool refreshedIdentity = false;
			const MotionVector latestFlow{ delta.x * scaleX, delta.y * scaleY };
			const MotionVector accumulatedFlow{ (estimate.total.x - keyframeTotal.x) * scaleX, (estimate.total.y - keyframeTotal.y) * scaleY };
			// 最近邻成员图在不足半像素时不能搬运；用可信关键帧的累计运动补足辨识证据。
			// 只维护已经通过全部接受门的同一位移，不增加候选，也不让静止一致独自认证新成员。
			if (targetId && canRecover && std::abs(latestFlow.x) < .5 && std::abs(latestFlow.y) < .5
				&& Length(accumulatedFlow.x, accumulatedFlow.y) >= 1.0
				&& Length((estimate.total.x - identityRefreshTotal.x) * scaleX, (estimate.total.y - identityRefreshTotal.y) * scaleY) >= 1.0)
			{
				const auto identityBegin = Clock::now();
				// 空历史要求每个补充像素都赢过零流，再经当前参考复核，不能复活动画孔洞。
				const auto accumulatedMembers = CertifyIdentity(keyframe, gray, accumulatedFlow, estimate.support & keyframeSupport, {});
				const auto currentMembers = CertifyIdentity(reference, gray, latestFlow, estimate.support, accumulatedMembers);
				cv::bitwise_or(estimate.identityMask, currentMembers, estimate.identityMask);
				stats.verificationMs += Milliseconds(identityBegin); refreshedIdentity = true;
			}
			Stage(debug, TrackerDebugStage::Commit);
			if (estimate.locksTarget)
			{
				if (!targetId) { targetId = 1; identityRefreshTotal = estimate.total; }
				else if (refreshedIdentity) identityRefreshTotal = estimate.total;
				if (keyframe.empty())
				{ keyframe = gray; keyframeTracks = estimate.tracks; keyframeTotal = estimate.total; keyframeTime = inputTime; keyframeSequence = frame.sequence; keyframeGeneration = generation + 1; keyframeSupport = estimate.support; keyframeIdentityMask = estimate.identityMask; }
				else if (!stats.usedKeyframe && Length((total.x - keyframeTotal.x) * scaleX, (total.y - keyframeTotal.y) * scaleY) > .3 * std::max(24, std::min(support.width, support.height)))
				{ keyframe = reference; keyframeTracks = tracks; keyframeTotal = total; keyframeTime = referenceTime; keyframeSequence = sequence; keyframeGeneration = generation; keyframeSupport = support; keyframeIdentityMask = identityMask; }
			}
			reference = std::move(gray); tracks = std::move(estimate.tracks); total = estimate.total; support = estimate.support; identityMask = std::move(estimate.identityMask);
			sequence = frame.sequence; referenceTime = inputTime; ++generation; rejects = 0;
			phase = targetId ? MotionTargetPhase::Locked : MotionTargetPhase::Discovering;
			auto result = Result(stats); result.accepted = true; result.reason = MotionReason::None; result.delta = delta; result.confidence = estimate.confidence;
			Stage(debug, TrackerDebugStage::Commit, true); return result;
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
			INKEYS_DEBUG_STAGE(CoarseMatch); INKEYS_DEBUG_STAGE(TargetSelection); INKEYS_DEBUG_STAGE(PatchVerification); INKEYS_DEBUG_STAGE(Recovery);
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
			INKEYS_DEBUG_CHECK(BlackMaximum, "maximum gray <= 2"); INKEYS_DEBUG_CHECK(InitialPointsLow, "initial points < 8");
			INKEYS_DEBUG_CHECK(ForwardPointsLow, "Forward valid points < 8"); INKEYS_DEBUG_CHECK(FbPointsLow, "FB valid points < 8");
			INKEYS_DEBUG_CHECK(AffineCellsLow, "affine cells < 16 (skip diagnostic fit)");
			INKEYS_DEBUG_CHECK(AffineSolveFailed, "affine solve failed (skip diagnostic fit)");
			INKEYS_DEBUG_CHECK(AffineVariationHigh, "target-local nontranslation operand: variation > .7");
			INKEYS_DEBUG_CHECK(AffineResidualLow, "nontranslation operand: affine residual < .55 * translation residual");
			INKEYS_DEBUG_CHECK(AffineSupportHigh, "target-local nontranslation operand: supported points / points >= .75");
			INKEYS_DEBUG_CHECK(NonTranslation, "NonTranslation: variation AND residual AND support");
			INKEYS_DEBUG_CHECK(ZeroModeAmbiguity, "zero-main AND other ratio > .15 AND interior >= 6 (last evaluated candidate)");
			INKEYS_DEBUG_CHECK(RunnerUpHigh, "runner-up original-cell ratio > .35"); INKEYS_DEBUG_CHECK(InliersLow, "inliers < 8");
			INKEYS_DEBUG_CHECK(SpatialRatioLow, "target-local spatial fit ratio < .60"); INKEYS_DEBUG_CHECK(SurvivalRatioLow, "survival ratio < .35");
			INKEYS_DEBUG_CHECK(CoverageCells, "coverage AND: inlier cells >= 16"); INKEYS_DEBUG_CHECK(CoverageInterior, "coverage AND: interior cells >= 8");
			INKEYS_DEBUG_CHECK(CoverageColumns, "coverage AND: columns >= 4"); INKEYS_DEBUG_CHECK(CoverageRows, "coverage AND: rows >= 3");
			INKEYS_DEBUG_CHECK(CoverageX, "coverage AND: X span >= .5"); INKEYS_DEBUG_CHECK(CoverageY, "coverage AND: Y span >= .4");
			INKEYS_DEBUG_CHECK(JumpHigh, "legacy global jump gate (unused)"); INKEYS_DEBUG_CHECK(AnchorDisagreement, "anchor delta vs block motion distance > 1.5");
			INKEYS_DEBUG_CHECK(TemplatesLow, "local templates < 3"); INKEYS_DEBUG_CHECK(IndependentBlocksLow, "independent two-dimensional blocks < 3");
			INKEYS_DEBUG_CHECK(MatchScoreLow, "low-point block score < .90"); INKEYS_DEBUG_CHECK(MatchUniquenessLow, "unique blocks < 2");
			INKEYS_DEBUG_CHECK(TargetConflict, "independent target conflict"); INKEYS_DEBUG_CHECK(RecoveryTimeout, "untrusted interval >= 1 second");
			INKEYS_DEBUG_CHECK(IdentityContradictions, "identity tile contradictions >= 2"); INKEYS_DEBUG_CHECK(RecoveryFramesLimit, "recovery inputs >= 24");
			INKEYS_DEBUG_CHECK(IndependentMotion, "independent pixel motion blocks >= 3");
			INKEYS_DEBUG_CHECK(IdentitySupportLow, "historical identity tiles < 3");
			INKEYS_DEBUG_CHECK(VerificationBudget, "competitive coarse hypotheses not verified");
#undef INKEYS_DEBUG_CHECK
		default: return "None";
		}
	}
	const char* MotionTargetPhaseName(MotionTargetPhase phase) noexcept
	{
		switch (phase)
		{
		case MotionTargetPhase::Discovering: return "Discovering";
		case MotionTargetPhase::Locked: return "Locked";
		case MotionTargetPhase::Recovering: return "Recovering";
		}
		return "Unknown";
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
			INKEYS_MOTION_REASON(AwaitingMotion); INKEYS_MOTION_REASON(TargetAmbiguous); INKEYS_MOTION_REASON(MatchAmbiguous);
			INKEYS_MOTION_REASON(InsufficientOverlap); INKEYS_MOTION_REASON(RecoveryExpired);
#undef INKEYS_MOTION_REASON
		}
		return "Unknown";
	}
}
