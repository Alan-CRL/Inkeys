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
	std::vector<UInkShape> StandardShapeParts(const shapes::GeometryPart& part)
	{
		if (!part.style.stroke || part.style.linePattern == shapes::LinePattern::Solid || part.paths.empty() ||
			!part.paths.front().closed || !(part.paths.front().lengths.back() > 0)) return { StandardShapePart(part) };
		std::vector<UInkShape> result;
		if (part.style.fill)
		{
			auto filled = part; filled.style.stroke = false;
			result.push_back(StandardShapePart(filled));
		}
		const auto& path = part.paths.front(); const float perimeter = path.lengths.back();
		auto pointAt = [&](float distance)
		{
			const auto next = std::upper_bound(path.lengths.begin(),path.lengths.end(),distance);
			if (next == path.lengths.end()) return path.points.back();
			const size_t i = static_cast<size_t>(next-path.lengths.begin());
			if (!i) return path.points.front();
			const float f = (distance-path.lengths[i-1]) / (path.lengths[i]-path.lengths[i-1]);
			return shapes::Vec2{ path.points[i-1].x+f*(path.points[i].x-path.points[i-1].x),
				path.points[i-1].y+f*(path.points[i].y-path.points[i-1].y) };
		};
		// 标准格式用八个单调半段表达镜像相位，不把每条短虚线展开成对象。
		for (unsigned sector = 0; sector < 8; ++sector)
		{
			const float begin = perimeter * (sector / 8.f), end = perimeter * ((sector+1) / 8.f);
			auto output = StandardShapePart(part); output.fill.reset();
			UInkLineGeometry line;
			auto append = [&](shapes::Vec2 p) { line.points.push_back({p.x,p.y}); };
			append(pointAt(begin));
			for (size_t i=1; i<path.points.size(); ++i)
				if (path.lengths[i] > begin && path.lengths[i] < end) append(path.points[i]);
			append(pointAt(end));
			if (sector & 1) std::reverse(line.points.begin(),line.points.end());
			output.declaredShapeType = 1; output.geometry = std::move(line);
			output.stroke->dashOffset = part.style.dashOffset + shapes::ClosedDashCoordinate(0,perimeter,part.style);
			result.push_back(std::move(output));
		}
		return result;
	}
	bool AppendParametricShape(const Draw3UInkStrokeSnapshot& source, UInkCanvas& canvas,
		Draw3UInkCapabilityReport& report)
	{
		if (!source.shape || !shapes::Validate(*source.shape)) return false;
		auto normalized = *source.shape;
		if (normalized.construction && normalized.construction->tool.preset == shapes::PresetKind::Cone)
		{
			// 旧四部件圆锥可读；写出时元数据与标准部件必须来自同一个切线版本。
			auto rebuilt = shapes::MakeConstructedShape(*normalized.construction,normalized.style,normalized.id,normalized.revision);
			if (!rebuilt) return false;
			rebuilt->transform = normalized.transform; normalized = std::move(*rebuilt);
		}
		shapes::GeometryResult geometry;
		if (!shapes::BuildGeometry(normalized, geometry)) return false;
		// 与画布共用派生器，v4引用计数同时包含辅助部件；根仍只保存构造真值和aux。
		shapes::GeometryResult auxiliary;
		if (!shapes::BuildAuxiliaryGeometry(normalized,auxiliary)) return false;
		geometry.parts.insert(geometry.parts.end(),auxiliary.parts.begin(),auxiliary.parts.end());
		std::vector<UInkShape> standard;
		for (const auto& part : geometry.parts)
		{
			if (part.paths.empty()) return false;
			for (auto& output : StandardShapeParts(part)) standard.push_back(std::move(output));
		}
		const auto count = standard.size();
		const auto metadataVersion = kInkeysShapeVersion;
		if (!count || count > UINT32_MAX || canvas.content.size() > UINT32_MAX - count) return false;
		for (size_t index = 0; index < count; ++index)
		{
			auto output = std::move(standard[index]);
			output.contentId = static_cast<uint32_t>(canvas.content.size());
			output.undoId = source.undoId;
			output.renderOnlyWhenLatest = source.renderOnlyWhenLatest;
			// 只在首部件存根对象，后续部件用引用，避免复合对象产生平方级元数据。
			auto metadata = index == 0 ? EncodeInkeysShapeMetadata(normalized) : ShapeValue(ShapeMap{});
			if (index != 0)
			{
				auto& reference = std::get<ShapeMap>(metadata.value);
				ShapeField(reference, "version", ShapeValue(metadataVersion));
				ShapeField(reference, "id", ShapeValue(FormatUInkGuid(UInkGuid(normalized.id))));
				ShapeField(reference, "revision", ShapeValue(normalized.revision));
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
