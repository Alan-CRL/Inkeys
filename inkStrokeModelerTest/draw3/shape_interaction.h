#pragma once

#include "shape_core.h"

namespace draw3::shapes
{
	enum class EditHandleKind : uint32_t { Move, Rotate, Scale, Center, Endpoint, Vertex, Radius, Axis, Angle, Depth, Height, Focus };
	struct EditHandle
	{
		uint32_t id = 0;
		EditHandleKind kind = EditHandleKind::Move;
		Vec2 position;
		bool operator==(const EditHandle&) const = default;
	};
	// 0=整体移动，1=旋转；10/12/14/16=立体外框左上/右上/右下/左下。
	// 100=中心，101=半径，102/103=轴或角，104=深度，105=高度，106/107=F1/F2；1000+=端点/顶点。
	std::vector<EditHandle> BuildEditHandles(const ShapeObject&, const BuildOptions& = {}, Bounds* frameRect = nullptr);
	std::optional<ShapeObject> EditShape(const ShapeObject& before, uint32_t handleId,
		Vec2 down, Vec2 current, uint64_t newRevision, const BuildOptions& = {}, std::string* error = nullptr,
		std::optional<float> angleDelta = std::nullopt);
	std::optional<ShapeObject> FlipShape(const ShapeObject&, bool horizontal, uint64_t newRevision,
		const BuildOptions& = {}, std::string* error = nullptr);
	std::optional<ShapeObject> RotateShape(const ShapeObject&, float radians, uint64_t newRevision,
		const BuildOptions& = {}, std::string* error = nullptr);
	std::optional<ShapeObject> TransformShapeGesture(const ShapeObject&, Vec2 pivot, Vec2 translation,
		float scale, float rotation, uint64_t newRevision, const BuildOptions& = {}, std::string* error = nullptr);
	std::optional<ShapeObject> RestyleShape(const ShapeObject& before, const ShapeStyle&,
		uint64_t newRevision, const BuildOptions& = {}, std::string* error = nullptr);
	std::optional<ShapeObject> SetRegularSides(const ShapeObject&, uint32_t sides,
		uint64_t newRevision, std::string* error = nullptr);
	Construction MakeSolidConstruction(const ToolSpec&, Vec2, Vec2, float);
	std::optional<ShapeObject> SolidBasePreview(const ToolSpec&, Vec2, Vec2, const ShapeStyle&, const ObjectId&, uint64_t, std::string* error = nullptr);
	std::optional<ShapeObject> SetRightSolid(const ShapeObject&, bool, uint64_t, std::string* error = nullptr);
	std::optional<ShapeObject> SetPlanarConstraint(const ShapeObject&, PlanarConstraint,
		uint64_t newRevision, std::string* error = nullptr);
	class EditSession
	{
	public:
		bool Begin(const ShapeObject&, uint32_t handleId, Vec2 down, const BuildOptions& = {});
		// 无效拖动仍提供钳制后的Candidate；只有Commit返回的对象可写入历史。
		bool Preview(Vec2 current, uint64_t newRevision, std::string* error = nullptr,
			std::optional<float> angleDelta = std::nullopt);
		bool PreviewTransform(Vec2 pivot, Vec2 translation, float scale, float rotation,
			uint64_t newRevision, std::string* error = nullptr);
		std::optional<float> AngleDelta() const noexcept;
		const std::optional<ShapeObject>& Candidate() const noexcept;
		std::optional<ShapeObject> Commit();
		void Cancel() noexcept;
	private:
		std::optional<ShapeObject> before_, candidate_;
		uint32_t handleId_ = 0;
		Vec2 down_;
		BuildOptions options_;
		Vec2 lastValid_, anglePoint_;
		float lastAngle_ = 0, angleDelta_ = 0, lastValidAngle_ = 0;
		bool angleActive_ = false, canCommit_ = false;
	};
	struct ConstructionStatus
	{
		bool active = false;
		uint32_t step = 0, confirmedSteps = 0, totalSteps = 0;
		bool canComplete = false, canBack = false, finished = false, canForward = false;
		PresetKind preset = PresetKind::Line;
	};
	class ConstructionSession
	{
	public:
		bool Begin(const ToolSpec&, const ShapeStyle&, const ObjectId&, uint64_t revision = 1);
		void ObserveRaw(Vec2 point) noexcept; // 真实轨迹累计角度，预测端点不得改变圈数。
		bool PreviewDrag(Vec2 down, Vec2 end, std::string* error = nullptr);
		bool ConfirmDrag(Vec2 rawDown, Vec2 rawUp, std::string* error = nullptr);
		// 已确认顶点优先命中；调整期间不追加新点。
		bool BeginPointEdit(Vec2 down, float radius);
		bool BeginPointEditAt(size_t index, Vec2 down);
		bool CancelPointEdit();
		const std::vector<Vec2>& Points() const noexcept { return points_; }
		bool Back();
		bool Forward();
		void Cancel() noexcept;
		bool CanComplete() const noexcept;
		std::optional<ShapeObject> Complete();
		const std::optional<ShapeObject>& Candidate() const noexcept;
		ConstructionStatus Status() const noexcept;
	private:
		std::optional<ShapeObject> Generate(Vec2 down, Vec2 end, bool confirm, std::string* error);
		bool RebuildPoints(std::string* error = nullptr);
		ToolSpec tool_;
		ShapeStyle style_;
		ObjectId id_ = {};
		uint64_t revision_ = 1;
		bool active_ = false, finished_ = false;
		std::vector<Vec2> points_;
		std::vector<std::vector<Vec2>> pointUndo_;
		std::optional<size_t> pointEdit_;
		std::vector<Vec2> pointEditBefore_;
		Vec2 pointEditDown_;
		Vec2 start_, end_;
		uint32_t confirmed_ = 0;
		float lastAngle_ = 0, sweep_ = 0;
		bool angleStarted_ = false;
		std::optional<ShapeObject> candidate_, confirmedCandidate_;
		struct BackedStep
		{
			std::vector<Vec2> points;
			Vec2 start, end;
			uint32_t confirmed = 0;
			float lastAngle = 0, sweep = 0;
			bool finished = false, angleStarted = false;
			std::optional<ShapeObject> candidate;
		};
		std::vector<BackedStep> redo_;
	};
	// 每个文档runtime独占；恢复语义快照时仍申请新号，避免缓存与分支复用旧revision。
	class RevisionAllocator
	{
	public:
		uint64_t Next(const ObjectId&, uint64_t observed = 0);
	private:
		std::vector<std::pair<ObjectId, uint64_t>> revisions_;
	};
}
