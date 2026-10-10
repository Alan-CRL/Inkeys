#include "../Inkeys/Inkeys/Graphics/Surface.hpp"

#include <gdiplus.h>

#include <filesystem>
#include <iostream>
#include <utility>

int RunSurfaceTests()
{
	using Inkeys::Graphics::DibSurface;
	int failures = 0;
	auto check = [&](bool condition, const char* name)
	{
		if (!condition)
		{
			std::cerr << "FAILED surface: " << name << '\n';
			++failures;
		}
	};

	DibSurface original(16, 12);
	original.clear(0xFF332211u);
	DibSurface copied(original);
	check(copied.equals(original), "deep copy content");
	copied.pixels()[0] = 0;
	check(original.pixels()[0] == 0xFF332211u, "deep copy ownership");

	DibSurface moved(std::move(copied));
	check(copied.empty() && moved.width() == 16 && moved.height() == 12, "noexcept move");
	check(moved.resize(8, 6) && moved.width() == 8 && moved.height() == 6, "resize");
	check(!moved.resize(0, 6) && moved.width() == 8 && moved.height() == 6, "resize rollback");

	DibSurface source(4, 4);
	source.clear(0xFF224466u);
	DibSurface destination(4, 4);
	destination.clear();
	check(destination.composite(source) && destination.equals(source), "alpha composite");

	Gdiplus::GdiplusStartupInput startupInput;
	ULONG_PTR token = 0;
	check(Gdiplus::GdiplusStartup(&token, &startupInput, nullptr) == Gdiplus::Ok, "GDI+ startup");
	const auto pngPath = std::filesystem::temp_directory_path() / L"inkeys-dib-surface-test.png";
	check(source.savePng(pngPath.c_str()), "PNG save");
	DibSurface loaded;
	check(loaded.loadFromFile(pngPath.c_str()) && loaded.equals(source), "PNG load");
	DibSurface translucent(4, 4);
	translucent.clear(0x80402010u);
	check(translucent.savePng(pngPath.c_str()), "premultiplied PNG save");
	check(loaded.loadFromFile(pngPath.c_str()) && loaded.equals(translucent), "premultiplied PNG roundtrip");
	DibSurface unchanged(source);
	check(!unchanged.loadFromFile(L"Z:\\missing\\inkeys.png") && unchanged.equals(source), "load rollback");
	std::error_code removeError;
	std::filesystem::remove(pngPath, removeError);
	if (token)
		Gdiplus::GdiplusShutdown(token);

	const DWORD baseline = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
	for (int index = 0; index < 1000; ++index)
	{
		DibSurface stress(32, 32);
		check(stress.resize(64, 64), "stress resize");
	}
	const DWORD after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
	check(after <= baseline + 2, "GDI handles released");
	return failures;
}
