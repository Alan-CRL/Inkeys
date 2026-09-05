module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
	}

	UInkExtra MakeInkeysBindingExtra(Draw3UInkImportBindingMode mode)
	{
		UInkExtra extra;
		extra.emplace_back(TextValue("inkeysBindingMode"), TextValue(
			mode == Draw3UInkImportBindingMode::StableSlideId
				? "slide-id" : "page-index"));
		return extra;
	}

	bool HasInkeysBindingExtra(const std::optional<UInkExtra>& extra,
		Draw3UInkImportBindingMode mode) noexcept
	{
		if (!extra || extra->size() != 1) return false;
		const auto* key = std::get_if<std::string>(&(*extra)[0].first.value);
		const auto* value = std::get_if<std::string>(&(*extra)[0].second.value);
		return key && value && *key == "inkeysBindingMode" &&
			*value == (mode == Draw3UInkImportBindingMode::StableSlideId
				? "slide-id" : "page-index");
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
				document.header.pageNum != expectation.pageCount ||
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
				workspace.currentPageIndex >= expectation.pageCount ||
				expectation.pageCount == 0 || document.canvases.size() != expectation.pageCount ||
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
			for (const UInkCanvas& canvas : document.canvases)
			{
				if (!canvas.workspaceGuid ||
					canvas.workspaceGuid->Bytes() != workspace.guid.Bytes() ||
					canvas.deviceGuid || canvas.layerIndex != 0 || canvas.layerNumber != 0 ||
					canvas.pageGuid.IsZero() || canvas.temporaryWorkspace ||
					canvas.temporaryDevice || canvas.temporaryPage || canvas.temporaryLayer ||
					canvas.presentationUnbound ||
					canvas.pageNumber != canvas.pageIndex + 1 ||
					!canvas.viewport || canvas.viewport->scale != 1.0f ||
					!std::isfinite(canvas.viewport->x) || !std::isfinite(canvas.viewport->y) ||
					!HasInkeysBindingExtra(canvas.extra, expectation.bindingMode) ||
					(stable && (!canvas.slideId || canvas.pageIndex >= expectation.slideIds.size() ||
						*canvas.slideId != expectation.slideIds[canvas.pageIndex])) ||
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
				for (std::size_t contentIndex = 0;
					contentIndex < canvas.content.size(); ++contentIndex)
				{
					std::optional<Draw3UInkStrokeSnapshot> stroke;
					if (const auto* ink = std::get_if<UInkInk>(&canvas.content[contentIndex]))
					{
						if (ink->contentId != contentIndex) stroke = std::nullopt;
						else stroke = ImportInk(*ink);
					}
					else if (const auto* shape = std::get_if<UInkShape>(&canvas.content[contentIndex]))
					{
						if (shape->contentId != contentIndex) stroke = std::nullopt;
						else stroke = ImportShape(*shape);
					}
					if (!stroke)
					{
						result.status = Draw3UInkImportStatus::UnsupportedContent;
						result.error = "content";
						return result;
					}
					if (stroke->undoId != contentIndex)
					{
						result.status = Draw3UInkImportStatus::InvalidDocument;
						result.error = "undo_order";
						return result;
					}
					imported.strokes.push_back(std::move(*stroke));
				}
				if (!ordered.emplace(canvas.pageIndex, std::move(imported)).second)
				{
					result.status = Draw3UInkImportStatus::TopologyMismatch;
					result.error = "duplicate_page";
					return result;
				}
			}
			for (uint32_t pageIndex = 0; pageIndex < expectation.pageCount; ++pageIndex)
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
}
