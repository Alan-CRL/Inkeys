#pragma once

#include "../StartupPreview.Effect.h"
#include "../Bar/Bar.ThemeMaterial.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Inkeys::UI::StartupPreview::Effects
{
	inline constexpr double AssemblySeconds = 2.4;
	// 最晚落位留出完整微光时长，保证2.4秒时所有像素已与底板逐像素一致。
	inline constexpr double LatestArrivalSeconds = 2.16;
	inline constexpr double ArrivalFlashSeconds = .22;
	inline constexpr double StreakSampleSeconds = .045;
	inline constexpr std::size_t MaximumParticles = 2048;
	inline constexpr float ParticleTileDip = 5.5f;
	// 深浅都连续铺满；深色全部用低明度色，因此可用更高强度而不整体发灰。
	[[nodiscard]] inline constexpr float GlowOpacity(bool light) noexcept { return light ? .46f : .8f; }

	struct Point final { float x = 0.f, y = 0.f; };
	struct Rect final { float left = 0.f, top = 0.f, right = 0.f, bottom = 0.f; };
	inline float Width(Rect r) noexcept { return (std::max)(0.f, r.right - r.left); }
	inline float Height(Rect r) noexcept { return (std::max)(0.f, r.bottom - r.top); }
	inline bool Valid(Rect r) noexcept
	{
		return std::isfinite(r.left) && std::isfinite(r.top) && std::isfinite(r.right)
			&& std::isfinite(r.bottom) && std::isfinite(Width(r)) && std::isfinite(Height(r))
			&& Width(r) > 0.f && Height(r) > 0.f;
	}
	inline double Smooth(double value) noexcept
	{
		const double t = std::clamp(value, 0.0, 1.0);
		return t * t * (3.0 - 2.0 * t);
	}
	inline bool AssemblyReady(StartupPreviewEffect effect, double elapsed) noexcept
	{
		return effect != StartupPreviewEffect::PixelAssembly || elapsed >= AssemblySeconds;
	}
	struct LinearColor final { float r = 0.f, g = 0.f, b = 0.f; };
	// OKLCH到sRGB（D2D默认颜色空间）；同亮度不同色相保持感知一致，避免互补色混成灰褐。
	inline LinearColor Oklch(float lightness, float chroma, float hueDegrees) noexcept
	{
		const float h = hueDegrees * .017453293f, a = chroma * std::cos(h), b = chroma * std::sin(h);
		const auto cube = [](float v) { return v * v * v; };
		const float l = cube(lightness + .3963377774f * a + .2158037573f * b);
		const float m = cube(lightness - .1055613458f * a - .0638541728f * b);
		const float s = cube(lightness - .0894841775f * a - 1.2914855480f * b);
		const auto encode = [](float v) {
			v = std::clamp(v, 0.f, 1.f);
			return v <= .0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.f / 2.4f) - .055f;
		};
		return { encode(4.0767416621f * l - 3.3077115913f * m + .2309699292f * s),
			encode(-1.2684380046f * l + 2.6097574011f * m - .3413193965f * s),
			encode(-.0041960863f * l - .7034186147f * m + 1.7076147010f * s) };
	}
	// 色相锁定在薰衣草→兰花粉→樱花粉→玫瑰（约280°~357°），中心只在306°~330°间缓慢漂移。
	inline float GlowHueCenter(double elapsed) noexcept
	{
		return 318.f + 12.f * static_cast<float>(std::sin(elapsed * .13));
	}

	// 柔光流彩色场以1/4分辨率在CPU生成：色相按权重线性混合且始终位于同一粉紫窗口内，任何位置都不会混出灰褐。
	inline constexpr std::uint32_t GlowFieldScale = 4;
	struct GlowPalette final
	{
		// 每度一个预计算sRGB颜色；两行分别为色块间较暗与色块中心较亮的明度。
		std::array<LinearColor, 360> dim{}, bright{};
	};
	inline GlowPalette MakeGlowPalette(bool light) noexcept
	{
		GlowPalette palette;
		// 深浅同一铺色方式：浅色为高明度淡粉紫；深色色块间为深梅紫/暗玫瑰，色块中心升到更亮更艳的薰衣草玫瑰。
		// 深色不使用任何高明度浅色，避免浅色半透明叠在暗底上整体发灰。
		const float low = light ? .77f : .33f, high = light ? .84f : .58f;
		const float lowChroma = light ? .16f : .11f, highChroma = light ? .16f : .18f;
		for (int hue = 0; hue < 360; ++hue)
		{
			palette.dim[hue] = Oklch(low, lowChroma, static_cast<float>(hue));
			palette.bright[hue] = Oklch(high, highChroma, static_cast<float>(hue));
		}
		return palette;
	}
	// 输出BGRA预乘（alpha恒为255）；logical尺寸为全分辨率底板，像素中心按GlowFieldScale映射。
	inline void GlowField(const GlowPalette& palette, double elapsed, std::uint32_t width, std::uint32_t height,
		float logicalWidth, float logicalHeight, std::uint32_t* output) noexcept
	{
		if (!output || !width || !height || logicalWidth <= 0.f || logicalHeight <= 0.f) return;
		struct Blob final { float x, y, rx, ry, hue; };
		constexpr std::array<float, 7> phases{ .7f, 2.8f, 4.9f, 7.0f, 9.1f, 11.2f, 13.3f };
		constexpr std::array<float, 7> speeds{ .23f, .3114f, .3928f, .2542f, .3356f, .417f, .2784f };
		constexpr std::array<float, 7> offsets{ -62.f, 18.f, -24.f, 62.f, -4.f, 40.f, -44.f };
		constexpr std::array<float, 7> heights{ 1.f, 1.305f, 1.11f, 1.415f, 1.22f, 1.025f, 1.33f };
		const float t = static_cast<float>(elapsed), center = GlowHueCenter(elapsed);
		std::array<Blob, 7> blobs{};
		for (std::size_t i = 0; i < blobs.size(); ++i)
		{
			const float phase = phases[i], speed = speeds[i];
			blobs[i] = { .5f + .5f * std::sin(phase + t * speed), .5f + .55f * std::sin(phase * 1.7f + t * (speed + .11f)),
				.16f + .05f * std::sin(t * .7f + phase), .9f * heights[i], center + offsets[i] * .35f + 5.f * std::sin(t * .5f + phase) };
		}
		const auto sample = [](const std::array<LinearColor, 360>& row, float hue) {
			hue = std::fmod(hue, 360.f); if (hue < 0.f) hue += 360.f;
			const int a = static_cast<int>(hue) % 360, b = (a + 1) % 360; const float f = hue - std::floor(hue);
			return LinearColor{ row[a].r + (row[b].r - row[a].r) * f, row[a].g + (row[b].g - row[a].g) * f, row[a].b + (row[b].b - row[a].b) * f };
		};
		for (std::uint32_t y = 0; y < height; ++y)
		{
			const float v = (y + .5f) * GlowFieldScale / logicalHeight;
			for (std::uint32_t x = 0; x < width; ++x)
			{
				const float u = (x + .5f) * GlowFieldScale / logicalWidth;
				// 低权重的横向底色保证内侧连续有色，色块再在其上按权重改变色相与明度。
				float weight = .08f, hue = (center - 14.f + 28.f * u) * .08f, peak = 0.f;
				for (const auto& b : blobs)
				{
					const float dx = (u - b.x) / b.rx, dy = (v - b.y) / b.ry;
					const float w = std::exp(-(dx * dx + dy * dy) * 2.2f);
					weight += w; hue += w * b.hue; peak = (std::max)(peak, w);
				}
				hue /= weight;
				const auto dim = sample(palette.dim, hue), bright = sample(palette.bright, hue);
				const auto channel = [peak](float a, float b) {
					return static_cast<std::uint32_t>(std::lround(std::clamp(a + (b - a) * peak, 0.f, 1.f) * 255.f));
				};
				output[static_cast<std::size_t>(y) * width + x] = 0xff000000U | (channel(dim.r, bright.r) << 16)
					| (channel(dim.g, bright.g) << 8) | channel(dim.b, bright.b);
			}
		}
	}

	struct Appearance final
	{
		bool light = false;
		std::uint32_t surface = BarThemeMaterial::Rgb(24, 24, 24);
		std::uint32_t frame = BarThemeMaterial::Rgb(255, 255, 255);
		float backgroundOpacity = .8f;
		float contentOpacity = 1.f;
		float canonicalFrameOpacity = .225f;
	};
	inline Appearance ResolveAppearance(int themeMode, int background, int content, bool floating) noexcept
	{
		Appearance result;
		result.light = themeMode == 2 && !floating;
		const float themeOpacity = floating ? .6f : static_cast<float>(
			BarThemeMaterial::DefaultResidentFillOpacity(result.light ? 1.0 : 0.0));
		result.backgroundOpacity = background == 0 ? themeOpacity
			: std::clamp(background, 30, 100) * .01f * (floating ? .75f : 1.f);
		result.contentOpacity = std::clamp(content, 30, 100) * .01f;
		// 组内底板归一为1；用户透明度只在整组合成时应用一次，描边保持主题默认比例。
		result.canonicalFrameOpacity = .18f / themeOpacity;
		if (result.light)
		{
			result.surface = BarThemeMaterial::LightColor(BarThemeMaterial::ColorRole::Surface);
			result.frame = BarThemeMaterial::LightColor(BarThemeMaterial::ColorRole::SurfaceFrame);
		}
		return result;
	}

	inline Rect ScatterBounds(Rect target, Rect work, bool floating) noexcept
	{
		const float w = Width(target), h = Height(target);
		// 汇聚来源只在目标上方/两侧的收敛区域：主栏约1.5W×2.4H且底边对齐，角落外扩一圈。
		Rect area = floating ? Rect{ target.left - w * .5f, target.top - h * .6f,
			target.right + w * .5f, target.bottom + h * .3f }
			: Rect{ target.left - w * .25f, target.bottom - h * 2.4f,
				target.right + w * .25f, target.bottom };
		area.left = (std::max)(work.left, area.left);
		area.top = (std::max)(work.top, area.top);
		area.right = (std::min)(work.right, area.right);
		area.bottom = (std::min)(work.bottom, area.bottom);
		return Width(area) > 0.f && Height(area) > 0.f ? area : target;
	}
	inline Rect PresentationBounds(Rect target, Rect scatter, Rect work, float scale) noexcept
	{
		const float padding = 8.f * scale;
		return { std::floor((std::min)(target.left, (std::max)(work.left, scatter.left - padding))),
			std::floor((std::min)(target.top, (std::max)(work.top, scatter.top - padding))),
			std::ceil((std::max)(target.right, (std::min)(work.right, scatter.right + padding))),
			std::ceil((std::max)(target.bottom, (std::min)(work.bottom, scatter.bottom + padding))) };
	}
	class Random final
	{
	public:
		explicit Random(std::uint32_t seed) : state_(seed ? seed : 1U) {}
		float Next() noexcept
		{
			state_ ^= state_ << 13; state_ ^= state_ >> 17; state_ ^= state_ << 5;
			return static_cast<float>(state_ >> 8) / 16777216.f;
		}
	private:
		std::uint32_t state_;
	};

	struct Particle final
	{
		Rect source;
		Point target, origin;
		float bend = 0.f;
		float begin = 0.f;
		float arrive = 0.f;
	};
	struct ParticleState final
	{
		Point position;
		float progress = 0.f;
	};
	inline ParticleState Evaluate(const Particle& p, double elapsed) noexcept
	{
		const double span = (std::max)(1e-3, static_cast<double>(p.arrive - p.begin));
		const float u = static_cast<float>(std::clamp((elapsed - p.begin) / span, 0.0, 1.0));
		if (u >= 1.f) return { p.target, 1.f };
		// 三次缓出：远处快速飞入、落位前减速；弧线偏移在起止两端都归零。
		const float e = 1.f - (1.f - u) * (1.f - u) * (1.f - u);
		const float dx = p.target.x - p.origin.x, dy = p.target.y - p.origin.y;
		const float length = (std::max)(1e-3f, std::hypot(dx, dy));
		const float offset = p.bend * std::sin(3.14159265f * u) * (1.f - u);
		return { { p.origin.x + dx * e - dy / length * offset, p.origin.y + dy * e + dx / length * offset }, u };
	}
	inline Point Position(const Particle& p, double elapsed) noexcept { return Evaluate(p, elapsed).position; }
	// 像素自身随进度淡入并由0.55倍放大到原尺寸。
	inline float ParticleOpacity(float progress) noexcept { return static_cast<float>(Smooth(progress / .35)); }
	inline float ParticleScale(float progress) noexcept { return 1.f - .45f * (1.f - static_cast<float>(Smooth(progress / .6))); }
	inline float FlashOpacity(const Particle& p, double elapsed) noexcept
	{
		const double f = (elapsed - p.arrive) / ArrivalFlashSeconds;
		return f < 0.0 || f >= 1.0 ? 0.f : static_cast<float>((1.0 - f) * (1.0 - f));
	}

	inline std::vector<Particle> BuildParticles(const std::uint8_t* bgra, std::uint32_t width,
		std::uint32_t height, std::uint32_t pitch, Rect target, Rect scatter,
		float scale, bool floating, std::uint32_t seed)
	{
		std::vector<Particle> particles;
		if (!bgra || !width || !height || width > 32768 || height > 32768 || pitch < width * 4
			|| !std::isfinite(scale) || scale <= 0.f || scale > 32768.f || !Valid(target) || !Valid(scatter)) return particles;
		Random random(seed);
		int tile = (std::max)(1, static_cast<int>(std::ceil((std::max)(ParticleTileDip * scale,
			std::sqrt(static_cast<float>(width) * height / static_cast<float>(MaximumParticles))))));
		while (static_cast<std::uint64_t>((width + tile - 1) / tile) * ((height + tile - 1) / tile) > MaximumParticles) ++tile;
		const float centerX = target.left + Width(target) * .5f, centerY = target.top + Height(target) * .5f;
		// 主栏的视觉汇聚点在其下方，像素从上方与两侧飞来；角落从按钮中心向外辐射。
		const Point sink = floating ? Point{ centerX, centerY } : Point{ centerX, target.bottom + Height(target) * 1.2f };
		const float reach = floating ? (std::max)(Width(target), Height(target)) * .5f * 1.4142f : Width(target) * .5f;
		const float nearDistance = (floating ? 30.f : 70.f) * scale, farDistance = (floating ? 70.f : 170.f) * scale;
		for (std::uint32_t y = 0; y < height; y += tile)
			for (std::uint32_t x = 0; x < width; x += tile)
			{
				const auto w = (std::min)(static_cast<std::uint32_t>(tile), width - x);
				const auto h = (std::min)(static_cast<std::uint32_t>(tile), height - y);
				bool visible = false;
				for (std::uint32_t row = y; row < y + h && !visible; ++row)
					for (std::uint32_t col = x; col < x + w; ++col) if (bgra[row * pitch + col * 4 + 3]) { visible = true; break; }
				if (!visible) continue;
				Particle p;
				p.source = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + w), static_cast<float>(y + h) };
				p.target = { target.left + x + w * .5f, target.top + y + h * .5f };
				const float dx = p.target.x - sink.x, dy = p.target.y - sink.y;
				const float angle = std::atan2(dy, dx) + (random.Next() - .5f) * .9f;
				const float distance = nearDistance + random.Next() * (farDistance - nearDistance);
				p.origin = { std::clamp(p.target.x + std::cos(angle) * distance, scatter.left, scatter.right),
					std::clamp(p.target.y + std::sin(angle) * distance * (floating ? 1.f : .75f), scatter.top, scatter.bottom) };
				// 由中心向外分波落位；同一波内随机错开，避免整齐的扫描线。
				const float spread = floating ? std::hypot(p.target.x - centerX, p.target.y - centerY) / reach
					: std::abs(p.target.x - centerX) / reach;
				p.arrive = static_cast<float>((std::min)(LatestArrivalSeconds,
					.62 + 1.36 * std::pow(std::clamp(spread, 0.f, 1.f), .85f) + random.Next() * .2));
				p.begin = (std::max)(0.f, p.arrive - (.78f + random.Next() * .36f));
				p.bend = (random.Next() - .5f) * 46.f * scale;
				particles.push_back(p);
			}
		return particles;
	}
}
