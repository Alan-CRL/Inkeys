#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <variant>

namespace draw3::shapes
{
	using ObjectId = std::array<uint8_t, 16>;
	struct Vec2 { float x = 0; float y = 0; bool operator==(const Vec2&) const = default; };
	struct Transform
	{
		float m11 = 1, m12 = 0, m21 = 0, m22 = 1, dx = 0, dy = 0;
		bool operator==(const Transform&) const = default;
	};
	// start/end 为线端点或矩形对角；圆/椭圆用 start 作圆心，end 作椭圆半轴，radius 作圆半径或矩形圆角。
	// 旧矩形例外：radius保存名义4 DIP，查询时由BuildOptions.displayScale转换。
	struct Geometry
	{
		Vec2 start, end;
		float radius = 0;
		std::vector<Vec2> vertices;
		bool operator==(const Geometry&) const = default;
	};
	enum class Kind : uint32_t { Line, Rectangle, Circle, Ellipse, Polyline, Polygon, Compound };
	enum class CreationMode : uint32_t { Drag, CenterRadius, Points, MultiStage, Parameters };
	enum class PresetKind : uint32_t { Line, Arrow, Rectangle, RoundedRectangle, Circle, Ellipse, Triangle, Parallelogram, Rhombus, Trapezoid, RegularPolygon,
		Polyline, Polygon, Arc, Sector, Cuboid, Cube, Cylinder, Cone, Prism, Pyramid };
	enum class FillMode : uint32_t { Outline, Fill, OutlineAndFill };
	struct ToolSpec
	{
		PresetKind preset = PresetKind::Line;
		CreationMode creationMode = CreationMode::Drag;
		uint32_t sides = 3;
		float rectangleCornerRadius = 4; // Bridge 使用 DIP；构造参数和工厂使用局部画布像素。
		bool operator==(const ToolSpec&) const = default;
	};
	struct VertexConstruction { std::vector<Vec2> vertices; bool operator==(const VertexConstruction&) const = default; };
	struct ArcConstruction { float sweepRadians = 0; bool operator==(const ArcConstruction&) const = default; };
	struct SolidConstruction
	{
		Vec2 depth; // 长方体/正方体的投影向量，不冒充真实边长。
		float height = 0; // 圆柱/圆锥/棱柱/棱锥的局部竖直高度。
		bool operator==(const SolidConstruction&) const = default;
	};
	using ConstructionParameters = std::variant<std::monostate, VertexConstruction, ArcConstruction, SolidConstruction>;
	// 局部构造参数是真值；geometry/parts 必须由同一构造器派生，保存后仍可按模板编辑。
	struct Construction
	{
		ToolSpec tool;
		Vec2 start, end;
		ConstructionParameters parameters;
		bool legacyArrow = false; // v2旧箭头保留按线长决定头部的公式。
		bool operator==(const Construction&) const = default;
	};
	enum class LinePattern : uint32_t { Solid, Dashed, Dotted, DashDot };
	enum class LegacyKind : uint32_t { None, SolidLine, DashedLine, OutlineRectangle, FilledRectangle };
	struct ShapeStyle
	{
		bool stroke = true, fill = false;
		uint32_t strokeRgb = 0, fillRgb = 0;
		float strokeOpacity = 1, fillOpacity = 1, width = 2;
		LinePattern linePattern = LinePattern::Solid;
		float dashOffset = 0;
		bool operator==(const ShapeStyle&) const = default;
	};
	struct ShapePart
	{
		uint32_t id = 1;
		Kind kind = Kind::Line;
		Geometry geometry;
		Transform transform;
		std::optional<ShapeStyle> style;
		bool operator==(const ShapePart&) const = default;
	};
	struct ShapeObject
	{
		ObjectId id = {};
		uint64_t revision = 1;
		Kind kind = Kind::Line;
		CreationMode creationMode = CreationMode::Drag;
		Geometry geometry;
		Transform transform;
		ShapeStyle style;
		std::vector<ShapePart> parts;
		LegacyKind legacyKind = LegacyKind::None;
		std::optional<Construction> construction;
		bool operator==(const ShapeObject&) const = default;
	};
	struct Bounds
	{
		float left = 0, top = 0, right = 0, bottom = 0;
		bool valid = false;
		bool operator==(const Bounds&) const = default;
	};
	struct BuildOptions
	{
		float displayScale = 1;
		float maxErrorPixels = 0.25f;
		bool operator==(const BuildOptions&) const = default;
	};
	struct Path
	{
		std::vector<Vec2> points;
		std::vector<float> lengths; // 与 points 同长；闭合轮廓最后一点重复起点。
		bool closed = false;
	};
	using Triangle = std::array<Vec2, 3>;
	enum class ControlKind : uint32_t { Endpoint, Vertex, Center, Radius, Corner };
	struct ControlPoint { uint32_t partId = 0, index = 0; ControlKind kind = ControlKind::Vertex; Vec2 position; };
	struct GeometryPart
	{
		uint32_t partId = 0;
		Kind kind = Kind::Line;
		Geometry geometry; // 原始局部参数，解析渲染可与 transform 组合使用。
		Transform transform;
		ShapeStyle style; // width 已按局部变换的等效比例变换到画布像素。
		std::vector<Path> paths;
		std::vector<Triangle> triangles;
		std::vector<ControlPoint> controls;
		Bounds bounds;
	};
	struct GeometryResult { std::vector<GeometryPart> parts; Bounds bounds; };
	enum class PrimitiveKind : uint32_t { Segment, Triangle, Ellipse, RoundedRectangle };
	inline constexpr uint32_t kPrimitiveForceSolid = 16; // 同一覆盖批次中的实线箭头不参与箭杆虚线。
	struct alignas(16) GpuPrimitive
	{
		Vec2 p0, p1, p2, p3;
		float pathStart = 0, pathLength = 0, halfWidth = 0, radius = 0;
		uint32_t kind = 0, flags = 0;
		float padding[2] = {};
	};
	static_assert(sizeof(GpuPrimitive) == 64);
	static_assert(offsetof(GpuPrimitive, p0) == 0 && offsetof(GpuPrimitive, p1) == 8);
	static_assert(offsetof(GpuPrimitive, p2) == 16 && offsetof(GpuPrimitive, p3) == 24);
	static_assert(offsetof(GpuPrimitive, pathStart) == 32 && offsetof(GpuPrimitive, pathLength) == 36);
	static_assert(offsetof(GpuPrimitive, halfWidth) == 40 && offsetof(GpuPrimitive, radius) == 44);
	static_assert(offsetof(GpuPrimitive, kind) == 48 && offsetof(GpuPrimitive, flags) == 52 && offsetof(GpuPrimitive, padding) == 56);
	struct RenderBatch
	{
		ShapeStyle style;
		bool fill = false;
		uint32_t partId = 0;
		std::vector<GpuPrimitive> primitives;
	};
	struct RenderData { std::vector<RenderBatch> batches; Bounds bounds; };
	struct ValidationResult
	{
		std::string error;
		explicit operator bool() const noexcept { return error.empty(); }
	};
	// 自定义处理器只输出局部几何；注册完成后 Freeze，渲染期间只读。
	using GeometryBuilder = std::function<bool(const Geometry&, const BuildOptions&, GeometryPart&, std::string&)>;
	class Registry
	{
	public:
		Registry();
		bool Register(Kind kind, GeometryBuilder builder);
		void Freeze() noexcept;
		bool IsFrozen() const noexcept;
		const GeometryBuilder* Find(Kind kind) const noexcept;
	private:
		std::vector<std::pair<Kind, GeometryBuilder>> builders_;
		bool frozen_ = false;
	};
	const Registry& DefaultRegistry();
	Vec2 ApplyTransform(const Transform&, Vec2) noexcept;
	Transform ComposeTransforms(const Transform& outer, const Transform& inner) noexcept;
	bool IsZeroId(const ObjectId&) noexcept;
	ValidationResult Validate(const ShapeObject&, const Registry& = DefaultRegistry());
	bool BuildGeometry(const ShapeObject&, GeometryResult&, const BuildOptions& = {},
		std::string* error = nullptr, const Registry& = DefaultRegistry());
	bool BuildRenderData(const ShapeObject&, RenderData&, const BuildOptions& = {},
		std::string* error = nullptr, const Registry& = DefaultRegistry());
	std::vector<ControlPoint> ControlPoints(const GeometryResult&);
	enum class HitKind : uint32_t { None, Fill, Stroke };
	struct HitResult { ObjectId objectId = {}; uint32_t partId = 0; HitKind kind = HitKind::None; float distance = 0; };
	HitResult HitTest(const ShapeObject&, const GeometryResult&, Vec2 point, float tolerance = 0);
	bool IsDashVisible(float pathDistance, const ShapeStyle&) noexcept;
	Bounds UnionBounds(Bounds, Bounds) noexcept;
	std::optional<ShapeObject> MakeConstructedShape(const Construction&, const ShapeStyle&,
		const ObjectId&, uint64_t revision = 1, std::string* error = nullptr);
	struct ToolCapabilities
	{
		bool enabled = false, multiStage = false, points = false, fill = false, regularSides = false;
	};
	ToolCapabilities GetToolCapabilities(PresetKind) noexcept;
	CreationMode DefaultCreationMode(PresetKind) noexcept;
	std::optional<ShapeObject> MakePresetShape(const ToolSpec&, Vec2 start, Vec2 end,
		const ShapeStyle&, const ObjectId&, uint64_t revision = 1, std::string* error = nullptr);
	std::optional<ShapeObject> MakeLegacyShape(LegacyKind, Vec2 start, Vec2 end,
		float width, uint32_t rgb, float opacity, ObjectId);
	class GeometryCache
	{
	public:
		std::shared_ptr<const GeometryResult> Get(const ShapeObject&, const BuildOptions& = {}, std::string* error = nullptr);
		Bounds Invalidate(const ObjectId&, Bounds replacement = {});
		void Clear() noexcept;
	private:
		struct Entry { ObjectId id; uint64_t revision; BuildOptions options; std::shared_ptr<const GeometryResult> geometry; };
		std::vector<Entry> entries_;
	};
	enum class CommandKind : uint32_t { Create, ReplaceGeometry, ChangeStyle, Transform, Delete };
	struct ShapeCommand
	{
		CommandKind kind = CommandKind::Create;
		ObjectId objectId = {};
		uint64_t expectedRevision = 0;
		std::optional<ShapeObject> before, after;
	};
	// 先校验全部快照，再一次性替换目标；失败时不会部分修改。
	bool ApplyCommand(const ShapeCommand&, std::optional<ShapeObject>& target, std::string* error = nullptr);
	ShapeCommand InverseCommand(const ShapeCommand&);
	class CreationTransaction
	{
	public:
		bool Preview(ShapeObject candidate, std::string* error = nullptr);
		const std::optional<ShapeObject>& Candidate() const noexcept;
		std::optional<ShapeCommand> Commit(std::string* error = nullptr);
		void Cancel() noexcept;
	private:
		std::optional<ShapeObject> candidate_;
	};
}
