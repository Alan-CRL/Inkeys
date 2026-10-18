#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <limits>
#include <chrono>
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h"

import Inkeys.CV.ShapeRecognition;
import Inkeys.Drawing.Draw3.ink_document;
import Inkeys.Drawing.Draw3.ink_history;
import Inkeys.Drawing.Draw3.shape_recognition;

namespace
{
	bool Expect(bool condition, std::string_view name)
	{
		if (!condition) std::cerr << "[Draw3ShapeRecognition] failed: " << name << '\n';
		return condition;
	}

	using CvPoint = Inkeys::CV::InkPoint;
	using CvStroke = std::vector<CvPoint>;

	CvStroke Line(float x0, float y0, float x1, float y1, float width = 5.0f)
	{
		return { { x0, y0, width }, { x1, y1, width } };
	}

	Inkeys::CV::ShapeResult RecognizeShape(const std::vector<CvStroke>& strokes,
		Inkeys::CV::ShapeRecognitionDiagnostics* diagnostics = nullptr,
		float dpiScale = 1.0f)
	{
		std::vector<Inkeys::CV::InkStrokeView> views;
		views.reserve(strokes.size());
		for (const CvStroke& stroke : strokes) views.push_back({ stroke });
		return Inkeys::CV::RecognizeInkShape(views, dpiScale, diagnostics);
	}

	bool RecognizesRectangle(const std::vector<CvStroke>& strokes)
	{
		return RecognizeShape(strokes).type ==
			Inkeys::CV::ShapeType::AxisAlignedRectangle;
	}

	std::vector<CvStroke> StandardRectangle()
	{
		return {
			Line(10.0f, 20.0f, 130.0f, 20.0f),
			Line(130.0f, 20.0f, 130.0f, 100.0f),
			Line(130.0f, 100.0f, 10.0f, 100.0f),
			Line(10.0f, 100.0f, 10.0f, 20.0f)
		};
	}

	std::vector<CvStroke> ScaleStrokes(
		std::vector<CvStroke> strokes, float scale)
	{
		for (CvStroke& stroke : strokes)
		{
			for (CvPoint& point : stroke)
			{
				point.x *= scale;
				point.y *= scale;
				point.width *= scale;
			}
		}
		return strokes;
	}

	std::vector<CvStroke> RectangleSample(float width, float height,
		float angleDegrees = 0.0f)
	{
		std::vector<CvStroke> result = {
			Line(0, 0, width, 0, 3), Line(width, 0, width, height, 3),
			Line(width, height, 0, height, 3), Line(0, height, 0, 0, 3)
		};
		const float angle = angleDegrees * 3.14159265359f / 180.0f;
		for (auto& stroke : result)
			for (auto& point : stroke)
			{
				const float x = point.x - width * 0.5f;
				const float y = point.y - height * 0.5f;
				point.x = x * std::cos(angle) - y * std::sin(angle) + width * 0.5f;
				point.y = x * std::sin(angle) + y * std::cos(angle) + height * 0.5f;
			}
		return result;
	}

	int RunHandDrawnRegressionTests()
	{
		int failures = 0;
		for (const float angle : { 2.0f, 5.0f })
			if (!Expect(RecognizesRectangle(RectangleSample(600, 200, angle)),
				"long hand-drawn rectangle tolerates slight rotation")) ++failures;
		for (const float angle : { 15.0f, 30.0f, 45.0f })
			if (!Expect(!RecognizesRectangle(RectangleSample(240, 120, angle)),
				"intentional rotation is preserved")) ++failures;
		if (!Expect(!RecognizesRectangle(RectangleSample(180, 63.5f)) &&
			RecognizesRectangle(RectangleSample(180, 64)),
			"64 DIP short-side boundary filters writing")) ++failures;
		if (!Expect(!RecognizesRectangle(RectangleSample(600, 40)),
			"large area cannot bypass the short-side limit")) ++failures;
		return failures;
	}

	Inkeys::CV::ShapeEvaluation EvaluateSample(const std::vector<CvStroke>& strokes,
		const Inkeys::CV::ShapeRecognitionContext& context = {},
		Inkeys::CV::ShapeRecognitionDiagnostics* diagnostics = nullptr)
	{
		std::vector<Inkeys::CV::InkStrokeView> views;
		for (const auto& stroke : strokes) views.push_back({ stroke });
		return Inkeys::CV::EvaluateInkShapes(views, context, diagnostics);
	}

	CvStroke RoundedRectangleSample(float width, float height, float radius)
	{
		CvStroke stroke;
		const std::array<std::pair<float, float>, 4> centers = {
			std::pair{width-radius,radius}, {width-radius,height-radius},
			{radius,height-radius}, {radius,radius} };
		for (int corner = 0; corner < 4; ++corner)
			for (int i = 0; i <= 16; ++i)
			{
				const float angle = (corner - 1 + i / 16.0f) * 1.57079632679f;
				stroke.push_back({ centers[corner].first + radius * std::cos(angle),
					centers[corner].second + radius * std::sin(angle), 3 });
			}
		stroke.push_back(stroke.front());
		return stroke;
	}

	int RunCornerAndClosureTests()
	{
		using namespace Inkeys::CV;
		int failures = 0;
		const auto check = [&](const std::vector<CvStroke>& strokes, bool expected, std::string_view name)
		{
			ShapeRecognitionDiagnostics diagnostics;
			const auto evaluation = EvaluateSample(strokes, {}, &diagnostics);
			const bool accepted = evaluation.result.type == ShapeType::AxisAlignedRectangle;
			if (accepted == expected) return;
			++failures;
			Expect(false, name);
			std::cerr << "  reject=" << ShapeRecognitionRejectReasonName(diagnostics.rejectionReason) <<
				" coverage=" << diagnostics.totalCoverage << " max_deviation=" << diagnostics.maximumAxisDeviationDegrees <<
				" quality=" << diagnostics.confidence << " failures=[";
			for (std::size_t i = 0; i < diagnostics.failedConditionCount; ++i)
				std::cerr << ShapeRecognitionRejectReasonName(diagnostics.failedConditions[i]) << ',';
			std::cerr << "]\n";
		};
		// 固定的意图样例：收尾缺口与圆钝转角不能要求用户先画出精确矩形。
		for (float radius : {16.0f,24.0f,32.0f})
			check({RoundedRectangleSample(240,140,radius)}, true, "four straight sides with soft hand-drawn corners");
		const std::vector<CvStroke> nearCornerGap = { {
			{0,0,3}, {240,0,3}, {240,140,3}, {0,140,3}, {0,36,3} } };
		check(nearCornerGap, true, "single endpoint closure gap near a corner");
		auto reverseGap = nearCornerGap;
		std::reverse(reverseGap[0].begin(),reverseGap[0].end());
		check(reverseGap, true, "closure evidence does not depend on drawing direction");
		check({ {{0,36,3},{0,140,3},{240,140,3}}, {{0,0,3},{240,0,3},{240,140,3}} },
			true, "unordered reversed strokes retain one endpoint closure");
		ShapeRecognitionDiagnostics closureDiagnostics;
		const auto closureEvaluation = EvaluateSample(nearCornerGap, {}, &closureDiagnostics);
		if (!Expect(closureEvaluation.result.type == ShapeType::AxisAlignedRectangle &&
			std::abs(closureDiagnostics.endpointClosureGapDip - 36) < 0.1f &&
			closureDiagnostics.totalCoverage < 0.96f && closureDiagnostics.confidence < 0.96f,
			"closure permission preserves the measured coverage and quality penalty")) ++failures;

		check({ {{0,50,3}, {0,0,3}, {240,0,3}, {240,140,3}, {0,140,3}, {0,86,3}} },
			true, "single endpoint closure gap in the middle of an edge");
		check({ {{24,0,3}, {240,0,3}, {240,140,3}, {0,140,3}, {0,24,3}} },
			true, "closure straddles a supported corner");
		check({ Line(12,0,168,0,3), Line(180,12,180,88,3),
			Line(168,100,12,100,3), Line(0,88,0,12,3) },
			true, "four independent sides leave small gaps at their corners");
		ShapeRecognitionDiagnostics cornerDiagnostics;
		EvaluateSample({Line(12,0,168,0,3),Line(180,12,180,88,3),
			Line(168,100,12,100,3),Line(0,88,0,12,3)}, {}, &cornerDiagnostics);
		if (!Expect(cornerDiagnostics.totalCoverage > 0.82f && cornerDiagnostics.totalCoverage < 0.84f &&
			cornerDiagnostics.cornerCoverageAllowance > 0.02f && cornerDiagnostics.cornerCoverageAllowance < 0.03f,
			"corner allowance is reported separately from measured coverage")) ++failures;
		for (float scale : {1.0f,1.25f,1.5f,2.0f})
		{
			const auto rounded = RecognizeShape(ScaleStrokes({RoundedRectangleSample(240,140,32)},scale), nullptr, scale);
			const auto gap = RecognizeShape(ScaleStrokes(nearCornerGap,scale), nullptr, scale);
			if (!Expect(rounded.type == ShapeType::AxisAlignedRectangle && gap.type == ShapeType::AxisAlignedRectangle,
				"soft corners and endpoint gaps remain DIP-equivalent")) ++failures;
		}
		// 按日志中的 2x 显示及可信 EDID 重放固定样例，物理先验不能绕过结构检查。
		ShapeRecognitionContext physical;
		physical.dipPerPixelX = physical.dipPerPixelY = 0.5f;
		physical.physicalScaleTrusted = true;
		physical.millimetersPerDipX = 280.0f/1440;
		physical.millimetersPerDipY = 190.0f/960;
		if (!Expect(EvaluateSample(ScaleStrokes({RoundedRectangleSample(240,140,32)},2),physical).result.type ==
			ShapeType::AxisAlignedRectangle && EvaluateSample(ScaleStrokes(nearCornerGap,2),physical).result.type ==
			ShapeType::AxisAlignedRectangle, "soft corners and closure work with the observed display scale")) ++failures;
		check({ {{0,0,3},{240,0,3},{240,140,3},{0,140,3},{0,90,3},
			{30,80,3},{30,60,3},{0,50,3},{0,0,3}} }, false,
			"an off-boundary detour cannot borrow endpoint closure evidence");
		auto zigzagCorner = RoundedRectangleSample(240,140,24);
		zigzagCorner.insert(zigzagCorner.begin()+17,
			{ {240,40,3},{232,56,3},{240,72,3},{232,88,3},{240,104,3} });
		check({zigzagCorner}, false, "soft corners do not exempt zigzags in the middle of a side");
		auto skewed = RoundedRectangleSample(240,140,24);
		for (auto& p : skewed) p.x += 0.35f*p.y;
		check({skewed}, false, "soft corners do not conceal a nonorthogonal quadrilateral");
		check({ {{0,0,3}, {240,0,3}, {240,140,3}, {0,140,3}, {0,66,3}} },
			false, "a large opening is not inferred as closure");
		check({ Line(0,0,240,0,3), Line(240,0,240,50,3), Line(240,86,240,140,3),
			Line(240,140,0,140,3), Line(0,140,0,86,3), Line(0,50,0,0,3) },
			false, "two large openings cannot both use the closure allowance");
		check({RoundedRectangleSample(240,140,60)}, false, "mostly curved sides retain rounded shape intent");
		CvStroke openEllipse;
		for (int i = 0; i <= 60; ++i)
		{
			const float angle = i*6.28318530718f/64;
			openEllipse.push_back({120+120*std::cos(angle),70+70*std::sin(angle),3});
		}
		check({openEllipse}, false, "an ellipse with a small opening retains its curved shape");

		check({ Line(30,0,150,0,3), Line(180,30,180,70,3),
			Line(150,100,30,100,3), Line(0,70,0,30,3) },
			false, "four isolated stubs do not establish a rectangle");
		return failures;
	}

	int RunBalancedModelTests()
	{
		using namespace Inkeys::CV;
		int failures = 0;
		auto rough = RectangleSample(180, 100);
		for (auto& stroke : rough)
		{
			const auto a = stroke.front(), b = stroke.back();
			stroke.clear();
			const float length = std::hypot(b.x - a.x, b.y - a.y);
			for (int i = 0; i <= 20; ++i)
			{
				const float t = i / 20.0f;
				const float noise = 2 * std::sin(3.14159265359f * t) + 0.7f * std::sin(12.5663706144f * t);
				stroke.push_back({ a.x + (b.x - a.x) * t - (b.y - a.y) / length * noise,
					a.y + (b.y - a.y) * t + (b.x - a.x) / length * noise,
					3 + 1.2f * std::sin(i * 0.73f) });
			}
		}
		if (!Expect(RecognizesRectangle(rough), "3 DIP curved pressure-varying rectangle")) ++failures;
		auto extended = RectangleSample(180, 100);
		extended[0] = Line(-4, 0, 185, 0, 3);
		if (!Expect(RecognizesRectangle(extended), "small endpoint overshoots do not set rectangle extrema")) ++failures;
		const auto tilted = RecognizeShape(RectangleSample(600, 200, 5));
		if (!Expect(tilted.type == ShapeType::AxisAlignedRectangle &&
			std::abs(tilted.rectangle.right - tilted.rectangle.left - 600) < 1 &&
			std::abs(tilted.rectangle.bottom - tilted.rectangle.top - 200) < 1,
			"snapping preserves fitted dimensions rather than rotated AABB")) ++failures;
		for (float scale : { 1.0f, 1.25f, 1.5f, 2.0f })
		{
			const auto result = RecognizeShape(ScaleStrokes(rough, scale), nullptr, scale);
			if (!Expect(result.type == ShapeType::AxisAlignedRectangle,
				"rough rectangle is equivalent at fractional DPI")) ++failures;
		}
		auto anisotropic = rough;
		for (auto& stroke : anisotropic) for (auto& p : stroke) { p.x *= 2; p.y *= 1.25f; p.width *= std::sqrt(2.5f); }
		ShapeRecognitionContext context;
		context.dipPerPixelX = 0.5f; context.dipPerPixelY = 0.8f;
		if (!Expect(EvaluateSample(anisotropic, context).result.type == ShapeType::AxisAlignedRectangle,
			"XY display scales are applied independently")) ++failures;
		auto translated = rough;
		for (auto& stroke : translated) for (auto& p : stroke) { p.x += 1000000; p.y -= 1000000; }
		if (!Expect(RecognizesRectangle(translated), "local fitting is stable in a distant viewport")) ++failures;

		std::vector<std::vector<CvStroke>> negatives = {
			{ { {0,0,3},{180,0,3},{150,100,3},{30,100,3},{0,0,3} } },
			{ { {100,0,3},{180,100,3},{100,200,3},{20,100,3},{100,0,3} } }
		};
		for (float aspect : { 1.0f, 1.5f, 3.0f })
		{
			CvStroke ellipse;
			for (int i = 0; i <= 120; ++i)
			{
				const float angle = i * 6.28318530718f / 120;
				ellipse.push_back({ 90 * aspect * std::cos(angle),90 * std::sin(angle),3 });
			}
			const auto result = EvaluateSample({ ellipse });
			if (!Expect(result.selection.index < result.count &&
				result.hypotheses[result.selection.index].model == ShapeModelKind::Ellipse &&
				result.selection.rejectionReason == ShapeRecognitionRejectReason::UnsupportedShape,
				"unsupported ellipse wins classification without rectangle fallback")) ++failures;
			negatives.push_back({ ellipse });
		}
		for (const auto& sample : negatives)
			if (!Expect(!RecognizesRectangle(sample), "competing closed geometry remains ink")) ++failures;
		std::vector<CvStroke> zigzag = { {{0,0,3},{30,10,3},{60,0,3},{90,10,3},{120,0,3},{150,10,3},{180,0,3}},
			Line(180,0,180,100,3),Line(180,100,0,100,3),Line(0,100,0,0,3) };
		auto denseZigzag = zigzag;
		for (auto& stroke : denseZigzag)
		{
			const auto original = stroke; stroke.clear();
			for (std::size_t i = 1; i < original.size(); ++i)
				for (int j = 0; j < 20; ++j)
				{
					const float t = j/20.0f;
					stroke.push_back({original[i-1].x+(original[i].x-original[i-1].x)*t,
						original[i-1].y+(original[i].y-original[i-1].y)*t,3});
				}
			stroke.push_back(original.back());
		}
		if (!Expect(!RecognizesRectangle(zigzag) && !RecognizesRectangle(denseZigzag),
			"sampling density cannot hide sustained zigzags")) ++failures;
		const auto triangle = EvaluateSample({ { {0,100,3},{90,0,3},{180,100,3},{0,100,3} } });
		if (!Expect(triangle.selection.index < triangle.count &&
			triangle.hypotheses[triangle.selection.index].model == ShapeModelKind::Triangle &&
			triangle.result.type == ShapeType::Unknown, "triangle remains a competing model")) ++failures;
		if (!Expect(!RecognizesRectangle(RectangleSample(600,100,5)),
			"large correction movement preserves a very elongated rectangle")) ++failures;

		ShapeHypothesis rectangle;
		rectangle.eligible = rectangle.canCorrect = true; rectangle.quality = 0.94f;
		rectangle.toleranceDip = 6; rectangle.strokeCount = 4; rectangle.group = 4;
		rectangle.rejectionReason = ShapeRecognitionRejectReason::None;
		rectangle.fittedCorners = { CvPoint{0,0,0},CvPoint{180,0,0},CvPoint{180,100,0},CvPoint{0,100,0} };
		auto ellipse = rectangle; ellipse.model = ShapeModelKind::Ellipse; ellipse.canCorrect = false; ellipse.quality = 0.82f;
		auto line = rectangle; line.model = ShapeModelKind::Line; line.closed = false; line.quality = 0.99f; line.group = 1;
		std::array<ShapeHypothesis, 3> models = { rectangle,ellipse,line };
		for (int i = 0; i < 6; ++i)
		{
			const auto selected = SelectShapeHypothesis(models, {});
			if (!Expect(selected.rejectionReason == ShapeRecognitionRejectReason::None &&
				models[selected.index].model == ShapeModelKind::Rectangle,
				"model ordering and a final straight edge do not change the winner")) ++failures;
			std::rotate(models.begin(), models.begin() + 1, models.end());
			if (i == 2) std::reverse(models.begin(), models.end());
		}
		ellipse.quality = 0.90f;
		models = { rectangle,ellipse,line };
		if (!Expect(SelectShapeHypothesis(models, {}).rejectionReason == ShapeRecognitionRejectReason::AmbiguousModels,
			"close model scores reject automatic correction")) ++failures;
		ellipse.quality = 0.99f; models = { rectangle,ellipse,line };
		if (!Expect(SelectShapeHypothesis(models, {}).rejectionReason == ShapeRecognitionRejectReason::AmbiguousModels,
			"unsupported model cannot be filtered out before arbitration")) ++failures;
		rectangle.quality = 0.82f; models = { rectangle,ellipse,line };
		if (!Expect(SelectShapeHypothesis(models, {}).rejectionReason == ShapeRecognitionRejectReason::UnsupportedShape,
			"clear unsupported winner keeps original ink")) ++failures;
		rectangle.quality = 0.94f;
		auto duplicate = rectangle; duplicate.group = duplicate.strokeCount = 5; duplicate.quality = 0.93f;
		models = { rectangle,duplicate,line };
		auto selected = SelectShapeHypothesis(models, {});
		if (!Expect(selected.rejectionReason == ShapeRecognitionRejectReason::None && models[selected.index].strokeCount == 5,
			"equivalent suffixes include valid retracing")) ++failures;
		for (auto& corner : duplicate.fittedCorners) corner.x += 1000;
		models = { rectangle,duplicate,line };
		if (!Expect(SelectShapeHypothesis(models, {}).rejectionReason == ShapeRecognitionRejectReason::AmbiguousModels,
			"different plausible rectangle groups are ambiguous")) ++failures;

		context = {}; context.physicalScaleTrusted = true;
		context.millimetersPerDipX = 0.25f; context.millimetersPerDipY = 0.30f;
		const auto physical = EvaluateSample(rough,context);
		if (!Expect(physical.result.type == ShapeType::AxisAlignedRectangle,
			"trusted physical prior retains a clear hand-drawn rectangle")) ++failures;
		for (const auto& model : physical.hypotheses)
			if (model.toleranceDip > 0 && !Expect(model.toleranceDipX >= model.toleranceDip*0.75f &&
				model.toleranceDipX <= std::min(model.toleranceDip*1.25f,14.0f) &&
				model.toleranceDipY >= model.toleranceDip*0.75f && model.toleranceDipY <= std::min(model.toleranceDip*1.25f,14.0f),
				"physical prior stays within its bounded tolerance")) ++failures;
		context.millimetersPerDipX = std::numeric_limits<float>::quiet_NaN();
		ShapeRecognitionDiagnostics invalidPhysical;
		if (!Expect(EvaluateSample(rough,context,&invalidPhysical).result.type == ShapeType::AxisAlignedRectangle &&
			!invalidPhysical.context.physicalScaleTrusted && invalidPhysical.context.scaleFallbackReason == ShapeScaleFallbackReason::InvalidPhysicalScale,
			"invalid physical scale falls back to DIP")) ++failures;
		context = {}; context.dipPerPixelY = 0;
		if (!Expect(EvaluateSample(rough,context).result.type == ShapeType::Unknown,
			"invalid logical scale fails closed")) ++failures;
		auto invisible = RectangleSample(180,100);
		for (auto& p : invisible.back()) p.width = 0;
		if (!Expect(!RecognizesRectangle(invisible), "an invisible zero-width edge is not shape evidence")) ++failures;
		auto invalid = rough; invalid[0][0].x = std::numeric_limits<float>::quiet_NaN();
		if (!Expect(!RecognizesRectangle(invalid), "invalid source geometry fails closed")) ++failures;
		auto dense = RectangleSample(600,200);
		for (auto& stroke : dense)
		{
			const auto a = stroke.front(), b = stroke.back(); stroke.clear();
			for (int i = 0; i < 5000; ++i)
			{
				const float t = i / 4999.0f;
				stroke.push_back({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,3});
			}
		}
		ShapeRecognitionDiagnostics budget;
		const auto started = std::chrono::steady_clock::now();
		if (!Expect(EvaluateSample(dense,{},&budget).result.type == ShapeType::AxisAlignedRectangle && budget.sampledPointCount <= 4096,
			"dense input remains within the shared sample budget")) ++failures;
		std::cout << "[Draw3ShapeRecognition] dense_sample_ms=" <<
			std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count() << '\n';
		return failures;
	}

	int RunRecognitionScaleTests()
	{
		using namespace Inkeys::Drawing::Draw3;
		int failures = 0;
		SpeedEraser::DisplayScale display;
		display.monitor = 1; display.generation = 7;
		display.pixelWidth = 1920; display.pixelHeight = 1080;
		display.logicalOutputKnown = display.physicalAvailable = true;
		display.cmPerPixelX = display.cmPerPixelY = 0.025f;
		SpeedEraser::InputSource pen;
		pen.kind = SpeedEraser::SourceKind::IntegratedPen;
		pen.recognition = SpeedEraser::SourceRecognition::PointerCursor;
		pen.contextId = 2; pen.cursorId = 3; pen.generation = 4;
		pen.mappedMonitor = 1; pen.mappedWidth = 1920; pen.mappedHeight = 1080;
		const auto scale = CaptureShapeRecognitionScale(display,pen);
		const std::array<RenderItemId,2> ids = { RenderItemId{1,3},RenderItemId{2,3} };
		std::array<ShapeStrokeProvenance,2> provenance = { ShapeStrokeProvenance{ids[0],scale},ShapeStrokeProvenance{ids[1],scale} };
		const auto current = CaptureShapeRecognitionScale(display,{});
		if (!Expect(ResolveShapeRecognitionContext(current,provenance,ids).physicalScaleTrusted,
			"matching direct-input strokes enable EDID scale")) ++failures;
		if (!Expect(!ResolveShapeRecognitionContext(current,{provenance.data(),1},ids).physicalScaleTrusted,
			"loaded or missing provenance uses DIP")) ++failures;
		provenance[1].scale.sourceGeneration++;
		if (!Expect(!ResolveShapeRecognitionContext(current,provenance,ids).physicalScaleTrusted &&
			ResolveShapeRecognitionContext(current,provenance,ids).scaleFallbackReason == Inkeys::CV::ShapeScaleFallbackReason::SourceChanged,
			"mixed source epochs use DIP")) ++failures;
		provenance[1].scale = scale;
		display.generation++;
		if (!Expect(!ResolveShapeRecognitionContext(CaptureShapeRecognitionScale(display,{}),provenance,ids).physicalScaleTrusted,
			"changed display epoch cannot reuse old physical provenance")) ++failures;
		display.generation--;
		for (auto kind : { SpeedEraser::SourceKind::Mouse,SpeedEraser::SourceKind::ExternalPen,SpeedEraser::SourceKind::Unknown })
		{
			pen.kind = kind;
			if (!Expect(!CaptureShapeRecognitionScale(display,pen).context.physicalScaleTrusted,
				"indirect and unknown sources cannot use physical scale")) ++failures;
		}
		pen.kind = SpeedEraser::SourceKind::IntegratedPen;
		pen.mappedMonitor = 2;
		if (!Expect(!CaptureShapeRecognitionScale(display,pen).context.physicalScaleTrusted,
			"mismatched input mapping uses DIP")) ++failures;
		pen.mappedMonitor = 1; display.physicalAvailable = false;
		if (!Expect(!CaptureShapeRecognitionScale(display,pen).context.physicalScaleTrusted,
			"missing EDID scale uses DIP")) ++failures;
		display.physicalAvailable = true; display.dipPerPixelX = 0.5f; display.dipPerPixelY = 0.8f;
		const auto fractional = CaptureShapeRecognitionScale(display,pen);
		if (!Expect(std::abs(fractional.context.millimetersPerDipX - 0.5f) < 0.0001f &&
			std::abs(fractional.context.millimetersPerDipY - 0.3125f) < 0.0001f,
			"physical conversion respects separate XY DIP scales")) ++failures;
		return failures;
	}

	int RunRecognitionSamples()
	{
		int failures = 0;
		if (!Expect(RecognizesRectangle(StandardRectangle()),
			"standard four-stroke rectangle")) ++failures;
		if (!Expect(RecognizesRectangle({ {
			{ 10.0f, 20.0f, 5.0f }, { 130.0f, 20.0f, 5.0f },
			{ 130.0f, 100.0f, 5.0f }, { 10.0f, 100.0f, 5.0f },
			{ 10.0f, 20.0f, 5.0f } } }), "single closed stroke")) ++failures;
		Inkeys::CV::ShapeRecognitionDiagnostics roughDiagnostics;
		const std::vector<CvStroke> roughRectangle = { {
			{ 12.0f, 23.0f, 4.2f }, { 35.0f, 18.0f, 5.1f },
			{ 72.0f, 22.0f, 5.8f }, { 128.0f, 17.0f, 4.7f },
			{ 133.0f, 43.0f, 5.4f }, { 129.0f, 76.0f, 6.1f },
			{ 134.0f, 101.0f, 4.9f }, { 99.0f, 97.0f, 5.6f },
			{ 61.0f, 103.0f, 4.3f }, { 14.0f, 98.0f, 5.2f },
			{ 8.0f, 72.0f, 5.9f }, { 13.0f, 42.0f, 4.6f },
			{ 12.0f, 23.0f, 5.0f } } };
		const Inkeys::CV::ShapeResult rough = RecognizeShape(
			roughRectangle, &roughDiagnostics);
		if (rough.type == Inkeys::CV::ShapeType::Unknown)
		{
			std::cerr << "[Draw3ShapeRecognition] rough_reject=" << Inkeys::CV::ShapeRecognitionRejectReasonName(roughDiagnostics.rejectionReason) <<
				" max_deviation=" << roughDiagnostics.maximumAxisDeviationDegrees << " local=[";
			for (const auto value : roughDiagnostics.edgeSegmentAxisDeviationDegrees) std::cerr << value << ',';
			std::cerr << "] fit=[";
			for (const auto value : roughDiagnostics.edgeFitAxisDeviationDegrees) std::cerr << value << ',';
			std::cerr << "]\n";
		}
		if (!Expect(rough.type == Inkeys::CV::ShapeType::AxisAlignedRectangle &&
			roughDiagnostics.selectedApproximationEpsilonRatio >= 0.02f &&
			roughDiagnostics.edgeBandDip >=
				roughDiagnostics.thresholds.minimumEdgeBandDip,
			"rough pressure-varying single stroke")) ++failures;
		Inkeys::CV::ShapeRecognitionDiagnostics multiScaleDiagnostics;
		const Inkeys::CV::ShapeResult multiScale = RecognizeShape({ {
			{ 10.0f, 20.0f, 6.0f }, { 70.0f, 8.0f, 6.0f },
			{ 130.0f, 20.0f, 6.0f }, { 130.0f, 100.0f, 6.0f },
			{ 10.0f, 100.0f, 6.0f }, { 10.0f, 20.0f, 6.0f } } },
			&multiScaleDiagnostics);
		if (!Expect(multiScale.type == Inkeys::CV::ShapeType::AxisAlignedRectangle &&
			multiScaleDiagnostics.approximationCornerCounts[0] == 5 &&
			multiScaleDiagnostics.selectedApproximationEpsilonRatio > 0.02f,
			"five-corner hull falls back to a coarser quad")) ++failures;
		if (!Expect(RecognizesRectangle({ {
			{ 10.0f, 100.0f, 5.0f }, { 10.0f, 20.0f, 5.0f },
			{ 130.0f, 20.0f, 5.0f } }, {
			{ 130.0f, 20.0f, 5.0f }, { 130.0f, 100.0f, 5.0f },
			{ 10.0f, 100.0f, 5.0f } } }), "two L-shaped strokes")) ++failures;
		if (!Expect(RecognizesRectangle({
			Line(130.0f, 100.0f, 130.0f, 20.0f),
			Line(10.0f, 20.0f, 10.0f, 100.0f),
			Line(130.0f, 20.0f, 10.0f, 20.0f),
			Line(10.0f, 100.0f, 130.0f, 100.0f) }),
			"reverse and unordered edges")) ++failures;
		if (!Expect(RecognizesRectangle({ {
			{ 10.0f, 20.0f, 5.0f }, { 50.0f, 19.0f, 5.0f },
			{ 90.0f, 21.0f, 5.0f }, { 130.0f, 20.0f, 5.0f } },
			{ { 130.0f, 20.0f, 5.0f }, { 129.0f, 60.0f, 5.0f },
			{ 131.0f, 100.0f, 5.0f } },
			{ { 131.0f, 100.0f, 5.0f }, { 70.0f, 99.0f, 5.0f },
			{ 10.0f, 100.0f, 5.0f } },
			{ { 10.0f, 100.0f, 5.0f }, { 11.0f, 60.0f, 5.0f },
			{ 10.0f, 20.0f, 5.0f } } }), "slight jitter")) ++failures;
		if (!Expect(RecognizesRectangle({
			Line(16.0f, 20.0f, 124.0f, 20.0f),
			Line(130.0f, 26.0f, 130.0f, 94.0f),
			Line(124.0f, 100.0f, 16.0f, 100.0f),
			Line(10.0f, 94.0f, 10.0f, 26.0f) }), "small corner gaps")) ++failures;
		auto repeated = StandardRectangle();
		repeated.push_back(Line(10.0f, 20.5f, 130.0f, 20.5f));
		if (!Expect(RecognizesRectangle(repeated), "repeated edge")) ++failures;

		Inkeys::CV::ShapeRecognitionDiagnostics dpi1Diagnostics;
		Inkeys::CV::ShapeRecognitionDiagnostics dpi2Diagnostics;
		const Inkeys::CV::ShapeResult dpi1 = RecognizeShape(
			roughRectangle, &dpi1Diagnostics, 1.0f);
		const Inkeys::CV::ShapeResult dpi2 = RecognizeShape(
			ScaleStrokes(roughRectangle, 2.0f), &dpi2Diagnostics, 2.0f);
		if (!Expect(dpi1.type == Inkeys::CV::ShapeType::AxisAlignedRectangle &&
			dpi2.type == Inkeys::CV::ShapeType::AxisAlignedRectangle &&
			std::abs(dpi1.rectangle.left - dpi2.rectangle.left / 2.0f) < 0.01f &&
			std::abs(dpi1Diagnostics.edgeBandDip - dpi2Diagnostics.edgeBandDip) < 0.01f,
			"DPI-scaled recognition is DIP-equivalent")) ++failures;

		if (!Expect(!RecognizesRectangle({
			Line(0.0f, 0.0f, 20.0f, 0.0f), Line(20.0f, 0.0f, 20.0f, 20.0f),
			Line(20.0f, 20.0f, 0.0f, 20.0f), Line(0.0f, 20.0f, 0.0f, 0.0f) }),
			"too small")) ++failures;
		if (!Expect(!RecognizesRectangle({
			Line(10.0f, 20.0f, 10.0f, 100.0f),
			Line(10.0f, 100.0f, 130.0f, 100.0f),
			Line(130.0f, 100.0f, 130.0f, 20.0f) }), "open U")) ++failures;
		if (!Expect(!RecognizesRectangle({ {
			{ 10.0f, 20.0f, 5.0f }, { 130.0f, 20.0f, 5.0f },
			{ 130.0f, 100.0f, 5.0f }, { 10.0f, 100.0f, 5.0f } } }),
			"open C")) ++failures;
		if (!Expect(!RecognizesRectangle({
			Line(10.0f, 20.0f, 130.0f, 20.0f),
			Line(130.0f, 20.0f, 130.0f, 100.0f),
			Line(130.0f, 100.0f, 10.0f, 100.0f) }),
			"explicit missing edge")) ++failures;
		if (!Expect(!RecognizesRectangle({ {
			{ 30.0f, 20.0f, 5.0f }, { 110.0f, 20.0f, 5.0f },
			{ 130.0f, 40.0f, 5.0f }, { 130.0f, 80.0f, 5.0f },
			{ 110.0f, 100.0f, 5.0f }, { 30.0f, 100.0f, 5.0f },
			{ 10.0f, 80.0f, 5.0f }, { 10.0f, 40.0f, 5.0f },
			{ 30.0f, 20.0f, 5.0f } } }), "over-rounded corners")) ++failures;
		if (!Expect(!RecognizesRectangle({ {
			{ 10.0f, 20.0f, 5.0f }, { 30.0f, 27.0f, 5.0f },
			{ 50.0f, 20.0f, 5.0f }, { 70.0f, 27.0f, 5.0f },
			{ 90.0f, 20.0f, 5.0f }, { 110.0f, 27.0f, 5.0f },
			{ 130.0f, 20.0f, 5.0f } },
			Line(130.0f, 20.0f, 130.0f, 100.0f),
			Line(130.0f, 100.0f, 10.0f, 100.0f),
			Line(10.0f, 100.0f, 10.0f, 20.0f) }),
			"zigzag edge exceeds axis limit")) ++failures;
		if (!Expect(!RecognizesRectangle({ {
			{ 10.0f, 100.0f, 5.0f }, { 70.0f, 20.0f, 5.0f },
			{ 130.0f, 100.0f, 5.0f }, { 10.0f, 100.0f, 5.0f } } }),
			"triangle")) ++failures;
		if (!Expect(!RecognizesRectangle({ {
			{ 30.0f, 20.0f, 5.0f }, { 130.0f, 20.0f, 5.0f },
			{ 110.0f, 100.0f, 5.0f }, { 10.0f, 100.0f, 5.0f },
			{ 30.0f, 20.0f, 5.0f } } }), "parallelogram")) ++failures;
		if (!Expect(!RecognizesRectangle({ {
			{ 40.0f, 10.0f, 5.0f }, { 140.0f, 50.0f, 5.0f },
			{ 110.0f, 130.0f, 5.0f }, { 10.0f, 90.0f, 5.0f },
			{ 40.0f, 10.0f, 5.0f } } }), "tilted rectangle")) ++failures;
		if (!Expect(!RecognizesRectangle({
			Line(10.0f, 20.0f, 130.0f, 100.0f),
			Line(130.0f, 20.0f, 10.0f, 100.0f) }), "X strokes")) ++failures;
		auto interior = StandardRectangle();
		interior.push_back({ { 20.0f, 30.0f, 5.0f }, { 120.0f, 90.0f, 5.0f },
			{ 20.0f, 90.0f, 5.0f }, { 120.0f, 30.0f, 5.0f } });
		if (!Expect(!RecognizesRectangle(interior), "interior scribble")) ++failures;

		CvStroke circle;
		for (int index = 0; index <= 64; ++index)
		{
			const float angle = static_cast<float>(index) * 6.28318530718f / 64.0f;
			circle.push_back({ 70.0f + 60.0f * std::cos(angle),
				60.0f + 40.0f * std::sin(angle), 5.0f });
		}
		if (!Expect(!RecognizesRectangle({ circle }), "ellipse")) ++failures;

		Inkeys::CV::ShapeRecognitionDiagnostics acceptedDiagnostics;
		const Inkeys::CV::ShapeResult accepted = RecognizeShape(
			StandardRectangle(), &acceptedDiagnostics);
		if (!Expect(accepted.type == Inkeys::CV::ShapeType::AxisAlignedRectangle &&
			acceptedDiagnostics.accepted &&
			acceptedDiagnostics.failedConditionCount == 0 &&
			acceptedDiagnostics.strokes[0].sampledPoints.size() ==
				acceptedDiagnostics.strokes[0].sampledPointCount &&
			acceptedDiagnostics.rejectionReason ==
				Inkeys::CV::ShapeRecognitionRejectReason::None &&
			acceptedDiagnostics.sourcePointCount == 8 &&
			acceptedDiagnostics.sampledPointCount >= 4 &&
			acceptedDiagnostics.sampledPointCount <=
				acceptedDiagnostics.thresholds.maximumTotalSampledPoints &&
			acceptedDiagnostics.confidence >=
				acceptedDiagnostics.thresholds.minimumConfidence,
			"accepted sample exposes complete diagnostics")) ++failures;

		Inkeys::CV::ShapeRecognitionDiagnostics smallDiagnostics;
		const Inkeys::CV::ShapeResult small = RecognizeShape({
			Line(0.0f, 0.0f, 20.0f, 0.0f), Line(20.0f, 0.0f, 20.0f, 20.0f),
			Line(20.0f, 20.0f, 0.0f, 20.0f), Line(0.0f, 20.0f, 0.0f, 0.0f) },
			&smallDiagnostics);
		if (!Expect(small.type == Inkeys::CV::ShapeType::Unknown &&
			!smallDiagnostics.accepted &&
			smallDiagnostics.rejectionReason ==
				Inkeys::CV::ShapeRecognitionRejectReason::ShapeTooSmall &&
			smallDiagnostics.shortSide == 20.0f &&
			smallDiagnostics.minimumShortSide == 64.0f &&
			smallDiagnostics.thresholds.minimumShortSideWidthMultiple == 8.0f &&
			std::string_view(Inkeys::CV::ShapeRecognitionRejectReasonName(
				smallDiagnostics.rejectionReason)) == "shape_too_small" &&
			smallDiagnostics.failedConditionCount >= 1 &&
			std::isfinite(smallDiagnostics.edgeCoverage[0]) &&
			std::isfinite(smallDiagnostics.totalCoverage) &&
			std::isfinite(smallDiagnostics.confidence),
			"rejected sample exposes reason metric and threshold")) ++failures;
		return failures;
	}

	Inkeys::Drawing::Draw3::StrokeTileFootprint Footprint(int tileX)
	{
		using namespace Inkeys::Drawing::Draw3;
		return { { static_cast<float>(tileX * 256), 0.0f,
			static_cast<float>((tileX + 1) * 256), 128.0f },
			{ { tileX * 2, 0 } }, { { tileX, 0 } } };
	}

	int RunConditionalHistoryTests()
	{
		using namespace Inkeys::Drawing::Draw3;
		int failures = 0;
		CanvasRuntimeHistory history;
		const auto first = history.AppendStroke(0, Footprint(0));
		const auto second = history.AppendStroke(1, Footprint(1));
		if (!Expect(first.has_value() && second.has_value(), "history sources append"))
			return failures + 1;
		const std::array<RenderItemId, 2> sources = { *first, *second };
		if (!Expect(history.SetRenderOnlyWhenLatest(sources, true),
			"mark conditional sources")) ++failures;
		const auto correction = history.AppendStroke(2, Footprint(2));
		if (!Expect(correction.has_value(), "correction append")) return failures + 1;
		if (!Expect(!history.Find(*first)->visible && !history.Find(*second)->visible &&
			history.Find(*correction)->visible, "correction hides sources")) ++failures;
		if (!Expect(history.VisibleCompositionTiles({ -1.0f, -1.0f, 1024.0f, 256.0f }) ==
			std::vector<SignedTileCoordinate>{ { 2, 0 } },
			"correction tile visibility")) ++failures;
		if (!Expect(history.UndoLastVisible(*correction) &&
			history.Find(*first)->visible && history.Find(*second)->visible,
			"undo correction restores all sources")) ++failures;
		if (!Expect(history.VisibleCompositionTiles({ -1.0f, -1.0f, 1024.0f, 256.0f }) ==
			std::vector<SignedTileCoordinate>{ { 0, 0 }, { 1, 0 } },
			"undo restores source tile references")) ++failures;
		if (!Expect(history.UndoLastVisible(*second) && history.Find(*first)->visible &&
			!history.Find(*second)->active, "source undo remains per stroke")) ++failures;
		if (!Expect(history.RedoLastUndone(*second) && history.Find(*second)->visible,
			"source redo restores conditional tail")) ++failures;
		if (!Expect(history.RedoLastUndone(*correction) &&
			!history.Find(*first)->visible && !history.Find(*second)->visible,
			"correction redo hides sources")) ++failures;
		if (!Expect(history.VisibleCompositionTiles({ -1.0f, -1.0f, 1024.0f, 256.0f }) ==
			std::vector<SignedTileCoordinate>{ { 2, 0 } },
			"redo restores correction tile references")) ++failures;

		CanvasRuntimeHistory metadataResetHistory;
		const auto resetSource = metadataResetHistory.AppendStroke(0, Footprint(5));
		if (!resetSource || !metadataResetHistory.SetRenderOnlyWhenLatest(
			std::span<const RenderItemId>(&*resetSource, 1), true) ||
			!metadataResetHistory.AppendStroke(1, Footprint(6))) return failures + 1;
		if (!Expect(!metadataResetHistory.Find(*resetSource)->visible &&
			metadataResetHistory.SetRenderOnlyWhenLatest(
				std::span<const RenderItemId>(&*resetSource, 1), false) &&
			metadataResetHistory.Find(*resetSource)->active &&
			metadataResetHistory.Find(*resetSource)->visible,
			"clearing final conditional metadata restores active visibility")) ++failures;

		if (!Expect(history.UndoLastVisible(*correction), "undo before branching")) ++failures;
		const std::vector<RenderItemId> cleared = history.ClearLatestConditionalGroup();
		history.DiscardRedoBranch();
		const auto branch = history.AppendStroke(3, Footprint(3));
		if (!Expect(cleared.size() == 2 && branch.has_value() &&
			history.Find(*first)->visible && history.Find(*second)->visible &&
			!history.Find(*first)->renderOnlyWhenLatest && history.RedoDepth() == 0,
			"new branch solidifies restored sources")) ++failures;

		CanvasRuntimeHistory otherPage;
		const auto other = otherPage.AppendStroke(0, Footprint(4));
		if (!Expect(other.has_value() && otherPage.Find(*other)->visible &&
			history.Revision() != 0, "page histories remain isolated")) ++failures;
		return failures;
	}

	int RunSuffixGroupingTests()
	{
		using namespace Inkeys::Drawing::Draw3;
		int failures = 0;
		const auto check = [&](const std::vector<CvStroke>& input, std::size_t expectedSources,
			std::string_view name)
		{
			std::array<std::uint8_t,16> guid = {}; guid[15] = 19;
			InkPage page{InkGuid(guid)};
			auto* canvas = page.GetOrCreateCanvas(kDefaultDeviceKey);
			CanvasRuntimeHistory history;
			for (const auto& stroke : input)
			{
				std::vector<StoredInkPoint> points;
				for (const auto& p : stroke) points.push_back({p.x,p.y,p.width});
				const auto index = canvas->AppendStroke(InkStroke({StoredInkType::Pen,0x123456,1,0},std::move(points)));
				const auto footprint = index ? BuildStrokeTileFootprint(canvas->Strokes()[*index]) : std::nullopt;
				if (!index || !footprint || !history.AppendStroke(*index,*footprint)) return false;
			}
			const auto plan = BuildShapeCorrectionPlan(*canvas,history,1.0f);
			return Expect(expectedSources ? plan && plan->sourceItems.size() == expectedSources &&
				plan->sourceItems.back() == *history.LastVisibleItem() : !plan,name);
		};
		const CvStroke first = {{0,0,3},{240,0,3},{240,100,3},{0,100,3},{0,0,3}};
		const CvStroke second = {{260,0,3},{500,0,3},{500,100,3},{260,100,3},{260,0,3}};
		if (!check({first,second},1,"adjacent rectangles are not consumed as one larger shape")) ++failures;
		if (!check({RoundedRectangleSample(240,140,32)},1,
			"the correction adapter accepts a supported soft-corner rectangle")) ++failures;
		if (!check({ {{0,36,3},{0,140,3},{240,140,3}}, {{0,0,3},{240,0,3},{240,140,3}} },2,
			"a final straight side does not defeat a two-stroke endpoint closure")) ++failures;

		CvStroke circle;
		for (int i = 0; i <= 64; ++i)
		{
			const float a = i*6.28318530718f/64;
			circle.push_back({360+70*std::cos(a),80+70*std::sin(a),3});
		}
		if (!check({first,circle},0,"latest unsupported closed shape prevents an older rectangle fallback")) ++failures;
		if (!check({Line(0,0,65,0,3),Line(115,0,180,0,3),Line(180,0,180,100,3),
			Line(180,100,0,100,3),Line(0,100,0,0,3)},0,
			"large middle gap is not concealed by total coverage")) ++failures;
		return failures;
	}

	int RunAdapterAndTriggerTests()
	{
		using namespace Inkeys::Drawing::Draw3;
		int failures = 0;
		std::array<std::uint8_t, 16> bytes = {};
		bytes[15] = 1;
		InkPage page{ InkGuid(bytes) };
		InkCanvas* canvas = page.GetOrCreateCanvas(kDefaultDeviceKey);
		CanvasRuntimeHistory history;
		StoredInkStyle style{ StoredInkType::Pen, 0x123456u, 0.75f, 0 };
		for (const CvStroke& input : StandardRectangle())
		{
			std::vector<StoredInkPoint> points;
			for (const CvPoint& point : input)
				points.push_back({ point.x, point.y, point.width });
			const auto index = canvas->AppendStroke(InkStroke(style, std::move(points)));
			const auto footprint = index
				? BuildStrokeTileFootprint(canvas->Strokes()[*index]) : std::nullopt;
			if (!index || !footprint || !history.AppendStroke(*index, *footprint))
				return failures + 1;
		}
		ShapeRecognitionDatasetDiagnostics acceptedDataset;
		const auto plan = BuildShapeCorrectionPlan(
			*canvas, history, 1.0f, &acceptedDataset);
		if (!Expect(plan.has_value() && plan->sourceItems.size() == 4 &&
			plan->replacement.Style().inkType == StoredInkType::OutlineRectangle &&
			plan->replacement.Style().fallbackRgb == style.fallbackRgb &&
			plan->replacement.Style().opacity == style.opacity &&
			plan->replacement.Points()[0].width == 5.0f,
			"adapter preserves style and all source strokes")) ++failures;
		if (!Expect(acceptedDataset.accepted &&
			acceptedDataset.acceptedStrokeCount == 4 &&
			acceptedDataset.collectedStrokeCount == 4 &&
			acceptedDataset.attempts.size() == 4 &&
			acceptedDataset.attempts.front().outcome ==
				ShapeRecognitionAttemptOutcome::Accepted &&
			acceptedDataset.attempts.front().vision.accepted &&
			acceptedDataset.attempts.front().sourcePointCount == 8 &&
			acceptedDataset.attempts.front().sourceItems.size() == 4 &&
			acceptedDataset.strokes.size() == 4 &&
			acceptedDataset.strokes.front().pointsDip.size() == 2 &&
			acceptedDataset.strokes.front().pointsDip.front().x == 10.0f,
			"adapter reports accepted maximum suffix")) ++failures;

		std::array<std::uint8_t, 16> pressureBytes = {};
		pressureBytes[15] = 7;
		InkPage pressurePage{ InkGuid(pressureBytes) };
		InkCanvas* pressureCanvas = pressurePage.GetOrCreateCanvas(kDefaultDeviceKey);
		CanvasRuntimeHistory pressureHistory;
		const std::array<float, 4> pressureWidths = { 3.5f, 5.0f, 6.5f, 4.2f };
		const std::vector<CvStroke> pressureRectangle = StandardRectangle();
		for (std::size_t index = 0; index < pressureRectangle.size(); ++index)
		{
			std::vector<StoredInkPoint> points;
			for (const CvPoint& point : pressureRectangle[index])
				points.push_back({ point.x, point.y, pressureWidths[index] });
			const auto strokeIndex = pressureCanvas->AppendStroke(
				InkStroke(style, std::move(points)));
			const auto footprint = strokeIndex
				? BuildStrokeTileFootprint(pressureCanvas->Strokes()[*strokeIndex])
				: std::nullopt;
			if (!strokeIndex || !footprint ||
				!pressureHistory.AppendStroke(*strokeIndex, *footprint))
				return failures + 1;
		}
		ShapeRecognitionDatasetDiagnostics pressureDataset;
		const auto pressurePlan = BuildShapeCorrectionPlan(
			*pressureCanvas, pressureHistory, 1.0f, &pressureDataset);
		if (!Expect(pressurePlan.has_value() && pressurePlan->sourceItems.size() == 4 &&
			pressureDataset.collectedStrokeCount == 4 &&
			pressureDataset.collectionStopReason ==
				ShapeCandidateCollectionStopReason::HistoryStart,
			"pressure width changes do not split one pen style")) ++failures;
		if (!Expect(canvas->SetStrokeRenderOnlyWhenLatest(0, true) &&
			canvas->Strokes()[0].RenderOnlyWhenLatest() &&
			canvas->SetStrokeRenderOnlyWhenLatest(0, false) &&
			!canvas->Strokes()[0].RenderOnlyWhenLatest(),
			"conditional metadata stays on InkStroke")) ++failures;

		std::vector<StoredInkPoint> unrelated = {
			{ 200.0f, 200.0f, 5.0f }, { 240.0f, 230.0f, 5.0f } };
		// 尾段调试与识别条件标记必须独立，避免合并后的构造参数互相覆盖。
		InkStroke conditionalTail(style, unrelated, std::size_t{ 0 }, true);
		if (!Expect(conditionalTail.DebugTailSegmentStart() == std::size_t{ 0 } &&
			conditionalTail.RenderOnlyWhenLatest(),
			"tail diagnostics coexist with conditional metadata")) ++failures;
		conditionalTail.SetRenderOnlyWhenLatest(false);
		if (!Expect(conditionalTail.DebugTailSegmentStart() == std::size_t{ 0 } &&
			!conditionalTail.RenderOnlyWhenLatest() &&
			!InkStroke(style, unrelated).DebugTailSegmentStart(),
			"conditional changes preserve tail diagnostics and default metadata")) ++failures;

		const auto unrelatedIndex = canvas->AppendStroke(InkStroke(style, std::move(unrelated)));
		const auto unrelatedFootprint = unrelatedIndex
			? BuildStrokeTileFootprint(canvas->Strokes()[*unrelatedIndex]) : std::nullopt;
		if (!unrelatedIndex || !unrelatedFootprint ||
			!history.AppendStroke(*unrelatedIndex, *unrelatedFootprint)) return failures + 1;
		if (!Expect(!BuildShapeCorrectionPlan(*canvas, history, 1.0f).has_value(),
			"latest unrelated stroke blocks replacement")) ++failures;
		if (!Expect(!canvas->RollbackLastStroke(*unrelatedIndex - 1) &&
			canvas->RollbackLastStroke(*unrelatedIndex) && canvas->Strokes().size() == 4,
			"uncommitted tail rollback is index-guarded")) ++failures;

		std::array<std::uint8_t, 16> diagnosticBytes = {};
		diagnosticBytes[15] = 2;
		InkPage diagnosticPage{ InkGuid(diagnosticBytes) };
		InkCanvas* diagnosticCanvas = diagnosticPage.GetOrCreateCanvas(kDefaultDeviceKey);
		CanvasRuntimeHistory diagnosticHistory;
		ShapeRecognitionDatasetDiagnostics collectionDiagnostics;
		if (!Expect(diagnosticCanvas &&
			!BuildShapeCorrectionPlan(*diagnosticCanvas, diagnosticHistory, 1.0f,
				&collectionDiagnostics).has_value() &&
			collectionDiagnostics.collectionStopReason ==
				ShapeCandidateCollectionStopReason::NoActiveItem,
			"adapter reports no active candidate")) ++failures;

		StoredInkStyle highlighterStyle = style;
		highlighterStyle.inkType = StoredInkType::Highlighter;
		const auto highlighterIndex = diagnosticCanvas->AppendStroke(InkStroke(
			highlighterStyle, { { 0.0f, 0.0f, 5.0f }, { 100.0f, 0.0f, 5.0f } }));
		const auto highlighterFootprint = highlighterIndex
			? BuildStrokeTileFootprint(diagnosticCanvas->Strokes()[*highlighterIndex])
			: std::nullopt;
		if (!highlighterIndex || !highlighterFootprint ||
			!diagnosticHistory.AppendStroke(*highlighterIndex, *highlighterFootprint))
			return failures + 1;
		if (!Expect(!BuildShapeCorrectionPlan(*diagnosticCanvas, diagnosticHistory,
			1.0f, &collectionDiagnostics).has_value() &&
			collectionDiagnostics.collectionStopReason ==
				ShapeCandidateCollectionStopReason::NonPenStroke &&
			collectionDiagnostics.collectedStrokeCount == 0,
			"adapter reports non-pen boundary")) ++failures;

		diagnosticBytes[15] = 3;
		InkPage stylePage{ InkGuid(diagnosticBytes) };
		InkCanvas* styleCanvas = stylePage.GetOrCreateCanvas(kDefaultDeviceKey);
		CanvasRuntimeHistory styleHistory;
		StoredInkStyle alternateStyle = style;
		alternateStyle.fallbackRgb = 0x654321u;
		const std::array<StoredInkStyle, 2> styles = { style, alternateStyle };
		for (std::size_t index = 0; index < styles.size(); ++index)
		{
			const auto strokeIndex = styleCanvas->AppendStroke(InkStroke(styles[index],
				{ { 10.0f, 20.0f + static_cast<float>(index) * 80.0f, 5.0f },
				{ 130.0f, 20.0f + static_cast<float>(index) * 80.0f, 5.0f } }));
			const auto footprint = strokeIndex
				? BuildStrokeTileFootprint(styleCanvas->Strokes()[*strokeIndex])
				: std::nullopt;
			if (!strokeIndex || !footprint ||
				!styleHistory.AppendStroke(*strokeIndex, *footprint)) return failures + 1;
		}
		if (!Expect(!BuildShapeCorrectionPlan(*styleCanvas, styleHistory, 1.0f,
			&collectionDiagnostics).has_value() &&
			collectionDiagnostics.collectionStopReason ==
				ShapeCandidateCollectionStopReason::StyleMismatch &&
			collectionDiagnostics.collectedStrokeCount == 1 &&
			collectionDiagnostics.attempts.size() == 1,
			"adapter reports style boundary before vision attempt")) ++failures;

		diagnosticBytes[15] = 4;
		InkPage sixStrokePage{ InkGuid(diagnosticBytes) };
		InkCanvas* sixStrokeCanvas = sixStrokePage.GetOrCreateCanvas(kDefaultDeviceKey);
		CanvasRuntimeHistory sixStrokeHistory;
		for (std::size_t index = 0; index < 6; ++index)
		{
			const float y = 20.0f + static_cast<float>(index) * 10.0f;
			const auto strokeIndex = sixStrokeCanvas->AppendStroke(InkStroke(
				style, { { 10.0f, y, 5.0f }, { 130.0f, y, 5.0f } }));
			const auto footprint = strokeIndex
				? BuildStrokeTileFootprint(sixStrokeCanvas->Strokes()[*strokeIndex])
				: std::nullopt;
			if (!strokeIndex || !footprint ||
				!sixStrokeHistory.AppendStroke(*strokeIndex, *footprint)) return failures + 1;
		}
		if (!Expect(!BuildShapeCorrectionPlan(*sixStrokeCanvas, sixStrokeHistory, 1.0f,
			&collectionDiagnostics).has_value() &&
			collectionDiagnostics.collectedStrokeCount == 6 &&
			collectionDiagnostics.collectionStopReason ==
				ShapeCandidateCollectionStopReason::HistoryStart,
			"six-stroke history start is not reported as a truncated candidate")) ++failures;

		ShapeRecognitionDatasetDiagnostics formatterDiagnostics;
		formatterDiagnostics.attempts.emplace_back();
		auto& formatterAttempt = formatterDiagnostics.attempts.front();
		formatterAttempt.vision.dpiScale = 1.5f;
		formatterAttempt.vision.context.dipPerPixelX = formatterAttempt.vision.context.dipPerPixelY = 1 / 1.5f;
		formatterAttempt.vision.medianWidth = 7.0f;
		formatterAttempt.vision.bounds = { 1.0f, 2.0f, 31.0f, 42.0f };
		formatterAttempt.vision.rejectionReason =
			Inkeys::CV::ShapeRecognitionRejectReason::ShapeTooSmall;
		formatterAttempt.vision.failedConditions[0] =
			Inkeys::CV::ShapeRecognitionRejectReason::ShapeTooSmall;
		formatterAttempt.vision.failedConditionCount = 1;
		formatterAttempt.vision.hypothesisCount = 1;
		formatterAttempt.vision.hypotheses[0].quality = 0.9f;
		formatterAttempt.vision.inputStrokeCount = 1;
		formatterAttempt.vision.strokes[0].sourcePointCount = 2;
		formatterAttempt.vision.strokes[0].sampledPointCount = 2;
		formatterAttempt.vision.strokes[0].sampledPoints = {
			{ 1.5f, 3.0f, 4.5f }, { 6.0f, 7.5f, 4.5f } };
		formatterAttempt.representativeWidth = 0.0f;
		formatterDiagnostics.dpiScale = 1.5f;
		formatterDiagnostics.strokes.push_back({
			{ 3, 7 }, 11, 2, false,
			{ { 1.0f, 2.0f, 3.0f }, { 4.0f, 5.0f, 3.0f } } });
		std::ostringstream capturedDatasetOutput;
		std::streambuf* previousOutput = std::cout.rdbuf(capturedDatasetOutput.rdbuf());
		WriteShapeRecognitionDatasetDiagnostics(formatterDiagnostics);
		std::cout.rdbuf(previousOutput);
		const std::string formattedDataset = capturedDatasetOutput.str();
		if (!Expect(formattedDataset.find("dpi_scale=1.5000") != std::string::npos &&
			formattedDataset.find("record=model") != std::string::npos &&
			formattedDataset.find("quality=0.9000") != std::string::npos &&
			formattedDataset.find("record=selection") != std::string::npos &&
			formattedDataset.find("corner_transition_dip=") != std::string::npos &&
			formattedDataset.find("endpoint_closure_gap_dip=") != std::string::npos &&
			formattedDataset.find("corner_coverage_allowance=") != std::string::npos &&
			formattedDataset.find("max_closure_arc_ratio=") != std::string::npos &&
			formattedDataset.find("physical_trusted=false") != std::string::npos &&
			formattedDataset.find("record=startup format_version=3") != std::string::npos &&
			formattedDataset.find("record=stroke") != std::string::npos &&
			formattedDataset.find("stroke_id=7:3") != std::string::npos &&
			formattedDataset.find("points_dip=[(1.0000:2.0000:3.0000)") !=
				std::string::npos &&
			formattedDataset.find("sampled_points_dip=[(1.0000:2.0000:3.0000)") !=
				std::string::npos &&
			formattedDataset.find("median_width_px=7.0000") != std::string::npos &&
			formattedDataset.find("median_width_dip=4.6667") != std::string::npos &&
			formattedDataset.find("replacement_width_px=0.0000") != std::string::npos &&
			formattedDataset.find("vision_bounds_px=[1.0000,2.0000,31.0000,42.0000]") !=
				std::string::npos &&
			formattedDataset.find("primary_reject_reason=shape_too_small") !=
				std::string::npos &&
			formattedDataset.find("failed_conditions=[shape_too_small]") !=
				std::string::npos,
			"dataset formatter preserves rejected-sample scale and geometry")) ++failures;

		if (!Expect(!ShapeRecognitionDiagnosticsEnabled(),
			"dataset diagnostics default disabled")) ++failures;
		SetShapeRecognitionDiagnosticsEnabled(true);
		if (!Expect(ShapeRecognitionDiagnosticsEnabled(),
			"dataset diagnostics can be enabled independently")) ++failures;
		SetShapeRecognitionDiagnosticsEnabled(false);
		if (!Expect(!ShapeRecognitionDiagnosticsEnabled(),
			"dataset diagnostics can be disabled independently")) ++failures;

		if (!Expect(CanAttemptShapeRecognition({}), "idle trigger accepts once ready")) ++failures;
		if (!Expect(!CanAttemptShapeRecognition({ true, false, false, false, false }) &&
			!CanAttemptShapeRecognition({ false, true, false, false, false }) &&
			!CanAttemptShapeRecognition({ false, false, true, false, false }) &&
			!CanAttemptShapeRecognition({ false, false, false, true, false }) &&
			!CanAttemptShapeRecognition({ false, false, false, false, true }),
			"trigger rejects every active gate")) ++failures;

		ShapeRecognitionTriggerLatch latch;
		latch.Arm(page.PageGuid(), history.Revision());
		if (!Expect(latch.Pending() &&
			!latch.ConsumeIfReady(page.PageGuid(), history.Revision(),
				{ true, false, false, false, false }) && latch.Pending(),
			"trigger remains armed while a gate is active")) ++failures;
		if (!Expect(latch.ConsumeIfReady(page.PageGuid(), history.Revision(), {}) &&
			!latch.Pending() &&
			!latch.ConsumeIfReady(page.PageGuid(), history.Revision(), {}),
			"one pen commit is consumed exactly once")) ++failures;
		latch.Arm(page.PageGuid(), history.Revision());
		if (!Expect(!latch.ConsumeIfReady(
			page.PageGuid(), history.Revision() + 1, {}) && !latch.Pending(),
			"history change invalidates a pending commit")) ++failures;
		return failures;
	}
}

int RunDraw3ShapeRecognitionTests()
{
	return RunRecognitionSamples() + RunHandDrawnRegressionTests() + RunCornerAndClosureTests() + RunBalancedModelTests() +
		RunRecognitionScaleTests() + RunConditionalHistoryTests() + RunSuffixGroupingTests() +
		RunAdapterAndTriggerTests();
}
