module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

module draw3.uink_draw3_import;

namespace draw3::uink
{
	namespace
	{
		UInkMessagePackValue TextValue(std::string value)
		{
			UInkMessagePackValue result;
			result.value = std::move(value);
			return result;
		}

		bool IsFinitePositive(float value) noexcept
		{
			return std::isfinite(value) && value > 0.0f;
		}

		std::optional<Draw3UInkStrokeSnapshot> ImportInk(const UInkInk& ink)
		{
			if (ink.renderOnlyWhenLatest || ink.extra || ink.color.extended ||
				ink.effectiveTexture != 0 || ink.points.empty() ||
				!std::isfinite(ink.opacity) || ink.opacity < 0.0f || ink.opacity > 1.0f)
				return std::nullopt;
			Draw3UInkStrokeSnapshot result;
			switch (ink.effectiveKind)
			{
			case UInkInkKind::Erase: result.style.kind = Draw3UInkStrokeKind::Eraser; break;
			case UInkInkKind::Pen: result.style.kind = Draw3UInkStrokeKind::Pen; break;
			case UInkInkKind::Highlighter:
				result.style.kind = Draw3UInkStrokeKind::Highlighter; break;
			default: return std::nullopt;
			}
			result.style.opacity = ink.opacity;
			result.style.fallbackRgb = ink.color.fallbackRgb;
			result.style.texture = 0;
			result.undoId = ink.undoId;
			for (const UInkInkPoint& point : ink.points)
			{
				if (point.style || !std::isfinite(point.x) || !std::isfinite(point.y) ||
					!IsFinitePositive(point.width)) return std::nullopt;
				result.points.push_back({ point.x, point.y, point.width });
			}
			return result;
		}

		std::optional<Draw3UInkStrokeSnapshot> ImportShape(const UInkShape& shape)
		{
			if (shape.renderOnlyWhenLatest || shape.extra) return std::nullopt;
			Draw3UInkStrokeSnapshot result;
			result.undoId = shape.undoId;
			if (const auto* line = std::get_if<UInkLineGeometry>(&shape.geometry))
			{
				if (shape.declaredShapeType != 0 || line->points.size() != 2 ||
					!shape.stroke || shape.fill || shape.stroke->color.extended ||
					shape.stroke->effectiveStartMarker != 0 ||
					shape.stroke->effectiveEndMarker != 0 ||
					!IsFinitePositive(shape.stroke->width)) return std::nullopt;
				result.style.kind = shape.stroke->dashArray.empty()
					? Draw3UInkStrokeKind::SolidLine
					: Draw3UInkStrokeKind::DashedLine;
				result.style.opacity = shape.stroke->opacity;
				result.style.fallbackRgb = shape.stroke->color.fallbackRgb;
				for (const UInkShapePoint& point : line->points)
				{
					if (!std::isfinite(point.x) || !std::isfinite(point.y)) return std::nullopt;
					result.points.push_back({ point.x, point.y, shape.stroke->width });
				}
				return result;
			}

			const auto* rectangle = std::get_if<UInkRectangleGeometry>(&shape.geometry);
			if (!rectangle || shape.declaredShapeType != 2 ||
				!std::isfinite(rectangle->centerX) || !std::isfinite(rectangle->centerY) ||
				!IsFinitePositive(rectangle->width) || !IsFinitePositive(rectangle->height) ||
				rectangle->rotation != 0.0f || static_cast<bool>(shape.stroke) ==
					static_cast<bool>(shape.fill)) return std::nullopt;
			float width = 1.0f;
			if (shape.stroke)
			{
				if (shape.stroke->color.extended || !shape.stroke->dashArray.empty() ||
					!IsFinitePositive(shape.stroke->width)) return std::nullopt;
				result.style.kind = Draw3UInkStrokeKind::OutlineRectangle;
				result.style.opacity = shape.stroke->opacity;
				result.style.fallbackRgb = shape.stroke->color.fallbackRgb;
				width = shape.stroke->width;
			}
			else
			{
				if (!shape.fill || shape.fill->color.extended) return std::nullopt;
				result.style.kind = Draw3UInkStrokeKind::FilledRectangle;
				result.style.opacity = shape.fill->opacity;
				result.style.fallbackRgb = shape.fill->color.fallbackRgb;
			}
			const float halfWidth = rectangle->width * 0.5f;
			const float halfHeight = rectangle->height * 0.5f;
			result.points = {
				{ rectangle->centerX - halfWidth, rectangle->centerY - halfHeight, width },
				{ rectangle->centerX + halfWidth, rectangle->centerY + halfHeight, width }
			};
			return result;
		}

		bool ImportCanvasContent(const UInkCanvas& canvas,
			Draw3UInkCanvasSnapshot& imported)
		{
			for (std::size_t contentIndex = 0;
				contentIndex < canvas.content.size(); ++contentIndex)
			{
				std::optional<Draw3UInkStrokeSnapshot> stroke;
				const UInkClear* clear = nullptr;
				if (const auto* ink = std::get_if<UInkInk>(&canvas.content[contentIndex]))
				{
					if (ink->contentId == contentIndex) stroke = ImportInk(*ink);
				}
				else if (const auto* shape =
					std::get_if<UInkShape>(&canvas.content[contentIndex]))
				{
					if (shape->contentId == contentIndex) stroke = ImportShape(*shape);
				}
				else if (const auto* sourceClear =
					std::get_if<UInkClear>(&canvas.content[contentIndex]))
				{
					if (sourceClear->contentId == contentIndex) clear = sourceClear;
				}
				if (!stroke && !clear) return false;
				const uint32_t undoId = stroke ? stroke->undoId : clear->undoId;
				if (undoId != contentIndex) return false;
				if (stroke)
				{
					imported.operations.emplace_back(*stroke);
					imported.strokes.push_back(std::move(*stroke));
				}
				else
				{
					Draw3UInkClearSnapshot importedClear;
					importedClear.undoId = clear->undoId;
					importedClear.extra = clear->extra;
					imported.operations.emplace_back(std::move(importedClear));
					imported.strokes.clear();
					++imported.intervalOrdinal;
				}
			}
			return true;
		}
	}

	UInkExtra MakeInkeysBindingExtra(Draw3UInkImportBindingMode mode)
	{
		UInkExtra extra;
		extra.emplace_back(TextValue("inkeysBindingMode"), TextValue(
			mode == Draw3UInkImportBindingMode::StableSlideId
				? "slide-id" : "page-index"));
		return extra;
	}

	UInkExtra MakeInkeysEndScreenExtra(Draw3UInkImportBindingMode mode)
	{
		UInkExtra extra = MakeInkeysBindingExtra(mode);
		extra.emplace_back(TextValue("inkeysPageKind"), TextValue("end-screen"));
		return extra;
	}

	bool HasInkeysBindingExtra(const std::optional<UInkExtra>& extra,
		Draw3UInkImportBindingMode mode) noexcept
	{
		if (!extra) return false;
		for (const auto& pair : *extra)
		{
			const auto* key = std::get_if<std::string>(&pair.first.value);
			const auto* value = std::get_if<std::string>(&pair.second.value);
			if (key && value && *key == "inkeysBindingMode" &&
				*value == (mode == Draw3UInkImportBindingMode::StableSlideId
					? "slide-id" : "page-index")) return true;
		}
		return false;
	}

	bool HasInkeysPageStateExtra(const std::optional<UInkExtra>& extra,
		bool retained) noexcept
	{
		if (!extra) return false;
		for (const auto& pair : *extra)
		{
			const auto* key = std::get_if<std::string>(&pair.first.value);
			const auto* value = std::get_if<std::string>(&pair.second.value);
			if (key && value && *key == "inkeysPageState")
				return *value == (retained ? "retained" : "active");
		}
		return false;
	}

	Draw3UInkImportResult ImportDraw3UInkDocument(
		const UInkDocument& document) noexcept
	{
		Draw3UInkImportResult result;
		try
		{
			if (!document.headerExtension || document.header.guid.IsZero() ||
				document.headerExtension->workspaces.size() != 1 ||
				document.header.pageNum == 0 || document.canvases.empty())
			{
				result.status = Draw3UInkImportStatus::InvalidDocument;
				result.error = "document_identity";
				return result;
			}
			const UInkWorkspace& workspace =
				document.headerExtension->workspaces.front();
			Draw3UInkExportSnapshot snapshot;
			snapshot.fileGuid = document.header.guid;
			snapshot.workspaceGuid = workspace.guid;
			snapshot.workspaceName = workspace.name;
			snapshot.workspaceType = workspace.workspaceType;
			snapshot.hostId = workspace.hostId;
			snapshot.currentPageIndex = workspace.currentPageIndex;
			snapshot.workspaceExtra = workspace.extra;
			snapshot.devices = document.headerExtension->devices;
			snapshot.assignedIndependentUndoGroups = true;
			snapshot.dpiScale = 1.0f;
			std::map<uint32_t, Draw3UInkCanvasSnapshot> ordered;
			for (const UInkCanvas& canvas : document.canvases)
			{
				if (!canvas.workspaceGuid || *canvas.workspaceGuid != workspace.guid ||
					canvas.pageGuid.IsZero() || canvas.layerIndex != 0 ||
					canvas.layerNumber != 0 || !canvas.viewport ||
					canvas.viewport->scale != 1.0f || canvas.temporaryWorkspace ||
					canvas.temporaryDevice || canvas.temporaryPage || canvas.temporaryLayer)
				{
					result.status = Draw3UInkImportStatus::InvalidDocument;
					result.error = "canvas_topology";
					return result;
				}
				Draw3UInkCanvasSnapshot imported;
				imported.deviceGuid = canvas.deviceGuid;
				imported.pageGuid = canvas.pageGuid;
				imported.pageIndex = canvas.pageIndex;
				imported.pageNumber = canvas.pageNumber;
				imported.slideId = canvas.slideId;
				imported.viewport = *canvas.viewport;
				imported.extra = canvas.extra;
				if (!ImportCanvasContent(canvas, imported) ||
					!ordered.emplace(canvas.pageIndex, std::move(imported)).second)
				{
					result.status = Draw3UInkImportStatus::UnsupportedContent;
					result.error = "content";
					return result;
				}
			}
			for (uint32_t pageIndex = 0; pageIndex < ordered.size(); ++pageIndex)
			{
				auto found = ordered.find(pageIndex);
				if (found == ordered.end())
				{
					result.status = Draw3UInkImportStatus::TopologyMismatch;
					result.error = "page_gap";
					return result;
				}
				snapshot.canvases.push_back(std::move(found->second));
			}
			result.status = Draw3UInkImportStatus::Success;
			result.snapshot = std::move(snapshot);
			return result;
		}
		catch (...)
		{
			result.status = Draw3UInkImportStatus::InvalidDocument;
			result.error = "exception";
			return result;
		}
	}

	Draw3UInkImportResult ImportApplicationOwnedPresentation(
		const UInkDocument& document,
		const Draw3UInkImportExpectation& expectation) noexcept
	{
		Draw3UInkImportResult result;
		try
		{
			if (expectation.fileGuid.IsZero() ||
				document.header.guid.Bytes() != expectation.fileGuid.Bytes() ||
				document.header.deviceNum != 1 || document.header.workspaceNum != 1 ||
				document.header.pageNum != document.canvases.size() ||
				!document.headerExtension || document.usesImplicitWorkspace ||
				!document.usesImplicitDevice ||
				document.headerExtension->workspaces.size() != 1 ||
				!document.headerExtension->devices.empty() ||
				document.headerExtension->name ||
				document.headerExtension->explanation ||
				document.headerExtension->extra)
			{
				result.status = Draw3UInkImportStatus::IdentityMismatch;
				result.error = "document_identity";
				return result;
			}
			const UInkWorkspace& workspace = document.headerExtension->workspaces.front();
			const bool stable = expectation.bindingMode ==
				Draw3UInkImportBindingMode::StableSlideId;
			if (workspace.guid.IsZero() || workspace.hostId != expectation.hostId ||
				workspace.parentWorkspaceGuid || !workspace.parentResolved || !workspace.usable ||
				workspace.workspaceType != (stable ? 2 : kInkeysPageIndexWorkspaceType) ||
				!HasInkeysBindingExtra(workspace.extra, expectation.bindingMode) ||
				(expectation.pageCount == 0 ||
					(expectation.allowEndScreen &&
						expectation.pageCount == std::numeric_limits<uint32_t>::max())) ||
				document.canvases.empty() ||
				(stable && expectation.slideIds.size() != expectation.pageCount) ||
				(!stable && !expectation.slideIds.empty()))
			{
				result.status = Draw3UInkImportStatus::TopologyMismatch;
				result.error = "workspace_topology";
				return result;
			}

			Draw3UInkExportSnapshot snapshot;
			snapshot.fileGuid = document.header.guid;
			snapshot.workspaceGuid = workspace.guid;
			snapshot.workspaceName = workspace.name;
			snapshot.workspaceType = workspace.workspaceType;
			snapshot.hostId = workspace.hostId;
			snapshot.currentPageIndex = workspace.currentPageIndex;
			snapshot.workspaceExtra = workspace.extra;
			snapshot.assignedIndependentUndoGroups = true;
			std::map<uint32_t, Draw3UInkCanvasSnapshot> ordered;
			std::map<int32_t, Draw3UInkCanvasSnapshot> bySlideId;
			std::optional<Draw3UInkCanvasSnapshot> endScreen;
			for (const UInkCanvas& canvas : document.canvases)
			{
				const UInkInkeysPageKind kind = InkeysPageKind(canvas.extra);
				const bool endPage = kind == UInkInkeysPageKind::EndScreen;
				if (!canvas.workspaceGuid ||
					canvas.workspaceGuid->Bytes() != workspace.guid.Bytes() ||
					canvas.deviceGuid || canvas.layerIndex != 0 || canvas.layerNumber != 0 ||
					canvas.pageGuid.IsZero() || canvas.temporaryWorkspace ||
					canvas.temporaryDevice || canvas.temporaryPage || canvas.temporaryLayer ||
					canvas.presentationUnbound ||
					canvas.pageIndex == std::numeric_limits<uint32_t>::max() ||
					canvas.pageNumber != canvas.pageIndex + 1 ||
					!canvas.viewport || canvas.viewport->scale != 1.0f ||
					!std::isfinite(canvas.viewport->x) || !std::isfinite(canvas.viewport->y) ||
					!HasInkeysBindingExtra(canvas.extra, expectation.bindingMode) ||
					kind == UInkInkeysPageKind::Invalid ||
					(endPage && (!expectation.allowEndScreen || canvas.slideId ||
						!HasInkeysPageStateExtra(canvas.extra, false))) ||
					(stable && !endPage && !canvas.slideId) ||
					(!stable && canvas.slideId))
				{
					result.status = Draw3UInkImportStatus::TopologyMismatch;
					result.error = "canvas_topology";
					return result;
				}
				Draw3UInkCanvasSnapshot imported;
				imported.pageGuid = canvas.pageGuid;
				imported.pageIndex = canvas.pageIndex;
				imported.pageNumber = canvas.pageNumber;
				imported.slideId = canvas.slideId;
				imported.viewport = *canvas.viewport;
				imported.extra = canvas.extra;
				imported.retained = stable && HasInkeysPageStateExtra(canvas.extra, true);
				if (!ImportCanvasContent(canvas, imported))
				{
					result.status = Draw3UInkImportStatus::UnsupportedContent;
					result.error = "content";
					return result;
				}
				if (!ordered.emplace(canvas.pageIndex, std::move(imported)).second)
				{
					result.status = Draw3UInkImportStatus::TopologyMismatch;
					result.error = "duplicate_page";
					return result;
				}
				if (endPage)
				{
					if (endScreen)
					{
						result.status = Draw3UInkImportStatus::TopologyMismatch;
						result.error = "duplicate_end_screen";
						return result;
					}
					endScreen = ordered.at(canvas.pageIndex);
				}
				else if (stable)
				{
					const auto& stored = ordered.at(canvas.pageIndex);
					if (!canvas.slideId || (!expectation.knownSlideIds.empty() &&
						std::find(expectation.knownSlideIds.begin(), expectation.knownSlideIds.end(),
							*canvas.slideId) == expectation.knownSlideIds.end()) ||
						!bySlideId.emplace(*canvas.slideId, stored).second)
					{
						result.status = Draw3UInkImportStatus::TopologyMismatch;
						result.error = "duplicate_slide_id";
						return result;
					}
				}
			}
			for (uint32_t pageIndex = 0; pageIndex < ordered.size(); ++pageIndex)
				if (ordered.find(pageIndex) == ordered.end())
				{
					result.status = Draw3UInkImportStatus::TopologyMismatch;
					result.error = "page_gap";
					return result;
				}
			const auto savedCurrent = ordered.find(workspace.currentPageIndex);
			if (savedCurrent == ordered.end() || savedCurrent->second.retained)
			{
				result.status = Draw3UInkImportStatus::TopologyMismatch;
				result.error = "current_page";
				return result;
			}
			if (endScreen)
			{
				const uint32_t storedEndIndex = endScreen->pageIndex;
				if (storedEndIndex == 0 ||
					(!stable && (storedEndIndex != expectation.pageCount ||
						ordered.size() != static_cast<std::size_t>(expectation.pageCount) + 1)))
				{
					result.status = Draw3UInkImportStatus::TopologyMismatch;
					result.error = "end_screen_position";
					return result;
				}
				// 保存时的 N 可不同于当前 PPT 的 N；结束页须紧随旧活动页且位于 retained 之前。
				for (const auto& [pageIndex, canvas] : ordered)
					if ((pageIndex < storedEndIndex && canvas.retained) ||
						(pageIndex > storedEndIndex && (!stable || !canvas.retained)))
					{
						result.status = Draw3UInkImportStatus::TopologyMismatch;
						result.error = "end_screen_position";
						return result;
					}
			}
			if (endScreen && workspace.currentPageIndex == endScreen->pageIndex)
				snapshot.currentPageIndex = expectation.pageCount;
			else if (stable && savedCurrent->second.slideId)
			{
				const auto found = std::find(expectation.slideIds.begin(),
					expectation.slideIds.end(), *savedCurrent->second.slideId);
				snapshot.currentPageIndex = found == expectation.slideIds.end() ? 0 :
					static_cast<uint32_t>(found - expectation.slideIds.begin());
			}
			else if (workspace.currentPageIndex >= expectation.pageCount)
			{
				result.status = Draw3UInkImportStatus::TopologyMismatch;
				result.error = "current_page";
				return result;
			}
			if (stable)
			{
				for (std::size_t pageIndex = 0; pageIndex < expectation.slideIds.size(); ++pageIndex)
				{
					auto found = bySlideId.find(expectation.slideIds[pageIndex]);
					// 当前 PPT 新增的 SlideID 没有历史 Canvas，由绘制线程创建空页。
					if (found == bySlideId.end()) continue;
					found->second.pageIndex = static_cast<uint32_t>(pageIndex);
					found->second.pageNumber = static_cast<uint32_t>(pageIndex + 1);
					found->second.retained = false;
					snapshot.activeCanvases.push_back(found->second);
				}
				if (endScreen)
				{
					endScreen->pageIndex = expectation.pageCount;
					endScreen->pageNumber = expectation.pageCount + 1;
					endScreen->retained = false;
					snapshot.activeCanvases.push_back(std::move(*endScreen));
				}
				for (auto& [id, canvas] : bySlideId)
					if (std::find(expectation.slideIds.begin(), expectation.slideIds.end(), id) == expectation.slideIds.end())
					{
						// 已保存索引证实归属的旧活动页，删除 SlideID 后可安全转为 retained。
						if (!canvas.retained &&
							(expectation.knownSlideIds.empty() ||
								std::find(expectation.knownSlideIds.begin(),
									expectation.knownSlideIds.end(), id) ==
									expectation.knownSlideIds.end()))
						{
							result.status = Draw3UInkImportStatus::TopologyMismatch;
							result.error = "unmarked_retained_slide";
							return result;
						}
						canvas.pageIndex = endScreen ?
							expectation.pageCount + 1 + static_cast<uint32_t>(snapshot.retainedCanvases.size()) :
							static_cast<uint32_t>(snapshot.activeCanvases.size() + snapshot.retainedCanvases.size());
						canvas.pageNumber = canvas.pageIndex + 1;
						canvas.retained = true;
						snapshot.retainedCanvases.push_back(std::move(canvas));
					}
				snapshot.canvases = snapshot.activeCanvases;
			}
			else
			{
				for (uint32_t pageIndex = 0; pageIndex < expectation.pageCount; ++pageIndex)
				{
					auto found = ordered.find(pageIndex);
					if (found == ordered.end() || InkeysPageKind(found->second.extra) !=
						UInkInkeysPageKind::Normal)
					{
						result.status = Draw3UInkImportStatus::TopologyMismatch;
						result.error = "page_gap";
						return result;
					}
					snapshot.activeCanvases.push_back(std::move(found->second));
				}
				if (endScreen)
				{
					endScreen->pageIndex = expectation.pageCount;
					endScreen->pageNumber = expectation.pageCount + 1;
					snapshot.activeCanvases.push_back(std::move(*endScreen));
				}
				else if (ordered.size() != expectation.pageCount)
				{
					result.status = Draw3UInkImportStatus::TopologyMismatch;
					result.error = "extra_page";
					return result;
				}
				snapshot.canvases = snapshot.activeCanvases;
			}
			result.status = Draw3UInkImportStatus::Success;
			result.snapshot = std::move(snapshot);
			return result;
		}
		catch (...)
		{
			result.status = Draw3UInkImportStatus::InvalidDocument;
			result.error = "exception";
			return result;
		}
	}
}
