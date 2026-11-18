module;
// 第二宿主验证（--ui3-host-offscreen-test）：证明主栏内容不依赖主栏自己的窗口与 GDI 兼容目标。
// 同一帧主栏内容经协调器的「内容重放」桥画进测试自建的帧目标（普通位图形态、外部上下文形态）
// 与测试新建的内容绘制器，再与现有 GDI 兼容路径（主栏帧目标 + 帧事务 + 测试提交端读回）逐字节比较。
// 场景搭建与确定性处理复用像素基线入口（Bar.Golden.Test.cpp）；全程不创建任何窗口。
#include "../../../IdtMain.h"
#include "../../../IdtState.h"
#include "../../../resource.h"
#include "../../Drawing/Draw3/Draw3.ShapeInteraction.h"
#include "../../Log/Log.h"
#include "../../Window/Window.Legacy.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <cstdint>
#include <crtdbg.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Inkeys.UI.Bar;
import :Main;
import :Theme;
import Inkeys.UI.RenderPipeline;
import Inkeys.Text.Font;

using namespace Inkeys::UI::UI3;

namespace Inkeys::UI::Bar
{
	namespace
	{
		namespace Pipeline = Inkeys::UI::RenderPipeline;
		using Microsoft::WRL::ComPtr;

		struct HostScene
		{
			const char* name;
			bool drawAttribute;
			bool dark;
			double scale;
		};
		constexpr HostScene HostScenes[]{
			{"expanded-light-100", false, false, 1.0},
			{"draw-attr-dark-150", true, true, 1.5},
		};
		// 首帧之后再连续渲染的帧数：超过绘制器「连续 8 帧」的遮罩晋升窗口，覆盖缓存状态变化前后。
		constexpr unsigned StabilityFrames = 9;

		// 一幅读回结果：预乘 BGRA8、自上而下、无行间填充。
		struct Frame
		{
			UINT32 width = 0, height = 0;
			std::vector<BYTE> bytes;
			[[nodiscard]] bool Valid() const noexcept
			{ return width && height && bytes.size() == static_cast<size_t>(width) * height * 4; }
		};
		// 现有 GDI 兼容路径一次提交的读回：ULW 源裁剪的像素与几何。
		struct GdiCapture
		{
			Frame frame;
			POINT source{};       // 源裁剪在主栏帧目标位图中的左上角
			POINT destination{};  // 提交到屏幕的左上角
			unsigned serial = 0;  // 第几次读回，用于确认每帧都有新的真实提交
		};
		// 重放回调里只在回调期间有效的闭包不能留存，这里只留几何。
		struct ReplayGeometry
		{
			FrameCoordinateMapping mapping;
			RECT viewport{};
			RECT contentBounds{};
			D2D1_SIZE_U targetSize{};
		};

		struct Report
		{
			std::ostringstream text;
			unsigned checks = 0, failures = 0;
			void Check(bool ok, const std::string& name, const std::string& why = {})
			{
				++checks;
				if (ok) { text << "OK " << name << '\n'; return; }
				++failures;
				text << "FAIL " << name << ' ' << (why.empty() ? "check failed" : why) << '\n';
				Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::Bar,
					"Host offscreen check {}: {}", name, why.empty() ? "check failed" : why);
			}
		};

		// 当前线程名下的顶层窗口与 message-only 窗口数。测试的全部绘制都在本线程同步执行，
		// 被测代码若创建了窗口必然归属本线程；按线程统计可避开其他注入线程的无关窗口。
		[[nodiscard]] unsigned CountThreadWindows() noexcept
		{
			unsigned count = 0;
			const DWORD thread = GetCurrentThreadId();
			EnumThreadWindows(thread, [](HWND, LPARAM value) -> BOOL
			{ ++*reinterpret_cast<unsigned*>(value); return TRUE; }, reinterpret_cast<LPARAM>(&count));
			for (HWND window = nullptr; (window = FindWindowExW(HWND_MESSAGE, window, nullptr, nullptr)) != nullptr;)
				if (GetWindowThreadProcessId(window, nullptr) == thread) ++count;
			return count;
		}

		// 位图读回：CPU_READ | CANNOT_DRAW 副本 CopyFromBitmap 后 Map；必须在 EndDraw 之后调用。
		[[nodiscard]] bool ReadBitmap(ID2D1DeviceContext* context, ID2D1Bitmap1* bitmap, Frame& out)
		{
			out = {};
			if (!context || !bitmap) return false;
			const auto size = bitmap->GetPixelSize();
			const auto format = bitmap->GetPixelFormat();
			if (!size.width || !size.height || format.format != DXGI_FORMAT_B8G8R8A8_UNORM
				|| format.alphaMode != D2D1_ALPHA_MODE_PREMULTIPLIED) return false;
			ComPtr<ID2D1Bitmap1> readable;
			const auto properties = D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format);
			if (FAILED(context->CreateBitmap(size, nullptr, 0, &properties, &readable))
				|| FAILED(readable->CopyFromBitmap(nullptr, bitmap, nullptr))) return false;
			D2D1_MAPPED_RECT mapped{};
			if (FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return false;
			const size_t stride = static_cast<size_t>(size.width) * 4;
			const bool valid = mapped.bits && mapped.pitch >= stride;
			if (valid)
			{
				out.bytes.resize(stride * size.height);
				for (UINT32 row = 0; row < size.height; ++row)
					std::memcpy(out.bytes.data() + stride * row,
						mapped.bits + static_cast<size_t>(mapped.pitch) * row, stride);
			}
			const HRESULT unmap = readable->Unmap();
			if (!valid || FAILED(unmap)) { out = {}; return false; }
			out.width = size.width; out.height = size.height;
			return true;
		}

		// 逐字节比较两幅像素；完全相同返回空串，否则给出尺寸差异或首个不同像素与不同像素总数。不设容差。
		[[nodiscard]] std::string Difference(const Frame& a, const Frame& b)
		{
			std::ostringstream text;
			text.imbue(std::locale::classic());
			if (!a.Valid() || !b.Valid() || a.width != b.width || a.height != b.height)
			{
				text << "size " << a.width << 'x' << a.height << '(' << a.bytes.size() << " bytes) vs "
					<< b.width << 'x' << b.height << '(' << b.bytes.size() << " bytes)";
				return text.str();
			}
			if (a.bytes == b.bytes) return {};
			size_t differing = 0, first = 0;
			const size_t total = a.bytes.size() / 4;
			for (size_t pixel = 0; pixel < total; ++pixel)
				if (std::memcmp(a.bytes.data() + pixel * 4, b.bytes.data() + pixel * 4, 4) != 0)
					if (differing++ == 0) first = pixel;
			const auto value = [&](const Frame& frame)
			{
				const BYTE* p = frame.bytes.data() + first * 4;
				text << '(' << int(p[0]) << ',' << int(p[1]) << ',' << int(p[2]) << ',' << int(p[3]) << ')';
			};
			text << "first=(" << first % a.width << ',' << first / a.width << ") bgra="; value(a);
			text << " vs "; value(b);
			text << " differing=" << differing << '/' << total;
			return text.str();
		}

		[[nodiscard]] bool Crop(const Frame& frame, POINT origin, SIZE size, Frame& out)
		{
			out = {};
			if (!frame.Valid() || origin.x < 0 || origin.y < 0 || size.cx <= 0 || size.cy <= 0
				|| static_cast<std::uint64_t>(origin.x) + size.cx > frame.width
				|| static_cast<std::uint64_t>(origin.y) + size.cy > frame.height) return false;
			out.width = static_cast<UINT32>(size.cx); out.height = static_cast<UINT32>(size.cy);
			out.bytes.resize(static_cast<size_t>(size.cx) * size.cy * 4);
			for (LONG row = 0; row < size.cy; ++row)
				std::memcpy(out.bytes.data() + static_cast<size_t>(row) * size.cx * 4,
					frame.bytes.data() + (static_cast<size_t>(origin.y + row) * frame.width + origin.x) * 4,
					static_cast<size_t>(size.cx) * 4);
			return true;
		}

		// 失败时把原始预乘 BGRA 留在输出目录，文件名带尺寸，便于离线对照。
		void Dump(const std::filesystem::path& directory, const std::string& name, const Frame& frame)
		{
			if (!frame.Valid()) return;
			std::ofstream raw(directory / (name + '.' + std::to_string(frame.width) + 'x'
				+ std::to_string(frame.height) + ".bgra"), std::ios::binary | std::ios::trunc);
			raw.write(reinterpret_cast<const char*>(frame.bytes.data()), static_cast<std::streamsize>(frame.bytes.size()));
		}

		// 测试自建的第二宿主：自己的内容绘制器 + 自己的帧目标，不借用主栏的任何目标资源。
		class SecondHost
		{
		public:
			SecondHost(BarUISetClass& owner, FrameTargetKind kind, bool externalClip)
				: renderer_(Services(owner, resets_)), target_(renderer_, kind), kind_(kind), externalClip_(externalClip)
			{
				// 与主栏对自己绘制器做的初始化相同（BarUISetClass::ConfigureLocalizedTypography）。
				renderer_.ConfigureLocalizedTypography();
				target_.SetExternalDirtyClipEnabled(externalClip);
			}
			SecondHost(const SecondHost&) = delete;
			SecondHost& operator=(const SecondHost&) = delete;
			~SecondHost() { target_.Discard(); }

			// 把重放桥交来的同一帧内容画进自己的目标并读回；宿主自己负责帧的开始、结束与提交。
			bool Render(const BarMainBarContentReplay& replay, const Pipeline::DeviceEpoch& epoch)
			{
				pixels_ = {}; failure_.clear(); result_ = {};
				const bool ok = kind_ == FrameTargetKind::ExternalContext
					? RenderExternal(replay, epoch) : RenderBitmap(replay, epoch);
				if (!ok && failure_.empty()) failure_ = "render failed";
				if (ok) ++frames_;
				return ok;
			}
			void DiscardTarget() { target_.Discard(); }

			[[nodiscard]] UIRendering& Renderer() noexcept { return renderer_; }
			[[nodiscard]] FrameTarget& Target() noexcept { return target_; }
			[[nodiscard]] const Frame& Pixels() const noexcept { return pixels_; }
			[[nodiscard]] const std::string& Failure() const noexcept { return failure_; }
			[[nodiscard]] const FrameTransactionResult& Result() const noexcept { return result_; }
			[[nodiscard]] unsigned Resets() const noexcept { return resets_; }
			[[nodiscard]] unsigned Frames() const noexcept { return frames_; }
			[[nodiscard]] unsigned Recreations() const noexcept { return recreations_; }
			[[nodiscard]] ID2D1Bitmap1* ExternalBitmap() const noexcept { return externalBitmap_.Get(); }

		private:
			// 与主栏绘制器相同的服务（排版缓存、本地化排版、设备缓存清理），只在清理回调上加计数。
			static UIRenderingServices Services(BarUISetClass& owner, unsigned& resets)
			{
				auto services = MakeMainBarRenderingServices(&owner);
				auto forward = std::move(services.resetDeviceCaches);
				services.resetDeviceCaches = [&resets, forward = std::move(forward)]
				{
					++resets;
					if (forward) forward();
				};
				return services;
			}
			[[nodiscard]] static D2D1_RECT_F ViewportRect(const BarMainBarContentReplay& replay) noexcept
			{
				// 与生产路径的脏区同一坐标系：基础变换（只有平移）下的缩放后布局像素。本宿主每帧整幅重画视口。
				return D2D1::RectF(static_cast<FLOAT>(replay.viewport.left), static_cast<FLOAT>(replay.viewport.top),
					static_cast<FLOAT>(replay.viewport.right), static_cast<FLOAT>(replay.viewport.bottom));
			}
			[[nodiscard]] bool Fail(const char* step, HRESULT result)
			{
				std::ostringstream text;
				text << step << " hr=0x" << std::hex << static_cast<unsigned long>(result);
				failure_ = text.str();
				return false;
			}
			// 新建的位图内容未定义：宿主先整幅清空一次，之后每帧只重画视口。
			[[nodiscard]] static HRESULT ClearWhole(ID2D1DeviceContext* context)
			{
				const auto transparent = D2D1::ColorF(0, 0.0F);
				context->BeginDraw();
				context->SetTransform(D2D1::Matrix3x2F::Identity());
				context->Clear(&transparent);
				return context->EndDraw();
			}

			// 普通位图形态：帧目标自建不带 GDI 兼容的位图；帧事务结束绘制后把目标位图交给读回提交端。
			bool RenderBitmap(const BarMainBarContentReplay& replay, const Pipeline::DeviceEpoch& epoch)
			{
				const auto size = target_.PixelSize();
				const bool recreate = !target_.TargetBitmap() || target_.DeviceGeneration() != epoch.generation
					|| size.width != replay.targetSize.width || size.height != replay.targetSize.height;
				HRESULT hr = target_.Ensure(epoch, replay.targetSize.width, replay.targetSize.height);
				if (FAILED(hr)) return Fail("Ensure", hr);
				auto* context = target_.DeviceContext();
				if (!context || !target_.TargetBitmap()) return Fail("Ensure produced no target", E_POINTER);
				if (recreate)
				{
					++recreations_;
					hr = ClearWhole(context);
					if (FAILED(hr)) return Fail("initial clear", hr);
				}
				replay.prepare(renderer_, context, replay.mapping);
				// 新建绘制器的字号帧只能来自重放桥的同步；漏同步在静止像素上看不出来，这里直接断言。
				if (!renderer_.IsFontFrameReady()) return Fail("font frame not synchronized", E_UNEXPECTED);
				FrameTransaction transaction(target_, ViewportRect(replay));
				if (!transaction.Active() || transaction.DeviceContext() != context)
					return Fail("transaction inactive", E_UNEXPECTED);
				replay.draw(renderer_, transaction.DeviceContext(), replay.mapping);
				bool sourceValid = false;
				result_ = transaction.Commit([&](const FramePresentSource& source) -> FramePresentOutcome
				{
					sourceValid = !source.sourceDc && source.targetBitmap == target_.TargetBitmap()
						&& source.pixelSize.width == replay.targetSize.width
						&& source.pixelSize.height == replay.targetSize.height;
					const bool read = sourceValid && ReadBitmap(context, source.targetBitmap, pixels_);
					return { read, read ? static_cast<DWORD>(ERROR_SUCCESS) : static_cast<DWORD>(ERROR_INVALID_DATA) };
				});
				if (!sourceValid) return Fail("present source", E_UNEXPECTED);
				// 没有 GDI interop 的形态：GDI 两步记为成功且不执行；EndDraw 由帧事务执行。
				if (!result_.commitRequested || result_.getDc != S_OK || result_.releaseDc != S_OK
					|| result_.releaseDcAttempted || !result_.presentAttempted || !result_.presentSucceeded
					|| FAILED(result_.endDraw) || !result_.PixelsCommitted())
					return Fail("transaction result", FAILED(result_.endDraw) ? result_.endDraw : E_FAIL);
				if (transaction.Active() || target_.DirtyClipActive()) return Fail("transaction not finished", E_UNEXPECTED);
				return pixels_.Valid();
			}

			// 外部上下文形态：设备上下文与位图都由宿主创建，BeginDraw/清空/EndDraw 由宿主执行，
			// 帧目标只借用上下文并维护绘制器的帧状态。
			bool RenderExternal(const BarMainBarContentReplay& replay, const Pipeline::DeviceEpoch& epoch)
			{
				HRESULT hr = S_OK;
				if (!externalContext_ || externalGeneration_ != epoch.generation)
				{
					if (!epoch.d2dDevice) return Fail("D2DDevice", E_POINTER);
					externalBitmap_.Reset(); externalContext_.Reset();
					hr = epoch.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &externalContext_);
					if (FAILED(hr)) return Fail("CreateDeviceContext", hr);
					externalContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
					externalGeneration_ = epoch.generation;
				}
				const auto size = externalBitmap_ ? externalBitmap_->GetPixelSize() : D2D1::SizeU();
				if (!externalBitmap_ || size.width != replay.targetSize.width || size.height != replay.targetSize.height)
				{
					externalContext_->SetTarget(nullptr);
					externalBitmap_.Reset();
					const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
						D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
					hr = externalContext_->CreateBitmap(replay.targetSize, nullptr, 0, &properties, &externalBitmap_);
					if (FAILED(hr)) return Fail("CreateBitmap", hr);
					externalContext_->SetTarget(externalBitmap_.Get());
					++recreations_;
				}
				hr = target_.AttachExternalContext(externalContext_.Get(), epoch.generation,
					replay.targetSize.width, replay.targetSize.height);
				if (FAILED(hr)) return Fail("AttachExternalContext", hr);
				if (target_.DeviceContext() != externalContext_.Get() || target_.TargetBitmap()
					|| target_.GdiInteropRenderTarget()) return Fail("external target state", E_UNEXPECTED);

				externalContext_->BeginDraw();
				// 帧目标在这种形态下不清空：宿主自己整幅清空。
				const auto transparent = D2D1::ColorF(0, 0.0F);
				externalContext_->SetTransform(D2D1::Matrix3x2F::Identity());
				externalContext_->Clear(&transparent);
				replay.prepare(renderer_, externalContext_.Get(), replay.mapping);
				const bool fontFrameReady = renderer_.IsFontFrameReady();
				// 帧目标不推脏区裁剪时由宿主自己裁到视口：内容的柔和阴影会画到视口之外，
				// 必须由宿主或帧目标之一提供裁剪。
				if (!externalClip_)
					externalContext_->PushAxisAlignedClip(ViewportRect(replay), D2D1_ANTIALIAS_MODE_ALIASED);
				bool transactionValid = false;
				{
					FrameTransaction transaction(target_, ViewportRect(replay));
					transactionValid = transaction.Active() && transaction.DeviceContext() == externalContext_.Get()
						&& target_.DirtyClipActive() == externalClip_;
					if (transactionValid)
					{
						replay.draw(renderer_, transaction.DeviceContext(), replay.mapping);
						// 宿主自行呈现：空提交端；事务不替宿主 EndDraw。
						result_ = transaction.Commit({});
						transactionValid = !transaction.Active();
					}
				}
				if (!externalClip_) externalContext_->PopAxisAlignedClip();
				const HRESULT endDraw = externalContext_->EndDraw();
				target_.NotifyExternalEndDraw(endDraw);
				if (!fontFrameReady) return Fail("font frame not synchronized", E_UNEXPECTED);
				if (!transactionValid) return Fail("transaction inactive", E_UNEXPECTED);
				if (FAILED(endDraw)) return Fail("EndDraw", endDraw);
				if (!result_.commitRequested || result_.getDc != S_OK || result_.releaseDc != S_OK
					|| result_.releaseDcAttempted || result_.presentAttempted || !result_.presentSucceeded
					|| result_.endDraw != S_OK || !result_.PixelsCommitted())
					return Fail("transaction result", E_FAIL);
				if (target_.DirtyClipActive()) return Fail("clip left active", E_UNEXPECTED);
				return ReadBitmap(externalContext_.Get(), externalBitmap_.Get(), pixels_) || Fail("readback", E_FAIL);
			}

			unsigned resets_ = 0;       // resetDeviceCaches 服务被调用的次数
			unsigned frames_ = 0;       // 成功画完并读回的帧数
			unsigned recreations_ = 0;  // 目标位图（重新）创建的次数
			UIRendering renderer_;
			FrameTarget target_;
			FrameTargetKind kind_;
			bool externalClip_;         // 外部上下文形态下由帧目标（true）还是宿主（false）裁到视口
			ComPtr<ID2D1DeviceContext> externalContext_;
			ComPtr<ID2D1Bitmap1> externalBitmap_;
			std::uint64_t externalGeneration_ = 0;
			Frame pixels_;
			FrameTransactionResult result_;
			std::string failure_;
		};

		// 帧事务的异常收尾：提交端或分步观察回调抛出异常时，事务仍须先 ReleaseDC、再 EndDraw（各一次），
		// 否则设备上下文停在「已 BeginDraw / DC 未归还」状态，下一帧无法开始。用与主栏相同的 GDI 兼容形态验证：
		// 每次抛出之后紧接着做一次完整提交，四步必须全部成功。绘制器不带服务，不触碰主栏的任何缓存。
		void CheckTransactionUnwind(Report& report)
		{
			UIRendering renderer;
			FrameTarget target(renderer, FrameTargetKind::GdiCompatibleBitmap);
			const D2D1_RECT_F whole = D2D1::RectF(0.0F, 0.0F, 64.0F, 64.0F);
			const HRESULT ensured = target.Ensure(Pipeline::GetDeviceEpoch(), 64, 64);
			report.Check(SUCCEEDED(ensured) && target.GdiInteropRenderTarget(), "transaction-unwind.target",
				"GDI compatible frame target was not created");
			if (FAILED(ensured) || !target.GdiInteropRenderTarget()) return;
			const auto commit = [&](const FramePresentSink& sink, const FrameCommitStepObserver& observer,
				FrameTransactionResult& result)
			{
				FrameTransaction transaction(target, whole);
				if (!transaction.Active()) return false;
				result = transaction.Commit(sink, observer);
				return !transaction.Active();
			};
			const FramePresentSink readOnly = [](const FramePresentSource& source) -> FramePresentOutcome
			{ return { source.sourceDc != nullptr, static_cast<DWORD>(ERROR_SUCCESS) }; };
			const auto healthy = [&]
			{
				FrameTransactionResult result;
				return commit(readOnly, {}, result) && result.getDc == S_OK && result.presentAttempted
					&& result.releaseDcAttempted && result.releaseDc == S_OK && result.endDraw == S_OK
					&& result.PixelsCommitted() && !target.DirtyClipActive();
			};
			report.Check(healthy(), "transaction-unwind.baseline", "plain commit failed before any exception");
			struct Case { const char* name; bool throwInSink; FrameCommitStep step; };
			const Case cases[]{
				{"transaction-unwind.sink-throws", true, FrameCommitStep::GetDC},
				{"transaction-unwind.observer-throws-before-get-dc", false, FrameCommitStep::GetDC},
				{"transaction-unwind.observer-throws-before-release-dc", false, FrameCommitStep::ReleaseDC},
				{"transaction-unwind.observer-throws-before-end-draw", false, FrameCommitStep::EndDraw},
			};
			for (const auto& item : cases)
			{
				bool thrown = false;
				try
				{
					FrameTransactionResult ignored;
					const FramePresentSink sink = item.throwInSink
						? FramePresentSink([](const FramePresentSource&) -> FramePresentOutcome { throw std::runtime_error("sink"); })
						: readOnly;
					const FrameCommitStepObserver observer = item.throwInSink ? FrameCommitStepObserver{}
						: FrameCommitStepObserver([&](FrameCommitStep step, bool begin)
						{ if (begin && step == item.step) throw std::runtime_error("observer"); });
					(void)commit(sink, observer, ignored);
				}
				catch (const std::runtime_error&) { thrown = true; }
				// 异常必须传到调用方，且之后的一帧照常完成。
				const bool recovered = healthy();
				report.Check(thrown && recovered, item.name,
					!thrown ? "exception did not reach the caller" : "next frame could not begin, get the DC or commit");
			}
			target.Discard();
		}

		void RunScene(const HostScene& scene, Report& report, const std::filesystem::path& directory)
		{
			auto& owner = barUISet;
			const std::string prefix = std::string(scene.name) + '.';
			const auto check = [&](bool ok, const char* name, const std::string& why = {})
			{ report.Check(ok, prefix + name, why); };
			const auto dump = [&](const char* name, const Frame& frame) { Dump(directory, prefix + name, frame); };
			unsigned windowViolations = floating_window != nullptr ? 1u : 0u;
			const unsigned windowsBefore = CountThreadWindows();

			// —— 基准：不接重放回调，只走现有 GDI 兼容路径（与像素基线入口完全相同的流程）。
			GdiCapture reference;
			{
				unsigned captures = 0;
				BarGoldenMainSceneHost host;
				host.captured = [&](const UPDATELAYEREDWINDOWINFO& info, const std::vector<BYTE>& bytes)
				{
					reference.frame.width = static_cast<UINT32>(info.psize->cx);
					reference.frame.height = static_cast<UINT32>(info.psize->cy);
					reference.frame.bytes = bytes;
					reference.source = *info.pptSrc; reference.destination = *info.pptDst;
					reference.serial = ++captures;
				};
				host.settled = [&](const std::function<bool()>& renderFrame) { return renderFrame(); };
				const HRESULT hr = RunGoldenMainSceneForHost(scene.drawAttribute, scene.dark, scene.scale, host);
				check(hr == S_OK && reference.frame.Valid(), "reference-scene",
					hr == S_FALSE ? "fixed-frame animation did not settle" : "scene setup, submit or BGRA readback failed");
				if (hr != S_OK || !reference.frame.Valid()) return;
			}
			if (floating_window != nullptr) ++windowViolations;

			// —— 同一场景再搭一遍并接上重放回调：现有路径照常提交，每帧提交成功后把同一帧内容交给三个第二宿主。
			GdiCapture gdi;
			unsigned captures = 0, replayCalls = 0, replayedFrames = 0;
			bool armed = false, hostsRendered = true;
			ReplayGeometry geometry;
			std::optional<SecondHost> plain, external, externalNoClip;
			const auto hosts = [&] { return std::array<SecondHost*, 3>{ &*plain, &*external, &*externalNoClip }; };
			bool phasesReached = false;

			BarGoldenMainSceneHost host;
			host.captured = [&](const UPDATELAYEREDWINDOWINFO& info, const std::vector<BYTE>& bytes)
			{
				gdi.frame.width = static_cast<UINT32>(info.psize->cx);
				gdi.frame.height = static_cast<UINT32>(info.psize->cy);
				gdi.frame.bytes = bytes;
				gdi.source = *info.pptSrc; gdi.destination = *info.pptDst;
				gdi.serial = ++captures;
			};
			host.replay = [&](const BarMainBarContentReplay& replay)
			{
				++replayCalls;
				if (floating_window != nullptr) ++windowViolations;
				if (!armed) return;
				geometry = { replay.mapping, replay.viewport, replay.contentBounds, replay.targetSize };
				const auto epoch = Pipeline::GetDeviceEpoch();
				for (auto* second : hosts()) hostsRendered = second->Render(replay, epoch) && hostsRendered;
				++replayedFrames;
			};
			host.settled = [&](const std::function<bool()>& renderFrame)
			{
				phasesReached = true;
				// 第二宿主在场景静止后才建立：三个各有自己的新绘制器与帧目标。
				plain.emplace(owner, FrameTargetKind::PlainBitmap, true);
				external.emplace(owner, FrameTargetKind::ExternalContext, true);
				externalNoClip.emplace(owner, FrameTargetKind::ExternalContext, false);
				armed = true;

				// ① 第一帧：现有路径提交一次，三个第二宿主各画一次。
				const unsigned gdiSerialBefore = gdi.serial;
				const bool rendered = renderFrame();
				const GdiCapture gdiFirst = gdi;
				const Frame plainFirst = plain->Pixels();
				const Frame externalFirst = external->Pixels();
				const Frame externalNoClipFirst = externalNoClip->Pixels();
				const ReplayGeometry first = geometry;
				const auto epoch = Pipeline::GetDeviceEpoch();

				// 检查 1：第二宿主（普通位图形态 + 新绘制器）经内容绘制入口画入。
				{
					std::ostringstream why;
					auto* mainBitmap = owner.specTarget.TargetBitmap();
					auto* plainBitmap = plain->Target().TargetBitmap();
					const bool distinct = &plain->Renderer() != &owner.spec
						&& plain->Target().BoundRenderer() == &plain->Renderer()
						&& owner.specTarget.BoundRenderer() == &owner.spec
						&& plainBitmap && mainBitmap && plainBitmap != mainBitmap
						&& plain->Target().DeviceContext() && plain->Target().DeviceContext() != owner.specTarget.DeviceContext()
						&& plain->Renderer().GetDeviceContext() == plain->Target().DeviceContext();
					// 两条路径的目标形态确实不同：主栏带 GDI 兼容与 interop，第二宿主两者都没有。
					const bool forms = plain->Target().Kind() == FrameTargetKind::PlainBitmap
						&& !plain->Target().GdiInteropRenderTarget()
						&& plainBitmap && !(plainBitmap->GetOptions() & D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE)
						&& (plainBitmap->GetOptions() & D2D1_BITMAP_OPTIONS_TARGET)
						&& owner.specTarget.Kind() == FrameTargetKind::GdiCompatibleBitmap
						&& owner.specTarget.GdiInteropRenderTarget()
						&& mainBitmap && (mainBitmap->GetOptions() & D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE);
					const bool drawn = rendered && replayedFrames == 1 && gdiFirst.serial == gdiSerialBefore + 1
						&& plain->Frames() == 1 && plain->Recreations() == 1 && plainFirst.Valid()
						&& plainFirst.width == first.targetSize.width && plainFirst.height == first.targetSize.height
						&& plain->Renderer().GetDeviceGeneration() == epoch.generation;
					if (!rendered) why << "production frame was not committed; ";
					if (!plain->Failure().empty()) why << "second host: " << plain->Failure() << "; ";
					if (!distinct) why << "second host shares renderer, context or target with the main bar; ";
					if (!forms) why << "unexpected frame target forms; ";
					if (!drawn) why << "replayed=" << replayedFrames << " frames=" << plain->Frames()
						<< " recreations=" << plain->Recreations() << " size=" << plainFirst.width << 'x' << plainFirst.height;
					check(distinct && forms && drawn && plain->Failure().empty(), "second-host-plain-bitmap", why.str());
				}

				// 坐标映射自洽：现有路径提交到屏幕的位置经映射换回目标像素，应正好是它的 ULW 源裁剪起点。
				// （映射与 ULW 参数由宿主各自算出，这里交叉核对，之后用它定位视口。）
				const POINT viewportTarget = ScreenToTarget(first.mapping, gdiFirst.destination);
				const SIZE viewportSize{ first.viewport.right - first.viewport.left, first.viewport.bottom - first.viewport.top };
				{
					std::ostringstream why;
					const bool ok = viewportTarget.x == gdiFirst.source.x && viewportTarget.y == gdiFirst.source.y
						&& static_cast<LONG>(gdiFirst.frame.width) == viewportSize.cx
						&& static_cast<LONG>(gdiFirst.frame.height) == viewportSize.cy
						&& first.viewport.left - first.mapping.targetOrigin.x == gdiFirst.source.x
						&& first.viewport.top - first.mapping.targetOrigin.y == gdiFirst.source.y
						&& first.mapping.layoutScale == scene.scale && first.mapping.lightingOutset == 0
						&& first.targetSize.width == owner.specTarget.PixelSize().width
						&& first.targetSize.height == owner.specTarget.PixelSize().height;
					why << "mapped=(" << viewportTarget.x << ',' << viewportTarget.y << ") source=(" << gdiFirst.source.x << ','
						<< gdiFirst.source.y << ") viewport=" << viewportSize.cx << 'x' << viewportSize.cy << " gdi="
						<< gdiFirst.frame.width << 'x' << gdiFirst.frame.height << " scale=" << first.mapping.layoutScale;
					check(ok, "coordinate-mapping", why.str());
				}

				// 检查 2：与现有 GDI 兼容路径逐字节一致。现有路径的一帧就是它提交给窗口的 ULW 源裁剪（全幅）；
				// 第二宿主目标里视口以外的部分由检查 3 要求全透明，两者合起来覆盖第二宿主目标的每个像素。
				{
					Frame plainViewport;
					const bool cropped = Crop(plainFirst, gdiFirst.source,
						SIZE{ static_cast<LONG>(gdiFirst.frame.width), static_cast<LONG>(gdiFirst.frame.height) }, plainViewport);
					const std::string difference = cropped ? Difference(plainViewport, gdiFirst.frame)
						: std::string("viewport is outside the second host target");
					if (!difference.empty()) { dump("plain", plainFirst); dump("gdi", gdiFirst.frame); }
					check(difference.empty(), "matches-gdi-path", difference);
				}
				// 现有路径没有被重放扰动：接了重放回调的这次提交与不接回调的基准逐字节相同。
				{
					std::string difference = Difference(gdiFirst.frame, reference.frame);
					if (difference.empty() && (gdiFirst.source.x != reference.source.x || gdiFirst.source.y != reference.source.y
						|| gdiFirst.destination.x != reference.destination.x || gdiFirst.destination.y != reference.destination.y))
						difference = "ULW source or destination moved";
					if (!difference.empty()) { dump("gdi-with-replay", gdiFirst.frame); dump("gdi-reference", reference.frame); }
					check(difference.empty(), "gdi-path-undisturbed", difference);
				}

				// 检查 3：范围。预期矩形 = 已呈现内容范围：本帧内容的视觉外框（宿主在绘制前按布局算出，与读回像素无关）
				// 与现有路径实际提交到屏幕的窗口范围之交（内容超出显示器的部分不会被呈现），经坐标映射换到目标像素。
				// 主栏没有统一的光效外扩（lightingOutset 恒为 0），ExpandByLightingOutset 在这里不改变矩形。
				{
					const RECT contentScreen{
						first.contentBounds.left + first.mapping.screenOrigin.x, first.contentBounds.top + first.mapping.screenOrigin.y,
						first.contentBounds.right + first.mapping.screenOrigin.x, first.contentBounds.bottom + first.mapping.screenOrigin.y };
					const RECT presentedScreen{ gdiFirst.destination.x, gdiFirst.destination.y,
						gdiFirst.destination.x + static_cast<LONG>(gdiFirst.frame.width),
						gdiFirst.destination.y + static_cast<LONG>(gdiFirst.frame.height) };
					const POINT topLeft = ScreenToTarget(first.mapping, POINT{
						(std::max)(contentScreen.left, presentedScreen.left), (std::max)(contentScreen.top, presentedScreen.top) });
					const POINT bottomRight = ScreenToTarget(first.mapping, POINT{
						(std::min)(contentScreen.right, presentedScreen.right), (std::min)(contentScreen.bottom, presentedScreen.bottom) });
					const RECT expected = ExpandByLightingOutset(first.mapping, RECT{ topLeft.x, topLeft.y, bottomRight.x, bottomRight.y });
					const bool contained = expected.left >= 0 && expected.top >= 0 && expected.right > expected.left
						&& expected.bottom > expected.top && plainFirst.Valid()
						&& expected.right <= static_cast<LONG>(plainFirst.width) && expected.bottom <= static_cast<LONG>(plainFirst.height);
					size_t outside = 0, inside = 0;
					RECT visible{ LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
					if (plainFirst.Valid())
						for (LONG y = 0; y < static_cast<LONG>(plainFirst.height); ++y)
							for (LONG x = 0; x < static_cast<LONG>(plainFirst.width); ++x)
							{
								const BYTE* pixel = plainFirst.bytes.data() + (static_cast<size_t>(y) * plainFirst.width + x) * 4;
								// 预乘像素全透明即四个字节全为 0。
								if (!(pixel[0] | pixel[1] | pixel[2] | pixel[3])) continue;
								if (x < expected.left || x >= expected.right || y < expected.top || y >= expected.bottom) { ++outside; continue; }
								++inside;
								visible.left = (std::min)(visible.left, x); visible.top = (std::min)(visible.top, y);
								visible.right = (std::max)(visible.right, x + 1); visible.bottom = (std::max)(visible.bottom, y + 1);
							}
					// 矩形内不只是「有像素」：可见像素的外接框至少占预期矩形宽高的一半，排除只画出零星内容。
					const bool filled = inside > 0
						&& (visible.right - visible.left) * 2 >= expected.right - expected.left
						&& (visible.bottom - visible.top) * 2 >= expected.bottom - expected.top;
					std::ostringstream why;
					why << "expected=[" << expected.left << ',' << expected.top << ',' << expected.right << ',' << expected.bottom
						<< "] content=[" << first.contentBounds.left - first.mapping.targetOrigin.x << ','
						<< first.contentBounds.top - first.mapping.targetOrigin.y << ','
						<< first.contentBounds.right - first.mapping.targetOrigin.x << ','
						<< first.contentBounds.bottom - first.mapping.targetOrigin.y
						<< "] viewport=[" << viewportTarget.x << ',' << viewportTarget.y << ',' << viewportTarget.x + viewportSize.cx << ','
						<< viewportTarget.y + viewportSize.cy
						<< "] target=" << plainFirst.width << 'x' << plainFirst.height << " inside=" << inside << " outside=" << outside;
					if (inside) why << " visible=[" << visible.left << ',' << visible.top << ',' << visible.right << ',' << visible.bottom << ']';
					report.text << "INFO " << prefix << "content-range " << why.str() << '\n';
					if (!(contained && filled && outside == 0)) dump("plain-range", plainFirst);
					check(contained && filled && outside == 0, "content-range", why.str());
				}

				// 检查 4：外部上下文形态。宿主自建上下文与位图、自行 BeginDraw/EndDraw；结果与检查 1 的整幅目标逐字节相同。
				{
					std::string difference = external->Failure().empty() && external->Frames() == 1
						? Difference(externalFirst, plainFirst) : "external host: " + external->Failure();
					const bool separate = &external->Renderer() != &owner.spec && &external->Renderer() != &plain->Renderer()
						&& external->ExternalBitmap() && external->ExternalBitmap() != plain->Target().TargetBitmap()
						&& external->Target().DeviceContext() != plain->Target().DeviceContext()
						&& external->Target().Kind() == FrameTargetKind::ExternalContext;
					if (difference.empty() && !separate) difference = "external host shares renderer, context or target";
					if (!difference.empty()) { dump("external", externalFirst); dump("plain", plainFirst); }
					check(difference.empty(), "external-context", difference);
				}
				{
					// 同一形态、帧目标不推脏区裁剪（宿主自己裁到视口，绘制器不做脏区剔除）：结果仍须相同。
					std::string difference = externalNoClip->Failure().empty() && externalNoClip->Frames() == 1
						? Difference(externalNoClipFirst, plainFirst) : "external host: " + externalNoClip->Failure();
					if (!difference.empty()) { dump("external-noclip", externalNoClipFirst); dump("plain", plainFirst); }
					check(difference.empty(), "external-context-without-clip", difference);
				}

				// 连续多帧：两个绘制器交替使用同一批图元，结果不得漂移（字号修正、位图缓存、遮罩晋升）。
				{
					bool allRendered = true;
					for (unsigned frame = 0; frame < StabilityFrames; ++frame) allRendered = renderFrame() && allRendered;
					std::string difference;
					if (!allRendered || !hostsRendered || replayedFrames != 1 + StabilityFrames)
						difference = "a frame was not committed or replayed";
					if (difference.empty()) difference = Difference(plain->Pixels(), plainFirst);
					if (difference.empty()) difference = Difference(gdi.frame, gdiFirst.frame);
					if (difference.empty()) difference = Difference(external->Pixels(), plainFirst);
					if (difference.empty()) difference = Difference(externalNoClip->Pixels(), plainFirst);
					if (!difference.empty()) { dump("plain-later", plain->Pixels()); dump("gdi-later", gdi.frame); }
					check(difference.empty(), "stable-across-frames", difference);
				}

				// 检查 5：设备缓存归属。丢弃第二宿主的帧目标：它的绘制器必须清掉设备缓存（服务回调计数）并放下上下文；
				// 重建后再画一次，与丢弃前逐字节相同。旧位图与旧上下文在比较期间保持存活，新对象的地址不可能与之重合。
				{
					const Frame before = plain->Pixels();
					const unsigned resetsBefore = plain->Resets();
					const unsigned recreationsBefore = plain->Recreations();
					const ComPtr<ID2D1Bitmap1> oldBitmap = plain->Target().TargetBitmap();
					const ComPtr<ID2D1DeviceContext> oldContext = plain->Target().DeviceContext();
					plain->DiscardTarget();
					const unsigned resetsAfterDiscard = plain->Resets();
					const bool released = resetsAfterDiscard == resetsBefore + 1
						&& !plain->Renderer().GetDeviceContext() && plain->Renderer().GetDeviceGeneration() == 0
						&& !plain->Target().TargetBitmap() && !plain->Target().DeviceContext()
						&& plain->Target().DeviceGeneration() == 0;
					const unsigned framesBefore = plain->Frames();
					const bool rerendered = renderFrame() && plain->Frames() == framesBefore + 1;
					const bool rebuilt = plain->Recreations() == recreationsBefore + 1
						&& plain->Target().TargetBitmap() && plain->Target().TargetBitmap() != oldBitmap.Get()
						&& plain->Target().DeviceContext() && plain->Target().DeviceContext() != oldContext.Get()
						&& plain->Renderer().GetDeviceContext() == plain->Target().DeviceContext()
						&& plain->Renderer().GetDeviceGeneration() == Pipeline::GetDeviceEpoch().generation
						&& plain->Resets() > resetsBefore;
					std::ostringstream why;
					why << "resets " << resetsBefore << "->" << resetsAfterDiscard << "->" << plain->Resets()
						<< " recreations " << recreationsBefore << "->" << plain->Recreations();
					if (!plain->Failure().empty()) why << " second host: " << plain->Failure();
					report.text << "INFO " << prefix << "device-cache-reset " << why.str() << '\n';
					check(released && rerendered && rebuilt, "device-cache-reset", why.str());

					std::string difference = rerendered ? Difference(plain->Pixels(), before) : std::string("frame was not redrawn");
					if (!difference.empty()) { dump("plain-recreated", plain->Pixels()); dump("plain-before-discard", before); }
					check(difference.empty(), "recreated-target-matches", difference);

					// 第二宿主的清理经服务清掉了共用图元的位图缓存：现有路径与另外两个宿主照常重画，结果不变。
					difference = Difference(gdi.frame, gdiFirst.frame);
					if (difference.empty()) difference = Difference(external->Pixels(), before);
					if (difference.empty()) difference = Difference(externalNoClip->Pixels(), before);
					if (difference.empty() && !hostsRendered) difference = "a second host failed to render";
					if (!difference.empty()) dump("gdi-after-discard", gdi.frame);
					check(difference.empty(), "other-hosts-unaffected", difference);
				}
				return true;
			};
			const HRESULT hr = RunGoldenMainSceneForHost(scene.drawAttribute, scene.dark, scene.scale, host);
			// 第二宿主先于下一场景的夹具重建释放（清理回调会访问主栏的图元表）。
			externalNoClip.reset(); external.reset(); plain.reset();
			check(hr == S_OK && phasesReached, "replay-scene",
				hr == S_FALSE ? "fixed-frame animation did not settle" : "scene setup, submit or BGRA readback failed");
			if (floating_window != nullptr) ++windowViolations;

			// 全程没有窗口：主栏窗口句柄始终为空，本线程名下没有任何顶层或 message-only 窗口。
			{
				const unsigned windowsAfter = CountThreadWindows();
				std::ostringstream why;
				why << "floating_window violations=" << windowViolations << " thread windows " << windowsBefore << "->" << windowsAfter
					<< " replay callbacks=" << replayCalls;
				check(windowViolations == 0 && windowsBefore == 0 && windowsAfter == 0 && replayCalls > 0, "no-window", why.str());
			}
		}
	}

	namespace
	{
		// 日志验收：本入口早于产品的日志启动点返回，所以自己开一个写进隔离目录的日志会话；
		// 排空后核对 UI3 的帧目标生命周期记录确实落盘。日志入口是非阻塞的（拿不到锁即丢弃），
		// 因此只要求每类记录至少出现一次，不核对条数。
		void CheckLifecycleLog(Report& report, const std::filesystem::path& logDirectory, bool started)
		{
			Inkeys::Log::FlushAndStop();
			std::string text;
			std::error_code ignored;
			if (std::filesystem::exists(logDirectory, ignored))
				for (const auto& entry : std::filesystem::directory_iterator(logDirectory, ignored))
					if (entry.is_regular_file() && entry.path().extension() == L".log")
					{
						std::ifstream file(entry.path(), std::ios::binary);
						text.append(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
					}
			// 只数 UI3 模块的行：行格式为 [时间][级别][模块][线程]… 正文。
			const auto count = [&](std::string_view needle)
			{
				size_t found = 0;
				for (size_t begin = 0; begin < text.size();)
				{
					size_t end = text.find('\n', begin);
					if (end == std::string::npos) end = text.size();
					const std::string_view line(text.data() + begin, end - begin);
					if (line.find("[UI3]") != std::string_view::npos && line.find(needle) != std::string_view::npos) ++found;
					begin = end + 1;
				}
				return found;
			};
			const size_t recreated = count("frame target recreated"), attached = count("frame target attached"),
				discarded = count("frame target discarded");
			std::ostringstream why;
			why << "started=" << started << " bytes=" << text.size() << " recreated=" << recreated
				<< " attached=" << attached << " discarded=" << discarded;
			report.text << "INFO log.ui3-frame-target-lifecycle " << why.str() << '\n';
			report.Check(started && !text.empty(), "log.session", why.str());
			report.Check(recreated > 0 && attached > 0 && discarded > 0, "log.ui3-frame-target-lifecycle", why.str());
		}
	}

	int RunUi3HostOffscreenTest()
	{
		// 只初始化共享设备、字体与隔离配置；不创建 HWND，不启动主窗口或后台交互线程。
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		Report report;
		report.text.imbue(std::locale::classic());
		std::ofstream results;
		struct RuntimeScope
		{
			std::wstring originalPath = globalPath;
			bool com = false, pipeline = false;
			~RuntimeScope()
			{
				SetThemeStyleSource(nullptr);
				Inkeys::Drawing::Draw3::PublishShapeUiSnapshot({});
				barUISet.specTarget.Discard();
				if (pipeline) Pipeline::Shutdown();
				if (com) CoUninitialize();
				// 提前退出的路径上也要停掉测试日志会话（已停止时为空操作）。
				if (Inkeys::Log::Started()) Inkeys::Log::FlushAndStop();
				globalPath = originalPath;
			}
		} runtime;
		int exitCode = 0;
		try
		{
			const auto directory = std::filesystem::absolute(L"Build/ui3-host");
			std::filesystem::create_directories(directory / L"runtime-config/Inkeys/Config");
			// 产品配置归一化写回只会落在隔离目录；不调用任何用户配置加载器。
			globalPath = (directory / L"runtime-config").wstring() + L"\\";
			// 测试专用日志会话：只删本入口上次留下的日志，避免旧文件被当成这次的记录。
			const auto logDirectory = directory / L"runtime-config/Inkeys/Logs";
			{
				std::error_code ignored;
				if (std::filesystem::exists(logDirectory, ignored))
					for (const auto& entry : std::filesystem::directory_iterator(logDirectory, ignored))
						if (entry.is_regular_file() && entry.path().extension() == L".log")
							std::filesystem::remove(entry.path(), ignored);
			}
			Inkeys::Log::SessionInfo logSession;
			logSession.rootPath = globalPath;
			logSession.buildConfiguration = L"Dev";
			logSession.launch = L"ui3-host-offscreen-test";
			logSession.threshold = Inkeys::Log::Threshold::Info;
			const bool logStarted = Inkeys::Log::Start(logSession);
			// 只删除本入口上次留下的失败转储，避免旧文件被误认为这次的结果。
			for (const auto& entry : std::filesystem::directory_iterator(directory))
				if (entry.is_regular_file() && entry.path().extension() == L".bgra")
				{
					std::error_code ignored;
					std::filesystem::remove(entry.path(), ignored);
				}
			results.open(directory / L"results.log", std::ios::binary | std::ios::trunc);
			if (!results) throw std::runtime_error("output stream");
			const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			runtime.com = SUCCEEDED(com);
			if (!runtime.com) exitCode = 2;
			else
			{
				runtime.pipeline = SUCCEEDED(Pipeline::Initialize());
				if (!runtime.pipeline) exitCode = 3;
			}
			if (exitCode) report.Check(false, "runtime", exitCode == 2 ? "COM initialization failed" : "RenderPipeline::Initialize failed");
			else
			{
				IdtFontFileLoader::IsLoaderInitialized(); IdtFontCollectionLoader::IsLoaderInitialized();
				const std::array<UINT, 4> fonts{IDR_TTF1, IDR_TTF7, IDR_TTF3, IDR_TTF8};
				const bool assets = SUCCEEDED(Pipeline::InitializeFontCollection(IdtFontFileLoader::GetLoader(),
					IdtFontCollectionLoader::GetLoader(), fonts));
				report.Check(assets, "font-collection", "font collection initialization failed");
				if (assets)
					for (const auto& scene : HostScenes)
					{
						try { RunScene(scene, report, directory); }
						catch (...) { report.Check(false, std::string(scene.name) + ".exception", "scene or output exception"); }
					}
				if (assets)
				{
					try { CheckTransactionUnwind(report); }
					catch (...) { report.Check(false, "transaction-unwind.exception", "unexpected exception"); }
					try { CheckLifecycleLog(report, logDirectory, logStarted); }
					catch (...) { report.Check(false, "log.exception", "unexpected exception"); }
				}
			}
		}
		catch (...)
		{
			report.Check(false, "runner", "output directory or stream failure");
		}
		report.Check(floating_window == nullptr, "no-window-at-exit", "floating_window is not null");
		std::ostringstream summary;
		summary << "checks=" << report.checks << " failures=" << report.failures;
		results << report.text.str() << summary.str() << '\n';
		results.close();
		if (!results)
		{
			// 结果文件写不出来时整次运行不能算通过。
			++report.failures;
			summary.str(std::string{});
			summary << "checks=" << report.checks << " failures=" << report.failures;
			Inkeys::Log::TryWrite(Inkeys::Log::Level::Error, Inkeys::Log::Module::Bar, std::nullopt,
				"Host offscreen test could not write results.log");
		}
		std::fprintf(stderr, "%s\n", summary.str().c_str());
		return exitCode ? exitCode : report.failures ? 1 : 0;
	}
}
