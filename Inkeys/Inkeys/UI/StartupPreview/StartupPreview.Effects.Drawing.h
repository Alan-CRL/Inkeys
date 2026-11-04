#pragma once

#include "StartupPreview.Drawing.h"
#include "StartupPreview.Effects.Policy.h"
#include <d2d1_3.h>
#include <d2d1effects.h>
#include <new>

namespace Inkeys::UI::StartupPreview::Drawing
{
	using Microsoft::WRL::ComPtr;
	inline D2D1_COLOR_F EffectColor(std::uint32_t rgb, float alpha = 1.f) noexcept
	{
		return D2D1::ColorF((rgb & 255) / 255.f, ((rgb >> 8) & 255) / 255.f,
			((rgb >> 16) & 255) / 255.f, alpha);
	}

	// 生产和无窗口像素测试共用整组背景绘制，避免测试复制另一份效果/alpha算法。
	class EffectRenderer final
	{
	public:
		void Reset() noexcept
		{
			if (blur_) blur_->SetInput(0, nullptr);
			if (context_) context_->SetTarget(nullptr);
			shimmer_.Reset(); glint_.Reset(); fill_.Reset(); frame_.Reset(); maskBrush_.Reset();
			blur_.Reset(); field_.Reset(); fx_.Reset(); particleBatch_.Reset(); fxBatch_.Reset(); sprites_.Reset();
			shape_.Reset(); mask_.Reset(); base_.Reset(); group_.Reset(); context_.Reset();
			particles_.clear(); fieldPixels_.clear(); particleDestinations_.clear(); particleSources_.clear();
			particleColors_.clear(); fxDestinations_.clear(); fxSources_.clear(); fxColors_.clear(); fxTransforms_.clear();
			content_ = {};
		}
		// allowSpriteBatch 仅供测试强制走逐个 DrawBitmap 回退路径，验证两条路径像素一致。
		[[nodiscard]] HRESULT Initialize(ID2D1DeviceContext* context, UINT width, UINT height,
			Effects::Rect target, Effects::Rect scatter, float scale, bool floating,
			StartupPreviewEffect effect, Effects::Appearance appearance, std::uint32_t seed, bool allowSpriteBatch = true)
		{
			Reset();
			if (!context || !width || !height || scale <= 0.f || Effects::Width(target) <= 0.f || Effects::Height(target) <= 0.f) return E_INVALIDARG;
			context_ = context; width_ = width; height_ = height; target_ = target; scale_ = scale;
			floating_ = floating; effect_ = effect; appearance_ = appearance; seed_ = seed; content_ = target;
			baseWidth_ = static_cast<UINT>(std::lround(Effects::Width(target)));
			baseHeight_ = static_cast<UINT>(std::lround(Effects::Height(target)));
			HRESULT hr = Bitmap(baseWidth_, baseHeight_, &base_);
			if (SUCCEEDED(hr)) hr = Bitmap(width_, height_, &group_);
			const auto maskProperties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
				D2D1::PixelFormat(DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			if (SUCCEEDED(hr)) hr = context_->CreateBitmap(D2D1::SizeU(baseWidth_, baseHeight_), nullptr, 0, maskProperties, &mask_);
			ComPtr<ID2D1Factory> factory; context_->GetFactory(&factory);
			if (SUCCEEDED(hr)) hr = CreateShape(factory.Get(), static_cast<float>(baseWidth_), static_cast<float>(baseHeight_), scale, floating, &shape_);
			if (SUCCEEDED(hr)) hr = context_->CreateSolidColorBrush(EffectColor(appearance.surface), &fill_);
			if (SUCCEEDED(hr)) hr = context_->CreateSolidColorBrush(EffectColor(appearance.frame), &frame_);
			if (SUCCEEDED(hr)) hr = context_->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f), &maskBrush_);
			if (SUCCEEDED(hr)) hr = CreateShimmer(context_.Get(), D2D1::Point2F(), D2D1::Point2F(1.f, 1.f), &shimmer_, appearance.light && effect != StartupPreviewEffect::Aurora);
			if (FAILED(hr)) return hr;
			context_->SetTransform(D2D1::Matrix3x2F::Identity());
			context_->SetTarget(base_.Get()); context_->BeginDraw(); context_->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
			DrawShape(context_.Get(), shape_.Get(), fill_.Get(), frame_.Get(), scale_, 1.f, appearance_.canonicalFrameOpacity);
			hr = context_->EndDraw(); if (FAILED(hr)) return hr;
			context_->SetTarget(mask_.Get()); context_->BeginDraw(); context_->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
			maskBrush_->SetOpacity(1.f); context_->FillGeometry(shape_.Get(), maskBrush_.Get());
			maskBrush_->SetOpacity(appearance_.canonicalFrameOpacity); context_->DrawGeometry(shape_.Get(), maskBrush_.Get(), scale_);
			hr = context_->EndDraw(); context_->SetTarget(nullptr); if (FAILED(hr)) return hr;
			if (effect_ == StartupPreviewEffect::Aurora) return InitializeGlow();
			if (effect_ != StartupPreviewEffect::PixelAssembly) return S_OK;
			try
			{
				ComPtr<ID2D1Bitmap1> readable;
				const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
					D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
				hr = context_->CreateBitmap(D2D1::SizeU(baseWidth_, baseHeight_), nullptr, 0, properties, &readable);
				if (SUCCEEDED(hr)) hr = readable->CopyFromBitmap(nullptr, base_.Get(), nullptr);
				D2D1_MAPPED_RECT mapped{};
				if (SUCCEEDED(hr)) hr = readable->Map(D2D1_MAP_OPTIONS_READ, &mapped);
				if (FAILED(hr)) return hr;
				try { particles_ = Effects::BuildParticles(mapped.bits, baseWidth_, baseHeight_, mapped.pitch, target_, scatter, scale_, floating_, seed_); }
				catch (...) { (void)readable->Unmap(); throw; }
				hr = readable->Unmap(); if (FAILED(hr)) return hr;
				if (particles_.empty()) return E_FAIL;
				const std::size_t count = particles_.size();
				particleDestinations_.resize(count); particleSources_.resize(count); particleColors_.resize(count);
				fxDestinations_.resize(count * 2); fxSources_.resize(count * 2); fxColors_.resize(count * 2); fxTransforms_.resize(count * 2);
			}
			catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
			return InitializeParticles(allowSpriteBatch);
		}

		[[nodiscard]] HRESULT Prepare(double elapsed, D2D1_POINT_2F shimmerStart, D2D1_POINT_2F shimmerEnd)
		{
			if (!context_ || !group_) return E_UNEXPECTED;
			elapsed = (std::max)(0.0, elapsed);
			HRESULT hr = S_OK;
			if (effect_ == StartupPreviewEffect::Aurora) hr = UpdateGlow(elapsed);
			if (FAILED(hr)) return hr;
			content_ = target_;
			context_->SetTarget(group_.Get()); context_->SetTransform(D2D1::Matrix3x2F::Identity());
			context_->BeginDraw(); context_->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
			if (effect_ == StartupPreviewEffect::PixelAssembly && elapsed < Effects::AssemblySeconds)
			{
				DrawParticles(elapsed);
				hr = context_->EndDraw(); context_->SetTarget(nullptr); return hr;
			}
			context_->SetTransform(D2D1::Matrix3x2F::Translation(target_.left, target_.top));
			context_->DrawBitmap(base_.Get());
			if (effect_ == StartupPreviewEffect::Aurora) DrawGlow(elapsed);
			shimmer_->SetStartPoint(shimmerStart); shimmer_->SetEndPoint(shimmerEnd);
			shimmer_->SetOpacity(effect_ == StartupPreviewEffect::Aurora ? .45f : 1.f);
			DrawShimmer(context_.Get(), mask_.Get(), shimmer_.Get(), static_cast<float>(baseWidth_), static_cast<float>(baseHeight_));
			context_->SetTransform(D2D1::Matrix3x2F::Identity());
			hr = context_->EndDraw(); context_->SetTarget(nullptr); return hr;
		}
		void DrawBackground(ID2D1DeviceContext* destination) const
		{
			destination->DrawBitmap(group_.Get(), D2D1::RectF(0.f, 0.f, static_cast<float>(width_), static_cast<float>(height_)),
				appearance_.backgroundOpacity, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
		}
		// 本帧实际有内容的整像素范围（含目标底板与进度）；呈现层与上一已提交帧求并集作为脏区。
		[[nodiscard]] RECT ContentBounds() const noexcept
		{
			const float padding = 2.f * scale_;
			return { static_cast<LONG>((std::max)(0.f, std::floor(content_.left - padding))),
				static_cast<LONG>((std::max)(0.f, std::floor(content_.top - padding))),
				static_cast<LONG>((std::min)(static_cast<float>(width_), std::ceil(content_.right + padding))),
				static_cast<LONG>((std::min)(static_cast<float>(height_), std::ceil(content_.bottom + padding))) };
		}
		[[nodiscard]] const std::vector<Effects::Particle>& Particles() const noexcept { return particles_; }
		[[nodiscard]] bool UsesSpriteBatch() const noexcept { return sprites_ != nullptr; }
		[[nodiscard]] ID2D1Bitmap1* Base() const noexcept { return base_.Get(); }
	private:
		[[nodiscard]] HRESULT Bitmap(UINT width, UINT height, ID2D1Bitmap1** bitmap)
		{
			const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			return context_->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, properties, bitmap);
		}
		[[nodiscard]] HRESULT InitializeGlow()
		{
			fieldWidth_ = (std::max)(2U, (baseWidth_ + Effects::GlowFieldScale - 1) / Effects::GlowFieldScale);
			fieldHeight_ = (std::max)(2U, (baseHeight_ + Effects::GlowFieldScale - 1) / Effects::GlowFieldScale);
			try { fieldPixels_.resize(static_cast<std::size_t>(fieldWidth_) * fieldHeight_); }
			catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
			palette_ = Effects::MakeGlowPalette(appearance_.light);
			const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			HRESULT hr = context_->CreateBitmap(D2D1::SizeU(fieldWidth_, fieldHeight_), nullptr, 0, properties, &field_);
			if (SUCCEEDED(hr)) hr = context_->CreateEffect(CLSID_D2D1GaussianBlur, &blur_);
			// 低分辨率色场只做一次轻模糊再线性放大；HARD边界让色场铺满到轮廓，不产生四周羽化。
			if (SUCCEEDED(hr)) hr = blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 1.f);
			if (SUCCEEDED(hr)) hr = blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
			if (SUCCEEDED(hr)) blur_->SetInput(0, field_.Get());
			if (FAILED(hr)) return hr;
			// 深色镜面光减半，避免在暗底上盖一层灰白。
			const float glint = appearance_.light ? .35f : .05f;
			const std::array<D2D1_GRADIENT_STOP, 2> stops{ D2D1::GradientStop(0.f, D2D1::ColorF(1.f, 1.f, 1.f, glint)),
				D2D1::GradientStop(1.f, D2D1::ColorF(1.f, 1.f, 1.f, 0.f)) };
			ComPtr<ID2D1GradientStopCollection> collection;
			hr = context_->CreateGradientStopCollection(stops.data(), static_cast<UINT32>(stops.size()), &collection);
			if (SUCCEEDED(hr)) hr = context_->CreateRadialGradientBrush(
				D2D1::RadialGradientBrushProperties(D2D1::Point2F(), D2D1::Point2F(), 1.f, 1.f), collection.Get(), &glint_);
			return hr;
		}
		[[nodiscard]] HRESULT UpdateGlow(double elapsed)
		{
			Effects::GlowField(palette_, elapsed, fieldWidth_, fieldHeight_, static_cast<float>(baseWidth_),
				static_cast<float>(baseHeight_), fieldPixels_.data());
			return field_->CopyFromMemory(nullptr, fieldPixels_.data(), fieldWidth_ * sizeof(std::uint32_t));
		}
		void DrawGlow(double elapsed)
		{
			// 只按实际轮廓裁剪；连续色场以 source-over 合成，深浅只差明度与强度。
			const auto translation = D2D1::Matrix3x2F::Translation(target_.left, target_.top);
			context_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), shape_.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
				D2D1::IdentityMatrix(), Effects::GlowOpacity(appearance_.light)), nullptr);
			const float fieldScale = static_cast<float>(Effects::GlowFieldScale);
			context_->SetTransform(D2D1::Matrix3x2F::Scale(fieldScale, fieldScale) * translation);
			context_->DrawImage(blur_.Get(), D2D1::Point2F(), D2D1::RectF(0.f, 0.f, static_cast<float>(fieldWidth_), static_cast<float>(fieldHeight_)),
				D2D1_INTERPOLATION_MODE_LINEAR);
			context_->SetTransform(translation);
			const float w = static_cast<float>(baseWidth_), h = static_cast<float>(baseHeight_);
			glint_->SetCenter(D2D1::Point2F(w * (.5f + .42f * static_cast<float>(std::sin(elapsed * .31))), 0.f));
			glint_->SetRadiusX(w * .22f); glint_->SetRadiusY(w * .22f);
			context_->FillRectangle(D2D1::RectF(0.f, 0.f, w, h), glint_.Get());
			context_->PopLayer();
		}

		[[nodiscard]] HRESULT InitializeParticles(bool allowSpriteBatch)
		{
			// 32x8 特效图集：上4行为拖尾色 alpha 0→1 横向渐变，下4行为不透明落位微光色；全部只靠 alpha 调制。
			const auto trail = appearance_.light ? D2D1::ColorF(0.f, 106.f / 255.f, 99.f / 255.f) : D2D1::ColorF(88.f / 255.f, 1.f, 236.f / 255.f);
			const auto flash = appearance_.light ? D2D1::ColorF(0.f, 150.f / 255.f, 140.f / 255.f) : D2D1::ColorF(200.f / 255.f, 1.f, 248.f / 255.f);
			std::array<std::uint32_t, FxWidth * FxHeight> atlas{};
			const auto pack = [](D2D1_COLOR_F c, float a) {
				const auto byte = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
				return (byte(a) << 24) | (byte(c.r * a) << 16) | (byte(c.g * a) << 8) | byte(c.b * a);
			};
			for (UINT y = 0; y < FxHeight; ++y)
				for (UINT x = 0; x < FxWidth; ++x)
					atlas[y * FxWidth + x] = y < FxHeight / 2 ? pack(trail, static_cast<float>(x) / (FxWidth - 1)) : pack(flash, 1.f);
			const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			HRESULT hr = context_->CreateBitmap(D2D1::SizeU(FxWidth, FxHeight), atlas.data(), FxWidth * sizeof(std::uint32_t), properties, &fx_);
			if (FAILED(hr)) return hr;
			for (std::size_t i = 0; i < particles_.size(); ++i)
			{
				const auto& source = particles_[i].source;
				particleSources_[i] = D2D1::RectU(static_cast<UINT32>(source.left), static_cast<UINT32>(source.top),
					static_cast<UINT32>(source.right), static_cast<UINT32>(source.bottom));
				fxSources_[i] = D2D1::RectU(0, 0, FxWidth, FxHeight / 2);
				fxSources_[particles_.size() + i] = D2D1::RectU(FxWidth / 4, FxHeight / 2, FxWidth * 3 / 4, FxHeight);
			}
			// Win10 起用 SpriteBatch 一次提交全部像素；更早系统或测试回退为逐个 DrawBitmap，视觉一致。
			if (!allowSpriteBatch || FAILED(context_->QueryInterface(IID_PPV_ARGS(&sprites_)))) { sprites_.Reset(); return S_OK; }
			hr = sprites_->CreateSpriteBatch(&particleBatch_);
			if (SUCCEEDED(hr)) hr = sprites_->CreateSpriteBatch(&fxBatch_);
			if (SUCCEEDED(hr)) hr = particleBatch_->AddSprites(static_cast<UINT32>(particles_.size()), particleDestinations_.data(), particleSources_.data());
			if (SUCCEEDED(hr)) hr = fxBatch_->AddSprites(static_cast<UINT32>(fxDestinations_.size()), fxDestinations_.data(), fxSources_.data());
			if (FAILED(hr)) { particleBatch_.Reset(); fxBatch_.Reset(); sprites_.Reset(); }
			return S_OK;
		}
		void BuildSprites(double elapsed)
		{
			const std::size_t count = particles_.size();
			const float trailAlpha = appearance_.light ? .30f : .34f, flashAlpha = appearance_.light ? .28f : .5f;
			Effects::Rect bounds = target_;
			const auto include = [&bounds](float left, float top, float right, float bottom) {
				bounds.left = (std::min)(bounds.left, left); bounds.top = (std::min)(bounds.top, top);
				bounds.right = (std::max)(bounds.right, right); bounds.bottom = (std::max)(bounds.bottom, bottom);
			};
			const D2D1_RECT_F empty{};
			for (std::size_t i = 0; i < count; ++i)
			{
				const auto& p = particles_[i];
				const auto state = Effects::Evaluate(p, elapsed);
				const float w = Effects::Width(p.source), h = Effects::Height(p.source);
				const bool started = elapsed >= p.begin;
				const float opacity = started ? Effects::ParticleOpacity(state.progress) : 0.f;
				const float size = Effects::ParticleScale(state.progress);
				const float halfW = w * size * .5f, halfH = h * size * .5f;
				particleDestinations_[i] = opacity > 0.f ? D2D1::RectF(state.position.x - halfW, state.position.y - halfH,
					state.position.x + halfW, state.position.y + halfH) : empty;
				particleColors_[i] = Tint(opacity);
				if (opacity > 0.f) include(state.position.x - halfW, state.position.y - halfH, state.position.x + halfW, state.position.y + halfH);
				// 速度线只取最近45ms的位移：随速度伸缩，落位后长度归零，不再有累积光晕。
				auto& streak = fxDestinations_[i]; streak = empty; fxColors_[i] = Tint(0.f); fxTransforms_[i] = D2D1::Matrix3x2F::Identity();
				if (started && state.progress < 1.f)
				{
					const auto previous = Effects::Position(p, (std::max)(static_cast<double>(p.begin), elapsed - Effects::StreakSampleSeconds));
					const float dx = state.position.x - previous.x, dy = state.position.y - previous.y, length = std::hypot(dx, dy);
					if (length >= 1.2f * scale_)
					{
						const float half = w * .55f * .5f;
						streak = D2D1::RectF(0.f, -half, length, half);
						fxColors_[i] = Tint(trailAlpha * static_cast<float>(Effects::Smooth(state.progress / .3)));
						fxTransforms_[i] = D2D1::Matrix3x2F::Rotation(std::atan2(dy, dx) * 57.29578f) * D2D1::Matrix3x2F::Translation(previous.x, previous.y);
						include((std::min)(previous.x, state.position.x) - half, (std::min)(previous.y, state.position.y) - half,
							(std::max)(previous.x, state.position.x) + half, (std::max)(previous.y, state.position.y) + half);
					}
				}
				const float flash = Effects::FlashOpacity(p, elapsed) * flashAlpha;
				fxDestinations_[count + i] = flash > 0.f ? D2D1::RectF(p.target.x - w * .5f, p.target.y - h * .5f, p.target.x + w * .5f, p.target.y + h * .5f) : empty;
				fxColors_[count + i] = Tint(flash);
				fxTransforms_[count + i] = D2D1::Matrix3x2F::Identity();
			}
			content_ = bounds;
		}
		void DrawParticles(double elapsed)
		{
			BuildSprites(elapsed);
			const auto count = static_cast<UINT32>(particles_.size());
			// 两条路径都用 ALIASED：SpriteBatch 强制要求，回退路径也避免亚像素相邻块的抗锯齿接缝。
			const auto previous = context_->GetAntialiasMode();
			context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
			if (sprites_)
			{
				const HRESULT particlesSet = particleBatch_->SetSprites(0, count, particleDestinations_.data(), nullptr, particleColors_.data());
				const HRESULT fxSet = fxBatch_->SetSprites(0, count * 2, fxDestinations_.data(), nullptr, fxColors_.data(), fxTransforms_.data());
				if (SUCCEEDED(particlesSet) && SUCCEEDED(fxSet))
				{
					// 图层顺序：速度线在下，像素居中，落位微光在上。
					sprites_->DrawSpriteBatch(fxBatch_.Get(), 0, count, fx_.Get(), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, D2D1_SPRITE_OPTIONS_CLAMP_TO_SOURCE_RECTANGLE);
					sprites_->DrawSpriteBatch(particleBatch_.Get(), 0, count, base_.Get(), D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_SPRITE_OPTIONS_NONE);
					sprites_->DrawSpriteBatch(fxBatch_.Get(), count, count, fx_.Get(), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, D2D1_SPRITE_OPTIONS_CLAMP_TO_SOURCE_RECTANGLE);
					context_->SetAntialiasMode(previous);
					return;
				}
			}
			const auto drawFx = [this](std::size_t begin, std::size_t end) {
				for (std::size_t i = begin; i < end; ++i)
				{
					if (fxColors_[i].a <= 0.f) continue;
					const auto& s = fxSources_[i];
					context_->SetTransform(fxTransforms_[i]);
					context_->DrawBitmap(fx_.Get(), fxDestinations_[i], fxColors_[i].a, D2D1_INTERPOLATION_MODE_LINEAR,
						D2D1::RectF(static_cast<float>(s.left), static_cast<float>(s.top), static_cast<float>(s.right), static_cast<float>(s.bottom)));
				}
				context_->SetTransform(D2D1::Matrix3x2F::Identity());
			};
			drawFx(0, count);
			for (UINT32 i = 0; i < count; ++i)
			{
				if (particleColors_[i].a <= 0.f) continue;
				const auto& s = particleSources_[i];
				context_->DrawBitmap(base_.Get(), particleDestinations_[i], particleColors_[i].a, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
					D2D1::RectF(static_cast<float>(s.left), static_cast<float>(s.top), static_cast<float>(s.right), static_cast<float>(s.bottom)));
			}
			drawFx(count, count * 2);
			context_->SetAntialiasMode(previous);
		}
		// 精灵颜色按直通 alpha 与预乘位图相乘；RGB 保持 1 只缩放不透明度，与 DrawBitmap(opacity) 等价。
		[[nodiscard]] static D2D1_COLOR_F Tint(float alpha) noexcept { return D2D1::ColorF(1.f, 1.f, 1.f, alpha); }

		static constexpr UINT FxWidth = 32, FxHeight = 8;
		ComPtr<ID2D1DeviceContext> context_;
		ComPtr<ID2D1DeviceContext3> sprites_;
		ComPtr<ID2D1SpriteBatch> particleBatch_, fxBatch_;
		ComPtr<ID2D1Bitmap1> base_, mask_, group_, field_, fx_;
		ComPtr<ID2D1Geometry> shape_;
		ComPtr<ID2D1SolidColorBrush> fill_, frame_, maskBrush_;
		ComPtr<ID2D1LinearGradientBrush> shimmer_;
		ComPtr<ID2D1RadialGradientBrush> glint_;
		ComPtr<ID2D1Effect> blur_;
		std::vector<Effects::Particle> particles_;
		std::vector<std::uint32_t> fieldPixels_;
		std::vector<D2D1_RECT_F> particleDestinations_, fxDestinations_;
		std::vector<D2D1_RECT_U> particleSources_, fxSources_;
		std::vector<D2D1_COLOR_F> particleColors_, fxColors_;
		std::vector<D2D1_MATRIX_3X2_F> fxTransforms_;
		Effects::GlowPalette palette_{};
		Effects::Rect target_{}, content_{};
		Effects::Appearance appearance_{};
		StartupPreviewEffect effect_ = StartupPreviewEffect::Shimmer;
		UINT width_ = 0, height_ = 0, baseWidth_ = 0, baseHeight_ = 0, fieldWidth_ = 0, fieldHeight_ = 0;
		float scale_ = 1.f;
		bool floating_ = false;
		std::uint32_t seed_ = 1;
	};
}
