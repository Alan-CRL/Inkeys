namespace
{
	std::vector<float> ShapeDashArray(const shapes::ShapeStyle& style)
	{
		switch (style.linePattern)
		{
		case shapes::LinePattern::Dashed: return { 4 * style.width, 6 * style.width };
		case shapes::LinePattern::Dotted: return { 0, 3 * style.width };
		case shapes::LinePattern::DashDot: return { 4 * style.width, 5 * style.width, 0, 5 * style.width };
		default: return {};
		}
	}
	UInkShape StandardShapePart(const shapes::GeometryPart& part)
	{
		UInkShape result;
		if (part.style.stroke)
		{
			UInkShapeStroke stroke;
			stroke.color.fallbackRgb = part.style.strokeRgb;
			stroke.opacity = part.style.strokeOpacity;
			stroke.width = part.style.width;
			stroke.dashArray = ShapeDashArray(part.style);
			stroke.dashOffset = part.style.dashOffset;
			result.stroke = std::move(stroke);
		}
		if (part.style.fill)
		{
			UInkShapeFill fill;
			fill.color.fallbackRgb = part.style.fillRgb;
			fill.opacity = part.style.fillOpacity;
			result.fill = fill;
		}
		const auto& t = part.transform;
		const float sx = std::hypot(t.m11, t.m12), sy = std::hypot(t.m21, t.m22);
		const bool orthogonal = std::abs(t.m11 * t.m21 + t.m12 * t.m22) <= 0.00001f * sx * sy;
		// UInk rotation 使用弧度，精确的局部变换另保存在扩展中。
		const float rotation = std::atan2(t.m12, t.m11);
		if (part.kind == shapes::Kind::Rectangle && orthogonal)
		{
			const auto& g = part.geometry;
			const auto center = shapes::ApplyTransform(t, { (g.start.x + g.end.x) * 0.5f, (g.start.y + g.end.y) * 0.5f });
			UInkRectangleGeometry rectangle;
			rectangle.centerX = center.x; rectangle.centerY = center.y;
			rectangle.width = std::abs(g.end.x - g.start.x) * sx;
			rectangle.height = std::abs(g.end.y - g.start.y) * sy;
			rectangle.rotation = rotation;
			rectangle.cornerRadiusX = std::min(g.radius, std::min(std::abs(g.end.x - g.start.x), std::abs(g.end.y - g.start.y)) * 0.5f) * sx;
			rectangle.cornerRadiusY = rectangle.cornerRadiusX / sx * sy;
			result.declaredShapeType = 2; result.geometry = rectangle;
			return result;
		}
		if ((part.kind == shapes::Kind::Circle || part.kind == shapes::Kind::Ellipse) && orthogonal)
		{
			const auto center = shapes::ApplyTransform(t, part.geometry.start);
			const float rx = part.kind == shapes::Kind::Circle ? part.geometry.radius : part.geometry.end.x;
			const float ry = part.kind == shapes::Kind::Circle ? part.geometry.radius : part.geometry.end.y;
			if (part.kind == shapes::Kind::Circle && std::abs(sx - sy) <= 0.00001f * std::max(sx, sy))
			{
				result.declaredShapeType = 5;
				result.geometry = UInkCircleGeometry{ center.x, center.y, rx * sx };
			}
			else
			{
				result.declaredShapeType = 4;
				result.geometry = UInkEllipseGeometry{ center.x, center.y, 2 * rx * sx, 2 * ry * sy, rotation };
			}
			return result;
		}
		UInkLineGeometry path;
		const auto& outline = part.paths.front();
		for (auto point : outline.points) path.points.push_back({ point.x, point.y });
		if (outline.closed && path.points.size() > 1) path.points.pop_back();
		result.declaredShapeType = outline.closed ? 6 : part.kind == shapes::Kind::Line ? 0 : 1;
		result.geometry = std::move(path);
		return result;
	}
	bool AppendParametricShape(const Draw3UInkStrokeSnapshot& source, UInkCanvas& canvas,
		Draw3UInkCapabilityReport& report)
	{
		shapes::GeometryResult geometry;
		if (!source.shape || !shapes::BuildGeometry(*source.shape, geometry)) return false;
		const auto count = geometry.parts.size();
		if (!count || count > UINT32_MAX || canvas.content.size() > UINT32_MAX - count) return false;
		for (size_t index = 0; index < count; ++index)
		{
			if (geometry.parts[index].paths.empty()) return false;
			auto output = StandardShapePart(geometry.parts[index]);
			output.contentId = static_cast<uint32_t>(canvas.content.size());
			output.undoId = source.undoId;
			output.renderOnlyWhenLatest = source.renderOnlyWhenLatest;
			// 只在首部件存根对象，后续部件用引用，避免复合对象产生平方级元数据。
			auto metadata = index == 0 ? EncodeInkeysShapeMetadata(*source.shape) : ShapeValue(ShapeMap{});
			if (index != 0)
			{
				auto& reference = std::get<ShapeMap>(metadata.value);
				ShapeField(reference, "version", ShapeValue(uint64_t{ source.shape->construction ? 2u : 1u }));
				ShapeField(reference, "id", ShapeValue(FormatUInkGuid(UInkGuid(source.shape->id))));
				ShapeField(reference, "revision", ShapeValue(source.shape->revision));
				ShapeField(reference, "reference", ShapeValue(true));
			}
			auto& map = std::get<ShapeMap>(metadata.value);
			ShapeField(map, "partIndex", ShapeValue(uint64_t(index)));
			ShapeField(map, "partCount", ShapeValue(uint64_t(count)));
			output.extra = ShapeMap{ { ShapeValue(std::string("inkeysShape")), std::move(metadata) } };
			canvas.content.emplace_back(std::move(output));
		}
		++report.shapeCount;
		return true;
	}
}
