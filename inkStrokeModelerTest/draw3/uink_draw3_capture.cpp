module;

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

module draw3.uink_draw3_capture;

namespace draw3::uink
{
	namespace
	{
		UInkGuid ConvertGuid(const InkGuid& guid) noexcept
		{
			return UInkGuid(guid.Bytes());
		}

		void AddCaptureDiagnostic(std::vector<UInkDiagnostic>& diagnostics,
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

		std::optional<Draw3UInkStrokeKind> ConvertKind(StoredInkType type) noexcept
		{
			switch (type)
			{
			case StoredInkType::Pen: return Draw3UInkStrokeKind::Pen;
			case StoredInkType::Highlighter: return Draw3UInkStrokeKind::Highlighter;
			case StoredInkType::Eraser: return Draw3UInkStrokeKind::Eraser;
			case StoredInkType::SolidLine: return Draw3UInkStrokeKind::SolidLine;
			case StoredInkType::DashedLine: return Draw3UInkStrokeKind::DashedLine;
			case StoredInkType::OutlineRectangle:
				return Draw3UInkStrokeKind::OutlineRectangle;
			case StoredInkType::FilledRectangle:
				return Draw3UInkStrokeKind::FilledRectangle;
			default: return std::nullopt;
			}
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
				AddCaptureDiagnostic(result.diagnostics, "snapshot.identity");
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
					AddCaptureDiagnostic(result.diagnostics, "snapshot.devices");
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
				if (page.PageGuid().IsZero() || page.Canvases().empty())
				{
					result.status = page.PageGuid().IsZero()
						? Draw3UInkExportStatus::InvalidIdentity
						: Draw3UInkExportStatus::InvalidSnapshot;
					AddCaptureDiagnostic(result.diagnostics, "snapshot.page.canvas");
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
							AddCaptureDiagnostic(result.diagnostics, "snapshot.canvas.device");
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
					for (size_t strokeIndex = 0;
						strokeIndex < sourceCanvas.Strokes().size(); ++strokeIndex)
					{
						const InkStroke& stroke = sourceCanvas.Strokes()[strokeIndex];
						const std::optional<Draw3UInkStrokeKind> kind =
							ConvertKind(stroke.Style().inkType);
						if (!kind)
						{
							result.status = Draw3UInkExportStatus::InvalidSourceStroke;
							return result;
						}
						Draw3UInkStrokeSnapshot copy;
						copy.style = { *kind, stroke.Style().opacity,
							stroke.Style().fallbackRgb, stroke.Style().texture };
						copy.points.reserve(stroke.Points().size());
						for (const StoredInkPoint& point : stroke.Points())
							copy.points.push_back({ point.x, point.y, point.width });
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
}
