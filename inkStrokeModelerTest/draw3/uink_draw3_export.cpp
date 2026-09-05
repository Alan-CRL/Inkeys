module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

module draw3.uink_draw3_export;

import draw3.uink_codec;

namespace draw3::uink
{
	namespace
	{
		void AddExportDiagnostic(std::vector<UInkDiagnostic>& diagnostics,
			std::string path)
		{
			diagnostics.push_back({ UInkDiagnosticCode::InvalidFieldValue,
				UInkDiagnosticSeverity::Error, 0, 0, std::move(path), 0 });
		}

		std::vector<Draw3UInkCanvasSnapshot> CanonicalCanvases(
			const Draw3UInkExportSnapshot& snapshot)
		{
			if (snapshot.activeCanvases.empty() && snapshot.retainedCanvases.empty())
				return snapshot.canvases;
			std::vector<Draw3UInkCanvasSnapshot> result;
			result.reserve(snapshot.activeCanvases.size() + snapshot.retainedCanvases.size());
		for (auto canvas : snapshot.activeCanvases)
		{
			canvas.pageIndex = static_cast<uint32_t>(result.size());
			canvas.pageNumber = canvas.pageIndex + 1;
			canvas.retained = false;
			result.push_back(std::move(canvas));
		}
		for (auto canvas : snapshot.retainedCanvases)
		{
			canvas.pageIndex = static_cast<uint32_t>(result.size());
			canvas.pageNumber = canvas.pageIndex + 1;
			canvas.retained = true;
			result.push_back(std::move(canvas));
		}
			return result;
		}

		std::optional<UInkExtra> WithPageStateMarker(
			const std::optional<UInkExtra>& source, bool retained)
		{
			UInkExtra value = source.value_or(UInkExtra{});
			value.erase(std::remove_if(value.begin(), value.end(), [](const auto& pair)
				{
					const auto* key = std::get_if<std::string>(&pair.first.value);
					return key && *key == "inkeysPageState";
				}), value.end());
			UInkMessagePackValue key; key.value = std::string("inkeysPageState");
			UInkMessagePackValue state; state.value = std::string(retained ? "retained" : "active");
			value.emplace_back(std::move(key), std::move(state));
			return value;
		}

		bool IsFinitePositive(float value) noexcept
		{
			return std::isfinite(value) && value > 0.0f;
		}

		bool IsShapeKind(Draw3UInkStrokeKind kind) noexcept
		{
			return kind == Draw3UInkStrokeKind::SolidLine ||
				kind == Draw3UInkStrokeKind::DashedLine ||
				kind == Draw3UInkStrokeKind::OutlineRectangle ||
				kind == Draw3UInkStrokeKind::FilledRectangle;
		}

		bool IsKnownStrokeKind(Draw3UInkStrokeKind kind) noexcept
		{
			return kind == Draw3UInkStrokeKind::Pen ||
				kind == Draw3UInkStrokeKind::Highlighter ||
				kind == Draw3UInkStrokeKind::Eraser || IsShapeKind(kind);
		}

		bool ValidateSnapshotStroke(const Draw3UInkStrokeSnapshot& stroke) noexcept
		{
			if (!IsKnownStrokeKind(stroke.style.kind) || stroke.style.texture != 0 ||
				stroke.points.empty() || !std::isfinite(stroke.style.opacity) ||
				stroke.style.opacity < 0.0f || stroke.style.opacity > 1.0f ||
				stroke.style.fallbackRgb > 0xffffff) return false;
			for (const Draw3UInkPoint& point : stroke.points)
			{
				if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
					!IsFinitePositive(point.width)) return false;
			}
			if (IsShapeKind(stroke.style.kind))
			{
				if (stroke.points.size() != 2 || stroke.points[0].width != stroke.points[1].width)
					return false;
				if ((stroke.style.kind == Draw3UInkStrokeKind::OutlineRectangle ||
					stroke.style.kind == Draw3UInkStrokeKind::FilledRectangle) &&
					(stroke.points[0].x == stroke.points[1].x ||
						stroke.points[0].y == stroke.points[1].y)) return false;
			}
			return true;
		}

		UInkColor ConvertColor(uint32_t fallbackRgb) noexcept
		{
			UInkColor color;
			color.fallbackRgb = fallbackRgb;
			return color;
		}

		UInkShapeStroke ConvertStrokeStyle(const Draw3UInkStrokeSnapshot& source)
		{
			UInkShapeStroke stroke;
			stroke.color = ConvertColor(source.style.fallbackRgb);
			stroke.opacity = source.style.opacity;
			stroke.width = source.points[0].width;
			if (source.style.kind == Draw3UInkStrokeKind::DashedLine)
			{
				// 与 draw3 HLSL 的中心段和中心间隔保持一致。
				stroke.dashArray = { stroke.width * 4.0f, stroke.width * 6.0f };
			}
			return stroke;
		}

		UInkContent ConvertStroke(const Draw3UInkStrokeSnapshot& source,
			uint32_t contentId, float dpiScale, Draw3UInkCapabilityReport& report)
		{
			if (!IsShapeKind(source.style.kind))
			{
				UInkInk ink;
				ink.contentId = contentId;
				ink.undoId = source.undoId;
				switch (source.style.kind)
				{
				case Draw3UInkStrokeKind::Eraser:
					ink.declaredInkType = 0;
					ink.effectiveKind = UInkInkKind::Erase;
					break;
				case Draw3UInkStrokeKind::Highlighter:
					ink.declaredInkType = 2;
					ink.effectiveKind = UInkInkKind::Highlighter;
					++report.approximatedHighlighterNibCount;
					break;
				default:
					ink.declaredInkType = 1;
					ink.effectiveKind = UInkInkKind::Pen;
					break;
				}
				ink.color = ConvertColor(source.style.fallbackRgb);
				ink.opacity = source.style.opacity;
				ink.declaredTexture = 0;
				ink.effectiveTexture = 0;
				ink.renderOnlyWhenLatest = source.renderOnlyWhenLatest;
				ink.points.reserve(source.points.size());
				for (const Draw3UInkPoint& point : source.points)
					ink.points.push_back({ point.x, point.y, point.width, std::nullopt });
				++report.inkCount;
				return ink;
			}

			UInkShape shape;
			shape.contentId = contentId;
			shape.undoId = source.undoId;
			shape.renderOnlyWhenLatest = source.renderOnlyWhenLatest;
			if (source.style.kind == Draw3UInkStrokeKind::SolidLine ||
				source.style.kind == Draw3UInkStrokeKind::DashedLine)
			{
				shape.declaredShapeType = 0;
				UInkLineGeometry line;
				line.points = { { source.points[0].x, source.points[0].y },
					{ source.points[1].x, source.points[1].y } };
				shape.geometry = std::move(line);
				shape.stroke = ConvertStrokeStyle(source);
			}
			else
			{
				shape.declaredShapeType = 2;
				const float left = std::min(source.points[0].x, source.points[1].x);
				const float right = std::max(source.points[0].x, source.points[1].x);
				const float top = std::min(source.points[0].y, source.points[1].y);
				const float bottom = std::max(source.points[0].y, source.points[1].y);
				UInkRectangleGeometry rectangle;
				rectangle.centerX = (left + right) * 0.5f;
				rectangle.centerY = (top + bottom) * 0.5f;
				rectangle.width = right - left;
				rectangle.height = bottom - top;
				const float radius = std::min(4.0f * dpiScale,
					std::min(rectangle.width, rectangle.height) * 0.5f);
				rectangle.cornerRadiusX = radius;
				rectangle.cornerRadiusY = radius;
				shape.geometry = rectangle;
				if (source.style.kind == Draw3UInkStrokeKind::OutlineRectangle)
					shape.stroke = ConvertStrokeStyle(source);
				else
				{
					UInkShapeFill fill;
					fill.color = ConvertColor(source.style.fallbackRgb);
					fill.opacity = source.style.opacity;
					shape.fill = fill;
				}
			}
			++report.shapeCount;
			return shape;
		}
	}

	Draw3UInkExportResult ExportDraw3SnapshotToUInk(
		const Draw3UInkExportSnapshot& snapshot)
	{
		Draw3UInkExportResult result;
		try
		{
			if (snapshot.fileGuid.IsZero() || snapshot.workspaceGuid.IsZero() ||
				!std::isfinite(snapshot.dpiScale) || snapshot.dpiScale <= 0.0f)
			{
				result.status = Draw3UInkExportStatus::InvalidIdentity;
				return result;
			}

			UInkDocument document;
			document.header.guid = snapshot.fileGuid;
			document.usesImplicitWorkspace = false;
			document.usesImplicitDevice = snapshot.devices.empty();
			UInkHeaderExtension extension;
			extension.devices = snapshot.devices;
			UInkWorkspace workspace;
			workspace.guid = snapshot.workspaceGuid;
			workspace.workspaceType = snapshot.workspaceType;
			workspace.name = snapshot.workspaceName;
			workspace.hostId = snapshot.hostId;
			workspace.currentPageIndex = snapshot.currentPageIndex;
			workspace.extra = snapshot.workspaceExtra;
			extension.workspaces.push_back(std::move(workspace));
			document.headerExtension = std::move(extension);
			document.header.deviceNum = document.usesImplicitDevice ? 1u :
				static_cast<uint32_t>(snapshot.devices.size());
			document.header.workspaceNum = 1;

			std::set<std::array<uint8_t, 16>> pages;
			std::map<std::array<uint8_t, 16>, uint32_t> pageIndicesByGuid;
			std::map<uint32_t, std::array<uint8_t, 16>> pageGuidsByIndex;
			std::set<std::tuple<std::optional<std::array<uint8_t, 16>>,
				std::array<uint8_t, 16>>> canvasKeys;
			const auto canvases = CanonicalCanvases(snapshot);
			for (std::size_t canonicalIndex = 0; canonicalIndex < canvases.size(); ++canonicalIndex)
			{
				Draw3UInkCanvasSnapshot sourceCanvas = canvases[canonicalIndex];
				if (sourceCanvas.pageGuid.IsZero() || !std::isfinite(sourceCanvas.viewport.x) ||
					!std::isfinite(sourceCanvas.viewport.y) ||
					!IsFinitePositive(sourceCanvas.viewport.scale) ||
					sourceCanvas.viewport.scale != 1.0f ||
					(document.usesImplicitDevice && sourceCanvas.deviceGuid) ||
					(!document.usesImplicitDevice && !sourceCanvas.deviceGuid))
				{
					result.status = Draw3UInkExportStatus::InvalidSnapshot;
					return result;
				}
				if (!document.usesImplicitDevice)
				{
					const bool known = std::any_of(snapshot.devices.begin(), snapshot.devices.end(),
						[&](const UInkDevice& device)
						{
							return device.guid == *sourceCanvas.deviceGuid;
						});
					if (!known)
					{
						result.status = Draw3UInkExportStatus::MissingDeviceMapping;
						return result;
					}
				}
				const auto pageByGuid = pageIndicesByGuid.emplace(
					sourceCanvas.pageGuid.Bytes(), sourceCanvas.pageIndex);
				const auto pageByIndex = pageGuidsByIndex.emplace(
					sourceCanvas.pageIndex, sourceCanvas.pageGuid.Bytes());
				if ((!pageByGuid.second && pageByGuid.first->second != sourceCanvas.pageIndex) ||
					(!pageByIndex.second && pageByIndex.first->second != sourceCanvas.pageGuid.Bytes()))
				{
					result.status = Draw3UInkExportStatus::InvalidSnapshot;
					return result;
				}
				const auto key = std::make_tuple(sourceCanvas.deviceGuid ?
					std::optional<std::array<uint8_t, 16>>(sourceCanvas.deviceGuid->Bytes()) :
					std::nullopt, sourceCanvas.pageGuid.Bytes());
				if (!canvasKeys.insert(key).second)
				{
					result.status = Draw3UInkExportStatus::InvalidSnapshot;
					return result;
				}

				UInkCanvas canvas;
				canvas.workspaceGuid = snapshot.workspaceGuid;
				canvas.deviceGuid = sourceCanvas.deviceGuid;
				canvas.pageGuid = sourceCanvas.pageGuid;
				canvas.pageIndex = sourceCanvas.pageIndex;
				canvas.pageNumber = sourceCanvas.pageNumber;
				canvas.layerIndex = 0;
				canvas.layerNumber = 0;
				canvas.slideId = sourceCanvas.slideId;
				canvas.viewport = sourceCanvas.viewport;
				canvas.extra = WithPageStateMarker(sourceCanvas.extra, sourceCanvas.retained);
				uint32_t previousUndo = 0;
				for (size_t index = 0; index < sourceCanvas.strokes.size(); ++index)
				{
					const Draw3UInkStrokeSnapshot& stroke = sourceCanvas.strokes[index];
					if (!ValidateSnapshotStroke(stroke) ||
						(index == 0 && stroke.undoId != 0) ||
						(index != 0 && stroke.undoId < previousUndo))
					{
						result.status = Draw3UInkExportStatus::InvalidSourceStroke;
						AddExportDiagnostic(result.diagnostics, "snapshot.stroke");
						return result;
					}
					canvas.content.push_back(ConvertStroke(stroke,
						static_cast<uint32_t>(index), snapshot.dpiScale, result.capabilities));
					previousUndo = stroke.undoId;
				}
				document.canvases.push_back(std::move(canvas));
				pages.insert(sourceCanvas.pageGuid.Bytes());
			}
			uint32_t expectedPageIndex = 0;
			for (const auto& [pageIndex, pageGuid] : pageGuidsByIndex)
			{
				(void)pageGuid;
				if (pageIndex != expectedPageIndex++)
				{
					result.status = Draw3UInkExportStatus::InvalidSnapshot;
					return result;
				}
			}

			document.header.pageNum = static_cast<uint32_t>(pages.size());
			// 复用唯一的 UInk writer 验证器，避免导出成功后才发现注册树或字段不合法。
			const UInkEncodeResult validated = EncodeUInkDocument(document);
			if (validated.status != UInkEncodeStatus::Success)
			{
				result.status = Draw3UInkExportStatus::InvalidSnapshot;
				result.diagnostics = validated.diagnostics;
				return result;
			}
			result.capabilities.assignedIndependentUndoGroups =
				snapshot.assignedIndependentUndoGroups;
			result.status = Draw3UInkExportStatus::Success;
			result.document = std::move(document);
			return result;
		}
		catch (...)
		{
			result.status = Draw3UInkExportStatus::InvalidSnapshot;
			return result;
		}
	}
}
