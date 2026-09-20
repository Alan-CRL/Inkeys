#pragma once
#include "pch.h"
#include "Stroke.h"

namespace DirectInkPresenter
{
	namespace Ink
	{
		struct StrokeOperation
		{
			// true为启用，false为禁用
			bool bOperation;
			// 线条UUID
			UUID uuStrokeID;
		};

		class StrokeCollection : std::list<Stroke>
		{
		public:
			StrokeCollection(ID2D1Factory* d2dFactory);
			virtual ~StrokeCollection() = default;

			// 手动添加、擦除
			Stroke* Create(D2D1_COLOR_F d2dColor = UI::Graphics::ColorF(UI::Graphics::ColorF::Red));
			void Erase(ID2D1Geometry* d2dGeometry);
			// 撤销和重做
			void Undo();
			void Redo();
			// 查询是否允许撤销和重做
			bool IsUndoAllow();
			bool IsRedoAllow();
			// 将更改记录提交为一系列的命令
			// true xxxxx (启用 xxxxx)
			// ...
			// false xxx (禁用 xxx)
			void Commit();
			// 丢弃尚未提交的更改
			void DiscardChanges();
			// 绘制
			void Draw(ID2D1RenderTarget* d2dRenderTarget, ID2D1Brush* d2dBrush);
		private:
			// 清除临时撤回的线条数据，此后无法重做
			void CleanTrashStack();
			// 永久删除线条，不可恢复
			void Remove(const std::vector<UUID>& strokeUIDs);
			// 执行指令，更改线条的可见属性
			void Execute(const std::vector<StrokeOperation>& strokeOperations, bool bRevert);
			// 几何区域是否包括该点
			static inline bool IsContainPoint(D2D1_POINT_2F d2dPoint, ID2D1Geometry* d2dGeometry);

			Utils::ComPtr<ID2D1Factory> m_d2dFactory = nullptr;
			// 记录这一批次线条变更，由手动添加和擦除所产生的线条数据，提交到m_operationStack
			std::vector<StrokeOperation> m_operations = {};
			// 操作记录堆栈，记录了一系列命令
			std::stack<std::vector<StrokeOperation>> m_operatonStack = {};
			// 临时撤回的操作记录，记录了一系列命令
			std::stack<std::vector<StrokeOperation>> m_operatonTrashStack = {};
		};
	}
}