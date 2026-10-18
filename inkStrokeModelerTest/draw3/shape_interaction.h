#pragma once

#include "shape_core.h"

namespace draw3::shapes
{
	enum class EditHandleKind : uint32_t { Move, Rotate, Scale, Center, Endpoint, Vertex, Radius, Axis, Angle, Depth, Height };
	struct EditHandle
	{
		uint32_t id = 0;
		EditHandleKind kind = EditHandleKind::Move;
		Vec2 position;
		bool operator==(const EditHandle&) const = default;
	};
	// id 在相同参数模板内稳定；0为本体移动，1为旋转，10..17为框缩放，1000+为顶点。
	std::vector<EditHandle> BuildEditHandles(const ShapeObject&, const BuildOptions& = {});
	std::optional<ShapeObject> EditShape(const ShapeObject& before, uint32_t handleId,
		Vec2 down, Vec2 current, uint64_t newRevision, const BuildOptions& = {}, std::string* error = nullptr);
	std::optional<ShapeObject> RestyleShape(const ShapeObject& before, const ShapeStyle&,
		uint64_t newRevision, const BuildOptions& = {}, std::string* error = nullptr);
	std::optional<ShapeObject> SetRegularSides(const ShapeObject&, uint32_t sides,
		uint64_t newRevision, std::string* error = nullptr);
	class EditSession
	{
	public:
		bool Begin(const ShapeObject&, uint32_t handleId, Vec2 down, const BuildOptions& = {});
		bool Preview(Vec2 current, uint64_t newRevision, std::string* error = nullptr);
		const std::optional<ShapeObject>& Candidate() const noexcept;
		std::optional<ShapeObject> Commit();
		void Cancel() noexcept;
	private:
		std::optional<ShapeObject> before_, candidate_;
		uint32_t handleId_ = 0;
		Vec2 down_;
		BuildOptions options_;
	};
	struct ConstructionStatus
	{
		bool active = false;
		uint32_t step = 0, confirmedSteps = 0;
		bool canComplete = false, canBack = false, finished = false;
		PresetKind preset = PresetKind::Line;
	};
	class ConstructionSession
	{
	public:
		bool Begin(const ToolSpec&, const ShapeStyle&, const ObjectId&, uint64_t revision = 1);
		void ObserveRaw(Vec2 point) noexcept; // 真实轨迹累计角度，预测端点不得改变圈数。
		bool PreviewDrag(Vec2 down, Vec2 end, std::string* error = nullptr);
		bool ConfirmDrag(Vec2 rawDown, Vec2 rawUp, std::string* error = nullptr);
		bool Back();
		void Cancel() noexcept;
		bool CanComplete() const noexcept;
		std::optional<ShapeObject> Complete();
		const std::optional<ShapeObject>& Candidate() const noexcept;
		ConstructionStatus Status() const noexcept;
	private:
		std::optional<ShapeObject> Generate(Vec2 down, Vec2 end, bool confirm, std::string* error);
		ToolSpec tool_;
		ShapeStyle style_;
		ObjectId id_ = {};
		uint64_t revision_ = 1;
		bool active_ = false, finished_ = false;
		std::vector<Vec2> points_;
		Vec2 start_, end_;
		uint32_t confirmed_ = 0;
		float lastAngle_ = 0, sweep_ = 0;
		bool angleStarted_ = false;
		std::optional<ShapeObject> candidate_, confirmedCandidate_;
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
