module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
#include <vector>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#pragma comment(lib, "opencv_core4.lib")
#pragma comment(lib, "opencv_imgproc4.lib")

module Inkeys.CV.ShapeRecognition;

namespace Inkeys::CV
{
	namespace
	{
		constexpr ShapeRecognitionThresholds kThresholds = {};
		constexpr std::size_t kMaximumSourcePoints = 1024 * 1024;
		constexpr float kPi = 3.14159265358979323846f;
		using Reason = ShapeRecognitionRejectReason;

		struct SampledStroke
		{
			std::vector<cv::Point2f> points;
			std::vector<float> widths;
			float length = 0;
		};

		struct PreparedInk
		{
			std::vector<SampledStroke> strokes;
			std::vector<cv::Point2f> points;
			std::vector<float> weights;
			cv::Point2d origin;
			ShapeRecognitionContext context;
			float medianWidth = 0;
			float length = 0;
		};

		struct Model
		{
			ShapeModelKind kind = ShapeModelKind::Rectangle;
			std::vector<cv::Point2f> vertices;
			std::vector<float> arc;
			float perimeter = 0;
			float shortSide = 0;
			float longSide = 0;
			float angle = 0;
			bool closed = true;
			// 椭圆按解析曲线投影，折线仅用于有界的周长参数化。
			cv::RotatedRect ellipse;
		};

		struct Projection
		{
			float distance = (std::numeric_limits<float>::max)();
			float arc = 0;
			std::size_t edge = 0;
			cv::Point2f normal = { 0, 1 };
		};

		struct Interval { float begin; float end; };

		struct Metrics
		{
			ShapeHypothesis hypothesis;
			std::array<float, 4> edgeCoverage = {};
			std::array<float, 4> edgeGap = {};
			std::array<float, 4> edgeStartGap = {};
			std::array<float, 4> edgeEndGap = {};
			std::array<float, 4> localDeviation = {};
			std::array<float, 4> fitDeviation = {};
			std::array<std::vector<float>, 4> distances;
			std::array<float, 6> strokeInlier = {};
			std::array<std::array<float, 4>, 6> contribution = {};
			std::array<float, 4> coveredLength = {};
			std::array<Reason, 24> failures = {};
			std::size_t failureCount = 0;
			float tolerance = 0;
			float minimumStrokeInlier = 1;
			float maximumDeviation = 0;
			float averageDeviation = 0;
			float cornerScore = 1;
			float cornerTransition = 0;
			float endpointClosureGap = 0;
			float cornerCoverageAllowance = 0;
		};

		float Length(cv::Point2f p) { return std::hypot(p.x, p.y); }
		float Dot(cv::Point2f a, cv::Point2f b) { return a.x * b.x + a.y * b.y; }
		float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
		bool Finite(cv::Point2f p) { return std::isfinite(p.x) && std::isfinite(p.y); }

		float Percentile(std::vector<float> values, float q)
		{
			if (values.empty()) return 0;
			std::sort(values.begin(), values.end());
			const float position = q * static_cast<float>(values.size() - 1);
			const auto index = static_cast<std::size_t>(position);
			return values[index] + (values[std::min(index + 1, values.size() - 1)] - values[index]) *
				(position - static_cast<float>(index));
		}

		float WeightedMedian(std::vector<std::pair<float, float>>& values)
		{
			std::sort(values.begin(), values.end());
			double total = 0;
			for (const auto& value : values) total += value.second;
			double sum = 0;
			for (const auto& value : values)
			{
				sum += value.second;
				if (sum >= total * 0.5) return value.first;
			}
			return values.back().first;
		}

		cv::Point2f ToDip(const InkPoint& p, const PreparedInk& ink)
		{
			return { static_cast<float>((static_cast<double>(p.x) - ink.origin.x) * ink.context.dipPerPixelX),
				static_cast<float>((static_cast<double>(p.y) - ink.origin.y) * ink.context.dipPerPixelY) };
		}

		InkPoint ToPixel(cv::Point2f p, const PreparedInk& ink)
		{
			return { static_cast<float>(p.x / ink.context.dipPerPixelX + ink.origin.x),
				static_cast<float>(p.y / ink.context.dipPerPixelY + ink.origin.y), 0 };
		}

		Reason Prepare(std::span<const InkStrokeView> input, const ShapeRecognitionContext& context,
			PreparedInk& ink, ShapeRecognitionDiagnostics* diagnostics)
		{
			if (input.empty() || input.size() > kThresholds.maximumStrokeCount) return Reason::InvalidStrokeCount;
			if (!std::isfinite(context.dipPerPixelX) || !std::isfinite(context.dipPerPixelY) ||
				context.dipPerPixelX <= 0 || context.dipPerPixelY <= 0) return Reason::InvalidDpiScale;
			if (input.front().points.empty()) return Reason::StrokeResamplingFailed;
			ink.context = context;
			ink.context.physicalScaleTrusted = context.physicalScaleTrusted &&
				std::isfinite(context.millimetersPerDipX) && std::isfinite(context.millimetersPerDipY) &&
				context.millimetersPerDipX >= 0.01f && context.millimetersPerDipY >= 0.01f &&
				context.millimetersPerDipX <= 10 && context.millimetersPerDipY <= 10;
			ink.context.scaleFallbackReason = ink.context.physicalScaleTrusted ? ShapeScaleFallbackReason::None :
				context.physicalScaleTrusted ? ShapeScaleFallbackReason::InvalidPhysicalScale : context.scaleFallbackReason;
			ink.origin = { input.front().points.front().x, input.front().points.front().y };
			std::size_t sourceCount = 0;
			std::vector<float> widths;
			const float widthScale = std::sqrt(context.dipPerPixelX) * std::sqrt(context.dipPerPixelY);
			// 先给每笔保留预算，避免较长的前几笔耗尽总点数而丢掉闭合边。
			const std::size_t budget = std::min(kThresholds.maximumPointsPerStroke,
				kThresholds.maximumTotalSampledPoints / input.size());
			for (const auto& source : input)
			{
				if (source.points.size() > kMaximumSourcePoints - sourceCount) return Reason::SourcePointLimit;
				sourceCount += source.points.size();
				if (source.points.size() < 2) return Reason::StrokeResamplingFailed;
				std::vector<double> arc(source.points.size(), 0);
				bool positiveWidth = false;
				for (std::size_t i = 0; i < source.points.size(); ++i)
				{
					const auto& p = source.points[i];
					if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.width) || p.width < 0)
						return Reason::InvalidSampleSet;
					positiveWidth |= p.width > 0;
					if (i) arc[i] = arc[i - 1] + Length(ToDip(p, ink) - ToDip(source.points[i - 1], ink));
				}
				if (!positiveWidth) return Reason::InvalidMedianWidth;
				const double length = arc.back();
				if (!std::isfinite(length) || length <= 0 || length > (std::numeric_limits<float>::max)())
					return Reason::StrokeResamplingFailed;
				const std::size_t count = static_cast<std::size_t>(std::clamp(
					std::ceil(length / kThresholds.resampleSpacingDip) + 1, 2.0, static_cast<double>(budget)));
				SampledStroke stroke;
				stroke.length = static_cast<float>(length);
				std::size_t segment = 1;
				for (std::size_t i = 0; i < count; ++i)
				{
					const double target = length * i / static_cast<double>(count - 1);
					while (segment + 1 < arc.size() && arc[segment] < target) ++segment;
					const double span = arc[segment] - arc[segment - 1];
					const float t = span > 0 ? static_cast<float>(std::clamp((target - arc[segment - 1]) / span, 0.0, 1.0)) : 0;
					const auto a = ToDip(source.points[segment - 1], ink);
					const auto b = ToDip(source.points[segment], ink);
					const cv::Point2f point = a + (b - a) * t;
					const float width = (source.points[segment - 1].width * (1 - t) + source.points[segment].width * t) * widthScale;
					if (!Finite(point) || !std::isfinite(width)) return Reason::InvalidSampleSet;
					stroke.points.push_back(point);
					stroke.widths.push_back(width);
					if (width > 0) widths.push_back(width);
					ink.points.push_back(point);
					const float step = static_cast<float>(length / static_cast<double>(count - 1));
					ink.weights.push_back(step * ((i == 0 || i + 1 == count) ? 0.5f : 1.0f));
				}
				ink.length += stroke.length;
				ink.strokes.push_back(std::move(stroke));
			}
			if (widths.empty()) return Reason::InvalidMedianWidth;
			ink.medianWidth = Percentile(std::move(widths), 0.5f);
			if (diagnostics)
			{
				diagnostics->sourcePointCount = sourceCount;
				diagnostics->sampledPointCount = ink.points.size();
				diagnostics->context = ink.context;
				const float pixelScale = 1 / widthScale;
				diagnostics->medianWidth = ink.medianWidth * pixelScale;
				diagnostics->sourcePathLength = ink.length * pixelScale;
				for (std::size_t s = 0; s < ink.strokes.size(); ++s)
				{
					auto& report = diagnostics->strokes[s];
					report.sourcePointCount = input[s].points.size();
					report.sampledPointCount = ink.strokes[s].points.size();
					report.pathLength = ink.strokes[s].length * pixelScale;
					for (std::size_t i = 0; i < ink.strokes[s].points.size(); ++i)
					{
						auto point = ToPixel(ink.strokes[s].points[i], ink);
						point.width = ink.strokes[s].widths[i] * pixelScale;
						report.sampledPoints.push_back(point);
					}
				}
			}
			return Reason::None;
		}

		bool FinishModel(Model& model)
		{
			model.arc = { 0 };
			model.perimeter = 0;
			const std::size_t edges = model.closed ? model.vertices.size() : model.vertices.size() - 1;
			for (std::size_t i = 0; i < edges; ++i)
			{
				const float length = Length(model.vertices[(i + 1) % model.vertices.size()] - model.vertices[i]);
				if (!std::isfinite(length) || length < 0.001f) return false;
				model.perimeter += length;
				model.arc.push_back(model.perimeter);
			}
			if (model.closed)
			{
				const auto box = cv::minAreaRect(model.vertices);
				model.shortSide = std::min(box.size.width, box.size.height);
				model.longSide = std::max(box.size.width, box.size.height);
			}
			else model.longSide = model.perimeter;
			return std::isfinite(model.perimeter) && model.perimeter > 0;
		}

		Projection Project(const Model& model, cv::Point2f point)
		{
			Projection result;
			if (model.kind == ShapeModelKind::Ellipse)
			{
				const float angle = model.ellipse.angle * kPi / 180;
				const float c = std::cos(angle), s = std::sin(angle);
				const auto delta = point - model.ellipse.center;
				const float x = delta.x * c + delta.y * s, y = -delta.x * s + delta.y * c;
				const float a = model.ellipse.size.width * 0.5f, b = model.ellipse.size.height * 0.5f;
				float theta = std::atan2(y / b, x / a);
				for (int iteration = 0; iteration < 6; ++iteration)
				{
					const float ct = std::cos(theta), st = std::sin(theta);
					const float f = (b * b - a * a) * st * ct + a * x * st - b * y * ct;
					const float df = (b * b - a * a) * (ct * ct - st * st) + a * x * ct + b * y * st;
					if (std::abs(df) < 1e-6f) break;
					theta -= std::clamp(f / df, -0.5f, 0.5f);
				}
				theta = std::fmod(theta + 4 * kPi, 2 * kPi);
				const float ct = std::cos(theta), st = std::sin(theta);
				result.distance = std::hypot(x - a * ct, y - b * st);
				const float position = theta * static_cast<float>(model.vertices.size()) / (2 * kPi);
				result.edge = std::min(static_cast<std::size_t>(position), model.vertices.size() - 1);
				result.arc = model.arc[result.edge] + (position - static_cast<float>(result.edge)) *
					(model.arc[result.edge + 1] - model.arc[result.edge]);
				cv::Point2f normal{ ct / a, st / b };
				normal *= 1 / Length(normal);
				result.normal = { normal.x * c - normal.y * s, normal.x * s + normal.y * c };
				return result;
			}
			float nearestSquared = (std::numeric_limits<float>::max)();
			for (std::size_t i = 0; i + 1 < model.arc.size(); ++i)
			{
				const auto a = model.vertices[i];
				const auto delta = model.vertices[(i + 1) % model.vertices.size()] - a;
				const float length = model.arc[i + 1] - model.arc[i];
				const float t = std::clamp(Dot(point - a, delta) / (length * length), 0.0f, 1.0f);
				const auto offset = point - a - delta * t;
				const float squared = Dot(offset,offset);
				if (squared < nearestSquared)
				{
					nearestSquared = squared;
					result.edge = i;
					result.arc = model.arc[i] + t * length;
					result.normal = { -delta.y / length, delta.x / length };
				}
			}
			result.distance = std::sqrt(nearestSquared);
			return result;
		}

		float Tolerance(float base, cv::Point2f normal, const PreparedInk& ink)
		{
			if (!ink.context.physicalScaleTrusted) return base;
			// 各方向统一使用同一个物理误差先验，且只允许有限地修正 DIP 容差。
			const float physical = kThresholds.physicalNoiseMillimeters * std::hypot(
				normal.x / ink.context.millimetersPerDipX, normal.y / ink.context.millimetersPerDipY);
			return std::clamp(physical, base * 0.75f, std::min(base * 1.25f, kThresholds.maximumEdgeBandDip));
		}

		Model Rectangle(float left, float top, float right, float bottom, float angle)
		{
			Model result;
			result.angle = angle;
			const cv::Point2f u{ std::cos(angle), std::sin(angle) }, v{ -u.y, u.x };
			result.vertices = { u * left + v * top, u * right + v * top,
				u * right + v * bottom, u * left + v * bottom };
			FinishModel(result);
			return result;
		}

		bool FitRectangle(const std::vector<cv::Point2f>& seed, const PreparedInk& ink, Model& result)
		{
			const auto delta = seed[1] - seed[0];
			float angle = std::atan2(delta.y, delta.x);
			angle = std::atan2(std::sin(4 * angle), std::cos(4 * angle)) * 0.25f;
			cv::Point2f u{ std::cos(angle), std::sin(angle) }, v{ -u.y, u.x };
			float left = Dot(seed[0], u), right = left, top = Dot(seed[0], v), bottom = top;
			for (const auto p : seed)
			{
				left = std::min(left, Dot(p, u)); right = std::max(right, Dot(p, u));
				top = std::min(top, Dot(p, v)); bottom = std::max(bottom, Dot(p, v));
			}
			result = Rectangle(left, top, right, bottom, angle);
			for (std::size_t iteration = 0; iteration < kThresholds.maximumFitIterations; ++iteration)
			{
				std::array<std::vector<cv::Point2f>, 4> groups;
				std::array<std::vector<std::size_t>, 4> indices;
				for (std::size_t i = 0; i < ink.points.size(); ++i)
				{
					const auto edge = Project(result, ink.points[i]).edge;
					groups[edge].push_back(ink.points[i]); indices[edge].push_back(i);
				}
				double c = 0, s = 0;
				for (std::size_t edge = 0; edge < 4; ++edge)
				{
					if (groups[edge].size() < 2) return false;
					cv::Vec4f line;
					cv::fitLine(groups[edge], line, cv::DIST_HUBER, 0, 0.01, 0.001);
					const double direction = std::atan2(line[1], line[0]);
					const float weight = result.arc[edge + 1] - result.arc[edge];
					c += std::cos(4 * direction) * weight; s += std::sin(4 * direction) * weight;
				}
				angle = static_cast<float>(std::atan2(s, c) * 0.25);
				u = { std::cos(angle), std::sin(angle) }; v = { -u.y, u.x };
				std::array<float, 4> positions = {};
				for (std::size_t edge = 0; edge < 4; ++edge)
				{
					std::vector<std::pair<float, float>> values;
					for (const auto i : indices[edge]) values.emplace_back(Dot(ink.points[i], edge % 2 ? u : v), ink.weights[i]);
					positions[edge] = WeightedMedian(values);
				}
				if (positions[1] <= positions[3] || positions[2] <= positions[0]) return false;
				result = Rectangle(positions[3], positions[0], positions[1], positions[2], angle);
			}
			return result.shortSide > 0.001f;
		}

		bool FitPolygon(const std::vector<cv::Point2f>& seed, const PreparedInk& ink, Model& result)
		{
			result.kind = seed.size() == 3 ? ShapeModelKind::Triangle : ShapeModelKind::Quadrilateral;
			result.vertices = seed;
			if (!FinishModel(result)) return false;
			for (std::size_t iteration = 0; iteration < kThresholds.maximumFitIterations; ++iteration)
			{
				std::vector<std::vector<cv::Point2f>> groups(seed.size());
				for (const auto point : ink.points) groups[Project(result, point).edge].push_back(point);
				std::vector<cv::Vec4f> lines(seed.size());
				for (std::size_t e = 0; e < seed.size(); ++e)
				{
					if (groups[e].size() < 2) return false;
					cv::fitLine(groups[e], lines[e], cv::DIST_HUBER, 0, 0.01, 0.001);
				}
				for (std::size_t e = 0; e < seed.size(); ++e)
				{
					const auto a = lines[(e + seed.size() - 1) % seed.size()], b = lines[e];
					const float det = a[0] * b[1] - a[1] * b[0];
					if (std::abs(det) < 0.05f) return false;
					const float t = ((b[2] - a[2]) * b[1] - (b[3] - a[3]) * b[0]) / det;
					result.vertices[e] = { a[2] + a[0] * t, a[3] + a[1] * t };
					if (!Finite(result.vertices[e])) return false;
				}
				if (!cv::isContourConvex(result.vertices) || !FinishModel(result)) return false;
			}
			return true;
		}

		float MergeCoverage(const std::vector<Interval>& intervals, float begin, float end,
			float* gap = nullptr, float* startGap = nullptr, float* endGap = nullptr, float* interiorGap = nullptr)
		{
			float cursor = begin, covered = 0, largest = 0;
			float first = end, last = begin, interior = 0;
			for (const auto interval : intervals)
			{
				const float a = std::clamp(interval.begin, begin, end), b = std::clamp(interval.end, begin, end);
				if (b <= a) continue;
				if (last > begin) interior = std::max(interior, a - cursor);
				first = std::min(first, a); last = std::max(last, b);
				largest = std::max(largest, a - cursor);
				covered += std::max(0.0f, b - std::max(cursor, a)); cursor = std::max(cursor, b);
			}
			largest = std::max(largest, end - cursor);
			if (gap) *gap = largest;
			if (startGap) *startGap = first - begin;
			if (endGap) *endGap = end - last;
			if (interiorGap) *interiorGap = interior;
			return covered;
		}

		Interval FindEndpointClosureGap(const Model& model, const PreparedInk& ink,
			const std::vector<Interval>& intervals, float tolerance)
		{
			struct Endpoint { cv::Point2f point; Projection projection; };
			std::array<Endpoint, 12> endpoints;
			std::size_t count = 0;
			for (const auto& stroke : ink.strokes)
				for (const auto point : {stroke.points.front(),stroke.points.back()})
				{
					const auto projection = Project(model,point);
					if (projection.distance <= Tolerance(tolerance,projection.normal,ink))
						endpoints[count++] = {point,projection};
				}
			Interval best = {};
			const float maximumArc = std::max(2 * tolerance, kThresholds.maximumClosureArcRatio * model.shortSide);
			const float maximumDistance = std::max(2 * tolerance, kThresholds.maximumClosureDistanceRatio * model.shortSide);
			const auto nearArc = [&](float a, float b)
			{
				const float distance = std::fmod(std::abs(a-b),model.perimeter);
				return std::min(distance,model.perimeter-distance) <= 2 * kThresholds.resampleSpacingDip;
			};
			const auto consider = [&](float begin, float end)
			{
				if (end-begin <= best.end-best.begin || end-begin > maximumArc) return;
				for (std::size_t a = 0; a < count; ++a)
				{
					if (!nearArc(endpoints[a].projection.arc,begin)) continue;
					for (std::size_t b = 0; b < count; ++b)
						if (a != b && nearArc(endpoints[b].projection.arc,end) &&
							Length(endpoints[a].point-endpoints[b].point) <= maximumDistance)
						{
							best = {begin,end}; return;
						}
				}
			};
			float first = model.perimeter, cursor = 0;
			bool covered = false;
			for (const auto interval : intervals)
			{
				if (interval.end <= interval.begin) continue;
				if (!covered) first = interval.begin;
				else if (interval.begin > cursor) consider(cursor,interval.begin);
				cursor = std::max(cursor,interval.end); covered = true;
			}
			if (covered) consider(cursor,model.perimeter+first);
			return best;
		}

		void Fail(Metrics& metrics, Reason reason)
		{
			if (metrics.hypothesis.rejectionReason == Reason::None) metrics.hypothesis.rejectionReason = reason;
			if (metrics.failureCount < metrics.failures.size()) metrics.failures[metrics.failureCount++] = reason;
		}

		void CheckStraightness(const Model& model, const PreparedInk& ink,
			std::span<const InkStrokeView> source, Metrics& metrics)
		{
			const std::size_t edgeCount = model.arc.size()-1;
			std::array<double, 4> sum = {}, length = {};
			std::array<std::vector<cv::Point2f>, 4> groups;
			for (const auto p : ink.points)
			{
				const auto projection = Project(model, p);
				if (projection.distance <= 2 * metrics.tolerance) groups[projection.edge].push_back(p);
			}
			const float windowLength = std::max(kThresholds.minimumLongSegmentDip, ink.length / 4096.0f);
			std::size_t windows = 0;
			const auto measureWindow = [&](cv::Point2f a, cv::Point2f b)
			{
				const auto delta = b - a;
				const float chord = Length(delta);
				if (chord <= 0.001f) return;
				const auto projection = Project(model, (a + b) * 0.5f);
				const float along = projection.arc-model.arc[projection.edge];
				const float edgeLength = model.arc[projection.edge+1]-model.arc[projection.edge];
				const float cornerLength = std::max(windowLength,metrics.cornerTransition);
				if (along < cornerLength || edgeLength-along < cornerLength) return;
				// 跨越真实拐角的窗口不属于某一条直边，不能当作局部偏轴。
				if (Project(model,a).edge != projection.edge || Project(model,b).edge != projection.edge ||
					projection.distance > 2 * metrics.tolerance) return;
				const float deviation = std::asin(std::clamp(std::abs(Dot(delta,projection.normal)) / chord, 0.0f, 1.0f)) * 180 / kPi;
				sum[projection.edge] += deviation * windowLength; length[projection.edge] += windowLength;
				metrics.maximumDeviation = std::max(metrics.maximumDeviation,deviation);
			};
			// 固定弧长取窗口，同一锯齿不会因原始采样点变密而绕过直线性检查。
			for (const auto& stroke : source)
			{
				auto start = ToDip(stroke.points.front(),ink), previous = start;
				float remaining = windowLength;
				for (std::size_t i = 1; i < stroke.points.size() && windows < 4096; ++i)
				{
					auto a = previous;
					const auto b = ToDip(stroke.points[i],ink);
					float segment = Length(b-a);
					while (segment >= remaining && windows < 4096)
					{
						const auto end = a + (b-a) * (remaining/segment);
						measureWindow(start,end); ++windows;
						a = start = end; segment -= remaining; remaining = windowLength;
					}
					remaining -= segment; previous = b;
				}
			}
			std::array<cv::Point2f,4> directions = {};
			std::array<bool,4> fitted = {};
			double total = 0, totalLength = 0;
			for (std::size_t e = 0; e < edgeCount; ++e)
			{
				if (length[e] > 0) metrics.localDeviation[e] = static_cast<float>(sum[e] / length[e]);
				if (metrics.localDeviation[e] > kThresholds.maximumAverageAxisDeviationDegrees)
					Fail(metrics, Reason::AxisDeviationTooLarge);
				total += sum[e]; totalLength += length[e];
				if (groups[e].size() >= 2)
				{
					cv::Vec4f line;
					cv::fitLine(groups[e], line, cv::DIST_HUBER, 0, 0.01, 0.001);
					directions[e] = {line[0],line[1]}; fitted[e] = true;
					const auto edge = model.vertices[(e + 1) % model.vertices.size()] - model.vertices[e];
					metrics.fitDeviation[e] = std::asin(std::clamp(std::abs(line[0] * edge.y - line[1] * edge.x) / Length(edge), 0.0f, 1.0f)) * 180 / kPi;
					if (metrics.fitDeviation[e] > kThresholds.maximumCornerAngleDeviationDegrees) Fail(metrics, Reason::AxisDeviationTooLarge);
				}
			}
			for (std::size_t e = 0; model.kind == ShapeModelKind::Rectangle && e < 4; ++e)
			{
				const auto a = directions[e], b = directions[(e+1)%4], opposite = directions[(e+2)%4];
				if (fitted[e] && fitted[(e+1)%4] && std::abs(Dot(a,b)) > std::sin(kThresholds.maximumCornerAngleDeviationDegrees*kPi/180))
					Fail(metrics,Reason::AxisDeviationTooLarge);
				if (fitted[e] && fitted[(e+2)%4] && std::abs(a.x*opposite.y-a.y*opposite.x) > std::sin(kThresholds.maximumOppositeEdgeDeviationDegrees*kPi/180))
					Fail(metrics,Reason::AxisDeviationTooLarge);
			}
			metrics.averageDeviation = totalLength > 0 ? static_cast<float>(total / totalLength) : 0;
			if (metrics.maximumDeviation > kThresholds.maximumAxisDeviationDegrees) Fail(metrics, Reason::AxisDeviationTooLarge);
		}

		Metrics Measure(const Model& model, const PreparedInk& ink,
			std::span<const InkStrokeView> source, bool reportDistances)
		{
			Metrics metrics;
			auto& h = metrics.hypothesis;
			h.model = model.kind; h.closed = model.closed; h.strokeCount = ink.strokes.size();
			h.rejectionReason = Reason::None;
			metrics.tolerance = std::clamp(std::max({ kThresholds.minimumEdgeBandDip,
				model.shortSide * kThresholds.edgeBandShortSideRatio, ink.medianWidth * kThresholds.edgeBandWidthMultiple }),
				kThresholds.minimumEdgeBandDip, kThresholds.maximumEdgeBandDip);
			// 转角过渡区只排除拐弯窗口，直边中段继续执行原有锯齿和偏轴检查。
			if (model.kind == ShapeModelKind::Rectangle)
				metrics.cornerTransition = std::min(0.25f * model.shortSide,
					std::max(2 * metrics.tolerance,kThresholds.cornerTransitionShortSideRatio * model.shortSide));
			h.toleranceDip = metrics.tolerance;
			h.toleranceDipX = Tolerance(metrics.tolerance,{1,0},ink);
			h.toleranceDipY = Tolerance(metrics.tolerance,{0,1},ink);
			for (std::size_t i = 0; i < std::min(model.vertices.size(), h.fittedCorners.size()); ++i)
				h.fittedCorners[i] = ToPixel(model.vertices[i], ink);
			std::vector<Interval> intervals;
			double error = 0, inlier = 0, total = 0;
			for (std::size_t s = 0; s < ink.strokes.size(); ++s)
			{
				const auto& points = ink.strokes[s].points;
				double strokeInlier = 0, strokeLength = 0;
				for (std::size_t i = 1; i < points.size(); ++i)
				{
					const float length = Length(points[i] - points[i - 1]);
					const auto a = Project(model, points[i - 1]), b = Project(model, points[i]);
					const auto mid = Project(model, (points[i] + points[i - 1]) * 0.5f);
					const float tolerance = Tolerance(metrics.tolerance, mid.normal, ink);
					const float residual = mid.distance / tolerance;
					error += std::min(residual * residual, 1.0f) * length; total += length; strokeLength += length;
					if (reportDistances && mid.edge < 4) metrics.distances[mid.edge].push_back(mid.distance);
					if (mid.distance <= tolerance)
					{
						inlier += length; strokeInlier += length;
						if (mid.edge < 4) metrics.contribution[s][mid.edge] += length;
					}
					if (a.distance > Tolerance(metrics.tolerance, a.normal, ink) ||
						b.distance > Tolerance(metrics.tolerance, b.normal, ink) || mid.distance > tolerance) continue;
					float begin = a.arc, end = b.arc;
					if (begin > end) std::swap(begin, end);
					if (model.closed && end - begin > model.perimeter * 0.5f)
					{
						if (model.perimeter - end + begin <= 2 * length + 0.01f)
						{
							intervals.push_back({ end, model.perimeter }); intervals.push_back({ 0, begin });
						}
					}
					else if (end - begin <= 2 * length + 0.01f) intervals.push_back({ begin, end });
				}
				metrics.strokeInlier[s] = strokeLength > 0 ? static_cast<float>(strokeInlier / strokeLength) : 0;
				metrics.minimumStrokeInlier = std::min(metrics.minimumStrokeInlier, metrics.strokeInlier[s]);
			}
			std::sort(intervals.begin(), intervals.end(), [](const auto& a, const auto& b) { return a.begin < b.begin; });
			h.residual = total > 0 ? static_cast<float>(error / total) : 1;
			h.inlierRatio = total > 0 ? static_cast<float>(inlier / total) : 0;
			h.coverage = Clamp01(MergeCoverage(intervals, 0, model.perimeter) / model.perimeter);
			const int parameters = model.kind == ShapeModelKind::Line ? 4 :
				model.kind == ShapeModelKind::Triangle ? 6 : model.kind == ShapeModelKind::Quadrilateral ? 8 : 5;
			h.quality = 0.50f * (1 - h.residual) + 0.30f * h.coverage + 0.20f * h.inlierRatio - 0.01f * (parameters - 2);
			const float minimum = std::max(kThresholds.minimumShortSideDip, kThresholds.minimumShortSideWidthMultiple * ink.medianWidth);
			if ((model.closed ? model.shortSide : model.longSide) + 0.001f < minimum) Fail(metrics, Reason::ShapeTooSmall);
			if (model.closed && model.longSide / model.shortSide > kThresholds.maximumAspectRatio + 0.001f) Fail(metrics, Reason::AspectRatioTooLarge);
			const float pathRatio = ink.length / model.perimeter;
			if (pathRatio < kThresholds.minimumPathPerimeterRatio || pathRatio > kThresholds.maximumPathPerimeterRatio) Fail(metrics, Reason::PathLengthRatioOutOfRange);
			if (metrics.minimumStrokeInlier < kThresholds.minimumStrokeOnEdgeRatio) Fail(metrics, Reason::StrokeEdgeRatioTooLow);
			if (1 - h.inlierRatio > kThresholds.maximumOffEdgeRatio) Fail(metrics, Reason::OffEdgeRatioTooHigh);
			if (model.kind != ShapeModelKind::Ellipse) CheckStraightness(model,ink,source,metrics);
			if (model.kind == ShapeModelKind::Rectangle)
			{
				// 最多一个真实端点间的缺口可用于闭合门槛；不计入覆盖、残差或质量分。
				const auto closure = FindEndpointClosureGap(model,ink,intervals,metrics.tolerance);
				metrics.endpointClosureGap = closure.end-closure.begin;
				auto gateIntervals = intervals;
				if (metrics.endpointClosureGap > 0)
				{
					gateIntervals.push_back({closure.begin,std::min(closure.end,model.perimeter)});
					if (closure.end > model.perimeter) gateIntervals.push_back({0,closure.end-model.perimeter});
					std::sort(gateIntervals.begin(),gateIntervals.end(),[](const auto& a,const auto& b) { return a.begin < b.begin; });
				}
				bool supportedCorners = true;
				float cornerMissing = 0;
				const float cornerTolerance = std::max(2 * metrics.tolerance, kThresholds.maximumCornerGapRatio * model.shortSide);
				for (std::size_t e = 0; e < 4; ++e)
				{
					const float edgeLength = model.arc[e + 1] - model.arc[e];
					metrics.coveredLength[e] = MergeCoverage(intervals, model.arc[e], model.arc[e + 1],
						&metrics.edgeGap[e], &metrics.edgeStartGap[e], &metrics.edgeEndGap[e]);
					metrics.edgeCoverage[e] = metrics.coveredLength[e] / edgeLength;
					if (metrics.edgeCoverage[e] < kThresholds.minimumEdgeCoverage) Fail(metrics, Reason::EdgeCoverageTooLow);
					float gap = 0, startGap = 0, endGap = 0, interiorGap = 0;
					MergeCoverage(gateIntervals,model.arc[e],model.arc[e+1],&gap,&startGap,&endGap,&interiorGap);
					if (gap > std::max(2 * metrics.tolerance, kThresholds.maximumEdgeGapRatio * edgeLength)) Fail(metrics, Reason::EdgeGapTooLarge);
					const float cornerGap = std::max(startGap,endGap);
					metrics.cornerScore = std::min(metrics.cornerScore, Clamp01(1 - cornerGap / cornerTolerance));
					if (cornerGap > cornerTolerance) Fail(metrics, Reason::CornerSupportTooLow);
					supportedCorners &= std::max(metrics.edgeStartGap[e],metrics.edgeEndGap[e]) <= cornerTolerance &&
						interiorGap <= 2 * kThresholds.resampleSpacingDip;
					cornerMissing += metrics.edgeStartGap[e] + metrics.edgeEndGap[e];
				}
				// 四边均有支撑且中段连续时，只对角区缺失放宽至多 5 个百分点；Q 仍使用真实 C。
				if (supportedCorners)
					metrics.cornerCoverageAllowance = std::min({kThresholds.maximumCornerCoverageAllowance,
						cornerMissing/model.perimeter,std::max(0.0f,kThresholds.minimumTotalCoverage-h.coverage)});
				const auto center = (model.vertices[0] + model.vertices[2]) * 0.5f;
				const float width = model.arc[1], height = model.arc[2] - model.arc[1];
				const auto a = ToPixel(center - cv::Point2f{ width * 0.5f, height * 0.5f }, ink);
				const auto b = ToPixel(center + cv::Point2f{ width * 0.5f, height * 0.5f }, ink);
				h.correction = { a.x, a.y, b.x, b.y };
				h.fittedAngleDegrees = model.angle * 180 / kPi;
				h.snapDisplacementDip = std::hypot(width, height) * std::sin(std::abs(model.angle) * 0.5f);
			}
			else if (model.kind == ShapeModelKind::Triangle || model.kind == ShapeModelKind::Quadrilateral)
			{
				// 完整多边形需要每条边的支持，不能用其余边的覆盖掩盖缺边。
				for (std::size_t e = 0; e + 1 < model.arc.size(); ++e)
					if (MergeCoverage(intervals, model.arc[e], model.arc[e + 1]) /
						(model.arc[e + 1] - model.arc[e]) < kThresholds.minimumEdgeCoverage)
						Fail(metrics, Reason::EdgeCoverageTooLow);
			}
			if (h.coverage + metrics.cornerCoverageAllowance + 1e-6f < kThresholds.minimumTotalCoverage)
				Fail(metrics, Reason::TotalCoverageTooLow);
			h.eligible = metrics.failureCount == 0;
			h.canCorrect = h.eligible && model.kind == ShapeModelKind::Rectangle;
			if (h.canCorrect && std::abs(h.fittedAngleDegrees) > kThresholds.maximumSnapAngleDegrees)
			{
				h.canCorrect = false; h.rejectionReason = Reason::SnapAngleTooLarge;
			}
			if (h.canCorrect && h.snapDisplacementDip > std::max(2 * metrics.tolerance, kThresholds.maximumSnapDisplacementRatio * model.shortSide))
			{
				h.canCorrect = false; h.rejectionReason = Reason::SnapDisplacementTooLarge;
			}
			return metrics;
		}

		bool EquivalentRectangle(const ShapeHypothesis& a, const ShapeHypothesis& b, const ShapeRecognitionContext& context)
		{
			if (a.model != ShapeModelKind::Rectangle || b.model != a.model) return false;
			const float tolerance = std::min(a.toleranceDip, b.toleranceDip);
			for (const auto& p : a.fittedCorners)
			{
				float nearest = (std::numeric_limits<float>::max)();
				for (const auto& q : b.fittedCorners)
					nearest = std::min(nearest, std::hypot((p.x - q.x) * context.dipPerPixelX, (p.y - q.y) * context.dipPerPixelY));
				if (nearest > tolerance) return false;
			}
			return true;
		}

		void ReportRectangle(const Model& model, const Metrics& metrics, const PreparedInk& ink, ShapeRecognitionDiagnostics& d)
		{
			const float pixels = 1 / std::sqrt(ink.context.dipPerPixelX * ink.context.dipPerPixelY);
			d.shortSide = model.shortSide * pixels; d.longSide = model.longSide * pixels;
			d.minimumShortSide = std::max(kThresholds.minimumShortSideDip, kThresholds.minimumShortSideWidthMultiple * ink.medianWidth) * pixels;
			d.aspectRatio = model.longSide / model.shortSide;
			d.rectanglePerimeter = model.perimeter * pixels; d.pathPerimeterRatio = ink.length / model.perimeter;
			d.rectangularity = d.hullArea / (model.shortSide * model.longSide * pixels * pixels);
			d.edgeBandDip = metrics.tolerance; d.edgeBand = metrics.tolerance * pixels;
			d.edgeCoverage = metrics.edgeCoverage;
			d.totalCoverage = metrics.hypothesis.coverage; d.offEdgeRatio = 1 - metrics.hypothesis.inlierRatio;
			d.minimumStrokeOnEdgeRatio = metrics.minimumStrokeInlier;
			d.maximumAxisDeviationDegrees = metrics.maximumDeviation; d.weightedAxisDeviationDegrees = metrics.averageDeviation;
			d.edgeSegmentAxisDeviationDegrees = metrics.localDeviation; d.edgeFitAxisDeviationDegrees = metrics.fitDeviation;
			d.worstCornerScore = metrics.cornerScore; d.confidence = metrics.hypothesis.quality;
			d.cornerTransitionDip = metrics.cornerTransition; d.endpointClosureGapDip = metrics.endpointClosureGap;
			d.cornerCoverageAllowance = metrics.cornerCoverageAllowance;
			d.fittedAngleDegrees = metrics.hypothesis.fittedAngleDegrees; d.snapDisplacementDip = metrics.hypothesis.snapDisplacementDip;
			d.failedConditions = metrics.failures; d.failedConditionCount = metrics.failureCount;
			for (std::size_t e = 0; e < 4; ++e)
			{
				d.edgeCoveredLength[e] = metrics.coveredLength[e] * pixels; d.edgeLargestGap[e] = metrics.edgeGap[e] * pixels;
				d.edgeDistanceP50[e] = Percentile(metrics.distances[e], 0.50f) * pixels;
				d.edgeDistanceP90[e] = Percentile(metrics.distances[e], 0.90f) * pixels;
				d.edgeDistanceP95[e] = Percentile(metrics.distances[e], 0.95f) * pixels;
				for (std::size_t s = 0; s < ink.strokes.size(); ++s)
				{
					d.strokes[s].onEdgeRatio = metrics.strokeInlier[s];
					d.strokes[s].edgeContributionLength[e] = metrics.contribution[s][e] * pixels;
				}
			}
		}
	}

	const char* ShapeRecognitionRejectReasonName(
		ShapeRecognitionRejectReason reason) noexcept
	{
		switch (reason)
		{
		case ShapeRecognitionRejectReason::None: return "none";
		case ShapeRecognitionRejectReason::InvalidStrokeCount: return "invalid_stroke_count";
		case ShapeRecognitionRejectReason::InvalidDpiScale: return "invalid_dpi_scale";
		case ShapeRecognitionRejectReason::SourcePointLimit: return "source_point_limit";
		case ShapeRecognitionRejectReason::StrokeResamplingFailed: return "stroke_resampling_failed";
		case ShapeRecognitionRejectReason::InvalidSampleSet: return "invalid_sample_set";
		case ShapeRecognitionRejectReason::InvalidMedianWidth: return "invalid_median_width";
		case ShapeRecognitionRejectReason::HullTooSmall: return "hull_too_small";
		case ShapeRecognitionRejectReason::InvalidHullGeometry: return "invalid_hull_geometry";
		case ShapeRecognitionRejectReason::HullIsNotQuadrilateral: return "hull_not_quadrilateral";
		case ShapeRecognitionRejectReason::InvalidBounds: return "invalid_bounds";
		case ShapeRecognitionRejectReason::ShapeTooSmall: return "shape_too_small";
		case ShapeRecognitionRejectReason::AspectRatioTooLarge: return "aspect_ratio_too_large";
		case ShapeRecognitionRejectReason::RectangularityTooLow: return "rectangularity_too_low";
		case ShapeRecognitionRejectReason::PathLengthRatioOutOfRange: return "path_ratio_out_of_range";
		case ShapeRecognitionRejectReason::StrokeEdgeRatioTooLow: return "stroke_edge_ratio_too_low";
		case ShapeRecognitionRejectReason::OffEdgeRatioTooHigh: return "off_edge_ratio_too_high";
		case ShapeRecognitionRejectReason::EdgeCoverageTooLow: return "edge_coverage_too_low";
		case ShapeRecognitionRejectReason::InvalidLineFit: return "invalid_line_fit";
		case ShapeRecognitionRejectReason::AxisDeviationTooLarge: return "axis_deviation_too_large";
		case ShapeRecognitionRejectReason::TotalCoverageTooLow: return "total_coverage_too_low";
		case ShapeRecognitionRejectReason::ConfidenceTooLow: return "confidence_too_low";
		case ShapeRecognitionRejectReason::OpenCvException: return "opencv_exception";
		case ShapeRecognitionRejectReason::NoViableModel: return "no_viable_model";
		case ShapeRecognitionRejectReason::AmbiguousModels: return "ambiguous_models";
		case ShapeRecognitionRejectReason::UnsupportedShape: return "unsupported_shape";
		case ShapeRecognitionRejectReason::CornerSupportTooLow: return "corner_support_too_low";
		case ShapeRecognitionRejectReason::EdgeGapTooLarge: return "edge_gap_too_large";
		case ShapeRecognitionRejectReason::SnapAngleTooLarge: return "snap_angle_too_large";
		case ShapeRecognitionRejectReason::SnapDisplacementTooLarge: return "snap_displacement_too_large";
		case ShapeRecognitionRejectReason::EquivalentModel: return "equivalent_model";
		default: return "unexpected_exception";
		}
	}


	const char* ShapeScaleFallbackReasonName(ShapeScaleFallbackReason reason) noexcept
	{
		switch (reason)
		{
		case ShapeScaleFallbackReason::None: return "none";
		case ShapeScaleFallbackReason::PhysicalUnavailable: return "physical_unavailable";
		case ShapeScaleFallbackReason::IndirectInput: return "indirect_input";
		case ShapeScaleFallbackReason::UnrecognizedSource: return "unrecognized_source";
		case ShapeScaleFallbackReason::MappingMismatch: return "mapping_mismatch";
		case ShapeScaleFallbackReason::MissingProvenance: return "missing_provenance";
		case ShapeScaleFallbackReason::DisplayChanged: return "display_changed";
		case ShapeScaleFallbackReason::SourceChanged: return "source_changed";
		case ShapeScaleFallbackReason::InvalidPhysicalScale: return "invalid_physical_scale";
		default: return "unknown";
		}
	}

	const char* ShapeModelKindName(ShapeModelKind model) noexcept
	{
		switch (model)
		{
		case ShapeModelKind::Rectangle: return "rectangle";
		case ShapeModelKind::Line: return "line";
		case ShapeModelKind::Ellipse: return "ellipse";
		case ShapeModelKind::Triangle: return "triangle";
		case ShapeModelKind::Quadrilateral: return "quadrilateral";
		default: return "unknown";
		}
	}

	ShapeSelection SelectShapeHypothesis(std::span<const ShapeHypothesis> hypotheses,
		const ShapeRecognitionContext& context) noexcept
	{
		ShapeSelection selection;
		// 最多六个后缀，每组五个模型；仲裁无需分配或依赖模型注册顺序。
		if (hypotheses.size() > 30) return selection;
		std::array<bool, 30> hidden = {};
		bool hasClosed = false;
		for (const auto& h : hypotheses) hasClosed |= h.eligible && h.closed;
		for (std::size_t i = 0; i < hypotheses.size(); ++i)
		{
			const auto& a = hypotheses[i];
			if (!a.eligible || !std::isfinite(a.quality) || (hasClosed && !a.closed)) { hidden[i] = true; continue; }
			for (std::size_t j = 0; j < i; ++j)
			{
				if (hidden[j] || !EquivalentRectangle(a, hypotheses[j], context)) continue;
				const auto& b = hypotheses[j];
				const bool preferA = std::abs(a.quality - b.quality) <= 0.02f
					? a.strokeCount > b.strokeCount : a.quality > b.quality;
				if (preferA) hidden[j] = true;
				else hidden[i] = true;
			}
		}
		std::size_t best = hypotheses.size(), second = hypotheses.size();
		for (std::size_t i = 0; i < hypotheses.size(); ++i)
		{
			if (hidden[i]) continue;
			if (best == hypotheses.size() || hypotheses[i].quality > hypotheses[best].quality)
			{ second = best; best = i; }
			else if (second == hypotheses.size() || hypotheses[i].quality > hypotheses[second].quality) second = i;
		}
		if (best == hypotheses.size()) return selection;
		selection.index = best;
		selection.margin = second == hypotheses.size() ? 1.0f : hypotheses[best].quality - hypotheses[second].quality;
		const auto& winner = hypotheses[best];
		if (winner.quality < kThresholds.minimumConfidence) selection.rejectionReason = Reason::ConfidenceTooLow;
		else if (selection.margin + 1e-6f < kThresholds.minimumModelMargin) selection.rejectionReason = Reason::AmbiguousModels;
		else if (winner.model != ShapeModelKind::Rectangle) selection.rejectionReason = Reason::UnsupportedShape;
		else if (!winner.canCorrect) selection.rejectionReason = winner.rejectionReason;
		else selection.rejectionReason = Reason::None;
		return selection;
	}

	ShapeEvaluation EvaluateInkShapes(std::span<const InkStrokeView> strokes,
		const ShapeRecognitionContext& context, ShapeRecognitionDiagnostics* diagnostics) noexcept
	{
		ShapeEvaluation result;
		if (diagnostics)
		{
			*diagnostics = {}; diagnostics->thresholds = kThresholds;
			diagnostics->inputStrokeCount = strokes.size(); diagnostics->context = context;
			diagnostics->dpiScale = 1 / std::sqrt(context.dipPerPixelX * context.dipPerPixelY);
		}
		auto reject = [&](Reason reason)
		{
			result.selection.rejectionReason = reason;
			if (diagnostics)
			{
				diagnostics->rejectionReason = reason;
				bool recorded = false;
				for (std::size_t i = 0; i < diagnostics->failedConditionCount; ++i) recorded |= diagnostics->failedConditions[i] == reason;
				if (!recorded && diagnostics->failedConditionCount < diagnostics->failedConditions.size())
					diagnostics->failedConditions[diagnostics->failedConditionCount++] = reason;
			}
			return result;
		};
		try
		{
			PreparedInk ink;
			if (const auto reason = Prepare(strokes, context, ink, diagnostics); reason != Reason::None) return reject(reason);
			std::vector<cv::Point2f> hull;
			cv::convexHull(ink.points, hull, true, true);
			if (hull.size() < 2) return reject(Reason::InvalidHullGeometry);
			const float perimeter = static_cast<float>(cv::arcLength(hull, true));
			if (diagnostics)
			{
				diagnostics->hullPointCount = hull.size();
				const float pixels = 1 / std::sqrt(context.dipPerPixelX * context.dipPerPixelY);
				diagnostics->hullArea = static_cast<float>(std::abs(cv::contourArea(hull))) * pixels * pixels;
				diagnostics->hullPerimeter = perimeter * pixels;
				float l = ink.points[0].x, r = l, t = ink.points[0].y, b = t;
				for (const auto p : ink.points) { l = std::min(l, p.x); r = std::max(r, p.x); t = std::min(t, p.y); b = std::max(b, p.y); }
				const auto a = ToPixel({ l,t }, ink), c = ToPixel({ r,b }, ink);
				diagnostics->bounds = { a.x,a.y,c.x,c.y };
			}
			std::array<Model, 5> bestModels;
			std::array<Metrics, 5> bestMetrics;
			std::array<bool, 5> present = {};
			auto consider = [&](const Model& model)
			{
				const auto index = static_cast<std::size_t>(model.kind);
				Metrics metrics = Measure(model, ink, strokes, diagnostics && model.kind == ShapeModelKind::Rectangle);
				const bool better = !present[index] || (metrics.hypothesis.eligible != bestMetrics[index].hypothesis.eligible
					? metrics.hypothesis.eligible : metrics.hypothesis.quality > bestMetrics[index].hypothesis.quality);
				if (better) { bestModels[index] = model; bestMetrics[index] = std::move(metrics); present[index] = true; }
			};
			std::vector<std::vector<cv::Point2f>> rectangleSeeds, polygonSeeds;
			const auto newSeed = [](const auto& seed, auto& seen)
			{
				for (const auto& previous : seen)
				{
					if (previous.size() != seed.size()) continue;
					bool equivalent = true;
					for (const auto p : seed)
					{
						float nearest = (std::numeric_limits<float>::max)();
						for (const auto q : previous) nearest = std::min(nearest,Length(p-q));
						equivalent &= nearest <= 0.25f;
					}
					if (equivalent) return false;
				}
				seen.push_back(seed);
				return true;
			};
			if (hull.size() >= 3)
			{
				const auto box = cv::minAreaRect(hull);
				std::array<cv::Point2f, 4> corners;
				box.points(corners.data());
				Model rectangle;
				const std::vector<cv::Point2f> boxSeed(corners.begin(),corners.end());
				rectangleSeeds.push_back(boxSeed);
				if (FitRectangle(boxSeed, ink, rectangle)) consider(rectangle);
				for (std::size_t e = 0; e < kThresholds.approximationEpsilonRatios.size(); ++e)
				{
					std::vector<cv::Point2f> polygon;
					cv::approxPolyDP(hull, polygon, perimeter * kThresholds.approximationEpsilonRatios[e], true);
					if (diagnostics) diagnostics->approximationCornerCounts[e] = polygon.size();
					if ((polygon.size() != 3 && polygon.size() != 4) || !cv::isContourConvex(polygon)) continue;
					Model fitted;
					if (newSeed(polygon,polygonSeeds) && FitPolygon(polygon, ink, fitted)) consider(fitted);
					if (polygon.size() == 4)
					{
						// 等价初值只拟合一次，避免多尺度简化重复消耗抬笔预算。
						if (newSeed(polygon,rectangleSeeds) && FitRectangle(polygon,ink,fitted)) consider(fitted);
						if (diagnostics && !std::isfinite(diagnostics->selectedApproximationEpsilonRatio))
						{
							diagnostics->selectedApproximationEpsilonRatio = kThresholds.approximationEpsilonRatios[e];
							diagnostics->approximatedCornerCount = 4; diagnostics->selectedQuadCornerCount = 4;
							for (std::size_t i = 0; i < 4; ++i) diagnostics->selectedQuad[i] = ToPixel(polygon[i], ink);
						}
					}
				}
			}
			Model line;
			line.kind = ShapeModelKind::Line; line.closed = false;
			cv::Vec4f direction;
			cv::fitLine(ink.points, direction, cv::DIST_HUBER, 0, 0.01, 0.001);
			const cv::Point2f u{ direction[0],direction[1] }, center{ direction[2],direction[3] };
			float first = Dot(ink.points[0] - center, u), last = first;
			for (const auto p : ink.points) { first = std::min(first, Dot(p - center, u)); last = std::max(last, Dot(p - center, u)); }
			line.vertices = { center + u * first, center + u * last };
			if (FinishModel(line)) consider(line);
			if (ink.points.size() >= 5 && hull.size() >= 5)
			{
				Model ellipse;
				ellipse.kind = ShapeModelKind::Ellipse;
				ellipse.ellipse = cv::fitEllipseDirect(ink.points);
				const auto& e = ellipse.ellipse;
				if (Finite(e.center) && std::isfinite(e.angle) && e.size.width > 0.01f && e.size.height > 0.01f &&
					std::isfinite(e.size.width) && std::isfinite(e.size.height))
				{
					const float angle = e.angle * kPi / 180;
					for (int i = 0; i < 128; ++i)
					{
						const float t = 2 * kPi * i / 128;
						const float x = e.size.width * 0.5f * std::cos(t), y = e.size.height * 0.5f * std::sin(t);
						ellipse.vertices.push_back(e.center + cv::Point2f{ x * std::cos(angle) - y * std::sin(angle), x * std::sin(angle) + y * std::cos(angle) });
					}
					if (FinishModel(ellipse)) consider(ellipse);
				}
			}
			const auto rectangleIndex = static_cast<std::size_t>(ShapeModelKind::Rectangle);
			const auto quadIndex = static_cast<std::size_t>(ShapeModelKind::Quadrilateral);
			if (present[rectangleIndex] && present[quadIndex] && bestMetrics[rectangleIndex].hypothesis.eligible)
			{
				const auto& quad = bestModels[quadIndex];
				float angleDeviation = 0;
				for (std::size_t i = 0; i < 4; ++i)
				{
					const auto a = quad.vertices[(i + 1) % 4] - quad.vertices[i];
					const auto b = quad.vertices[(i + 2) % 4] - quad.vertices[(i + 1) % 4];
					angleDeviation = std::max(angleDeviation, std::asin(std::clamp(std::abs(Dot(a,b)) / (Length(a)*Length(b)), 0.0f, 1.0f)) * 180 / kPi);
				}
				// 自由四边形只有明显非正交且改善充分时才是另一种解释。
				const float improvement = bestMetrics[quadIndex].hypothesis.quality - bestMetrics[rectangleIndex].hypothesis.quality + 0.03f;
				if (angleDeviation <= 10 || improvement < kThresholds.minimumModelMargin)
				{
					bestMetrics[quadIndex].hypothesis.eligible = false;
					bestMetrics[quadIndex].hypothesis.rejectionReason = Reason::EquivalentModel;
				}
			}
			for (std::size_t i = 0; i < present.size(); ++i)
				if (present[i]) result.hypotheses[result.count++] = bestMetrics[i].hypothesis;
			result.selection = SelectShapeHypothesis({ result.hypotheses.data(),result.count }, ink.context);
			if (result.selection.rejectionReason == Reason::None)
			{
				const auto& winner = result.hypotheses[result.selection.index];
				result.result = { ShapeType::AxisAlignedRectangle,winner.quality,winner.correction };
			}
			if (diagnostics)
			{
				if (present[rectangleIndex]) ReportRectangle(bestModels[rectangleIndex], bestMetrics[rectangleIndex], ink, *diagnostics);
				diagnostics->hypotheses = result.hypotheses; diagnostics->hypothesisCount = result.count;
				diagnostics->modelMargin = result.selection.margin; diagnostics->accepted = result.result.type != ShapeType::Unknown;
			}
			if (result.selection.rejectionReason == Reason::NoViableModel && present[rectangleIndex])
				return reject(bestMetrics[rectangleIndex].hypothesis.rejectionReason);
			if (result.selection.rejectionReason != Reason::None) return reject(result.selection.rejectionReason);
			return result;
		}
		catch (const cv::Exception&) { result = {}; return reject(Reason::OpenCvException); }
		catch (...) { result = {}; return reject(Reason::UnexpectedException); }
	}

	ShapeResult RecognizeInkShape(std::span<const InkStrokeView> strokes,
		const ShapeRecognitionContext& context, ShapeRecognitionDiagnostics* diagnostics) noexcept
	{
		return EvaluateInkShapes(strokes, context, diagnostics).result;
	}

	ShapeResult RecognizeInkShape(std::span<const InkStrokeView> strokes,
		float dpiScale, ShapeRecognitionDiagnostics* diagnostics) noexcept
	{
		ShapeRecognitionContext context;
		context.dipPerPixelX = context.dipPerPixelY = 1 / dpiScale;
		return RecognizeInkShape(strokes, context, diagnostics);
	}
}
