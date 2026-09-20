#include "pch.h"
#include "StrokeCollection.h"

DirectInkPresenter::Ink::StrokeCollection::StrokeCollection(ID2D1Factory* d2dFactory) : m_d2dFactory(d2dFactory)
{
}

DirectInkPresenter::Ink::Stroke* DirectInkPresenter::Ink::StrokeCollection::Create(D2D1_COLOR_F d2dColor)
{
	emplace_back(m_d2dFactory.Get(), nullptr, d2dColor, 5.f);
	m_operations.push_back({ true, back().GetUID() });
	return &back();
}

bool DirectInkPresenter::Ink::StrokeCollection::IsContainPoint(D2D1_POINT_2F d2dPoint, ID2D1Geometry* d2dGeometry)
{
	BOOL bContain = false;
	Utils::ThrowIfFailed(
		d2dGeometry->FillContainsPoint(
			d2dPoint,
			UI::Graphics::IdentityMatrix(),
			&bContain
		)
	);
	return bContain;
}

void DirectInkPresenter::Ink::StrokeCollection::Erase(ID2D1Geometry* d2dGeometry)
{
	for (auto it = begin(); it != end(); it++)
	{
		if (it->GetVisibility())
		{
			D2D1_GEOMETRY_RELATION d2dRelation = D2D1_GEOMETRY_RELATION_UNKNOWN;
			Utils::ThrowIfFailed(
				d2dGeometry->CompareWithGeometry(it->GetPathGeometry(), UI::Graphics::Matrix3x2F::Identity(), &d2dRelation)
			);
			if (
				d2dRelation == D2D1_GEOMETRY_RELATION_OVERLAP or
				d2dRelation == D2D1_GEOMETRY_RELATION_CONTAINS or
				d2dRelation == D2D1_GEOMETRY_RELATION_IS_CONTAINED
				)
			{
				// 禁用这个线段
				m_operations.push_back({ false, it->GetUID() });

				if (d2dRelation != D2D1_GEOMETRY_RELATION_CONTAINS)
				{
					// 需要构造子对象的标志
					bool stroke_splitted = true;
					// 构造子对象
					// 插入到后面
					std::list<Stroke>::iterator stroke;

					for (const auto& i : it->GetRawPoints())
					{
						if (IsContainPoint(i, d2dGeometry))
						{
							// 线段被分割
							// ——   ——
							// 
							stroke_splitted = true;
						}
						else
						{
							if (stroke_splitted)
							{
								stroke_splitted = false;
								stroke = emplace(it, m_d2dFactory.Get(), (*it).m_d2dStrokeStyle.Get(), (*it).m_d2dColor, (*it).m_strokeWidth);
								m_operations.push_back({ true, stroke->GetUID() });
							}
							stroke->Add(i);
						}
					}
				}
			}
		}
	}
}

void DirectInkPresenter::Ink::StrokeCollection::Undo()
{
	if (IsUndoAllow())
	{
		Execute(m_operatonStack.top(), true);
		m_operatonTrashStack.push(m_operatonStack.top());
		m_operatonStack.pop();
	}
}

void DirectInkPresenter::Ink::StrokeCollection::Redo()
{
	if (IsRedoAllow())
	{
		Execute(m_operatonTrashStack.top(), false);
		m_operatonStack.push(m_operatonTrashStack.top());
		m_operatonTrashStack.pop();
	}
}

bool DirectInkPresenter::Ink::StrokeCollection::IsUndoAllow()
{
	return !m_operatonStack.empty();
}

bool DirectInkPresenter::Ink::StrokeCollection::IsRedoAllow()
{
	return !m_operatonTrashStack.empty();
}

void DirectInkPresenter::Ink::StrokeCollection::Commit()
{
	if (!m_operations.empty())
	{
		Execute(m_operations, false);
		CleanTrashStack();
		m_operatonStack.push(m_operations);
		m_operations.clear();
	}
}

void DirectInkPresenter::Ink::StrokeCollection::DiscardChanges()
{
	if (!m_operations.empty())
	{
		Execute(m_operations, true);
		m_operatonTrashStack.push(m_operations);
		CleanTrashStack();
		m_operations.clear();
	}
}

void DirectInkPresenter::Ink::StrokeCollection::Draw(ID2D1RenderTarget* d2dRenderTarget, ID2D1Brush* d2dBrush)
{
	for (auto it = begin(); it != end(); it++)
	{
		if (it->GetVisibility())
		{
			d2dRenderTarget->FillGeometry(
				it->GetPathGeometry(),
				d2dBrush
			);
		}
	}
}

void DirectInkPresenter::Ink::StrokeCollection::CleanTrashStack()
{
	std::vector<UUID> strokeUUIDs = {};
	while (!m_operatonTrashStack.empty())
	{
		for (auto it = m_operatonTrashStack.top().begin(); it != m_operatonTrashStack.top().end(); it++)
		{
			if (it->bOperation == true)
			{
				strokeUUIDs.push_back(it->uuStrokeID);
			}
		}
		m_operatonTrashStack.pop();
	}
	Remove(strokeUUIDs);
}

void DirectInkPresenter::Ink::StrokeCollection::Remove(const std::vector<UUID>& strokeUIDs)
{
	for (auto it = rbegin(); it != rend();)
	{
		bool bErased = false;
		for (auto i = strokeUIDs.begin(); i != strokeUIDs.end(); i++)
		{
			if (it != rend() and it->GetUID() == *i)
			{
				bErased = true;
				it = decltype(it)(erase((++it).base()));
			}
		}
		if (!bErased)
		{
			it++;
		}
	}
}

void DirectInkPresenter::Ink::StrokeCollection::Execute(const std::vector<StrokeOperation>& strokeOperations, bool bRevert)
{
	for (auto it = rbegin(); it != rend(); it++)
	{
		for (const auto& i : strokeOperations)
		{
			if (it->GetUID() == i.uuStrokeID)
			{
				it->SetVisibility(bRevert ? !i.bOperation : i.bOperation);
			}
		}
	}
}

//DirectInkPresenter::Ink::Stroke* DirectInkPresenter::Ink::StrokeCollection::Find(DWORD dwContactID)
//{
//    DirectInkPresenter::Ink::Stroke* stroke = nullptr;
//    if (m_strokeContact.find(dwContactID) != m_strokeContact.end())
//    {
//        stroke = m_strokeContact[dwContactID];
//    }
//    return stroke;
//}

//void DirectInkPresenter::Ink::StrokeCollection::Update(ID2D1Factory* d2dFactory)
//{
//    ID2D1Geometry** d2dGeometryArray = new ID2D1Geometry*[size()];
//    Utils::ThrowIfFailed(
//        d2dGeometryArray ? S_OK : E_POINTER
//    );
//    for (int i = 0; i < size(); i++)
//    {
//        at(i).Update(d2dFactory);
//        d2dGeometryArray[i] = at(i).Geometry();
//    }
//    Utils::ThrowIfFailed(
//        d2dFactory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, d2dGeometryArray, size(), &m_d2dGeometryGroup)
//    );
//    delete[] d2dGeometryArray;
//}