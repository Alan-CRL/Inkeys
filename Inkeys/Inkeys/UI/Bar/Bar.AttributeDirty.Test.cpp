#include "../../../IdtMain.h"
#include "../../../IdtState.h"
#include "../../../resource.h"
#include "../../Drawing/Draw3/Draw3.ShapeInteraction.h"

#include <windows.h>
#include <crtdbg.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// UI3 自动脏区离屏验证（--ui3-attribute-dirty-test）。
// 在完整主栏场景里逐项改变视觉状态，每帧核对「实际变化像素 ⊆ 影子登记表脏区」，并记录影子与生产脏区面积。
// 夹具每帧强制整幅提交，生产读回即为正确整幅画面，相邻两帧之差就是真实变化。
// V3（影子模式）只诊断：报告越界帧与未覆盖区域，基础设施失败才返回非 0；
// 面板与自绘对象接入后（V4 起）把对应场景改为硬断言。

module Inkeys.UI.Bar;
import :Main;
import :Theme;
import Inkeys.UI.RenderPipeline;
import Inkeys.Text.Font;

using namespace Inkeys::UI::UI3;
using namespace Inkeys::UI::Bar;
namespace Pipeline = Inkeys::UI::RenderPipeline;

namespace
{
	struct Capture
	{
		SIZE size{};
		std::vector<BYTE> bytes;
		unsigned serial = 0;
	};

	struct StepStats
	{
		std::string name;
		unsigned frames = 0, compared = 0, violations = 0, skippedMapping = 0, purity = 0;
		// 只用生产脏区（登记表 ∪ 光影）判定的越界：不含影子登记表，检验主栏迁移后的覆盖完整性。
		unsigned productionViolations = 0;
		unsigned changedFrames = 0, purityLogged = 0;
		// 未分类整窗帧数（原因位 1）、各帧原因位并集，以及只有已登记字号修正动画在进行的帧数。
		unsigned fullFrames = 0, fullReasons = 0, fontRegistryFrames = 0;
		double shadowArea = 0, presentArea = 0;
		RECT worstUncovered{};
	};

	LONG Area(const RECT& rect) noexcept
	{
		return rect.right > rect.left && rect.bottom > rect.top ? (rect.right - rect.left) * (rect.bottom - rect.top) : 0;
	}

	// 两帧之间变化像素的外接矩形（客户区坐标）；无变化时为空。
	RECT DifferenceBounds(const Capture& before, const Capture& after)
	{
		RECT bounds{ LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
		bool any = false;
		const LONG width = after.size.cx, height = after.size.cy;
		for (LONG y = 0; y < height; ++y)
		{
			const BYTE* a = before.bytes.data() + static_cast<size_t>(y) * width * 4;
			const BYTE* b = after.bytes.data() + static_cast<size_t>(y) * width * 4;
			if (std::memcmp(a, b, static_cast<size_t>(width) * 4) == 0) continue;
			for (LONG x = 0; x < width; ++x)
				if (std::memcmp(a + x * 4, b + x * 4, 4) != 0)
				{
					any = true;
					bounds.left = (std::min)(bounds.left, x); bounds.right = (std::max)(bounds.right, x + 1);
					bounds.top = (std::min)(bounds.top, y); bounds.bottom = (std::max)(bounds.bottom, y + 1);
				}
		}
		return any ? bounds : RECT{};
	}

	bool Contains(const RECT& outer, const RECT& inner) noexcept
	{
		if (Area(inner) == 0) return true;
		return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right && inner.bottom <= outer.bottom;
	}

	std::string Text(const RECT& rect)
	{
		std::ostringstream text;
		text << '[' << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom << ']';
		return text.str();
	}
}

namespace Inkeys::UI::Bar
{
	int RunUi3AttributeDirtyTest()
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		struct RuntimeScope
		{
			std::wstring originalPath = globalPath;
			bool com = false, pipeline = false;
			~RuntimeScope()
			{
				SetBarVisualShadowTestHook({});
				SetThemeStyleSource(nullptr);
				Inkeys::Drawing::Draw3::PublishShapeUiSnapshot({});
				barUISet.specTarget.Discard();
				if (pipeline) Pipeline::Shutdown();
				if (com) CoUninitialize();
				globalPath = originalPath;
			}
		} runtime;

		const auto directory = std::filesystem::absolute(L"Build/ui3-attribute-dirty");
		std::filesystem::create_directories(directory / L"runtime-config/Inkeys/Config");
		globalPath = (directory / L"runtime-config").wstring() + L"\\";
		std::ofstream report(directory / L"results.log", std::ios::binary | std::ios::trunc);
		if (!report) return 4;

		runtime.com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
		if (!runtime.com) return 2;
		runtime.pipeline = SUCCEEDED(Pipeline::Initialize());
		if (!runtime.pipeline) return 3;
		IdtFontFileLoader::IsLoaderInitialized(); IdtFontCollectionLoader::IsLoaderInitialized();
		const std::array<UINT, 4> fonts{ IDR_TTF1, IDR_TTF7, IDR_TTF3, IDR_TTF8 };
		if (FAILED(Pipeline::InitializeFontCollection(IdtFontFileLoader::GetLoader(), IdtFontCollectionLoader::GetLoader(), fonts)))
			return 5;

		auto& owner = barUISet;
		Capture current, previous;
		BarVisualShadowFrame shadow{};
		unsigned shadowFrames = 0, captures = 0;
		std::vector<StepStats> steps;
		StepStats* step = nullptr;

		SetBarVisualShadowTestHook([&](const BarVisualShadowFrame& frame) { shadow = frame; ++shadowFrames; });

		BarGoldenMainSceneHost host;
		host.captured = [&](const UPDATELAYEREDWINDOWINFO& info, const std::vector<BYTE>& bytes)
			{
				current.size = *info.psize;
				current.bytes = bytes;
				current.serial = ++captures;
			};
		// 自然帧对照（动画关闭）：每个交互先跑自然帧（生产局部提交，读回保留画面 R），再强制整幅（参考 F），
		// 状态不变时 R 必须逐字节等于 F；自然帧未呈现而前后参考不同，说明变化未触发呈现。两类都是硬失败。
		unsigned naturalChecks = 0, naturalMismatches = 0, naturalMissed = 0, naturalPartial = 0, naturalUnsettled = 0, naturalTransient = 0;
		// 开动画按压步骤中带「未分类」整窗位的自然帧数（按钮标签字号修正已由登记表采样，不得再整窗）。
		unsigned naturalAnimFrames = 0, naturalAnimUnclassified = 0, naturalAnimUnsettled = 0;
		double naturalDirtyArea = 0, naturalWindowArea = 0;
		std::function<bool()> naturalFrameRef;
		host.settledNatural = [&](const std::function<bool()>& renderFrame, const std::function<bool()>& naturalFrame) -> bool
			{
				naturalFrameRef = naturalFrame;
				return host.settled(renderFrame);
			};
		host.settled = [&](const std::function<bool()>& renderFrame)
			{
				// 渲染一帧并核对；返回基础设施是否正常。
				auto frame = [&]() -> bool
					{
						const unsigned shadowBefore = shadowFrames;
						previous = current;
						if (!renderFrame()) return false;
						if (!step) return true;
						++step->frames;
						if (shadowFrames == shadowBefore) return true; // 本帧影子未提交（未进入呈现分支）
						step->purity += static_cast<unsigned>(shadow.purityMismatches + shadow.purityGroupMismatches);
						if (!shadow.purityNames.empty() && step->purityLogged < 3)
						{
							++step->purityLogged;
							report << "IMPURE " << step->name << " frame=" << step->frames;
							for (const auto& name : shadow.purityNames) report << ' ' << name;
							report << '\n';
						}
						if (previous.bytes.empty() || previous.size.cx != current.size.cx || previous.size.cy != current.size.cy
							|| previous.bytes.size() != current.bytes.size())
						{
							++step->skippedMapping;
							return true;
						}
						++step->compared;
						const RECT changed = DifferenceBounds(previous, current);
						if (Area(changed) > 0) ++step->changedFrames;
						if (shadow.fullReason & 1u) ++step->fullFrames;
						step->fullReasons |= shadow.fullReason;
						if (shadow.fontRegistryOnly) ++step->fontRegistryFrames;
						// 允许范围 = 影子登记表 ∪ 面板登记表 ∪ 生产光影脏区（光影不在登记表比较范围）。
						RECT allowed = shadow.shadowDamage;
						for (const RECT& extra : { shadow.panelDamage, shadow.lightDamage })
						{
							if (Area(extra) == 0) continue;
							if (Area(allowed) == 0) { allowed = extra; continue; }
							allowed.left = (std::min)(allowed.left, extra.left); allowed.top = (std::min)(allowed.top, extra.top);
							allowed.right = (std::max)(allowed.right, extra.right); allowed.bottom = (std::max)(allowed.bottom, extra.bottom);
						}
						RECT production = shadow.panelDamage;
						if (Area(shadow.lightDamage) > 0)
						{
							if (Area(production) == 0) production = shadow.lightDamage;
							else
							{
								production.left = (std::min)(production.left, shadow.lightDamage.left); production.top = (std::min)(production.top, shadow.lightDamage.top);
								production.right = (std::max)(production.right, shadow.lightDamage.right); production.bottom = (std::max)(production.bottom, shadow.lightDamage.bottom);
							}
						}
						if (!Contains(production, changed))
						{
							++step->productionViolations;
							report << "PROD-UNCOVERED " << step->name << " frame=" << step->frames << " changed=" << Text(changed)
								<< " panel=" << Text(shadow.panelDamage) << " light=" << Text(shadow.lightDamage) << " changedNames=";
							for (const auto& n : shadow.panelChangedNames) report << n << ' ';
							report << '\n';
						}
						step->shadowArea += Area(allowed);
						step->presentArea += Area(shadow.presentDirty);
						if (!Contains(allowed, changed))
						{
							++step->violations;
							if (Area(changed) > Area(step->worstUncovered)) step->worstUncovered = changed;
							report << "UNCOVERED " << step->name << " frame=" << step->frames << " changed=" << Text(changed)
								<< " shadow=" << Text(shadow.shadowDamage) << " panel=" << Text(shadow.panelDamage)
								<< " present=" << Text(shadow.presentDirty) << '\n';
						}
						return true;
					};
				auto run = [&](const char* name, const std::function<void()>& action, int frames = 40) -> bool
					{
						steps.push_back({});
						step = &steps.back();
						step->name = name;
						action();
						owner.UpdateRendering(false);
						for (int i = 0; i < frames; ++i) if (!frame()) return false;
						step = nullptr;
						return true;
					};
				// 悬停握手与产品交互端一致：进入时由交互端启动淡入并置 Showing，离开时交给渲染端淡出。
				auto hoverStage = [&](IdtAtomic<ButtonHoverStageEnum>& stage, BarUISetShapeEnum key, const char* name) -> bool
					{
						const auto shape = owner.shapeMap[key];
						if (!shape || !shape->fill) return true;
						const std::string enter = std::string(name) + ".hover";
						const std::string leave = std::string(name) + ".leave";
						return run(enter.c_str(), [&] { (void)StartButtonHoverVisual(&shape->pct, &*shape->fill, &stage); })
							&& run(leave.c_str(), [&] { (void)StopButtonHoverVisual(&shape->pct, &*shape->fill, &stage, false); });
					};
				// 基线两帧不计入统计，建立影子已提交快照与上一帧读回。
				if (!frame() || !frame()) return false;
				bool ok = run("static", [] {});
				ok = ok && hoverStage(owner.drawAttributeBrushHoverStage, BarUISetShapeEnum::DrawAttributeBar_Brush1, "draw.brush");
				ok = ok && hoverStage(owner.drawAttributeHighlightHoverStage, BarUISetShapeEnum::DrawAttributeBar_Highlight1, "draw.highlighter");
				ok = ok && hoverStage(owner.drawAttributeLaserHoverStage, BarUISetShapeEnum::DrawAttributeBar_Laser, "draw.laser");
				ok = ok && hoverStage(owner.drawAttributeColorPickerEntryHoverStage,
					BarUISetShapeEnum::DrawAttributeBar_ColorSelect12Feedback, "draw.picker-entry");
				// 粗细区按组合登记，三档预设与三角按钮的悬停底板必须进入组合采样（V10 实机回归）。
				ok = ok && hoverStage(owner.drawAttributeThicknessFineHoverStage, BarUISetShapeEnum::DrawAttributeBar_ThicknessFine, "draw.thickness-fine");
				ok = ok && hoverStage(owner.drawAttributeThicknessCoarseHoverStage, BarUISetShapeEnum::DrawAttributeBar_ThicknessCoarse, "draw.thickness-coarse");
				ok = ok && hoverStage(owner.drawAttributeThicknessAdjustHoverStage, BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust, "draw.thickness-adjust");
				for (size_t i = 0; i < owner.drawAttributeSwatchFeedback.size() && i < 2; ++i)
				{
					const auto key = static_cast<BarUISetShapeEnum>(static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1Feedback) + i);
					const std::string name = "draw.swatch" + std::to_string(i + 1);
					ok = ok && hoverStage(owner.drawAttributeSwatchFeedback[i].hoverStage, key, name.c_str());
				}
				if (auto* select = owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Select)])
				{
					ok = ok && run("main.select.hover", [&] { (void)StartButtonHoverVisual(*select); });
					ok = ok && run("main.select.leave", [&] { (void)StopButtonHoverVisual(*select, false); });
					ok = ok && run("main.select.press", [&] { SetButtonPressedVisual(*select, true); });
					ok = ok && run("main.select.release", [&] { SetButtonPressedVisual(*select, false); });
				}
				ok = ok && run("picker.open", [&] { owner.barState.drawAttributeBar.colorPickerOpen = true; }, 60);
				ok = ok && run("picker.tone", [&] { owner.colorPicker.colorPickerDarkTone = !owner.colorPicker.colorPickerDarkTone; }, 40);
				ok = ok && run("picker.close", [&] { owner.barState.drawAttributeBar.colorPickerOpen = false; }, 60);
				ok = ok && run("more.expand", [&] { owner.barState.moreExpanded = true; }, 60);
				ok = ok && run("more.collapse", [&] { owner.barState.moreExpanded = false; }, 60);

				// —— 属性面板：经面板自己的 Pointer 入口注入真实悬停/按下/抬起（布局坐标 = 呈现快照矩形中心 × zoom）。
				auto message = [](UINT kind, double x, double y, bool left)
					{
						ExMessage m{}; m.message = kind; m.x = static_cast<short>(x); m.y = static_cast<short>(y); m.lbutton = left;
						return m;
					};
				ok = ok && run("eraser.open", [&]
					{
						owner.barState.drawAttribute = false; (void)ChangeStateModeToEraser();
						owner.barState.eraserAttribute = true; owner.barButtonSet.StateUpdate();
					}, 90);
				auto eraserCenter = [&](int item)
					{
						const auto snapshot = owner.eraserAttribute.PresentationSnapshot();
						const auto r = snapshot.geometry.items[item];
						return std::pair<double, double>{ (r.left + r.right) / 2 * snapshot.zoom, (r.top + r.bottom) / 2 * snapshot.zoom };
					};
				// 0=清空、9=设置只悬停（点击会切模式或打开设置窗口）；1–3 尺寸、4 自动粗细、5 菜单箭头、10 手掌。
				for (int item : { 0, 1, 2, 3, 4, 10 })
				{
					const std::string base = "eraser.item" + std::to_string(item);
					const auto [x, y] = eraserCenter(item);
					ok = ok && run((base + ".hover").c_str(), [&] { (void)owner.eraserAttribute.Pointer(owner, message(WM_MOUSEMOVE, x, y, false)); });
					if (item != 0)
					{
						ok = ok && run((base + ".press").c_str(), [&] { (void)owner.eraserAttribute.Pointer(owner, message(WM_LBUTTONDOWN, x, y, true)); });
						ok = ok && run((base + ".release").c_str(), [&] { (void)owner.eraserAttribute.Pointer(owner, message(WM_LBUTTONUP, x, y, false)); }, 60);
					}
					ok = ok && run((base + ".leave").c_str(), [&] { (void)owner.eraserAttribute.ResetPointerFeedback(); });
				}
				{
					const auto [x, y] = eraserCenter(5);
					ok = ok && run("eraser.menu.open", [&]
						{
							(void)owner.eraserAttribute.Pointer(owner, message(WM_LBUTTONDOWN, x, y, true));
							(void)owner.eraserAttribute.Pointer(owner, message(WM_LBUTTONUP, x, y, false));
						}, 90);
				}
				for (int item : { 6, 7, 8, 9 })
				{
					const std::string base = "eraser.menu.item" + std::to_string(item);
					const auto [x, y] = eraserCenter(item);
					ok = ok && run((base + ".hover").c_str(), [&] { (void)owner.eraserAttribute.Pointer(owner, message(WM_MOUSEMOVE, x, y, false)); });
					if (item != 9)
					{
						ok = ok && run((base + ".press").c_str(), [&] { (void)owner.eraserAttribute.Pointer(owner, message(WM_LBUTTONDOWN, x, y, true)); });
						ok = ok && run((base + ".release").c_str(), [&] { (void)owner.eraserAttribute.Pointer(owner, message(WM_LBUTTONUP, x, y, false)); }, 60);
					}
					ok = ok && run((base + ".leave").c_str(), [&] { (void)owner.eraserAttribute.ResetPointerFeedback(); });
				}
				ok = ok && run("eraser.close", [&] { owner.barState.eraserSensitivityOpen = false; owner.barState.eraserAttribute = false; }, 90);

				ok = ok && run("geometry.open", [&]
					{
						(void)ChangeStateModeToShape(); owner.barState.geometryAttribute = true; owner.barButtonSet.StateUpdate();
					}, 120);
				auto geometryCenter = [&](int item)
					{
						const auto snapshot = owner.geometryAttribute.PresentationSnapshot();
						const auto r = snapshot.items[item];
						return std::pair<double, double>{ (r.left + r.right) / 2 * snapshot.zoom, (r.top + r.bottom) / 2 * snapshot.zoom };
					};
				// 类别前两项悬停；线型与粗细档悬停并点击（选中转移）。
				for (int item : { 0, 1, GeometryStyleBegin + 1, GeometryPatternBegin + 1, GeometryThicknessBegin + 1, GeometryThicknessBegin + 2 })
				{
					const std::string base = "geometry.item" + std::to_string(item);
					const auto [x, y] = geometryCenter(item);
					ok = ok && run((base + ".hover").c_str(), [&] { (void)owner.geometryAttribute.Pointer(owner, message(WM_MOUSEMOVE, x, y, false)); });
					if (item >= GeometryStyleBegin)
					{
						ok = ok && run((base + ".press").c_str(), [&] { (void)owner.geometryAttribute.Pointer(owner, message(WM_LBUTTONDOWN, x, y, true)); });
						ok = ok && run((base + ".release").c_str(), [&] { (void)owner.geometryAttribute.Pointer(owner, message(WM_LBUTTONUP, x, y, false)); }, 60);
					}
					ok = ok && run((base + ".leave").c_str(), [&] { (void)owner.geometryAttribute.ResetPointerFeedback(); });
				}
				ok = ok && run("geometry.close", [&] { owner.barState.geometryAttribute = false; }, 120);
				if (!ok || !naturalFrameRef) return ok;

				// —— 自然帧对照阶段：关闭动画，每个交互一帧到位。
				const bool savedAnimation = UiAnimationEnabled;
				UiAnimationEnabled = false;
				Capture reference;
				if (!renderFrame()) return false;
				reference = current;
				auto natural = [&](const std::string& name, const std::function<void()>& action, int maxFrames = 8) -> bool
					{
						action();
						const BarVisualShadowFrame before = shadow;
						// 动画关闭时部分交互仍分两帧完成（悬停握手先显示后淡出），连续跑自然帧直到不再呈现，
						// 取最后一次局部提交的保留画面；状态稳定后才与整幅参考比较。
						bool presented = false;
						Capture retained;
						BarVisualShadowFrame naturalShadow{};
						RECT dirtyUnion{};
						std::vector<RECT> iterations;
						std::vector<std::vector<std::string>> changedLog;
						bool settled = false, replaced = false, alphaReplaced = false;
						unsigned anyFullReason = 0;
						for (int i = 0; i < maxFrames; ++i)
						{
							if (!naturalFrameRef()) { settled = true; break; }
							presented = true; retained = current; naturalShadow = shadow; iterations.push_back(shadow.presentDirty);
							if (naturalChecks < 6 || name.find("n.draw.swatch2") != std::string::npos || name.find("n.more") != std::string::npos || name.find("n.main.preset2") != std::string::npos || name.find("n.main.open") != std::string::npos || name.find("n.main.panel") != std::string::npos || name.find("n.anim") != std::string::npos)
							{
								report << "NATURAL-FRAME " << name << " i=" << i << " viewport=" << shadow.viewport.cx << 'x' << shadow.viewport.cy
									<< " present=" << Text(shadow.presentDirty) << " shadow=" << Text(shadow.shadowDamage)
									<< " panel=" << Text(shadow.panelDamage) << " light=" << Text(shadow.lightDamage) << " reason=" << shadow.fullReason << " changed=";
								for (const auto& n : shadow.panelChangedNames) report << n << ' ';
								report << '\n';
							}
							changedLog.push_back(shadow.panelChangedNames);
							// 只统计开动画的按下/抬起步骤（打开/关闭面板本身另有整窗原因）。
							const bool animPress = name.rfind("n.anim", 0) == 0
								&& (name.find(".press") != std::string::npos || name.find(".release") != std::string::npos);
							if (animPress)
							{
								++naturalAnimFrames;
								if (shadow.fullReason & 1u) ++naturalAnimUnclassified;
							}
							if (shadow.fullReason & 4u) replaced = true;
							if (shadow.fullReason & 8u) alphaReplaced = true;
							anyFullReason |= shadow.fullReason;
							// 1 像素保活不计入并集（它固定在窗口左上角，会把并集拉成大矩形）。
							if (Area(shadow.presentDirty) <= 1) {}
							else if (Area(dirtyUnion) == 0) dirtyUnion = shadow.presentDirty;
							else
							{
								dirtyUnion.left = (std::min)(dirtyUnion.left, shadow.presentDirty.left); dirtyUnion.top = (std::min)(dirtyUnion.top, shadow.presentDirty.top);
								dirtyUnion.right = (std::max)(dirtyUnion.right, shadow.presentDirty.right); dirtyUnion.bottom = (std::max)(dirtyUnion.bottom, shadow.presentDirty.bottom);
							}
						}
						naturalShadow.presentDirty = dirtyUnion;
						// 开动画按压步骤逐项把关：必须真的局部呈现过（非空脏区、非整窗替换），否则视为未通过。
						if (name.rfind("n.anim", 0) == 0
							&& (name.find(".press") != std::string::npos || name.find(".release") != std::string::npos)
							&& (!presented || replaced || alphaReplaced || anyFullReason != 0 || Area(dirtyUnion) == 0))
						{
							++naturalAnimUnsettled;
							report << "NATURAL-ANIM-FAIL " << name << " presented=" << presented << " replaced=" << replaced << " alpha=" << alphaReplaced << " reasons=" << anyFullReason
								<< " dirty=" << Text(dirtyUnion) << '\n';
						}
						if (!renderFrame()) return false;
						const Capture full = current;
						++naturalChecks;
						const bool sameSize = full.size.cx == reference.size.cx && full.size.cy == reference.size.cy;
						if (presented && replaced)
						{
							// 窗口尺寸变化引起整窗替换（容量收放的过渡）：比较的不是局部脏区，单独计数。
							++naturalTransient;
						}
						else if (presented && !settled)
						{
							// 状态持续变化（例如动画关闭时选中按钮上的悬停与选中目标互相改写）：无法与整幅参考逐字节比较。
							++naturalUnsettled;
							if (name.rfind("n.anim", 0) == 0 && (name.find(".press") != std::string::npos || name.find(".release") != std::string::npos))
								++naturalAnimUnsettled;
							report << "NATURAL-UNSETTLED " << name << '\n';
						}
						else if (presented)
						{
							if (retained.size.cx == full.size.cx && retained.size.cy == full.size.cy && retained.bytes != full.bytes)
							{
								++naturalMismatches;
								report << "NATURAL-MISMATCH " << name << " retained!=full diff=" << Text(DifferenceBounds(retained, full))
									<< " dirty=" << Text(naturalShadow.presentDirty) << " frames=";
								for (const auto& r : iterations) report << Text(r);
								report << " changed=";
								for (const auto& names : changedLog)
								{
									report << '{';
									for (const auto& n : names) report << n << ' ';
									report << '}';
								}
								report << '\n';
							}
							const LONG window = full.size.cx * full.size.cy;
							naturalWindowArea += window; naturalDirtyArea += Area(naturalShadow.presentDirty);
							if (Area(naturalShadow.presentDirty) < window) ++naturalPartial;
							report << "NATURAL " << name << " reason=" << naturalShadow.fullReason << " dirty=" << Text(naturalShadow.presentDirty)
								<< " area=" << (window > 0 ? static_cast<double>(Area(naturalShadow.presentDirty)) / window : 0.0) << '\n';
						}
						else if (sameSize && reference.bytes != full.bytes)
						{
							++naturalMissed;
							report << "NATURAL-MISSED " << name << " changed=" << Text(DifferenceBounds(reference, full)) << '\n';
						}
						(void)before;
						reference = full;
						return true;
					};
				// 准备步骤：切换工具模式、开合面板会改变窗口尺寸（整窗替换）并牵动旧路径，强制整幅推进到稳定，不做断言。
				auto setup = [&](const std::function<void()>& action) -> bool
					{
						action();
						for (int i = 0; i < 4; ++i) if (!renderFrame()) return false;
						reference = current;
						return true;
					};
				bool nat = setup([&]
					{
						owner.barState.drawAttribute = false; (void)ChangeStateModeToEraser();
						owner.barState.eraserAttribute = true; owner.barButtonSet.StateUpdate();
					});
				for (int item : { 0, 1, 2, 3, 4, 10 })
				{
					const std::string base = "n.eraser.item" + std::to_string(item);
					const auto [x, y] = eraserCenter(item);
					nat = nat && natural(base + ".hover", [&] { (void)owner.eraserAttribute.Pointer(owner, message(WM_MOUSEMOVE, x, y, false)); });
					if (item != 0)
					{
						nat = nat && natural(base + ".press", [&] { (void)owner.eraserAttribute.Pointer(owner, message(WM_LBUTTONDOWN, x, y, true)); });
						nat = nat && natural(base + ".release", [&] { (void)owner.eraserAttribute.Pointer(owner, message(WM_LBUTTONUP, x, y, false)); });
					}
					nat = nat && natural(base + ".leave", [&] { (void)owner.eraserAttribute.ResetPointerFeedback(); });
				}
				{
					const auto [x, y] = eraserCenter(5);
					nat = nat && setup([&]
						{
							(void)owner.eraserAttribute.Pointer(owner, message(WM_LBUTTONDOWN, x, y, true));
							(void)owner.eraserAttribute.Pointer(owner, message(WM_LBUTTONUP, x, y, false));
						});
				}
				for (int item : { 6, 7, 8 })
				{
					const std::string base = "n.eraser.menu.item" + std::to_string(item);
					const auto [x, y] = eraserCenter(item);
					nat = nat && natural(base + ".hover", [&] { (void)owner.eraserAttribute.Pointer(owner, message(WM_MOUSEMOVE, x, y, false)); });
					nat = nat && natural(base + ".press", [&] { (void)owner.eraserAttribute.Pointer(owner, message(WM_LBUTTONDOWN, x, y, true)); });
					nat = nat && natural(base + ".release", [&] { (void)owner.eraserAttribute.Pointer(owner, message(WM_LBUTTONUP, x, y, false)); });
					nat = nat && natural(base + ".leave", [&] { (void)owner.eraserAttribute.ResetPointerFeedback(); });
				}
				nat = nat && setup([&] { owner.barState.eraserSensitivityOpen = false; owner.barState.eraserAttribute = false; });
				nat = nat && setup([&]
					{
						(void)ChangeStateModeToShape(); owner.barState.geometryAttribute = true; owner.barButtonSet.StateUpdate();
					});
				for (int item : { 0, 1, GeometryStyleBegin, GeometryStyleBegin + 1, GeometryPatternBegin, GeometryPatternBegin + 1,
					GeometryThicknessBegin, GeometryThicknessBegin + 1, GeometryThicknessBegin + 2 })
				{
					const std::string base = "n.geometry.item" + std::to_string(item);
					const auto [x, y] = geometryCenter(item);
					nat = nat && natural(base + ".hover", [&] { (void)owner.geometryAttribute.Pointer(owner, message(WM_MOUSEMOVE, x, y, false)); });
					if (item >= GeometryStyleBegin)
					{
						nat = nat && natural(base + ".press", [&] { (void)owner.geometryAttribute.Pointer(owner, message(WM_LBUTTONDOWN, x, y, true)); });
						nat = nat && natural(base + ".release", [&] { (void)owner.geometryAttribute.Pointer(owner, message(WM_LBUTTONUP, x, y, false)); });
					}
					nat = nat && natural(base + ".leave", [&] { (void)owner.geometryAttribute.ResetPointerFeedback(); });
				}
				nat = nat && setup([&] { owner.barState.geometryAttribute = false; });
				// 绘制属性：悬停握手、色块按压、取色器开合与深浅切换。
				nat = nat && setup([&]
					{
						(void)ChangeStateModeToPen(); owner.barState.drawAttribute = true; owner.barButtonSet.StateUpdate();
					});
				auto drawHover = [&](IdtAtomic<ButtonHoverStageEnum>& stage, BarUISetShapeEnum key, const std::string& name) -> bool
					{
						const auto shape = owner.shapeMap[key];
						if (!shape || !shape->fill) return true;
						return natural(name + ".hover", [&] { (void)StartButtonHoverVisual(&shape->pct, &*shape->fill, &stage); })
							&& natural(name + ".leave", [&] { (void)StopButtonHoverVisual(&shape->pct, &*shape->fill, &stage, false); });
					};
				nat = nat && drawHover(owner.drawAttributeHighlightHoverStage, BarUISetShapeEnum::DrawAttributeBar_Highlight1, "n.draw.highlighter");
				// 激光按钮独立绘制，须单独登记（Codex 终审 #1）。
				nat = nat && drawHover(owner.drawAttributeLaserHoverStage, BarUISetShapeEnum::DrawAttributeBar_Laser, "n.draw.laser");
				nat = nat && drawHover(owner.drawAttributeColorPickerEntryHoverStage, BarUISetShapeEnum::DrawAttributeBar_ColorSelect12Feedback, "n.draw.picker-entry");
				// 粗细区悬停：动画关闭时悬停淡入/淡出仍按真实时长推进，放宽帧数推进到稳定后再与整幅参考逐字节比较；
				// 选中档的悬停会被渲染端清除，这里只测未选中的中、粗两档与三角按钮。
				auto thicknessHover = [&](IdtAtomic<ButtonHoverStageEnum>& stage, BarUISetShapeEnum key, const std::string& name) -> bool
					{
						const auto shape = owner.shapeMap[key];
						if (!shape || !shape->fill) return true;
						return natural(name + ".hover", [&] { (void)StartButtonHoverVisual(&shape->pct, &*shape->fill, &stage); }, 400)
							&& natural(name + ".leave", [&] { (void)StopButtonHoverVisual(&shape->pct, &*shape->fill, &stage, false); }, 400);
					};
				nat = nat && thicknessHover(owner.drawAttributeThicknessMediumHoverStage, BarUISetShapeEnum::DrawAttributeBar_ThicknessMedium, "n.draw.thickness-medium");
				nat = nat && thicknessHover(owner.drawAttributeThicknessCoarseHoverStage, BarUISetShapeEnum::DrawAttributeBar_ThicknessCoarse, "n.draw.thickness-coarse");
				nat = nat && thicknessHover(owner.drawAttributeThicknessAdjustHoverStage, BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust, "n.draw.thickness-adjust");
				for (size_t i = 0; i < 3 && i < owner.drawAttributeSwatchFeedback.size(); ++i)
				{
					const auto key = static_cast<BarUISetShapeEnum>(static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1Feedback) + i);
					const std::string name = "n.draw.swatch" + std::to_string(i + 1);
					nat = nat && drawHover(owner.drawAttributeSwatchFeedback[i].hoverStage, key, name);
					nat = nat && natural(name + ".press", [&] { owner.drawAttributeSwatchFeedback[i].pressScale.SetTar(ButtonPressScale); });
					nat = nat && natural(name + ".release", [&] { owner.drawAttributeSwatchFeedback[i].pressScale.SetTar(1.0); });
				}
				nat = nat && natural("n.draw.picker.open", [&] { owner.barState.drawAttributeBar.colorPickerOpen = true; });
				nat = nat && natural("n.draw.picker.tone", [&] { owner.colorPicker.colorPickerDarkTone = !owner.colorPicker.colorPickerDarkTone; });
				nat = nat && natural("n.draw.picker.close", [&] { owner.barState.drawAttributeBar.colorPickerOpen = false; });
				nat = nat && setup([&] { owner.barState.drawAttribute = false; });
				// 主栏与 More（V6）：主栏按钮悬停/按下/抬起、More 展开后浮层按钮与关闭键按压、收起。
				for (const auto preset : { BarButtonPresetEnum::Select, BarButtonPresetEnum::Recall, BarButtonPresetEnum::Clean })
				{
					auto* button = owner.barButtonSet.preset[static_cast<int>(preset)];
					if (!button) continue;
					const std::string base = "n.main.preset" + std::to_string(static_cast<int>(preset));
					nat = nat && natural(base + ".hover", [&] { (void)StartButtonHoverVisual(*button); });
					nat = nat && natural(base + ".press", [&] { SetButtonPressedVisual(*button, true); });
					nat = nat && natural(base + ".release", [&] { SetButtonPressedVisual(*button, false); });
					nat = nat && natural(base + ".leave", [&] { (void)StopButtonHoverVisual(*button, false); });
				}
				// 打开属性面板的主栏按钮（V10 实机：按下瞬间整窗刷新）：按真实 Down 的视觉写入顺序模拟——
				// 结束悬停、置按压态、唤醒渲染；分别在面板关闭与已打开时测，记录整窗原因与刷新面积。
				auto pressMain = [&](BarButtonPresetEnum preset, const std::string& base) -> bool
					{
						// More 按钮不在 preset[] 里，单独取。
						auto* button = preset == BarButtonPresetEnum::More ? owner.barButtonSet.GetMoreButton()
							: owner.barButtonSet.preset[static_cast<int>(preset)];
						if (!button) return true;
						return natural(base + ".hover", [&] { (void)StartButtonHoverVisual(*button); })
							&& natural(base + ".press", [&] { (void)StopButtonHoverVisual(*button, true); SetButtonPressedVisual(*button, true); })
							&& natural(base + ".release", [&] { SetButtonPressedVisual(*button, false); });
					};
				for (const auto preset : { BarButtonPresetEnum::Draw, BarButtonPresetEnum::Eraser, BarButtonPresetEnum::Geometry, BarButtonPresetEnum::More })
					nat = nat && pressMain(preset, "n.main.open" + std::to_string(static_cast<int>(preset)));
				nat = nat && setup([&] { owner.barState.drawAttribute = true; });
				for (const auto preset : { BarButtonPresetEnum::Draw, BarButtonPresetEnum::Eraser, BarButtonPresetEnum::Geometry, BarButtonPresetEnum::More })
					nat = nat && pressMain(preset, "n.main.panel" + std::to_string(static_cast<int>(preset)));
				nat = nat && setup([&] { owner.barState.drawAttribute = false; });
				nat = nat && natural("n.more.expand", [&] { owner.barState.moreExpanded = true; });
				{
					const auto more = owner.barButtonSet.GetMoreButtonSnapshot();
					BarButtonClass* item = !more.explicitMore.empty() ? more.explicitMore.front().get()
						: (!more.forcedOverflow.empty() ? more.forcedOverflow.front().get() : nullptr);
					if (item)
					{
						nat = nat && natural("n.more.item.hover", [&] { (void)StartButtonHoverVisual(*item); });
						nat = nat && natural("n.more.item.press", [&] { SetButtonPressedVisual(*item, true); });
						nat = nat && natural("n.more.item.release", [&] { SetButtonPressedVisual(*item, false); });
						nat = nat && natural("n.more.item.leave", [&] { (void)StopButtonHoverVisual(*item, false); });
					}
				}
				nat = nat && natural("n.more.close.press", [&] { owner.barState.moreClosePress = true; });
				nat = nat && natural("n.more.close.release", [&] { owner.barState.moreClosePress = false; });
				nat = nat && natural("n.more.collapse", [&] { owner.barState.moreExpanded = false; });
				// 开动画按压（V10 实机：按下瞬间整窗）：按钮标签的字号修正随按压目标播放，
				// 修正值已入登记表采样，只唤醒不整窗；跑到稳定后仍与整幅参考逐字节比较。
				UiAnimationEnabled = true;
				for (const auto preset : { BarButtonPresetEnum::Draw, BarButtonPresetEnum::Select })
				{
					auto* button = owner.barButtonSet.preset[static_cast<int>(preset)];
					if (!button) continue;
					const std::string base = "n.anim.main" + std::to_string(static_cast<int>(preset));
					nat = nat && natural(base + ".press", [&] { (void)StopButtonHoverVisual(*button, true); SetButtonPressedVisual(*button, true); }, 120);
					nat = nat && natural(base + ".release", [&] { SetButtonPressedVisual(*button, false); }, 120);
				}
				// 面板内按钮（实机日志的 geometry.button*）：按真实指针 Down/Up 驱动。
				nat = nat && natural("n.anim.geometry.open", [&]
					{
						(void)ChangeStateModeToShape(); owner.barState.geometryAttribute = true; owner.barButtonSet.StateUpdate();
					}, 240);
				for (int item : { 0, GeometryStyleBegin + 1 })
				{
					const std::string base = "n.anim.geometry.item" + std::to_string(item);
					const auto [x, y] = geometryCenter(item);
					nat = nat && natural(base + ".press", [&] { (void)owner.geometryAttribute.Pointer(owner, message(WM_LBUTTONDOWN, x, y, true)); }, 120);
					nat = nat && natural(base + ".release", [&] { (void)owner.geometryAttribute.Pointer(owner, message(WM_LBUTTONUP, x, y, false)); }, 120);
					nat = nat && natural(base + ".leave", [&] { (void)owner.geometryAttribute.ResetPointerFeedback(); }, 120);
				}
				nat = nat && natural("n.anim.geometry.close", [&] { owner.barState.geometryAttribute = false; }, 240);
				UiAnimationEnabled = savedAnimation;
				return nat;
			};

		const HRESULT hr = RunGoldenMainSceneForHost(true, true, 1.0, host);
		unsigned totalFrames = 0, totalCompared = 0, totalViolations = 0, totalPurity = 0, totalProduction = 0;
		for (const auto& item : steps)
		{
			totalFrames += item.frames; totalCompared += item.compared;
			totalViolations += item.violations; totalPurity += item.purity; totalProduction += item.productionViolations;
			const double ratio = item.presentArea > 0 ? item.shadowArea / item.presentArea : 0.0;
			report << "STEP " << item.name << " frames=" << item.frames << " compared=" << item.compared
				<< " changedFrames=" << item.changedFrames << " violations=" << item.violations << " prodViolations=" << item.productionViolations
				<< " skippedMapping=" << item.skippedMapping << " purity=" << item.purity
				<< " shadow/present=" << ratio << " worst=" << Text(item.worstUncovered)
				<< " unclassified=" << item.fullFrames << " reasons=" << item.fullReasons << " fontRegistry=" << item.fontRegistryFrames << '\n';
		}
		report << "NATURAL-SUMMARY checks=" << naturalChecks << " mismatches=" << naturalMismatches << " missed=" << naturalMissed
			<< " unsettled=" << naturalUnsettled << " transient=" << naturalTransient
			<< " partial=" << naturalPartial << " meanArea=" << (naturalWindowArea > 0 ? naturalDirtyArea / naturalWindowArea : 0.0)
			<< " animFrames=" << naturalAnimFrames << " animUnclassified=" << naturalAnimUnclassified
			<< " animUnsettled=" << naturalAnimUnsettled << '\n';
		report << "SUMMARY hr=0x" << std::hex << static_cast<unsigned>(hr) << std::dec << " steps=" << steps.size()
			<< " frames=" << totalFrames << " compared=" << totalCompared << " violations=" << totalViolations
			<< " prodViolations=" << totalProduction << " purity=" << totalPurity << " shadowFrames=" << shadowFrames << '\n';
		std::cout << "[AttributeDirty] hr=0x" << std::hex << static_cast<unsigned>(hr) << std::dec << " steps=" << steps.size()
			<< " frames=" << totalFrames << " compared=" << totalCompared << " violations=" << totalViolations
			<< " prodViolations=" << totalProduction << " purity=" << totalPurity << " shadowFrames=" << shadowFrames
			<< " natural=" << naturalChecks << " naturalMismatches=" << naturalMismatches << " naturalMissed=" << naturalMissed << std::endl;
		// V3 影子模式只诊断：基础设施失败（场景未搭起、没有影子帧）才返回非 0。
		if (hr != S_OK || shadowFrames == 0 || totalCompared == 0) return 1;
		// 自然帧对照是生产路径的硬断言：局部保留画面必须等于整幅重画，变化必须触发呈现。
		if (naturalChecks == 0 || naturalMismatches != 0 || naturalMissed != 0) return 6;
		// V6 起主栏、More、绘制属性、属性面板全部由生产登记表负责：像素变化必须落在生产脏区（登记表 ∪ 光影）内，
		// 且生产登记表在绘制后重新采样与准备时一致（绘制阶段不得再写入已采样字段）。
		if (totalProduction != 0) return 7;
		if (totalPurity != 0) return 8;
		// 开动画按压：按钮标签字号修正交给登记表，不得出现未分类整窗帧，且必须在预算内收敛后逐字节对照。
		if (naturalAnimFrames == 0 || naturalAnimUnclassified != 0 || naturalAnimUnsettled != 0) return 9;
		return 0;
	}
}
