module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>
#include <windows.h>

module Inkeys.Drawing.Draw3.canvas_debug;

namespace Inkeys::Drawing::Draw3::CanvasDebug
{
	namespace
	{
		struct Rgb { std::uint8_t r, g, b; };

		constexpr int kFirstGlyph = 32;
		constexpr int kGlyphCount = 95; // ASCII 32..126
		constexpr std::size_t kMaxColumns = 16;
		constexpr int kPerfChars = 32;      // 性能区/曲线宽度（字符数）。
		constexpr int kLabelChars = 16;     // 标记标签最多字符数。
		constexpr int kLabelLines = 2;
		constexpr float kUnknown = -1.0f;
		constexpr float kPalmMaximumAspectRatio = 3.5f; // 与手掌橡皮识别一致。
		constexpr float kMaximumDrawnAreaPx = 4096.0f;

		constexpr Rgb kBackground = { 16, 18, 24 };
		constexpr std::uint32_t kBackgroundAlpha = 218; // 约 0.85，便于透出桌面内容。
		constexpr Rgb kText = { 238, 241, 247 };
		constexpr Rgb kDim = { 146, 156, 172 };
		constexpr Rgb kAccent = { 110, 198, 255 };
		constexpr Rgb kChartLine = { 72, 190, 255 };
		constexpr Rgb kGrid = { 255, 255, 255 };
		constexpr Rgb kBlack = { 0, 0, 0 };

		bool Finite(float value) noexcept { return std::isfinite(value); }

		bool Known(float value) noexcept { return Finite(value) && value >= 0.0f; }

		int RoundToInt(float value, int low, int high) noexcept
		{
			if (!Finite(value)) return 0;
			return static_cast<int>(std::clamp(std::lround(value), static_cast<long>(low), static_cast<long>(high)));
		}

		void UnionInto(RECT& target, const RECT& addition) noexcept
		{
			if (addition.left >= addition.right || addition.top >= addition.bottom) return;
			if (target.left >= target.right || target.top >= target.bottom) { target = addition; return; }
			target.left = std::min(target.left, addition.left);
			target.top = std::min(target.top, addition.top);
			target.right = std::max(target.right, addition.right);
			target.bottom = std::max(target.bottom, addition.bottom);
		}

		RECT ClampRect(RECT rect, int width, int height) noexcept
		{
			rect.left = std::clamp(static_cast<int>(rect.left), 0, std::max(width, 0));
			rect.right = std::clamp(static_cast<int>(rect.right), 0, std::max(width, 0));
			rect.top = std::clamp(static_cast<int>(rect.top), 0, std::max(height, 0));
			rect.bottom = std::clamp(static_cast<int>(rect.bottom), 0, std::max(height, 0));
			if (rect.left >= rect.right || rect.top >= rect.bottom) return {};
			return rect;
		}

		bool SameRect(const RECT& a, const RECT& b) noexcept
		{
			return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
		}

		// ---- 位图原语（预乘 BGRA，0xAARRGGBB） ----
		struct Surface
		{
			std::uint32_t* pixels = nullptr;
			int stride = 0;
			int width = 0;
			int height = 0;
		};

		std::uint32_t Premultiplied(Rgb color, std::uint32_t alpha) noexcept
		{
			alpha = std::min<std::uint32_t>(alpha, 255);
			const auto channel = [&](std::uint8_t value) { return (value * alpha + 127) / 255; };
			return (alpha << 24) | (channel(color.r) << 16) | (channel(color.g) << 8) | channel(color.b);
		}

		void BlendPixel(const Surface& surface, int x, int y, Rgb color, std::uint32_t alpha) noexcept
		{
			if (x < 0 || y < 0 || x >= surface.width || y >= surface.height) return;
			std::uint32_t& pixel = surface.pixels[static_cast<std::size_t>(y) * surface.stride + x];
			pixel = BlendPremultipliedOver(pixel, color.r, color.g, color.b, alpha);
		}

		void BlendRect(const Surface& surface, int left, int top, int right, int bottom,
			Rgb color, std::uint32_t alpha) noexcept
		{
			left = std::max(left, 0);
			top = std::max(top, 0);
			right = std::min(right, surface.width);
			bottom = std::min(bottom, surface.height);
			for (int y = top; y < bottom; ++y)
				for (int x = left; x < right; ++x) BlendPixel(surface, x, y, color, alpha);
		}

		void DrawGlyphs(const Surface& surface, const std::vector<std::uint8_t>& coverage,
			int cellWidth, int cellHeight, int x, int y, const char* text,
			Rgb color, std::uint32_t alpha = 255) noexcept
		{
			if (coverage.empty() || !text) return;
			for (const char* cursor = text; *cursor; ++cursor, x += cellWidth)
			{
				const unsigned char code = static_cast<unsigned char>(*cursor);
				if (code < kFirstGlyph || code >= kFirstGlyph + kGlyphCount) continue;
				const std::uint8_t* glyph = coverage.data() +
					static_cast<std::size_t>(code - kFirstGlyph) * cellWidth * cellHeight;
				for (int gy = 0; gy < cellHeight; ++gy)
				{
					const int py = y + gy;
					if (py < 0 || py >= surface.height) continue;
					for (int gx = 0; gx < cellWidth; ++gx)
					{
						const std::uint32_t cov = glyph[gy * cellWidth + gx];
						if (!cov) continue;
						BlendPixel(surface, x + gx, py, color, (cov * alpha + 127) / 255);
					}
				}
			}
		}

		// 四个圆角的覆盖率（板子悬浮在画布中，不贴边）。
		std::uint32_t CornerCoverage(int x, int y, int width, int height, int radius) noexcept
		{
			if (radius <= 0) return 255;
			float centerX = 0.0f, centerY = 0.0f;
			if (x < radius) centerX = static_cast<float>(radius);
			else if (x >= width - radius) centerX = static_cast<float>(width - radius);
			else return 255;
			if (y < radius) centerY = static_cast<float>(radius);
			else if (y >= height - radius) centerY = static_cast<float>(height - radius);
			else return 255;
			const float dx = x + 0.5f - centerX;
			const float dy = y + 0.5f - centerY;
			const float distance = std::sqrt(dx * dx + dy * dy) - static_cast<float>(radius);
			return static_cast<std::uint32_t>(std::clamp(0.5f - distance, 0.0f, 1.0f) * 255.0f + 0.5f);
		}

		// ---- 接触表列注册表：新增“速度”等列只需在 ContactSample 加字段并追加一项 ----
		struct Column
		{
			const char* header;
			int width;
			bool rightAlign;
			void (*format)(const ContactSample&, char* out, std::size_t capacity);
		};

		void FormatType(const ContactSample& s, char* out, std::size_t n)
		{
			const char* name = "Touch";
			switch (s.kind)
			{
			case ContactKind::Touch: name = "Touch"; break;
			case ContactKind::Pen: name = "Pen"; break;
			case ContactKind::PenEraser: name = "Eraser"; break;
			case ContactKind::MouseLeft: name = "MouseL"; break;
			case ContactKind::MouseRight: name = "MouseR"; break;
			}
			std::snprintf(out, n, "%s", name);
		}

		void FormatX(const ContactSample& s, char* out, std::size_t n)
		{
			std::snprintf(out, n, "%d", RoundToInt(s.x, -9999, 99999));
		}

		void FormatY(const ContactSample& s, char* out, std::size_t n)
		{
			std::snprintf(out, n, "%d", RoundToInt(s.y, -9999, 99999));
		}

		void FormatPressure(const ContactSample& s, char* out, std::size_t n)
		{
			if (Known(s.pressure)) std::snprintf(out, n, "%.2f", std::min(s.pressure, 9.99f));
			else std::snprintf(out, n, "--");
		}

		bool ValidArea(const ContactSample& s) noexcept
		{
			return Finite(s.areaWidthPx) && Finite(s.areaHeightPx) &&
				s.areaWidthPx > 0.0f && s.areaHeightPx > 0.0f;
		}

		void FormatAreaPx(const ContactSample& s, char* out, std::size_t n)
		{
			if (ValidArea(s)) std::snprintf(out, n, "%d*%d",
				RoundToInt(s.areaWidthPx, 0, 9999), RoundToInt(s.areaHeightPx, 0, 9999));
			else std::snprintf(out, n, "--");
		}

		void FormatAreaCm(const ContactSample& s, char* out, std::size_t n)
		{
			if (ValidArea(s) && s.cmPerPixelX > 0.0f && s.cmPerPixelY > 0.0f)
				std::snprintf(out, n, "%.1f*%.1f",
					std::min(s.areaWidthPx * s.cmPerPixelX, 999.9f),
					std::min(s.areaHeightPx * s.cmPerPixelY, 999.9f));
			else std::snprintf(out, n, "--");
		}

		constexpr float kRadiansToDegrees = 57.2957795f;

		// 高度角：笔与屏幕平面的夹角，90° 为垂直于屏幕；等于 90° - 倾斜。
		void FormatAltitude(const ContactSample& s, char* out, std::size_t n)
		{
			if (Known(s.tilt)) std::snprintf(out, n, "%d", RoundToInt(90.0f - s.tilt * kRadiansToDegrees, 0, 90));
			else std::snprintf(out, n, "--");
		}

		// 方位角：笔身在屏幕平面内的指向（0..359°）；内部方向 orientation = 360° - 方位角。
		void FormatAzimuth(const ContactSample& s, char* out, std::size_t n)
		{
			if (!Known(s.orientation)) { std::snprintf(out, n, "--"); return; }
			int degrees = RoundToInt(360.0f - s.orientation * kRadiansToDegrees, 0, 720);
			degrees %= 360;
			std::snprintf(out, n, "%d", degrees);
		}

		void FormatPath(const ContactSample& s, char* out, std::size_t n)
		{
			const char* name = "--";
			switch (s.path)
			{
			case InputPath::RtsExtended: name = "RTS"; break;
			case InputPath::RtsBasicXY: name = "RTS-XY"; break;
			case InputPath::Win32Mouse: name = "Win32"; break;
			default: break;
			}
			std::snprintf(out, n, "%s", name);
		}

		void FormatSource(const ContactSample& s, char* out, std::size_t n)
		{
			const char* name = "--";
			switch (s.source)
			{
			case SourceClass::Mouse: name = "Mouse"; break;
			case SourceClass::ExternalPen: name = "ExtPen"; break;
			case SourceClass::IntegratedPen: name = "IntPen"; break;
			case SourceClass::Touch: name = "Touch"; break;
			case SourceClass::TouchPad: name = "Pad"; break;
			default: break;
			}
			std::snprintf(out, n, "%s", name);
		}

		void FormatId(const ContactSample& s, char* out, std::size_t n)
		{
			// cid:x —— x 为断触续接前一个接触的 cid，不是续接为 0。
			if (s.hasRtsIds) std::snprintf(out, n, "%u:%u", s.contactId, s.previousContactId);
			else std::snprintf(out, n, "--");
		}

		void FormatStage(const ContactSample& s, char* out, std::size_t n)
		{
			const char* name = "--";
			switch (s.stage)
			{
			case ContactStage::Down: name = "Down"; break;
			case ContactStage::Move: name = "Move"; break;
			case ContactStage::Up: name = "Up"; break;
			case ContactStage::Cancelled: name = "Cancel"; break;
			default: break;
			}
			std::snprintf(out, n, "%s", name);
		}

		void FormatPublishCount(const ContactSample& s, char* out, std::size_t n)
		{
			if (s.stage == ContactStage::Unknown) std::snprintf(out, n, "--");
			else std::snprintf(out, n, "%llu", static_cast<unsigned long long>(std::min<std::uint64_t>(s.publishCount, 9999999ull)));
		}

		void FormatLatency(const ContactSample& s, char* out, std::size_t n)
		{
			if (Known(s.latencyMs)) std::snprintf(out, n, "%.1f", std::min(s.latencyMs, 999.9f));
			else std::snprintf(out, n, "--");
		}

		void FormatSpeedPx(const ContactSample& s, char* out, std::size_t n)
		{
			if (Known(s.speedPxPerSec)) std::snprintf(out, n, "%d", RoundToInt(s.speedPxPerSec, 0, 99999));
			else std::snprintf(out, n, "--");
		}

		void FormatSpeedCm(const ContactSample& s, char* out, std::size_t n)
		{
			if (Known(s.speedCmPerSec)) std::snprintf(out, n, "%.1f", std::min(s.speedCmPerSec, 999.9f));
			else std::snprintf(out, n, "--");
		}

		constexpr Column kColumns[] = {
			{ "Type", 6, false, FormatType },
			{ "Src", 6, false, FormatSource },
			{ "Path", 6, false, FormatPath },
			{ "Id", 13, false, FormatId },
			{ "Phase", 6, false, FormatStage },
			{ "Seq", 7, true, FormatPublishCount },
			{ "X", 5, true, FormatX },
			{ "Y", 5, true, FormatY },
			{ "Prs", 5, true, FormatPressure },
			{ "Alt", 3, true, FormatAltitude },
			{ "Azi", 3, true, FormatAzimuth },
			{ "Area px", 9, true, FormatAreaPx },
			{ "Area cm", 9, true, FormatAreaCm },
			{ "Spd px/s", 8, true, FormatSpeedPx },
			{ "Spd cm/s", 8, true, FormatSpeedCm },
			{ "Lat ms", 6, true, FormatLatency },
		};
		constexpr std::size_t kColumnCount = std::size(kColumns);
		static_assert(kColumnCount <= kMaxColumns);

		constexpr int TableChars() noexcept
		{
			int total = 0;
			for (const Column& column : kColumns) total += column.width + 1;
			return total - 1;
		}

		// ---- 布局 ----
		struct Metrics
		{
			int cellWidth = 7;
			int cellHeight = 14;
			int padding = 8;
			int gap = 3;
			int lineHeight = 17;
			int chartHeight = 56;
			int cornerRadius = 6;
		};

		Metrics MakeMetrics(int cellWidth, int cellHeight, float dpi) noexcept
		{
			Metrics m;
			m.cellWidth = cellWidth;
			m.cellHeight = cellHeight;
			m.padding = std::max(6, static_cast<int>(std::lround(8.0f * dpi)));
			m.gap = std::max(2, static_cast<int>(std::lround(3.0f * dpi)));
			m.lineHeight = cellHeight + m.gap;
			m.chartHeight = cellHeight * 4;
			m.cornerRadius = std::max(4, static_cast<int>(std::lround(6.0f * dpi)));
			return m;
		}

		int LayoutHeight(const Metrics& m, int rows) noexcept
		{
			int y = m.padding;
			y += m.lineHeight + m.gap;            // 标题
			y += 3 * m.lineHeight + m.gap;        // 性能三行（含输入通路）
			y += m.chartHeight + m.gap;           // 曲线
			if (rows > 0)
			{
				y += m.gap;
				y += m.lineHeight + m.gap;        // 表头 + 分隔线
				y += rows * m.lineHeight;
			}
			return y + m.padding;
		}

		int LayoutWidth(const Metrics& m, bool hasRows) noexcept
		{
			return 2 * m.padding + (hasRows ? TableChars() : kPerfChars) * m.cellWidth;
		}

		// ---- 标签文本：不使用 snprintf，避免热路径格式化 ----
		char* AppendText(char* cursor, const char* text) noexcept
		{
			while (*text) *cursor++ = *text++;
			return cursor;
		}

		char* AppendInt(char* cursor, int value) noexcept
		{
			if (value < 0) { *cursor++ = '-'; value = -value; }
			char digits[12];
			int count = 0;
			do { digits[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value > 0 && count < 11);
			while (count > 0) *cursor++ = digits[--count];
			return cursor;
		}

		char* AppendTenths(char* cursor, int tenths) noexcept
		{
			cursor = AppendInt(cursor, tenths / 10);
			*cursor++ = '.';
			*cursor++ = static_cast<char>('0' + tenths % 10);
			return cursor;
		}
	}

	bool ShouldShowSolidAreaBox(float areaWidthPx, float areaHeightPx,
		float palmThresholdAreaPx) noexcept
	{
		if (!Finite(areaWidthPx) || !Finite(areaHeightPx) || !Finite(palmThresholdAreaPx)) return false;
		if (areaWidthPx <= 0.0f || areaHeightPx <= 0.0f || palmThresholdAreaPx <= 0.0f) return false;
		const float larger = std::max(areaWidthPx, areaHeightPx);
		const float smaller = std::min(areaWidthPx, areaHeightPx);
		if (larger / smaller > kPalmMaximumAspectRatio) return false;
		// 与 PalmEraser::State::Observe 一致：面积达到阈值即激活。
		return static_cast<double>(areaWidthPx) * static_cast<double>(areaHeightPx) >=
			static_cast<double>(palmThresholdAreaPx);
	}

	std::uint32_t BlendPremultipliedOver(std::uint32_t destination,
		std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint32_t alpha) noexcept
	{
		alpha = std::min<std::uint32_t>(alpha, 255);
		if (alpha == 0) return destination;
		const std::uint32_t inverse = 255 - alpha;
		const std::uint32_t da = destination >> 24;
		const std::uint32_t dr = (destination >> 16) & 0xFF;
		const std::uint32_t dg = (destination >> 8) & 0xFF;
		const std::uint32_t db = destination & 0xFF;
		const std::uint32_t a = std::min<std::uint32_t>(255, alpha + (da * inverse + 127) / 255);
		const auto channel = [&](std::uint32_t source, std::uint32_t below)
		{
			return std::min<std::uint32_t>(a, (source * alpha + 127) / 255 + (below * inverse + 127) / 255);
		};
		return (a << 24) | (channel(red, dr) << 16) | (channel(green, dg) << 8) | channel(blue, db);
	}

	Overlay::Overlay()
	{
		SYSTEM_INFO info = {};
		GetSystemInfo(&info);
		processorCount_ = std::max<int>(1, static_cast<int>(info.dwNumberOfProcessors));
		LARGE_INTEGER frequency = {};
		if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
			qpcFrequency_ = static_cast<double>(frequency.QuadPart);
	}

	// 用 GDI 一次性把 ASCII 字形渲染成灰度覆盖率；绘制阶段只做内存拷贝，不再调用 GDI。
	void Overlay::BuildAtlas(float dpiScale)
	{
		dpiScale_ = dpiScale;
		atlas_ = {};
		atlas_.dpi = dpiScale;
		const int fontPixels = std::max(10, static_cast<int>(std::lround(12.0f * dpiScale)));
		atlas_.cellWidth = std::max(5, static_cast<int>(std::lround(fontPixels * 0.55f)));
		atlas_.cellHeight = fontPixels + 2;

		HDC dc = CreateCompatibleDC(nullptr);
		if (dc)
		{
			LOGFONTW logFont = {};
			logFont.lfHeight = -fontPixels;
			logFont.lfWeight = FW_NORMAL;
			logFont.lfCharSet = DEFAULT_CHARSET;
			logFont.lfQuality = ANTIALIASED_QUALITY; // 灰度抗锯齿，便于预乘 alpha 合成。
			logFont.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
			wcscpy_s(logFont.lfFaceName, L"Consolas");
			HFONT font = CreateFontIndirectW(&logFont);
			HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
			TEXTMETRICW metrics = {};
			if (font && GetTextMetricsW(dc, &metrics) && metrics.tmAveCharWidth > 0 && metrics.tmHeight > 0)
			{
				atlas_.cellWidth = metrics.tmAveCharWidth;
				atlas_.cellHeight = metrics.tmHeight;
			}

			BITMAPINFO info = {};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = atlas_.cellWidth * kGlyphCount;
			info.bmiHeader.biHeight = -atlas_.cellHeight;
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;
			void* bits = nullptr;
			HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
			if (bitmap && bits && font)
			{
				HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
				std::memset(bits, 0, static_cast<std::size_t>(atlas_.cellWidth) * kGlyphCount * atlas_.cellHeight * 4);
				SetBkMode(dc, TRANSPARENT);
				SetTextColor(dc, RGB(255, 255, 255));
				SetTextAlign(dc, TA_LEFT | TA_TOP);
				for (int glyph = 0; glyph < kGlyphCount; ++glyph)
				{
					const wchar_t character = static_cast<wchar_t>(kFirstGlyph + glyph);
					TextOutW(dc, glyph * atlas_.cellWidth, 0, &character, 1);
				}
				GdiFlush();
				const auto* source = static_cast<const std::uint32_t*>(bits);
				const int sourceStride = atlas_.cellWidth * kGlyphCount;
				atlas_.coverage.assign(static_cast<std::size_t>(kGlyphCount) * atlas_.cellWidth * atlas_.cellHeight, 0);
				for (int glyph = 0; glyph < kGlyphCount; ++glyph)
					for (int y = 0; y < atlas_.cellHeight; ++y)
						for (int x = 0; x < atlas_.cellWidth; ++x)
						{
							const std::uint32_t pixel = source[y * sourceStride + glyph * atlas_.cellWidth + x];
							atlas_.coverage[(static_cast<std::size_t>(glyph) * atlas_.cellHeight + y) * atlas_.cellWidth + x] =
								static_cast<std::uint8_t>((pixel >> 8) & 0xFF); // 灰度：取 G 通道。
						}
				SelectObject(dc, oldBitmap);
			}
			if (bitmap) DeleteObject(bitmap);
			if (oldFont) SelectObject(dc, oldFont);
			if (font) DeleteObject(font);
			DeleteDC(dc);
		}

		const Metrics m = MakeMetrics(atlas_.cellWidth, atlas_.cellHeight, dpiScale);
		capacityWidth_ = LayoutWidth(m, true);
		capacityHeight_ = LayoutHeight(m, static_cast<int>(kMaxContacts));
		plate_.assign(static_cast<std::size_t>(capacityWidth_) * capacityHeight_, 0u);
		plateWidth_ = plateHeight_ = 0;
		plateRect_ = committedPlateRect_ = {};

		const int labelPadding = std::max(2, static_cast<int>(std::lround(3.0f * dpiScale)));
		labelTileWidth_ = kLabelChars * atlas_.cellWidth + 2 * labelPadding;
		labelTileHeight_ = kLabelLines * atlas_.cellHeight + 2 * labelPadding;
		for (Slot& slot : slots_)
		{
			slot.label.assign(static_cast<std::size_t>(labelTileWidth_) * labelTileHeight_, 0u);
			slot.labelValid = false;
			slot.labelUpload = false;
			std::fill(std::begin(slot.labelKey), std::end(slot.labelKey), 0);
		}
		markers_ = {};
		committedMarkers_ = {};
		markerBounds_ = committedMarkerBounds_ = {};
		gpuStale_ = true;
		plateUpload_ = true;
	}

	void Overlay::BeginContacts() noexcept
	{
		for (Slot& slot : slots_) slot.seen = false;
	}

	void Overlay::ObserveContact(const ContactSample& sample) noexcept
	{
		Slot* target = nullptr;
		Slot* free = nullptr;
		Slot* oldestTerminal = nullptr;
		for (Slot& slot : slots_)
		{
			if (slot.used)
			{
				if (slot.sample.key == sample.key) { target = &slot; break; }
				if (slot.terminal && (!oldestTerminal || slot.terminalMs < oldestTerminal->terminalMs))
					oldestTerminal = &slot;
			}
			else if (!free) free = &slot;
		}
		if (!target)
		{
			target = free ? free : oldestTerminal;
			if (!target) return; // 全部槽都被活动接触占用：忽略超出容量的接触。
			target->used = true;
			target->terminal = false;
			target->terminalMs = 0.0;
			target->order = ++orderCounter_;
			target->labelValid = false;
			target->rasterized = {};
			target->speedQpc = 0;
			target->speedPx = target->speedCm = kUnknown;
			target->speedAdvanced = false;
			target->speedAdvanceMs = 0.0;
			target->latencyQpc = 0;
			target->latencyMs = kUnknown;
			rowsStructureDirty_ = true;
		}
		if (target->terminal) { target->seen = true; return; } // 已结束的行不再接受更新。
		// 速度：相邻输入快照的坐标差 / QPC 差；至少相隔 4 ms 才结算，避免高回报率下的量化噪声，
		// 再做指数平滑。Move 快照是 latest-wins，所以这里测到的是被绘制线程读到的采样之间的平均速度。
		if (sample.qpc > 0)
		{
			if (target->speedQpc == 0)
			{
				target->speedQpc = sample.qpc;
				target->speedAnchorX = sample.x;
				target->speedAnchorY = sample.y;
			}
			else if (sample.qpc > target->speedQpc)
			{
				const double seconds = static_cast<double>(sample.qpc - target->speedQpc) / qpcFrequency_;
				if (seconds >= 0.004)
				{
					const double dx = static_cast<double>(sample.x) - target->speedAnchorX;
					const double dy = static_cast<double>(sample.y) - target->speedAnchorY;
					const float px = static_cast<float>(std::sqrt(dx * dx + dy * dy) / seconds);
					target->speedPx = target->speedPx < 0.0f ? px : target->speedPx * 0.6f + px * 0.4f;
					if (sample.cmPerPixelX > 0.0f && sample.cmPerPixelY > 0.0f)
					{
						const double cx = dx * sample.cmPerPixelX;
						const double cy = dy * sample.cmPerPixelY;
						const float cm = static_cast<float>(std::sqrt(cx * cx + cy * cy) / seconds);
						target->speedCm = target->speedCm < 0.0f ? cm : target->speedCm * 0.6f + cm * 0.4f;
					}
					else target->speedCm = kUnknown;
					target->speedQpc = sample.qpc;
					target->speedAnchorX = sample.x;
					target->speedAnchorY = sample.y;
					target->speedAdvanced = true;
				}
			}
		}
		target->sample = sample;
		target->sample.speedPxPerSec = target->speedPx;
		target->sample.speedCmPerSec = target->speedCm;
		target->sample.latencyMs = target->latencyMs;
		target->seen = true;
	}

	bool Overlay::ContactSpeed(std::uint64_t key, float& pxPerSec, float& cmPerSec) const noexcept
	{
		for (const Slot& slot : slots_)
			if (slot.used && slot.sample.key == key)
			{
				pxPerSec = slot.speedPx;
				cmPerSec = slot.speedCm;
				return true;
			}
		return false;
	}

	void Overlay::OnPresented(std::int64_t nowQpc) noexcept
	{
		for (Slot& slot : slots_)
		{
			if (!slot.used || slot.terminal || slot.sample.qpc <= 0 || slot.sample.qpc == slot.latencyQpc) continue;
			slot.latencyQpc = slot.sample.qpc; // 同一个输入快照只结算一次。
			if (nowQpc < slot.sample.qpc) continue;
			const float milliseconds = static_cast<float>(
				static_cast<double>(nowQpc - slot.sample.qpc) * 1000.0 / qpcFrequency_);
			if (!Finite(milliseconds) || milliseconds > 10000.0f) continue;
			slot.latencyMs = slot.latencyMs < 0.0f ? milliseconds : slot.latencyMs * 0.6f + milliseconds * 0.4f;
			slot.sample.latencyMs = slot.latencyMs;
		}
	}

	bool Overlay::ContactLatency(std::uint64_t key, float& milliseconds) const noexcept
	{
		for (const Slot& slot : slots_)
			if (slot.used && slot.sample.key == key)
			{
				milliseconds = slot.latencyMs;
				return true;
			}
		return false;
	}

	void Overlay::OnFrame(double workMs, bool counted) noexcept
	{
		if (!counted || !std::isfinite(workMs) || workMs < 0.0) return;
		++frameCount_;
		workSumMs_ += workMs;
	}

	bool Overlay::ConsumePlateUpload() noexcept
	{
		const bool pending = plateUpload_;
		plateUpload_ = false;
		return pending;
	}

	const std::uint32_t* Overlay::LabelPixels(std::size_t slot) const noexcept
	{
		return slot < slots_.size() ? slots_[slot].label.data() : nullptr;
	}

	bool Overlay::ConsumeLabelUpload(std::size_t slot, int& width, int& height) noexcept
	{
		if (slot >= slots_.size()) return false;
		Slot& entry = slots_[slot];
		if (!entry.labelUpload) return false;
		entry.labelUpload = false;
		width = entry.labelWidth;
		height = entry.labelHeight;
		return width > 0 && height > 0;
	}

	void Overlay::MarkGpuStale() noexcept
	{
		gpuStale_ = true;
		plateUpload_ = true;
		for (Slot& slot : slots_)
			if (slot.labelValid) slot.labelUpload = true;
	}

	RECT Overlay::VisibleBounds() const noexcept
	{
		RECT bounds = {};
		if (plateWidth_ > 0) UnionInto(bounds, plateRect_);
		UnionInto(bounds, committedMarkerBounds_);
		return bounds;
	}

	double Overlay::MillisecondsUntilNextUpdate(double nowMs) const noexcept
	{
		double best = -1.0;
		const auto consider = [&](double ms)
		{
			ms = std::max(ms, 1.0);
			if (best < 0.0 || ms < best) best = ms;
		};
		consider(perfStarted_ ? windowStartMs_ + kSnapshotIntervalMs - nowMs : 1.0);
		if (rowValuesDirty_) consider(lastRowRasterMs_ + kContactValueRefreshMs - nowMs);
		for (const Slot& slot : slots_)
			if (slot.used && slot.terminal) consider(slot.terminalMs + kContactLingerMs - nowMs);
		return best;
	}

	void Overlay::SettleSnapshot(double nowMs, double targetFps)
	{
		const auto sampleCpu = [&]()
		{
			FILETIME creation = {}, exit = {}, kernel = {}, user = {};
			if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return;
			const auto to64 = [](const FILETIME& t)
			{ return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
			const std::uint64_t total = to64(kernel) + to64(user);
			if (cpuBaselineValid_ && nowMs > previousCpuWallMs_)
			{
				const double wall100ns = (nowMs - previousCpuWallMs_) * 10000.0;
				const double used = static_cast<double>(total - previousCpu100ns_);
				cpuPercent_ = std::clamp(used / (wall100ns * processorCount_) * 100.0, 0.0, 100.0);
			}
			previousCpu100ns_ = total;
			previousCpuWallMs_ = nowMs;
			cpuBaselineValid_ = true;
		};

		if (!perfStarted_)
		{
			perfStarted_ = true;
			windowStartMs_ = nowMs;
			sampleCpu();
			perfDirty_ = true;
			return;
		}
		const double elapsedMs = nowMs - windowStartMs_;
		if (elapsedMs < kSnapshotIntervalMs) return;

		const double seconds = elapsedMs / 1000.0;
		fps_ = static_cast<double>(frameCount_) / seconds;
		unlimitedFps_ = (frameCount_ > 0 && workSumMs_ > 0.0)
			? static_cast<double>(frameCount_) / (workSumMs_ / 1000.0) : 0.0;
		const double interval = targetFps > 0.0 ? 1000.0 / targetFps : 1000.0 / 60.0;
		loadPercent_ = frameCount_ > 0
			? (workSumMs_ / static_cast<double>(frameCount_)) / interval * 100.0 : 0.0;
		sampleCpu();

		// 长时间被阻塞（没有调用）时按 0.5s 补足样本，保持曲线横轴等于真实时间。
		const std::size_t repeats = static_cast<std::size_t>(std::clamp(
			std::floor(elapsedMs / kSnapshotIntervalMs), 1.0, static_cast<double>(kChartSamples)));
		for (std::size_t i = 0; i < repeats; ++i)
		{
			if (chartCount_ < kChartSamples) chart_[chartCount_++] = static_cast<float>(fps_);
			else
			{
				std::memmove(chart_.data(), chart_.data() + 1, (kChartSamples - 1) * sizeof(float));
				chart_[kChartSamples - 1] = static_cast<float>(fps_);
			}
		}
		windowStartMs_ = nowMs;
		frameCount_ = 0;
		workSumMs_ = 0.0;
		// 只有显示精度（0.1）内的数值或曲线真的变化时才需要重绘板子。
		const std::array<long, 4> shown = {
			std::lround(std::min(fps_, 99999.9) * 10.0), std::lround(std::min(unlimitedFps_, 99999.9) * 10.0),
			std::lround(std::min(loadPercent_, 999.9) * 10.0), std::lround(std::min(cpuPercent_, 999.9) * 10.0) };
		if (shown != shownPerf_ || chartCount_ != shownChartCount_ || chart_ != shownChart_)
		{
			shownPerf_ = shown;
			shownChart_ = chart_;
			shownChartCount_ = chartCount_;
			perfDirty_ = true;
		}
	}

	void Overlay::RasterizePlate()
	{
		std::array<const Slot*, kMaxContacts> rows = {};
		int rowCount = 0;
		for (const Slot& slot : slots_)
			if (slot.used) rows[rowCount++] = &slot;
		std::sort(rows.begin(), rows.begin() + rowCount,
			[](const Slot* a, const Slot* b) { return a->order < b->order; });

		const Metrics m = MakeMetrics(atlas_.cellWidth, atlas_.cellHeight, dpiScale_);
		const bool hasRows = rowCount > 0;
		plateWidth_ = std::min(LayoutWidth(m, hasRows), capacityWidth_);
		plateHeight_ = std::min(LayoutHeight(m, rowCount), capacityHeight_);
		const Surface surface{ plate_.data(), capacityWidth_, plateWidth_, plateHeight_ };

		for (int y = 0; y < plateHeight_; ++y)
			for (int x = 0; x < plateWidth_; ++x)
			{
				const std::uint32_t alpha = kBackgroundAlpha *
					CornerCoverage(x, y, plateWidth_, plateHeight_, m.cornerRadius) / 255;
				surface.pixels[static_cast<std::size_t>(y) * surface.stride + x] = Premultiplied(kBackground, alpha);
			}

		const auto& coverage = atlas_.coverage;
		const int cw = atlas_.cellWidth;
		const int ch = atlas_.cellHeight;
		const int left = m.padding;
		const int contentWidth = plateWidth_ - 2 * m.padding;
		int y = m.padding;

		DrawGlyphs(surface, coverage, cw, ch, left, y, "CANVAS DEBUG", kAccent);
		y += m.lineHeight + m.gap;

		char text[64];
		// 两行性能数据：固定列起点 0/4/14/24，数值右对齐，位数变化不错位。
		std::snprintf(text, sizeof(text), "%7.1f", std::min(fps_, 99999.9));
		DrawGlyphs(surface, coverage, cw, ch, left, y, "FPS", kDim);
		DrawGlyphs(surface, coverage, cw, ch, left + 4 * cw, y, text, kText);
		DrawGlyphs(surface, coverage, cw, ch, left + 14 * cw, y, "Unlimited", kDim);
		std::snprintf(text, sizeof(text), "%8.1f", std::min(unlimitedFps_, 99999.9));
		DrawGlyphs(surface, coverage, cw, ch, left + 24 * cw, y, text, kText);
		y += m.lineHeight;
		DrawGlyphs(surface, coverage, cw, ch, left, y, "Load", kDim);
		std::snprintf(text, sizeof(text), "%6.1f%%", std::min(loadPercent_, 999.9));
		DrawGlyphs(surface, coverage, cw, ch, left + 4 * cw, y, text, kText);
		DrawGlyphs(surface, coverage, cw, ch, left + 14 * cw, y, "Proc CPU", kDim);
		std::snprintf(text, sizeof(text), "%7.1f%%", std::min(cpuPercent_, 999.9));
		DrawGlyphs(surface, coverage, cw, ch, left + 24 * cw, y, text, kText);
		y += m.lineHeight;
		{
			const char* path = "--";
			switch (inputPath_)
			{
			case InputPath::RtsExtended: path = "RTS extended"; break;
			case InputPath::RtsBasicXY: path = "RTS basic X/Y"; break;
			case InputPath::Win32Mouse: path = "Win32 mouse"; break;
			default: break;
			}
			DrawGlyphs(surface, coverage, cw, ch, left, y, "Input", kDim);
			DrawGlyphs(surface, coverage, cw, ch, left + 6 * cw, y, path, kText);
			shownInputPath_ = inputPath_;
		}
		y += m.lineHeight + m.gap;

		// 帧率曲线：任务管理器风格，最近 60 秒，右侧为最新。
		{
			const int cx = left, cy = y, cwid = contentWidth, chei = m.chartHeight;
			BlendRect(surface, cx, cy, cx + cwid, cy + chei, kBlack, 70);
			for (int i = 1; i < 4; ++i)
				BlendRect(surface, cx, cy + chei * i / 4, cx + cwid, cy + chei * i / 4 + 1, kGrid, 26);
			for (int i = 1; i < 6; ++i)
				BlendRect(surface, cx + cwid * i / 6, cy, cx + cwid * i / 6 + 1, cy + chei, kGrid, 26);
			float maximum = 0.0f;
			for (std::size_t i = 0; i < chartCount_; ++i) maximum = std::max(maximum, chart_[i]);
			float scale = 30.0f;
			while (scale < maximum && scale < 1.0e7f) scale *= 2.0f;
			const int thickness = std::max(1, static_cast<int>(std::lround(1.5f * dpiScale_)));
			const std::size_t firstSlot = kChartSamples - chartCount_;
			int previousY = -1;
			for (int px = 0; px < cwid && chartCount_ > 0; ++px)
			{
				const float t = cwid > 1 ? px * static_cast<float>(kChartSamples - 1) / static_cast<float>(cwid - 1) : 0.0f;
				if (t < static_cast<float>(firstSlot)) { previousY = -1; continue; }
				const std::size_t i0 = static_cast<std::size_t>(t);
				const std::size_t i1 = std::min(i0 + 1, kChartSamples - 1);
				const float f = t - static_cast<float>(i0);
				const float v0 = chart_[i0 - firstSlot];
				const float v1 = chart_[i1 - firstSlot];
				const float v = v0 + (v1 - v0) * f;
				const int level = static_cast<int>(std::lround(std::clamp(v / scale, 0.0f, 1.0f) * static_cast<float>(chei - 2)));
				const int lineY = cy + (chei - 1) - level;
				for (int fy = lineY; fy < cy + chei - 1; ++fy)
					BlendPixel(surface, cx + px, fy, kChartLine, 52);
				int top = lineY, bottom = lineY + thickness - 1;
				if (previousY >= 0)
				{
					top = std::min(top, previousY);
					bottom = std::max(bottom, std::max(previousY, lineY));
				}
				for (int fy = top; fy <= bottom; ++fy)
					BlendPixel(surface, cx + px, fy, kChartLine, 255);
				previousY = lineY;
			}
			std::snprintf(text, sizeof(text), "%d", static_cast<int>(scale));
			DrawGlyphs(surface, coverage, cw, ch, cx + 3, cy + 1, text, kDim, 200);
			DrawGlyphs(surface, coverage, cw, ch, cx + cwid - 5 * cw - 3, cy + chei - ch - 1, "60 s", kDim, 200);
			y += chei + m.gap;
		}

		if (hasRows)
		{
			y += m.gap;
			int column = 0;
			for (const Column& definition : kColumns)
			{
				const int length = static_cast<int>(std::strlen(definition.header));
				const int offset = definition.rightAlign ? definition.width - length : 0;
				DrawGlyphs(surface, coverage, cw, ch, left + (column + offset) * cw, y, definition.header, kAccent);
				column += definition.width + 1;
			}
			y += m.lineHeight;
			BlendRect(surface, left, y - m.gap / 2, left + contentWidth, y - m.gap / 2 + 1, kGrid, 40);
			y += m.gap;
			for (int row = 0; row < rowCount; ++row)
			{
				const Slot& slot = *rows[row];
				const Rgb color = slot.terminal ? kDim : kText;
				int start = 0;
				for (const Column& definition : kColumns)
				{
					char cell[32] = {};
					definition.format(slot.sample, cell, sizeof(cell));
					const int length = static_cast<int>(std::strlen(cell));
					const int offset = definition.rightAlign ? std::max(0, definition.width - length) : 0;
					DrawGlyphs(surface, coverage, cw, ch, left + (start + offset) * cw, y, cell, color);
					start += definition.width + 1;
				}
				y += m.lineHeight;
			}
		}
	}

	void Overlay::RasterizeLabel(Slot& slot, const MarkerView& view)
	{
		const int cw = atlas_.cellWidth;
		const int ch = atlas_.cellHeight;
		const int padding = std::max(2, static_cast<int>(std::lround(3.0f * dpiScale_)));
		char line1[kLabelChars + 1] = {};
		char line2[kLabelChars + 1] = {};
		char* cursor = line1;
		cursor = AppendInt(cursor, slot.labelKey[0]);
		*cursor++ = '*';
		cursor = AppendInt(cursor, slot.labelKey[1]);
		cursor = AppendText(cursor, " px");
		*cursor = 0;
		const bool hasCm = slot.labelKey[4] != 0;
		if (hasCm)
		{
			cursor = line2;
			cursor = AppendTenths(cursor, slot.labelKey[2]);
			*cursor++ = '*';
			cursor = AppendTenths(cursor, slot.labelKey[3]);
			cursor = AppendText(cursor, " cm");
			*cursor = 0;
		}
		const int width = view.labelWidth;
		const int height = view.labelHeight;
		const Surface surface{ slot.label.data(), labelTileWidth_, width, height };
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x)
				surface.pixels[static_cast<std::size_t>(y) * surface.stride + x] = Premultiplied(kBlack, 158);
		DrawGlyphs(surface, atlas_.coverage, cw, ch, padding, padding, line1, kText);
		if (hasCm) DrawGlyphs(surface, atlas_.coverage, cw, ch, padding, padding + ch, line2, kText);
		slot.labelWidth = width;
		slot.labelHeight = height;
		slot.labelValid = true;
		slot.labelUpload = true;
		++slot.labelRevision;
	}

	void Overlay::UpdateMarkers(const FrameParams& params)
	{
		const float dpi = dpiScale_;
		const int cw = atlas_.cellWidth;
		const int ch = atlas_.cellHeight;
		const int padding = std::max(2, static_cast<int>(std::lround(3.0f * dpi)));
		markerBounds_ = {};
		for (std::size_t i = 0; i < kMaxContacts; ++i)
		{
			Slot& slot = slots_[i];
			MarkerView view;
			if (params.markers && slot.used && !slot.terminal)
			{
				const ContactSample& s = slot.sample;
				const float pressure = Known(s.pressure) ? std::min(s.pressure, 1.0f) : 0.5f;
				view.active = true;
				view.x = s.x;
				view.y = s.y;
				view.dotRadius = (2.5f + 3.5f * pressure) * dpi;
				view.ringRadius = (9.0f + 13.0f * pressure) * dpi;
				view.ringStroke = std::max(1.5f, 1.5f * dpi);
				float extent = view.ringRadius + view.ringStroke + 2.0f;
				RECT bounds = {
					static_cast<LONG>(std::floor(s.x - extent)), static_cast<LONG>(std::floor(s.y - extent)),
					static_cast<LONG>(std::ceil(s.x + extent)), static_cast<LONG>(std::ceil(s.y + extent)) };
				if (ValidArea(s))
				{
					const float halfWidth = std::min(s.areaWidthPx, kMaximumDrawnAreaPx) * 0.5f;
					const float halfHeight = std::min(s.areaHeightPx, kMaximumDrawnAreaPx) * 0.5f;
					view.hasBox = true;
					view.boxLeft = s.x - halfWidth;
					view.boxRight = s.x + halfWidth;
					view.boxTop = s.y - halfHeight;
					view.boxBottom = s.y + halfHeight;
					view.boxStroke = std::max(2.0f, std::round(2.0f * dpi));
					view.solidBox = ShouldShowSolidAreaBox(s.areaWidthPx, s.areaHeightPx, s.palmThresholdAreaPx);
					const float inflate = view.boxStroke + 3.0f; // 圆头线帽 + 抗锯齿
					UnionInto(bounds, RECT{ static_cast<LONG>(std::floor(view.boxLeft - inflate)),
						static_cast<LONG>(std::floor(view.boxTop - inflate)),
						static_cast<LONG>(std::ceil(view.boxRight + inflate)),
						static_cast<LONG>(std::ceil(view.boxBottom + inflate)) });

					// 标签文本由整数/十分位决定；只有内容变化时才重绘位图。
					const int widthPx = RoundToInt(s.areaWidthPx, 0, 9999);
					const int heightPx = RoundToInt(s.areaHeightPx, 0, 9999);
					const bool hasCm = s.cmPerPixelX > 0.0f && s.cmPerPixelY > 0.0f;
					const int widthCm = hasCm ? RoundToInt(s.areaWidthPx * s.cmPerPixelX * 10.0f, 0, 9999) : 0;
					const int heightCm = hasCm ? RoundToInt(s.areaHeightPx * s.cmPerPixelY * 10.0f, 0, 9999) : 0;
					int length1 = 0, length2 = 0;
					{
						char scratch[kLabelChars + 8];
						char* end = AppendInt(scratch, widthPx); ++end; end = AppendInt(end, heightPx); end += 3;
						length1 = std::min(static_cast<int>(end - scratch), kLabelChars);
						if (hasCm)
						{
							end = AppendTenths(scratch, widthCm); ++end; end = AppendTenths(end, heightCm); end += 3;
							length2 = std::min(static_cast<int>(end - scratch), kLabelChars);
						}
					}
					view.hasLabel = true;
					view.labelWidth = std::max(length1, length2) * cw + 2 * padding;
					view.labelHeight = (hasCm ? 2 : 1) * ch + 2 * padding;
					view.labelX = static_cast<int>(std::lround(s.x - view.labelWidth * 0.5f));
					view.labelY = static_cast<int>(std::ceil(view.boxBottom + view.boxStroke + 3.0f * dpi));
					const int key[6] = { widthPx, heightPx, widthCm, heightCm, hasCm ? 1 : 0, view.labelWidth };
					if (!slot.labelValid || std::memcmp(key, slot.labelKey, sizeof(key)) != 0 ||
						slot.labelWidth != view.labelWidth || slot.labelHeight != view.labelHeight)
					{
						std::memcpy(slot.labelKey, key, sizeof(key));
						RasterizeLabel(slot, view);
					}
					view.labelRevision = slot.labelRevision;
					UnionInto(bounds, RECT{ view.labelX - 1, view.labelY - 1,
						view.labelX + view.labelWidth + 1, view.labelY + view.labelHeight + 1 });
				}
				view.bounds = ClampRect(bounds, params.canvasWidth, params.canvasHeight);
			}
			markers_[i] = view;
			if (view.active) UnionInto(markerBounds_, view.bounds);
		}
	}

	RECT Overlay::Update(const FrameParams& params)
	{
		RECT dirty = {};
		if (params.canvasWidth <= 0 || params.canvasHeight <= 0) return dirty;
		const float dpi = (Finite(params.dpiScale) && params.dpiScale > 0.0f) ? params.dpiScale : 1.0f;
		bool needRaster = false;
		RECT staleBounds = {};
		if (atlas_.dpi <= 0.0f || std::abs(atlas_.dpi - dpi) > 0.001f)
		{
			// DPI 变化会重置板子/标记几何；旧范围仍要并入脏区，避免旧像素残留。
			staleBounds = VisibleBounds();
			BuildAtlas(dpi);
			needRaster = true;
		}
		canvasWidth_ = params.canvasWidth;
		canvasHeight_ = params.canvasHeight;

		inputPath_ = params.inputPath;
		SettleSnapshot(params.nowMs, params.targetFps);
		if (inputPath_ != shownInputPath_) perfDirty_ = true;
		if (perfDirty_) { needRaster = true; perfDirty_ = false; }

		// 接触表维护：未再观察到或已 Up 的行转为“保留”，超时后移除。
		for (Slot& slot : slots_)
		{
			if (!slot.used) continue;
			if (slot.speedAdvanced)
			{
				slot.speedAdvanced = false;
				slot.speedAdvanceMs = params.nowMs;
			}
			else if (!slot.terminal && slot.speedPx > 0.0f && params.nowMs - slot.speedAdvanceMs > 100.0)
			{
				// 输入停止（或手指/笔静止不再上报）：速度归零，而不是停在最后一个值。
				slot.speedPx = 0.0f;
				if (slot.speedCm > 0.0f) slot.speedCm = 0.0f;
				slot.sample.speedPxPerSec = slot.speedPx;
				slot.sample.speedCmPerSec = slot.speedCm;
			}
			if (!slot.terminal && (slot.sample.terminal || !slot.seen))
			{
				slot.terminal = true;
				slot.terminalMs = params.nowMs;
				rowsStructureDirty_ = true;
			}
			else if (slot.terminal && params.nowMs - slot.terminalMs >= kContactLingerMs)
			{
				slot.used = false;
				slot.terminal = false;
				slot.labelValid = false;
				slot.labelUpload = false;
				slot.sample = {};
				slot.speedQpc = 0;
				slot.speedPx = slot.speedCm = kUnknown;
				slot.speedAdvanced = false;
				slot.latencyQpc = 0;
				slot.latencyMs = kUnknown;
				rowsStructureDirty_ = true;
			}
			else if (!slot.terminal)
			{
				const ContactSample& a = slot.sample;
				const ContactSample& b = slot.rasterized;
				if (a.kind != b.kind || a.x != b.x || a.y != b.y || a.pressure != b.pressure ||
					a.tilt != b.tilt || a.orientation != b.orientation ||
					a.areaWidthPx != b.areaWidthPx || a.areaHeightPx != b.areaHeightPx ||
					a.cmPerPixelX != b.cmPerPixelX || a.cmPerPixelY != b.cmPerPixelY ||
					a.path != b.path || a.source != b.source || a.stage != b.stage ||
					a.publishCount != b.publishCount ||
					std::lround(a.latencyMs * 10.0f) != std::lround(b.latencyMs * 10.0f) ||
					a.contactId != b.contactId || a.previousContactId != b.previousContactId ||
					RoundToInt(a.speedPxPerSec, -1, 99999) != RoundToInt(b.speedPxPerSec, -1, 99999) ||
					std::lround(a.speedCmPerSec * 10.0f) != std::lround(b.speedCmPerSec * 10.0f))
					rowValuesDirty_ = true;
			}
		}
		if (rowsStructureDirty_) needRaster = true;
		else if (rowValuesDirty_ && params.nowMs - lastRowRasterMs_ >= kContactValueRefreshMs) needRaster = true;

		if (needRaster)
		{
			RasterizePlate();
			for (Slot& slot : slots_) if (slot.used) slot.rasterized = slot.sample;
			lastRowRasterMs_ = params.nowMs;
			rowsStructureDirty_ = false;
			rowValuesDirty_ = false;
			plateUpload_ = true;
		}

		// 锚点固定为“展开到最大（全部列、满行数）时水平、竖直居中”的位置：平时较窄的板子
		// 因此位于水平居中偏左处，内容变化时左端和上端不动，只向右、向下生长。
		const int left = std::max(0, (canvasWidth_ - capacityWidth_) / 2);
		const int top = std::max(0, (canvasHeight_ - capacityHeight_) / 2);
		plateRect_ = { left, top, left + plateWidth_, top + plateHeight_ };
		if (needRaster || gpuStale_ || !SameRect(plateRect_, committedPlateRect_))
		{
			UnionInto(dirty, committedPlateRect_);
			UnionInto(dirty, plateRect_);
			committedPlateRect_ = plateRect_;
			gpuStale_ = false;
		}

		UpdateMarkers(params);
		for (std::size_t i = 0; i < kMaxContacts; ++i)
		{
			const MarkerView& current = markers_[i];
			const MarkerView& previous = committedMarkers_[i];
			const bool same = current.active == previous.active &&
				(!current.active || (
					current.x == previous.x && current.y == previous.y &&
					current.dotRadius == previous.dotRadius && current.ringRadius == previous.ringRadius &&
					current.hasBox == previous.hasBox && current.boxLeft == previous.boxLeft &&
					current.boxTop == previous.boxTop && current.boxRight == previous.boxRight &&
					current.boxBottom == previous.boxBottom && current.solidBox == previous.solidBox &&
					current.hasLabel == previous.hasLabel && current.labelX == previous.labelX &&
					current.labelY == previous.labelY && current.labelWidth == previous.labelWidth &&
					current.labelHeight == previous.labelHeight && current.labelRevision == previous.labelRevision));
			if (same) continue;
			if (previous.active) UnionInto(dirty, previous.bounds);
			if (current.active) UnionInto(dirty, current.bounds);
		}
		committedMarkers_ = markers_;
		committedMarkerBounds_ = markerBounds_;
		UnionInto(dirty, staleBounds);
		return dirty;
	}
}
