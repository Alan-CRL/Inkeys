module;
#include "../../../IdtMain.h"
#include <d2d1_1helper.h>
#include "../../../IdtState.h"
#include "../../../IdtConfiguration.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../Drawing/Draw3/Assets/EraserGripVisual.h"
#include "../../Drawing/Draw3/Draw3.Product.h"
#include "../../../resource.h"
#include <d3d11.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <crtdbg.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <span>
#include <thread>
#include <vector>
#include <bit>
#include <limits>
#include <cmath>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <optional>
#pragma comment(lib,"windowscodecs.lib")

#include <cstdint>
#include <cstddef>

module Inkeys.UI.Bar;
import :Main;
import :Scene;
import :Theme;
import Inkeys.UI.RenderPipeline;
import Inkeys.UI.PageControl;
import Inkeys.UI.Freeze;
import Inkeys.Other.Config;
import Inkeys.UI.Bar.LayoutModel;
import Inkeys.Text.Font;
import Inkeys.Conv.Text;
import Inkeys.UI.UI3.Material;
import Inkeys.UI.UI3.PresentationProbe;
import Inkeys.UI.Bar.MainLayoutPolicy;
import Inkeys.UI.StartupPlacement;

using namespace Inkeys::UI::UI3;

extern const double BarThicknessTooltipPadding;
extern const double BarThicknessTooltipCloseReserve;
extern const double BarThicknessTooltipTitleFontSize;
extern const double BarThicknessTooltipBodyFontSize;
extern const double BarThicknessTooltipLineGap;

namespace Inkeys::UI::Bar
{
	namespace
	{
		class MissingGlyphProbe final : public IDWriteTextRenderer
		{
		public:
			bool missing = false;
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override
			{
				if (!output) return E_POINTER;
				*output = nullptr;
				if (id != __uuidof(IUnknown) && id != __uuidof(IDWriteTextRenderer)
					&& id != __uuidof(IDWritePixelSnapping)) return E_NOINTERFACE;
				*output = static_cast<IDWriteTextRenderer*>(this); return S_OK;
			}
			ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
			ULONG STDMETHODCALLTYPE Release() override { return 1; }
			HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override
			{ *disabled = TRUE; return S_OK; }
			HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* matrix) override
			{ *matrix = { 1, 0, 0, 1, 0, 0 }; return S_OK; }
			HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixels) override
			{ *pixels = 1; return S_OK; }
			HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT, FLOAT, DWRITE_MEASURING_MODE,
				const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override
			{
				for (UINT32 index = 0; index < run->glyphCount; ++index)
					missing |= run->glyphIndices[index] == 0;
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override { return S_OK; }
			HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override { return S_OK; }
			HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override { return S_OK; }
		};

		class WhiteboardButtonStyleFixture : public BarButtonSetClass
		{
		public:
			using BarButtonSetClass::UpdateWhiteboardButtonStyle;
		};
		class DragCursorLightFixture : public BarUISetClass
		{
		public:
			void SeedCursor(POINT screenPoint, POINT layoutScreenOrigin)
			{
				// 仅在离屏 fixture 中种入真实输入快照，测试期间不发送任何新鼠标消息。
				std::lock_guard lock(borderCursorLightMutex);
				borderCursorLightScreenPoint = D2D1::Point2F(
					static_cast<FLOAT>(screenPoint.x), static_cast<FLOAT>(screenPoint.y));
				borderCursorLightPoint = D2D1::Point2F(
					static_cast<FLOAT>(screenPoint.x - layoutScreenOrigin.x),
					static_cast<FLOAT>(screenPoint.y - layoutScreenOrigin.y));
				borderCursorLightSerial = 41;
				borderCursorInputAvailable = true;
				borderCursorLightReady = true;
				borderCursorTrackingState = BarBorderCursorTrackingStateEnum::Inside;
			}
			void SetAvailability(bool available, bool ready = true, bool nearRegion = true)
			{
				std::lock_guard lock(borderCursorLightMutex);
				borderCursorInputAvailable = available; borderCursorLightReady = ready;
				borderCursorLightNearVisibleRegion = nearRegion;
				borderCursorTrackingState = !available ? BarBorderCursorTrackingStateEnum::Dormant
					: nearRegion ? BarBorderCursorTrackingStateEnum::Inside : BarBorderCursorTrackingStateEnum::Grace;
			}
			void SetTranslationDistractors(POINT live, POINT presented)
			{
				std::lock_guard lock(directWindowDragMutex);
				directWindowDragTranslationX = live.x; directWindowDragTranslationY = live.y;
				// 渲染已吸收目标位移时，最后成功窗口仍可能保留不为零的实际残差。
				RebaseBottomDockPresentedWindow(presented);
			}
			void Prepare(POINT layoutScreenOrigin)
			{
				(void)PrepareFrameLighting(1.0 / 60.0, layoutScreenOrigin, ResolveMainBarFrameLightingInput(this,
					static_cast<int>(StateModeSelectEnum::IdtSelection), 0, 0, 0));
			}
		};

		std::vector<unsigned char> ReadSvgProofPixels(ID2D1DeviceContext* context, ID2D1Bitmap1* source)
		{
			if (!context || !source) return {};
			const auto size = source->GetPixelSize();
			const std::uint64_t pixelsCount = std::uint64_t{size.width} * size.height;
			if (size.width == 0 || size.height == 0 || pixelsCount > 64 * 1024 * 1024 / 4) return {};
			const auto bytes = pixelsCount * 4;
			ComPtr<ID2D1Bitmap1> readable;
			const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, source->GetPixelFormat());
			if (FAILED(context->CreateBitmap(size, nullptr, 0, &props, &readable)) || FAILED(readable->CopyFromBitmap(nullptr, source, nullptr))) return {};
			D2D1_MAPPED_RECT mapped{};
			if (FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return {};
			std::vector<unsigned char> pixels(static_cast<std::size_t>(bytes));
			for (UINT y = 0; y < size.height; ++y) std::memcpy(pixels.data() + std::size_t{y} * size.width * 4, mapped.bits + std::size_t{y} * mapped.pitch, std::size_t{size.width} * 4);
			readable->Unmap(); return pixels;
		}
		std::array<unsigned char,4> ReadEraserTestPixel(ID2D1DeviceContext* context,ID2D1Bitmap1* source,UINT x,UINT y)
		{
			// 离屏几何可能越出目标；断言读取应返回失败像素，不能访问映射外内存。
			if (!context || !source) return {};
			const auto size = source->GetPixelSize();
			if (x >= size.width || y >= size.height) return {};
			ComPtr<ID2D1Bitmap1> readable;
			const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,source->GetPixelFormat());
			if(FAILED(context->CreateBitmap(source->GetPixelSize(),nullptr,0,&props,&readable)) || FAILED(readable->CopyFromBitmap(nullptr,source,nullptr)))return {};
			D2D1_MAPPED_RECT mapped{};if(FAILED(readable->Map(D2D1_MAP_OPTIONS_READ,&mapped)))return {};
			const auto* pixel=mapped.bits+y*mapped.pitch+x*4;std::array<unsigned char,4> result{pixel[0],pixel[1],pixel[2],pixel[3]};
			readable->Unmap();return result;
		}
		HRESULT SaveEraserTestPng(ID2D1DeviceContext* context,ID2D1Bitmap1* source,const std::filesystem::path& path)
		{
			ComPtr<ID2D1Bitmap1> readable;
			const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,source->GetPixelFormat());
			HRESULT hr=context->CreateBitmap(source->GetPixelSize(),nullptr,0,&props,&readable);if(FAILED(hr))return hr;
			hr=readable->CopyFromBitmap(nullptr,source,nullptr);if(FAILED(hr))return hr;
			D2D1_MAPPED_RECT map{};hr=readable->Map(D2D1_MAP_OPTIONS_READ,&map);if(FAILED(hr))return hr;
			ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;
			ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> options;
			hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
			if(SUCCEEDED(hr))hr=factory->CreateStream(&stream);
			if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE);
			if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
			if(SUCCEEDED(hr))hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
			if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,&options);
			if(SUCCEEDED(hr))hr=frame->Initialize(options.Get());
			const auto size=source->GetPixelSize();
			if(SUCCEEDED(hr))hr=frame->SetSize(size.width,size.height);
			WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
			if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);
			if(SUCCEEDED(hr))hr=frame->WritePixels(size.height,map.pitch,map.pitch*size.height,map.bits);
			if(SUCCEEDED(hr))hr=frame->Commit();if(SUCCEEDED(hr))hr=encoder->Commit();
			readable->Unmap();return hr;
		}
	}

	static void TestBarLayoutProjection(const std::function<void(bool, const char*)>& expect,
		const std::function<bool()>& frame)
	{
		auto& buttons = barUISet.barButtonSet;
		auto original = buttons.MainLayoutSnapshot();
		auto more = buttons.GetMoreButtonSnapshot();
		expect(more.forcedOverflow.empty() && more.explicitMore.size() == 3 &&
			more.explicitMore.front().get() == buttons.preset[(int)BarButtonPresetEnum::Setting],
			"O1 explicit More contains Settings and both visible copies only");
		if (more.explicitMore.size() != 3) return;
		auto first = more.explicitMore[1], second = more.explicitMore[2];
		expect(first->id == BarComponentId::Explorer && second->id == first->id && first != second &&
			first->state == &first->localState && second->state == &second->localState, "O1 independent More instances");
		barUISet.barState.moreExpanded = true;
		bool presented = true;
		for (int tick = 0; tick < 20 && presented; ++tick) presented = frame();
		expect(presented, "O1 expanded More frame presented");
		for (const auto& button : { first, second })
		{
			const auto bounds = Inkeys::UI::Bar::ResolveButtonHitSnapshot(*button,
				{ button->button.inhX, button->button.inhY }).boundsDip;
			const double zoom = barUISet.BottomDockPresentedSnapshot().zoom;
			const POINT center{ barUISet.BottomDockRigidVisualX(static_cast<int>(std::lround((bounds.left + bounds.right) * zoom / 2))),
				barUISet.BottomDockRigidVisualY(static_cast<int>(std::lround((bounds.top + bounds.bottom) * zoom / 2))) };
			auto hit = HitPresentedLayoutButtonForTest(center);
			expect(presented && hit == button, "O1 published More center hits its occurrence");
			if (hit != button) continue;
			SetButtonPressedVisual(*hit, true);
			expect(hit->localState.emph == WidgetEmphasize::Pressed &&
				(button == first ? second : first)->localState.emph != WidgetEmphasize::Pressed,
				"O1 coordinate hit presses only its own local state");
			SetButtonPressedVisual(*hit, false);
		}
		barUISet.barState.moreExpanded = false;
		SetButtonPressedVisual(*first, true);
		expect(first->state->emph == WidgetEmphasize::Pressed && second->state->emph != WidgetEmphasize::Pressed,
			"O1 press affects only its occurrence");
		SetButtonPressedVisual(*first, false);
		buttons.Load();
		expect(buttons.MainLayoutSnapshot() == original && buttons.GetMoreButtonSnapshot().explicitMore == more.explicitMore,
			"O2 reload preserves main fingerprint and More identities");
		for (auto size : { BarSizeMode::Compact, BarSizeMode::Large })
		{
			SetSizeMode(static_cast<int>(size));
			expect(frame() && AppliedSizeMode() == size, "O1 size transition rendered");
			auto layout = ResolveBarButtonListLayout(buttons.buttonList.Snapshot(), BarButtonListLayoutKind::Main, {}, size);
			std::vector<double> centers, targets;
			std::vector<std::string> ids;
			for (const auto& placement : layout.placements)
			{
				if (placement.button->id == BarButtonId::UserDivider)
				{
					centers.push_back(placement.x);
					targets.push_back(placement.button->button.x.tar);
					expect(placement.width == 1 && placement.height == (size == BarSizeMode::Large ? 50 : 28.5),
						"O1 actual divider dimensions");
				}
				if (IsExtensionBarButtonId(placement.button->id)) ids.push_back(placement.button->id);
			}
			expect(ids == std::vector<std::string>{ BarComponentId::Explorer, BarComponentId::Control,
				BarComponentId::Desktop, BarComponentId::Taskmgr }, "O1 four explicit main components, no hidden or missing plugin slot");
			expect(centers.size() == 2 && centers[1] - centers[0] == 6 && std::abs(targets[1] - targets[0]) == 6,
				"O1 real main frame separates consecutive divider centers");
		}
	}

	static void TestBarLayoutLifetime(const std::function<void(bool, const char*)>& expect,
		const std::function<bool()>& frame)
	{
		auto& buttons = barUISet.barButtonSet;
		auto model = buttons.ReadBarLayout();
		auto more = buttons.GetMoreButtonSnapshot().explicitMore;
		if (!model || more.size() != 3) { expect(false, "O2 lifetime fixture"); return; }
		auto visible = buttons.buttonList.Snapshot();
		for (const auto& item : more) visible.push_back(item);
		std::vector<shared_ptr<BarButtonClass>> copies;
		for (const auto& item : visible)
			if (item->id == BarComponentId::Explorer) copies.push_back(item);
		expect(copies.size() == 3, "O2 three registered Explorer clones");
		expect(I18n::load(1, L"JSON", L"en-US"), "O2 change locale");
		buttons.RefreshLocalizedText();
		auto* dc = barUISet.specTarget.DeviceContext();
		for (const auto& copy : copies)
		{
			expect(copy->name.content.GetTar() == IW(I18nKey.UI.Bar.Components.ExplorerLabel), "O2 localized cloned label");
			expect(dc && copy->icon.CacheBitmap(dc, 20, 20, barUISet.spec.GetDeviceGeneration()) && copy->icon.cacheBitmap,
				"O2 seed a real device cache per copy");
		}
		barUISet.specTarget.Discard();
		for (const auto& copy : copies) expect(!copy->icon.cacheBitmap, "O2 every clone discards bitmap");
		auto target = more.back();
		int actions = 0;
		target->clickFunc = [&] { ++actions; };
		expect(ReplayMoreLayoutPress(target, [] {}) && actions == 1, "O2 control press activates live target");
		auto next = RemoveAllOf(*model, BarComponentId::Explorer, buttons.GetLayoutCatalog());
		expect(ReplayMoreLayoutPress(target, [&]
		{
			expect(buttons.ApplyBarLayout(*model, next) == BarButtonSetClass::ApplyResult::Applied &&
				buttons.ApplyBarLayout(next, *model) == BarButtonSetClass::ApplyResult::Applied,
				"O2 remove and restore before owner consumes");
			expect(frame() && buttons.GetMoreButtonSnapshot().explicitMore == more, "O2 coalesced reload reuses pressed instance");
		}) && actions == 1, "O2 coalesced reload cancels press despite reused identity");
		auto final = next;
		final[Segment::More].clear();
		final[Segment::B1].push_back({ BarButtonId::Setting });
		expect(ReplayMoreLayoutPress(target, [&]
		{
			expect(buttons.ApplyBarLayout(*model, next) == BarButtonSetClass::ApplyResult::Applied,
				"O2 first save");
			expect(buttons.ApplyBarLayout(*model, final) == BarButtonSetClass::ApplyResult::Stale,
				"O2 stale editor rejected");
			expect(buttons.ApplyBarLayout(next, final) == BarButtonSetClass::ApplyResult::Applied,
				"O2 second save before frame");
			// 已发布的紧凑悬停提示也可能仍持有被删项。
			barUISet.hoverHintSource.main = target;
			barUISet.hoverHintSource.more = true;
			expect(frame() && !barUISet.hoverHintSource.main, "O2 owner consumes final layout and drops stale hover source");
		}) && actions == 1, "O2 retired pressed target cannot activate on frozen Up");
		target->clickFunc = {};
		// ApplyBarLayout 不写盘；生产由设置业务队列写出最新完整配置，这里直接执行同一写入。
		expect(config.Write(), "O2 queued full-config write");
		Config restarted;
		expect(restarted.ReadAll() && restarted.GetBarLayout() == ToConfig(final) &&
			buttons.ReadBarLayout() == final && !buttons.GetMoreButtonSnapshot().HasButtons(),
			"O2 final frame and disk agree with empty More");
		std::vector<std::string> actual;
		for (const auto& button : buttons.buttonList.Snapshot())
			if (IsExtensionBarButtonId(button->id) || button->id == BarButtonId::Setting) actual.push_back(button->id);
		expect(actual == std::vector<std::string>{ BarComponentId::Control, BarComponentId::Desktop,
			BarComponentId::Taskmgr, BarButtonId::Setting }, "O2 last frame moved singleton Settings to B1");
		std::weak_ptr<BarButtonClass> retired;
		for (const auto& button : buttons.buttonList.Snapshot())
			if (button->id == BarComponentId::Taskmgr) retired = button;
		visible.clear();
		auto withoutTaskmgr = RemoveAllOf(final, BarComponentId::Taskmgr, buttons.GetLayoutCatalog());
		// 重载后、新命中表发布前，旧帧坐标不得命中任何按钮（含地址不变的单例 Setting）。
		const auto presentedCenter = [](const shared_ptr<BarButtonClass>& button)
		{
			const auto bounds = Inkeys::UI::Bar::ResolveButtonHitSnapshot(*button,
				{ button->button.inhX, button->button.inhY }).boundsDip;
			const double zoom = barUISet.BottomDockPresentedSnapshot().zoom;
			return POINT{ barUISet.BottomDockRigidVisualX(static_cast<int>(std::lround((bounds.left + bounds.right) * zoom / 2))),
				barUISet.BottomDockRigidVisualY(static_cast<int>(std::lround((bounds.top + bounds.bottom) * zoom / 2))) };
		};
		shared_ptr<BarButtonClass> setting;
		for (const auto& button : buttons.buttonList.Snapshot())
			if (button->id == BarButtonId::Setting) setting = button;
		const POINT settingCenter = setting ? presentedCenter(setting) : POINT{};
		expect(setting && HitPresentedLayoutButtonForTest(settingCenter) == setting, "O2 presented Settings center hits before reload");
		const auto generation = buttons.LayoutGeneration();
		expect(buttons.ApplyBarLayout(final, withoutTaskmgr) == BarButtonSetClass::ApplyResult::Applied &&
			buttons.ConsumeLayoutReload() && !retired.expired(), "O2 retired instance survives before presentation");
		expect(buttons.LayoutGeneration() == generation + 1 && buttons.LayoutHitSuspended()
			&& !HitPresentedLayoutButtonForTest(settingCenter), "O2 stale rectangle rejected until replacement frame publishes");
		expect(frame() && retired.expired(), "O2 retired instance released after replacement frame");
		expect(!buttons.LayoutHitSuspended() && setting && HitPresentedLayoutButtonForTest(presentedCenter(setting)) == setting,
			"O2 hits resume after replacement frame");
		// 同值提交不重载；只清旧开关时写配置但不重载主栏。
		expect(buttons.ApplyBarLayout(withoutTaskmgr, withoutTaskmgr) == BarButtonSetClass::ApplyResult::Unchanged
			&& !buttons.ConsumeLayoutReload() && buttons.LayoutGeneration() == generation + 1, "O2 same-value apply neither reloads nor bumps generation");
		config.UI.Bar.ShortcutButton.Appliance.Explorer = true;
		expect(buttons.ApplyBarLayout(withoutTaskmgr, withoutTaskmgr, true) == BarButtonSetClass::ApplyResult::Applied
			&& !buttons.ConsumeLayoutReload() && config.GetLegacyBarComponents().empty(), "O2 legacy-only reset clears flags without reload");
		expect(buttons.ApplyBarLayout(withoutTaskmgr, final) == BarButtonSetClass::ApplyResult::Applied && frame(),
			"O2 restore final layout after retirement check");
		config.UI.Bar.IconOrder = 3;
		config.Experimental.Inkeys3.UI3.SizeMode = 1;
		config.UI.Bar.ShortcutButton.Appliance.Control = true;
		expect(buttons.ApplyBarLayout(final, Reset(), true) == BarButtonSetClass::ApplyResult::Applied && frame()
			&& config.Write() && restarted.ReadAll() && restarted.GetBarLayout() == Config{}.GetBarLayout()
			&& restarted.GetLegacyBarComponents().empty() && restarted.UI.Bar.IconOrder.load() == 3
			&& restarted.Experimental.Inkeys3.UI3.SizeMode.load() == 1, "H4 real reset retains preferences and clears flags");
	}

	static void TestBarLayoutRestart(const std::function<void(bool, const char*)>& expect)
	{
		namespace fs = std::filesystem;
		const auto root = fs::temp_directory_path() / (L"inkeys-bar-layout-" + std::to_wstring(GetCurrentProcessId())
			+ L"-" + std::to_wstring(GetTickCount64()));
		if (!fs::create_directory(root)) { expect(false, "unique private layout directory"); return; }
		struct Restore
		{
			fs::path root;
			std::wstring originalPath;
			Config original;
			~Restore()
			{
				config = original;
				globalPath = originalPath;
				std::error_code ignored;
				fs::remove_all(root, ignored);
			}
		} restore{ root, globalPath };
		restore.original = config;
		config = Config{};
		globalPath = root.wstring() + L"\\";
		fs::create_directories(root / L"Inkeys/Config");
		const fs::path file = config.GetFilePath();
		{
			BarButtonSetClass startup;
			startup.PresetInitialization();
			startup.InitializeBarLayout();
			expect(!fs::exists(file), "H5 factory layout startup performs no write");
		}
		config.UI.Bar.ShortcutButton.Appliance.Explorer = true;
		config.UI.Bar.ShortcutButton.Appliance.Control = true;
		config.UI.Bar.ShortcutButton.System.Desktop = true;
		{
			BarButtonSetClass startup;
			startup.PresetInitialization();
			// 缺少注册资源仍须导入。把配置路径暂时放在普通文件下面，强制首次保存失败。
			std::ofstream(root / L"blocked").put('x');
			globalPath = (root / L"blocked").wstring() + L"\\";
			startup.InitializeBarLayout();
			auto model = startup.ReadBarLayout();
			expect(model && (*model)[Segment::B1] == BarLayoutEntries{ { BarComponentId::Explorer }, { BarComponentId::Control } }
				&& (*model)[Segment::More] == BarLayoutEntries{ { BarButtonId::Setting }, { BarComponentId::Desktop } }
				&& config.GetLegacyBarComponents().empty(), "H5 failed save retains complete memory import");
			globalPath = root.wstring() + L"\\";
			if (!model) return;
			// 启动保存失败不留待写状态：同值提交无变化，之后任意一次完整配置写入即带上已导入的布局。
			expect(startup.ApplyBarLayout(*model, *model) == BarButtonSetClass::ApplyResult::Unchanged, "H5 same-value apply after failed startup save");
			expect(config.Write(), "H5 later full-config write persists memory import");
		}
		Config reopened;
		expect(reopened.ReadAll() && reopened.GetBarLayout() == config.GetBarLayout() &&
			reopened.GetLegacyBarComponents().empty(), "H5 persisted retry reopens");
		config = reopened;
		{
			BarButtonSetClass startup;
			startup.PresetInitialization();
			auto before = config.GetBarLayout();
			if (!fs::is_regular_file(file)) { expect(false, "retry created private main.json"); return; }
			auto time = fs::last_write_time(file);
			startup.InitializeBarLayout();
			expect(config.GetBarLayout() == before && fs::last_write_time(file) == time, "H5 second startup neither appends nor saves");
		}
		auto model = Reset();
		model[Segment::B1] = { { BarComponentId::Explorer }, { BarButtonId::UserDivider, BarButtonSizeKind::OneTwo },
			{ BarButtonId::UserDivider, BarButtonSizeKind::OneTwo }, { BarComponentId::Control }, { BarComponentId::Desktop },
			{ BarComponentId::Taskmgr }, { "absent.plugin" }, { BarComponentId::Control, BarButtonSizeKind::TwoTwo, false } };
		model[Segment::More].push_back({ BarComponentId::Explorer });
		model[Segment::More].push_back({ BarComponentId::Explorer });
		model[Segment::More].push_back({ BarComponentId::Control, BarButtonSizeKind::TwoTwo, false });
		config.ReplaceBarLayout(ToConfig(model), true);
		expect(config.Write() && reopened.ReadAll() && reopened.GetBarLayout() == ToConfig(model), "O1 disk fixture roundtrip");
		config = reopened;
		BarGoldenMainSceneHost host;
		host.settled = [&](const auto& frame)
		{
			barUISet.barButtonSet.InitializeBarLayout();
			TestBarLayoutProjection(expect, frame);
			TestBarLayoutLifetime(expect, frame);
			return true;
		};
		expect(RunGoldenMainSceneForHost(false, true, 1, host) == S_OK, "O1/O2 real offscreen frame host settles");
	}

	int RunEraserAttributeOffscreenTest()
	{
		// 此命令只初始化共享UI设备并离屏绘制，不创建或显示任何HWND。
		SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
		_CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
		const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
		if(FAILED(com))return 2;
		if(FAILED(RenderPipeline::Initialize())){CoUninitialize();return 3;}
		IdtFontFileLoader::IsLoaderInitialized();IdtFontCollectionLoader::IsLoaderInitialized();
		const std::array<UINT,8> fonts{IDR_TTF1,IDR_TTF7,IDR_TTF3,IDR_TTF8,
			IDR_NOTO_JP_REGULAR,IDR_NOTO_JP_BOLD,IDR_NOTO_KR_REGULAR,IDR_NOTO_KR_BOLD};
		const HRESULT fontCollectionHr=RenderPipeline::InitializeFontCollection(
			IdtFontFileLoader::GetLoader(),IdtFontCollectionLoader::GetLoader(),fonts);
		std::filesystem::create_directories(L"Build/eraser-b");
		std::ofstream report("Build/eraser-b/offscreen-results.log");
		int failures=0;
		auto expect=[&](bool ok,const char* why){if(!ok){++failures;report<<"[EraserVisual] FAIL "<<why<<'\n';}};
		expect(SUCCEEDED(fontCollectionHr),"SC/TC font collection initializes");
		{
			// 使用真实主栏 Shape 绘制通道验证 cap 与收展显隐独立，半收起不能仍停在30%。
			UIRendering renderer; FrameTarget frameTarget{renderer};
			const bool ready=SUCCEEDED(frameTarget.Ensure(RenderPipeline::GetDeviceEpoch(),80,40));
			expect(ready,"idle main body owns a real offscreen renderer");
			if(ready){
				UiShapeClass body;body.Initialization(40,20,80,40,0,0,0,RGB(30,30,30),std::nullopt);
				body.enable.Initialization(true);body.themeSurface=true;body.residentSurface=true;
				body.lightFillOpacityScale=1.10;
				const auto draw=[&](double pct,double idle){
					body.pct.SetDirect(pct);renderer.SetFrameZoom(1);renderer.SetResidentIdleFade(idle);
					auto* dc=frameTarget.DeviceContext();dc->SetDpi(96,96);dc->SetTransform(D2D1::IdentityMatrix());
					frameTarget.BeginFrame(dc,D2D1::RectF(0,0,80,40));
					const bool drew=renderer.Shape(dc,body,{0,0});frameTarget.FinishDrawCommands(dc);
					expect((drew||pct==0)&&SUCCEEDED(frameTarget.EndFrame(dc)),"idle main body draws through production Shape");
					return static_cast<int>(ReadEraserTestPixel(dc,frameTarget.TargetBitmap(),40,20)[3]);
				};
				for(double light:{0.0,1.0}){
					body.lightMaterial=light;
					for(double pct:{.8,.6,.4,.2,.0}){
						const int actual=draw(pct,1);
						expect(std::abs(actual-static_cast<int>(std::lround(255*.3*pct/.8)))<=2,
							"idle body shrink preserves independent visibility through every intermediate frame");
					}
					const int full=draw(.8,0),half=draw(.4,0);
					expect(std::abs(half*2-full)<=2,"normal body visibility is unchanged when idle is off");
				}
				renderer.SetResidentIdleFade(0);frameTarget.Discard();
			}
		}
		// screenPoint/serial 保持不变，验证直移吸收和显示原点变更不依赖下一次 WM_MOUSEMOVE。
		{
			const bool savedEdge = BarUiEdgeLightingEnabled;
			const bool savedDynamic = BarUiDynamicEdgeLightingEnabled;
			const double savedSpeed = UiAnimationSpeedRate;
			const bool savedAnimation = UiAnimationEnabled;
			BarUiEdgeLightingEnabled = true; BarUiDynamicEdgeLightingEnabled = true;
			UiAnimationSpeedRate = 1.0;
			UiAnimationEnabled = true;
			struct RebaseCase { POINT screen, beforeOrigin, afterOrigin; };
			const std::array cases{
				RebaseCase{{900,450},{300,0},{0,0}},
				RebaseCase{{-1240,400},{-2220,-180},{-1920,-360}},
				RebaseCase{{-1200,500},{1510,-890},{-1920,400}},
				RebaseCase{{-1250,260},{-1590,-135},{-1920,-300}},
				RebaseCase{{-1250,260},{-1590,-135},{-1960,-325}},
				RebaseCase{{450,300},{-100,120},{-100,120}}};
			for(const double zoom:{0.6,1.0,1.5,2.0})for(const auto& item:cases)
			{
				auto fixture = std::make_unique<DragCursorLightFixture>();
				fixture->spec.SetFrameZoom(zoom); fixture->SeedCursor(item.screen,item.beforeOrigin);
				for(int frame=0;frame<90;++frame)fixture->Prepare(item.beforeOrigin);
				const auto before = fixture->spec.SnapshotFrameLighting();
				const auto beforeBounds = fixture->lightingStateMachine.GetFrameCursorLightDamageBounds();
				const auto atScreen = [&](const UiFrameLightingSnapshot& light,POINT origin)
				{
					return std::abs(light.cursorLight.x + origin.x - item.screen.x)<0.01F
						&& std::abs(light.cursorLight.y + origin.y - item.screen.y)<0.01F;
				};
				expect(atScreen(before,item.beforeOrigin) && before.cursorLightVisible
					&& std::abs(before.cursorIntensity-1.0F)<0.0001F,
					"drag cursor baseline reaches full strength in the original layout domain");
				// 同帧捕获值故意与 live/最后成功呈现残差不同，不能在 Prepare 中重新取后两者。
				fixture->SetTranslationDistractors({730,-510},{-40,-25});
				fixture->Prepare(item.afterOrigin);
				const auto after = fixture->spec.SnapshotFrameLighting();
				const auto afterBounds = fixture->lightingStateMachine.GetFrameCursorLightDamageBounds();
				expect(atScreen(after,item.afterOrigin),
					"drag release rebases real Prepare cursor light without a new input serial");
				const bool originChanged = item.beforeOrigin.x!=item.afterOrigin.x || item.beforeOrigin.y!=item.afterOrigin.y;
				expect(fixture->lightingStateMachine.DidFrameCursorLightChange()==originChanged
					&& ((beforeBounds.left!=afterBounds.left || beforeBounds.top!=afterBounds.top)==originChanged)
					&& afterBounds.left<=after.cursorLight.x-after.cursorRadius
					&& afterBounds.right>=after.cursorLight.x+after.cursorRadius
					&& afterBounds.top<=after.cursorLight.y-after.cursorRadius
					&& afterBounds.bottom>=after.cursorLight.y+after.cursorRadius,
					"drag release local light change invalidates complete old and new damage bounds");
				const auto surfaceLight = [&](const UiFrameLightingSnapshot& light)
				{
					SurfaceSharedLighting shared;
					shared.cursorScreenX=light.cursorScreenLight.x;shared.cursorScreenY=light.cursorScreenLight.y;
					shared.cursorRadiusPixels=light.cursorRadius;shared.cursorIntensity=light.cursorIntensity;
					shared.cursorVisible=light.cursorLightVisible;shared.edgeLightingEnabled=light.edgeLightingEnabled;
					return ResolveSurfaceFrameLightingSnapshot(shared,{-2000,-500,-1600,-300},12);
				};
				const auto beforeSurface=surfaceLight(before),afterSurface=surfaceLight(after);
				expect(beforeSurface.cursorLight.x==afterSurface.cursorLight.x
					&& beforeSurface.cursorLight.y==afterSurface.cursorLight.y
					&& beforeSurface.cursorRadius==afterSurface.cursorRadius
					&& beforeSurface.cursorIntensity==afterSurface.cursorIntensity
					&& beforeSurface.cursorLightVisible==afterSurface.cursorLightVisible,
					"drag rebase keeps the real PageControl screen-light mapping continuous");
				bool stable = true;
				for(int frame=0;frame<120;++frame)
				{
					fixture->Prepare(item.afterOrigin);
					const auto light = fixture->spec.SnapshotFrameLighting();
					stable &= atScreen(light,item.afterOrigin) && light.cursorLightVisible
						&& std::abs(light.cursorIntensity-before.cursorIntensity)<0.0001F
						&& std::abs(light.cursorRadius-240.0*zoom)<0.001
						&& light.cursorScreenLight.x==before.cursorScreenLight.x
						&& light.cursorScreenLight.y==before.cursorScreenLight.y
						&& !fixture->lightingStateMachine.DidFrameCursorLightChange();
				}
				expect(stable,"stationary release stays continuous for 120 frames at each zoom");
				report<<"[DragCursorLight] zoom="<<zoom<<" origin="<<item.beforeOrigin.x<<','<<item.beforeOrigin.y
					<<" -> "<<item.afterOrigin.x<<','<<item.afterOrigin.y<<" local="<<before.cursorLight.x<<','<<before.cursorLight.y
					<<" -> "<<after.cursorLight.x<<','<<after.cursorLight.y<<" stable="<<stable<<'\n';
			}
			// 独立调用真实 Prepare/Shape，只保留第三光；用屏幕几何对齐同一边框像素。
			for(const double zoom:{0.6,1.0,1.5,2.0})
			{
				auto fixture=std::make_unique<DragCursorLightFixture>();
				fixture->spec.SetFrameZoom(zoom);fixture->SeedCursor({900,450},{300,0});
				for(int frame=0;frame<90;++frame)fixture->Prepare({300,0});
				expect(SUCCEEDED(fixture->specTarget.Ensure(RenderPipeline::GetDeviceEpoch(),512,512)),
					"drag third-light pixel target initializes");
				auto* dc=fixture->specTarget.DeviceContext();
				if(!dc)continue;
				UiShapeClass border(0,0,160,60,12,12,2,std::nullopt,RGB(255,255,255));
				border.enable.Initialization(true);border.pct.SetDirect(1);
				border.framePct.emplace(0);border.frameLightPct.emplace(1);
				border.frameRendering=UiFrameRenderingEnum::PointLight; border.framePrimaryLightEnabled=false;
				const auto draw=[&](POINT origin)
				{
					const auto light=fixture->spec.SnapshotFrameLighting();
					fixture->spec.SetFrameCursorLightLocalGeometry(light.cursorLight,D2D1::SizeF(light.cursorRadius,light.cursorRadius));
					// 几何从固定 screenPoint 派生，不能用待验证的局部光源反推形状而掩盖错误。
					const double left=(900.0-origin.x)/zoom-80.0,top=(450.0-origin.y)/zoom-20.0;
					dc->BeginDraw();dc->SetTransform(D2D1::Matrix3x2F::Translation(
						static_cast<FLOAT>(64.0-left*zoom),static_cast<FLOAT>(64.0-top*zoom)));
					dc->Clear(D2D1::ColorF(0,0,0,0));
					const bool drew=fixture->spec.Shape(dc,border,UiInheritClass(left,top));
					const HRESULT hr=dc->EndDraw();fixture->spec.HandleFrameEndDrawResult(hr);
					expect(drew && SUCCEEDED(hr),"real third-light border renders across drag release");
					return ReadSvgProofPixels(dc,fixture->specTarget.TargetBitmap());
				};
				const auto before=draw({300,0});
				fixture->SetTranslationDistractors({730,-510},{-40,-25});fixture->Prepare({0,0});
				const auto after=draw({0,0});
				unsigned maxDelta=0,lit=0;
				if(before.size()==after.size())for(size_t i=0;i<before.size();++i)
				{
					maxDelta=std::max(maxDelta,static_cast<unsigned>(std::abs(static_cast<int>(before[i])-after[i])));
					lit+=before[i]>0;
				}
				expect(before.size()==512*512*4 && after.size()==before.size() && lit>100 && maxDelta<=2,
					"isolated real third-light pixels remain continuous after absorption");
				fixture->SetAvailability(false);
				for(int frame=0;frame<90;++frame)fixture->Prepare({0,0});
				const auto withoutCursor=draw({0,0});
				const auto darkBytes=std::count_if(withoutCursor.begin(),withoutCursor.end(),[](unsigned char pixel){return pixel>0;});
				expect(withoutCursor.size()==after.size() && darkBytes==0 && lit>100,
					"third-light-off control is transparent while the isolated enabled border contributes pixels");
				report<<"[DragCursorPixels] zoom="<<zoom<<" lit="<<lit<<" maxDelta="<<maxDelta<<" offLit="<<darkBytes<<'\n';
				fixture->specTarget.Discard();
			}
			// 保持既有 Prepare 门禁；这里只检验生产光照帧，不启动 Raw Input/timer/HWND 状态机。
			{
				auto fixture=std::make_unique<DragCursorLightFixture>();
				fixture->spec.SetFrameZoom(1);fixture->SeedCursor({900,450},{300,0});
				const auto settle=[&](POINT origin){for(int frame=0;frame<90;++frame)fixture->Prepare(origin);};
				settle({300,0});const auto inside=fixture->spec.SnapshotFrameLighting();
				fixture->SetAvailability(true,true,false);fixture->Prepare({0,0});
				const auto grace=fixture->spec.SnapshotFrameLighting();
				expect(grace.cursorLightVisible && grace.cursorIntensity==inside.cursorIntensity,
					"Grace outside the accepting region does not change third-light strength at release");
				fixture->SetAvailability(false);settle({0,0});
				expect(!fixture->spec.SnapshotFrameLighting().cursorLightVisible,"Dormant input fades the third light without new input serial");
				fixture->SetAvailability(true,false);settle({0,0});
				expect(!fixture->spec.SnapshotFrameLighting().cursorLightVisible,"unready cursor input remains hidden across a rebase");
				fixture->SetAvailability(true);settle({0,0});
				BarUiDynamicEdgeLightingEnabled=false;settle({-40,-25});
				expect(!fixture->spec.SnapshotFrameLighting().cursorLightVisible,"dynamic-light gate preserves its fade-out behavior");
				BarUiDynamicEdgeLightingEnabled=true;settle({-40,-25});
				UiAnimationEnabled=false;fixture->Prepare({0,0});
				expect(!fixture->spec.SnapshotFrameLighting().cursorLightVisible,"animation gate immediately hides third light during a rebase");
				UiAnimationEnabled=true;settle({0,0});
				BarUiEdgeLightingEnabled=false;fixture->Prepare({300,0});
				expect(!fixture->spec.SnapshotFrameLighting().edgeLightingEnabled
					&& !fixture->spec.SnapshotFrameLighting().cursorLightVisible,"total-light gate hides all light during a rebase");
			}
			UiAnimationSpeedRate=savedSpeed;
			BarUiEdgeLightingEnabled = savedEdge; BarUiDynamicEdgeLightingEnabled = savedDynamic;
			UiAnimationEnabled = savedAnimation;
		}
		auto& owner=barUISet;
		expect(I18n::load(1,L"JSON",L"zh-CN"),"default bundled translation loads");
		owner.ConfigureLocalizedTypography();
		owner.barButtonSet.PresetInitialization();owner.barMedia.LoadFormat();
		// 同一生产 Word 路径对照独立推导的整数字号，不用截图观感代替最终像素尺寸验证。
		{
			// 独立绘制器配对产品预览缓存，仍验证真实字体解析与 DWrite 布局。
			struct FontTestRenderer : UIRendering
			{
				using UIRendering::UIRendering;
				BarPenPreviewRenderer preview;
					FrameTarget target{*this};
				void DiscardDeviceResources()
				{ preview.ResetDeviceCaches(); target.Discard(); }
				auto* GetThicknessFineDialLabelLayout(int value, FLOAT zoom, ID2D1DeviceContext* context)
				{ return preview.GetThicknessFineDialLabelLayout(*this, value, zoom, context); }
				bool UpdateFontRenderingPolicy(bool allow) noexcept
				{
					if (!UIRendering::UpdateFontRenderingPolicy(allow)) return false;
					preview.ResetTypographyCaches(); return true;
				}
			};
			FontTestRenderer fonts(MakeMainBarRenderingServices(&owner)), reference(MakeMainBarRenderingServices(&owner));
			const bool ready = SUCCEEDED(fonts.target.Ensure(RenderPipeline::GetDeviceEpoch(), 256, 128))
				&& SUCCEEDED(reference.target.Ensure(RenderPipeline::GetDeviceEpoch(), 256, 128));
			expect(ready, "font pixel policy creates offscreen targets");
			if (ready)
			{
				struct FontCase { FLOAT dpi, scaleX, scaleY; bool pixels; };
				for (const auto item : {FontCase{96,1,1,false}, FontCase{144,1.03F,0.91F,false},
					FontCase{192,0.95F,0.83F,false}, FontCase{192,1.03F,0.91F,true}})
				{
					constexpr double zoom = 0.6, contentScale = 0.85;
					const double deviceScale = item.scaleY * (item.pixels ? 1.0 : item.dpi / 96.0);
					const double expectedPixels = std::floor(13.0 * zoom * deviceScale);
					const double expectedSize = expectedPixels / deviceScale * contentScale;
					const auto draw = [&](UIRendering& renderer, FrameTarget& frameTarget, bool allow, double logicalSize)
					{
						renderer.UpdateFontRenderingPolicy(allow); renderer.SetFrameZoom(zoom);
						auto* dc = frameTarget.DeviceContext(); dc->SetDpi(item.dpi, item.dpi);
						dc->SetUnitMode(item.pixels ? D2D1_UNIT_MODE_PIXELS : D2D1_UNIT_MODE_DIPS);
						dc->SetTransform(D2D1::IdentityMatrix());
						const float units = item.pixels ? 1.0F : 96.0F / item.dpi;
						frameTarget.BeginFrame(dc, D2D1::RectF(0,0,256 * units,128 * units));
						dc->SetTransform(D2D1::Matrix3x2F::Scale(item.scaleX,item.scaleY)
							* D2D1::Matrix3x2F::Translation(3.25F,2.75F));
						UiWordClass word(0,0,180,70,L"字号 123",logicalSize,RGB(20,20,20));
						word.enable.Initialization(true); word.pct.SetDirect(1); word.contentScale=contentScale;
						const bool drew = renderer.Word(dc,word,{10,10},DWRITE_FONT_WEIGHT_NORMAL);
						frameTarget.FinishDrawCommands(dc); dc->SetTransform(D2D1::IdentityMatrix());
						expect(drew && SUCCEEDED(frameTarget.EndFrame(dc)), "font Word renders through the real D2D path");
						return ReadSvgProofPixels(dc,frameTarget.TargetBitmap());
					};
					const auto actual=draw(fonts,fonts.target,false,13.0);
					const auto expected=draw(reference,reference.target,true,expectedSize/(zoom*contentScale));
					expect(!actual.empty() && actual==expected,
						"Word pixels retain continuous content scale around the projected stationary target");
					const auto fractional=draw(fonts,fonts.target,true,13.0);
					expect(fractional!=actual, "fractional preference preserves a distinct untruncated Word render");
					expect(draw(fonts,fonts.target,false,13.0)==actual, "font preference switches back without stale cached text");
				}
				auto* dc=fonts.target.DeviceContext(); dc->SetDpi(96,96); dc->SetUnitMode(D2D1_UNIT_MODE_DIPS);
				dc->SetTransform(D2D1::IdentityMatrix()); fonts.UpdateFontRenderingPolicy(false);
				auto* label=fonts.GetThicknessFineDialLabelLayout(15,0.63F,dc);
				expect(label && label->layout && static_cast<IDWriteTextFormat*>(label->layout.Get())->GetFontSize()==6.0F,
					"fine dial uses truncated pixels in its actual DWrite layout");
				expect(label==fonts.GetThicknessFineDialLabelLayout(15,0.63F,dc),
					"fine dial reuses the same effective font layout");
				dc->SetTransform(D2D1::Matrix3x2F::Scale(1.03F,0.91F));
				auto* scaled=fonts.GetThicknessFineDialLabelLayout(15,0.63F,dc);
				expect(scaled && scaled->layout
					&& std::abs(static_cast<IDWriteTextFormat*>(scaled->layout.Get())->GetFontSize()*0.91-5.0)<0.00001,
					"fine dial cache keys include final font size after external transform");
				fonts.UpdateFontRenderingPolicy(true);
				label=fonts.GetThicknessFineDialLabelLayout(15,0.63F,dc);
				expect(label && std::abs(static_cast<IDWriteTextFormat*>(label->layout.Get())->GetFontSize()-6.3F)<0.00001F,
					"fine dial refreshes layout when fractional fonts are enabled");
				fonts.UpdateFontRenderingPolicy(false);
				expect(fonts.ResolveFontSize(dc,0.5)==0.0F, "subpixel font is skipped rather than enlarged");
				dc->SetDpi(120,192); dc->SetTransform(D2D1::Matrix3x2F(1,0,0.6F,0.8F,0,0));
				const double rotatedScale=std::hypot(0.6*1.25,0.8*2.0);
				expect(std::abs(fonts.ResolveFontSize(dc,7.8)*rotatedScale-std::floor(7.8*rotatedScale))<0.00001,
					"font height includes both axes of rotated/sheared DPI mapping");

				// 真实公共按钮绘制覆盖按压目标和 FLOAT 当前矩阵抵消曾导致的 13px->12px 边界。
				{
					dc->SetDpi(96,96);dc->SetUnitMode(D2D1_UNIT_MODE_DIPS);dc->SetTransform(D2D1::IdentityMatrix());
					fonts.SetFrameZoom(1);fonts.UpdateFontRenderingPolicy(false);
					BarButtonClass button;
					button.button.Initialization(0,0,80,80,4,4,1,RGB(240,240,240),RGB(20,20,20));
					button.button.enable.Initialization(true);button.button.pct.SetDirect(1);
					button.name.Initialization(0,0,70,24,L"Press",13,RGB(20,20,20));
					button.name.enable.Initialization(true);button.name.pct.SetDirect(1);
					ButtonDrawOptions options;options.drawIcon=false;
					auto time=std::chrono::steady_clock::now();
					const auto drawButton=[&](double dt){
						time+=std::chrono::nanoseconds(1)+std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(dt));
						fonts.BeginFontFrame(time,{dt,1,true,false},1);fonts.SetFontTargetScale(1);
						dc->SetTransform(D2D1::IdentityMatrix());fonts.target.BeginFrame(dc,D2D1::RectF(0,0,256,128));
						expect(DrawButtonVisual(fonts,dc,button,{10,10},options),"real font press button draws");
						const double first=button.name.fontCorrection.Current();
						(void)DrawButtonVisual(fonts,dc,button,{10,10},options);
						expect(button.name.fontCorrection.Current()==first,"duplicate real button draw advances font once");
						fonts.target.FinishDrawCommands(dc);expect(SUCCEEDED(fonts.target.EndFrame(dc)),"font press frame commits D2D commands");
						return 13.0*static_cast<double>(button.pressScale.val)*first;
					};
					for(const double current:{1.0,0.95,0.96,0.99,1.01,0.9,1.0}){
						button.pressScale.SetDirect(current);button.pressScale.SetTar(1,0.2);(void)drawButton(0);
						expect(button.name.fontCorrection.Target()==1.0,"release target stays exactly 13px across FLOAT press matrices");
					}
					button.pressScale.SetDirect(1);button.pressScale.SetTar(0.95,0.2);
					double previous=13;int intermediate=0;
					for(int tick=0;tick<30;++tick){
						if(!button.pressScale.IsSame())(void)UiAdvanceAnimation(button.pressScale,{0.01,1,true,false});
						const double pixel=drawButton(0.01);intermediate+=std::abs(pixel-std::round(pixel))>0.00001;
						expect(std::abs(pixel-previous)<0.2,"real pressed font follows a continuous fractional trajectory");previous=pixel;
					}
					expect(intermediate>10&&std::abs(previous-12.0)<0.00001&&!fonts.FontAnimationsActive(),
						"pressed hold settles at floor(13*0.95) and font scheduler becomes idle");
					const double held=previous;button.pressScale.SetTar(1,0.2);
					expect(std::abs(drawButton(0)-held)<0.00001,"release retarget at zero dt preserves current visible font");
					for(int tick=0;tick<30;++tick){
						if(!button.pressScale.IsSame())(void)UiAdvanceAnimation(button.pressScale,{0.01,1,true,false});
						previous=drawButton(0.01);
					}
					expect(std::abs(previous-13.0)<0.00001&&std::abs(drawButton(0)-previous)<0.00001,
						"real release ends at 13px without an idle-frame floor jump");
					// 字号目标不变时，内容过渡可连续经过小数及 Back 回弹；末尾只回到同一个 c。
					button.name.contentScale=0.83;const double c=button.name.fontCorrection.Current();(void)drawButton(0.01);
					expect(button.name.fontCorrection.Current()==c,"content midpoint does not retarget stationary font correction");
					button.name.contentScale=1.03;(void)drawButton(0.01);button.name.contentScale=1;(void)drawButton(0.01);
					expect(button.name.fontCorrection.Current()==c,"content overshoot and final frame keep continuous correction");
				}


				// 更多的派生尺寸逐帧变化，但文字必须从首帧就使用最终字号目标。
				for (const auto kind : {ButtonVisualLayoutKind::StandardTwoTwo,
					ButtonVisualLayoutKind::StandardTwoOne})
				for (const double zoom : {1.0, 0.6})
				for (const bool allowFractional : {false, true})
				{
					const double nominal=ResolveButtonVisualMetrics(kind).primaryFontSizeDip;
					const double expected=allowFractional?nominal*zoom:std::floor(nominal*zoom);
					const double duration=UiDefaultOperationDur,dt=duration/32.0;
					fonts.SetFrameZoom(zoom);fonts.UpdateFontRenderingPolicy(allowFractional);
					dc->SetDpi(96,96);dc->SetUnitMode(D2D1_UNIT_MODE_DIPS);dc->SetTransform(D2D1::IdentityMatrix());
					BarButtonClass button;
					button.button.Initialization(0,0,70,70,4,4,1,RGB(240,240,240),RGB(20,20,20));
					button.button.enable.Initialization(true);button.button.pct.SetDirect(1);
					button.name.Initialization(0,0,70,24,kind==ButtonVisualLayoutKind::StandardTwoTwo?L"设置":L"组件",nominal,RGB(20,20,20));
					button.name.enable.Initialization(true);button.name.pct.SetDirect(1);
					ButtonDrawOptions options;options.drawIcon=false;
					auto time=std::chrono::steady_clock::now();
					const auto drawFrame=[&](double elapsed){
						time+=std::chrono::nanoseconds(1)+std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(elapsed));
						fonts.BeginFontFrame(time,{elapsed,1,true,false},zoom);fonts.SetFontTargetScale(1);
						dc->SetTransform(D2D1::IdentityMatrix());fonts.target.BeginFrame(dc,D2D1::RectF(0,0,256,128));
						expect(DrawButtonVisual(fonts,dc,button,{10,10},options),"More label draws through the production button path");
						const double size=fonts.ResolveWordFontSize(dc,button.name,button.name.size.val*zoom);
						fonts.target.FinishDrawCommands(dc);expect(SUCCEEDED(fonts.target.EndFrame(dc)),"More font frame commits D2D commands");
						return size;
					};
					UiValueClass scale{0.01};
					const UiCurveSpecClass opening{UiCurveEnum::EaseOutBack,UiCurveEnum::EaseOutBack,0,false};
					const UiCurveSpecClass closing{UiCurveEnum::EaseInBack,UiCurveEnum::EaseInBack,0,false};
					// 与生产 PositionContentMore 一致：字号取整目标只取展开态倍率 1，收起端紧凑倍率不参与取整。
					const auto moreFrame=[&](double elapsed){
						ConfigureButtonVisualTargets(button,kind,duration,scale.val,true,std::nullopt,1.0);
						return drawFrame(elapsed);
					};
					const auto proportional=[&](double size){
						return std::abs(size-expected*static_cast<double>(scale.val))<0.00001;
					};
					// 隐藏布局不消费字体修正；覆盖首次可见尺寸尚不足 1px 的边界。
					ConfigureButtonVisualTargets(button,kind,duration,0.01,true,std::nullopt,1.0);
					scale.SetTar(1,duration,std::nullopt,false,opening);
					expect(std::abs(moreFrame(0)-expected*0.01)<0.00001,"first visible More label projects the final size even below one pixel");
					bool overshoot=false;
					double settled=0;
					for(int tick=0;tick<36;++tick){
						if(!scale.IsSame())(void)UiAdvanceAnimation(scale,{dt,1,true,false});
						overshoot|=scale.val>1;settled=moreFrame(dt);
						expect(std::abs(settled-expected*static_cast<double>(scale.val))<0.00001,
							"More opening retains its raw scale curve without a second font-growth animation");
					}
					expect(overshoot&&scale.IsSame()&&!fonts.FontAnimationsActive()
						&&std::abs(settled-expected)<0.00001&&moreFrame(0)==settled,
						"More opening and overshoot settle at the final font size with no idle-frame growth");
					scale.SetTar(0.16,duration,std::nullopt,false,closing);
					for(int tick=0;tick<8;++tick){(void)UiAdvanceAnimation(scale,{dt,1,true,false});
						expect(proportional(moreFrame(dt)),"More closing keeps labels proportional to the panel scale");}
					const double interrupted=moreFrame(0);
					scale.SetTar(1,duration,std::nullopt,false,opening);
					expect(std::abs(moreFrame(0)-interrupted)<0.00001,"reopening More at zero dt preserves the current label size");
					for(int tick=0;tick<36;++tick){
						if(!scale.IsSame())(void)UiAdvanceAnimation(scale,{dt,1,true,false});
						settled=moreFrame(dt);
						expect(proportional(settled),"reversed More opening keeps labels proportional to the panel scale");
					}
					expect(!fonts.FontAnimationsActive()&&std::abs(settled-expected)<0.00001&&moreFrame(0)==settled,
						"reversed More opening has no font growth after the panel settles");
					// 完整收起到紧凑态再重开：修正系数不追赶收起端，开合全程等比、落定后无字号尾段。
					scale.SetTar(0.16,duration,std::nullopt,false,closing);
					for(int tick=0;tick<36;++tick){
						if(!scale.IsSame())(void)UiAdvanceAnimation(scale,{dt,1,true,false});
						expect(proportional(moreFrame(dt)),"full More closing keeps labels proportional to the panel scale");
					}
					expect(scale.IsSame()&&!fonts.FontAnimationsActive(),"closed More leaves no font correction tail");
					scale.SetTar(1,duration,std::nullopt,false,opening);
					for(int tick=0;tick<36;++tick){
						if(!scale.IsSame())(void)UiAdvanceAnimation(scale,{dt,1,true,false});
						settled=moreFrame(dt);
						expect(proportional(settled),"More reopening from compact keeps labels proportional to the panel scale");
					}
					expect(!fonts.FontAnimationsActive()&&std::abs(settled-expected)<0.00001&&moreFrame(0)==settled,
						"More reopening from compact settles without a second font growth");
					// 同一溢出按钮回到主栏时，不得沿用紧凑面板的静止字号覆盖。
					scale.SetDirect(0.16);(void)moreFrame(duration);
					ConfigureButtonVisualTargets(button,kind,duration,1,false);
					button.name.size.SetDirect(button.name.size.tar);
					expect(button.name.fontTargetSize<0&&std::abs(drawFrame(duration)-expected)<0.00001,
						"moving a More button back to the main bar restores its ordinary font target");
				}
			}
			fonts.DiscardDeviceResources(); reference.DiscardDeviceResources();
		}

		// 早期 CLI 未读取配置；准备已规范化的内存布局，避免 Load 触发迁移写盘。
		auto a2Fixture=Inkeys::config.UI.Bar.FixedButtonsA2.Snapshot();
		for(auto& entry:a2Fixture)
			if(entry.Id==Inkeys::BarButtonId::Freeze)entry.Size=Inkeys::BarButtonSizeKind::TwoTwo;
		Inkeys::config.UI.Bar.FixedButtonsA2.Replace(std::move(a2Fixture));
		owner.barButtonSet.Load();
		// UpdateRendering 在 Pen 状态还会刷新粗细文字；离屏 fixture 不加载整套窗口控件。
		owner.wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay] =
			std::make_shared<UiWordClass>(0.0,0.0,0.0,0.0,L"",13.0);
		// 直接覆盖生产 PositionUpdate 的策略门禁，不能让后续刷新重新按中轴覆盖空间模式。
		{
			const int savedDirection = Inkeys::config.UI.Bar.ExpandDirectionAdjustment.load();
			const auto savedMainButton = owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
			const auto savedPlacement = owner.startupPlacement;
			const bool savedFold = owner.barState.fold;
			const bool savedSide = owner.barState.widgetPosition.mainBar;
			const bool savedPrimary = owner.barState.widgetPosition.primaryBar;
			const auto savedWidth = owner.barWindow.w, savedHeight = owner.barWindow.h;
			const auto mainButton = std::make_shared<UiSuperellipseClass>(800,500,80,80,10.0,1.0,std::nullopt,std::nullopt);
			owner.superellipseMap[BarUISetSuperellipseEnum::MainButton] = mainButton;
			// 早期 CLI 尚未启动交互/渲染，复用公开启动入口准备浮动状态。
			(void)owner.InitializeStartupPlacement(Inkeys::UI::StartupPlacement::BottomLeftFloating);
			expect(!owner.IsBottomDockLayoutLocked(),"PositionUpdate fixture initializes floating state through the production startup entry");
			owner.barWindow.w = 1000; owner.barWindow.h = 1000;
			Inkeys::config.UI.Bar.ExpandDirectionAdjustment = 0;
			owner.barState.widgetPosition.mainBar = true; owner.barState.PositionUpdate(1.0);
			expect(!owner.barState.widgetPosition.mainBar,"explicit centerline setting retains the legacy right-half classification");
			Inkeys::config.UI.Bar.ExpandDirectionAdjustment = 1;
			owner.barState.widgetPosition.mainBar = true;
			for(int frame=0;frame<120;++frame)owner.barState.PositionUpdate(1.0);
			expect(owner.barState.widgetPosition.mainBar,"space strategy survives repeated production PositionUpdate across the centerline");
			mainButton->x.SetDirect(200);mainButton->y.SetDirect(100);owner.barState.widgetPosition.mainBar = false;
			owner.barState.PositionUpdate(1.0);
			expect(!owner.barState.widgetPosition.mainBar && owner.barState.widgetPosition.primaryBar,
				"space strategy keeps the horizontal side while retaining the original upper/lower reversal");
			owner.superellipseMap[BarUISetSuperellipseEnum::MainButton] = savedMainButton;
			(void)owner.InitializeStartupPlacement(savedPlacement);
			owner.barState.fold = savedFold;
			owner.barState.widgetPosition.mainBar = savedSide;owner.barState.widgetPosition.primaryBar = savedPrimary;
			owner.barWindow.w = savedWidth;owner.barWindow.h = savedHeight;
			Inkeys::config.UI.Bar.ExpandDirectionAdjustment = savedDirection;
		}
		// 真实设置入口在无内容变化且工具不变时，也必须同步主栏显隐和清空状态。
		SetContentStateUpdatesReady(true);
		ChangeStateModeToPen();owner.UpdateRendering();
		auto* selectModeButton=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Select];
		auto* pierceModeButton=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Pierce];
		auto* freezeModeButton=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Freeze];
		auto* clearModeButton=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Clean];
		std::vector<BarButtonClass*> stableButtons;
		for(int index=0;index<owner.barButtonSet.tot;++index)
			stableButtons.push_back(owner.barButtonSet.buttonList.Get(index));
		const auto pierceIndex=std::find(stableButtons.begin(),stableButtons.end(),pierceModeButton);
		expect(pierceIndex!=stableButtons.end() && std::count(stableButtons.begin(),stableButtons.end(),pierceModeButton)==1
			&& pierceIndex+1!=stableButtons.end() && *(pierceIndex+1)==freezeModeButton,
			"hidden Pierce is one stable runtime member immediately before Freeze");
		const auto expectStableButtons=[&]()
		{
			bool same=owner.barButtonSet.tot==static_cast<int>(stableButtons.size());
			for(size_t index=0;index<stableButtons.size() && same;++index)
				same=owner.barButtonSet.buttonList.Get(static_cast<int>(index))==stableButtons[index];
			expect(same,"mode, content and Freeze changes retain hidden Pierce identity and list order");
		};
		const bool changedToSeparate=SetSelectAndClickThroughMode(1);
		selectModeButton->name.AdvanceContentTransition(1.0,1.0);
		expect(changedToSeparate && GetStateModeSnapshot().StateModeSelect==StateModeSelectEnum::IdtPen
			&& pierceModeButton->IsVisible() && pierceModeButton->size==ButtonSizeEnum::twoOne
			&& freezeModeButton->size==ButtonSizeEnum::twoOne
			&& selectModeButton->name.content.GetTar()==IW(I18nKey.UI.Bar.MainButtons.SelectClearLabel),
			"setting mode updates compact buttons while tool and empty content stay unchanged");
		// 在工具状态不变时切语言，必须绕过旧styleKey缓存且保留按钮身份。
		for (const auto& language : Inkeys::Localization::Locales)
		{
			expect(I18n::load(1,L"JSON",language.code),"load live language while drawing in separate mode");
			owner.barButtonSet.RefreshLocalizedText();
			owner.barButtonSet.UpdateMainPresentation(GetStateModeSnapshot());
			expect(selectModeButton->name.content.GetTar()==IW(I18nKey.UI.Bar.MainButtons.SelectClearLabel)
				&& selectModeButton->name.content.GetVal()==IW(I18nKey.UI.Bar.MainButtons.SelectClearLabel),
				"active separate-mode label updates without tool change");
			selectModeButton->name.AdvanceContentTransition(0.1,1.0);
			expect(selectModeButton->name.content.GetVal()==IW(I18nKey.UI.Bar.MainButtons.SelectClearLabel),
				"old text fade cannot resurrect previous language after immediate switch");
			expect(GetStateModeSnapshot().StateModeSelect==StateModeSelectEnum::IdtPen
				&& pierceModeButton->size==ButtonSizeEnum::twoOne && freezeModeButton->size==ButtonSizeEnum::twoOne,
				"live language preserves drawing mode and compact paired buttons");
			expectStableButtons();
		}
		expect(I18n::load(1,L"JSON",L"zh-CN"),"restore locale after live state labels");
		owner.barButtonSet.RefreshLocalizedText();owner.barButtonSet.UpdateMainPresentation(GetStateModeSnapshot());
		// 通过持久化布局触发组件刷新，验证分离式有效半高网格。
		owner.barButtonSet.RegisterBuiltInComponents();
		const auto originalLayout = Inkeys::config.GetBarLayout();
		auto explorerLayout = originalLayout;
		explorerLayout.extension.insert(explorerLayout.extension.begin(), { Inkeys::BarComponentId::Explorer });
		Inkeys::config.ReplaceBarLayout(explorerLayout, false);
		owner.barButtonSet.Load();owner.UpdateRendering();
		std::vector<BarButtonClass*> componentButtons;
		for(int index=0;index<owner.barButtonSet.tot;++index)
			componentButtons.push_back(owner.barButtonSet.buttonList.Get(index));
		const auto componentPierce=std::find(componentButtons.begin(),componentButtons.end(),pierceModeButton);
		expect(pierceModeButton->IsVisible() && freezeModeButton->IsVisible()
			&& pierceModeButton->size==ButtonSizeEnum::twoOne && freezeModeButton->size==ButtonSizeEnum::twoOne,
			"component reload retains Separate drawing Pierce and Freeze as twoOne");
		expect(componentPierce!=componentButtons.end() && componentPierce+1!=componentButtons.end()
			&& *(componentPierce+1)==freezeModeButton && std::count(componentButtons.begin(),componentButtons.end(),pierceModeButton)==1,
			"component reload preserves stable Pierce/Freeze identities and adjacency");
		expect(componentButtons.size()==stableButtons.size()+1,
			"real Explorer component switch adds one toolbar member");
		Inkeys::config.ReplaceBarLayout(originalLayout, false);
		owner.barButtonSet.Load();owner.UpdateRendering();
		expect(SetSelectAndClickThroughMode(0) && GetStateModeSnapshot().StateModeSelect==StateModeSelectEnum::IdtPen
			&& !pierceModeButton->IsVisible() && freezeModeButton->size==ButtonSizeEnum::twoTwo,
			"combined setting immediately hides Pierce and enlarges Freeze");
		// 直接执行生产 Pierce 回调，验证按钮切换身份和三类记忆工具恢复。
		SetSelectAndClickThroughMode(1);
		for(const auto tool:{StateModeSelectEnum::IdtPen,StateModeSelectEnum::IdtShape,StateModeSelectEnum::IdtEraser})
		{
			if(tool==StateModeSelectEnum::IdtPen)ChangeStateModeToPenTool(PenToolSelectionEnum::Highlighter);
			else if(tool==StateModeSelectEnum::IdtShape){ChangeStateModeToShape();SetShapeModeSelect(ShapeModeSelectEnum::IdtShapeRectangle1);}
			else ChangeStateModeToEraser();
			owner.UpdateRendering();
			const auto annotation=GetStateModeSnapshot();
			const auto revision=StateModeTransitionRevision();
			freezeModeButton->clickFunc();
			expect(Freeze::IsActive(),"Freeze fixture is active before the Pierce button click");
			pierceModeButton->clickFunc();owner.UpdateRendering();expectStableButtons();
			expect(GetStateModeSnapshot().StateModeSelect==StateModeSelectEnum::IdtClickThrough &&
				StateModeTransitionRevision()==revision+1 && !Freeze::IsActive() &&
				pierceModeButton->state->state==WidgetState::Selected,
				"production Pierce callback enters click-through and selects the same button");
			pierceModeButton->clickFunc();owner.UpdateRendering();expectStableButtons();
			const auto restored=GetStateModeSnapshot();
			const auto restoredPreset=tool==StateModeSelectEnum::IdtPen ? BarButtonPresetEnum::Draw :
				tool==StateModeSelectEnum::IdtShape ? BarButtonPresetEnum::Geometry : BarButtonPresetEnum::Eraser;
			expect(restored.StateModeSelect==tool && restored.lastAnnotationMode==tool &&
				restored.laserActive==annotation.laserActive && restored.Pen.ModeSelect==annotation.Pen.ModeSelect &&
				restored.Shape.ModeSelect==annotation.Shape.ModeSelect && StateModeTransitionRevision()==revision+2 &&
				!Freeze::IsActive() && pierceModeButton->state->state!=WidgetState::Selected &&
				owner.barButtonSet.preset[(int)restoredPreset]->state->state==WidgetState::Selected,
				"production Pierce callback restores the remembered tool and its selected button without Freeze");
			pierceModeButton->clickFunc();owner.UpdateRendering();
			expect(GetStateModeSnapshot().StateModeSelect==StateModeSelectEnum::IdtClickThrough &&
				pierceModeButton->state->state==WidgetState::Selected,"same Pierce callback can reenter click-through");
			pierceModeButton->clickFunc();owner.UpdateRendering();
			expect(GetStateModeSnapshot().StateModeSelect==tool && StateModeTransitionRevision()==revision+4 &&
				!Freeze::IsActive(),"same Pierce callback can exit again with one revision per click");
		}
		SetSelectAndClickThroughMode(0);
		ChangeStateModeToSelection();owner.UpdateRendering();
		expect(clearModeButton->state->state==WidgetState::Disable,
			"empty Selection Clear uses unified disabled state");
		ChangeStateModeToPen();owner.UpdateRendering();
		expect(clearModeButton->state->state!=WidgetState::Disable,"empty drawing Clear can enter Selection");
		SetSelectAndClickThroughMode(1);ChangeStateModeToClickThrough();owner.UpdateRendering();
		expect(pierceModeButton->state->state==WidgetState::Selected &&
			clearModeButton->state->state!=WidgetState::Disable,"Click-through highlight and empty Clear are active");
		owner.barButtonSet.ExecuteClearClick(false);owner.UpdateRendering();
		expect(GetStateModeSnapshot().StateModeSelect==StateModeSelectEnum::IdtSelection &&
			clearModeButton->state->state==WidgetState::Disable && !pierceModeButton->IsVisible(),
			"empty Click-through Clear enters Selection and removes hidden Pierce hit target");
		SetPptPresentationActive(true);Freeze::SetPresentationActive(true);owner.UpdateRendering();
		expect(!freezeModeButton->IsVisible() && !pierceModeButton->IsVisible(),
			"PPT empty Selection hides both A2 interaction buttons");
		ChangeStateModeToPen();owner.UpdateRendering();
		expect(!freezeModeButton->IsVisible() && pierceModeButton->IsVisible() &&
			pierceModeButton->size==ButtonSizeEnum::twoTwo,"PPT Pierce fills hidden Freeze slot");
		Freeze::SetPresentationActive(false);SetPptPresentationActive(false);
		SetSelectAndClickThroughMode(0);ChangeStateModeToSelection();owner.UpdateRendering();
		// 用真实两行按钮与位置动画检查 X 策略，隐藏锚点仍共用当前批次目标。
		for(int order=0;order<4;++order)for(bool opensRight:{false,true})
		{
			SetSelectAndClickThroughMode(1);ChangeStateModeToPen();owner.UpdateRendering();
			const double targetX = ResolveBarMainIconX(340.0,380.0,NormalizeBarIconOrder(order),opensRight);
			const UiCurveSpecClass curve{UiCurveEnum::EaseOutCubic,UiCurveEnum::EaseOutCubic,0.0,false};
			pierceModeButton->button.x.SetDirect(180.0);freezeModeButton->button.x.SetDirect(180.0);
			pierceModeButton->button.y.SetDirect(21.25);freezeModeButton->button.y.SetDirect(58.75);
			UiSetLayoutPositionTarget(pierceModeButton->button.x,targetX,UiDefaultOperationDur,true,curve);
			UiSetLayoutPositionTarget(freezeModeButton->button.x,targetX,UiDefaultOperationDur,true,curve);
			expect(pierceModeButton->button.x.val==180.0 && freezeModeButton->button.x.val==180.0,
				"sorting retargets from current visible values instead of jumping");
			for(int frame=0;frame<12;++frame)
			{
				const UiAnimationAdvanceContextClass tick{0.05,1,true,false};
				UiAdvanceAnimation(pierceModeButton->button.x,tick);UiAdvanceAnimation(freezeModeButton->button.x,tick);
			}
			expect(pierceModeButton->button.x.val==targetX && freezeModeButton->button.x.val==targetX
				&& pierceModeButton->button.y.val<freezeModeButton->button.y.val,
				"all horizontal policies keep Pierce above Freeze and their shared column");
			SetSelectAndClickThroughMode(0);owner.UpdateRendering();
			AnchorHiddenBarButton(owner.barButtonSet,false,BarButtonPresetEnum::Pierce,BarButtonPresetEnum::Freeze,
				[&](UiValueClass& value,double target){UiSetLayoutPositionTarget(value,target,UiDefaultOperationDur,true,curve);});
			expect(!pierceModeButton->IsVisible() && pierceModeButton->button.x.tar==targetX,
				"hidden Pierce retains the mirrored column without becoming a hit target");
			expectStableButtons();
		}
		// 连续收窄/展开和 PPT 进出：隐藏按钮只改 hide，不能离开列表或保留右侧旧坐标。
		const auto positionTarget=[](UiValueClass& value,double target)
			{ value.SetTar(target,UiDefaultOperationDur); };
		const auto anchorPierce=[&]()
			{ AnchorHiddenBarButton(owner.barButtonSet,false,BarButtonPresetEnum::Pierce,
				BarButtonPresetEnum::Freeze,positionTarget); };
		const auto advancePierce=[&](double dt)
		{
			const UiAnimationAdvanceContextClass frame{dt,1,true,false};
			UiAdvanceAnimation(pierceModeButton->button.x,frame);
			UiAdvanceAnimation(pierceModeButton->button.y,frame);
		};
		for(int cycle=0;cycle<3;++cycle)
		{
			ChangeStateModeToSelection();SetCurrentPageHasContent(false);owner.UpdateRendering();expectStableButtons();
			pierceModeButton->button.x.SetDirect(1000.0);
			pierceModeButton->button.y.SetDirect(20.0);
			freezeModeButton->button.x.SetDirect(450.0-cycle*75.0);
			freezeModeButton->button.y.SetDirect(40.0);
			anchorPierce();advancePierce(0.0);
			expect(pierceModeButton->button.x.val==1000.0 && pierceModeButton->button.x.tar==freezeModeButton->button.x.tar
				&& pierceModeButton->button.y.tar==freezeModeButton->button.y.tar,
				"hidden Pierce retargets to Freeze without jumping on zero-time advance");
			for(int frame=0;frame<12;++frame){anchorPierce();advancePierce(0.05);}
			expect(pierceModeButton->button.x.val==freezeModeButton->button.x.tar
				&& pierceModeButton->button.y.val==freezeModeButton->button.y.tar,
				"repeated hidden frames settle Pierce at Freeze instead of the old right edge");
			SetSelectAndClickThroughMode(1);ChangeStateModeToPen();owner.UpdateRendering();expectStableButtons();
			expect(pierceModeButton->IsVisible() && pierceModeButton->button.x.val==freezeModeButton->button.x.tar,
				"revealed Pierce starts from the retained Freeze column");
			SetSelectAndClickThroughMode(0);owner.UpdateRendering();expectStableButtons();
			freezeModeButton->button.x.SetDirect(250.0);anchorPierce();
			for(int frame=0;frame<12;++frame){anchorPierce();advancePierce(0.05);}
			expect(!pierceModeButton->IsVisible() && pierceModeButton->button.x.val==250.0,
				"Combined also retains and positions hidden Pierce for the next mode change");
			SetPptPresentationActive(true);Freeze::SetPresentationActive(true);owner.UpdateRendering();expectStableButtons();
			auto* a2Divider=std::distance(stableButtons.begin(),pierceIndex)>=2 ? *(pierceIndex-2) : nullptr;
			auto* whiteboardAnchor=std::distance(stableButtons.begin(),pierceIndex)>=1 ? *(pierceIndex-1) : nullptr;
			expect(a2Divider && a2Divider->preset==BarButtonPresetEnum::Divider && a2Divider->IsVisible(),
				"default A2 boundary remains two positions before Pierce");
			expect(whiteboardAnchor && whiteboardAnchor->preset==BarButtonPresetEnum::Whiteboard && whiteboardAnchor->IsVisible(),
				"restored Whiteboard is the nearest visible predecessor when Freeze is unavailable");
			if(a2Divider){a2Divider->button.x.SetDirect(300.0);a2Divider->button.y.SetDirect(40.0);}
			if(whiteboardAnchor){whiteboardAnchor->button.x.SetDirect(375.0);whiteboardAnchor->button.y.SetDirect(50.0);}
			anchorPierce();
			for(int frame=0;frame<12;++frame){anchorPierce();advancePierce(0.05);}
			expect(!freezeModeButton->IsVisible() && !pierceModeButton->IsVisible() && whiteboardAnchor
				&& pierceModeButton->button.x.val==whiteboardAnchor->button.x.tar
				&& pierceModeButton->button.y.val==whiteboardAnchor->button.y.tar,
				"hidden Pierce anchors to restored Whiteboard when Freeze is unavailable");
			// 保留原来的较远锚点覆盖：白板也不可见时仍应回退到 A2 分隔线。
			if(whiteboardAnchor)
			{
				const bool savedWhiteboardHide=whiteboardAnchor->hide;
				whiteboardAnchor->hide=true;
				for(int frame=0;frame<12;++frame){anchorPierce();advancePierce(0.05);}
				expect(a2Divider && pierceModeButton->button.x.val==a2Divider->button.x.tar
					&& pierceModeButton->button.y.val==a2Divider->button.y.tar,
					"hidden Pierce still falls back to A2 boundary when both Freeze and Whiteboard are unavailable");
				whiteboardAnchor->hide=savedWhiteboardHide;
			}
			SetSelectAndClickThroughMode(1);owner.UpdateRendering();expectStableButtons();
			expect(pierceModeButton->IsVisible() && pierceModeButton->size==ButtonSizeEnum::twoTwo,
				"PPT reveals the same Pierce member in the full slot");
			Freeze::SetPresentationActive(false);SetPptPresentationActive(false);
		}
		// 真实 B1 组件 0/1/2/3+ 增删：不执行组件动作，不启动Host或窗口。
		{
			const auto savedLayout = Inkeys::config.GetBarLayout();
			const bool savedFold=owner.barState.fold;
			owner.barState.fold=false;
			const char* ids[] = { Inkeys::BarComponentId::Explorer, Inkeys::BarComponentId::Taskmgr,
				Inkeys::BarComponentId::Control, Inkeys::BarComponentId::Desktop };
			const auto reload=[&](int count)
			{
				auto values = savedLayout;
				values.extension.clear();
				for (int i = 0; i < count; ++i) values.extension.push_back({ ids[i] });
				values.extension.push_back({ Inkeys::BarButtonId::MoreBoundary });
				values.extension.push_back({ Inkeys::BarButtonId::Setting });
				Inkeys::config.ReplaceBarLayout(std::move(values), false);
				owner.barButtonSet.Load();owner.UpdateRendering();
			};
			const auto switchTool=[&](StateModeSelectEnum tool)
			{
				if(tool==StateModeSelectEnum::IdtSelection)ChangeStateModeToSelection();
				else if(tool==StateModeSelectEnum::IdtPen)ChangeStateModeToPen();
				else if(tool==StateModeSelectEnum::IdtShape)ChangeStateModeToShape();
				else if(tool==StateModeSelectEnum::IdtEraser)ChangeStateModeToEraser();
				else ChangeStateModeToClickThrough();
			};
			const auto savedA1=Inkeys::config.UI.Bar.FixedButtonsA1.Snapshot();
			const auto savedA2=Inkeys::config.UI.Bar.FixedButtonsA2.Snapshot();
			int matrixCases=0;
			Inkeys::Testing::WorkspaceSceneScope workspaceScope;
			for(int count:{0,1,2,3,4,2,1,0})
			for(int mode:{0,1})for(int scene:{0,1,2})for(bool content:{false,true})
			for(const auto tool:{StateModeSelectEnum::IdtSelection,StateModeSelectEnum::IdtPen,
				StateModeSelectEnum::IdtShape,StateModeSelectEnum::IdtEraser,StateModeSelectEnum::IdtClickThrough})
			{
				(void)workspaceScope.Set(scene==2 ? WorkspaceScene::Whiteboard : scene==1 ? WorkspaceScene::Presentation : WorkspaceScene::Desktop);
				SetWhiteboardActive(scene==2);Freeze::SetWhiteboardActive(scene==2);
				SetPptPresentationActive(scene==1);Freeze::SetPresentationActive(scene==1);
				SetSelectAndClickThroughMode(mode);SetCurrentPageHasContent(content);switchTool(tool);
				owner.barState.fold=false;reload(count);
				const bool selectionEmpty=tool==StateModeSelectEnum::IdtSelection && !content;
				const bool pierceVisible=mode==1 && scene!=2 && !selectionEmpty;
				expect(pierceModeButton->IsVisible()==pierceVisible && freezeModeButton->IsVisible()==(scene==0)
					&& (!pierceVisible || pierceModeButton->size==(scene==1?ButtonSizeEnum::twoTwo:ButtonSizeEnum::twoOne))
					&& (scene!=0 || freezeModeButton->size==(pierceVisible?ButtonSizeEnum::twoOne:ButtonSizeEnum::twoTwo)),
					"component matrix keeps the workspace/mode/content A2 grids");
				expect(owner.barButtonSet.preset[(int)BarButtonPresetEnum::Eraser]->IsVisible()==!(scene!=2 && selectionEmpty)
					&& owner.barButtonSet.preset[(int)BarButtonPresetEnum::Geometry]->IsVisible()==!(scene!=2 && selectionEmpty)
					&& (clearModeButton->state->state==WidgetState::Disable)==selectionEmpty,
					"component matrix retains compact tools and Clear availability");
				const auto more=owner.barButtonSet.GetMoreButtonSnapshot();
				std::vector<std::string> mainIds;
				for (const auto& button : owner.barButtonSet.buttonList.Snapshot())
					if (Inkeys::IsExtensionBarButtonId(button->id)) mainIds.push_back(button->id);
				expect(owner.barButtonSet.tot==static_cast<int>(stableButtons.size())+count
					&& mainIds == std::vector<std::string>(ids, ids + count)
					&& more.explicitMore.size()==1 && more.explicitMore.front()->id==Inkeys::BarButtonId::Setting
					&& more.forcedOverflow.empty(), "explicit B1 order and singleton Settings across component matrix");
				const auto before=owner.barButtonSet.MainLayoutSnapshot();
				owner.barButtonSet.Load();owner.UpdateRendering();
				for(int repeat=0;repeat<3;++repeat)owner.UpdateRendering();
				expect(owner.barButtonSet.MainLayoutSnapshot()==before
					&& owner.barButtonSet.preset[(int)BarButtonPresetEnum::Freeze]==freezeModeButton
					&& owner.barButtonSet.preset[(int)BarButtonPresetEnum::Pierce]==pierceModeButton,
					"repeated Load and projection preserve all effective geometry and stable identities");
				++matrixCases;
			}
			// 真实准备路径必须在三种稳定工作区保持形状面板展开，同时保留统一收起条件。
			auto* geometryModeButton=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Geometry];
			for(int scene:{0,1,2})
			{
				(void)workspaceScope.Set(scene==2 ? WorkspaceScene::Whiteboard : scene==1 ? WorkspaceScene::Presentation : WorkspaceScene::Desktop);
				SetWhiteboardActive(scene==2);Freeze::SetWhiteboardActive(scene==2);
				SetPptPresentationActive(scene==1);Freeze::SetPresentationActive(scene==1);
				SetCurrentPageHasContent(true);owner.barState.fold=false;ChangeStateModeToShape();owner.UpdateRendering();
				owner.barState.geometryAttribute=true;owner.UpdateRendering();
				expect(owner.barState.geometryAttribute,
					"Shape panel stays open through production preparation in Desktop, Presentation and Whiteboard");
				ChangeStateModeToPen();owner.barState.geometryAttribute=true;owner.UpdateRendering();
				expect(!owner.barState.geometryAttribute,"Shape panel closes when the active tool is not Shape");
				ChangeStateModeToShape();owner.barState.fold=false;owner.UpdateRendering();
				owner.barState.fold=true;owner.barState.geometryAttribute=true;owner.UpdateRendering();
				expect(!owner.barState.geometryAttribute,"Shape panel closes when the main bar folds");
				owner.barState.fold=false;owner.UpdateRendering();
				geometryModeButton->hide=true;owner.barState.geometryAttribute=true;
				owner.barButtonSet.PrepareMainPanels(GetStateModeSnapshot());
				expect(!owner.barState.geometryAttribute,"Shape panel closes when its main button is invisible");
				owner.UpdateRendering();
			}
			report<<"[MainPresentation] componentMatrixCases="<<matrixCases<<'\n';
			expect(WhiteboardActive() && !HideWhiteboardSnapIndicator()
				&& ResolveBarBottomDockIndicatorTarget(BarBottomDockMode::BottomDocked,true,true,true)
				&& !ResolveBarBottomDockIndicatorTarget(BarBottomDockMode::BottomDocked,false,true,true)
				&& !ResolveBarBottomDockIndicatorTarget(BarBottomDockMode::BottomDocked,true,true,false),
				"active Whiteboard permits eligible drag feedback while entry and unchanged gestures stay hidden");
			(void)workspaceScope.Set(WorkspaceScene::Desktop);
			SetWhiteboardActive(false);Freeze::SetWhiteboardActive(false);
			SetPptPresentationActive(false);Freeze::SetPresentationActive(false);
			SetSelectAndClickThroughMode(1);ChangeStateModeToPen();SetCurrentPageHasContent(false);reload(0);
			const auto compact=owner.barButtonSet.MainLayoutSnapshot();
			const double compactX=pierceModeButton->button.x.val;
			const double compactY=pierceModeButton->button.y.val;
			const double compactH=freezeModeButton->button.h.val;
			SetSelectAndClickThroughMode(0);owner.UpdateRendering();
			const auto full=owner.barButtonSet.MainLayoutSnapshot();
			expect(compact!=full && pierceModeButton->button.x.val==compactX
				&& pierceModeButton->button.y.val==compactY && freezeModeButton->button.h.val==compactH,
				"equal-width split/full A2 change invalidates geometry without moving current animation values");
			SetSelectAndClickThroughMode(1);owner.UpdateRendering();
			const auto stable=owner.barButtonSet.MainLayoutSnapshot();
			auto* draw=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Draw];
			const auto savedEmph=draw->state->emph.load();const auto savedHover=draw->hoverStage.load();
			draw->state->emph=WidgetEmphasize::Pressed;draw->hoverStage=ButtonHoverStageEnum::Showing;
			draw->pressScale.SetTar(0.9,0.4);draw->name.color.SetTar(RGB(1,2,3),0.4);
			expect(owner.barButtonSet.MainLayoutSnapshot()==stable,
				"hover, press, color and animation values are absent from the layout fingerprint");
			draw->state->emph=savedEmph;draw->hoverStage=savedHover;
			draw->pressScale.SetDirect(1.0);
			owner.barButtonSet.buttonList.Swap(0,1);
			expect(owner.barButtonSet.MainLayoutSnapshot()!=stable,"equal-width member permutation changes the stable layout fingerprint");
			owner.barButtonSet.buttonList.Swap(0,1);
			// 模拟目标提交使用真实timeline/SetTar helper：早期可加入、晚期从当前值完整重开。
			UiTimelineClass batch;batch.Restart(UiDefaultOperationDur);
			pierceModeButton->button.y.SetDirect(40.0);freezeModeButton->button.y.SetDirect(40.0);
			freezeModeButton->button.h.SetDirect(70.0);
			UiSetLayoutPositionTarget(pierceModeButton->button.y,21.25,batch.GetRemainingDuration(),true,ButtonPressCurve());
			UiSetLayoutPositionTarget(freezeModeButton->button.y,58.75,batch.GetRemainingDuration(),true,ButtonPressCurve());
			freezeModeButton->button.h.SetTar(32.5,batch.GetRemainingDuration());
			const UiAnimationAdvanceContextClass zero{0.0,1,true,false};
			UiAdvanceAnimation(freezeModeButton->button.h,zero);
			expect(freezeModeButton->button.h.val==70.0 && batch.CanJoin(),"zero-time split targets retain current geometry and permit early batch join");
			batch.Advance(UiDefaultOperationDur*0.75,1);
			const UiAnimationAdvanceContextClass partial{UiDefaultOperationDur*0.75,1,true,false};
			UiAdvanceAnimation(freezeModeButton->button.h,partial);
			const double interruptedHeight=freezeModeButton->button.h.val;
			expect(!batch.CanJoin(),"late main-layout update cannot join the old half-finished batch");
			SetSelectAndClickThroughMode(0);owner.UpdateRendering();
			batch.Restart(UiDefaultOperationDur);freezeModeButton->button.h.SetTar(70.0,batch.GetRemainingDuration());
			expect(freezeModeButton->button.h.val==interruptedHeight,"late layout retarget starts at the displayed height");
			const auto repeated=owner.barButtonSet.MainLayoutSnapshot();
			for(int frame=0;frame<120;++frame)
			{
				owner.UpdateRendering();
				const UiAnimationAdvanceContextClass tick{0.01,1,true,false};
				UiAdvanceAnimation(freezeModeButton->button.h,tick);batch.Advance(0.01,1);
				selectModeButton->name.AdvanceContentTransition(0.01,1);
			}
			expect(owner.barButtonSet.MainLayoutSnapshot()==repeated && !batch.IsActive()
				&& freezeModeButton->button.h.val==70.0 && selectModeButton->name.content.GetVal()==selectModeButton->name.content.GetTar(),
				"same-state refresh leaves the batch and content animation free to settle");
			const auto fixedEqual=[](const auto& left,const auto& right)
			{
				if(left.size()!=right.size())return false;
				for(size_t i=0;i<left.size();++i)if(left[i].Id!=right[i].Id || left[i].Size!=right[i].Size)return false;
				return true;
			};
			expect(fixedEqual(savedA1,Inkeys::config.UI.Bar.FixedButtonsA1.Snapshot())
				&& fixedEqual(savedA2,Inkeys::config.UI.Bar.FixedButtonsA2.Snapshot()),"effective presentation never rewrites persisted A1/A2 sizes or order");
			Inkeys::config.ReplaceBarLayout(savedLayout, false);
			owner.barState.fold=savedFold;owner.barButtonSet.Load();owner.UpdateRendering();expectStableButtons();
		}
		// 用真实历史刷新和墙钟到期入口覆盖精简态，保留保护身份但不能再次显示按钮。
		auto* undoModeButton=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Recall];
		auto* redoModeButton=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Redo];
		for(const bool redo:{false,true})
		{
			ChangeStateModeToPen();SetCurrentPageHasContent(true);
			{
				std::scoped_lock lock(owner.recallChromeMutex);
				owner.recallChrome={};
				owner.recallChrome.availability={true,true};
				owner.recallChrome.layout=RecallChromeLayout::Compact;
				owner.recallChrome=ApplyRecallChromeCommandResult(owner.recallChrome,redo,{true,true},true);
				owner.ArmRecallChromeIdleDeadline();
			}
			ChangeStateModeToSelection();SetCurrentPageHasContent(false);owner.UpdateRendering();
			const auto hiddenHistory=[&]()
			{
				const auto chrome=owner.RecallChromeView();
				expect(!undoModeButton->IsVisible() && !redoModeButton->IsVisible()
					&& !owner.barState.recallAttribute && !chrome.popupOpen
					&& !chrome.mainUndoVisible && !chrome.mainRedoVisible,
					"empty Selection chrome stays hidden across refresh and timer projection");
			};
			hiddenHistory();
			const auto protectedHold=owner.recallChrome.hold;
			const auto protectedDeadline=owner.recallChromeIdleDeadline;
			owner.UpdateRendering();hiddenHistory();
			owner.NoteRecallHistoryAvailability(true,true);hiddenHistory();
			owner.NoteRecallHistoryAvailability(false,false);hiddenHistory();
			unsigned retry=0;
			expect(!owner.PollRecallChromeIdleTimer(retry) && retry>0 && !owner.AdvanceRecallChromeIdle(10.0)
				&& owner.recallChrome.hold==protectedHold && owner.recallChromeIdleDeadline==protectedDeadline,
				"compact Selection preserves the 3 second wall-clock protection");
			SetCurrentPageHasContent(true);owner.UpdateRendering();
			expect(redo ? redoModeButton->IsVisible() : undoModeButton->IsVisible(),
				"nonempty Selection retains the protected history layout");
			SetCurrentPageHasContent(false);owner.UpdateRendering();hiddenHistory();
			owner.recallChromeIdleDeadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);
			expect(owner.PollRecallChromeIdleTimer(retry) && owner.AdvanceRecallChromeIdle(0.0),
				"actual idle expiry applies the pending history layout");
			hiddenHistory();
			expect(owner.recallChrome.hold==RecallChromeHold::None && !owner.recallChromeIdleArmed,
				"idle settlement completes without redisplaying hidden history buttons");
			ChangeStateModeToPen();owner.UpdateRendering();
			expect(undoModeButton->IsVisible() && !redoModeButton->IsVisible(),
				"leaving compact Selection restores the original settled history layout");
		}
		// 无Host的真实历史事件/墙钟/拒绝命令入口，不把种入Accepted策略当成产品命令执行。
		{
			ChangeStateModeToPen();SetCurrentPageHasContent(true);owner.barState.fold=false;
			const auto seedHold=[&](bool redo)
			{
				std::scoped_lock lock(owner.recallChromeMutex);
				owner.recallChrome={};owner.recallChrome.availability={true,true};
				owner.recallChrome.layout=RecallChromeLayout::Compact;
				owner.recallChrome=ApplyRecallChromeCommandResult(owner.recallChrome,redo,{true,true},true);
				owner.ArmRecallChromeIdleDeadline();
			};
			for(bool redo:{false,true})
			{
				seedHold(redo);owner.UpdateRendering();
				const auto protectedLayout=owner.barButtonSet.MainLayoutSnapshot();
				const auto protectedHold=owner.recallChrome.hold;
				const auto deadline=owner.recallChromeIdleDeadline;
				unsigned retry=0;
				expect(!owner.PollRecallChromeIdleTimer(retry) && retry>0 && !owner.AdvanceRecallChromeIdle(100.0)
					&& owner.recallChromeIdleDeadline==deadline,"early timer and large animation dt cannot consume the wall-clock hold");
				{
					std::scoped_lock lock(owner.recallChromeMutex);
					owner.recallChromeIdleDeadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);
				}
				if(redo)owner.OpenRecallFromRedo();else owner.OpenRecallFromUndo();
				expect(owner.recallChromeIdleDeadline>std::chrono::steady_clock::now()
					&& !owner.PollRecallChromeIdleTimer(retry) && retry>0 && owner.recallChrome.hold==protectedHold
					&& owner.barButtonSet.MainLayoutSnapshot()==protectedLayout,
					"gray production command extends the hold without directly changing button geometry");
				owner.UpdateRendering();
				expect((redo?redoModeButton:undoModeButton)->state->state!=WidgetState::Disable,
					"gray protected history remains eligible for repeat-click feedback");
				const auto refreshedDeadline=owner.recallChromeIdleDeadline;
				owner.NoteRecallPopupDismissed();owner.ClearRecallForDrawing();owner.SettleRecallChromeForWorkspace();
				expect(owner.recallChromeIdleDeadline==refreshedDeadline && owner.recallChrome.hold==protectedHold
					&& owner.barButtonSet.MainLayoutSnapshot()==protectedLayout,
					"popup, drawing and workspace events only suppress popup while protecting main geometry");
				owner.UpdateRendering();
				expect(!owner.RecallChromeView().popupOpen,"unified view publishes suppressed popup identity");
				owner.barButtonSet.Load();owner.UpdateRendering();
				expect(owner.barButtonSet.MainLayoutSnapshot()==protectedLayout,"component Load cannot reset a protected history identity");
				const auto beforeExpiry=owner.barButtonSet.MainLayoutSnapshot();
				{
					std::scoped_lock lock(owner.recallChromeMutex);
					owner.recallChromeIdleDeadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);
				}
				expect(owner.PollRecallChromeIdleTimer(retry) && owner.AdvanceRecallChromeIdle(0.0)
					&& !owner.recallChromeIdleArmed && owner.recallChrome.hold==RecallChromeHold::None
					&& owner.barButtonSet.MainLayoutSnapshot()==beforeExpiry,
					"idle expiry settles state without a second effective-button write path");
				owner.barButtonSet.UpdateMainPresentation(GetStateModeSnapshot());
				expect(undoModeButton->IsVisible() && undoModeButton->size==ButtonSizeEnum::twoTwo && !redoModeButton->IsVisible(),
					"the frame presentation entry applies the settled history grid before target scanning");
			}
			{
				std::scoped_lock lock(owner.recallChromeMutex);
				owner.recallChrome={};owner.recallChrome.layout=RecallChromeLayout::Compact;
			}
			owner.NoteRecallHistoryAvailability(false,false);owner.UpdateRendering();
			expect(owner.recallChrome.settlePending && owner.recallChromeIdleArmed
				&& undoModeButton->size==ButtonSizeEnum::twoOne && redoModeButton->size==ButtonSizeEnum::twoOne,
				"availability-only layout changes also keep a wall-clock pending transition");
			{
				std::scoped_lock lock(owner.recallChromeMutex);
				owner.recallChromeIdleDeadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);
			}
			expect(owner.AdvanceRecallChromeIdle(0.0),"availability-only idle transition is settled without a clicked hold");
			owner.UpdateRendering();
			expect(undoModeButton->size==ButtonSizeEnum::twoTwo && !redoModeButton->IsVisible(),
				"availability-only settled grid is applied by the same projection");
		}
		SetCurrentPageHasContent(false);SetSelectAndClickThroughMode(0);ChangeStateModeToSelection();owner.UpdateRendering();
		SetContentStateUpdatesReady(false);
		SetThemeStyleSource(&owner.barStyle);
		auto root=std::make_shared<UiSuperellipseClass>(100,430,BarMainButtonWidthDip,BarMainButtonHeightDip,3.0,
			ButtonFrameThicknessDip,GetThemeColor(ThemeColorEnum::Surface),GetThemeColor(ThemeColorEnum::SurfaceFrame));
		root->enable.Initialization(true);owner.superellipseMap[BarUISetSuperellipseEnum::MainButton]=root;
		const int statusIndicatorFailures=owner.statusIndicator.RunOffscreenTest(owner);
		expect(statusIndicatorFailures==0,"status indicator command/render/presented hit/Fan/progress production paths");
		// 设备创建或绘制失败可能释放资源；上面的断言已报告失败，避免保存时解引用空指针。
		if(owner.specTarget.DeviceContext()&&owner.specTarget.TargetBitmap())
		{
			std::filesystem::create_directories(L"Build/eraser-b/status-indicator");
			expect(SUCCEEDED(SaveEraserTestPng(owner.specTarget.DeviceContext(),owner.specTarget.TargetBitmap(),
				L"Build/eraser-b/status-indicator/fan.png")),"status indicator Fan PNG");
		}
		auto main=std::make_shared<UiShapeClass>(230,0,BarDefaultMainBarLayoutWidthDip,BarMainBarHeightDip,
			BarMainBarCornerRadiusDip,BarMainBarCornerRadiusDip,ButtonFrameThicknessDip,
			GetThemeColor(ThemeColorEnum::Surface),GetThemeColor(ThemeColorEnum::SurfaceFrame));
		main->enable.Initialization(true);main->pct.SetDirect(BarMainBarFillOpacity);
		main->framePct.emplace(BarMainBarFrameOpacity);main->frameLightPct.emplace(BarMainBarFrameOpacity);
		main->frameRendering=UiFrameRenderingEnum::PointLight;owner.shapeMap[BarUISetShapeEnum::MainBar]=main;
		// 真实Advance消费已捕获帧，即使锁外又发布空页Selection也不能混用下一帧门禁。
		{
			owner.barState.fold=false;owner.barState.drawAttribute=false;owner.barState.geometryAttribute=false;
			owner.barState.moreExpanded=false;owner.barState.eraserAttribute=false;
			for(const bool redo:{false,true})
			{
				{
					std::scoped_lock lock(owner.recallChromeMutex);
					owner.recallChrome={};owner.recallChrome.availability={true,true};
					owner.recallChrome.layout=RecallChromeLayout::Compact;
					owner.recallChrome=ApplyRecallChromeCommandResult(owner.recallChrome,redo,{true,true},true);
					owner.ArmRecallChromeIdleDeadline();
				}
				auto frameTool=GetStateModeSnapshot();frameTool.StateModeSelect=StateModeSelectEnum::IdtPen;
				owner.barButtonSet.UpdateMainPresentation(frameTool);
				const auto frameChrome=owner.RecallChromeView();
				auto* anchor=owner.barButtonSet.preset[(int)(frameChrome.redoExpanded?BarButtonPresetEnum::Redo:BarButtonPresetEnum::Recall)];
				const bool anchorVisible=anchor->IsVisible();
				anchor->button.x.SetDirect(200.0);anchor->button.y.SetDirect(40.0);
				anchor->button.w.SetDirect(70.0);anchor->button.h.SetDirect(70.0);
				frameTool.StateModeSelect=StateModeSelectEnum::IdtSelection;
				owner.barButtonSet.UpdateMainPresentation(frameTool);
				expect(frameChrome.popupOpen && frameChrome.popupShowsRedo!=redo && anchorVisible
					&& !owner.RecallChromeView().popupOpen && !anchor->IsVisible(),
					"fixture publishes a newer global history view after capturing an open frame and its anchor gate");
				BarRecallAttributePanel framePanel;
				for(int tick=0;tick<90;++tick)
					framePanel.Advance(owner,frameChrome,anchorVisible,1.0/60,1,1,96,{0,0,1000,1000},{0,0},0,0);
				framePanel.CommitPresented();
				expect(framePanel.PresentationSnapshot().panelVisible,
					"real history Advance preserves the captured frame popup despite newer global compact visibility");
				for(int tick=0;tick<90;++tick)
					framePanel.Advance(owner,frameChrome,false,1.0/60,1,1,96,{0,0,1000,1000},{0,0},0,0);
				framePanel.CommitPresented();
				expect(!framePanel.PresentationSnapshot().panelVisible,
					"real history Advance closes when the supplied frame anchor gate becomes hidden");
			}
			owner.barButtonSet.UpdateMainPresentation(GetStateModeSnapshot());
		}
		stateMode.StateModeSelect=StateModeSelectEnum::IdtEraser;owner.barState.fold=false;
		owner.barState.widgetPosition.mainBar=true;owner.barState.widgetPosition.primaryBar=false;
		const auto metrics=ResolveButtonVisualMetrics(ButtonVisualLayoutKind::StandardTwoTwo);
		const auto twoOneMetrics=ResolveButtonVisualMetrics(ButtonVisualLayoutKind::StandardTwoOne);
		const std::array presets{BarButtonPresetEnum::Select,BarButtonPresetEnum::Draw,BarButtonPresetEnum::Eraser,BarButtonPresetEnum::More,BarButtonPresetEnum::Setting};
		const std::array<const char*,5> localizedPresetLabelKeys{
			I18nKey.UI.Bar.MainButtons.SelectLabel,
			I18nKey.UI.Bar.MainButtons.DrawLabel,
			I18nKey.UI.Bar.MainButtons.EraserLabel,
			I18nKey.UI.Bar.MainButtons.MoreLabel,
			I18nKey.UI.Bar.MainButtons.SettingsLabel};
		auto updateLocalizedPresetLabels=[&]()
		{
			for(size_t i=0;i<presets.size();++i)
			{
				auto* b=presets[i]==BarButtonPresetEnum::More?owner.barButtonSet.GetMoreButton():owner.barButtonSet.preset[static_cast<int>(presets[i])];
				const auto label=IW(localizedPresetLabelKeys[i]);b->name.SetStringImmediate(label);b->TransitionContent(L"",label);b->name.size.SetDirect(b->LabelFontSize(metrics.primaryFontSizeDip));
			}
		};
		for(size_t i=0;i<presets.size();++i)
		{
			auto* b=presets[i]==BarButtonPresetEnum::More?owner.barButtonSet.GetMoreButton():owner.barButtonSet.preset[static_cast<int>(presets[i])];
			b->button.x.SetDirect(ButtonGapDip+metrics.buttonWidthDip/2+i*BarDefaultButtonColumnStepDip);
			b->button.y.SetDirect(BarMainBarHeightDip/2);b->button.w.SetDirect(metrics.buttonWidthDip);b->button.h.SetDirect(metrics.buttonHeightDip);
			b->name.y.SetDirect(metrics.primaryOffsetYDip);b->name.w.SetDirect(metrics.primarySlotWidthDip);b->name.h.SetDirect(metrics.primarySlotHeightDip);b->name.size.SetDirect(metrics.primaryFontSizeDip);
			b->icon.y.SetDirect(metrics.iconOffsetYDip);b->icon.SetWH(metrics.iconSizeDip,metrics.iconSizeDip);
			b->icon.w.SetDirect(b->icon.w.tar);b->icon.h.SetDirect(b->icon.h.tar);
			b->button.pct.SetDirect(presets[i]==BarButtonPresetEnum::Eraser?0.2:0);
			b->name.pct.SetDirect(1);b->icon.pct.SetDirect(1);
		}
		std::filesystem::create_directories(L"Build/eraser-b/visuals");

		const std::array<const char*,23> fixedLabelKeys{
			I18nKey.UI.Bar.MainButtons.SelectLabel,I18nKey.UI.Bar.MainButtons.MoveLabel,
			I18nKey.UI.Bar.MainButtons.DrawLabel,I18nKey.UI.Bar.MainButtons.LaserLabel,
			I18nKey.UI.Bar.MainButtons.HighlighterLabel,I18nKey.UI.Bar.MainButtons.HardPenLabel,
			I18nKey.UI.Bar.MainButtons.SoftPenLabel,I18nKey.UI.Bar.MainButtons.EraserLabel,
			I18nKey.UI.Bar.MainButtons.AreaEraserLabel,I18nKey.UI.Bar.MainButtons.GeometryLabel,
			I18nKey.UI.Bar.MainButtons.RectangleLabel,I18nKey.UI.Bar.MainButtons.StraightLineLabel,
			I18nKey.UI.Bar.MainButtons.UndoLabel,I18nKey.UI.Bar.MainButtons.RedoLabel,
			I18nKey.UI.Bar.MainButtons.ReturnToShowLabel,I18nKey.UI.Bar.MainButtons.ClearLabel,
			I18nKey.UI.Bar.MainButtons.WhiteboardLabel,I18nKey.UI.Bar.MainButtons.CloseWhiteboardLabel,
			I18nKey.UI.Bar.MainButtons.ResumeWhiteboardLabel,
			I18nKey.UI.Bar.MainButtons.FreezeLabel,I18nKey.UI.Bar.MainButtons.EndPresentationLabel,
			I18nKey.UI.Bar.MainButtons.SettingsLabel,I18nKey.UI.Bar.MainButtons.MoreLabel};
		const std::array<const char*,7> penMenuLabelKeys{
			I18nKey.UI.Bar.DrawAttributes.BrushLabel,I18nKey.UI.Bar.DrawAttributes.LaserLabel,
			I18nKey.UI.Bar.DrawAttributes.HighlighterLabel,I18nKey.UI.Bar.DrawAttributes.HardPenLabel,
			I18nKey.UI.Bar.DrawAttributes.SoftPenLabel,I18nKey.UI.Bar.DrawAttributes.FreeLineLabel,
			I18nKey.UI.Bar.DrawAttributes.AnnotationLineLabel};
		for(const auto& language:Inkeys::Localization::Locales)
		{
			expect(I18n::load(1,L"JSON",language.code),"bar language loads for layout audit");
			expect(owner.RefreshLocalizedTypography(),"new locale revision refreshes typography without device recreation");
			owner.barButtonSet.RefreshLocalizedText();
			expect(owner.barButtonSet.GetMoreButton()->name.content.GetVal()==IW(I18nKey.UI.Bar.MainButtons.MoreLabel),
				"production label refresh updates More in place");
			expect(!owner.RefreshLocalizedTypography(),"consumed locale revision does not invalidate every frame");
			updateLocalizedPresetLabels();
			report << "[I18nLayout] " << language.codeUtf8 << '\n';
			auto reportBounds = [&](const char* key, const wstring& text, double fontSize,
				D2D1_SIZE_F bounds, double maxWidth, double maxHeight, const char* stage)
			{
				auto printable = utf16ToUtf8(text);
				for (size_t offset = 0; (offset = printable.find('\n', offset)) != string::npos; offset += 2)
					printable.replace(offset, 1, "\\n");
				report << "[I18nBounds] " << language.codeUtf8 << " stage=" << stage
					<< " key=" << key << " font=" << fontSize << " measured=" << bounds.width << 'x' << bounds.height
					<< " slot=" << maxWidth << 'x' << maxHeight << " text=" << printable << '\n';
			};
			ComPtr<IDWriteTextFormat> localizedFormat;
			const auto factory = RenderPipeline::DWriteFactory();
			const auto collection = RenderPipeline::FontCollection();
			const HRESULT formatHr = factory->CreateTextFormat(language.fontFamily, collection.Get(),
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				13.0F, language.code, &localizedFormat);
			expect(SUCCEEDED(formatHr), "actual selected family creates a localized DWrite format");
			if (localizedFormat)
			{
				for (const auto& [key, value] : I18n::i18n)
				{
					if (value.empty()) continue;
					const auto text = I18n::getW(key);
					ComPtr<IDWriteTextLayout> layout;
					MissingGlyphProbe probe;
					const bool created = SUCCEEDED(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
						localizedFormat.Get(), 16000.0F, 16000.0F, &layout));
					const bool complete = created && SUCCEEDED(layout->Draw(nullptr, &probe, 0, 0)) && !probe.missing;
					if (!complete) report << "[I18nGlyph] " << language.codeUtf8 << ' ' << key << '\n';
					expect(complete, "actual DWrite glyph runs contain no missing characters");
				}
			}

			expect(owner.spec.GetFontFamily()==language.fontFamily && owner.spec.GetTextLocale()==language.code,
				"localized font family and DWrite locale match language");
			{
				WhiteboardButtonStyleFixture style;
				auto* button=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Whiteboard];
				style.preset[(int)BarButtonPresetEnum::Whiteboard]=button;
				UiSVGClass resumeIcon,closeIcon;
				resumeIcon.InitializationFromResource(L"UI",L"barResumeWhiteboard");
				closeIcon.InitializationFromResource(L"UI",L"barCloseWhiteboard");
				expect(!resumeIcon.svg.GetVal().empty() && !closeIcon.svg.GetVal().empty(),
					"Whiteboard resume and close SVG resources are available for real content checks");
				const bool savedFold=owner.barState.fold;
				const auto geometry=owner.barButtonSet.MainLayoutSnapshot();
				for(bool folded:{false,true})for(bool minimized:{true,false,true,false})
				{
					owner.barState.fold=folded;
					// 直接消费生产内容更新入口，不投递hover、翻页或任何窗口消息。
					style.UpdateWhiteboardButtonStyle(true,minimized);
					button->name.AdvanceContentTransition(1.0,1.0);
					button->icon.AdvanceContentTransition(1.0,1.0);
					expect(button->name.content.GetVal()==IW(minimized?I18nKey.UI.Bar.MainButtons.ResumeWhiteboardLabel
						:I18nKey.UI.Bar.MainButtons.CloseWhiteboardLabel)
						&& button->icon.svg.GetVal()==(minimized?resumeIcon.svg.GetVal():closeIcon.svg.GetVal())
						&& owner.barButtonSet.MainLayoutSnapshot()==geometry,
						"Whiteboard minimize and restore change real label and SVG without depending on folding or layout changes");
				}
				owner.barState.fold=savedFold;
				style.UpdateWhiteboardButtonStyle(false,true);
				button->name.AdvanceContentTransition(1.0,1.0);
				expect(button->name.content.GetVal()==IW(I18nKey.UI.Bar.MainButtons.WhiteboardLabel),
					"inactive whiteboard ignores a stale minimized flag");
			}
			// 独立注册表模拟每种语言的冷启动，验证真实组件标签与元数据而非模板副本。
			BarButtonSetClass localizedComponents;
			localizedComponents.RegisterBuiltInComponents();
			const struct { const char* id; const char* category; const char* title; const char* label; } componentCases[]{
				{ "Component.ShortcutButton.Appliance.Explorer", I18nKey.SettingsUI.Component.SoftwareSection, I18nKey.SettingsUI.Component.ExplorerTitle, I18nKey.UI.Bar.Components.ExplorerLabel },
				{ "Component.ShortcutButton.Appliance.Taskmgr", I18nKey.SettingsUI.Component.SoftwareSection, I18nKey.SettingsUI.Component.TaskManagerTitle, I18nKey.UI.Bar.Components.TaskManagerLabel },
				{ "Component.ShortcutButton.Appliance.Control", I18nKey.SettingsUI.Component.SoftwareSection, I18nKey.SettingsUI.Component.ControlPanelTitle, I18nKey.UI.Bar.Components.ControlPanelLabel },
				{ "Component.ShortcutButton.System.Desktop", I18nKey.SettingsUI.Component.SystemSection, I18nKey.SettingsUI.Component.ShowDesktopTitle, I18nKey.UI.Bar.Components.ShowDesktopLabel },
				{ "Component.ShortcutButton.System.LockWorkStation", I18nKey.SettingsUI.Component.SystemSection, I18nKey.SettingsUI.Component.LockScreenTitle, I18nKey.UI.Bar.Components.LockScreenLabel },
				{ "Component.ShortcutButton.Keyboard.Keyboardesc", I18nKey.SettingsUI.Component.KeyboardSection, I18nKey.SettingsUI.Component.EscapeTitle, I18nKey.UI.Bar.Components.EscapeLabel },
				{ "Component.ShortcutButton.Keyboard.KeyboardAltF4", I18nKey.SettingsUI.Component.KeyboardSection, I18nKey.SettingsUI.Component.AltF4Title, nullptr },
				{ "Component.ShortcutButton.RollCall.IslandCaller1", I18nKey.SettingsUI.Component.RollCallSection, I18nKey.SettingsUI.Component.IslandCaller1Title, I18nKey.UI.Bar.Components.RollCallLabel },
				{ "Component.ShortcutButton.RollCall.IslandCaller2", I18nKey.SettingsUI.Component.RollCallSection, I18nKey.SettingsUI.Component.IslandCaller2Title, I18nKey.UI.Bar.Components.RollCallLabel },
				{ "Component.ShortcutButton.RollCall.SecRandom1", I18nKey.SettingsUI.Component.RollCallSection, I18nKey.SettingsUI.Component.SecRandom1Title, I18nKey.UI.Bar.Components.RollCallLabel },
				{ "Component.ShortcutButton.RollCall.SecRandom2", I18nKey.SettingsUI.Component.RollCallSection, I18nKey.SettingsUI.Component.SecRandom2Title, I18nKey.UI.Bar.Components.RollCallLabel },
				{ "Component.ShortcutButton.RollCall.SecRandom2Compat", I18nKey.SettingsUI.Component.RollCallSection, I18nKey.SettingsUI.Component.SecRandom2CompatTitle, I18nKey.UI.Bar.Components.RollCallLabel },
				{ "Component.ShortcutButton.RollCall.NamePicker", I18nKey.SettingsUI.Component.RollCallSection, I18nKey.SettingsUI.Component.NamePickerTitle, I18nKey.UI.Bar.Components.RollCallLabel },
				{ "Component.ShortcutButton.Linkage.ClassislandSettings", I18nKey.SettingsUI.Component.ClassIslandSection, I18nKey.SettingsUI.Component.ClassIslandSettingsTitle, I18nKey.UI.Bar.Components.ClassIslandSettingsLabel },
				{ "Component.ShortcutButton.Linkage.ClassislandProfile", I18nKey.SettingsUI.Component.ClassIslandSection, I18nKey.SettingsUI.Component.ClassIslandProfileTitle, I18nKey.UI.Bar.Components.ClassIslandProfileLabel },
				{ "Component.ShortcutButton.Linkage.ClassislandClassswap", I18nKey.SettingsUI.Component.ClassIslandSection, I18nKey.SettingsUI.Component.ClassIslandSwapTitle, I18nKey.UI.Bar.Components.ClassIslandSwapLabel },
			};
			for(const auto& componentCase:componentCases)
			{
				BarButtonRegistrationClass registration;
				const bool found=localizedComponents.TryGetRegistration(componentCase.id,registration) && registration.button;
				expect(found,"actual built-in component registers in each locale");
				if(!found)continue;
				const auto text=registration.button->name.content.GetTar();
				expect(text==(componentCase.label?IW(componentCase.label):L"Alt+F4")
					&& registration.categoryName==IW(componentCase.category) && registration.settingsName==IW(componentCase.title),
					"actual component label and settings metadata use the selected locale");
				const double fontSize=owner.spec.FitButtonLabelFontSize(text,metrics.primaryFontSizeDip,metrics.primarySlotWidthDip,metrics.primarySlotHeightDip);
				const auto bounds=owner.spec.MeasureText(text,fontSize,DWRITE_FONT_WEIGHT_BOLD);
				if(bounds.width>metrics.primarySlotWidthDip+0.5f)reportBounds(componentCase.label?componentCase.label:componentCase.id,text,fontSize,bounds,metrics.primarySlotWidthDip,metrics.primarySlotHeightDip,"component");
				expect(bounds.width<=metrics.primarySlotWidthDip+0.5f,"localized component fits its unchanged toolbar slot");
			}
			for(const auto* key:fixedLabelKeys)
			{
				const auto text=IW(key);
				const double fontSize=owner.spec.FitButtonLabelFontSize(text,metrics.primaryFontSizeDip,metrics.primarySlotWidthDip,metrics.primarySlotHeightDip);
				const auto bounds=owner.spec.MeasureText(text,fontSize,DWRITE_FONT_WEIGHT_BOLD);
				const bool fits=bounds.width<=metrics.primarySlotWidthDip+0.5f;
				if(!fits)reportBounds(key,text,fontSize,bounds,metrics.primarySlotWidthDip,metrics.primarySlotHeightDip,"fixed");
				expect(fits,"localized fixed label fits the existing 70 DIP slot");
			}
			for(const auto* key:{I18nKey.UI.Bar.MainButtons.WhiteboardLabel,I18nKey.UI.Bar.MainButtons.FreezeLabel})
			{
				const auto text=IW(key);
				const double fontSize=owner.spec.FitButtonLabelFontSize(text,twoOneMetrics.primaryFontSizeDip,twoOneMetrics.primarySlotWidthDip,twoOneMetrics.primarySlotHeightDip);
				const auto bounds=owner.spec.MeasureText(text,fontSize,DWRITE_FONT_WEIGHT_BOLD);
				const bool fits=bounds.width<=twoOneMetrics.primarySlotWidthDip+0.5f;
				if(!fits)reportBounds(key,text,fontSize,bounds,twoOneMetrics.primarySlotWidthDip,twoOneMetrics.primarySlotHeightDip,"compact");
				expect(fits,"localized Whiteboard and Freeze labels fit the existing 37 DIP slot");
			}
			for(const auto preset:{BarButtonPresetEnum::Select,BarButtonPresetEnum::Pierce})
			{
				auto* b=owner.barButtonSet.preset[(int)preset];
				const auto* key=preset==BarButtonPresetEnum::Select ? I18nKey.UI.Bar.MainButtons.SelectClearLabel
					: I18nKey.UI.Bar.MainButtons.ClickThroughLabel;
				auto label=IW(key);
				// 直接测量真实语言资源，不在测试里重复生产代码的英文改写。
				expect(std::count(label.begin(),label.end(),L'\n')<=1,
					"localized short labels keep the resource contract of at most two lines");
				b->name.SetStringImmediate(label);b->TransitionContent(L"",label);
				for(const auto kind:{ButtonVisualLayoutKind::StandardTwoTwo,ButtonVisualLayoutKind::StandardTwoOne})
				{
					if(preset==BarButtonPresetEnum::Select && kind==ButtonVisualLayoutKind::StandardTwoOne)continue;
					const auto slot=ResolveButtonVisualMetrics(kind);
					const double fittedSize=owner.spec.FitButtonLabelFontSize(label,b->LabelFontSize(slot.primaryFontSizeDip),slot.primarySlotWidthDip,slot.primarySlotHeightDip);
					const auto measured=owner.spec.MeasureText(label,fittedSize,DWRITE_FONT_WEIGHT_BOLD);
					const bool fits=measured.width>0 && measured.width<=slot.primarySlotWidthDip+0.5f &&
						measured.height>0 && measured.height<=slot.primarySlotHeightDip+0.5f;
					if(!fits)reportBounds(key,label,fittedSize,measured,slot.primarySlotWidthDip,slot.primarySlotHeightDip,"short");
					expect(fits,"localized Select Clear and Click-through DWrite bounds fit original slots");
					// 只有 Select 在短标签与清空标签之间换字；Pierce 实际只保持同一穿透文案。
					if(label.find(L'\n')!=wstring::npos && preset==BarButtonPresetEnum::Select)
					{
						// 真实换字过渡的中点前后都测量当前字号，防止目标文案提前裁切。
						BarButtonClass transitioning(*b);
						const auto shortLabel=IW(I18nKey.UI.Bar.MainButtons.SelectLabel);
						transitioning.name.SetStringImmediate(shortLabel);
						transitioning.name.size.SetDirect(slot.primaryFontSizeDip);
						transitioning.TransitionContent(L"",label);
						for(const double dt:{0.0,0.19,0.02,0.05,0.5})
						{
							transitioning.name.AdvanceContentTransition(dt,1.0);
							const double currentSize=owner.spec.FitButtonLabelFontSize(transitioning.name.content.GetVal(),transitioning.name.size.val,slot.primarySlotWidthDip,slot.primarySlotHeightDip);
							const auto bounds=owner.spec.MeasureText(transitioning.name.content.GetVal(),currentSize,DWRITE_FONT_WEIGHT_BOLD);
							const bool transitionFits=bounds.width<=slot.primarySlotWidthDip+0.5f && bounds.height<=slot.primarySlotHeightDip+0.5f;
							if(!transitionFits)reportBounds(key,transitioning.name.content.GetVal(),currentSize,bounds,slot.primarySlotWidthDip,slot.primarySlotHeightDip,"transition");
							expect(transitionFits,"incoming multiline label fits before, at and after the transparent midpoint");
						}
						transitioning.TransitionContent(L"",shortLabel);
						expect(transitioning.LabelFontSize(slot.primaryFontSizeDip)==9.0,
							"outgoing multiline label keeps compact font until content changes");
						transitioning.name.AdvanceContentTransition(1.0,1.0);
						expect(transitioning.LabelFontSize(slot.primaryFontSizeDip)==slot.primaryFontSizeDip,
							"short label restores standard font after content transition");
					}
					else if(preset==BarButtonPresetEnum::Pierce)
					{
						BarButtonClass stable(*b);stable.name.SetStringImmediate(label);
						expect(!stable.TransitionContent(L"",label),"Pierce keeps its actual localized content when refreshed");
					}
				}
			}
			updateLocalizedPresetLabels();
			for(const auto* key:penMenuLabelKeys)
			{
				const auto text=IW(key);const auto bounds=owner.spec.MeasureText(text,12.0,DWRITE_FONT_WEIGHT_NORMAL);
				if(bounds.width>80.5f)reportBounds(key,text,12.0,bounds,80.0,0.0,"pen-menu");
				expect(bounds.width<=80.5f,"localized pen menu label fits its measured text slot");
			}
			{
				// P2按实际语言测量同宽列；使用生产布局的文字槽，保留严格宽度约束。
				const bool savedGeometry = owner.barState.geometryAttribute, savedDraw = owner.barState.drawAttribute;
				const bool savedEraser = owner.barState.eraserAttribute, savedRecall = owner.barState.recallAttribute;
				const bool savedMore = owner.barState.moreExpanded, savedFold = owner.barState.fold;
				owner.barState.geometryAttribute = true; owner.barState.fold = false;
				owner.barState.drawAttribute = owner.barState.eraserAttribute = owner.barState.recallAttribute = owner.barState.moreExpanded = false;
				auto shapeFrame = GetStateModeSnapshot(); shapeFrame.StateModeSelect = StateModeSelectEnum::IdtShape;
				auto panel = std::make_unique<BarGeometryAttributePanel>();
				for (int frame = 0; frame < 90; ++frame) panel->Advance(owner, shapeFrame, 1.0 / 60, 1, 1, {0,0,1000,620}, {}, 0, 0);
				panel->CommitPresented(); const auto geometry = panel->PresentationSnapshot();
				for (const int index : {0,2})
				{
					const auto key = index == 0 ? I18nKey.UI.Bar.GeometryAttributes.StraightLineLabel : I18nKey.UI.Bar.GeometryAttributes.RectangleLabel;
					const double slot = geometry.items[index].Width() - 4 * geometry.pose.scale;
					expect(slot > 0 && owner.spec.MeasureText(IW(key), 11 * geometry.pose.scale, DWRITE_FONT_WEIGHT_NORMAL).width <= slot,
						"localized geometry label fits its actual production text slot");
				}
				panel->Close(owner);
				owner.barState.geometryAttribute = savedGeometry; owner.barState.drawAttribute = savedDraw;
				owner.barState.eraserAttribute = savedEraser; owner.barState.recallAttribute = savedRecall;
				owner.barState.moreExpanded = savedMore; owner.barState.fold = savedFold;
			}
			const auto annotationBody=owner.spec.MeasureText(IW(I18nKey.UI.Bar.DrawAttributes.AnnotationDescription),BarThicknessTooltipBodyFontSize,DWRITE_FONT_WEIGHT_NORMAL);
			const auto overflowBody=owner.spec.MeasureText(IW(I18nKey.UI.Bar.DrawAttributes.ThicknessOverflowBody),BarThicknessTooltipBodyFontSize,DWRITE_FONT_WEIGHT_NORMAL);
			for(const auto* key:{I18nKey.UI.Bar.DrawAttributes.AnnotationUnavailableTitle,I18nKey.UI.Bar.DrawAttributes.AnnotationFixedUnsupportedTitle})
			{
				const auto title=owner.spec.MeasureText(IW(key),BarThicknessTooltipTitleFontSize,DWRITE_FONT_WEIGHT_SEMI_BOLD);
				const double width=std::ceil((std::max)(title.width,annotationBody.width))+BarThicknessTooltipPadding*2+BarThicknessTooltipCloseReserve;
				if(!(width>0 && width<=1000))reportBounds(key,IW(key),BarThicknessTooltipTitleFontSize,title,1000,0,"annotation-popup");
				expect(width>0 && width<=1000,"localized annotation popup expands within the offscreen work area");
			}
			const auto annotationTitleText=IW(I18nKey.UI.Bar.DrawAttributes.AnnotationFixedUnsupportedTitle);
			const auto annotationBodyText=IW(I18nKey.UI.Bar.DrawAttributes.AnnotationDescription);
			const auto annotationTitle=owner.spec.MeasureText(annotationTitleText,BarThicknessTooltipTitleFontSize,DWRITE_FONT_WEIGHT_SEMI_BOLD);
			const double annotationWidth=std::ceil((std::max)(annotationTitle.width,annotationBody.width))+BarThicknessTooltipPadding*2+BarThicknessTooltipCloseReserve;
			const double annotationHeight=std::ceil(annotationTitle.height+BarThicknessTooltipLineGap+annotationBody.height)+BarThicknessTooltipPadding*2;
			const auto overflowTitleText=IW(I18nKey.UI.Bar.DrawAttributes.ThicknessOverflowTitle);
			const auto overflowBodyText=IW(I18nKey.UI.Bar.DrawAttributes.ThicknessOverflowBody);
			const auto overflowTitle=owner.spec.MeasureText(IW(I18nKey.UI.Bar.DrawAttributes.ThicknessOverflowTitle),BarThicknessTooltipTitleFontSize,DWRITE_FONT_WEIGHT_SEMI_BOLD);
			const double overflowWidth=std::ceil((std::max)(overflowTitle.width,overflowBody.width))+BarThicknessTooltipPadding*2+BarThicknessTooltipCloseReserve;
			const double overflowHeight=std::ceil(overflowTitle.height+BarThicknessTooltipLineGap+overflowBody.height)+BarThicknessTooltipPadding*2;
			if(!(overflowWidth>0 && overflowWidth<=1000))reportBounds(I18nKey.UI.Bar.DrawAttributes.ThicknessOverflowTitle,overflowTitleText,BarThicknessTooltipTitleFontSize,overflowTitle,1000,0,"overflow-popup");
			expect(overflowWidth>0 && overflowWidth<=1000,"localized overflow popup expands within the offscreen work area");
			const double rgbLabelWidth=(std::max)({owner.spec.MeasureText(IW(I18nKey.UI.Bar.ColorPicker.RedChannelLabel),13.0).width,
				owner.spec.MeasureText(IW(I18nKey.UI.Bar.ColorPicker.GreenChannelLabel),13.0).width,
				owner.spec.MeasureText(IW(I18nKey.UI.Bar.ColorPicker.BlueChannelLabel),13.0).width});
			const double rgbValueWidth=owner.spec.MeasureText(L"255",13.0).width;
			const double opacityLabelWidth=owner.spec.MeasureText(IW(I18nKey.UI.Bar.ColorPicker.OpacityLabel),13.0).width;
			const double opacityValueWidth=owner.spec.MeasureText(L"100%",13.0).width;
			const double footerRequiredWidth=(rgbLabelWidth+rgbValueWidth)*3+opacityLabelWidth+opacityValueWidth+4*3.0+2*6.0+10.0;
			if(footerRequiredWidth>ColorPickerPanelWidth)reportBounds(I18nKey.UI.Bar.ColorPicker.OpacityLabel,IW(I18nKey.UI.Bar.ColorPicker.OpacityLabel),13.0,D2D1::SizeF(static_cast<float>(footerRequiredWidth),0),ColorPickerPanelWidth,0,"color-footer");
			expect(footerRequiredWidth<=ColorPickerPanelWidth,"localized color footer keeps RGB and opacity columns separate");

			for(const double zoom:{0.65,1.0,1.5})
			{
				owner.barStyle.darkStyle=true;owner.barStyle.zoom=zoom;
				const UINT width=static_cast<UINT>(700*zoom),height=static_cast<UINT>(245*zoom);
				expect(SUCCEEDED(owner.specTarget.Ensure(RenderPipeline::GetDeviceEpoch(),width,height)),"localized bar target setup");
				owner.spec.SetFrameZoom(zoom);owner.PrepareFrameLighting(1.0/60,{}, ResolveMainBarFrameLightingInput(&owner,static_cast<int>(StateModeSelectEnum::IdtEraser),0,0,0));
				auto* dc=owner.specTarget.DeviceContext();dc->BeginDraw();dc->SetTransform(D2D1::IdentityMatrix());dc->Clear(D2D1::ColorF(0.13f,0.14f,0.16f,1));
				main->fill->SetDirect(GetThemeColor(ThemeColorEnum::Surface));main->frame->SetDirect(GetThemeColor(ThemeColorEnum::SurfaceFrame));
				DrawBackgroundVisual(owner.spec,dc,*main,UiInheritClass(main->inhX,main->inhY));
				for(const auto preset:presets)
				{
					auto* b=preset==BarButtonPresetEnum::More?owner.barButtonSet.GetMoreButton():owner.barButtonSet.preset[static_cast<int>(preset)];
					b->name.color.SetDirect(GetThemeColor(ThemeColorEnum::TextPrimary));b->icon.color1->SetDirect(GetThemeColor(ThemeColorEnum::TextPrimary));
					DrawButtonVisual(owner.spec,dc,*b,b->button.Inherit(UiInheritEnum::CenterFromTopLeft,*main));
				}
				auto drawAuditSurface=[&](double x,double y,double w,double h)
				{
					UiShapeClass surface(0,0,w,h,8,8,1,GetThemeColor(ThemeColorEnum::Surface),GetThemeColor(ThemeColorEnum::SurfaceFrame));
					surface.enable.Initialization(true);surface.pct.SetDirect(0.96);surface.framePct.emplace(0.18);surface.frameLightPct.emplace(0);
					owner.spec.Shape(dc,surface,UiInheritClass(x,y));
				};
				auto drawAuditText=[&](const std::wstring& text,double x,double y,double w,double h,double size,DWRITE_FONT_WEIGHT weight=DWRITE_FONT_WEIGHT_NORMAL)
				{
					UiWordClass word(0,0,w,h,text,size,GetThemeColor(ThemeColorEnum::TextPrimary));
					word.enable.Initialization(true);word.pct.SetDirect(1);
					owner.spec.Word(dc,word,UiInheritClass(x,y),weight,DWRITE_TEXT_ALIGNMENT_LEADING);
				};
				const double annotationLeft=10,annotationTop=95;
				drawAuditSurface(annotationLeft,annotationTop,annotationWidth,annotationHeight);
				drawAuditText(annotationTitleText,annotationLeft+BarThicknessTooltipPadding,annotationTop+BarThicknessTooltipPadding,
					annotationWidth-BarThicknessTooltipPadding*2-BarThicknessTooltipCloseReserve,annotationTitle.height,
					BarThicknessTooltipTitleFontSize,DWRITE_FONT_WEIGHT_SEMI_BOLD);
				drawAuditText(annotationBodyText,annotationLeft+BarThicknessTooltipPadding,
					annotationTop+BarThicknessTooltipPadding+annotationTitle.height+BarThicknessTooltipLineGap,
					annotationWidth-BarThicknessTooltipPadding*2,annotationBody.height,BarThicknessTooltipBodyFontSize);
				const double overflowLeft=10,overflowTop=170;
				drawAuditSurface(overflowLeft,overflowTop,overflowWidth,overflowHeight);
				drawAuditText(overflowTitleText,overflowLeft+BarThicknessTooltipPadding,overflowTop+BarThicknessTooltipPadding,
					overflowWidth-BarThicknessTooltipPadding*2-BarThicknessTooltipCloseReserve,overflowTitle.height,
					BarThicknessTooltipTitleFontSize,DWRITE_FONT_WEIGHT_SEMI_BOLD);
				drawAuditText(overflowBodyText,overflowLeft+BarThicknessTooltipPadding,
					overflowTop+BarThicknessTooltipPadding+overflowTitle.height+BarThicknessTooltipLineGap,
					overflowWidth-BarThicknessTooltipPadding*2,overflowBody.height,BarThicknessTooltipBodyFontSize);
				const double footerLeft=390,footerTop=170,footerHeight=55,footerPadding=5,footerLabelGap=3,footerColumnGap=6;
				drawAuditSurface(footerLeft,footerTop,ColorPickerPanelWidth,footerHeight);
				const double rgbColumnWidth=rgbLabelWidth+footerLabelGap+rgbValueWidth;
				const double rX=footerLeft+footerPadding,gX=rX+rgbColumnWidth+footerColumnGap,bX=gX+rgbColumnWidth+footerColumnGap;
				const double opacityX=footerLeft+ColorPickerPanelWidth-footerPadding-opacityLabelWidth-footerLabelGap-opacityValueWidth;
				const std::array<const char*,4> footerLabelKeys{I18nKey.UI.Bar.ColorPicker.RedChannelLabel,I18nKey.UI.Bar.ColorPicker.GreenChannelLabel,I18nKey.UI.Bar.ColorPicker.BlueChannelLabel,I18nKey.UI.Bar.ColorPicker.OpacityLabel};
				const std::array<double,4> footerLabelX{rX,gX,bX,opacityX};
				const std::array<const wchar_t*,4> footerValues{L"255",L"255",L"255",L"100%"};
				for(size_t i=0;i<footerLabelKeys.size();++i)
				{
					const double labelWidth=i<3?rgbLabelWidth:opacityLabelWidth;
					const double valueWidth=i<3?rgbValueWidth:opacityValueWidth;
					drawAuditText(IW(footerLabelKeys[i]),footerLabelX[i],footerTop,labelWidth,footerHeight,13);
					drawAuditText(footerValues[i],footerLabelX[i]+labelWidth+footerLabelGap,footerTop,valueWidth,footerHeight,13);
				}
				expect(SUCCEEDED(dc->EndDraw()),"localized fixed bar renders");
				const auto file=std::filesystem::path(L"Build/eraser-b/visuals")/(L"bar-i18n-"+std::wstring(language.code)+L"-"+std::to_wstring(static_cast<int>(zoom*100))+L".png");
				expect(SUCCEEDED(SaveEraserTestPng(dc,owner.specTarget.TargetBitmap(),file)),"localized fixed bar PNG readback");
			}
		}
		expect(I18n::load(1,L"JSON",L"zh-CN"),"restore default test language");owner.ConfigureLocalizedTypography();updateLocalizedPresetLabels();
		const auto preferencesBeforeOpen=EraserPreferencesSnapshot();
		auto* eraserButton=owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Eraser)];
		stateMode.StateModeSelect=StateModeSelectEnum::IdtPen;
		eraserButton->clickFunc();
		expect(stateMode.StateModeSelect==StateModeSelectEnum::IdtEraser && !owner.barState.eraserAttribute,"first eraser click selects tool only");
		owner.barState.drawAttribute=true;owner.barState.geometryAttribute=true;owner.barState.moreExpanded=true;
		eraserButton->clickFunc();
		expect(owner.barState.eraserAttribute && !owner.barState.drawAttribute && !owner.barState.geometryAttribute && !owner.barState.moreExpanded,"immediate second click opens eraser and closes other panels");
		std::this_thread::sleep_for(std::chrono::milliseconds(310));eraserButton->clickFunc();
		expect(!owner.barState.eraserAttribute && EraserPreferencesSnapshot()==preferencesBeforeOpen,"next toggle closes without changing eraser configuration");
		for(int scenario=0;scenario<21;++scenario)
		{
			const bool dark=(scenario%2)==0;
			const UINT dpis[]={96,144,192};const double scales[]={0.65,1.0,1.5};
			const UINT dpi=scenario<18?dpis[scenario/6]:192;
			const double ui=scenario<18?scales[((scenario/2)+1)%3]:scenario==18?0.35:1.0,zoom=dpi/96.0*ui;
			expect(I18n::load(1,L"JSON",scenario==19?L"en-US":scenario==20?L"zh-TW":L"zh-CN"),"bundled translations load");
			owner.ConfigureLocalizedTypography();updateLocalizedPresetLabels();
			BarUiEdgeLightingEnabled=scenario!=16;BarUiDynamicEdgeLightingEnabled=scenario!=17;
			owner.barStyle.darkStyle=dark;owner.barStyle.zoom=zoom;
			owner.barState.eraserAttribute=true;owner.barState.eraserSensitivityOpen=true;
			owner.barState.widgetPosition.mainBar=scenario!=3;
			owner.barState.widgetPosition.primaryBar=scenario==3;
			root->y.SetDirect(scenario==3?140:430);
			Inkeys::config.Drawing.Eraser.Automatic=scenario!=2;
			const UINT width=static_cast<UINT>(1000*zoom),height=static_cast<UINT>(620*zoom);
			const auto epoch=RenderPipeline::GetDeviceEpoch();
			expect(SUCCEEDED(owner.specTarget.Ensure(epoch,width,height)),"shared renderer resource setup");
			owner.spec.SetFrameZoom(zoom);owner.PrepareFrameLighting(1.0/60,{}, ResolveMainBarFrameLightingInput(&owner,static_cast<int>(StateModeSelectEnum::IdtEraser),0,0,0));
			for(int f=0;f<90;++f)owner.eraserAttribute.Advance(owner,1.0/60,1,zoom,dpi,{0,0,static_cast<LONG>(width),static_cast<LONG>(height)},{0,0},0,0);
			expect(!owner.eraserAttribute.Active(),"animation settles without polling");
			owner.eraserAttribute.CommitPresented();const auto presentation=owner.eraserAttribute.PresentationSnapshot();
			auto lighting=owner.spec.SnapshotFrameLighting();
			lighting.cursorLight=D2D1::Point2F(static_cast<float>(EraserRectCenterX(presentation.geometry.automatic)*zoom),static_cast<float>(EraserRectCenterY(presentation.geometry.automatic)*zoom));
			lighting.cursorRadius=static_cast<float>(240*zoom);lighting.cursorIntensity=scenario==17?0.0f:1.0f;lighting.cursorLightVisible=scenario!=17;
			owner.spec.SetFrameLightingSnapshot(lighting, BarUiEdgeLightingEnabled);owner.spec.SetFrameCursorLightLocalGeometry(lighting.cursorLight,D2D1::SizeF(lighting.cursorRadius,lighting.cursorRadius));
			auto* dc=owner.specTarget.DeviceContext();dc->BeginDraw();dc->SetTransform(D2D1::IdentityMatrix());
			dc->Clear(dark?D2D1::ColorF(0.13f,0.14f,0.16f,1):D2D1::ColorF(0.91f,0.93f,0.95f,1));
			owner.eraserAttribute.Draw(owner.spec,dc);
			main->fill->SetDirect(GetThemeColor(ThemeColorEnum::Surface));main->frame->SetDirect(GetThemeColor(ThemeColorEnum::SurfaceFrame));
			DrawBackgroundVisual(owner.spec,dc,*main,UiInheritClass(main->inhX,main->inhY));
			for(const auto preset:presets)
			{
				auto* b=preset==BarButtonPresetEnum::More?owner.barButtonSet.GetMoreButton():owner.barButtonSet.preset[static_cast<int>(preset)];
				b->name.color.SetDirect(GetThemeColor(ThemeColorEnum::TextPrimary));b->icon.color1->SetDirect(GetThemeColor(ThemeColorEnum::TextPrimary));
				DrawButtonVisual(owner.spec,dc,*b,b->button.Inherit(UiInheritEnum::CenterFromTopLeft,*main));
			}
			expect(SUCCEEDED(dc->EndDraw()),"production panel renders successfully");
			owner.eraserAttribute.CommitPresented();
			const auto path=std::filesystem::path(L"Build/eraser-b/visuals")/(std::to_wstring(scenario)+(dark?L"-dark.png":L"-light.png"));
			expect(SUCCEEDED(SaveEraserTestPng(dc,owner.specTarget.TargetBitmap(),path)),"PNG readback succeeds");
			report<<"[EraserVisual] "<<path.string()<<" dpi="<<dpi<<" ui="<<ui<<'\n';
			for(size_t i=0;i<3;++i)
			{
				const auto rect=presentation.geometry.previews[i];
				expect(std::abs(rect.Width()*zoom-static_cast<int>(Inkeys::Drawing::Draw3::SpeedEraser::BaseSizePresets[i])*dpi/96.0)<0.001,"actual stable presented preview matches tool pixels");
				const auto pixel=ReadEraserTestPixel(dc,owner.specTarget.TargetBitmap(),static_cast<UINT>(EraserRectCenterX(rect)*zoom),static_cast<UINT>(EraserRectCenterY(rect)*zoom));
				expect(pixel[0]>=250 && pixel[1]>=250 && pixel[2]>=250,"Contact white remains opaque at all theme/DPI/UI scales");
			}
			expect(ERASER_GRIP_OPACITY==0.5f,"canvas Hover opacity remains unchanged");
			if(scenario==0)
			{
				EraserAttributeLayoutInput input;input.main={main->inhX,main->inhY,main->inhX+main->w.val,main->inhY+main->h.val};
				auto* anchor=owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Eraser)];
				input.anchor={anchor->button.inhX,anchor->button.inhY,anchor->button.inhX+anchor->button.w.val,anchor->button.inhY+anchor->button.h.val};
				input.work={0,0,1000,620};const auto layout=ResolveEraserAttributeLayout(input);
				const auto circle=layout.items[2];
				const auto white=ReadEraserTestPixel(dc,owner.specTarget.TargetBitmap(),static_cast<UINT>((circle.left+circle.right)/2),static_cast<UINT>((circle.top+circle.bottom)/2));
				expect(white[0]>=250 && white[1]>=250 && white[2]>=250,"stable preview uses opaque Contact white, not Hover alpha0.5");
				auto click=[&](int id,bool cancel=false)
				{
					const auto rect=layout.items[id];ExMessage m{};m.x=static_cast<short>((rect.left+rect.right)/2);m.y=static_cast<short>((rect.top+rect.bottom)/2);
					m.message=WM_LBUTTONDOWN;m.lbutton=true;expect(owner.eraserAttribute.Pointer(owner,m),"control accepts down");
					bool accepted=false;
					m.message=WM_LBUTTONUP;m.lbutton=false;expect(owner.eraserAttribute.Pointer(owner,m,cancel,false,&accepted),"control accepts up");
					return accepted;
				};
				click(8);expect(EraserPreferencesSnapshot().sensitivity==Inkeys::Drawing::Draw3::SpeedEraser::Sensitivity::High && owner.barState.eraserSensitivityOpen,"sensitivity updates without closing menu");
				expect(!click(9,true),"cancelled settings gesture is not accepted");
				expect(click(9) && owner.barState.eraserSensitivityOpen
					&& EraserPreferencesSnapshot().sensitivity==Inkeys::Drawing::Draw3::SpeedEraser::Sensitivity::High,
					"enabled settings accepts the pointer without changing eraser preferences");
				const auto beforeCorner=EraserPreferencesSnapshot().baseSize;const auto smallHit=layout.items[1];ExMessage corner{};
				corner.x=static_cast<short>(smallHit.left+0.5);corner.y=static_cast<short>(smallHit.top+0.5);
				corner.message=WM_LBUTTONDOWN;corner.lbutton=true;owner.eraserAttribute.Pointer(owner,corner);
				corner.message=WM_LBUTTONUP;corner.lbutton=false;owner.eraserAttribute.Pointer(owner,corner);
				expect(EraserPreferencesSnapshot().baseSize==beforeCorner,"circle bounding-box corner is not clickable");
				ExMessage hover{};hover.message=WM_MOUSEMOVE;hover.x=static_cast<short>(EraserRectCenterX(layout.previews[0]));hover.y=static_cast<short>(EraserRectCenterY(layout.previews[0]));
				owner.eraserAttribute.Pointer(owner,hover);for(int f=0;f<20;++f)owner.eraserAttribute.Advance(owner,1.0/60,1,zoom,dpi,{0,0,static_cast<LONG>(width),static_cast<LONG>(height)},{0,0},0,0);
				owner.eraserAttribute.CommitPresented();const auto hoverRegions=owner.eraserAttribute.PresentedRegions();
				expect(hoverRegions[2].right==hoverRegions[2].left && hoverRegions[2].bottom==hoverRegions[2].top,"hover no longer publishes a rectangular content tooltip");
				click(1);expect(EraserPreferencesSnapshot().baseSize==Inkeys::Drawing::Draw3::SpeedEraser::BaseSize::Small && owner.barState.eraserAttribute && !owner.barState.eraserSensitivityOpen,"outside menu click closes child then applies preset once");
				click(3,true);expect(EraserPreferencesSnapshot().baseSize==Inkeys::Drawing::Draw3::SpeedEraser::BaseSize::Small,"cancelled pointer does not select");
				click(4);expect(Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())==Inkeys::Drawing::Draw3::SpeedEraser::AutomaticState::Off && owner.barState.eraserAttribute,"body toggles only the master gate and stays open");
				click(5);expect(owner.barState.eraserSensitivityOpen && Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())==Inkeys::Drawing::Draw3::SpeedEraser::AutomaticState::Off,"arrow opens while auto is off without toggling");
				expect(Inkeys::Drawing::Draw3::PublishProductCommand(
					Inkeys::Drawing::Draw3::Bridge::CommandType::Clear) ==
					Inkeys::Drawing::Draw3::Bridge::CommandResult::NotRunning,
					"unstarted Draw3 product rejects clear command");
				click(0);expect(owner.barState.eraserAttribute,"rejected empty clear stays open without changing tool");
				Inkeys::config.Drawing.Eraser.MouseLeft=0;Inkeys::config.Drawing.Eraser.MouseRight=1;
				click(4);const auto restored=EraserPreferencesSnapshot();
				expect(Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(restored)==Inkeys::Drawing::Draw3::SpeedEraser::AutomaticState::On &&
					restored.entries[0].kind==Inkeys::Drawing::Draw3::SpeedEraser::EraserKind::Fixed &&
					restored.entries[1].kind==Inkeys::Drawing::Draw3::SpeedEraser::EraserKind::Speed,
					"reenabling master gate preserves mixed per-entry preferences");
				Inkeys::config.Drawing.Eraser.BaseDiameterDip=32;Inkeys::config.Drawing.Eraser.Sensitivity=1;
				owner.barState.eraserSensitivityOpen=true;
			}
			owner.eraserAttribute.Close(owner);
			for(int f=0;f<90;++f)owner.eraserAttribute.Advance(owner,1.0/60,1,zoom,dpi,{0,0,static_cast<LONG>(width),static_cast<LONG>(height)},{0,0},0,0);
			owner.eraserAttribute.CommitPresented();
			const auto regions=owner.eraserAttribute.PresentedRegions();expect(regions[0].right==regions[0].left,"closed panel has no hit region");
		}
		// 窄工作区保持生产正常布局并裁剪溢出，截图确认不会切入紧凑留白。
		{
			constexpr UINT narrowWidth=300,narrowHeight=620;
			I18n::load(1,L"JSON",L"zh-CN");owner.ConfigureLocalizedTypography();updateLocalizedPresetLabels();owner.barStyle.darkStyle=true;owner.barStyle.zoom=1;
			owner.barState.widgetPosition.mainBar=true;owner.barState.widgetPosition.primaryBar=false;
			owner.barState.eraserAttribute=true;owner.barState.eraserSensitivityOpen=false;
			Inkeys::config.Drawing.Eraser.Automatic=true;
			root->x.SetDirect(-70);root->y.SetDirect(430);
			const auto epoch=RenderPipeline::GetDeviceEpoch();
			expect(SUCCEEDED(owner.specTarget.Ensure(epoch,narrowWidth,narrowHeight)),"narrow target setup");
			owner.spec.SetFrameZoom(1);owner.PrepareFrameLighting(1.0/60,{}, ResolveMainBarFrameLightingInput(&owner,static_cast<int>(StateModeSelectEnum::IdtEraser),0,0,0));
			for(int f=0;f<90;++f)owner.eraserAttribute.Advance(owner,1.0/60,1,1,96,{0,0,narrowWidth,narrowHeight},{0,0},0,0);
			owner.eraserAttribute.CommitPresented();const auto narrow=owner.eraserAttribute.PresentationSnapshot();
			const double narrowCircleEndGap=narrow.geometry.reversed
				?narrow.geometry.previews[0].left-narrow.geometry.dividers[1].right
				:narrow.geometry.dividers[0].left-narrow.geometry.previews[2].right;
			const double narrowAutomaticGap=narrow.geometry.reversed
				?narrow.geometry.dividers[0].left-narrow.geometry.palm.right
				:narrow.geometry.automatic.left-narrow.geometry.dividers[1].right;
			expect(narrow.panelVisible && narrow.geometry.horizontalOverflow && narrow.geometry.panel.Width()<=narrowWidth &&
				std::abs((narrow.geometry.previews[1].left-narrow.geometry.previews[0].right)-EraserAttributeCircleGapDip)<0.001 &&
				std::abs((narrow.geometry.previews[2].left-narrow.geometry.previews[1].right)-EraserAttributeCircleGapDip)<0.001 &&
				std::abs(narrowCircleEndGap-EraserAttributeCircleGapDip)<0.001 &&
				std::abs(narrow.geometry.items[0].left-narrow.geometry.dividers[0].right-EraserAttributeDividerGapDip)<0.001 &&
				std::abs(narrow.geometry.dividers[1].left-narrow.geometry.items[0].right-EraserAttributeDividerGapDip)<0.001 &&
				std::abs(narrowAutomaticGap-EraserAttributeDividerGapDip)<0.001 &&
				std::abs(narrow.geometry.automatic.Width()-ButtonTwoSideDip-EraserAttributeAutomaticArrowWidthDip)<0.001 &&
				std::abs(narrow.geometry.items[4].Width()-ButtonTwoSideDip)<0.001 &&
				std::abs(narrow.geometry.items[5].Width()-EraserAttributeAutomaticArrowWidthDip)<0.001,
				"narrow work area clips the normal eraser layout without compacting normal gaps or controls");
			auto lighting=owner.spec.SnapshotFrameLighting();
			lighting.cursorLight={static_cast<float>(EraserRectCenterX(narrow.geometry.automatic)),static_cast<float>(EraserRectCenterY(narrow.geometry.automatic))};
			lighting.cursorRadius=240;lighting.cursorIntensity=1;lighting.cursorLightVisible=true;
			owner.spec.SetFrameLightingSnapshot(lighting, BarUiEdgeLightingEnabled);owner.spec.SetFrameCursorLightLocalGeometry(lighting.cursorLight,D2D1::SizeF(240,240));
			auto* dc=owner.specTarget.DeviceContext();dc->BeginDraw();dc->SetTransform(D2D1::IdentityMatrix());dc->Clear(D2D1::ColorF(0.13f,0.14f,0.16f,1));
			owner.eraserAttribute.Draw(owner.spec,dc);
			main->fill->SetDirect(GetThemeColor(ThemeColorEnum::Surface));main->frame->SetDirect(GetThemeColor(ThemeColorEnum::SurfaceFrame));
			DrawBackgroundVisual(owner.spec,dc,*main,UiInheritClass(main->inhX,main->inhY));
			for(const auto preset:presets)
			{
				auto* b=preset==BarButtonPresetEnum::More?owner.barButtonSet.GetMoreButton():owner.barButtonSet.preset[static_cast<int>(preset)];
				b->name.color.SetDirect(GetThemeColor(ThemeColorEnum::TextPrimary));b->icon.color1->SetDirect(GetThemeColor(ThemeColorEnum::TextPrimary));
				DrawButtonVisual(owner.spec,dc,*b,b->button.Inherit(UiInheritEnum::CenterFromTopLeft,*main));
			}
			expect(SUCCEEDED(dc->EndDraw()),"narrow production panel renders successfully");owner.eraserAttribute.CommitPresented();
			const std::filesystem::path narrowPath(L"Build/eraser-b/visuals/narrow-eraser-attribute.png");
			expect(SUCCEEDED(SaveEraserTestPng(dc,owner.specTarget.TargetBitmap(),narrowPath)),"narrow PNG readback succeeds");
			report<<"[EraserVisual] "<<narrowPath.string()<<" width="<<narrowWidth<<" height="<<narrowHeight<<'\n';
			root->x.SetDirect(100);
		}
		// 实际组件逐帧渲染：所有PNG与CSV都来自同一Advance/Draw/CommitPresented路径。
		{
			I18n::load(1,L"JSON",L"zh-CN");owner.ConfigureLocalizedTypography();updateLocalizedPresetLabels();owner.barStyle.darkStyle=true;owner.barStyle.zoom=1;
			BarUiEdgeLightingEnabled=true;BarUiDynamicEdgeLightingEnabled=true;UiAnimationEnabled=true;
			root->y.SetDirect(430);owner.barState.widgetPosition.primaryBar=false;owner.barState.widgetPosition.mainBar=true;
			const auto epoch=RenderPipeline::GetDeviceEpoch();expect(SUCCEEDED(owner.specTarget.Ensure(epoch,1000,620)),"animation target setup");
			owner.spec.SetFrameZoom(1);auto* dc=owner.specTarget.DeviceContext();
			std::filesystem::create_directories(L"Build/eraser-b/frames");
			std::ofstream trace("Build/eraser-b/frames.csv");trace<<"case,frame,panel_scale,menu_scale,panel_alpha,menu_alpha,panel_x,panel_y,panel_w,panel_h,menu_x,menu_y,menu_w,menu_h,bounds_l,bounds_t,bounds_r,bounds_b\n";
			auto tick=[&](const std::string& name,int f,double dt=1.0/60,bool save=false,
				RECT workArea={0,0,1000,620},POINT origin={0,0},bool dragPlacementLocked=false,
				const UiTimelineClass* parentTimeline=nullptr)
			{
				owner.eraserAttribute.Advance(owner,dt,UiAnimationSpeedRate,1,96,workArea,origin,0,0,parentTimeline,dragPlacementLocked);
				auto light=owner.spec.SnapshotFrameLighting();light.primaryLight={330,470};light.primaryRadius=480;light.primaryLightVisible=true;
				light.cursorLight={440,340};light.cursorRadius=240;light.cursorIntensity=1;light.cursorLightVisible=true;
				owner.spec.SetFrameLightingSnapshot(light, BarUiEdgeLightingEnabled);owner.spec.SetFrameCursorLightLocalGeometry(light.cursorLight,D2D1::SizeF(240,240));
				dc->BeginDraw();dc->SetTransform(D2D1::IdentityMatrix());dc->Clear(D2D1::ColorF(0.13f,0.14f,0.16f,1));
				owner.eraserAttribute.Draw(owner.spec,dc);
				main->fill->SetDirect(GetThemeColor(ThemeColorEnum::Surface));main->frame->SetDirect(GetThemeColor(ThemeColorEnum::SurfaceFrame));
				DrawBackgroundVisual(owner.spec,dc,*main,UiInheritClass(main->inhX,main->inhY));
				for(const auto preset:presets)
				{
					auto* b=preset==BarButtonPresetEnum::More?owner.barButtonSet.GetMoreButton():owner.barButtonSet.preset[static_cast<int>(preset)];
					b->name.color.SetDirect(GetThemeColor(ThemeColorEnum::TextPrimary));b->icon.color1->SetDirect(GetThemeColor(ThemeColorEnum::TextPrimary));
					DrawButtonVisual(owner.spec,dc,*b,b->button.Inherit(UiInheritEnum::CenterFromTopLeft,*main));
				}
				expect(SUCCEEDED(dc->EndDraw()),"animation frame draws without D2D error");owner.eraserAttribute.CommitPresented();
				const auto s=owner.eraserAttribute.PresentationSnapshot();const auto b=owner.eraserAttribute.Bounds();
				const auto p=s.geometry.panel,m=s.geometry.menu;
				trace<<name<<','<<f<<','<<s.panelPose.scale<<','<<s.menuPose.scale<<','<<s.panelOpacity<<','<<s.menuOpacity<<','<<p.left<<','<<p.top<<','<<p.Width()<<','<<p.Height()<<','<<m.left<<','<<m.top<<','<<m.Width()<<','<<m.Height()<<','<<b.left<<','<<b.top<<','<<b.right<<','<<b.bottom<<'\n';
				auto covered=[&](EraserAttributeRect r){return b.left<=r.left && b.top<=r.top && b.right>=r.right && b.bottom>=r.bottom;};
				if(s.panelVisible)expect(covered(p),"Bounds covers presented shell including geometric overshoot");
				if(s.menuVisible)expect(covered(m),"Bounds covers current child shell");
				expect(s.panelOpacity>=0 && s.panelOpacity<=1 && s.menuOpacity>=0 && s.menuOpacity<=1,"rendered alpha bounded separately from geometry");
				if(save)expect(SUCCEEDED(SaveEraserTestPng(dc,owner.specTarget.TargetBitmap(),std::filesystem::path(L"Build/eraser-b/frames")/(std::wstring(name.begin(),name.end())+L"-"+std::to_wstring(f)+L".png"))),"animation PNG readback");
				return s;
			};
			owner.eraserAttribute.Close(owner);for(int f=0;f<60;++f)tick("settle",f);
			owner.barState.eraserAttribute=true;double openPeak=0,menuOpenPeak=0,closePeak=0,menuClosePeak=0;bool mainOutsidePixel=false,menuOutsidePixel=false;
			EraserAttributePresentation stable;
			for(int f=0;f<36;++f)
			{
				const auto s=tick("panel-open",f,1.0/60,true);openPeak=(std::max)(openPeak,s.panelPose.scale);
				if(s.panelPose.scale>1.01 && s.geometry.panel.top+2<300)
				{
					const auto pixel=ReadEraserTestPixel(dc,owner.specTarget.TargetBitmap(),static_cast<UINT>(EraserRectCenterX(s.geometry.panel)),static_cast<UINT>(s.geometry.panel.top+2));
					mainOutsidePixel|=std::abs(static_cast<int>(pixel[0])-41)>2 || std::abs(static_cast<int>(pixel[1])-36)>2 || std::abs(static_cast<int>(pixel[2])-33)>2;
				}
				stable=s;
			}
			expect(openPeak>1.01 && stable.panelPose.scale==1 && mainOutsidePixel,"main opening overshoot exists in actual pixels outside final rectangle");
			// 直拖期间工作区相对坐标会越过主栏；应维持已呈现方向，松手后才走既有收拢再展开路径。
			const auto beforeDrag=stable;
			for(int f=0;f<12;++f)
			{
				const auto held=tick("drag-hold",f,1.0/60,f==0,{0,0,1000,1000},{0,-550},true);
				expect(held.geometry.below==beforeDrag.geometry.below &&
					std::abs(held.geometry.panel.top-beforeDrag.geometry.panel.top)<0.000001,
					"direct drag does not snap the presented eraser panel side or position");
			}
			owner.barState.widgetPosition.mainBar=false;owner.barState.widgetPosition.primaryBar=true;root->y.SetDirect(140);
			UiTimelineClass parentTimeline;parentTimeline.Restart(UiDefaultOperationDur);
			parentTimeline.Advance(UiDefaultOperationDur/4,1);
			const auto release=tick("drag-release",0,0,true,{0,0,1000,620},{0,0},false,&parentTimeline);
			const double absorbedY=EraserRectCenterY(release.geometry.anchor)-EraserRectCenterY(beforeDrag.geometry.anchor);
			expect(release.geometry.below==beforeDrag.geometry.below && owner.eraserAttribute.Active() &&
				std::abs(release.geometry.panel.top-beforeDrag.geometry.panel.top-absorbedY)<0.000001,
				"release first frame rebases the old panel without a work-area clamp flash");
			const int joinedSwitchFrames=static_cast<int>(std::ceil(parentTimeline.GetRemainingDuration()/(1.0/60)));
			const int joinedCollapseFrames=static_cast<int>(std::ceil((UiDefaultOperationDur/4)/(1.0/60)));
			for(int f=0;f<joinedCollapseFrames;++f)tick("drag-release",f+1,1.0/60,f%3==0);
			const auto switched=owner.eraserAttribute.PresentationSnapshot();
			expect(switched.geometry.below && switched.geometry.reversed && owner.eraserAttribute.Active(),
				"release commits the new direction on the parent midpoint frame without a late old-side frame");
			for(int f=joinedCollapseFrames;f<joinedSwitchFrames;++f)tick("drag-release",f+1,1.0/60,f%3==0);
			const auto released=owner.eraserAttribute.PresentationSnapshot();
			expect(released.geometry.below && released.geometry.reversed && released.panelPose.scale==1,
				"release side switch joins the draw-attribute parent batch and settles at the same deadline");
			owner.barState.widgetPosition.mainBar=true;owner.barState.widgetPosition.primaryBar=false;root->y.SetDirect(430);
			UiTimelineClass lateParentTimeline;lateParentTimeline.Restart(UiDefaultOperationDur);
			lateParentTimeline.Advance(UiDefaultOperationDur*0.75,1);
			tick("drag-reset",0,0,true,{0,0,1000,620},{0,0},false,&lateParentTimeline);
			const int lateParentFrames=static_cast<int>(std::ceil(lateParentTimeline.GetRemainingDuration()/(1.0/60)));
			for(int f=0;f<lateParentFrames;++f)tick("drag-reset",f+1,1.0/60,f%3==0);
			expect(owner.eraserAttribute.Active() && owner.eraserAttribute.PresentationSnapshot().geometry.below,
				"side switch after the parent midpoint starts a full independent draw-attribute batch");
			const int independentFrames=static_cast<int>(std::ceil(UiDefaultOperationDur/(1.0/60)));
			for(int f=lateParentFrames;f<independentFrames;++f)tick("drag-reset",f+1,1.0/60,f%3==0);
			stable=owner.eraserAttribute.PresentationSnapshot();
			expect(!stable.geometry.below && !stable.geometry.reversed && stable.panelPose.scale==1,
				"drag test restores the original presented side before interaction coverage");
			// RenderLoop先推进父时间线；即使该帧从中点前跨到中点后，仍应加入同一绘制属性批次。
			owner.barState.widgetPosition.mainBar=false;owner.barState.widgetPosition.primaryBar=true;root->y.SetDirect(140);
			UiTimelineClass crossingParentTimeline;crossingParentTimeline.Restart(UiDefaultOperationDur);
			crossingParentTimeline.Advance(UiDefaultOperationDur*0.49,1);
			crossingParentTimeline.Advance(1.0/60,UiAnimationSpeedRate);
			const auto crossed=tick("crossing-join",0,1.0/60,true,{0,0,1000,620},{0,0},false,&crossingParentTimeline);
			expect(crossed.geometry.below && crossed.geometry.reversed,
				"parent frame crossing 50 percent keeps the eraser in the joined draw-attribute batch");
			const int crossingFrames=static_cast<int>(std::ceil(crossingParentTimeline.GetRemainingDuration()/(1.0/60)));
			for(int f=0;f<crossingFrames;++f)tick("crossing-join",f+1,1.0/60,f%3==0);
			expect(owner.eraserAttribute.PresentationSnapshot().panelPose.scale==1,
				"joined crossing frame carries its midpoint remainder into the expansion segment");
			owner.barState.widgetPosition.mainBar=true;owner.barState.widgetPosition.primaryBar=false;root->y.SetDirect(430);
			for(int f=0;f<independentFrames;++f)tick("crossing-restore",f,1.0/60,f%3==0);
			stable=owner.eraserAttribute.PresentationSnapshot();
			expect(!stable.geometry.below && !stable.geometry.reversed && stable.panelPose.scale==1,
				"crossing-frame timing test restores the original side");
			// 保存真实按压及选中环交接帧，避免只有稳定态截图而遗漏交互动画。
			auto sizeMessage=[&](UINT kind,int item,bool held){ExMessage m{};m.message=static_cast<USHORT>(kind);m.x=static_cast<short>(EraserRectCenterX(stable.geometry.items[item]));m.y=static_cast<short>(EraserRectCenterY(stable.geometry.items[item]));m.lbutton=held;return m;};
			owner.eraserAttribute.Pointer(owner,sizeMessage(WM_LBUTTONDOWN,3,true));
			tick("size-press",0,1.0/60,true);expect(owner.eraserAttribute.Active(),"size press starts the shared button feedback animation");
			for(int f=1;f<10;++f)tick("size-press",f,1.0/60,true);
			owner.eraserAttribute.Pointer(owner,sizeMessage(WM_LBUTTONUP,3,false));
			tick("size-selection-fade",0,1.0/60,true);expect(owner.eraserAttribute.Active(),"selection ring fade and press release request continuation frames");
			for(int f=1;f<30;++f)tick("size-selection-fade",f,1.0/60,true);
			expect(!owner.eraserAttribute.Active(),"size press and reversible selection-ring fade settle to idle");
			// 没有Down票据的旧Up，即使当前中央Clear在命中点，也不能形成命令。
			ExMessage oldUp{};oldUp.message=WM_LBUTTONUP;oldUp.x=static_cast<short>(EraserRectCenterX(stable.geometry.items[0]));oldUp.y=static_cast<short>(EraserRectCenterY(stable.geometry.items[0]));
			owner.eraserAttribute.Pointer(owner,oldUp);expect(owner.barState.eraserAttribute && ResolveEraserAttributeRelease(-1,0,true,false)==-1,"opening release cannot trigger center Clear");
			owner.barState.eraserSensitivityOpen=true;
			for(int f=0;f<36;++f)
			{
				const auto s=tick("menu-open",f,1.0/60,true);menuOpenPeak=(std::max)(menuOpenPeak,s.menuPose.scale);
				if(s.menuPose.scale>1.01 && s.geometry.menu.top+2<205)
				{
					const auto pixel=ReadEraserTestPixel(dc,owner.specTarget.TargetBitmap(),static_cast<UINT>(EraserRectCenterX(s.geometry.menu)),static_cast<UINT>(s.geometry.menu.top+2));
					menuOutsidePixel|=std::abs(static_cast<int>(pixel[0])-41)>2 || std::abs(static_cast<int>(pixel[1])-36)>2 || std::abs(static_cast<int>(pixel[2])-33)>2;
				}
			}
			expect(menuOpenPeak>1.01 && menuOutsidePixel,"child opening overshoot is visible, not clipped by final menu bounds");
			owner.barState.eraserSensitivityOpen=false;
			for(int f=0;f<36;++f){const auto s=tick("menu-close",f,1.0/60,true);menuClosePeak=(std::max)(menuClosePeak,s.menuPose.scale);if(f<20)expect(s.menuVisible,"closing child remains visible during early Back phase");}
			expect(menuClosePeak>1.01 && !owner.eraserAttribute.PresentationSnapshot().menuVisible,"child close overshoots then ends");
			owner.barState.eraserSensitivityOpen=true;for(int f=0;f<45;++f)tick("child-ready",f);
			owner.eraserAttribute.Close(owner);
			for(int f=0;f<40;++f)
			{
				const auto s=tick("both-close",f,1.0/60,true);closePeak=(std::max)(closePeak,s.panelPose.scale);
				if(f<20)expect(s.panelVisible && s.menuVisible,"parent and child both finish their closing transition");
			}
			expect(closePeak>1.01 && !owner.eraserAttribute.PresentationSnapshot().panelVisible,"parent close has geometric rebound and no leftover hit region");
			for(int f=0;f<45;++f)tick("closed-idle",f);expect(!owner.eraserAttribute.Active(),"settled closed component sleeps");
			owner.barState.eraserAttribute=true;for(int f=0;f<7;++f)tick("reverse-start",f,1.0/60,true);
			const auto before=owner.eraserAttribute.PresentationSnapshot();owner.barState.eraserAttribute=false;const auto after=tick("reverse-latch",0,0,true);
			expect(std::abs(before.panelPose.scale-after.panelPose.scale)<0.000001 && std::abs(before.geometry.panel.top-after.geometry.panel.top)<0.000001,"halfway reversal does not reset to compact or target");
			for(int f=0;f<6;++f)tick("reverse-close",f,1.0/60,true);owner.barState.eraserAttribute=true;
			for(int f=0;f<40;++f)tick("reverse-open",f,1.0/60,true);
			owner.barState.eraserSensitivityOpen=true;for(int f=0;f<8;++f)tick("child-reverse-start",f);
			const auto beforeChild=owner.eraserAttribute.PresentationSnapshot();owner.barState.eraserSensitivityOpen=false;const auto afterChild=tick("child-reverse-latch",0,0);
			expect(std::abs(beforeChild.menuPose.scale-afterChild.menuPose.scale)<0.000001,"child reversal retains current geometry");
			for(int f=0;f<40;++f)tick("child-reverse-close",f);
			// 控件内跨动作区不重播整体Hover；两种方向都只执行Down所属动作。
			for(int source=0;source<3;++source)
			{
				const auto s=owner.eraserAttribute.PresentationSnapshot();auto message=[&](UINT kind,int item,bool held){ExMessage m{};m.message=static_cast<USHORT>(kind);m.x=static_cast<short>(EraserRectCenterX(s.geometry.items[item]));m.y=static_cast<short>(EraserRectCenterY(s.geometry.items[item]));m.lbutton=held;if(source)m.wheel=SHRT_MIN;return m;};
				owner.barState.eraserSensitivityOpen=false;const auto initial=Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot());
				owner.eraserAttribute.Pointer(owner,message(WM_LBUTTONDOWN,4,true),false,source!=0);owner.eraserAttribute.Pointer(owner,message(WM_MOUSEMOVE,5,true),false,source!=0);owner.eraserAttribute.Pointer(owner,message(WM_LBUTTONUP,5,false),false,source!=0);
				expect(Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())!=initial && !owner.barState.eraserSensitivityOpen,"normalized Mouse/Pen/Touch body-to-arrow executes body only");
				const auto afterBody=Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot());std::this_thread::sleep_for(std::chrono::milliseconds(310));
				owner.eraserAttribute.Pointer(owner,message(WM_LBUTTONDOWN,5,true),false,source!=0);owner.eraserAttribute.Pointer(owner,message(WM_MOUSEMOVE,4,true),false,source!=0);owner.eraserAttribute.Pointer(owner,message(WM_LBUTTONUP,4,false),false,source!=0);
				expect(Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())==afterBody && owner.barState.eraserSensitivityOpen,"normalized arrow-to-body executes menu only");
				for(int f=0;f<45;++f)tick("input-settle",f);
			}
			const auto beforeLeave=Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot());
			const auto leaveFrame=owner.eraserAttribute.PresentationSnapshot();ExMessage leave{};leave.x=static_cast<short>(EraserRectCenterX(leaveFrame.geometry.items[4]));leave.y=static_cast<short>(EraserRectCenterY(leaveFrame.geometry.items[4]));leave.message=WM_LBUTTONDOWN;leave.lbutton=true;
			owner.eraserAttribute.Pointer(owner,leave);owner.eraserAttribute.ResetPointerFeedback();leave.message=WM_LBUTTONUP;leave.lbutton=false;owner.eraserAttribute.Pointer(owner,leave);
			expect(Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())==beforeLeave,"window leave cancels stale press ownership");
			owner.barState.eraserSensitivityOpen=true;for(int f=0;f<45;++f)tick("swap-ready",f);
			owner.barState.widgetPosition.mainBar=false;owner.barState.widgetPosition.primaryBar=true;
			for(int f=0;f<65;++f){root->y.SetDirect(430-290*(std::min)(1.0,f/24.0));tick("side-swap",f,1.0/60,f%3==0);}
			const auto reversed=owner.eraserAttribute.PresentationSnapshot();expect(reversed.geometry.reversed && reversed.geometry.below && reversed.panelPose.scale==1,"side swap settles with upright group order");
			UiAnimationEnabled=false;owner.eraserAttribute.Close(owner);const auto disabled=tick("disabled-close",0,0,true);expect(!disabled.panelVisible && !disabled.menuVisible,"disabled animation closes immediately");
			owner.barState.eraserAttribute=true;owner.barState.eraserSensitivityOpen=true;const auto direct=tick("disabled-open",0,0,true);expect(direct.panelPose.scale==1 && direct.menuPose.scale==1 && direct.panelOpacity==1,"disabled animation opens at true size");
			UiAnimationEnabled=true;UiAnimationSpeedRate=2;owner.eraserAttribute.Close(owner);for(int f=0;f<15;++f)tick("double-speed-close",f);expect(!owner.eraserAttribute.PresentationSnapshot().panelVisible,"speed setting affects both surfaces");UiAnimationSpeedRate=1;
			report<<"[EraserFrames] panelOpenPeak="<<openPeak<<" menuOpenPeak="<<menuOpenPeak<<" panelClosePeak="<<closePeak<<" menuClosePeak="<<menuClosePeak<<" pixelOvershoot="<<mainOutsidePixel<<','<<menuOutsidePixel<<'\n';
		}
		// 新SVG在真实标准图标尺寸和三种状态下栅格化，不使用字体绘制A。
		for(bool dark:{false,true})for(UINT dpi:{96u,144u,192u})for(double ui:{0.65,1.0,1.5})
		{
			owner.barStyle.darkStyle=dark;const double zoom=dpi/96.0*ui;
			expect(SUCCEEDED(owner.specTarget.Ensure(RenderPipeline::GetDeviceEpoch(),static_cast<UINT>(250*zoom),static_cast<UINT>(90*zoom))),"SVG state target setup");owner.spec.SetFrameZoom(zoom);
			auto* dc=owner.specTarget.DeviceContext();dc->BeginDraw();dc->SetTransform(D2D1::IdentityMatrix());dc->Clear(dark?D2D1::ColorF(0.13f,0.14f,0.16f,1):D2D1::ColorF(0.91f,0.93f,0.95f,1));
			for(int state=0;state<3;++state)
			{
				BarButtonClass button;button.hide=false;button.button.Initialization(0,0,ButtonTwoSideDip,ButtonTwoSideDip,ButtonCornerRadiusDip,ButtonCornerRadiusDip,ButtonFrameThicknessDip,GetThemeColor(ThemeColorEnum::PressedFill),GetThemeColor(ThemeColorEnum::SurfaceFrame));
				button.button.enable.Initialization(true);button.button.framePct.emplace(0);button.button.frameLightPct.emplace(0);
				button.icon.Initialization(0,metrics.iconOffsetYDip,GetThemeColor(ThemeColorEnum::TextPrimary),std::nullopt);button.icon.InitializationFromResource(L"UI",L"barAutoEraser");button.icon.enable.Initialization(true);button.icon.SetWH(metrics.iconSizeDip,metrics.iconSizeDip);button.icon.w.SetDirect(button.icon.w.tar);button.icon.h.SetDirect(button.icon.h.tar);
				button.name.Initialization(0,metrics.primaryOffsetYDip,metrics.primarySlotWidthDip,metrics.primarySlotHeightDip,state==0?L"常规":state==1?L"选中":L"禁用",metrics.primaryFontSizeDip,GetThemeColor(ThemeColorEnum::TextPrimary));button.name.enable.Initialization(true);
				RetargetButtonInteractionVisual(button,true,state!=2,state==1,0.0);
				const UiAnimationAdvanceContextClass instant{0,1,false,true};UiAdvanceAnimation(button.button.pct,instant);UiAdvanceAnimation(*button.button.fill,instant);UiAdvanceAnimation(button.icon.pct,instant);UiAdvanceAnimation(*button.icon.color1,instant);UiAdvanceAnimation(button.name.pct,instant);UiAdvanceAnimation(button.name.color,instant);
				DrawButtonVisual(owner.spec,dc,button,UiInheritClass(10+state*80,10));
				expect(button.icon.cacheBitmap && button.icon.svg.GetVal().find(L"<text")==std::wstring::npos,"A icon rasterizes from path in normal/selected/disabled state");
			}
			expect(SUCCEEDED(dc->EndDraw()),"SVG states draw");
			const auto file=std::filesystem::path(L"Build/eraser-b/visuals")/(L"icon-"+std::to_wstring(dpi)+L"-"+std::to_wstring(static_cast<int>(ui*100))+(dark?L"-dark.png":L"-light.png"));expect(SUCCEEDED(SaveEraserTestPng(dc,owner.specTarget.TargetBitmap(),file)),"icon states PNG");
		}
		// B3只共用真实CacheBitmap/Svg/D2D离屏事务；不把数值observer组合称为主栏ULW。
		{
			constexpr UINT svgSize = 96;
			constexpr std::uint32_t svgTag = 0x10000;
			const auto epoch = RenderPipeline::GetDeviceEpoch();
			UIRendering renderer; FrameTarget frameTarget{renderer};
			const HRESULT setup = frameTarget.Ensure(epoch, svgSize, svgSize);
			auto* dc = frameTarget.DeviceContext();
			const bool deviceReady = SUCCEEDED(setup) && dc && epoch.generation != 0;
			expect(deviceReady, "B300 SVG proof premise: real WARP/D2D epoch and 96x96 target");
			if (deviceReady)
			{
				auto probe = std::make_unique<Ui3SvgProbe>(0xB301);
				UiSVGClass svg;
				bool bound = false;
				{
					SvgObservationScope ownedInit(probe.get(), false, true);
					svg.Initialization(0, 0, RGB(220, 110, 40), std::nullopt);
					svg.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><rect width="32" height="32" fill="rgba(10,0,7,0)"/></svg>)SVG");
					svg.w.SetDirect(32); svg.h.SetDirect(32); svg.pct.SetDirect(1); svg.enable.Initialization(true);
					bound = svg.BindObservationTag(svgTag);
				}
				expect(bound && svg.ObservationState().semanticKnown && svg.ObservationState().valueRevision == 1
					&& !svg.cacheBitmap && svg.svg.IsSame(), "B300 SVG proof premise: owned value built before tag, no old bitmap");
				if (bound && svg.ObservationState().semanticKnown)
				{
					renderer.SetFrameZoom(1.0);
					std::uint64_t attempt = 0;
					bool transactionOk = false, submitted = false;
					struct SvgTestFrame
					{
						bool paint = true, clear = true, commit = true, outerClip = true, nested = false, unknownWrite = false;
						D2D1_MATRIX_3X2_F transform = D2D1::IdentityMatrix();
						D2D1_RECT_F nestedClip = D2D1::RectF(20, 20, 30, 30);
						std::uint32_t dpi = 96, alpha = 255;
						double zoom = 1.0, x = 16.0, y = 16.0;
						std::uint64_t revision = 1, surface = 1;
					} options;
					auto currentEpoch = epoch;
					auto drawSvg = [&](const D2D1_RECT_F& clip, bool clocks)
					{
						SvgObservationScope rendering(probe.get(), clocks);
						renderer.SetFrameZoom(options.zoom);
						probe->BeginFrame(options.revision, currentEpoch.generation, ++attempt);
						svg.ObserveFiniteRequirement(true);
						Ui3SvgFrameTarget target;
						target.revision = options.revision; target.epoch = currentEpoch.generation; target.surfaceSerial = options.surface; target.frameAttemptSerial = attempt;
						target.width = target.height = target.backingWidth = target.backingHeight = svgSize;
						target.dpi = options.dpi; target.windowAlpha = options.alpha; target.zoomBits = std::bit_cast<std::uint64_t>(options.zoom);
						for (unsigned i = 0; i < 4; ++i) target.viewportBits[i] = std::bit_cast<std::uint32_t>(i < 2 ? 0.0f : static_cast<float>(svgSize));
						probe->BeginBackingWrite(dc, target);
						dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix());
						if (options.outerClip) frameTarget.PushDirtyClip(dc, clip);
						if (options.nested)
						{
							dc->PushAxisAlignedClip(options.nestedClip, D2D1_ANTIALIAS_MODE_ALIASED);
							const float rect[4]{options.nestedClip.left, options.nestedClip.top, options.nestedClip.right, options.nestedClip.bottom};
							const float matrix[6]{1,0,0,1,0,0}; std::uint32_t r[4], m[6];
							for (unsigned i = 0; i < 4; ++i) r[i] = std::bit_cast<std::uint32_t>(rect[i]);
							for (unsigned i = 0; i < 6; ++i) m[i] = std::bit_cast<std::uint32_t>(matrix[i]);
							ObserveUi3SvgClipPush(dc, r, m);
						}
						if (options.clear) { ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0, 0, 0, 0)); }
						dc->SetTransform(options.transform);
						submitted = options.paint && renderer.Svg(dc, svg, UiInheritClass(options.x, options.y));
						if (options.unknownWrite) { ObserveUi3SvgUnknownWrite(dc); dc->Clear(D2D1::ColorF(0, 0, 0, 0)); }
						dc->SetTransform(D2D1::IdentityMatrix());
						if (options.nested) { ObserveUi3SvgClipPop(dc); dc->PopAxisAlignedClip(); }
						if (options.outerClip) frameTarget.PopDirtyClip(dc);
						const auto proof = probe->FinishDrawing();
						transactionOk = SUCCEEDED(dc->EndDraw());
						probe->CompleteAttempt(transactionOk && options.commit);
						return proof;
					};
					const auto full = drawSvg(D2D1::RectF(0, 0, svgSize, svgSize), false);
					const auto actual = probe->Observation(svgTag);
					const auto counts = probe->CountersAfterOwnerStopped();
					const auto pixelOff = ReadEraserTestPixel(dc, frameTarget.TargetBitmap(), 32, 32);
					const bool cacheReady = transactionOk && submitted && svg.cacheBitmap && actual.used.ready && actual.used.semanticKnown
						&& actual.used.valueRevision == 1 && actual.used.epoch == epoch.generation && actual.used.surfaceSerial == 1
						&& actual.used.dpi == 96 && actual.used.pixelWidth == 32 && actual.used.pixelHeight == 32 && pixelOff[3] != 0;
					expect(cacheReady, "B300 SVG proof premise: actual bitmap/upload/draw/EndDraw and nonempty BGRA");
					expect(counts.lookupMiss == 1 && counts.createAttempt == 1 && counts.createSuccess == 1 && counts.createFailure == 0
						&& counts.parseCalls == 1 && counts.rasterCalls == 1 && counts.uploadCalls == 1 && counts.drawSubmit == 1
						&& counts.readyEntries == 1 && counts.logicalReadyBytes == 32 * 32 * 4 && counts.unknownReadyEntries == 0
						&& probe->InitializationCountersAfterOwnerStopped().parseCalls == 1,
						"B301 real SVG first creation counts separate initialization parse from frame parse");
					if (cacheReady)
					{
						expect(full.required == 1 && full.verified == 1 && full.failed == 0 && full.unverified == 0
							&& actual.coverage == Ui3SvgCoverage::FullVisibleCoverage && actual.use == Ui3SvgUse::DrawnVerified,
							"B302 actual full clip SVG must yield staged complete paint proof");
						Ui3FiniteSignature initial;
						initial.flags = 65; initial.stateMode = 1; initial.penMode = 0; initial.penColorRgb = RGB(220, 110, 40);
						initial.penWidthBits = std::bit_cast<std::uint32_t>(3.0f); initial.dpi = 96;
						initial.toolRevision = 1; initial.displaySerial = 2; initial.configZoomBits = std::bit_cast<std::uint64_t>(1.0);
						initial.validMask = Ui3FiniteRequiredMask;
						auto publication = std::make_unique<Ui3FinitePublication>(0xB303, initial);
						auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
						auto goal = initial; goal.flags = 64;
						auto mutation = publication->BeginMutation(1, Ui3FiniteScene::MainFold, 1);
						publication->MarkBusinessAccepted(mutation); publication->ObserveBusinessWrite(mutation);
						publication->FinishAtRenderRequest(mutation, goal, 0);
						Ui3FiniteAccepted accepted;
						const bool stable = observer->SnapshotAccepted(accepted);
						observer->BeginFrame(accepted, epoch.generation, 1);
						const bool consumed = observer->MarkConsumed(goal, 1, 1);
						for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
						auto candidate = observer->SettleCandidate(1, svgSize, svgSize);
						const bool bridgeReady = stable && consumed && candidate.settled && !candidate.svgProofComplete;
						expect(bridgeReady,
							"B300 finite bridge premise: legal signature/publication/consumed/layout; no pre-draw resource proof");
						const auto layoutCount = observer->CountersAfterRenderStopped().layoutSettled;
						expect(publication->CompletedRevision() == 0, "B303 complete receipt is absent before actual full resource transaction");
						candidate = observer->FinalizeResources(candidate, full);
						Ui3FiniteCommitIdentity identity;
						identity.epoch = epoch.generation; identity.surfaceSerial = 1; identity.frameAttemptSerial = 1;
						identity.targetWidth = identity.targetHeight = svgSize;
						const auto outcome = observer->CompleteAttempt(candidate, true, false, 0, identity);
						if (bridgeReady) expect(candidate.svgProofComplete && outcome == Ui3FiniteStatus::CompletedLayoutAndSvg,
							"B303 real SVG late resource finalization completes the same finite candidate");
						if (bridgeReady) expect(publication->CompletedRevision() == accepted.revision,
							"B303 acquire getter exposes only real completed revision receipt");
						expect(observer->CountersAfterRenderStopped().layoutSettled == layoutCount,
							"B304 late resource finalization does not repeat layout/consume/clock");
					}
					const auto partial = drawSvg(D2D1::RectF(0, 0, 28, svgSize), false);
					expect(transactionOk && submitted && partial.required == 1 && partial.verified == 0
						&& probe->Observation(svgTag).coverage == Ui3SvgCoverage::Partial,
						"B305 actual partial dirty clip cannot certify full SVG coverage");
					(void)drawSvg(D2D1::RectF(0, 0, svgSize, svgSize), true);
					const auto pixelOn = ReadEraserTestPixel(dc, frameTarget.TargetBitmap(), 32, 32);
					const auto after = probe->CountersAfterOwnerStopped();
					expect(transactionOk && pixelOff == pixelOn && counts.clockReads == 0 && after.clockReads > 0
						&& after.lookupHit == 2 && after.createSuccess == 1 && after.uploadCalls == 1,
						"B306 existing bitmap reuse and gated clocks retain actual BGRA pixel");
					expect(FitsUi3SvgCaptureBudget(32768, sizeof(RenderPipeline::RawCallbackSample), sizeof(RenderPipeline::RawBatchSample))
						&& !FitsUi3SvgCaptureBudget(1, (std::numeric_limits<std::size_t>::max)(), (std::numeric_limits<std::size_t>::max)()),
						"B307 combined 64MiB capture budget rejects multiplication/addition overflow");
					const auto fullClip = D2D1::RectF(0, 0, svgSize, svgSize);
					const auto empty = drawSvg(D2D1::RectF(0, 0, 1, 1), false);
					expect(transactionOk && empty.verified == 1 && empty.unverified == 0
						&& probe->Observation(svgTag).coverage == Ui3SvgCoverage::Empty
						&& probe->Observation(svgTag).use == Ui3SvgUse::RetainedVerified,
						"B310 known disjoint clip retains the exact previously verified SVG proof");
					options.outerClip = false;
					const auto unknown = drawSvg(fullClip, false);
					expect(transactionOk && unknown.unverified == 1 && probe->Observation(svgTag).coverage == Ui3SvgCoverage::Unknown,
						"B311 absent clip state cannot certify SVG");
					options = {}; options.nested = true;
					const auto nested = drawSvg(fullClip, false);
					expect(transactionOk && nested.verified == 0 && probe->Observation(svgTag).coverage == Ui3SvgCoverage::Partial,
						"B312 actual nested clip intersection is partial");
					options = {}; options.paint = options.clear = false;
					const auto retained = drawSvg(fullClip, false);
					const auto missingDraw = retained;
						// 首版不能以本帧没有重画/旧cache仍在来给retained证书。
					expect(transactionOk && retained.required == 1 && retained.verified == 0 && probe->Observation(svgTag).use != Ui3SvgUse::RetainedVerified,
						"B312 no actual redraw keeps required retained SVG unverified");
					options = {}; options.transform = D2D1::Matrix3x2F::Translation(7, 5); svg.angle.SetDirect(20);
					const auto transformed = drawSvg(fullClip, false);
					expect(transactionOk && transformed.verified == 1 && probe->Observation(svgTag).transformBits[4] != std::bit_cast<std::uint32_t>(0.0f),
						"B313 actual SVG rotation and translation retain full coverage proof");
					options = {}; svg.angle.SetDirect(0); svg.pct.SetDirect(0.5);
					const auto opacity = drawSvg(fullClip, false);
					expect(transactionOk && opacity.verified == 1 && probe->Observation(svgTag).finalOpacityBits == std::bit_cast<std::uint32_t>(0.5f),
						"B314 actual half opacity is captured with current expected opacity");
					options.alpha = 0;
					expect(drawSvg(fullClip, false).verified == 0, "B315 zero window alpha cannot certify expected visible SVG");
					options = {}; svg.pct.SetDirect(1); options.unknownWrite = true;
					expect(drawSvg(fullClip, false).verified == 0, "B316 real target write after SVG invalidates staged paint");
					options = {}; (void)drawSvg(fullClip, false);
					svg.enable.Initialization(false); svg.pct.SetDirect(0); options.paint = options.clear = false;
					expect(drawSvg(fullClip, false).verified == 0, "B317 hidden flag alone does not remove prior visible pixels");
					options.clear = true;
					const auto hidden = drawSvg(fullClip, false);
					expect(transactionOk && hidden.required == 1 && hidden.verified == 1 && probe->Observation(svgTag).use == Ui3SvgUse::HiddenExpected
						&& ReadEraserTestPixel(dc, frameTarget.TargetBitmap(), 32, 32)[3] == 0,
						"B318 real complete Clear certifies Hidden with old bounds");
					options = {}; svg.enable.Initialization(true); svg.pct.SetDirect(1); (void)drawSvg(fullClip, false);
					options.x = options.y = 44; options.clear = options.commit = false;
					(void)drawSvg(fullClip, false); // 真D2D写入新位置，模拟之后ULW失败/deferred，不叫真实ULW。
					expect(transactionOk && probe->Observation(svgTag).use == Ui3SvgUse::Unverified, "B319 failed/deferred completion revokes staged SVG proof");
					svg.enable.Initialization(false); svg.pct.SetDirect(0); options = {}; options.paint = false;
					const auto oldOnly = drawSvg(D2D1::RectF(0, 0, 52, 52), false);
					expect(transactionOk && oldOnly.verified == 0 && ReadEraserTestPixel(dc, frameTarget.TargetBitmap(), 60, 60)[3] != 0,
						"B320 Clear of old bounds cannot hide new region written by failed attempt");
					expect(drawSvg(fullClip, false).verified == 1 && ReadEraserTestPixel(dc, frameTarget.TargetBitmap(), 60, 60)[3] == 0,
						"B321 real full backing Clear repairs unknown Hidden lineage");
					options = {}; svg.enable.Initialization(true); svg.pct.SetDirect(1);
					const auto beforeColor = probe->CountersAfterOwnerStopped(); svg.color1->Initialization(RGB(40, 170, 90));
					const auto color = drawSvg(fullClip, false);
					expect(transactionOk && color.verified == 1 && probe->CountersAfterOwnerStopped().replacement == beforeColor.replacement + 1
						&& probe->Observation(svgTag).used.color1Rgb == RGB(40, 170, 90), "B322 real color replacement carries actual color and cache replacement");
					{
						SvgObservationScope writing(probe.get());
						svg.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><circle cx="16" cy="16" r="15" fill="rgba(10,0,7,0)"/></svg>)SVG");
					}
					const auto content = drawSvg(fullClip, false);
					expect(transactionOk && content.verified == 1 && probe->Observation(svgTag).used.valueRevision == 2,
						"B323 actual value commit increments revision and creates matching bitmap");
					svg.w.SetDirect(40); svg.h.SetDirect(40);
					expect(drawSvg(fullClip, false).verified == 1 && probe->Observation(svgTag).used.pixelWidth == 40,
						"B324 actual stable size follows original SVG raster size policy");
					svg.w.SetDirect(32); svg.h.SetDirect(32);
					const auto imageOffProof = drawSvg(fullClip, false); const auto imageOff = ReadSvgProofPixels(dc, frameTarget.TargetBitmap());
					const auto imageOnProof = drawSvg(fullClip, true); const auto imageOn = ReadSvgProofPixels(dc, frameTarget.TargetBitmap());
					expect(imageOffProof.verified == 1 && imageOnProof.verified == 1 && imageOff.size() == svgSize * svgSize * 4 && imageOff == imageOn,
						"B325 entire actual BGRA target is equal with instrumentation clocks on/off");
					const auto nullBefore = probe->CountersAfterOwnerStopped();
					{
						SvgObservationScope absent(nullptr, true); Ui3SvgStageTimer timer(Ui3SvgStage::Parse);
						dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); dc->Clear(D2D1::ColorF(0,0,0,0));
						(void)renderer.Svg(dc, svg, UiInheritClass(16,16)); expect(SUCCEEDED(dc->EndDraw()), "B326 default absent-scope actual draw succeeds");
					}
					expect(CurrentUi3SvgScope() == nullptr && probe->CountersAfterOwnerStopped().clockReads == nullBefore.clockReads
						&& probe->CountersAfterOwnerStopped().drawSubmit == nullBefore.drawSubmit, "B326 null scope adds no observation clock or draw counter");
					// 独立数值计量不能安装验真probe，也不能改变真实cache/绘制决策。
					static std::int64_t numericClockReads = 0;
					const auto numericClock = []() noexcept -> std::int64_t { return ++numericClockReads * 1000; };
					numericClockReads = 0;
					Ui3SvgStageTimings outerTiming, innerTiming;
					{
						SvgStageTimingScope absent(nullptr, numericClock);
						Ui3SvgStageTimer timer(Ui3SvgStage::Parse);
					}
					expect(numericClockReads == 0 && CurrentUi3SvgScope() == nullptr, "N01 default/null numeric scope reads no clock and installs no proof");
					{
						SvgStageTimingScope outer(&outerTiming, numericClock);
						{ Ui3SvgStageTimer timer(Ui3SvgStage::Parse); }
						{
							SvgStageTimingScope inner(&innerTiming, numericClock);
							{ Ui3SvgStageTimer timer(Ui3SvgStage::Raster); }
							{ SvgStageTimingScope absent(nullptr, numericClock); Ui3SvgStageTimer timer(Ui3SvgStage::Upload); }
						}
						{ Ui3SvgStageTimer timer(Ui3SvgStage::Draw); }
					}
					expect(numericClockReads == 6 && outerTiming.clockReads == 4 && innerTiming.clockReads == 2
						&& outerTiming.calls[0] == 1 && outerTiming.calls[3] == 1 && innerTiming.calls[1] == 1
						&& outerTiming.elapsedMs[0] > 0 && outerTiming.elapsedMs[3] > 0 && innerTiming.elapsedMs[1] > 0
						&& CurrentUi3SvgScope() == nullptr, "N02 numeric-only stages and nested/null scopes restore without proof state");
					Ui3SvgStageTimings combinedTiming;
					const auto combinedBefore = probe->CountersAfterOwnerStopped();
					const auto numericReadsBefore = numericClockReads;
					{
						SvgObservationScope observed(probe.get(), true);
						auto* originalScope = CurrentUi3SvgScope();
						{ SvgStageTimingScope timing(&combinedTiming, numericClock); Ui3SvgStageTimer timer(Ui3SvgStage::Upload); }
						expect(CurrentUi3SvgScope() == originalScope, "N03 numeric timing preserves the active proof scope identity");
					}
					const auto combinedAfter = probe->CountersAfterOwnerStopped();
					expect(combinedAfter.clockReads == combinedBefore.clockReads + 2 && numericClockReads == numericReadsBefore
						&& combinedTiming.clockReads == 2 && combinedTiming.calls[2] == 1
						&& std::abs(combinedAfter.uploadMs - combinedBefore.uploadMs - combinedTiming.elapsedMs[2]) < 0.0000001,
						"N04 dual sinks share one clock pair and retain original probe timing semantics");
					const auto bitmapBeforeTiming = svg.cacheBitmap.Get();
					auto renderNumeric = [&](Ui3SvgStageTimings* timing)
					{
						SvgStageTimingScope numeric(timing);
						expect(CurrentUi3SvgScope() == nullptr, "N05 numeric-only real renderer keeps proof absent");
						dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); dc->Clear(D2D1::ColorF(0,0,0,0));
						(void)renderer.Svg(dc, svg, UiInheritClass(16,16));
						expect(SUCCEEDED(dc->EndDraw()), "N05 real numeric timing draw completes");
						return ReadSvgProofPixels(dc, frameTarget.TargetBitmap());
					};
					Ui3SvgStageTimings realTiming;
					const auto numericOffPixels = renderNumeric(nullptr), numericOnPixels = renderNumeric(&realTiming);
					expect(numericOffPixels == numericOnPixels && numericOnPixels.size() == svgSize * svgSize * 4
						&& svg.cacheBitmap.Get() == bitmapBeforeTiming && realTiming.calls[0] == 0 && realTiming.calls[1] == 0
						&& realTiming.calls[2] == 0 && realTiming.calls[3] == 1 && realTiming.clockReads == 2
						&& probe->CountersAfterOwnerStopped().clockReads == combinedAfter.clockReads
						&& probe->CountersAfterOwnerStopped().drawSubmit == combinedAfter.drawSubmit,
						"N06 numeric on/off is BGRA exact with unchanged actual cache and no proof operations");
					Ui3SvgStageTimings overheadTiming;
					constexpr unsigned overheadIterations = 4096;
					const auto measureTimingCost = [&](Ui3SvgStageTimings* timing)
					{
						SvgStageTimingScope numeric(timing);
						const auto started = std::chrono::steady_clock::now();
						for (unsigned i = 0; i < overheadIterations; ++i) { Ui3SvgStageTimer timer(Ui3SvgStage::Draw); }
						return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - started).count() / overheadIterations;
					};
					const double inactivePairNs = measureTimingCost(nullptr), activePairNs = measureTimingCost(&overheadTiming);
					expect(overheadTiming.calls[3] == overheadIterations && overheadTiming.clockReads == overheadIterations * 2,
						"N07 bounded timing overhead control records exactly one clock pair per stage");
					report << "[Ui3SvgNumericTiming] iterations=" << overheadIterations << " inactive-pair-ns=" << inactivePairNs
						<< " active-pair-ns=" << activePairNs << " extra-pair-ns=" << activePairNs - inactivePairNs << '\n';
					// H1：全Clear之后的未知写不能因成功EndDraw被早先Clear追认。
					auto unknownReplayFrame = [&](bool unknownBeforeClear)
					{
						SvgObservationScope scope(probe.get()); auto target = probe->Target(); target.frameAttemptSerial = ++attempt;
						probe->BeginFrame(1,currentEpoch.generation,attempt); svg.ObserveFiniteRequirement(true); probe->BeginBackingWrite(dc,target);
						dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); frameTarget.PushDirtyClip(dc,fullClip);
						auto ReplayOutsideOldBounds = [&]()
						{
							ObserveUi3SvgUnknownWrite(dc);
							dc->DrawBitmap(svg.cacheBitmap.Get(),D2D1::RectF(64,64,80,80),1.0f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,nullptr);
						};
						if (unknownBeforeClear) ReplayOutsideOldBounds();
						ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0,0,0,0));
						if (!unknownBeforeClear) ReplayOutsideOldBounds();
						frameTarget.PopDirtyClip(dc); const auto resources = probe->FinishDrawing();
						const bool ended = SUCCEEDED(dc->EndDraw()); probe->CompleteAttempt(ended);
						expect(ended && resources.required == 1 && resources.verified == 0,
							"H100 unknown-replay premise: actual source bitmap, D2D writes, unverified frame and successful completion");
						return ended;
					};
					options = {}; svg.enable.Initialization(true); svg.pct.SetDirect(1);
					const auto h1Seed = drawSvg(fullClip,false);
					expect(transactionOk && h1Seed.verified == 1 && svg.cacheBitmap,
						"H100 seed premise: real successful SVG establishes old bounds");
					svg.enable.Initialization(false); svg.pct.SetDirect(0); options.paint = false;
					const bool replayed = unknownReplayFrame(false);
					const auto outsideBefore = ReadEraserTestPixel(dc,frameTarget.TargetBitmap(),72,72);
					expect(replayed && outsideBefore[3] != 0, "H100 hostile replay exists outside old 14..50 bounds");
					const auto hostileHidden = drawSvg(D2D1::RectF(0,0,52,52),false);
					const auto outsideAfter = ReadEraserTestPixel(dc,frameTarget.TargetBitmap(),72,72);
					expect(transactionOk && outsideAfter[3] != 0, "H100 small old-bounds Clear leaves actual outside replay pixels");
					if (replayed && outsideBefore[3] != 0 && transactionOk && outsideAfter[3] != 0)
						expect(hostileHidden.verified == 0 && probe->Observation(svgTag).use != Ui3SvgUse::HiddenExpected,
							"H101 earlier full Clear cannot certify Hidden after later unknown write and successful completion");
					// 反向正确序：先未知写，再真正全Clear，成功后的小Clear可以恢复Hidden证明。
					options = {}; svg.enable.Initialization(true); svg.pct.SetDirect(1); (void)drawSvg(fullClip,false);
					svg.enable.Initialization(false); svg.pct.SetDirect(0); options.paint = false;
					const bool orderedClear = unknownReplayFrame(true);
					const auto orderedHidden = drawSvg(D2D1::RectF(0,0,52,52),false);
					expect(orderedClear && transactionOk && orderedHidden.verified == 1
						&& ReadEraserTestPixel(dc,frameTarget.TargetBitmap(),72,72)[3] == 0,
						"H102 last full Clear after unknown write restores Hidden lineage without stale outside pixels");
					options = {}; svg.enable.Initialization(true); svg.pct.SetDirect(1); (void)drawSvg(fullClip,false);
					{
						SvgObservationScope writing(probe.get()); svg.InitializationFromString(L"not an SVG");
					}
					const auto parseFailure = drawSvg(fullClip, false);
					expect(transactionOk && !submitted && parseFailure.required == 1 && parseFailure.failed == 1 && parseFailure.verified == 0
						&& parseFailure.firstUnverifiedSvgTag == svgTag && parseFailure.firstUnverifiedReason != 0
						&& !svg.cacheBitmap, "B327 natural parse failure retains failure/tag/reason and never ready");
					{
						SvgObservationScope writing(probe.get()); svg.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><rect width="32" height="32" fill="rgba(10,0,7,0)"/></svg>)SVG");
					}
					probe->SetOffscreenFault(Ui3SvgOffscreenFault::RejectRasterResult);
					const auto rasterFailure = drawSvg(fullClip, false);
					expect(transactionOk && !submitted && rasterFailure.failed == 1 && !svg.cacheBitmap && probe->CountersAfterOwnerStopped().rasterFailure == 1,
						"B328 bounded injected rejection after actual raster call never commits bitmap");
					probe->SetOffscreenFault(Ui3SvgOffscreenFault::InvalidUploadAlpha);
					const auto uploadFailure = drawSvg(fullClip, false);
					expect(transactionOk && !submitted && uploadFailure.failed == 1 && !svg.cacheBitmap && probe->CountersAfterOwnerStopped().uploadFailure == 1,
						"B329 fixed injected invalid alpha makes actual CreateBitmap fail without ready commit");
					probe->SetOffscreenFault(Ui3SvgOffscreenFault::None); (void)drawSvg(fullClip, false);
					svg.w.SetDirect(42); svg.h.SetDirect(42); probe->SetOffscreenFault(Ui3SvgOffscreenFault::RejectRasterResult);
					const auto fallback = drawSvg(fullClip, false);
					expect(transactionOk && submitted && fallback.failed == 1 && fallback.verified == 0 && svg.cacheBitmap
						&& probe->Observation(svgTag).use == Ui3SvgUse::QualityFallback, "B330 original old-bitmap quality fallback draws but cannot certify new size");
					probe->SetOffscreenFault(Ui3SvgOffscreenFault::None); svg.w.SetDirect(32); svg.h.SetDirect(32);
					// R1：真实资源summary只经共享observer/停后吸收；NoChange保留引用而不造新commit。
					auto checkLedger = [&](const Ui3FiniteResourceProof& resource)
					{
						Ui3FiniteSignature initial;
						initial.flags = 65; initial.stateMode = 1; initial.penColorRgb = RGB(220,110,40);
						initial.penWidthBits = std::bit_cast<std::uint32_t>(3.0f); initial.dpi = 96; initial.toolRevision = 1;
						initial.displaySerial = 2; initial.configZoomBits = std::bit_cast<std::uint64_t>(1.0); initial.validMask = Ui3FiniteRequiredMask;
						auto publication = std::make_unique<Ui3FinitePublication>(0xB331, initial);
						auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
						auto goal = initial; goal.flags = 64;
						auto mutation = publication->BeginMutation(1, Ui3FiniteScene::MainFold, 1);
						publication->MarkBusinessAccepted(mutation); publication->FinishAtRenderRequest(mutation, goal, 0);
						Ui3FiniteAccepted accepted; const bool stable = observer->SnapshotAccepted(accepted);
						observer->BeginFrame(accepted, resource.epoch, resource.frameAttemptSerial);
						const bool consumed = observer->MarkConsumed(goal, 1, 1);
						for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
						auto candidate = observer->SettleCandidate(resource.surfaceSerial, svgSize, svgSize);
						expect(stable && consumed && candidate.settled, "B331 ledger premise: legal shared publication and candidate");
						candidate = observer->FinalizeResources(candidate, resource);
						Ui3FiniteCommitIdentity identity; identity.epoch = resource.epoch; identity.surfaceSerial = resource.surfaceSerial;
						identity.frameAttemptSerial = resource.frameAttemptSerial; identity.targetWidth = identity.targetHeight = svgSize;
						(void)observer->CompleteAttempt(candidate, true, false, 0, identity);
						expect(publication->CompletedRevision() == (resource.verified == resource.required && resource.required != 0 ? accepted.revision : 0),
							"B331 missing/partial/failure does not publish completed revision receipt");
						auto noChange = publication->BeginMutation(2, Ui3FiniteScene::MainFold, 2);
						publication->MarkBusinessAccepted(noChange); publication->FinishAtRenderRequest(noChange, goal, 0);
						observer->SealAfterRenderStopped(); publication->AbsorbAfterOwnersStopped(observer->RecordsAfterRenderStopped());
						const auto rows = publication->RecordsAfterOwnerStopped();
						expect(rows.size() == 2, "B331 two request rows retained");
						for (const auto& row : rows) expect(row.requiredSvg == resource.required && row.verifiedSvg == resource.verified
							&& row.failedSvg == resource.failed && row.unverifiedSvg == resource.unverified
							&& row.requiredSvg == row.verifiedSvg + row.failedSvg + row.unverifiedSvg
							&& row.firstUnverifiedSvgTag == resource.firstUnverifiedSvgTag && row.firstUnverifiedReason == resource.firstUnverifiedReason,
							"B331 full/partial/missing/API failure summary survives Complete and stopped Absorb");
						if (rows.size() == 2) expect(rows[1].reusedRevision == 1 && rows[1].finalCommitTicks == 0 && !rows[1].timingValid,
							"B331 no-change references real resource outcome without new commit time");
					};
					checkLedger(full); checkLedger(partial); checkLedger(missingDraw); checkLedger(parseFailure); checkLedger(uploadFailure);
					// R2：initial publication0经真实CacheBitmap，再由合法目标复用同一个bitmap。
					Ui3FiniteSignature initialDpi;
					initialDpi.flags = 65; initialDpi.stateMode = 1; initialDpi.penColorRgb = RGB(220,110,40);
					initialDpi.penWidthBits = std::bit_cast<std::uint32_t>(3.0f); initialDpi.dpi = 192; initialDpi.toolRevision = 1;
					initialDpi.displaySerial = 2; initialDpi.configZoomBits = std::bit_cast<std::uint64_t>(1.0); initialDpi.validMask = Ui3FiniteRequiredMask;
					auto initialPublication = std::make_unique<Ui3FinitePublication>(0xB332, initialDpi);
					auto initialObserver = std::make_unique<Ui3FiniteObserver>(*initialPublication);
					initialObserver->BeginFrame({}, currentEpoch.generation, attempt + 1, true);
					const bool initialConsumed = initialObserver->MarkConsumed(initialDpi, 1, 1);
					for (unsigned role = 1; role <= 6; ++role) initialObserver->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
					auto initialCandidate = initialObserver->SettleCandidate(1, svgSize, svgSize);
					expect(initialConsumed && initialCandidate.renderDpi == 192 && initialCandidate.accepted.stepId == 0,
						"B332 initial candidate locks real consumed DPI without fabricated accepted goal");
					options = {}; options.revision = 0; options.dpi = initialCandidate.renderDpi; options.zoom = 2.0; options.x = options.y = 8;
					{ SvgObservationScope reset(probe.get()); svg.ResetCache(); }
					const auto initialResources = drawSvg(fullClip, false); const auto initialCreates = probe->CountersAfterOwnerStopped();
					const auto* initialBitmap = svg.cacheBitmap.Get();
					initialCandidate = initialObserver->FinalizeResources(initialCandidate, initialResources);
					Ui3FiniteCommitIdentity initialIdentity; initialIdentity.epoch = currentEpoch.generation; initialIdentity.surfaceSerial = 1;
					initialIdentity.frameAttemptSerial = attempt; initialIdentity.targetWidth = initialIdentity.targetHeight = svgSize;
					(void)initialObserver->CompleteAttempt(initialCandidate, true, false, 0, initialIdentity);
					Ui3FixtureReadyValue initialReady;
					expect(initialResources.verified == 1 && probe->Observation(svgTag).used.dpi == 192 && initialObserver->TryReadReady(initialReady)
						&& (initialReady.flags & Ui3FiniteReadyLayoutStable) != 0, "B332 real initial cache supplies nonzero DPI and pure committed initial layout");
					auto nextSignature = initialDpi; nextSignature.flags = 64;
					auto nextMutation = initialPublication->BeginMutation(1, Ui3FiniteScene::MainFold, 1);
					initialPublication->MarkBusinessAccepted(nextMutation); initialPublication->FinishAtRenderRequest(nextMutation, nextSignature, 0);
					Ui3FiniteAccepted nextAccepted; const bool nextStable = initialObserver->SnapshotAccepted(nextAccepted);
					initialObserver->BeginFrame(nextAccepted, currentEpoch.generation, attempt + 1);
					const bool nextConsumed = initialObserver->MarkConsumed(nextSignature, 1, 1);
					for (unsigned role = 1; role <= 6; ++role) initialObserver->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
					auto nextCandidate = initialObserver->SettleCandidate(1, svgSize, svgSize); options.revision = nextAccepted.revision;
					const auto reused = drawSvg(fullClip, false); nextCandidate = initialObserver->FinalizeResources(nextCandidate, reused);
					initialIdentity.frameAttemptSerial = attempt;
					const auto nextOutcome = initialObserver->CompleteAttempt(nextCandidate, true, false, 0, initialIdentity);
					expect(nextStable && nextConsumed && nextCandidate.renderDpi == 192 && nextOutcome == Ui3FiniteStatus::CompletedLayoutAndSvg
						&& svg.cacheBitmap.Get() == initialBitmap && probe->CountersAfterOwnerStopped().createSuccess == initialCreates.createSuccess
						&& probe->CountersAfterOwnerStopped().uploadCalls == initialCreates.uploadCalls,
						"B333 accepted goal reuses actual initial bitmap without diagnostic rebuild");
					options.dpi = 144; options.zoom = 1.5;
					const auto dpiChanged = drawSvg(fullClip, false);
					expect(transactionOk && dpiChanged.verified == 1 && probe->Observation(svgTag).used.dpi == 144
						&& probe->Observation(svgTag).used.pixelWidth == 48, "B334 changed typed DPI uses original real zoom/raster policy");
					options.dpi = 96; // 相同zoom与bitmap，只有proof的DPI不匹配时不得重建来凑数。
					const auto dpiMismatchBefore = probe->CountersAfterOwnerStopped();
					expect(drawSvg(fullClip, false).verified == 0 && probe->CountersAfterOwnerStopped().uploadCalls == dpiMismatchBefore.uploadCalls,
						"B335 DPI mismatch on reused bitmap remains unverified without extra upload");
					options = {};
					(void)drawSvg(fullClip, false);
					UiSVGClass second;
					{
						SvgObservationScope owned(probe.get(), false, true);
						second.Initialization(0,0,RGB(90,40,220),std::nullopt);
						second.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"><rect width="24" height="24" fill="rgba(10,0,7,0)"/></svg>)SVG");
						second.w.SetDirect(24); second.h.SetDirect(24); second.pct.SetDirect(1); second.enable.Initialization(true);
						expect(second.BindObservationTag(svgTag + 1), "B336 second owned SVG tag binds");
					}
					auto drawPair = [&](bool overlaps)
					{
						SvgObservationScope scope(probe.get());
						auto target = probe->Target(); target.revision = 1; target.frameAttemptSerial = ++attempt;
						probe->BeginFrame(1, currentEpoch.generation, attempt);
						svg.ObserveFiniteRequirement(true); second.ObserveFiniteRequirement(true);
						probe->BeginBackingWrite(dc, target); dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix());
						frameTarget.PushDirtyClip(dc, fullClip); ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0,0,0,0));
						const bool a = renderer.Svg(dc, svg, UiInheritClass(8,8));
						const bool b = renderer.Svg(dc, second, UiInheritClass(overlaps ? 24 : 64, overlaps ? 24 : 64));
						frameTarget.PopDirtyClip(dc); const auto resource = probe->FinishDrawing();
						const bool ended = SUCCEEDED(dc->EndDraw()); probe->CompleteAttempt(ended);
						expect(a && b && ended, "B336 real two-SVG draw/EndDraw premise"); return resource;
					};
					const auto disjoint = drawPair(false);
					expect(disjoint.required == 2 && disjoint.verified == 2, "B336 actual disjoint SVG B preserves A full proof");
					const auto overlapping = drawPair(true);
					expect(overlapping.required == 2 && overlapping.verified == 1 && overlapping.unverified == 1
						&& overlapping.firstUnverifiedSvgTag == svgTag && probe->Observation(svgTag).use == Ui3SvgUse::Unverified,
						"B337 actual overlapping SVG B invalidates already submitted A");

					// B35：真实内嵌主logo的正常着色层叠必须完整证明；普通相交SVG/B337仍保守。
					{
						auto logos = std::make_unique<Ui3SvgProbe>(0xB350);
						UiSVGClass baseLogo, inkLogo;
						bool logosBound = false;
						{
							SvgObservationScope initialization(logos.get(), false, true);
							baseLogo.Initialization(0, 0, std::nullopt, std::nullopt); baseLogo.InitializationFromResource(L"UI", L"logo1");
							inkLogo.Initialization(0, 0, RGB(40, 170, 90), std::nullopt); inkLogo.InitializationFromResource(L"UI", L"Frame94");
							for (auto* logo : { &baseLogo, &inkLogo }) { logo->w.SetDirect(48); logo->h.SetDirect(48); logo->pct.SetDirect(1); logo->enable.Initialization(true); }
							logosBound = baseLogo.BindObservationTag(0x10000u) && inkLogo.BindObservationTag(0x10001u);
						}
						expect(logosBound, "B350 actual embedded logo1 and Frame94 owned tags bind");
						std::uint64_t logoAttempt = 0;
						auto paintLogos = [&](unsigned fault, bool observed = true)
						{
							SvgObservationScope scope(observed ? logos.get() : nullptr);
							Ui3SvgFrameTarget target; target.revision = 1; target.epoch = currentEpoch.generation; target.surfaceSerial = 1;
							target.frameAttemptSerial = ++logoAttempt; target.width = target.height = target.backingWidth = target.backingHeight = svgSize;
							target.dpi = 96; target.windowAlpha = 255; target.zoomBits = std::bit_cast<std::uint64_t>(1.0);
							for (unsigned i = 0; i < 4; ++i) target.viewportBits[i] = std::bit_cast<std::uint32_t>(i < 2 ? 0.0f : static_cast<float>(svgSize));
							if (observed) { logos->BeginFrame(1, currentEpoch.generation, logoAttempt); baseLogo.ObserveFiniteRequirement(true); inkLogo.ObserveFiniteRequirement(true); logos->BeginBackingWrite(dc, target); }
							dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); frameTarget.PushDirtyClip(dc, fullClip);
							ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0, 0, 0, 0));
							const auto savedInk = inkLogo.cacheBitmap;
							if (fault == 2 && observed) logos->DeclareMainLogoInkComposition(dc, &baseLogo, &inkLogo); // 底层还未提交，声明不可授权倒序。
							const bool first = renderer.Svg(dc, fault == 2 ? inkLogo : baseLogo, UiInheritClass(16, 16));
							if (fault == 3) { ObserveUi3SvgUnknownWrite(dc); dc->DrawBitmap(baseLogo.cacheBitmap.Get(), D2D1::RectF(72, 72, 88, 88), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr); }
							if (observed && fault != 1 && fault != 2) logos->DeclareMainLogoInkComposition(dc, fault == 8 ? &inkLogo : &baseLogo, &inkLogo);
							if (fault == 4) inkLogo.cacheBitmap = baseLogo.cacheBitmap; // 真实错误bitmap对象，typed proof不能追认。
							if (fault == 5) inkLogo.angle.SetDirect(90);
							if (fault == 6)
							{
								// frame dirty入口不允许二次Push；nested反例必须真正压入D2D clip，并沿原producer记录。
								const auto clip = D2D1::RectF(16, 16, 30, 30);
								dc->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
								D2D1_MATRIX_3X2_F effective{}; dc->GetTransform(&effective);
								const float rect[4]{ clip.left, clip.top, clip.right, clip.bottom };
								const float matrix[6]{ effective._11, effective._12, effective._21, effective._22, effective._31, effective._32 };
								std::uint32_t r[4], m[6];
								for (unsigned i = 0; i < 4; ++i) r[i] = std::bit_cast<std::uint32_t>(rect[i]);
								for (unsigned i = 0; i < 6; ++i) m[i] = std::bit_cast<std::uint32_t>(matrix[i]);
								ObserveUi3SvgClipPush(dc, r, m);
							}
							const bool last = renderer.Svg(dc, fault == 2 ? baseLogo : inkLogo, UiInheritClass(16, 16));
							if (fault == 6) { ObserveUi3SvgClipPop(dc); dc->PopAxisAlignedClip(); }
							if (fault == 7) { ObserveUi3SvgUnknownWrite(dc); dc->DrawBitmap(baseLogo.cacheBitmap.Get(), D2D1::RectF(72, 72, 88, 88), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr); }
							if (fault == 4) inkLogo.cacheBitmap = savedInk;
							inkLogo.angle.SetDirect(0);
							frameTarget.PopDirtyClip(dc); const auto resources = observed ? logos->FinishDrawing() : Ui3FiniteResourceProof{};
							const bool ended = SUCCEEDED(dc->EndDraw()); if (observed) logos->CompleteAttempt(ended);
							expect(first && last && ended, "B350 real logo pair DrawBitmap/EndDraw premise");
							return resources;
						};
						const auto composed = paintLogos(0); const auto composedPixels = ReadSvgProofPixels(dc, frameTarget.TargetBitmap());
						(void)paintLogos(0, false); const auto ordinaryPixels = ReadSvgProofPixels(dc, frameTarget.TargetBitmap());
						expect(composed.required == 2 && composed.verified == 2 && composed.failed == 0 && composed.unverified == 0
							&& composedPixels.size() == svgSize * svgSize * 4 && composedPixels == ordinaryPixels,
							"B351 declared actual logo composition proves both resources with identical entire ordinary BGRA");
						for (unsigned fault = 1; fault <= 8; ++fault)
						{
							const auto beforeVariant = logos->CountersAfterOwnerStopped();
							const auto resources = paintLogos(fault); const auto ink = logos->Observation(0x10001u);
							const auto afterVariant = logos->CountersAfterOwnerStopped();
							report << "[Ui3B352] variant=" << fault << " required=" << resources.required << " verified=" << resources.verified
								<< " failed=" << resources.failed << " unverified=" << resources.unverified << " firstTag=" << resources.firstUnverifiedSvgTag
								<< " reason=" << resources.firstUnverifiedReason << " inkSemantic=" << ink.used.semanticKnown << " inkQuality=" << ink.qualityMatches
								<< " inkCoverage=" << static_cast<unsigned>(ink.coverage) << " createDelta=" << afterVariant.createSuccess - beforeVariant.createSuccess << '\n';
							expect(resources.verified < 2, "B352 undeclared/reversed/unknown/wrong-bitmap/transform/partial/wrong-object composition remains unverified");
						}
					}
					// 纯Shape/Superellipse/CLIP文字真实同序写入：不相交保持SVG，相交覆盖必须拒证。
					{
						UIRendering pure(MakeMainBarRenderingServices(&owner)); pure.ConfigureLocalizedTypography(); pure.SetFrameZoom(1);
						options = {}; svg.enable.Initialization(true); svg.pct.SetDirect(1);
						const auto untouched = drawSvg(fullClip, false); const auto originalPixel = ReadEraserTestPixel(dc, frameTarget.TargetBitmap(), 32, 32);
						auto paintPure = [&](bool overlaps)
						{
							SvgObservationScope scope(probe.get()); auto target = probe->Target(); target.frameAttemptSerial = ++attempt;
							probe->BeginFrame(1, currentEpoch.generation, attempt); svg.ObserveFiniteRequirement(true); probe->BeginBackingWrite(dc, target);
							dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); frameTarget.PushDirtyClip(dc, fullClip);
							ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0, 0, 0, 0));
							UiShapeClass background(0, 0, 32, 32, 0, 0, 1, RGB(255, 255, 255), std::nullopt); background.enable.Initialization(true);
							bool painted = pure.Shape(dc, background, UiInheritClass(56, 56));
							painted = renderer.Svg(dc, svg, UiInheritClass(16, 16)) && painted;
							UiWordClass word(0, 0, 28, 28, L"文", 14, RGB(20, 20, 20)); word.enable.Initialization(true);
							painted = pure.Word(dc, word, UiInheritClass(60, 60)) && painted;
							UiSuperellipseClass shape(0, 0, 28, 28, 3, 1, RGB(255, 255, 255), std::nullopt); shape.enable.Initialization(true);
							painted = pure.Superellipse(dc, shape, UiInheritClass(overlaps ? 24 : 56, overlaps ? 24 : 56)) && painted;
							frameTarget.PopDirtyClip(dc); const auto resources = probe->FinishDrawing();
							const bool ended = SUCCEEDED(dc->EndDraw()); probe->CompleteAttempt(ended);
							expect(painted && ended, "B353 actual Shape/SVG/CLIP Word/Superellipse same-order D2D premise"); return resources;
						};
						expect(untouched.verified == 1 && paintPure(false).verified == 1
							&& ReadEraserTestPixel(dc, frameTarget.TargetBitmap(), 32, 32) == originalPixel,
							"B354 actual disjoint pure writes retain SVG proof and visible pixel");
						const auto covered = paintPure(true);
						expect(covered.verified == 0 && covered.firstUnverifiedReason == static_cast<unsigned>(Ui3SvgProofReason::Overwrite)
							&& ReadEraserTestPixel(dc, frameTarget.TargetBitmap(), 32, 32) != originalPixel,
							"B355 actual overlapping pure geometry remains rejected with visibly covered SVG");
					}
					// Hidden域外资格仅沿同epoch/surface的实际mapped写域，未知重放及失败事务不能绕过。
					{
						auto hiddenProbe = std::make_unique<Ui3SvgProbe>(0xB356); UiSVGClass offViewport;
						{
							SvgObservationScope initialization(hiddenProbe.get(), false, true);
							offViewport.Initialization(0, 0, RGB(220, 110, 40), std::nullopt);
							offViewport.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="rgba(10,0,7,0)"/></svg>)SVG");
							offViewport.w.SetDirect(16); offViewport.h.SetDirect(16); offViewport.enable.Initialization(true); offViewport.pct.SetDirect(1);
							expect(offViewport.BindObservationTag(0x10002u), "B356 actual outside SVG owned tag binds");
						}
						std::uint64_t hiddenAttempt = 0;
						auto paintHidden = [&](bool visible, bool mappedInside = false, bool unknownReplay = false, bool committed = true, bool expandedViewport = false)
						{
							SvgObservationScope scope(hiddenProbe.get()); offViewport.enable.Initialization(visible); offViewport.pct.SetDirect(visible ? 1 : 0);
							Ui3SvgFrameTarget target; target.revision = 1; target.epoch = currentEpoch.generation; target.surfaceSerial = 1; target.frameAttemptSerial = ++hiddenAttempt;
							target.width = target.height = visible ? svgSize : expandedViewport ? 48 : 32; target.backingWidth = target.backingHeight = svgSize;
							target.dpi = 96; target.windowAlpha = 255; target.zoomBits = std::bit_cast<std::uint64_t>(1.0);
							for (unsigned i = 0; i < 4; ++i) target.viewportBits[i] = std::bit_cast<std::uint32_t>(
								i < 2 ? (expandedViewport ? 48.0f : 0.0f) : expandedViewport ? 96.0f : static_cast<float>(target.width));
							hiddenProbe->BeginFrame(1, currentEpoch.generation, hiddenAttempt); offViewport.ObserveFiniteRequirement(true); hiddenProbe->BeginBackingWrite(dc, target);
							dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix());
							// negative保留当前viewport中的旧图，不把“已清除”误作“域外”证据。
							frameTarget.PushDirtyClip(dc, visible ? fullClip : mappedInside ? D2D1::RectF(32, 32, svgSize, svgSize) : D2D1::RectF(0, 0, 32, 32));
							ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0, 0, 0, 0));
							if (visible) { if (mappedInside) dc->SetTransform(D2D1::Matrix3x2F::Translation(-56, -56)); (void)renderer.Svg(dc, offViewport, UiInheritClass(64, 64)); }
							if (unknownReplay) { ObserveUi3SvgUnknownWrite(dc); dc->DrawBitmap(offViewport.cacheBitmap.Get(), D2D1::RectF(8, 8, 24, 24), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr); }
							dc->SetTransform(D2D1::IdentityMatrix()); frameTarget.PopDirtyClip(dc); const auto resources = hiddenProbe->FinishDrawing();
							const bool ended = SUCCEEDED(dc->EndDraw()); hiddenProbe->CompleteAttempt(ended && committed);
							expect(ended, "B356 actual viewport/clear/mapped SVG/EndDraw premise"); return resources;
						};
						expect(paintHidden(true).verified == 1 && ReadEraserTestPixel(dc, frameTarget.TargetBitmap(), 72, 72)[3] != 0, "B356 old full mapped paint established");
						const auto outside = paintHidden(false);
						expect(outside.required == 1 && outside.verified == 1 && hiddenProbe->Observation(0x10002u).outsidePresentedViewport
							&& !hiddenProbe->Observation(0x10002u).clearCoversOldBounds && ReadEraserTestPixel(dc, frameTarget.TargetBitmap(), 72, 72)[3] != 0
							&& ReadEraserTestPixel(dc, frameTarget.TargetBitmap(), 16, 16)[3] == 0,
							"B357 known old SVG entirely outside actual viewport is proven absent there without claiming backing Clear");
						// 不重新绘制SVG，直接把非零源viewport扩回保留旧像素；必须仍Require并拒绝Hidden。
						const auto expanded = paintHidden(false, false, false, true, true);
						expect(expanded.required == 1 && expanded.verified == 0 && expanded.unverified == 1
							&& hiddenProbe->NeedsHiddenProof(0x10002u) && hiddenProbe->Observation(0x10002u).use != Ui3SvgUse::HiddenExpected
							&& std::bit_cast<float>(hiddenProbe->Target().viewportBits[0]) == 48.0f
							&& std::bit_cast<float>(hiddenProbe->Target().viewportBits[1]) == 48.0f
							&& ReadEraserTestPixel(dc, frameTarget.TargetBitmap(), 72, 72)[3] != 0,
							"B362 outside success retains actual old paint: no repaint, source48,48 expansion requires and rejects still-visible old SVG");
					// B363：沿用像素只在先前全覆盖提交、同内容/几何/透明度且本帧clip可知时允许。
					{
						auto retainedProbe = std::make_unique<Ui3SvgProbe>(0xB363);
						UiSVGClass retainedSvg;
						{
							SvgObservationScope initialization(retainedProbe.get(), false, true);
							retainedSvg.Initialization(0, 0, RGB(220, 110, 40), std::nullopt);
							retainedSvg.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><rect width="32" height="32" fill="rgba(220,110,40,0.75)"/></svg>)SVG");
							retainedSvg.w.SetDirect(32); retainedSvg.h.SetDirect(32); retainedSvg.pct.SetDirect(1);
							retainedSvg.enable.Initialization(true);
							expect(retainedSvg.BindObservationTag(0x10003u), "B363 actual owned retained SVG tag binds");
						}
						std::uint64_t retainedAttempt = 0;
						auto paintRetained = [&](const D2D1_RECT_F& clip, bool unknownAfter = false, bool commit = true)
						{
							SvgObservationScope scope(retainedProbe.get()); renderer.SetFrameZoom(1.0);
							Ui3SvgFrameTarget target; target.revision = 1; target.epoch = currentEpoch.generation; target.surfaceSerial = 1;
							target.frameAttemptSerial = ++retainedAttempt; target.width = target.height = target.backingWidth = target.backingHeight = svgSize;
							target.dpi = 96; target.windowAlpha = 255; target.zoomBits = std::bit_cast<std::uint64_t>(1.0);
							for (unsigned i = 0; i < 4; ++i) target.viewportBits[i] = std::bit_cast<std::uint32_t>(i < 2 ? 0.0f : static_cast<float>(svgSize));
							retainedProbe->BeginFrame(1, currentEpoch.generation, retainedAttempt);
							retainedSvg.ObserveFiniteRequirement(true); retainedProbe->BeginBackingWrite(dc, target);
							dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); frameTarget.PushDirtyClip(dc, clip);
							ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0, 0, 0, 0));
							const bool drawn = renderer.Svg(dc, retainedSvg, UiInheritClass(16, 16));
							if (unknownAfter) { ObserveUi3SvgUnknownWrite(dc); dc->DrawBitmap(retainedSvg.cacheBitmap.Get(), D2D1::RectF(20, 20, 44, 44), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr); }
							frameTarget.PopDirtyClip(dc); const auto proof = retainedProbe->FinishDrawing();
							const bool ended = SUCCEEDED(dc->EndDraw()); retainedProbe->CompleteAttempt(ended && commit);
							expect(drawn && ended, "B363 actual same-bitmap D2D draw and EndDraw premise");
							return proof;
						};
						auto reportRetained = [&](const char* stage, const Ui3FiniteResourceProof& proof)
						{
							const auto observation = retainedProbe->Observation(0x10003u);
							report << "[Ui3B363] stage=" << stage << " required=" << proof.required << " verified=" << proof.verified
								<< " failed=" << proof.failed << " unverified=" << proof.unverified
								<< " reason=" << proof.firstUnverifiedReason << " coverage=" << static_cast<unsigned>(observation.coverage)
								<< " use=" << static_cast<unsigned>(observation.use) << " bitmap=" << observation.used.ready << ',' << observation.used.semanticKnown
								<< " quality=" << observation.qualityMatches << " opacity=" << std::bit_cast<float>(observation.finalOpacityBits)
								<< " dest=" << std::bit_cast<float>(observation.destBits[0]) << ',' << std::bit_cast<float>(observation.destBits[1])
								<< ',' << std::bit_cast<float>(observation.destBits[2]) << ',' << std::bit_cast<float>(observation.destBits[3]) << '\n';
						};
						const auto seeded = paintRetained(fullClip);
						const auto seedObservation = retainedProbe->Observation(0x10003u);
						reportRetained("seed", seeded);
						const auto referencePixels = ReadSvgProofPixels(dc, frameTarget.TargetBitmap());
						const auto disjoint = paintRetained(D2D1::RectF(60, 60, 90, 90));
						reportRetained("disjoint", disjoint);
						const auto disjointPixels = ReadSvgProofPixels(dc, frameTarget.TargetBitmap());
						const bool disjointPixelsSame = referencePixels.size() == svgSize * svgSize * 4 && disjointPixels == referencePixels;
						report << "[Ui3B363Pixels] stage=disjoint same=" << disjointPixelsSame << " bytes=" << disjointPixels.size() << '\n';
						expect(seeded.required == 1 && seeded.verified == 1 && seedObservation.use == Ui3SvgUse::DrawnVerified,
							"B363 seed is a fully visible, exact D2D SVG proof");
						expect(disjointPixelsSame, "B363 disjoint dirty Clear/D2D draw preserves exact previously committed BGRA");
						expect(disjoint.required == 1 && disjoint.verified == 1 && disjoint.unverified == 0
							&& retainedProbe->Observation(0x10003u).coverage == Ui3SvgCoverage::Empty
							&& retainedProbe->Observation(0x10003u).use == Ui3SvgUse::RetainedVerified,
							"B363 known disjoint dirty clip inherits only the exact previous full-coverage proof");
						const auto partial = paintRetained(D2D1::RectF(32, 0, 60, svgSize));
						reportRetained("partial", partial);
						const auto partialPixels = ReadSvgProofPixels(dc, frameTarget.TargetBitmap());
						const bool partialPixelsSame = partialPixels == referencePixels;
						report << "[Ui3B363Pixels] stage=partial same=" << partialPixelsSame << " bytes=" << partialPixels.size() << '\n';
						expect(partialPixelsSame, "B363 same-bitmap partial redraw recomposes exact previously committed BGRA");
						expect(partial.required == 1 && partial.verified == 0 && partial.unverified == 1
							&& retainedProbe->Observation(0x10003u).coverage == Ui3SvgCoverage::Partial
							&& retainedProbe->Observation(0x10003u).use == Ui3SvgUse::Unverified,
							"B363 partially overlapping dirty clip remains unverified even when D2D pixels happen to match");
						(void)paintRetained(fullClip);
						retainedSvg.pct.SetDirect(0.5);
						const auto changedOpacity = paintRetained(D2D1::RectF(60, 60, 90, 90));
						reportRetained("opacity", changedOpacity);
						expect(changedOpacity.verified == 0 && changedOpacity.unverified == 1,
							"B363 changed expected opacity cannot borrow previous full-coverage proof");
						retainedSvg.pct.SetDirect(1); (void)paintRetained(fullClip);
						const auto unknown = paintRetained(fullClip, true);
						reportRetained("unknown", unknown);
						expect(unknown.verified == 0 && unknown.unverified == 1,
							"B363 later unknown write still revokes visible SVG proof");
						(void)paintRetained(fullClip);
						const auto failedCommit = paintRetained(D2D1::RectF(60, 60, 90, 90), false, false);
						const auto afterFailedCommit = paintRetained(D2D1::RectF(60, 60, 90, 90));
						reportRetained("after-failed-commit", afterFailedCommit);
						expect(failedCommit.verified == 1 && afterFailedCommit.verified == 0 && afterFailedCommit.unverified == 1,
							"B363 failed presentation cannot leave a reusable retained-pixel proof");
					}
						// 消费者前提必须使用支持的closed-aux标记与偶数display serial，不能拿Unsupported伪作失败提交。
						Ui3FiniteSignature initial; initial.flags = 64; initial.stateMode = 1; initial.penColorRgb = RGB(10, 20, 30); initial.penWidthBits = std::bit_cast<std::uint32_t>(3.0f);
						initial.toolRevision = 1; initial.dpi = 96; initial.displaySerial = 2; initial.configZoomBits = std::bit_cast<std::uint64_t>(1.0); initial.validMask = Ui3FiniteRequiredMask;
						auto publication = std::make_unique<Ui3FinitePublication>(0xB358, initial); auto goal = initial; goal.flags ^= 1;
						auto mutation = publication->BeginMutation(1, Ui3FiniteScene::MainFold, 1); publication->MarkBusinessAccepted(mutation); publication->FinishAtRenderRequest(mutation, goal, 0);
						auto observer = std::make_unique<Ui3FiniteObserver>(*publication); Ui3FiniteAccepted accepted; const bool stable = observer->SnapshotAccepted(accepted); observer->BeginFrame(accepted, outside.epoch, outside.frameAttemptSerial);
						const bool consumed = observer->MarkConsumed(goal, 1, 1); for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
						auto candidate = observer->SettleCandidate(outside.surfaceSerial, 32, 32); candidate = observer->FinalizeResources(candidate, outside);
						expect(stable && accepted.status == Ui3FiniteStatus::Accepted && consumed && candidate.settled, "B358 legal accepted/consumed/settled production premise");
						const auto completion = observer->CompleteAttempt(candidate, false, false, 0);
						report << "[Ui3B358] stable=" << stable << " status=" << static_cast<unsigned>(accepted.status) << " flags=" << accepted.signature.flags
							<< " display=" << accepted.signature.displaySerial << " revision=" << accepted.revision << " publication=" << accepted.publicationSerial
							<< " consumed=" << consumed << " settled=" << candidate.settled << " svgComplete=" << candidate.svgProofComplete
							<< " candidate=" << candidate.accepted.revision << ',' << candidate.epoch << ',' << candidate.surfaceSerial << ',' << candidate.frameAttemptSerial
							<< " resource=" << outside.revision << ',' << outside.epoch << ',' << outside.surfaceSerial << ',' << outside.frameAttemptSerial
							<< " required=" << outside.required << " verified=" << outside.verified << " producer=" << outside.producerPresent
							<< " complete=" << static_cast<unsigned>(completion) << " completedRevision=" << publication->CompletedRevision() << '\n';
						expect(candidate.svgProofComplete && completion == Ui3FiniteStatus::Pending && publication->CompletedRevision() == 0, "B358 actual staged outside proof cannot complete failed present transaction");
						(void)paintHidden(true, true); const auto visibleOld = paintHidden(false, true);
						expect(visibleOld.verified == 0 && !hiddenProbe->Observation(0x10002u).outsidePresentedViewport
							&& ReadEraserTestPixel(dc, frameTarget.TargetBitmap(), 16, 16)[3] != 0, "B359 actual mapped old bounds in viewport remain unverified");
						(void)paintHidden(true); const auto replay = paintHidden(false, false, true);
						expect(replay.verified == 0 && ReadEraserTestPixel(dc, frameTarget.TargetBitmap(), 16, 16)[3] != 0,
							"B360 actual unknown replay into viewport prevents outside Hidden qualification");
						(void)paintHidden(true); const auto failedOutside = paintHidden(false, false, false, false);
						const auto afterFailedOutside = paintHidden(false);
						expect(failedOutside.verified == 1 && afterFailedOutside.verified == 0,
							"B361 real D2D write plus failed/deferred software completion revokes later outside Hidden proof");
					}
					// 真实独立WARP/D2D device epoch，局部SVG显式Reset沿原资源释放函数。
					RenderPipeline::DeviceEpoch nextEpoch; nextEpoch.backend = RenderPipeline::Backend::Warp; nextEpoch.generation = epoch.generation + 1;
					const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
					HRESULT nextHr = D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,
						nextEpoch.d3dDevice.GetAddressOf(),&nextEpoch.featureLevel,nextEpoch.immediateContext.GetAddressOf());
					if (SUCCEEDED(nextHr)) nextHr = nextEpoch.d3dDevice.As(&nextEpoch.dxgiDevice);
					const auto factory = RenderPipeline::D2DFactory();
					if (SUCCEEDED(nextHr)) nextHr = factory ? factory->CreateDevice(nextEpoch.dxgiDevice.Get(),nextEpoch.d2dDevice.GetAddressOf()) : E_POINTER;
					expect(SUCCEEDED(nextHr), "B338 independent real WARP/D2D epoch premise");
					if (SUCCEEDED(nextHr))
					{
						{ SvgObservationScope reset(probe.get()); svg.ResetCache(); second.ResetCache(); }
						nextHr = frameTarget.Ensure(nextEpoch, svgSize, svgSize); dc = frameTarget.DeviceContext();
						expect(SUCCEEDED(nextHr) && dc, "B338 actual context/target recreate premise");
						if (SUCCEEDED(nextHr) && dc)
						{
							currentEpoch = nextEpoch; options = {}; options.surface = 2;
							const auto newEpoch = drawSvg(fullClip, false);
							expect(transactionOk && newEpoch.verified == 1 && probe->Observation(svgTag).used.epoch == nextEpoch.generation
								&& probe->Observation(svgTag).used.surfaceSerial == 2, "B338 typed cache proof follows actual independent epoch upload");
						}
					}
					// 未观察创建的真实旧cache不能由owned-tag绑定追認为known。
					UiSVGClass oldCache;
					oldCache.Initialization(0,0,std::nullopt,std::nullopt);
					oldCache.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20"><rect width="20" height="20" fill="red"/></svg>)SVG");
					oldCache.w.SetDirect(20); oldCache.h.SetDirect(20); oldCache.enable.Initialization(true); oldCache.pct.SetDirect(1);
					expect(oldCache.CacheBitmap(dc,20,20,renderer.GetDeviceGeneration()), "B339 real unobserved old-cache premise");
					auto oldProbe = std::make_unique<Ui3SvgProbe>(0xB339);
					{
						SvgObservationScope owned(oldProbe.get(),false,true);
						expect(oldCache.BindObservationTag(0x20000) && !oldCache.ObservedBitmapProof().semanticKnown
							&& oldProbe->CountersAfterOwnerStopped().unknownReadyEntries == 1, "B339 binding tag does not certify unknown old bitmap");
					}
					{
						SvgObservationScope scope(oldProbe.get()); auto target = probe->Target(); target.frameAttemptSerial = 1;
						oldProbe->BeginFrame(1,currentEpoch.generation,1); oldCache.ObserveFiniteRequirement(true); oldProbe->BeginBackingWrite(dc,target);
						dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); frameTarget.PushDirtyClip(dc,fullClip);
						ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0,0,0,0)); (void)renderer.Svg(dc,oldCache,UiInheritClass(16,16));
						frameTarget.PopDirtyClip(dc); const auto unknownOld = oldProbe->FinishDrawing(); const bool ended = SUCCEEDED(dc->EndDraw()); oldProbe->CompleteAttempt(ended);
						expect(ended && unknownOld.required == 1 && unknownOld.verified == 0 && unknownOld.firstUnverifiedReason != 0,
							"B339 actual drawing of unknown old bitmap stays unverified");
					}
					{
						auto capacityProbe = std::make_unique<Ui3SvgProbe>(0xB340);
						auto objects = std::make_unique<std::array<UiSVGClass, Ui3SvgCapacity + 1>>();
						SvgObservationScope owned(capacityProbe.get(),false,true);
						bool first256 = true;
						for (std::size_t i = 0; i < objects->size(); ++i)
						{
							auto& item = (*objects)[i]; item.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><rect width="1" height="1"/></svg>)SVG");
							if (i < Ui3SvgCapacity) first256 &= item.BindObservationTag(0x20000 + static_cast<std::uint32_t>(i));
						}
						expect(first256 && !objects->back().BindObservationTag(0x10000), "B340 exact 256 mappings reject legal distinct 257th object");
						expect((*objects)[0].BindObservationTag(0x20000) && !(*objects)[1].BindObservationTag(0x20000)
							&& !objects->back().BindObservationTag(0x100FF) && !objects->back().BindObservationTag(0xFFFFFFFFu),
							"B341 duplicate object is idempotent; conflicting/unknown/huge tags reject before indexing");
						expect(!Ui3SvgProbe::CapacityEvictionApplicable && capacityProbe->CountersAfterOwnerStopped().capacityEvict == 0,
							"B341 per-object bitmap replacement is not capacity eviction");
					}
					report << "[Ui3SvgProofSize] record=" << sizeof(Ui3FiniteTargetRecord) << " candidate=" << sizeof(Ui3FiniteCandidate)
						<< " producer=" << sizeof(Ui3SvgProbe) << " publication=" << sizeof(Ui3FinitePublication) << " observer=" << sizeof(Ui3FiniteObserver) << '\n';
					report << "[Ui3SvgProof] firstRequired=" << full.required << " firstVerified=" << full.verified
						<< " coverage=" << static_cast<unsigned>(actual.coverage) << " parse=" << counts.parseCalls
						<< " raster=" << counts.rasterCalls << " upload=" << counts.uploadCalls << " readyBytes=" << counts.logicalReadyBytes << '\n';
				}
			}
			frameTarget.Discard();
		}
		// 在独立 Scheduler 回调中测生产遮罩提交，TLS 诊断只属于该回调。
		{
			const auto output=std::filesystem::path(L"Build/eraser-b/exact-mask");
			std::filesystem::create_directories(output);
			std::ofstream samples(output/L"measurements.csv");
			samples<<"case,block,frames,mean_ms,slices,exact_hit,transform_fallback,parent_create\n";
			std::atomic_bool finished=false;
			RenderPipeline::Scheduler scheduler;
			const bool sinkReady=scheduler.SetDiagnosticsSink([](std::string_view){return true;});
			const bool started=sinkReady && scheduler.Start();
			const bool registered=started && scheduler.Register(RenderPipeline::Client::Bar,[&](const RenderPipeline::FrameContext&)
				{
					constexpr UINT width=512,height=256;
					UIRendering probe(MakeMainBarRenderingServices(&owner)); FrameTarget probeTarget{probe};
					UiShapeClass shape(0,0,210,90,16,16,2,std::nullopt,RGB(220,220,220));
					shape.enable.Initialization(true);shape.pct.SetDirect(1);
					shape.framePct.emplace(0);shape.frameLightPct.emplace(1);
					shape.frameRendering=UiFrameRenderingEnum::PointLight;
					UiFrameLightingSnapshot light{};
					light.primaryLight=D2D1::Point2F(200,125);
					light.primaryRadius=480;light.primaryLightVisible=true;
					light.edgeLightingEnabled=true;
					probe.SetFrameZoom(1);probe.SetFrameLightingSnapshot(light, BarUiEdgeLightingEnabled);
					auto* diagnostic=RenderPipeline::CurrentFrameDiagnostics();
					expect(diagnostic!=nullptr,"exact mask probe has production diagnostics");
					const std::array cases{
						std::pair{"identity",D2D1::Matrix3x2F::Identity()},
						std::pair{"integer",D2D1::Matrix3x2F::Translation(32.0F,24.0F)},
						std::pair{"fractional",D2D1::Matrix3x2F::Translation(32.5F,24.0F)}};
					for(const auto& [name,transform]:cases)
					{
						probeTarget.Discard();
						const HRESULT setup=probeTarget.Ensure(RenderPipeline::GetDeviceEpoch(),width,height);
						expect(SUCCEEDED(setup),"exact mask probe target setup");
						if(FAILED(setup))continue;
						auto* dc=probeTarget.DeviceContext();
						auto drawFrame=[&]()
							{
								dc->BeginDraw();dc->SetTransform(transform);
								dc->Clear(D2D1::ColorF(0.0F,0.0F,0.0F,0.0F));
								probeTarget.PushDirtyClip(dc,D2D1::RectF(0,0,static_cast<FLOAT>(width),static_cast<FLOAT>(height)));
								const bool drew=probe.Shape(dc,shape,UiInheritClass(100,80));
								probeTarget.PopDirtyClip(dc);
								const HRESULT hr=dc->EndDraw();probe.HandleFrameEndDrawResult(hr);
								return drew && SUCCEEDED(hr);
							};
						for(int warm=0;warm<16;++warm)
							expect(drawFrame(),"exact mask warmup frame draws");
						for(int block=0;block<11;++block)
						{
							const auto beforeSlices=diagnostic?diagnostic->light.slices:0;
							const auto beforeHits=diagnostic?diagnostic->light.exactHit:0;
							const auto beforeFallback=diagnostic?diagnostic->light.exactFallback[static_cast<size_t>(RenderPipeline::ExactFallback::Transform)]:0;
							const auto beforeCreate=diagnostic?diagnostic->light.roundedParentCreate:0;
							const auto began=std::chrono::steady_clock::now();
							for(int frame=0;frame<64;++frame)
								expect(drawFrame(),"exact mask measured frame draws");
							const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count()/64.0;
							const auto slices=diagnostic?diagnostic->light.slices-beforeSlices:0;
							const auto hits=diagnostic?diagnostic->light.exactHit-beforeHits:0;
							const auto fallbacks=diagnostic?diagnostic->light.exactFallback[static_cast<size_t>(RenderPipeline::ExactFallback::Transform)]-beforeFallback:0;
							expect(slices==64*(name==std::string_view("identity")?1:9),"exact mask production FillOpacityMask count");
							expect(hits==(name==std::string_view("identity")?64:0) && fallbacks==(name==std::string_view("identity")?0:64),"exact mask transform eligibility");
							samples<<name<<','<<block<<",64,"<<elapsed<<','
								<<slices<<','<<hits<<','<<fallbacks<<','
								<<(diagnostic?diagnostic->light.roundedParentCreate-beforeCreate:0)<<'\n';
						}
						ComPtr<ID2D1Bitmap1> readable;
						const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,probeTarget.TargetBitmap()->GetPixelFormat());
						HRESULT hr=dc->CreateBitmap(D2D1::SizeU(width,height),nullptr,0,&properties,&readable);
						if(SUCCEEDED(hr))hr=readable->CopyFromBitmap(nullptr,probeTarget.TargetBitmap(),nullptr);
						D2D1_MAPPED_RECT mapped{};if(SUCCEEDED(hr))hr=readable->Map(D2D1_MAP_OPTIONS_READ,&mapped);
						expect(SUCCEEDED(hr),"exact mask BGRA readback");
						if(SUCCEEDED(hr))
						{
							std::ofstream pixels(output/(std::string(name)+".bgra"),std::ios::binary);
							for(UINT y=0;y<height;++y)pixels.write(reinterpret_cast<const char*>(mapped.bits+y*mapped.pitch),width*4);
							expect(pixels.good(),"exact mask BGRA saved");readable->Unmap();
						}
					}
					// 真实生产绘制验证共享光源快照，不能只检查 setter 回读值。
					if(probeTarget.DeviceContext())
					{
						auto* dc=probeTarget.DeviceContext();
						const auto alphaPixels=[&](const UiFrameLightingSnapshot& snapshot)
						{
							probe.SetFrameLightingSnapshot(snapshot, BarUiEdgeLightingEnabled);
							dc->SetTransform(D2D1::IdentityMatrix());
							probeTarget.BeginFrame(dc,D2D1::RectF(0,0,width,height));
							(void)probe.Shape(dc,shape,UiInheritClass(100,80));
							probeTarget.FinishDrawCommands(dc);
							HDC source=nullptr;
							HRESULT hr=probeTarget.GdiInteropRenderTarget()->GetDC(D2D1_DC_INITIALIZE_MODE_COPY,&source);
							if(SUCCEEDED(hr)) {const RECT unchanged{};hr=probeTarget.GdiInteropRenderTarget()->ReleaseDC(&unchanged);}
							const HRESULT end=probeTarget.EndFrame(dc);
							if(SUCCEEDED(hr))hr=end;
							ComPtr<ID2D1Bitmap1> readback;
							const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
								probeTarget.TargetBitmap()->GetPixelFormat());
							if(SUCCEEDED(hr))hr=dc->CreateBitmap(D2D1::SizeU(width,height),nullptr,0,&properties,&readback);
							if(SUCCEEDED(hr))hr=readback->CopyFromBitmap(nullptr,probeTarget.TargetBitmap(),nullptr);
							D2D1_MAPPED_RECT mapped{};if(SUCCEEDED(hr))hr=readback->Map(D2D1_MAP_OPTIONS_READ,&mapped);
							expect(SUCCEEDED(hr),"shared lighting snapshot transaction and alpha readback");
							std::size_t pixels=0;
							if(SUCCEEDED(hr))
							{
								for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)
									if(mapped.bits[y*mapped.pitch+x*4+3])++pixels;
								readback->Unmap();
							}
							return pixels;
						};
						UiFrameLightingSnapshot hidden{};hidden.primaryLight={200,125};hidden.primaryRadius=480;
						hidden.primaryLightVisible=false;hidden.edgeLightingEnabled=true;
						expect(alphaPixels(hidden)==0,"hidden primary light does not draw merely because its radius is retained");
						auto cursor=hidden;cursor.primaryRadius=0;cursor.cursorLight={200,125};
						cursor.cursorRadius=240;cursor.cursorIntensity=1;cursor.cursorLightVisible=true;
						expect(alphaPixels(cursor)>0,"cursor-only lighting renders without a primary radius");
						auto disabled=cursor;disabled.edgeLightingEnabled=false;
						expect(alphaPixels(disabled)==0,"snapshot edge-light disable suppresses all light passes");
						shape.framePct->SetDirect(0.30);
						expect(alphaPixels(hidden)>0,"base frame remains visible when both lights are hidden");
					}
					// 真正的全帧与保留像素局部重绘比较：确保跳过发生，且不是同一裁剪路径自证。
					{
						constexpr UINT targetWidth=1024,targetHeight=512;
						const auto buttonMetrics=ResolveButtonVisualMetrics(ButtonVisualLayoutKind::StandardTwoTwo);
						const auto initializeButton=[&](BarButtonClass& button,const wchar_t* label)
						{
							button.hide=false;
							button.button.Initialization(0,0,70,70,4,4,1,RGB(45,85,110),RGB(220,230,240));
							button.button.enable.Initialization(true);button.button.pct.SetDirect(0.65);
							button.button.framePct.emplace(0.30);button.button.frameLightPct.emplace(1.0);
							button.button.frameRendering=UiFrameRenderingEnum::PointLight;
							button.icon.Initialization(0,buttonMetrics.iconOffsetYDip,RGB(255,255,255),std::nullopt);
							button.icon.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="28" height="28"><path fill="#fff" d="M4 4H24V24H4Z"/></svg>)SVG");
							button.icon.enable.Initialization(true);button.icon.pct.SetDirect(1);
							button.icon.SetWH(28,28);button.icon.w.SetDirect(button.icon.w.tar);button.icon.h.SetDirect(button.icon.h.tar);
							button.name.Initialization(0,buttonMetrics.primaryOffsetYDip,buttonMetrics.primarySlotWidthDip,
								buttonMetrics.primarySlotHeightDip,label,buttonMetrics.primaryFontSizeDip,RGB(255,255,255));
							button.name.enable.Initialization(true);button.name.pct.SetDirect(1);
						};
						for(const double zoom:{1.0,1.5,2.0})for(const UINT dpi:{96u,144u,192u})
						{
							UIRendering full(MakeMainBarRenderingServices(&owner)),retained(MakeMainBarRenderingServices(&owner));FrameTarget fullTarget{full},retainedTarget{retained};
							const auto epoch=RenderPipeline::GetDeviceEpoch();
							const bool ready=SUCCEEDED(fullTarget.Ensure(epoch,targetWidth,targetHeight))
								&&SUCCEEDED(retainedTarget.Ensure(epoch,targetWidth,targetHeight));
							expect(ready,"shared dirty cull production target setup");if(!ready)continue;
							std::array<BarButtonClass,2> referenceButtons,partialButtons;
							for(auto* buttons:{&referenceButtons,&partialButtons}){
								initializeButton((*buttons)[0],L"Prev");initializeButton((*buttons)[1],L"Next");
								for(auto& button:*buttons){button.pressScale.SetDirect(dpi==96?1.0:dpi==144?0.94:1.07);
									button.icon.angle.SetDirect(17);button.icon.contentScale=1.08;button.name.contentScale=1.05;}
							}
							UiWordClass secondary;secondary.Initialization(0,31,50,13,L"/12",11,RGB(235,235,235));
							secondary.enable.Initialization(true);secondary.pct.SetDirect(1);
							UiShapeClass sidePanel(0,0,76,76,7,7,1,RGB(25,30,36),RGB(200,210,220));
							sidePanel.enable.Initialization(true);sidePanel.pct.SetDirect(0.85);sidePanel.frameRendering=UiFrameRenderingEnum::PointLight;
							sidePanel.framePct.emplace(0.25);sidePanel.frameLightPct.emplace(1);
							const auto popup=dpi==96?D2D1::Matrix3x2F::Identity():dpi==144
								?D2D1::Matrix3x2F::Translation(24.5F,12.25F)
								:D2D1::Matrix3x2F::Scale(1.03F,0.91F)*D2D1::Matrix3x2F::Rotation(4,D2D1::Point2F(100,100))
									*D2D1::Matrix3x2F::Translation(24.5F,12.25F);
							const auto fullClip=D2D1::RectF(0,0,targetWidth*96.0F/dpi,targetHeight*96.0F/dpi);
							// 独立推导一个包含左按钮和光晕的宽松 dirty，右侧按钮仍明确在外。
							D2D1_RECT_F dirty{100000,100000,-100000,-100000};
							for(const double x:{-15.0*zoom,105.0*zoom})for(const double y:{-15.0*zoom,105.0*zoom}){
								const FLOAT px=static_cast<FLOAT>(x*popup._11+y*popup._21+popup._31);
								const FLOAT py=static_cast<FLOAT>(x*popup._12+y*popup._22+popup._32);
								dirty.left=(std::min)(dirty.left,px);dirty.top=(std::min)(dirty.top,py);
								dirty.right=(std::max)(dirty.right,px);dirty.bottom=(std::max)(dirty.bottom,py);
							}
							const auto draw=[&](UIRendering& renderer,FrameTarget& frameTarget,std::array<BarButtonClass,2>& buttons,bool changed,const D2D1_RECT_F& clip)
							{
								auto* dc=frameTarget.DeviceContext();dc->SetDpi(static_cast<FLOAT>(dpi),static_cast<FLOAT>(dpi));
								dc->SetUnitMode(D2D1_UNIT_MODE_DIPS);dc->SetTransform(D2D1::IdentityMatrix());
								renderer.SetFrameZoom(zoom);renderer.SetFrameDiffuseReferenceZoom(zoom*1.1);
								renderer.SetFrameDiffuseMaskGeometryScale(1.06);
								UiFrameLightingSnapshot lighting{};lighting.primaryLight={180,100};lighting.primaryRadius=480;
								lighting.primaryLightVisible=true;lighting.edgeLightingEnabled=true;renderer.SetFrameLightingSnapshot(lighting, BarUiEdgeLightingEnabled);
								buttons[0].button.fill->SetDirect(changed?RGB(210,90,25):RGB(45,85,110));
								buttons[0].name.SetStringImmediate(changed?L"Changed":L"Prev");
								frameTarget.BeginFrame(dc,clip);dc->SetTransform(popup);
								renderer.PushPanelOpacity(dc,D2D1::RectF(0,0,230*static_cast<FLOAT>(zoom),90*static_cast<FLOAT>(zoom)),0.82);
								(void)DrawBackgroundVisual(renderer,dc,sidePanel,UiInheritClass(147,7));
								ButtonDrawOptions options;options.secondary=&secondary;
								(void)DrawButtonVisual(renderer,dc,buttons[0],UiInheritClass(10,10),options);
								(void)DrawButtonVisual(renderer,dc,buttons[1],UiInheritClass(150,10),options);
								dc->PopLayer();dc->SetTransform(D2D1::IdentityMatrix());frameTarget.FinishDrawCommands(dc);
								HDC source=nullptr;HRESULT hr=frameTarget.GdiInteropRenderTarget()->GetDC(D2D1_DC_INITIALIZE_MODE_COPY,&source);
								if(SUCCEEDED(hr)){const RECT unchanged{};hr=frameTarget.GdiInteropRenderTarget()->ReleaseDC(&unchanged);}
								const HRESULT end=frameTarget.EndFrame(dc);return SUCCEEDED(hr)&&SUCCEEDED(end);
							};
							expect(draw(full,fullTarget,referenceButtons,false,fullClip)&&draw(retained,retainedTarget,partialButtons,false,fullClip),"shared dirty cull seed full frames");
							expect(draw(full,fullTarget,referenceButtons,true,fullClip),"shared dirty cull independent full reference");
							const auto expected=ReadSvgProofPixels(fullTarget.DeviceContext(),fullTarget.TargetBitmap());
							expect(draw(retained,retainedTarget,partialButtons,true,dirty),"shared dirty cull retained partial transaction");
							const auto actual=ReadSvgProofPixels(retainedTarget.DeviceContext(),retainedTarget.TargetBitmap());
							expect(!expected.empty()&&actual==expected,"shared dirty cull retains exact BGRA with DPI/zoom/press/popup/light/layer and secondary text");
							expect(retained.GetFrameCulledDrawCount()>=2,"shared dirty cull really rejects standard button and independent panel draws");
							expect(partialButtons[1].button.inhX==150&&partialButtons[1].button.inhY==10,"culled standard button still publishes current inherited hit geometry");
							report<<"[Ui3DirtyCull] zoom="<<zoom<<" dpi="<<dpi<<" rejected="<<retained.GetFrameCulledDrawCount()<<" exactPixels="<<(actual==expected)<<'\n';
							fullTarget.Discard();retainedTarget.Discard();
						}
						// 未知自定义内容可能超出按钮，必须实际执行；普通 glyph/shape 仍可各自裁剪。
						UIRendering custom(MakeMainBarRenderingServices(&owner));FrameTarget customTarget{custom};const bool customReady=SUCCEEDED(customTarget.Ensure(RenderPipeline::GetDeviceEpoch(),256,128));
						expect(customReady,"shared dirty cull custom path target");
						if(customReady){
							auto* dc=customTarget.DeviceContext();custom.SetFrameZoom(1);dc->SetTransform(D2D1::IdentityMatrix());
							BarButtonClass button;initializeButton(button,L"Outside");unsigned calls=0;
							ButtonDrawOptions options;options.content=[&](UIRendering& renderer,ID2D1DeviceContext* context,ButtonClass&,const ButtonVisualInheritance&){
								++calls;auto* brush=renderer.ContentSolidBrush(RGB(255,0,0));if(!brush)return false;
								context->FillRectangle(D2D1::RectF(5,5,15,15),brush);return true;
							};
							customTarget.BeginFrame(dc,D2D1::RectF(0,0,40,40));
							(void)DrawButtonVisual(custom,dc,button,UiInheritClass(150,10),options);
							customTarget.FinishDrawCommands(dc);expect(SUCCEEDED(customTarget.EndFrame(dc)),"shared dirty cull custom transaction");
							const auto pixel=ReadEraserTestPixel(dc,customTarget.TargetBitmap(),10,10);
							expect(calls==1&&pixel[2]>0&&pixel[3]>0,"unbounded custom callback is not rejected with button chrome");
							expect(custom.ShouldDrawFrameBounds(dc,D2D1::RectF(1000,1000,1010,1010)),"no active dirty clip always preserves original draw path");
							customTarget.BeginFrame(dc,D2D1::RectF(0,0,40,40));
							const auto invalid=D2D1::RectF((std::numeric_limits<FLOAT>::quiet_NaN)(),0,10,10);
							expect(custom.ShouldDrawFrameBounds(dc,invalid),"invalid bounds conservatively bypass rejection");
							{auto finiteProbe=std::make_unique<Ui3SvgProbe>(0xB380);SvgObservationScope scope(finiteProbe.get());
								expect(custom.ShouldDrawFrameBounds(dc,D2D1::RectF(1000,1000,1010,1010)),"finite SVG resource observation keeps original draw calls");}
							customTarget.FinishDrawCommands(dc);expect(SUCCEEDED(customTarget.EndFrame(dc)),"shared dirty cull fallback transaction");
						}
						customTarget.Discard();
					}
					probeTarget.Discard();finished.store(true,std::memory_order_release);
					return RenderPipeline::FrameResult::Idle;
				});
			expect(registered,"exact mask diagnostic scheduler starts");
			if(registered)
			{
				const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
				while(!finished.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<deadline)
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				expect(finished.load(std::memory_order_acquire),"exact mask diagnostic scheduler finishes");
				scheduler.Unregister(RenderPipeline::Client::Bar);
			}
			if(started)scheduler.Stop();
		}
		// 生产分页按钮的图标缓存必须随独立 WARP device epoch 重建。
		{
			const auto epochA=RenderPipeline::GetDeviceEpoch();
			RenderPipeline::DeviceEpoch epochB;
			epochB.backend=RenderPipeline::Backend::Warp;
			epochB.generation=epochA.generation+1;
			const D3D_FEATURE_LEVEL featureLevels[]{D3D_FEATURE_LEVEL_11_0};
			HRESULT warpHr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT,featureLevels,1,D3D11_SDK_VERSION,
				epochB.d3dDevice.GetAddressOf(),&epochB.featureLevel,
				epochB.immediateContext.GetAddressOf());
			if(SUCCEEDED(warpHr))warpHr=epochB.d3dDevice.As(&epochB.dxgiDevice);
			const auto factory=RenderPipeline::D2DFactory();
			if(SUCCEEDED(warpHr))warpHr=factory
				? factory->CreateDevice(epochB.dxgiDevice.Get(),epochB.d2dDevice.GetAddressOf())
				: E_POINTER;
			expect(SUCCEEDED(warpHr),"independent WARP/D2D epoch initializes");

			BarPageSurface scene;
			// 测试自持帧目标（声明在场景之后，先于场景析构）；Ensure 前先让场景清自有设备资源。
			FrameTarget sceneTarget;
			scene.BindFrameTarget(&sceneTarget);
			const auto ensureScene=[&](const RenderPipeline::DeviceEpoch& epoch,UINT targetWidth,UINT targetHeight)
			{
				scene.PrepareDeviceGeneration(epoch.generation);
				return sceneTarget.Ensure(epoch,targetWidth,targetHeight);
			};
			BarPageSurfaceContent content;
			content.whiteboard=true;
			content.currentPage=L"1";
			content.totalPage=L"/3";
			const bool configured=scene.ConfigurePagination(content)
				&& scene.SetBounds({0,0,305,80},1.0F);
			(void)scene.Advance(std::chrono::steady_clock::now());
			expect(configured,"production PageSurface icon configures");
			const bool savedFractional=Inkeys::config.Experimental.Inkeys3.UI3.AllowFractionalFontSize.load();
			const auto policyTime=std::chrono::steady_clock::now();
			Inkeys::config.Experimental.Inkeys3.UI3.AllowFractionalFontSize=false;
			(void)scene.Advance(policyTime);(void)scene.ConsumeDamage();
			expect(!scene.IsInvalidated(),"font policy fixture starts with consumed damage");
			Inkeys::config.Experimental.Inkeys3.UI3.AllowFractionalFontSize=true;
			(void)scene.Advance(policyTime);
			expect(scene.IsInvalidated(),"same-time idle PageSurface invalidates on font policy change");
			scene.SetOpacity(0,0);
			(void)scene.Advance(policyTime+std::chrono::milliseconds(1));(void)scene.ConsumeDamage();
			expect(!scene.IsInvalidated(),"hidden font policy fixture starts without geometry damage");
			Inkeys::config.Experimental.Inkeys3.UI3.AllowFractionalFontSize=false;
			(void)scene.Advance(policyTime+std::chrono::milliseconds(1));
			expect(scene.IsInvalidated(),"hidden PageSurface applies font policy before restoring visibility");
			scene.SetOpacity(1,0);
			Inkeys::config.Experimental.Inkeys3.UI3.AllowFractionalFontSize=savedFractional;

			const RECT presentation=scene.PresentationBounds();
			const UINT width=static_cast<UINT>(presentation.right-presentation.left);
			const UINT height=static_cast<UINT>(presentation.bottom-presentation.top);
			auto frameTime=std::chrono::steady_clock::now();
			auto drawPageSurface=[&](const RenderPipeline::DeviceEpoch& epoch,UINT targetWidth,
				UINT targetHeight,std::vector<unsigned char>& pixels)
			{
				if(FAILED(ensureScene(epoch,targetWidth,targetHeight)))return false;
				auto* context=sceneTarget.DeviceContext();
				if(!context)return false;
				sceneTarget.BeginFrame(context,D2D1::RectF(0,0,
					static_cast<FLOAT>(targetWidth),static_cast<FLOAT>(targetHeight)));
				const bool rendered=scene.Render(context,frameTime).rendered;
				sceneTarget.FinishDrawCommands(context);
				const HRESULT endDraw=sceneTarget.EndFrame(context);
				if(!rendered || FAILED(endDraw))return false;
				ComPtr<ID2D1Image> image;
				context->GetTarget(&image);
				ComPtr<ID2D1Bitmap1> target;
				if(!image || FAILED(image.As(&target)))return false;
				ComPtr<ID2D1Bitmap1> readable;
				const auto props=D2D1::BitmapProperties1(
					D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
					target->GetPixelFormat());
				if(FAILED(context->CreateBitmap(target->GetPixelSize(),nullptr,0,
					&props,&readable)) || FAILED(readable->CopyFromBitmap(nullptr,target.Get(),nullptr)))
					return false;
				D2D1_MAPPED_RECT mapped{};
				if(FAILED(readable->Map(D2D1_MAP_OPTIONS_READ,&mapped)))return false;
				pixels.resize(static_cast<size_t>(targetWidth)*targetHeight*4);
				for(UINT y=0;y<targetHeight;++y)
					std::memcpy(pixels.data()+static_cast<size_t>(y)*targetWidth*4,
						mapped.bits+y*mapped.pitch,static_cast<size_t>(targetWidth)*4);
				readable->Unmap();
				return true;
			};
			if(SUCCEEDED(warpHr) && configured)
			{
				std::vector<unsigned char> baseline,afterRelease,preserved,
					afterImplicitRecreate,afterResize;
				const bool drewBaseline=drawPageSurface(epochA,width,height,baseline);
				expect(drewBaseline,"PageSurface icon renders in first epoch");
				if(drewBaseline)
				{
					bool visible=false;
					for(size_t pixel=3;pixel<baseline.size();pixel+=4)
						visible|=baseline[pixel]!=0;
					expect(visible,"PageSurface SVG contributes visible pixels");
					// 主栏主题切换必须直接作用于分页的真实背景，不能另行固定为深色。
					const bool originalTheme=owner.barStyle.darkStyle;
					for(const bool dark:{false,true})
					{
						owner.barStyle.darkStyle=dark;
						for(int frame=0;frame<40;++frame){frameTime+=std::chrono::milliseconds(20);(void)scene.Advance(frameTime);}
						std::vector<unsigned char> themed;
						const bool rendered=drawPageSurface(epochA,width,height,themed);
						expect(rendered,"PageSurface redraws after the shared main theme changes");
						const UINT sampleX=static_cast<UINT>(77-presentation.left),sampleY=static_cast<UINT>(40-presentation.top);
						if(rendered&&sampleX<width&&sampleY<height)
						{
							const auto at=(static_cast<size_t>(sampleY)*width+sampleX)*4;
							const auto expected=GetThemeColor(ThemeColorEnum::Surface);
							// 分页常驻底板与主栏同一常驻透明度：Dark 0.80、Light 端更实。
							const double residentOpacity=GetThemeResidentSurfaceOpacity(BarMainBarFillOpacity);
							const auto premultiplied=[&](int channel){return static_cast<int>(std::lround(channel*residentOpacity));};
							expect(std::abs(int(themed[at+3])-premultiplied(255))<=1
								&&std::abs(int(themed[at])-premultiplied(GetBValue(expected)))<=2
								&&std::abs(int(themed[at+1])-premultiplied(GetGValue(expected)))<=2
								&&std::abs(int(themed[at+2])-premultiplied(GetRValue(expected)))<=2,
								"PageSurface dark/light pixels follow the same UI3 surface color as the main bar");
						}
					}
					{
						// 用户常驻背景透明度：深浅两端都必须真实改变分页底板与主栏常驻表面的像素透明度。
						const int savedBackground=owner.barStyle.backgroundOpacity;
						auto readAlpha=[&](const std::vector<unsigned char>& pixels,UINT x,UINT y,UINT stride)
							{return int(pixels[(static_cast<size_t>(y)*stride+x)*4+3]);};
						for(const bool dark:{true,false})for(const int userOpacity:{0,50,100})
						{
							owner.barStyle.darkStyle=dark;owner.barStyle.backgroundOpacity=userOpacity;
							for(int frame=0;frame<40;++frame){frameTime+=std::chrono::milliseconds(20);(void)scene.Advance(frameTime);}
							const double expectedOpacity=userOpacity>0?userOpacity/100.0
								:GetThemeDefaultResidentBackgroundOpacity(dark);
							std::vector<unsigned char> themed;
							if(drawPageSurface(epochA,width,height,themed))
							{
								const UINT sampleX=static_cast<UINT>(77-presentation.left),sampleY=static_cast<UINT>(40-presentation.top);
								if(sampleX<width&&sampleY<height)
									expect(std::abs(readAlpha(themed,sampleX,sampleY,width)-int(std::lround(255*expectedOpacity)))<=1,
										"PageSurface background follows the user resident opacity in Dark and Light");
							}
							// 主栏常驻表面：与生产初始化一致的 themeSurface + residentSurface 形状。
							UiShapeClass mainBar(0.0,0.0,60.0,40.0,8.0,8.0,1.0,
								GetThemeColor(ThemeModeEnum::Dark,ThemeColorEnum::Surface),
								GetThemeColor(ThemeModeEnum::Dark,ThemeColorEnum::SurfaceFrame));
							mainBar.enable.Initialization(true);mainBar.pct.SetDirect(BarMainBarFillOpacity);
							mainBar.framePct.emplace(0.0);mainBar.themeSurface=true;mainBar.residentSurface=true;
							mainBar.lightFillOpacityScale=ThemeMaterial::LightResidentSurfaceOpacityScale;
							mainBar.lightMaterial=dark?0.0:1.0;
							bool drewMainBar=false;std::vector<unsigned char> barPixels;
							if(SUCCEEDED(ensureScene(epochA,width,height)))
								if(auto* context=sceneTarget.DeviceContext())
								{
									const D2D1_RECT_F all=D2D1::RectF(0,0,static_cast<FLOAT>(width),static_cast<FLOAT>(height));
									sceneTarget.BeginFrame(context,all);
									context->Clear(D2D1::ColorF(0,0,0,0));
									(void)sceneTarget.BoundRenderer()->Shape(context,mainBar,UiInheritClass(10.0,10.0));
									sceneTarget.FinishDrawCommands(context);
									if(SUCCEEDED(sceneTarget.EndFrame(context)))
									{
										ComPtr<ID2D1Image> image;context->GetTarget(&image);ComPtr<ID2D1Bitmap1> target;
										ComPtr<ID2D1Bitmap1> readable;
										const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
											D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
										D2D1_MAPPED_RECT mapped{};
										if(image&&SUCCEEDED(image.As(&target))
											&&SUCCEEDED(context->CreateBitmap(target->GetPixelSize(),nullptr,0,&props,&readable))
											&&SUCCEEDED(readable->CopyFromBitmap(nullptr,target.Get(),nullptr))
											&&SUCCEEDED(readable->Map(D2D1_MAP_OPTIONS_READ,&mapped)))
										{
											barPixels.assign(mapped.bits+30*mapped.pitch+40*4,mapped.bits+30*mapped.pitch+40*4+4);
											readable->Unmap();drewMainBar=true;
										}
									}
								}
							expect(drewMainBar,"main bar resident surface renders offscreen");
							if(drewMainBar)
								expect(std::abs(int(barPixels[3])-int(std::lround(255*expectedOpacity)))<=1,
									"main bar resident surface follows the user background opacity in Dark and Light");
						}
						owner.barStyle.backgroundOpacity=savedBackground;
					}
					owner.barStyle.darkStyle=originalTheme;
					for(int frame=0;frame<40;++frame){frameTime+=std::chrono::milliseconds(20);(void)scene.Advance(frameTime);}
					scene.ReleaseDeviceResources();
					const bool drewAfterRelease=drawPageSurface(epochB,width,height,afterRelease);
					expect(drewAfterRelease,"PageSurface icon renders after explicit release on new epoch");
					if(drewAfterRelease)
					{
						expect(afterRelease==baseline,"new epoch retains exact BGRA icon pixels");
						expect(ensureScene(epochA,0,height)==E_INVALIDARG,
						"invalid target does not replace working epoch");
						expect(drawPageSurface(epochB,width,height,preserved)
						&& preserved==afterRelease,"failed setup preserves current PageSurface pixels");
						expect(drawPageSurface(epochA,width,height,afterImplicitRecreate)
						&& afterImplicitRecreate==baseline,
						"implicit epoch recreation retains exact BGRA icon pixels");
						expect(drawPageSurface(epochA,width+8,height+8,afterResize),
						"target resize recreates PageSurface resources");
					if(afterResize.size()==static_cast<size_t>(width+8)*(height+8)*4)
					{
						bool samePixels=true;
						for(UINT y=0;y<height && samePixels;++y)
							samePixels=std::memcmp(afterResize.data()+static_cast<size_t>(y)*(width+8)*4,
								baseline.data()+static_cast<size_t>(y)*width*4,
								static_cast<size_t>(width)*4)==0;
						expect(samePixels,"target resize retains exact visible BGRA pixels");
					}
				}
				}
			}
			scene.ReleaseDeviceResources();
		}
		failures += Inkeys::UI::PageControl::RunOffscreenTests();
		// 主按钮展开/反向与点击脉冲：分开量CPU路径和完整离屏绘制，像素读取不计入计时。
		{
			struct PathProbe : UIRendering
			{
				using UIRendering::UIRendering;
				using UIRendering::GetSuperellipseGeometry;
			};
			const auto output=std::filesystem::path(L"Build/eraser-b/ui3-path");
			std::filesystem::create_directories(output);
			std::ofstream samples(output/L"measurements.csv");
			samples<<"zoom,block,geometry_frames,geometry_mean_ms,draw_frames,draw_mean_ms,successful_draws\n";
			std::array<std::array<double,2>,48> frames{};
			UiValueClass exponent(3.0),size(BarMainButtonWidthDip);
			const UiCurveSpecClass pulse{UiCurveEnum::EaseOutBack,UiCurveEnum::EaseInBack,0.0,false};
			for(size_t frame=0;frame<frames.size();++frame)
			{
				if(frame==0 || frame==12 || frame==24)
				{
					exponent.SetTar(frame==12?3.0:10.0,0.4);
					size.SetTar(BarMainButtonWidthDip,0.4,BarMainButtonWidthDip*1.1,true,pulse);
				}
				const UiAnimationAdvanceContextClass advance{1.0/60.0,1.0,true,false};
				UiAdvanceAnimation(exponent,advance);UiAdvanceAnimation(size,advance);
				frames[frame]={static_cast<double>(size.val),static_cast<double>(exponent.val)};
			}
			for(const double zoom:{1.0,1.5})
			{
				constexpr UINT width=256,height=192;
				PathProbe renderer(MakeMainBarRenderingServices(&owner)); FrameTarget frameTarget{renderer};
				const HRESULT setup=frameTarget.Ensure(RenderPipeline::GetDeviceEpoch(),width,height);
				expect(SUCCEEDED(setup),"UI3 path benchmark target setup");
				if(FAILED(setup))continue;
				renderer.SetFrameZoom(zoom);
				UiFrameLightingSnapshot light{};
				light.primaryLight=D2D1::Point2F(105,85);light.primaryRadius=480;
				light.primaryLightVisible=true;light.edgeLightingEnabled=true;
				renderer.SetFrameLightingSnapshot(light, BarUiEdgeLightingEnabled);
				UiSuperellipseClass shape(0,0,BarMainButtonWidthDip,BarMainButtonHeightDip,
					3.0,ButtonFrameThicknessDip,RGB(245,245,245),RGB(200,200,200));
				shape.enable.Initialization(true);shape.framePct.emplace(0.4);
				shape.frameRendering=UiFrameRenderingEnum::PointLight;
				auto* dc=frameTarget.DeviceContext();
				auto drawFrame=[&](const std::array<double,2>& values)
				{
					shape.w.SetDirect(values[0]);shape.h.SetDirect(values[0]);shape.n->SetDirect(values[1]);
					dc->BeginDraw();dc->SetTransform(D2D1::IdentityMatrix());
					dc->Clear(D2D1::ColorF(0,0,0,0));
					frameTarget.PushDirtyClip(dc,D2D1::RectF(0,0,width,height));
					const bool drew=renderer.Superellipse(dc,shape,UiInheritClass(40,32));
					frameTarget.PopDirtyClip(dc);
					const HRESULT hr=dc->EndDraw();renderer.HandleFrameEndDrawResult(hr);
					return drew && SUCCEEDED(hr);
				};
				for(const auto& frame:frames)expect(drawFrame(frame),"UI3 path benchmark warm draw");
				for(int block=0;block<7;++block)
				{
					bool geometryOk=true;
					const auto geometryStart=std::chrono::steady_clock::now();
					for(int repeat=0;repeat<32;++repeat)for(const auto& frame:frames)
					{
						const auto extent=static_cast<FLOAT>(frame[0]*zoom);
						const int segments=std::clamp(static_cast<int>((frame[0]*zoom*2)/8.0),24,128);
						geometryOk &= renderer.GetSuperellipseGeometry(40*static_cast<FLOAT>(zoom),32*static_cast<FLOAT>(zoom),
							extent,extent,static_cast<FLOAT>(frame[1]),segments)!=nullptr;
					}
					const double geometryMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-geometryStart).count()/(32*frames.size());
					unsigned successful=0;
					const auto drawStart=std::chrono::steady_clock::now();
					for(int repeat=0;repeat<4;++repeat)for(const auto& frame:frames)successful+=drawFrame(frame)?1:0;
					const double drawMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-drawStart).count()/(4*frames.size());
					expect(geometryOk && successful==4*frames.size(),"UI3 path benchmark all geometry/draws succeed");
					samples<<zoom<<','<<block<<','<<32*frames.size()<<','<<geometryMs<<','<<4*frames.size()<<','<<drawMs<<','<<successful<<'\n';
				}
				std::ofstream pixels(output/(zoom==1.0?L"zoom-1.bgra":L"zoom-1.5.bgra"),std::ios::binary);
				for(const auto& frame:frames)
				{
					expect(drawFrame(frame),"UI3 path snapshot draws");
					const auto data=ReadSvgProofPixels(dc,frameTarget.TargetBitmap());
					expect(data.size()==width*height*4,"UI3 path full BGRA readback");
					pixels.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
				}
				expect(samples.good() && pixels.good(),"UI3 path benchmark output saved");
				frameTarget.Discard();
			}
		}
		owner.specTarget.Discard();
		TestBarLayoutRestart(expect);
		owner.specTarget.Discard();RenderPipeline::Shutdown();CoUninitialize();
		report<<"[EraserVisual] failures="<<failures<<'\n';return failures?1:0;
	}
}
