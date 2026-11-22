module;
// 第二宿主验证（--ui3-host-offscreen-test）：证明主栏内容不依赖主栏自己的窗口与 GDI 兼容目标。
// 同一帧主栏内容经协调器的「内容重放」桥画进测试自建的帧目标（普通位图形态、外部上下文形态）
// 与测试新建的内容绘制器，再与现有 GDI 兼容路径（主栏帧目标 + 帧事务 + 测试提交端读回）逐字节比较。
// 场景搭建与确定性处理复用像素基线入口（Bar.Golden.Test.cpp）；全程不创建任何窗口。
#include "../../../IdtMain.h"
#include "../../../IdtState.h"
#include "../../../IdtI18n.h"
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
#include <cmath>
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
#include "../Setting/Setting.Motion.h"
#include "imgui/imgui_impl_dx11.h"

module Inkeys.UI.Bar;
import :Main;
import :ContentInstance;
import :MainContent;
import Inkeys.UI.Bar.LayoutModel;
import Inkeys.Other.Config;
import :Theme;
import Inkeys.UI.RenderPipeline;
import Inkeys.Text.Font;
#include "../Setting/Setting.ComponentEditor.h"

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

		// 三种目标各自拥有帧事务；外部宿主负责清屏和 EndDraw。
		bool PaintPreview(BarContentInstance& instance, const PreviewFrame& frame, FrameTargetKind kind,
			unsigned width, unsigned height, Frame& pixels, unsigned generationOffset = 0, bool failEndDraw = false, const wchar_t* ghostHint = nullptr)
		{
			FrameTarget target(instance.Renderer(),kind);
			FrameTarget storage(FrameTargetKind::PlainBitmap);
			auto epoch = Pipeline::GetDeviceEpoch();
			if (generationOffset)
			{
				ComPtr<ID2D1Device> replacement;
				auto factory = Pipeline::GetSharedAssets().d2dFactory;
				if (FAILED(factory->CreateDevice(epoch.dxgiDevice.Get(),&replacement))) return false;
				epoch.d2dDevice = replacement;
				epoch.generation += generationOffset;
			}
			const bool external = kind == FrameTargetKind::ExternalContext;
			if (FAILED((external ? storage : target).Ensure(epoch,width,height))) return false;
			if (external && FAILED(target.AttachExternalContext(storage.DeviceContext(),epoch.generation,width,height))) return false;
			auto* dc = target.DeviceContext();
			if (external)
			{
				dc->BeginDraw();
				dc->Clear(D2D1::ColorF(0,0.0f));
			}
			target.BeginFrame(dc,D2D1::RectF(0,0,static_cast<float>(width),static_cast<float>(height)));
			const auto transform = ghostHint ? D2D1::Matrix3x2F::Identity() : D2D1::Matrix3x2F::Translation(3.0f,7.0f);
			dc->SetTransform(transform);
			const float dpi=ghostHint ? 96.f : 120.f;
			dc->SetDpi(dpi,dpi);
			bool ok = ghostHint ? SUCCEEDED(instance.DrawGhost(target,std::string(BarComponentId::Explorer),
				{float(width/2),float(height/2)},ghostHint,std::nullopt)) : SUCCEEDED(instance.Draw(target,frame));
			D2D1_MATRIX_3X2_F returned{};
			float dpiX, dpiY;
			dc->GetTransform(&returned);
			dc->GetDpi(&dpiX,&dpiY);
			ok = ok && !std::memcmp(&transform,&returned,sizeof(returned)) && dpiX == dpi && dpiY == dpi;
			dc->SetDpi(96,96);
			dc->SetTransform(D2D1::Matrix3x2F::Identity());
			target.FinishDrawCommands(dc);
			// 故意制造 D2D 栈不匹配，让已发出命令的外部帧在 EndDraw 失败。
			if (failEndDraw) dc->PopAxisAlignedClip();
			HRESULT hr = target.EndFrame(dc);
			if (external) { hr = dc->EndDraw(); target.NotifyExternalEndDraw(hr); }
			instance.ConfirmFrame(frame.frameId,ok && SUCCEEDED(hr));
			if (failEndDraw) return ok && FAILED(hr);
			ok = ok && SUCCEEDED(hr);
			ok = ok && ReadBitmap(dc,(external ? storage : target).TargetBitmap(),pixels);
			target.Discard();
			return ok;
		}
		void CheckMatchingBarButton(const HostScene& scene, const ReplayGeometry& replay,
			const Frame& real, Report& report)
		{
			BarContentInstance preview;
			auto model = barUISet.barButtonSet.ReadBarLayout();
			// 离屏夹具直接调用 Load，走与生产 Load 相同的未提交模型回退。
			if (!model) model = FromConfig(config.GetBarLayout(),barUISet.barButtonSet.GetLayoutCatalog());
			report.Check(model && !(*model)[Segment::A1].empty(),"preview.match.model");
			if (!model) return;
			preview.SetLayout(*model,1);
			PreviewInputs input;
			// 给 1.5 倍场景留足上下两侧的 More 峰值预留，不让预览高度成为缩放上限。
			constexpr UINT previewWidth = 1600, previewHeight = 720;
			input.width = previewWidth; input.height = previewHeight;
			input.targetHeight = BarLargeSizeMetrics.barHeight * scene.scale;
			input.dynamicLighting = false;
			input.time = std::chrono::steady_clock::time_point{} + std::chrono::seconds(2);
			input.typography = MakeBarLocalizedTypographyServices().localizedTypography();
			const auto assets = Pipeline::GetSharedAssets();
			input.writeFactory = assets.dwriteFactory.Get(); input.fonts = assets.fontCollection.Get();
			auto frame = preview.Advance(input);
			const auto* select = barUISet.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Select)];
			const auto origin = LayoutToTarget(replay.mapping,D2D1::Point2F(static_cast<float>(select->button.inhX),
				static_cast<float>(select->button.inhY)));
			// 只对齐测试采样的像素相位；预览和实栏不需要使用相同的屏幕坐标。
			if (!scene.drawAttribute) PreviewTestAccess::AlignMainButtonPixelPhase(preview, BarButtonPresetEnum::Select, origin);
			Frame rendered;
			const bool painted = PaintPreview(preview,frame,FrameTargetKind::PlainBitmap,previewWidth,previewHeight,rendered);
			report.Check(painted,"preview.match.render");
			if (!painted) return;
			const auto body = barUISet.shapeMap[BarUISetShapeEnum::MainBar];
			const auto previewBody = preview.Geometry().body;
			report.Check(std::abs(previewBody.bottom-previewBody.top-body->h.val*scene.scale)<0.01,
				"preview.match.body-height");
			// 比较与主按钮光锚无关的选择图标和标签；深色属性盘使用不同的遮挡关系。
			if (!scene.drawAttribute)
			{
				for (const auto& h : preview.Geometry().items)
				{
					if (!h.position || (*model)[h.position->segment][h.position->index].Id != BarButtonId::Select) continue;
					Frame sample, reference;
					constexpr LONG inset = 8;
					const SIZE size{static_cast<LONG>(select->button.w.val)-2*inset,
						static_cast<LONG>(select->button.h.val)-2*inset};
					const bool cropped = Crop(real,{static_cast<LONG>(std::floor(origin.x))+inset,
						static_cast<LONG>(std::floor(origin.y))+inset},size,reference)
						&& Crop(rendered,{static_cast<LONG>(std::floor(h.rect.left))+inset,
							static_cast<LONG>(std::floor(h.rect.top))+inset},size,sample);
					if (cropped && sample.bytes!=reference.bytes)
					{
						report.text << "INFO preview.match origin=" << origin.x << "," << origin.y << " vs " << h.rect.left << "," << h.rect.top << '\n';
						Dump(L"Build/ui3-host","preview-select",sample);
						Dump(L"Build/ui3-host","real-select",reference);
					}
					report.Check(cropped && sample.bytes==reference.bytes,"preview.match.select-icon-label",Difference(reference,sample));
					return;
				}
				report.Check(false,"preview.match.select-missing");
			}
		}

		void CheckPreviewProjection(PreviewInputs input, Report& report)
		{
			struct RestoreAnimation
			{
				bool enabled = UiAnimationEnabled;
				double rate = UiAnimationSpeedRate;
				~RestoreAnimation() { UiAnimationEnabled = enabled; UiAnimationSpeedRate = rate; }
			} restore;
			UiAnimationEnabled = false;
			auto model = Reset();
			model[Segment::B1] = {{BarButtonId::UserDivider,BarButtonSizeKind::OneTwo},{BarButtonId::UserDivider,BarButtonSizeKind::OneTwo},
				{BarComponentId::Explorer,BarButtonSizeKind::TwoTwo,false},{BarComponentId::Explorer},{BarButtonId::Setting}};
			model[Segment::More].clear();
			input.sizeMode = BarSizeMode::Compact;
			input.presentation = true;
			input.edgeLighting = false;
			BarContentInstance instance;
			instance.SetLayout(model,10);
			instance.SetMoreExpanded(true);
			Frame pixels;
			auto paint = [&]
			{
				input.time += std::chrono::milliseconds(20);
				auto frame = instance.Advance(input);
				return PaintPreview(instance,frame,FrameTargetKind::PlainBitmap,1200,360,pixels);
			};
			auto count = [&](PreviewHitKind kind)
			{
				const auto& items = instance.Geometry().items;
				return std::count_if(items.begin(),items.end(),[&](const auto& h){return h.kind==kind;});
			};
			report.Check(paint() && count(PreviewHitKind::UserDivider)==2 && count(PreviewHitKind::Boundary)==2
				&& count(PreviewHitKind::Panel)==0 && count(PreviewHitKind::More)==1,"preview.dividers-empty-more");
			report.Check(!instance.Renderer().SnapshotFrameLighting().edgeLightingEnabled,"preview.edge-off");
			for (const auto& item:instance.Geometry().items)
			{
				if (item.kind!=PreviewHitKind::UserDivider) continue;
				const auto previous=instance.Geometry();
				const D2D1_POINT_2F sample{item.rect.right+previous.dividerReach*0.8f,(item.rect.top+item.rect.bottom)/2};
				const auto before=instance.HitTest(sample);
				auto narrow=input;
				narrow.width=120;
				const auto candidate=instance.Advance(narrow);
				const bool failed=PaintPreview(instance,candidate,FrameTargetKind::ExternalContext,120,360,pixels,0,true);
				const auto after=instance.HitTest(sample);
				report.Check(failed && before.position && after.position && before.kind==PreviewHitKind::UserDivider
					&& before.position->segment==after.position->segment && before.position->index==after.position->index
					&& instance.Geometry().frameId==previous.frameId,"preview.failed-divider-reach");
				break;
			}
			report.Check(paint(),"preview.restore-after-failure");
			bool presentation = false, freeze = false, hidden = false;
			std::optional<Position> recall;
			for (const auto& h : instance.Geometry().items)
			{
				if (!h.position) continue;
				const auto p = *h.position;
				presentation |= model[p.segment][p.index].Id == BarButtonId::EndShow;
				freeze |= model[p.segment][p.index].Id == BarButtonId::Freeze;
				hidden |= p.segment == Segment::B1 && p.index==2;
				if (h.kind==PreviewHitKind::Companion) recall=p;
			}
			report.Check(presentation && !freeze && !hidden,"preview.hidden-projection");
			report.Check(recall && instance.Lift(*recall,10) && paint()
				&& count(PreviewHitKind::Companion)==0,"preview.lift-recall-redo");
			instance.CancelLift();
			report.Check(paint() && count(PreviewHitKind::Companion)==1,"preview.restore-recall-redo");
			const auto& emptyGeometry = instance.Geometry();
			const auto more = std::find_if(emptyGeometry.items.begin(), emptyGeometry.items.end(),
				[](const auto& hit) { return hit.kind == PreviewHitKind::More; });
			std::optional<D2D1_POINT_2F> morePoint;
			if (more != emptyGeometry.items.end())
				morePoint = D2D1::Point2F((more->rect.left + more->rect.right) / 2, (more->rect.top + more->rect.bottom) / 2);
			report.Check(more != emptyGeometry.items.end() && !instance.Activate(*more) && !instance.MoreExpanded(),
				"preview.empty-more-does-not-toggle");
			const auto catalog = barUISet.barButtonSet.GetLayoutCatalog();
			bool emptyHover = false;
			if (morePoint)
			{
				using namespace Setting::ComponentEditor;
				struct CountedPreview
				{
					BarContentInstance& instance;
					int requests = 0;
					bool MoreExpanded() const { return instance.MoreExpanded(); }
					void SetMoreExpanded(bool expanded) { ++requests; instance.SetMoreExpanded(expanded); }
				} preview{instance};
				PreviewPressInput<CountedPreview, PreviewHit> hoverInput;
				hoverInput.drag.BeginDragging(0, DragPointerKind::Mouse, *morePoint, {10});
				const ItemSource source = std::string_view(BarComponentId::Explorer);
				const auto frameId = emptyGeometry.frameId;
				emptyHover = frameId != 0;
				// 每个采样点对应一帧，空面板拒绝展开后也应消费本轮停留。
				for (const double now : {1., 1.5, 1.75, 2.})
				{
					bool dirty = false;
					const auto hit = instance.HitTest(*morePoint);
					const auto drop = ResolveDragFrameDrop(hoverInput, preview, hit, true, now, dirty,
						model, source, catalog, false, false, {});
					emptyHover = emptyHover && hit.kind == PreviewHitKind::More && drop.action == DropAction::Store
						&& !dirty && !instance.MoreExpanded() && instance.Geometry().frameId == frameId;
				}
				emptyHover = emptyHover && preview.requests == 1;
			}
			report.Check(emptyHover, "preview.host-empty-more-hover-stays-clean");
			for (int i=0;i<11;++i) model[Segment::More].push_back({BarComponentId::Explorer});
			instance.SetLayout(model,11);
			report.Check(paint() && !instance.MoreExpanded() && count(PreviewHitKind::Panel)==0, "preview.empty-more-stays-closed");
			instance.SetMoreExpanded(true);
			report.Check(paint() && count(PreviewHitKind::Panel)==1,"preview.wrapped-panel");
			bool wrap = false;
			const auto& gaps = instance.Geometry().gaps;
			for (size_t i=1;i<gaps.size();++i)
				if (gaps[i].position.segment==Segment::More && gaps[i-1].position.segment==Segment::More
					&& gaps[i].position.index==gaps[i-1].position.index && gaps[i].start.y!=gaps[i-1].start.y) wrap=true;
			report.Check(wrap,"preview.more-row-break");
			Frame withoutBadge = pixels;
			PreviewTestAccess::ShowGeometryBadge(instance);
			report.Check(paint() && pixels.bytes!=withoutBadge.bytes,"preview.pinned-badge");
			Frame sameTime, newEpoch;
			auto withBadge = instance.Advance(input);
			const bool paintedSameTime = PaintPreview(instance,withBadge,FrameTargetKind::PlainBitmap,1200,360,sameTime);
			report.Check(paintedSameTime && sameTime.bytes==pixels.bytes,"preview.more-badge-same-time",Difference(pixels,sameTime));
			const bool paintedNewEpoch = PaintPreview(instance,withBadge,FrameTargetKind::ExternalContext,1200,360,newEpoch,1);
			report.Check(paintedNewEpoch && newEpoch.bytes==pixels.bytes,"preview.more-badge-new-epoch",Difference(pixels,newEpoch));
			const Position dividerSource{Segment::B1,1};
			bool preserved = false;
			if (instance.Lift(dividerSource,11) && paint())
				for (const auto& line : instance.Geometry().gaps)
					if (line.position.segment==Segment::B1 && line.position.index==dividerSource.index)
					{
						auto same = Move(model,dividerSource,line.position,catalog);
						preserved = same && *same == model;
					}
			report.Check(preserved,"preview.hidden-item-order");
			instance.CancelLift();
			report.Check(paint(),"preview.hidden-item-restored");
			const Position source{Segment::B1,3};
			report.Check(instance.Lift(source,11) && paint(),"preview.same-zone-lift");
			bool movedRight = false;
			for (const auto& line : instance.Geometry().gaps)
			{
				if (line.position.segment!=Segment::B1 || line.position.index!=model[Segment::B1].size()) continue;
				const auto candidates = instance.InsertionGaps({(line.start.x+line.end.x)/2,(line.start.y+line.end.y)/2});
				for (const auto& candidate : candidates)
				{
					if (candidate.position.segment!=Segment::B1 || candidate.position.index!=model[Segment::B1].size()) continue;
					auto next = Move(model,source,candidate.position,catalog);
					movedRight = next && (*next)[Segment::B1].back().Id==BarComponentId::Explorer
						&& instance.Place(*next,candidate.position,11);
					break;
				}
				break;
			}
			report.Check(movedRight,"preview.source-gap-after-hidden");
			model = Reset();
			model[Segment::B1] = {{BarComponentId::Explorer,BarButtonSizeKind::TwoOne},
				{BarComponentId::Control,BarButtonSizeKind::TwoOne}};
			input.sizeMode = BarSizeMode::Large;
			instance.SetLayout(model,12);
			instance.SetMoreExpanded(false);
			report.Check(paint(),"preview.stacked-pair");
			auto widthBefore = instance.Geometry().body;
			const bool stackedGap = std::any_of(instance.Geometry().gaps.begin(),instance.Geometry().gaps.end(),
				[](const auto& gap){return gap.position.segment==Segment::B1 && gap.start.y==gap.end.y;});
			report.Check(stackedGap && instance.Lift({Segment::B1,0},12) && paint()
				&& instance.Geometry().body.left==widthBefore.left,"preview.lift-half-column");
			model = Reset();
			instance.SetLayout(model,12);
			instance.SetMoreExpanded(false);
			input.sizeMode = BarSizeMode::Large;
			instance.Suspend();
			report.Check(instance.Geometry().items.empty(),"preview.suspended-hits");
			report.Check(paint(),"preview.no-animation");
			const auto original = instance.Geometry().body;
			if (instance.Geometry().items.empty()) { report.Check(false,"preview.empty-layout"); return; }
			const auto first = instance.Geometry().items.front().position;
			report.Check(first && instance.Lift(*first,12) && paint()
				&& instance.Geometry().body.left>original.left,"preview.snap-shrink");
			instance.CancelLift();
			report.Check(paint() && instance.Geometry().body.left==original.left,"preview.snap-cancel");
			if (!first) return;
			auto sampleRate = [&](double rate)
			{
				UiAnimationEnabled = true; UiAnimationSpeedRate = rate;
				instance.Lift(*first,12);
				// 静置后先取基准时刻，再量相同 dt 下的宽度变化。
				input.time += std::chrono::seconds(5);
				instance.Advance(input);
				const bool ok = paint();
				const auto left = instance.Geometry().body.left;
				instance.CancelLift();
				UiAnimationEnabled = false;
				return ok && paint() ? left : -1.0f;
			};
			const auto slow = sampleRate(0.5), fast = sampleRate(2);
			report.Check(slow>original.left && fast>slow,"preview.animation-rate");
			input.height=80;
			report.Check(paint(),"preview.constrained-host");
			auto pose=[&] {
				const auto r=instance.Geometry().body;
				return std::pair{r.top,r.bottom-r.top};
			};
			const auto closed=pose();
			UiAnimationEnabled=true; UiAnimationSpeedRate=1;
			input.time+=std::chrono::seconds(5);
			instance.SetMoreExpanded(true);
			// C5 合同：More 开合全程主栏位置与大小不变（上方常驻预留，相机只读目标）；反向在同一时刻接续。
			report.Check(paint() && pose()==closed,"preview.more-first-frame");
			report.Check(paint() && pose()==closed,"preview.more-mid-frame-stable");
			instance.SetMoreExpanded(false);
			input.time-=std::chrono::milliseconds(20);
			report.Check(paint() && pose()==closed,"preview.more-reverse-frame");
			for (int i=0;i<40;++i) paint();
			report.Check(pose()==closed,"preview.more-close-end");
			instance.SetMoreExpanded(true);
			for (int i=0;i<40;++i) paint();
			report.Check(pose()==closed,"preview.more-open-end");
		}

		// C5 复刻合同：More 回弹时预览相机与主栏不动、X 有像素、底板边缘与末端按钮同批次、分割线可插入 A1/B1/A2。
		// 几何与像素同为预览纹理坐标（Draw 内部以单位变换、96 DPI 绘制）。
		void CheckPreviewReplica(PreviewInputs input, Report& report)
		{
			struct RestoreAnimation
			{
				bool enabled = UiAnimationEnabled;
				double rate = UiAnimationSpeedRate;
				~RestoreAnimation() { UiAnimationEnabled = enabled; UiAnimationSpeedRate = rate; }
			} restore;
			UiAnimationEnabled = true; UiAnimationSpeedRate = 1;
			const auto catalog = barUISet.barButtonSet.GetLayoutCatalog();
			std::uint64_t revision = 20;
			for (const auto mode : {BarSizeMode::Large, BarSizeMode::Compact})
			{
				const std::string tag = mode == BarSizeMode::Large ? "large" : "compact";
				auto check = [&](bool ok, const char* label, const std::string& why = {})
				{ report.Check(ok, "preview.replica." + tag + "." + label, why); };
				input.sizeMode = mode;
				auto model = Reset();
				model[Segment::More] = {{BarComponentId::Explorer}, {BarComponentId::Control}};
				BarContentInstance instance;
				instance.SetLayout(model, ++revision);
				Frame pixels;
				PreviewFrame frame;
				bool ok = true;
				auto paint = [&]
				{
					input.time += std::chrono::milliseconds(16);
					frame = instance.Advance(input);
					return PaintPreview(instance, frame, FrameTargetKind::PlainBitmap, 1200,
						static_cast<unsigned>(std::ceil(input.height)), pixels);
				};
				// 开合 More 时：宿主据以裁切纹理的区域、缩放倍率与主栏底板都不能变；面板要有超过终点的回弹峰值。
				// 宽松宿主之外再测宽度受限的窄宿主（宽度只比主栏略宽，旧实现会把面板宽度/回弹计入缩放）。
				const auto center = [](const D2D1_RECT_F& rect)
				{ return D2D1::Point2F((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2); };
				auto moreOpen = [&](const char* suffix)
				{
					instance.SetMoreExpanded(false);
					for (int i = 0; i < 60; ++i) ok = paint() && ok;
					const auto closedBody = instance.Geometry().body;
					const auto closedBounds = frame.bounds;
					const double closedScale = frame.scale;
					const auto& items = instance.Geometry().items;
					const auto more = std::find_if(items.begin(), items.end(),
						[](const PreviewHit& hit) { return hit.kind == PreviewHitKind::More; });
					const auto morePoint = more != items.end() ? center(more->rect) : D2D1::Point2F();
					const bool confirmedMore = instance.HitTest(morePoint).kind == PreviewHitKind::More;
					instance.SetMoreExpanded(true);
					// 模拟拖停展开和下一次按下之间的窗口：像素仍在，编辑命中已失效。
					const std::string name = std::string("more-") + suffix;
					check(confirmedMore && instance.MoreExpanded() && !instance.Geometry().frameId
						&& instance.HitTest(morePoint).kind == PreviewHitKind::Empty
						&& instance.InsertionGaps(morePoint).empty()
						&& !instance.ShouldDismissMoreOnPress(true, morePoint), (name + "-press-before-confirm").c_str());
					float peak = 0, last = 0;
					bool bodyStable = true, regionStable = true, scaleStable = true, heightFits = true;
					for (int i = 0; i < 60; ++i)
					{
						ok = paint() && ok;
						const auto& g = instance.Geometry();
						bodyStable = bodyStable && std::abs(g.body.left - closedBody.left) < 0.01f
							&& std::abs(g.body.top - closedBody.top) < 0.01f && std::abs(g.body.bottom - closedBody.bottom) < 0.01f;
						regionStable = regionStable && std::abs(frame.bounds.top - closedBounds.top) < 0.01f
							&& std::abs(frame.bounds.bottom - closedBounds.bottom) < 0.01f;
						scaleStable = scaleStable && std::abs(frame.scale - closedScale) < 1e-9;
						heightFits = heightFits && frame.extent.height <= input.height;
						for (const auto& item : g.items)
							if (item.kind == PreviewHitKind::Panel) { last = item.rect.bottom - item.rect.top; peak = (std::max)(peak, last); }
					}
					check(instance.Geometry().frameId && instance.HitTest(morePoint).kind == PreviewHitKind::More
						&& !instance.ShouldDismissMoreOnPress(true, morePoint)
						&& instance.ShouldDismissMoreOnPress(false, morePoint), (name + "-press-after-confirm").c_str());
					check(bodyStable, (name + "-body-stable").c_str());
					check(regionStable, (name + "-region-stable").c_str(), "closed=" + std::to_string(closedBounds.top) + ".."
						+ std::to_string(closedBounds.bottom) + " open=" + std::to_string(frame.bounds.top) + ".." + std::to_string(frame.bounds.bottom));
					check(scaleStable, (name + "-scale-stable").c_str(), "closed=" + std::to_string(closedScale) + " open=" + std::to_string(frame.scale));
					check(heightFits, (name + "-fits-height").c_str());
					check(last > 0 && peak > last + 0.5f, (name + "-overshoot").c_str(),
						"peak=" + std::to_string(peak) + " final=" + std::to_string(last));
				};
				for (int i = 0; i < 60; ++i) ok = paint() && ok;
				const auto closed = instance.Geometry().body;
				const double roomyWidth = input.width;
				input.width = (closed.right - closed.left) * 1.08 + 40;
				moreOpen("narrow");
				input.width = roomyWidth;
				moreOpen("roomy");
				check(ok, "render");
				// X 图标必须真的画出来：关闭键矩形内要有明显的笔画亮度差（底板本身近似均匀）。
				int low = 3 * 255, high = 0;
				for (const auto& item : instance.Geometry().items)
				{
					if (item.kind != PreviewHitKind::Close || !pixels.Valid()) continue;
					for (int y = static_cast<int>(item.rect.top) + 2; y < static_cast<int>(item.rect.bottom) - 2; ++y)
						for (int x = static_cast<int>(item.rect.left) + 2; x < static_cast<int>(item.rect.right) - 2; ++x)
						{
							if (x < 0 || y < 0 || x >= static_cast<int>(pixels.width) || y >= static_cast<int>(pixels.height)) continue;
							const auto* px = &pixels.bytes[(static_cast<size_t>(y) * pixels.width + x) * 4];
							const int sum = px[0] + px[1] + px[2];
							low = (std::min)(low, sum); high = (std::max)(high, sum);
						}
				}
				check(high - low > 150, "close-glyph-pixels", "range=" + std::to_string(high - low));
				instance.SetMoreExpanded(false);
				for (int i = 0; i < 60; ++i) ok = paint() && ok;
				// 从设置宿主的真实落点入口起步：空 B1 的 More 边缘以及 A1/A2 的已呈现缝。
				using Setting::ComponentEditor::DropAction;
				using Setting::ComponentEditor::ResolveDrop;
				const auto geometry = instance.Geometry();
				const auto capsule = Setting::ComponentEditor::TrashBounds(frame.bounds, geometry.body.bottom, 170, 36);
				check(capsule.top > geometry.body.bottom && capsule.bottom < frame.bounds.bottom
					&& std::abs(center(capsule).x - center(geometry.body).x) < 0.1f, "trash-in-presented-reserve");
				auto dropDivider = [&](D2D1_POINT_2F point)
				{
					const bool more = instance.HitTest(point).kind == PreviewHitKind::More;
					std::vector<Position> gaps;
					for (const auto& gap : instance.InsertionGaps(point)) gaps.push_back(gap.position);
					const auto drop = ResolveDrop(model, std::string_view(BarButtonId::UserDivider),
						catalog, false, false, more, gaps);
					const auto inserted = drop.gap ? Insert(model, BarButtonId::UserDivider, *drop.gap, catalog) : std::nullopt;
					return std::pair{drop, inserted};
				};
				for (const auto zone : {Segment::A1, Segment::B1, Segment::A2})
				{
					const auto gap = std::find_if(geometry.gaps.begin(), geometry.gaps.end(),
						[&](const PreviewGap& g) { return g.position.segment == zone && g.position.index == 0; });
					bool inserted = false;
					if (gap != geometry.gaps.end())
					{
						const D2D1_POINT_2F point{gap->start.x, (gap->start.y + gap->end.y) / 2};
						const auto [drop, next] = dropDivider(point);
						const bool moreEdge = zone != Segment::B1 || instance.HitTest(point).kind == PreviewHitKind::More;
						inserted = moreEdge && drop.action == DropAction::Insert && drop.gap && drop.gap->segment == zone
							&& next && (*next)[zone].front().Id == BarButtonId::UserDivider;
					}
					check(inserted, zone == Segment::A1 ? "divider-drop-a1" : zone == Segment::B1 ? "divider-drop-empty-b1-more-edge" : "divider-drop-a2");
				}
				const auto more = std::find_if(geometry.items.begin(), geometry.items.end(),
					[](const PreviewHit& item) { return item.kind == PreviewHitKind::More; });
				check(more != geometry.items.end() && dropDivider(center(more->rect)).first.action == DropAction::CancelAdd,
					"divider-drop-more-cancels");

				if (mode == BarSizeMode::Compact)
				{
					Setting::ComponentEditor::PreviewPressInput<BarContentInstance, PreviewHit> hoverInput;
					auto moreHit = [&]() -> PreviewHit
					{
						for (const auto& item : instance.Geometry().items)
							if (item.kind == PreviewHitKind::More) return item;
						return {};
					};
					auto opensAfterDwell = [&](D2D1_POINT_2F point, const ItemSource& source)
					{
						const auto hit = instance.HitTest(point);
						const auto hoverFrame = [&](bool clipped, double now)
						{
							bool dirty = false;
							Setting::ComponentEditor::ResolveDragFrameDrop(hoverInput, instance, hit, clipped, now, dirty,
								model, source, catalog, false, false, {});
							return dirty;
						};
						return hit.kind == PreviewHitKind::More && hoverInput.drag.Phase() == DragPhase::Dragging
							&& !instance.MoreExpanded() && !hoverFrame(true, 1.)
							&& !hoverFrame(true, 1.49) && !instance.MoreExpanded()
							&& !hoverFrame(false, 1.5) // 裁框外应重置计时。
							&& !hoverFrame(true, 2.)
							&& hoverFrame(true, 2.5) && instance.MoreExpanded();
					};
					// 从靠外的固定项起拖，让同段首缝仍可重排，再取该缝与 More 的已呈现交集。
					const auto fixed = std::find_if(geometry.items.rbegin(), geometry.items.rend(), [](const PreviewHit& item)
					{ return item.kind == PreviewHitKind::Item && item.position && item.position->segment == Segment::A2; });
					bool blocked = false, fixedHover = false;
					if (fixed != geometry.items.rend() && more != geometry.items.end())
					{
						bool dirty = false;
						const bool lifted = hoverInput.Begin(instance, *fixed, 0, DragPointerKind::Mouse, center(fixed->rect), revision, 1.f)
							&& hoverInput.Update(instance, 0, center(more->rect), *more, true, true, dirty)
							&& hoverInput.drag.Phase() == DragPhase::Dragging;
						input.editing = lifted;
						for (int i = 0; i < 60; ++i) ok = paint() && ok;
						const auto& gaps = instance.Geometry().gaps;
						const auto firstGap = std::find_if(gaps.begin(), gaps.end(),
							[](const PreviewGap& gap) { return gap.position.segment == Segment::A2; });
						if (lifted && firstGap != gaps.end())
						{
							const auto button = moreHit().rect;
							const auto& area = firstGap->area;
							const auto overlap = D2D1::RectF((std::max)(area.left, button.left), (std::max)(area.top, button.top),
								(std::min)(area.right, button.right), (std::min)(area.bottom, button.bottom));
							const auto point = center(overlap);
							std::vector<Position> positions;
							for (const auto& gap : instance.InsertionGaps(point)) positions.push_back(gap.position);
							const bool hitMore = instance.HitTest(point).kind == PreviewHitKind::More;
							const auto drop = ResolveDrop(model, *fixed->position, catalog, false, false, hitMore, positions);
							blocked = overlap.left < overlap.right && overlap.top < overlap.bottom && hitMore
								&& std::any_of(positions.begin(), positions.end(), [&](Position pos)
									{ return pos.segment == Segment::A2 && pos.index == firstGap->position.index; })
								&& CanPlace(model, *fixed->position, firstGap->position, catalog) == PlaceDecision::Allowed
								&& drop.action == DropAction::NotAllowed && !drop.gap;
							fixedHover = opensAfterDwell(point, *fixed->position);
						}
					}
					check(blocked, "fixed-a2-drop-more-overlap");
					check(fixedHover, "fixed-a2-hover-more-half-second");
					hoverInput.Cancel(instance);
					instance.SetMoreExpanded(false);
					// 组件库分割线在 More 中心仍是 CancelAdd，悬停计时应照常展开。
					hoverInput.drag.BeginDragging(0, DragPointerKind::Mouse, center(moreHit().rect), {revision});
					input.editing = true;
					for (int i = 0; i < 60; ++i) ok = paint() && ok;
					const auto point = center(moreHit().rect);
					check(dropDivider(point).first.action == DropAction::CancelAdd
						&& opensAfterDwell(point, std::string_view(BarButtonId::UserDivider)), "divider-hover-more-half-second");
					hoverInput.Cancel(instance);
					instance.SetMoreExpanded(false);
					const bool libraryFrameReady = paint();
					ok = libraryFrameReady && ok;

					const auto libraryPoint = center(moreHit().rect);
					hoverInput.drag.BeginDragging(0, DragPointerKind::Mouse, libraryPoint, {revision});
					check(libraryFrameReady && opensAfterDwell(libraryPoint, std::string_view(BarComponentId::Explorer)),
						"host-library-item-hover-more-half-second");
					hoverInput.Cancel(instance);
					instance.SetMoreExpanded(false);
					input.editing = false;
					for (int i = 0; i < 60; ++i) ok = paint() && ok;
				}

				// 布局批次：底板右缘与最右侧主栏项右缘的距离在整个展开过程中保持不变。
				auto grown = model;
				grown[Segment::B1].push_back({BarComponentId::Explorer});
				instance.SetLayout(grown, ++revision);
				const float before = closed.right - closed.left;
				float lowGap = 1e9f, highGap = -1e9f, after = 0;
				for (int i = 0; i < 40; ++i)
				{
					ok = paint() && ok;
					const auto& g = instance.Geometry();
					float right = -1e9f;
					for (const auto& item : g.items)
						if (item.kind != PreviewHitKind::Panel && item.kind != PreviewHitKind::Close
							&& !(item.position && item.position->segment == Segment::More)) right = (std::max)(right, item.rect.right);
					lowGap = (std::min)(lowGap, g.body.right - right); highGap = (std::max)(highGap, g.body.right - right);
					after = g.body.right - g.body.left;
				}
				check(ok && after > before + 1, "batch-grows", "before=" + std::to_string(before) + " after=" + std::to_string(after));
				check(highGap - lowGap < 0.75f, "batch-edge-sync", "spread=" + std::to_string(highGap - lowGap));
				// 分割线：可从组件库插入 A1/B1/A2，不可进入 More。
				model = grown;
				for (const auto zone : {Segment::A1, Segment::B1, Segment::A2})
				{
					const Position gap{zone, 0};
					const auto next = Insert(model, BarButtonId::UserDivider, gap, catalog);
					const bool placed = next && instance.Place(*next, gap, revision);
					check(placed, zone == Segment::A1 ? "divider-insert-a1" : zone == Segment::B1 ? "divider-insert-b1" : "divider-insert-a2");
					if (placed) model = *next;
					ok = paint() && ok;
				}
				check(!Insert(model, BarButtonId::UserDivider, {Segment::More, 0}, catalog), "divider-not-more");
				// 沙盒工具模式：选择后橡皮/图形/撤回收拢隐藏且不再命中；切回绘制恢复，绘制选中显示软笔。
				auto visibleIds = [&]
				{
					std::vector<std::string> ids;
					for (const auto& h : instance.Geometry().items)
						if (h.position && h.kind == PreviewHitKind::Item) ids.push_back(model[h.position->segment][h.position->index].Id);
					return ids;
				};
				auto has = [](const std::vector<std::string>& ids, const char* id)
				{ return std::find(ids.begin(), ids.end(), id) != ids.end(); };
				for (int i = 0; i < 40; ++i) ok = paint() && ok;
				auto findHit = [&](PreviewHitKind kind, const char* id = nullptr) -> PreviewHit
				{
					for (const auto& hit : instance.Geometry().items)
						if (hit.kind == kind && (!id || (hit.position && model[hit.position->segment][hit.position->index].Id == id)))
							return hit;
					return {};
				};
				const auto drawHit = findHit(PreviewHitKind::Item, BarButtonId::Draw);
				check(instance.Press(drawHit) && PreviewTestAccess::PressTarget(instance, drawHit) == ButtonPressScale,
					"press-scale-target");
				input.pointer = D2D1::Point2F((drawHit.rect.left + drawHit.rect.right) / 2, (drawHit.rect.top + drawHit.rect.bottom) / 2);
				++input.pointerSerial;
				ok = paint() && ok;
				check(!frame.hintDeadline, "pressed-no-hover-deadline");
				bool pressInFlight = false;
				for (int i = 0; i < 8; ++i)
				{
					const double scale = PreviewTestAccess::PressScale(instance, drawHit);
					pressInFlight |= ButtonPressScale < scale && scale < 1.0;
					ok = paint() && ok;
				}
				check(pressInFlight, "press-scale-intermediate");
				input.pointer.reset();
				instance.ReleasePress();
				check(PreviewTestAccess::PressTarget(instance, drawHit) == 1.0, "release-scale-target");
				check(instance.Press(drawHit) && drawHit.position && instance.Lift(*drawHit.position, revision)
					&& PreviewTestAccess::PressTarget(instance, drawHit) == 1.0, "lift-releases-press");
				instance.CancelLift();
				ok = paint() && ok;
				const bool pressedBeforeRevision = instance.Press(drawHit);
				instance.SetLayout(model, ++revision);
				check(pressedBeforeRevision && PreviewTestAccess::PressTarget(instance, drawHit) == 1.0,
					"layout-revision-releases-press");
				ok = paint() && ok;
				const bool pressedBeforeSuspend = instance.Press(drawHit);
				instance.Suspend();
				check(pressedBeforeSuspend && PreviewTestAccess::PressTarget(instance, drawHit) == 1.0,
					"suspend-releases-press");
				ok = paint() && ok;
				const auto penIds = visibleIds();
				const auto penBody = instance.Geometry().body;
				using Preset = BarButtonPresetEnum;
				const auto eraserBefore = PreviewTestAccess::SampleCollapse(instance, Preset::Eraser);
				check(instance.Activate(findHit(PreviewHitKind::Item, BarButtonId::Select))
					&& instance.ToolMode() == StateModeSelectEnum::IdtSelection, "activate-select");
				bool collapseInFlight = false;
				for (int i = 0; i < 40; ++i)
				{
					ok = paint() && ok;
					const auto eraser = PreviewTestAccess::SampleCollapse(instance, Preset::Eraser);
					if (eraserBefore && eraser)
						collapseInFlight |= eraser->opacity > 0 && eraser->opacity < 1
							&& eraser->x > (std::min)(eraserBefore->x, eraser->targetX)
							&& eraser->x < (std::max)(eraserBefore->x, eraser->targetX);
				}
				check(collapseInFlight, "select-collapse-intermediate");
				const auto selectIds = visibleIds();
				const auto selectBody = instance.Geometry().body;
				check(has(penIds, BarButtonId::Eraser) && has(penIds, BarButtonId::Geometry) && has(penIds, BarButtonId::Recall)
					&& !has(selectIds, BarButtonId::Eraser) && !has(selectIds, BarButtonId::Geometry) && !has(selectIds, BarButtonId::Recall)
					&& selectBody.right - selectBody.left < penBody.right - penBody.left, "select-collapses");
				check(PreviewTestAccess::IsCollapsedTo(instance, Preset::Eraser, Preset::Draw)
					&& PreviewTestAccess::IsCollapsedTo(instance, Preset::Geometry, Preset::Draw)
					&& PreviewTestAccess::IsCollapsedTo(instance, Preset::Recall, Preset::Draw), "select-anchor-and-fade");
				const auto& gaps = instance.Geometry().gaps;
				check(std::none_of(gaps.begin(), gaps.end(), [&](const PreviewGap& gap)
					{
						const auto position = gap.position;
						if (position.segment != Segment::A1 || position.index >= model[Segment::A1].size()) return false;
						const auto& id = model[Segment::A1][position.index].Id;
						return id == BarButtonId::Eraser || id == BarButtonId::Geometry || id == BarButtonId::Recall;
					}), "select-gaps-exclude-hidden");
				check(instance.Activate(findHit(PreviewHitKind::Item, BarButtonId::Draw))
					&& instance.ToolMode() == StateModeSelectEnum::IdtPen, "activate-draw");
				for (int i = 0; i < 40; ++i) ok = paint() && ok;
				check(visibleIds() == penIds, "pen-restores");
				check(PreviewTestAccess::ButtonLabel(instance, BarButtonPresetEnum::Draw)
					== ResolveDrawStyle(true, false, false, false).LocalizedLabel(I18n::getW, ShapeToolLabel), "draw-soft-pen");
				check(instance.Activate(findHit(PreviewHitKind::Item, BarButtonId::Eraser))
					&& instance.ToolMode() == StateModeSelectEnum::IdtEraser, "activate-eraser");
				ok = paint() && ok;
				check(instance.Activate(findHit(PreviewHitKind::Item, BarButtonId::Geometry))
					&& instance.ToolMode() == StateModeSelectEnum::IdtShape, "activate-geometry");
				ok = paint() && ok;
				const auto moreHit = findHit(PreviewHitKind::More);
				const bool morePressed = instance.Press(moreHit);
				instance.ReleasePress();
				check(morePressed && !instance.MoreExpanded() && instance.Activate(moreHit) && instance.MoreExpanded()
					&& instance.Activate(moreHit) && !instance.MoreExpanded(), "activate-more-toggle");
				instance.Activate(moreHit);
				for (int i = 0; i < 40; ++i) ok = paint() && ok;
				const auto closeHit = findHit(PreviewHitKind::Close);
				check(instance.Press(closeHit) && PreviewTestAccess::PressTarget(instance, closeHit) == ButtonPressScale,
					"press-close-scale-target");
				check(instance.SetClosePressed(false) && PreviewTestAccess::PressTarget(instance, closeHit) == 1.0
					&& instance.SetClosePressed(true) && PreviewTestAccess::PressTarget(instance, closeHit) == ButtonPressScale
					&& instance.MoreExpanded(), "close-visual-leave-and-reenter");
				instance.ReleasePress();
				check(PreviewTestAccess::PressTarget(instance, closeHit) == 1.0, "release-close-scale-target");

				// 通过设置页使用的输入状态机，跨越拖动阈值仍由 X 持有同一次按压。
				Setting::ComponentEditor::PreviewPressInput<BarContentInstance, PreviewHit> hostInput;
				bool inputDirty = false;
				auto xPoint = center(closeHit.rect);
				const auto outside = center(findHit(PreviewHitKind::More).rect);
				auto pressX = [&]
				{
					const auto hit = instance.HitTest(xPoint);
					return hostInput.Begin(instance, hit, 0, DragPointerKind::Mouse, xPoint, revision, 1)
						&& hostInput.CloseHeld() && PreviewTestAccess::PressTarget(instance, closeHit) == ButtonPressScale;
				};
				auto moveX = [&](D2D1_POINT_2F point, bool down = true, bool over = true)
				{
					const auto hit = instance.HitTest(point);
					inputDirty = false;
					return hostInput.Update(instance, 0, point, hit, over, down, inputDirty)
						&& hostInput.CloseHeld() && inputDirty;
				};
				auto releaseX = [&](D2D1_POINT_2F point, std::uint64_t currentRevision, bool over = true,
					std::optional<PreviewHit> afterDraw = {})
				{
					const auto presentedHit = instance.HitTest(point);
					return hostInput.Release(instance, 0, point, afterDraw.value_or(presentedHit), presentedHit, over, currentRevision, inputDirty)
						&& hostInput.drag.Phase() == DragPhase::Idle
						&& PreviewTestAccess::PressTarget(instance, closeHit) == 1.0;
				};
				check(instance.HitTest(outside).kind == PreviewHitKind::More
					&& std::hypot(outside.x - xPoint.x, outside.y - xPoint.y) >= 4.f, "host-close-outside-fixture");
				check(!instance.ShouldDismissMoreOnPress(true, xPoint) && pressX(), "host-close-press");
				check(moveX(outside) && PreviewTestAccess::PressTarget(instance, closeHit) == 1.0
					&& instance.MoreExpanded(), "host-close-leave");
				check(moveX(xPoint) && PreviewTestAccess::PressTarget(instance, closeHit) == ButtonPressScale
					&& instance.MoreExpanded(), "host-close-reenter");
				check(moveX(xPoint, false) && PreviewTestAccess::PressTarget(instance, closeHit) == 1.0
					&& instance.MoreExpanded(), "host-close-up-releases-visual");
				inputDirty = false;
				check(releaseX(xPoint, revision) && !instance.MoreExpanded() && inputDirty, "host-close-release-inside");
				instance.SetMoreExpanded(true);
				for (int i = 0; i < 40; ++i) ok = paint() && ok;
				xPoint = center(findHit(PreviewHitKind::Close).rect);
				check(pressX() && moveX(outside) && releaseX(outside, revision) && instance.MoreExpanded(),
					"host-close-release-outside");
				check(pressX() && moveX(xPoint, true, false) && PreviewTestAccess::PressTarget(instance, closeHit) == 1.0
					&& releaseX(xPoint, revision, false) && instance.MoreExpanded(), "host-close-clipped-release");
				check(pressX() && releaseX(xPoint, revision + 1) && instance.MoreExpanded(),
					"host-close-stale-release");
				// 同一次抬起在绘制前后可以命中不同位置，X 只依据绘制前的结果。
				check(pressX() && releaseX(outside, revision, true, instance.HitTest(xPoint))
					&& instance.MoreExpanded(), "host-close-presented-outside");
				inputDirty = false;
				check(pressX() && releaseX(xPoint, revision, true, instance.HitTest(outside))
					&& !instance.MoreExpanded() && inputDirty, "host-close-presented-inside");
				instance.SetMoreExpanded(true);
				for (int i = 0; i < 40; ++i) ok = paint() && ok;
				xPoint = center(findHit(PreviewHitKind::Close).rect);
				const bool pressedForCancel = pressX();
				hostInput.Cancel(instance);
				check(pressedForCancel && hostInput.drag.Phase() == DragPhase::Idle
					&& PreviewTestAccess::PressTarget(instance, closeHit) == 1.0
					&& !releaseX(xPoint, revision) && instance.MoreExpanded(), "host-close-cancel");

				const auto keepOpenHit = findHit(PreviewHitKind::Item, BarComponentId::Control);
				check(PreviewTestAccess::KeepMoreOpenAfterAction(instance, keepOpenHit)
					&& !instance.Activate(keepOpenHit) && instance.MoreExpanded(), "activate-panel-keeps-open");
				check(closeHit.kind == PreviewHitKind::Close && instance.Activate(closeHit) && !instance.MoreExpanded(), "activate-close");
				instance.Activate(moreHit);
				ok = paint() && ok;
				check(instance.Activate(findHit(PreviewHitKind::Item, BarComponentId::Explorer)) && !instance.MoreExpanded(),
					"activate-panel-closes");
				instance.SetMoreExpanded(true);
				for (int i = 0; i < 40; ++i) ok = paint() && ok;
				if (mode == BarSizeMode::Compact)
				{
					auto hintVisible = [&]
					{
						const auto rect = PreviewTestAccess::HintBounds(instance);
						return rect.right > rect.left;
					};
					auto showMoreHint = [&]
					{
						const auto item = findHit(PreviewHitKind::Item, BarComponentId::Explorer);
						input.pointer = center(item.rect);
						++input.pointerSerial;
						ok = paint() && ok;
						const auto deadline = frame.hintDeadline;
						if (deadline) input.time = *deadline;
						ok = paint() && ok;
						return item.position && item.position->segment == Segment::More && deadline && hintVisible();
					};
					// 提示显示时抓起无关的 B1 项，立即观察清理，避免 Advance 的拖动门禁替被测分支清理。
					const Position b1{Segment::B1, model[Segment::B1].size() - 1};
					const bool shownBeforeCancel = showMoreHint();
					const bool liftedForCancel = instance.Lift(b1, revision);
					const bool shownOnCancel = hintVisible();
					instance.CancelLift();
					check(shownBeforeCancel && liftedForCancel && shownOnCancel && !hintVisible(),
						"cancel-lift-clears-visible-hint");
					ok = paint() && ok;
					const bool shownBeforePlace = showMoreHint();
					const bool liftedForPlace = instance.Lift(b1, revision);
					const bool shownOnPlace = hintVisible();
					const bool placed = liftedForPlace && instance.Place(model, b1, revision);
					check(shownBeforePlace && shownOnPlace && placed && !hintVisible(), "place-clears-visible-hint");
					ok = paint() && ok;

					// 保留另一项，让 More 一直展开，避免面板收起的门禁掩盖旧提示。
					const auto hovered = findHit(PreviewHitKind::Item, BarComponentId::Explorer);
					input.pointer = D2D1::Point2F((hovered.rect.left + hovered.rect.right) / 2, (hovered.rect.top + hovered.rect.bottom) / 2);
					++input.pointerSerial;
					ok = paint() && ok;
					const auto deadline = frame.hintDeadline;
					if (deadline) input.time = *deadline;
					ok = paint() && ok;
					const auto shown = PreviewTestAccess::HintBounds(instance);
					check(hovered.position && hovered.position->segment == Segment::More && deadline && shown.right > shown.left,
						"more-item-hint-before-removal");
					model[Segment::More].erase(model[Segment::More].begin());
					instance.SetLayout(model, ++revision);
					ok = paint() && ok;
					const auto cleared = PreviewTestAccess::HintBounds(instance);
					check(instance.MoreExpanded() && !frame.hintDeadline && cleared.right == cleared.left,
						"more-item-hint-cleared-without-pointer-move");
					input.pointer.reset();
				}
				auto moreRows = [&]
				{
					std::vector<float> rows;
					for (const auto& item : instance.Geometry().items)
						if (item.position && item.position->segment == Segment::More
							&& std::find(rows.begin(), rows.end(), item.rect.top) == rows.end())
							rows.push_back(item.rect.top);
					return rows.size();
				};
				const auto priorRows = moreRows();
				// 11 项在大、小尺寸均比单行多两行；检查布局刚替换起的每一帧。
				model[Segment::More].resize(11, {BarComponentId::Explorer});
				instance.SetLayout(model, ++revision);
				bool grownPanelFits = true;
				float firstGrownBottom = 0;
				for (int i = 0; i < 40; ++i)
				{
					ok = paint() && ok;
					const auto panel = findHit(PreviewHitKind::Panel);
					if (i == 0) firstGrownBottom = frame.bounds.bottom;
					grownPanelFits &= panel.kind == PreviewHitKind::Panel && panel.rect.left >= frame.bounds.left
						&& panel.rect.right <= frame.bounds.right && panel.rect.top >= frame.bounds.top
						&& panel.rect.bottom <= frame.bounds.bottom && frame.extent.height <= input.height;
				}
				check(priorRows == 1 && moreRows() == priorRows + 2 && grownPanelFits, "more-grow-two-rows-fits-every-frame");
				check(firstGrownBottom >= frame.bounds.bottom, "more-grow-reserves-target-envelope-immediately");

				// 把可用高度降到稳定包络的一半，宽度和目标栏高保持原值，使高度成为唯一新增的限制。
				const double roomyHeight = input.height;
				const double roomyScale = frame.scale;
				const double roomyExtent = frame.extent.height;
				input.height = std::floor(roomyExtent / 2);
				bool heightScaleCapped = input.height > 0 && input.height < roomyExtent;
				bool constrainedPanelFits = true;
				for (int i = 0; i < 40; ++i)
				{
					ok = paint() && ok;
					const auto panel = findHit(PreviewHitKind::Panel);
					heightScaleCapped &= frame.scale > 0 && frame.scale <= roomyScale * 0.51;
					constrainedPanelFits &= panel.kind == PreviewHitKind::Panel && panel.rect.left >= frame.bounds.left
						&& panel.rect.right <= frame.bounds.right && panel.rect.top >= frame.bounds.top
						&& panel.rect.bottom <= frame.bounds.bottom && frame.extent.height <= input.height;
				}
				check(heightScaleCapped, "more-height-limits-scale-every-frame");
				check(constrainedPanelFits, "more-height-keeps-panel-in-frame");
				input.height = roomyHeight;
				model[Segment::More].resize(1);
				instance.SetLayout(model, ++revision);
				ok = paint() && ok;
				const Position lastMore{Segment::More, 0}, endB1{Segment::B1, model[Segment::B1].size()};
				const auto moved = Move(model, lastMore, endB1, catalog);
				const bool placedLast = moved && instance.Lift(lastMore, revision) && instance.Place(*moved, endB1, revision);
				check(placedLast && !instance.MoreExpanded(), "place-last-more-closes");
				const auto restored = placedLast ? Move(*moved, endB1, lastMore, catalog) : std::nullopt;
				ok = paint() && ok;
				const bool placedBack = restored && instance.Lift(endB1, revision) && instance.Place(*restored, lastMore, revision);
				ok = paint() && ok;
				check(placedBack && !instance.MoreExpanded() && findHit(PreviewHitKind::Panel).kind != PreviewHitKind::Panel,
					"place-more-item-back-stays-closed");

				instance.SetMoreExpanded(true);
				const bool expandedBeforeRebuild = instance.MoreExpanded();
				auto emptyMore = model;
				emptyMore[Segment::More].clear();
				instance.SetLayout(emptyMore, ++revision);
				check(expandedBeforeRebuild && !instance.MoreExpanded(), "rebuild-empty-more-clears-expanded");
				check(ok, "sandbox-render");
			}
		}

		void CheckPreview(const HostScene& scene, Report& report, std::array<Frame,2>& lightSamples)
		{
			const auto assets = Pipeline::GetSharedAssets();
			PreviewInputs input;
			input.time = std::chrono::steady_clock::time_point{} + std::chrono::seconds(1);
			input.width = 1200; input.height = 360;
			input.writeFactory = assets.dwriteFactory.Get(); input.fonts = assets.fontCollection.Get();
			CheckPreviewProjection(input,report);
			CheckPreviewReplica(input,report);
			auto model = Reset();
			model[Segment::B1].push_back({BarComponentId::Explorer});
			model[Segment::B1].push_back({BarComponentId::Explorer});
			const auto catalog = barUISet.barButtonSet.GetLayoutCatalog();
			const auto point = [](D2D1_RECT_F r){ return D2D1::Point2F((r.left+r.right)/2,(r.top+r.bottom)/2); };
			auto check = [&](bool ok,const char* label){report.Check(ok,std::string(scene.name)+".preview."+label);};
			for (const auto mode : {BarSizeMode::Large,BarSizeMode::Compact})
			{
				BarContentInstance a,b,control;
				control.SetLayout(Reset(),9);
				a.SetLayout(model,7); b.SetLayout(Reset(),9);
				input.sizeMode = mode;
				const auto af = a.Advance(input);
				Frame plain, external, gdi;
				const bool painted = PaintPreview(a,af,FrameTargetKind::PlainBitmap,1200,360,plain)
					&& PaintPreview(a,af,FrameTargetKind::ExternalContext,1200,360,external,1)
					&& PaintPreview(a,af,FrameTargetKind::GdiCompatibleBitmap,1200,360,gdi);
				check(painted && plain.bytes == external.bytes && plain.bytes == gdi.bytes
					&& std::any_of(plain.bytes.begin(),plain.bytes.end(),[](BYTE v){return v != 0;}),"targets");
				Frame ghostPixels, bareGhost;
				const bool ghostDrawn = PaintPreview(a,af,FrameTargetKind::ExternalContext,1200,360,bareGhost,0,false,L"")
					&& PaintPreview(a,af,FrameTargetKind::ExternalContext,1200,360,ghostPixels,0,false,L"Hint");
				bool hintVisible = false;
				if (ghostDrawn && bareGhost.Valid() && ghostPixels.Valid())
					for (size_t i=3;i<ghostPixels.bytes.size();i+=4)
						if (bareGhost.bytes[i]==0 && ghostPixels.bytes[i]>64) { hintVisible=true; break; }
				check(hintVisible && ghostPixels.bytes[(180*1200+600)*4+3]>0 && ghostPixels.bytes[3]==0,"ghost-and-hint");
				auto& lightSample = lightSamples[static_cast<size_t>(mode)];
				if (!scene.dark) lightSample = plain;
				else check(lightSample.Valid() && lightSample.bytes!=plain.bytes,"theme-pixels");
				auto insideBudget = [](const Frame& image, const PreviewFrame& frame)
				{
					if (!image.Valid()) return false;
					for (unsigned y=0;y<image.height;++y)
						for (unsigned x=0;x<image.width;++x)
							if (image.bytes[(y*image.width+x)*4+3] &&
								(x==0 || y==0 || x+1==image.width || y+1==image.height ||
								 x+1<frame.bounds.left || x>frame.bounds.right || y+1<frame.bounds.top || y>frame.bounds.bottom))
								return false;
					return true;
				};
				check(insideBudget(plain,af),"shadow-bounds");
				if (!scene.dark && plain.Valid())
				{
					const auto body = a.Geometry().body;
					const auto x = static_cast<unsigned>((body.left+body.right)/2);
					auto alpha = [&](unsigned y){ return plain.bytes[(y*plain.width+x)*4+3]; };
					const auto bottom = static_cast<unsigned>(std::ceil(body.bottom));
					check(bottom+18<plain.height && alpha(bottom+1)>alpha(bottom+18),"shadow-decay");
				}
				const double realZoom = barUISet.barStyle.configZoom;
				barUISet.barStyle.configZoom = realZoom*1.5;
				Frame zoomed;
				const auto afterZoom = a.Advance(input);
				bool zoomOk = PaintPreview(a,afterZoom,FrameTargetKind::PlainBitmap,1200,360,zoomed);
				barUISet.barStyle.configZoom = realZoom;
				check(zoomOk && zoomed.bytes==plain.bytes,"desktop-zoom-independent");
				check(PreviewTestAccess::OwnsInactiveActions(a), "clone-ownership");
				const auto initial = a.Geometry();
				check(std::abs(initial.body.bottom-initial.body.top-input.targetHeight)<0.01
					&& std::abs(initial.body.left+initial.body.right-input.width)<0.01,"scale-center");
				auto duplicate = std::find_if(initial.items.begin(),initial.items.end(),[](const PreviewHit& h)
					{ return h.position && h.position->segment == Segment::B1 && h.position->index == 1; });
				if (duplicate == initial.items.end()) {check(false,"duplicate"); continue;}
				auto source = a.HitTest(point(duplicate->rect));
				check(source.position && source.position->segment == Segment::B1 && source.position->index == 1,"duplicate-hit");
				if (!source.position) continue;
				auto other = input;
				other.width = 240;
				other.pointer = D2D1::Point2F(10,20); other.pointerSerial = 1;
				other.time += std::chrono::milliseconds(1);
				const auto bf = b.Advance(other);
				const auto cf = control.Advance(other);
				Frame before,after;
				check(PaintPreview(b,bf,FrameTargetKind::PlainBitmap,240,360,before) && bf.scale < af.scale
					&& b.Geometry().body.left >= 0 && b.Geometry().body.right <= 240,"narrow");
				Frame expected;
				check(PaintPreview(control,cf,FrameTargetKind::PlainBitmap,240,360,expected)
					&& expected.bytes == before.bytes,"control-first");
				if (b.Geometry().items.empty()) { check(false,"narrow-layout"); continue; }
				const auto bState = PreviewTestAccess::ButtonState(b);
				const auto bSerial = b.Renderer().FontFrameSerial();
				const auto bLight = b.Renderer().SnapshotFrameLighting();
				check(a.Lift(*source.position,7),"lift");
				auto local = input;
				local.time += std::chrono::milliseconds(16);
				local.pointer = D2D1::Point2F(100,90); local.pointerSerial = 3;
				a.SetMoreExpanded(true);
				auto advance = [&]
				{
					local.time += std::chrono::milliseconds(40);
					return a.Advance(local);
				};
				PreviewFrame moving;
				for (int i = 0; i < 24; ++i) moving = advance();
				check(PaintPreview(a,moving,FrameTargetKind::PlainBitmap,1200,360,after),"lift-render");
				check(a.Geometry().body.right-a.Geometry().body.left < initial.body.right-initial.body.left,"shrink");
				check(insideBudget(after,moving),"panel-bounds");
				auto query = [&](Position wanted)->std::optional<Position>
				{
					for (const auto& candidate : a.Geometry().gaps)
					{
						if (candidate.position.segment != wanted.segment || candidate.position.index != wanted.index) continue;
						for (const auto& gap : a.InsertionGaps(D2D1::Point2F((candidate.start.x+candidate.end.x)/2,
							(candidate.start.y+candidate.end.y)/2)))
							if (gap.position.segment == wanted.segment && gap.position.index == wanted.index) return gap.position;
					}
					return {};
				};
				auto panelHit = std::find_if(a.Geometry().items.begin(),a.Geometry().items.end(),
					[](const auto& h){return h.kind==PreviewHitKind::Close;});
				check(panelHit!=a.Geometry().items.end() && a.HitTest(point(panelHit->rect)).kind==PreviewHitKind::Close,"more-close");
				const auto gap = query({Segment::More,model[Segment::More].size()});
				const auto moved = gap ? Move(model,*source.position,*gap,catalog) : std::nullopt;
				check(moved && moved->operator[](Segment::More).back().Id == BarComponentId::Explorer
					&& !a.Place(*moved,*gap,6) && a.Place(*moved,*gap,7),"more-move");
				if (!moved) continue;
				a.SetLayout(*moved,8);
				moving = advance();
				check(PaintPreview(a,moving,FrameTargetKind::ExternalContext,1200,360,after),"place-render");
				const auto destination = query({Segment::B1,0});
				const auto inserted = destination ? Insert(*moved,BarComponentId::Explorer,*destination,catalog) : std::nullopt;
				check(inserted && (*inserted)[Segment::B1].size()==2 && a.Place(*inserted,*destination,8),"insert");
				// 模拟预览宿主切主题；恢复后对照另一实例之前的同一帧。
				Frame originalTheme;
				for (int step=0; step<24; ++step) moving = advance();
				const bool originalPainted = PaintPreview(a,moving,FrameTargetKind::PlainBitmap,1200,360,originalTheme);
				const auto surface = a.Geometry().body;
				barUISet.barStyle.darkStyle = !scene.dark;
				auto opposite = advance();
				Frame themed;
				bool switched = PaintPreview(a,opposite,FrameTargetKind::PlainBitmap,1200,360,themed);
				barUISet.barStyle.darkStyle = scene.dark;
				const size_t surfacePixel = (size_t(surface.top+2)*1200 + size_t((surface.left+surface.right)/2))*4;
				check(originalPainted && switched && originalTheme.Valid() && themed.Valid()
					&& std::memcmp(originalTheme.bytes.data()+surfacePixel,themed.bytes.data()+surfacePixel,3)!=0,"theme-switch");
				const auto light = b.Renderer().SnapshotFrameLighting();
				check(PreviewTestAccess::ButtonState(b)==bState && b.Renderer().FontFrameSerial()==bSerial && light.cursorLight.x==bLight.cursorLight.x
					&& light.primaryLight.y==bLight.primaryLight.y && b.Geometry().frameId==bf.frameId
					&& PaintPreview(b,bf,FrameTargetKind::ExternalContext,240,360,after) && before.bytes==after.bytes,"independent");
				for (unsigned step = 0; step < 3; ++step)
				{
					other.time += std::chrono::milliseconds(15);
					other.pointer = point(b.Geometry().items.front().rect);
					++other.pointerSerial;
					const auto actualFrame = b.Advance(other);
					a.Advance(local);
					const auto controlFrame = control.Advance(other);
					check(PaintPreview(control,controlFrame,FrameTargetKind::PlainBitmap,240,360,expected)
						&& PaintPreview(b,actualFrame,FrameTargetKind::ExternalContext,240,360,after)
						&& after.bytes == expected.bytes && PreviewTestAccess::ButtonState(b)==PreviewTestAccess::ButtonState(control)
						&& b.Renderer().FontFrameSerial()==control.Renderer().FontFrameSerial(),"interleaved-frame");
				}
				local.pointer.reset();
				local.iconOrder = 3;
				a.SetMoreExpanded(false);
				for (int i=0;i<24;++i) moving = advance();
				check(PaintPreview(a,moving,FrameTargetKind::PlainBitmap,1200,360,after),"rtl-render");
				const auto restored = a.Geometry();
				if (restored.items.empty()) { check(false,"rtl-layout"); continue; }
				auto find = [](const PreviewGeometry& geometry, PreviewHitKind kind)
				{
					return std::find_if(geometry.items.begin(),geometry.items.end(),[&](const auto& h){return h.kind==kind;});
				};
				check(find(restored,PreviewHitKind::Panel)==restored.items.end()
					&& restored.items.front().rect.left > restored.items.back().rect.right,"rtl-folded");
				auto target = restored.items.front();
				check(target.position && a.Lift(*target.position,8),"rtl-lift");
				for (int i=0;i<3;++i) moving = advance();
				check(PaintPreview(a,moving,FrameTargetKind::PlainBitmap,1200,360,after) &&
					a.Geometry().body.left > restored.body.left,"lift-in-flight");
				a.CancelLift();
				for (int i=0;i<24;++i) moving = advance();
				check(PaintPreview(a,moving,FrameTargetKind::PlainBitmap,1200,360,after)
					&& std::abs(a.Geometry().body.left-restored.body.left)<0.01,"cancel");
				if (a.Geometry().items.empty()) { check(false,"cancel-layout"); continue; }
				const auto confirmed = a.Geometry().frameId;
				local.pointer = point(a.Geometry().items.front().rect);
				++local.pointerSerial;
				const auto hover = advance();
				// 悬停提示与真实主栏一致，只在小尺寸显示。
				check(hover.hintDeadline.has_value() == (mode == BarSizeMode::Compact),"hover-deadline");
				if (hover.hintDeadline)
				{
					local.time = *hover.hintDeadline - std::chrono::milliseconds(1);
					const auto early = a.Advance(local);
					Frame earlyPixels, shownPixels;
					check(early.hintDeadline && PaintPreview(a,early,FrameTargetKind::PlainBitmap,1200,360,earlyPixels),"hover-499ms");
					local.time += std::chrono::milliseconds(1);
					const auto due = a.Advance(local);
					auto differsAt = [&](int x,int y) {
						if (!earlyPixels.Valid() || !shownPixels.Valid() || x<0 || y<0 || x>=1200 || y>=360) return false;
						const size_t offset = (size_t(y)*1200+x)*4;
						return std::memcmp(earlyPixels.bytes.data()+offset,shownPixels.bytes.data()+offset,4)!=0;
					};
					const auto hint = PreviewTestAccess::HintBounds(a);
					check(!due.hintDeadline && PaintPreview(a,due,FrameTargetKind::PlainBitmap,1200,360,shownPixels)
						&& shownPixels.bytes!=earlyPixels.bytes && differsAt(int(hint.left+4),int((hint.top+hint.bottom)/2))
						&& !differsAt(1,1) && insideBudget(shownPixels,due),"hover-500ms");
					local.mouse = false;
					check(!advance().hintDeadline,"touch");
				}
				auto previous = a.Geometry().frameId;
				FrameTarget unbound;
				moving = advance();
				check(FAILED(a.Draw(unbound,moving)),"invalid-target");
				local.width = 600;
				moving = advance();
				check(PaintPreview(a,moving,FrameTargetKind::ExternalContext,600,360,after,0,true),"end-draw-failed");
				a.ConfirmFrame(confirmed,true);
				check(a.Geometry().frameId==previous,"failed-frame");
			}
		}

		void RunScene(const HostScene& scene, Report& report, const std::filesystem::path& directory,
			std::array<Frame,2>& lightSamples)
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
				CheckPreview(scene,report,lightSamples);
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
				CheckMatchingBarButton(scene,first,plainFirst,report);
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
				std::array<Frame,2> lightSamples;
				if (assets)
					for (const auto& scene : HostScenes)
					{
						try { RunScene(scene, report, directory, lightSamples); }
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
