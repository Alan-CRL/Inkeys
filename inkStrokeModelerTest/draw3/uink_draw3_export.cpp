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
		UInkGuid ConvertGuid(const InkGuid& guid) noexcept
		{
			return UInkGuid(guid.Bytes());
		}

		void AddExportDiagnostic(std::vector<UInkDiagnostic>& diagnostics,
			std::string path)
		{
			diagnostics.push_back({ UInkDiagnosticCode::InvalidFieldValue,
				UInkDiagnosticSeverity::Error, 0, 0, std::move(path), 0 });
		}

		const Draw3UInkDeviceMapping* FindDeviceMapping(
			const std::vector<Draw3UInkDeviceMapping>& mappings, DeviceKey key) noexcept
		{
			for (const Draw3UInkDeviceMapping& mapping : mappings)
				if (mapping.sourceDevice == key) return &mapping;
			return nullptr;
		}

		bool IsFinitePositive(float value) noexcept
		{
			return std::isfinite(value) && value > 0.0f;
		}

		bool IsKnownStoredInkType(StoredInkType type) noexcept
		{
			return type == StoredInkType::Pen || type == StoredInkType::Highlighter ||
				type == StoredInkType::Eraser || IsStoredShapeType(type);
		}

		bool ValidateSnapshotStroke(const Draw3UInkStrokeSnapshot& stroke) noexcept
		{
			if (!IsKnownStoredInkType(stroke.style.inkType) || stroke.style.texture != 0 ||
				stroke.points.empty() || !std::isfinite(stroke.style.opacity) ||
				stroke.style.opacity < 0.0f || stroke.style.opacity > 1.0f ||
				stroke.style.fallbackRgb > 0xffffff) return false;
			for (const StoredInkPoint& point : stroke.points)
			{
				if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
					!IsFinitePositive(point.width)) return false;
			}
			if (IsStoredShapeType(stroke.style.inkType))
			{
				if (stroke.points.size() != 2 || stroke.points[0].width != stroke.points[1].width)
					return false;
				if ((stroke.style.inkType == StoredInkType::OutlineRectangle ||
					stroke.style.inkType == StoredInkType::FilledRectangle) &&
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
			if (source.style.inkType == StoredInkType::DashedLine)
			{
				// 与 draw3 HLSL 的中心段和中心间隔保持一致。
				stroke.dashArray = { stroke.width * 4.0f, stroke.width * 6.0f };
			}
			return stroke;
		}

		UInkContent ConvertStroke(const Draw3UInkStrokeSnapshot& source,
			uint32_t contentId, float dpiScale, Draw3UInkCapabilityReport& report)
		{
			if (!IsStoredShapeType(source.style.inkType))
			{
				UInkInk ink;
				ink.contentId = contentId;
				ink.undoId = source.undoId;
				switch (source.style.inkType)
				{
				case StoredInkType::Eraser:
					ink.declaredInkType = 0;
					ink.effectiveKind = UInkInkKind::Erase;
					break;
				case StoredInkType::Highlighter:
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
				for (const StoredInkPoint& point : source.points)
					ink.points.push_back({ point.x, point.y, point.width, std::nullopt });
				++report.inkCount;
				return ink;
			}

			UInkShape shape;
			shape.contentId = contentId;
			shape.undoId = source.undoId;
			shape.renderOnlyWhenLatest = source.renderOnlyWhenLatest;
			if (source.style.inkType == StoredInkType::SolidLine ||
				source.style.inkType == StoredInkType::DashedLine)
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
				if (source.style.inkType == StoredInkType::OutlineRectangle)
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

	Draw3UInkCaptureResult CaptureDraw3UInkExportSnapshot(
		const InkCanvasCollection& collection,
		const Draw3UInkCaptureOptions& options)
	{
		Draw3UInkCaptureResult result;
		try
		{
			if (options.fileGuid.IsZero() || collection.WorkspaceGuid().IsZero() ||
				!std::isfinite(options.dpiScale) || options.dpiScale <= 0.0f)
			{
				result.status = Draw3UInkExportStatus::InvalidIdentity;
				AddExportDiagnostic(result.diagnostics, "snapshot.identity");
				return result;
			}
			std::set<uint64_t> sourceDevices;
			std::set<std::array<uint8_t, 16>> targetDevices;
			for (const Draw3UInkDeviceMapping& mapping : options.devices)
			{
				if (mapping.targetDevice.guid.IsZero() ||
					!sourceDevices.insert(mapping.sourceDevice.Value()).second ||
					!targetDevices.insert(mapping.targetDevice.guid.Bytes()).second)
				{
					result.status = Draw3UInkExportStatus::InvalidIdentity;
					AddExportDiagnostic(result.diagnostics, "snapshot.devices");
					return result;
				}
			}

			Draw3UInkExportSnapshot snapshot;
			snapshot.fileGuid = options.fileGuid;
			snapshot.workspaceGuid = ConvertGuid(collection.WorkspaceGuid());
			snapshot.workspaceName = options.workspaceName;
			snapshot.dpiScale = options.dpiScale;
			snapshot.assignedIndependentUndoGroups = true;
			for (const Draw3UInkDeviceMapping& mapping : options.devices)
				snapshot.devices.push_back(mapping.targetDevice);

			for (size_t pageIndex = 0; pageIndex < collection.Pages().size(); ++pageIndex)
			{
				const InkPage& page = collection.Pages()[pageIndex];
				if (page.PageGuid().IsZero())
				{
					result.status = Draw3UInkExportStatus::InvalidIdentity;
					AddExportDiagnostic(result.diagnostics, "snapshot.page.canvas");
					return result;
				}
				if (page.Canvases().empty())
				{
					// 不伪造 Device/viewport，也不静默遗漏 draw3 中仍存在的空页面。
					result.status = Draw3UInkExportStatus::InvalidSnapshot;
					AddExportDiagnostic(result.diagnostics, "snapshot.page.canvas");
					return result;
				}
				for (const InkCanvas& sourceCanvas : page.Canvases())
				{
					Draw3UInkCanvasSnapshot canvas;
					if (!options.devices.empty())
					{
						const Draw3UInkDeviceMapping* mapping =
							FindDeviceMapping(options.devices, sourceCanvas.Device());
						if (!mapping)
						{
							result.status = Draw3UInkExportStatus::MissingDeviceMapping;
							AddExportDiagnostic(result.diagnostics, "snapshot.canvas.device");
							return result;
						}
						canvas.deviceGuid = mapping->targetDevice.guid;
					}
					else if (sourceCanvas.Device() != kDefaultDeviceKey)
					{
						result.status = Draw3UInkExportStatus::MissingDeviceMapping;
						return result;
					}
					canvas.pageGuid = ConvertGuid(page.PageGuid());
					canvas.pageIndex = static_cast<uint32_t>(pageIndex);
					canvas.pageNumber = static_cast<uint32_t>(pageIndex + 1);
					canvas.viewport = { sourceCanvas.Viewport().x,
						sourceCanvas.Viewport().y, sourceCanvas.Viewport().scale };
					canvas.strokes.reserve(sourceCanvas.Strokes().size());
					for (size_t strokeIndex = 0; strokeIndex < sourceCanvas.Strokes().size(); ++strokeIndex)
					{
						const InkStroke& stroke = sourceCanvas.Strokes()[strokeIndex];
						Draw3UInkStrokeSnapshot copy;
						copy.style = stroke.Style();
						copy.points.assign(stroke.Points().begin(), stroke.Points().end());
						copy.undoId = static_cast<uint32_t>(strokeIndex);
						canvas.strokes.push_back(std::move(copy));
					}
					snapshot.canvases.push_back(std::move(canvas));
				}
			}
			result.status = Draw3UInkExportStatus::Success;
			result.snapshot = std::move(snapshot);
			return result;
		}
		catch (...)
		{
			result.status = Draw3UInkExportStatus::InvalidSnapshot;
			return result;
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
			workspace.workspaceType = 0;
			workspace.name = snapshot.workspaceName;
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
			for (const Draw3UInkCanvasSnapshot& sourceCanvas : snapshot.canvases)
			{
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
				canvas.viewport = sourceCanvas.viewport;
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
