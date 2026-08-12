#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Inkeys::Graphics::Detail
{
	[[nodiscard]] bool EnsureGdiplusReady() noexcept;

	class DibSurfaceImpl
	{
	public:
		using Pixel = std::uint32_t;

		DibSurfaceImpl() noexcept = default;
		DibSurfaceImpl(int width, int height);
		DibSurfaceImpl(const DibSurfaceImpl& other);
		DibSurfaceImpl(DibSurfaceImpl&& other) noexcept;
		DibSurfaceImpl& operator=(DibSurfaceImpl other) noexcept;
		~DibSurfaceImpl();

		[[nodiscard]] bool resize(int width, int height) noexcept;
		void reset() noexcept;
		void swap(DibSurfaceImpl& other) noexcept;

		[[nodiscard]] int width() const noexcept { return width_; }
		[[nodiscard]] int height() const noexcept { return height_; }
		[[nodiscard]] bool empty() const noexcept { return !dc_ || !bitmap_ || !bits_; }
		[[nodiscard]] HDC dc() const noexcept { return dc_; }
		[[nodiscard]] HBITMAP bitmap() const noexcept { return bitmap_; }
		[[nodiscard]] std::span<Pixel> pixels() noexcept;
		[[nodiscard]] std::span<const Pixel> pixels() const noexcept;

		void clear(Pixel bgra = 0) noexcept;
		[[nodiscard]] bool equals(const DibSurfaceImpl& other) const noexcept;
		[[nodiscard]] bool composite(
			const DibSurfaceImpl& source,
			int destinationX = 0,
			int destinationY = 0,
			BYTE opacity = 255) noexcept;
		[[nodiscard]] bool compositeScaled(
			const DibSurfaceImpl& source,
			const RECT& destination,
			const RECT& sourceRect,
			BYTE opacity = 255) noexcept;

		[[nodiscard]] bool loadFromFile(
			std::wstring_view path,
			int requestedWidth = 0,
			int requestedHeight = 0) noexcept;
		[[nodiscard]] bool loadFromResource(
			HINSTANCE instance,
			LPCWSTR resourceType,
			LPCWSTR resourceName,
			int requestedWidth = 0,
			int requestedHeight = 0) noexcept;
		[[nodiscard]] bool savePng(std::wstring_view path) const noexcept;

	private:
		[[nodiscard]] bool create(int width, int height) noexcept;

		int width_ = 0;
		int height_ = 0;
		HDC dc_ = nullptr;
		HBITMAP bitmap_ = nullptr;
		HGDIOBJ previousObject_ = nullptr;
		Pixel* bits_ = nullptr;
	};
}
