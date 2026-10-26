module;

#include "../../../IdtMain.h"
#include "../../../IdtConfiguration.h"
#include "../../../IdtState.h"
#include "../../../IdtI18n.h"
#include "../../../resource.h"
#include "../StartupPlacement.h"
#include "Bar.BottomDock.h"
#include "Bar.WindowGeometry.h"
#include "Bar.InteractionWorker.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <crtdbg.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

module Inkeys.UI.Bar;
import :Main;
import :Atomic;
import Inkeys.Display;
import Inkeys.UI.PageControl;
import Inkeys.UI.RenderPipeline;
import Inkeys.Text.Font;

namespace Inkeys::UI::Bar
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		namespace Pipeline = Inkeys::UI::RenderPipeline;
		namespace Pages = Inkeys::UI::PageControl;
		constexpr unsigned WarmFrames = 120, ActiveFrames = 600;
		constexpr unsigned TotalFrames = WarmFrames + ActiveFrames;
		constexpr std::array PageClients{ Pipeline::Client::PptBottomLeft,
			Pipeline::Client::PptBottomRight, Pipeline::Client::PptMiddleLeft,
			Pipeline::Client::PptMiddleRight };
		enum class Workload { Main, Ppt, Whiteboard };
		const char* Name(Workload workload)
		{
			return workload == Workload::Main ? "main" :
				workload == Workload::Ppt ? "ppt" : "whiteboard";
		}
		struct MainInputFeedback
		{
			ButtonPointerState pointer;
			std::atomic<std::uint64_t> desired = 0;
			std::atomic<unsigned> received = 0;
			static void Cancel(const SurfaceInputEvent& event, void* context)
			{
				Handle(event,context);
			}
			static void Handle(const SurfaceInputEvent& event, void* context)
			{
				auto& self = *static_cast<MainInputFeedback*>(context);
				const auto kind = event.message == WM_LBUTTONDOWN ? ButtonPointerEvent::Down
					: event.message == WM_LBUTTONUP ? ButtonPointerEvent::Up
					: event.message == WM_MOUSEMOVE ? ButtonPointerEvent::Move
					: event.message == WM_MOUSELEAVE ? ButtonPointerEvent::Leave : ButtonPointerEvent::Cancel;
				const auto result = self.pointer.Update(kind, event.hitButtonId);
				self.desired.store(result.hover | (result.pressed << 32), std::memory_order_release);
				self.received.fetch_add(1, std::memory_order_release);
			}
		};
		struct InputRegistration
		{
			HWND identity = nullptr;
			~InputRegistration()
			{
				if (identity) { UnregisterInteractionSurface(identity); (void)DrainInteractionSurfaceCallbacks(); }
			}
		};
		double Quantile(std::vector<double> values, double fraction)
		{
			if (values.empty()) return 0;
			std::sort(values.begin(), values.end());
			return values[static_cast<size_t>(std::ceil(fraction * values.size())) - 1];
		}
		double Milliseconds(const Pipeline::RawCaptureReport& report, std::int64_t ticks)
		{
			return static_cast<double>(ticks) * 1000.0 * report.clockPeriodNum
				/ report.clockPeriodDen;
		}
		double ComputeMilliseconds(const Pipeline::FrameDiagnostics& frame)
		{
			double result = 0;
			for (const auto stage : { Pipeline::FrameStage::WakeAndSnapshot,
				Pipeline::FrameStage::DisplayTransition, Pipeline::FrameStage::SubmitTargetsAndLayout,
				Pipeline::FrameStage::AdvanceAnimationsAndDeriveLayout,
				Pipeline::FrameStage::PrepareLightingAndDemand, Pipeline::FrameStage::DirtyAndPrepare })
				result += frame.stageMs[static_cast<size_t>(stage)];
			return result;
		}
		// 只读源 HDC，不运行任何 USER32 提交；GetDC/ReleaseDC/EndDraw 仍由生产事务执行。
		BOOL ReceivePresentation(const UPDATELAYEREDWINDOWINFO& info)
		{
			return info.hdcSrc && info.psize && info.psize->cx > 0
				&& info.psize->cy > 0 && info.pptSrc && info.pblend
				&& info.pblend->AlphaFormat == AC_SRC_ALPHA;
		}
		Pages::OffscreenSnapshot PageSnapshot(Workload workload, unsigned frame)
		{
			Pages::OffscreenSnapshot snapshot;
			snapshot.ppt.layout.session = 1;
			snapshot.ppt.layout.epoch = 1;
			snapshot.ppt.layout.showBottomPair = true;
			snapshot.ppt.layout.showMiddlePair = true;
			snapshot.ppt.targetRevision = frame + 1;
			snapshot.ppt.currentPage = static_cast<int>(frame % 10) + 1;
			snapshot.ppt.totalPage = 100;
			snapshot.ppt.presentationActive = workload == Workload::Ppt;
			snapshot.ppt.presentationVisible = workload == Workload::Ppt;
			auto& whiteboard = snapshot.whiteboard;
			whiteboard.expandedLayoutTarget = whiteboard.active = workload == Workload::Whiteboard;
			whiteboard.currentPage = snapshot.ppt.currentPage;
			whiteboard.totalPage = 100;
			whiteboard.previousEnabled = whiteboard.previousInteractive = whiteboard.currentPage > 1;
			whiteboard.generation = snapshot.ppt.targetRevision;
			const unsigned phase = frame % 120;
			whiteboard.menu = phase < 25 || phase >= 100 ? 0 : phase < 50 ? 1 : 2;
			constexpr std::array<std::uint32_t, 6> colors{
				0x123B32, 0xFFFFFF, 0xF5F1E6, 0xE5E7EB, 0x25282D, 0x172A46 };
			whiteboard.backgroundRgb = colors[frame / 20 % colors.size()];
			whiteboard.pattern = static_cast<int>(frame / 15 % 3);
			return snapshot;
		}
		void WriteRaw(const std::filesystem::path& prefix, const Pipeline::RawCaptureReport& report)
		{
			std::ofstream metadata(prefix.string() + "-metadata.json");
			metadata << "{\"schemaVersion\":" << report.schemaVersion
				<< ",\"clockPeriodNum\":" << report.clockPeriodNum
				<< ",\"clockPeriodDen\":" << report.clockPeriodDen
				<< ",\"runSerial\":" << report.runSerial
				<< ",\"callbackSeen\":" << report.callbackSeen
				<< ",\"callbackDropped\":" << report.callbackDropped
				<< ",\"batchSeen\":" << report.batchSeen
				<< ",\"batchDropped\":" << report.batchDropped
				<< ",\"backend\":\"WARP\",\"nativeUlw\":false,\"warmFrames\":120,\"activeFrames\":600,\"activity\":\"continuous-standard-press-and-page-content\"}\n";
			std::ofstream batches(prefix.string() + "-batches.csv");
			batches << "batch,frame_ticks,begin_ticks,end_ticks,work,executed,requested,continued,retried,valid\n";
			for (const auto& row : report.batches)
				batches << row.batchSerial << ',' << row.frameTimeTicks << ',' << row.beginTicks
					<< ',' << row.endTicks << ',' << row.work << ',' << row.executed << ','
					<< row.requested << ',' << row.continued << ',' << row.retried << ',' << row.validTime << '\n';
			std::ofstream callbacks(prefix.string() + "-callbacks.csv");
			callbacks << "batch,client,frame_ticks,start_ticks,end_ticks,epoch,result,committed,bar_commit_ticks,compute_ms,draw_ms,getdc_ms,ulw_ms,release_ms,end_ms,target_w,target_h,capacity_w,capacity_h,mask_create,mask_hit,mask_slices,zoom\n";
			for (const auto& row : report.callbacks)
			{
				const auto& frame = row.frame;
				callbacks << row.batchSerial << ',' << static_cast<unsigned>(row.client) << ','
					<< row.frameTimeTicks << ',' << row.startTicks << ',' << row.endTicks << ','
					<< row.contextEpoch << ',' << static_cast<unsigned>(row.result) << ','
					<< frame.presentCommitted << ',' << frame.barCommitTicks << ',' << ComputeMilliseconds(frame);
				for (const auto stage : { Pipeline::FrameStage::Draw, Pipeline::FrameStage::GetDC,
					Pipeline::FrameStage::ULW, Pipeline::FrameStage::ReleaseDC, Pipeline::FrameStage::EndDraw })
					callbacks << ',' << frame.stageMs[static_cast<size_t>(stage)];
				callbacks << ',' << frame.targetSize.cx << ',' << frame.targetSize.cy << ','
					<< frame.capacitySize.cx << ',' << frame.capacitySize.cy << ','
					<< frame.light.roundedParentCreate << ',' << frame.light.roundedParentHit
					<< ',' << frame.light.slices << ',' << frame.zoom << '\n';
			}
		}
		int MeasureRun(Workload workload, double scale, unsigned run,
			std::ofstream& summary, const std::filesystem::path& directory, bool& presetsInitialized)
		{
			auto& owner = barUISet;
			// 使用生产全局 owner，兼容仍由 BarState 消费的主栏业务状态；不调用窗口初始化。
			owner.barState.fold = false;
			owner.barState.drawAttribute = owner.barState.moreExpanded = false;
			owner.barWindow.x = owner.barWindow.y = 0;
			owner.barWindow.w = static_cast<unsigned>(1920 * scale);
			owner.barWindow.h = static_cast<unsigned>(1080 * scale);
			(void)owner.InitializeStartupPlacement(Inkeys::UI::StartupPlacement::BottomLeftFloating);
			InitializeUI(owner);
			owner.barStyle.dpiZoom = 1;
			owner.barStyle.configZoom = scale;
			owner.barStyle.zoom = scale;
			if(!presetsInitialized)
			{
				owner.barButtonSet.PresetInitialization();
				owner.barButtonSet.RegisterBuiltInComponents();
				presetsInitialized=true;
			}
			owner.barButtonSet.Load();
			owner.barButtonSet.StateUpdate();
			owner.barState.fold = false;
			ChangeStateModeToPen();
			owner.barMedia.LoadFormat();
			owner.spec.ConfigureLocalizedTypography();
			SetPptPresentationActive(workload == Workload::Ppt);
			SetWhiteboardActive(workload == Workload::Whiteboard);
			owner.UpdateRendering(false);
			const auto buttons=owner.barButtonSet.buttonList.Snapshot();
			const auto* drawPreset=owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Draw)];
			auto* activityButton=owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Geometry)];
			if(!activityButton
				||std::none_of(buttons.begin(),buttons.end(),[&](const auto& button){return button.get()==drawPreset;})
				||std::none_of(buttons.begin(),buttons.end(),[&](const auto& button){return button.get()==activityButton;}))
			{
				std::fputs("[UI3MultiTarget] Input/activity identity is not the rendered registration\n",stderr);return 1;
			}
			auto main = owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
			main->x.SetDirect(160); main->y.SetDirect(800);
			const auto mainFrame = CreateBarOffscreenRenderCallback(owner, ReceivePresentation);
			// 使用与产品相同的 mailbox/dispatcher；这些 HWND 值只是注册身份，从不调用 USER32。
			auto inputWorker = CreateOffscreenInteractionWorker();
			if (!inputWorker) return 1;
			MainInputFeedback mainFeedback;
			const HWND mainIdentity = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x100));
			if (!RegisterInteractionSurface(mainIdentity, 1, MainInputFeedback::Handle, nullptr, MainInputFeedback::Cancel, &mainFeedback, false)) return 1;
			InputRegistration mainRegistration{ mainIdentity };
			Pipeline::Scheduler scheduler;
			if (!mainFrame || !scheduler.ConfigureRawCapture(32768)) return 1;
			std::atomic<unsigned> frameOrdinal = 0;
			std::atomic_bool complete = false;
			std::atomic<unsigned> callbackCount = 0;
			std::int64_t idleExtraCallbacks = -1;
			unsigned submittedInput = 0;
			std::uint64_t mainCulledDraws = 0;
			unsigned visibleMainFrames = 0;
			std::uint64_t appliedFeedback = 0;
			const auto submitMainInput = [&](UINT message, std::uint64_t hit)
			{
				SurfaceInputEvent event; event.window = mainIdentity; event.generation = 1;
				event.message = message; event.hitButtonId = hit;
				if (SubmitSurfaceInput(event)) ++submittedInput;
			};
			bool registered = scheduler.Register(Pipeline::Client::Bar,
				[&](const Pipeline::FrameContext& context)
				{
					++callbackCount;
					const unsigned frame = frameOrdinal.fetch_add(1) + 1;
					// 首批由生产协调器完成浮动启动放置，下一批再按正常状态展开，避免首帧覆盖fixture。
					if(frame==2){owner.barState.fold=false;owner.UpdateRendering(false);}
					if (frame <= TotalFrames)
					{
						const unsigned phase = frame % 120;
						const bool drawOpen=phase>=25&&phase<55,moreOpen=phase>=65&&phase<95;
						if(owner.barState.drawAttribute!=drawOpen||owner.barState.moreExpanded!=moreOpen)
						{
							owner.barState.drawAttribute=drawOpen;owner.barState.moreExpanded=moreOpen;
							owner.UpdateRendering(false);
						}
						if (phase == 5) submitMainInput(WM_MOUSEMOVE, 1);
						if (phase == 12) submitMainInput(WM_LBUTTONDOWN, 1);
						if (phase == 18) submitMainInput(WM_LBUTTONUP, 1);
						if (phase == 35) submitMainInput(WM_MOUSELEAVE, 0);
					}
					else if (frame == TotalFrames + 1)
					{
						owner.barState.drawAttribute = owner.barState.moreExpanded = false;
						owner.UpdateRendering(false);
						submitMainInput(WM_CANCELMODE, 0);
					}
					// 输入线程只发布期望值，所有动画对象仍由当前 render owner 修改。
					const auto desired = mainFeedback.desired.load(std::memory_order_acquire);
					if (desired != appliedFeedback)
					{
						if (auto* button = owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Draw)])
						{
							if (desired & 0xFFFFFFFFULL) (void)StartBarButtonHoverVisual(*button);
							else (void)StopBarButtonHoverVisual(*button, false);
							SetBarButtonPressedVisual(*button, (desired >> 32) != 0);
						}
						appliedFeedback = desired;
					}
					// 主栏中心由生产显示布局持有；在另一枚真实按钮上持续按压动画，保留 Draw 的输入反馈。
					SetBarButtonPressedVisual(*activityButton, frame <= TotalFrames && (frame % 2) != 0);
					const auto result = mainFrame(context);
					if(frame>WarmFrames&&frame<=TotalFrames)
					{
						mainCulledDraws+=owner.spec.GetFrameCulledDrawCount();
						const auto visibleButton=[](const BarButtonClass* button)
						{
							return button&&button->button.enable.val&&button->button.w.val>0&&button->button.h.val>0
								&&button->icon.enable.val&&button->icon.w.val>0&&button->icon.h.val>0&&button->icon.pct.val>0.001
								&&button->name.enable.val&&button->name.size.val>0&&button->name.pct.val>0.001;
						};
						const auto body=owner.shapeMap[BarUISetShapeEnum::MainBar];
						if(!owner.barState.fold&&body&&body->enable.val&&body->pct.val>0.001
							&&visibleButton(drawPreset)&&visibleButton(activityButton))++visibleMainFrames;
					}
					if (frame > TotalFrames) complete.store(true, std::memory_order_release);
					return frame <= TotalFrames && result != Pipeline::FrameResult::DeviceLost
						? Pipeline::FrameResult::Continue : result;
				});
			for (unsigned index = 0; index < PageClients.size(); ++index)
			{
				const bool visible = workload == Workload::Ppt
					|| (workload == Workload::Whiteboard && index < 2);
				const LONG left = (index % 2) ? static_cast<LONG>(1500 * scale) : 0;
				const LONG top = index < 2 ? static_cast<LONG>(950 * scale) : static_cast<LONG>(450 * scale);
				const double logicalWidth = workload == Workload::Whiteboard
					? (index == 0 ? BarWhiteboardPaginationLeftWidthDip : BarWhiteboardPaginationWidthDip)
					: index >= 2 ? BarPptPaginationShortSideDip : BarPptPaginationLongSideDip;
				const double logicalHeight = workload == Workload::Whiteboard ? BarWhiteboardPaginationHeightDip
					: index >= 2 ? BarPptPaginationLongSideDip : BarPptPaginationShortSideDip;
				const LONG width = static_cast<LONG>(std::ceil(logicalWidth * scale));
				const LONG height = static_cast<LONG>(std::ceil(logicalHeight * scale));
				struct PageInputMapping { POINT destination{}; bool ready=false; };
				auto mapping=std::make_shared<PageInputMapping>();
				const HWND identity=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x101+index));
				const auto callback = Pages::CreatePageControlOffscreenRenderCallback(index,
					[&, workload] { return PageSnapshot(workload, (std::min)(frameOrdinal.load(), TotalFrames)); },
					RECT{ left, top, left + width, top + height }, static_cast<float>(scale),
					[mapping](const UPDATELAYEREDWINDOWINFO& info)
					{
						if(!ReceivePresentation(info)||!info.pptDst)return FALSE;
						mapping->destination=*info.pptDst;mapping->ready=true;return TRUE;
					}, identity);
				registered &= scheduler.Register(PageClients[index],
					[&, callback, visible, mapping, identity, left, top, scale, index](const Pipeline::FrameContext& context)
					{
						++callbackCount;
						const auto result = callback(context);
						const unsigned frame=frameOrdinal.load();
						if(visible&&mapping->ready&&frame<=TotalFrames)
						{
							const auto phase=frame%120;
							const UINT message=phase==5?WM_MOUSEMOVE:phase==12?WM_LBUTTONDOWN
								:phase==18?WM_LBUTTONUP:phase==35?WM_MOUSELEAVE:0;
							if(message)
							{
								SurfaceInputEvent event;event.window=identity;event.generation=1;event.message=message;
								event.screenPoint={left+static_cast<LONG>(35*scale)-mapping->destination.x,
									top+static_cast<LONG>(25*scale)-mapping->destination.y};
								(void)SubmitSurfaceInput(event);
								// 输入可能在当前绘制后到达，由同一个调度器安排后续帧消费反馈。
								scheduler.Request(Pipeline::Mask(PageClients[index]));
							}
						}
						// 局部 Scheduler 的光照需求由此显式请求；实际生产回调与提交规则不变。
						return frameOrdinal.load() <= TotalFrames && visible && result == Pipeline::FrameResult::Idle
							? Pipeline::FrameResult::Continue : result;
					});
			}
			const bool started = registered && scheduler.Start([](Clock::time_point now)
				{ return Pipeline::FrameContext{ Pipeline::GetDeviceEpoch(), Pipeline::GetSharedAssets(), now }; });
			if (started)
			{
				scheduler.Request(Pipeline::Mask(Pipeline::Client::Bar) | Pipeline::PptPageMask());
				const auto deadline = Clock::now() + std::chrono::minutes(3);
				while (!complete.load(std::memory_order_acquire) && Clock::now() < deadline)
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				if (complete.load() && workload == Workload::Whiteboard && run == 3)
				{
					// 等共享 hover 的正常淡出结束，再观测真正静置是否还在持续唤醒。
					std::this_thread::sleep_for(std::chrono::seconds(6));
					const auto before = callbackCount.load();
					std::this_thread::sleep_for(std::chrono::seconds(2));
					idleExtraCallbacks = callbackCount.load() - before;
				}
			}
			scheduler.Stop();
			const auto inputDeadline = Clock::now() + std::chrono::seconds(1);
			while (mainFeedback.received.load(std::memory_order_acquire) < submittedInput && Clock::now() < inputDeadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			const auto report = scheduler.TakeRawCapture();
			if (!started || !complete.load() || !report) return 1;
			const auto prefix = directory / (std::string(Name(workload)) + "-"
				+ std::to_string(static_cast<unsigned>(scale * 100)) + "-" + std::to_string(run));
			WriteRaw(prefix, *report);
			int failures = !report->sealed || report->callbackDropped || report->batchDropped
				|| report->invalidCallbacks || report->invalidBatches ? 1 : 0;
			std::vector<double> batchMs, mainMs, computeMs, drawMs, getDcMs, mainIntervals;
			std::uint64_t mainCommits = 0, allCommits = 0, warmMaskCreates = 0;
			std::array<std::uint64_t, 4> pageCommits{};
			std::int64_t previousMainCommit = 0;
			for (const auto& batch : report->batches)
				if (batch.batchSerial > WarmFrames && batch.batchSerial <= TotalFrames)
					batchMs.push_back(Milliseconds(*report, batch.endTicks - batch.beginTicks));
			for (const auto& row : report->callbacks)
			{
				if (row.batchSerial <= WarmFrames || row.batchSerial > TotalFrames) continue;
				const auto& frame = row.frame;
				if (row.batchSerial == 0 || row.batchSerial > report->batches.size()
					|| report->batches[row.batchSerial - 1].frameTimeTicks != row.frameTimeTicks) ++failures;
				if (frame.presentFailed || row.result == Pipeline::FrameResult::DeviceLost || frame.callbackException) ++failures;
				if (row.client != Pipeline::Client::Bar && workload == Workload::Main) ++failures;
				if (row.client == Pipeline::Client::PptMiddleLeft || row.client == Pipeline::Client::PptMiddleRight)
					if (workload == Workload::Whiteboard) ++failures;
				computeMs.push_back(ComputeMilliseconds(frame));
				drawMs.push_back(frame.stageMs[static_cast<size_t>(Pipeline::FrameStage::Draw)]);
				getDcMs.push_back(frame.stageMs[static_cast<size_t>(Pipeline::FrameStage::GetDC)]);
				warmMaskCreates += frame.light.roundedParentCreate;
				allCommits += frame.presentCommitted;
				for(std::size_t i=0;i<PageClients.size();++i)
					if(row.client==PageClients[i])pageCommits[i]+=frame.presentCommitted;
				if (row.client == Pipeline::Client::Bar)
				{
					mainMs.push_back(Milliseconds(*report, row.endTicks - row.startTicks));
					if (std::abs(frame.zoom - scale) > 0.001) ++failures;
					if (frame.hasBarCommitStamp)
					{
						++mainCommits;
						if (previousMainCommit) mainIntervals.push_back(Milliseconds(*report, frame.barCommitTicks - previousMainCommit));
						previousMainCommit = frame.barCommitTicks;
					}
				}
			}
			if (submittedInput < 24 || mainFeedback.received.load(std::memory_order_acquire) != submittedInput) ++failures;
			if (batchMs.size() != ActiveFrames || mainMs.size() != ActiveFrames || mainCommits != ActiveFrames) ++failures;
			for(std::size_t i=0;i<PageClients.size();++i)
			{
				const bool visible=workload==Workload::Ppt||(workload==Workload::Whiteboard&&i<2);
				if(pageCommits[i]!=(visible?ActiveFrames:0))++failures;
			}
			const double p95 = Quantile(batchMs, 0.95), p99 = Quantile(batchMs, 0.99);
			const bool budget = p95 <= 16.67 && p99 <= 33.4;
			if (!budget) ++failures;
			if (idleExtraCallbacks > 0) ++failures;
			if(mainCulledDraws==0||visibleMainFrames!=ActiveFrames)++failures;
			summary << Name(workload) << ',' << scale << ',' << run << ',' << batchMs.size() << ','
				<< mainCommits << ',' << allCommits << ',' << p95 << ',' << p99 << ','
				<< Quantile(mainMs, 0.95) << ',' << Quantile(computeMs, 0.95) << ','
				<< Quantile(drawMs, 0.95) << ',' << Quantile(getDcMs, 0.95) << ','
				<< Quantile(mainIntervals, 0.95) << ',' << Quantile(mainIntervals, 0.99) << ','
				<< warmMaskCreates << ',' << report->callbackDropped << ',' << idleExtraCallbacks << ','
				<< mainFeedback.received.load() << ',' << mainCulledDraws << ',' << visibleMainFrames << ',' << pageCommits[0] << ',' << pageCommits[1] << ','
				<< pageCommits[2] << ',' << pageCommits[3] << ',' << failures << '\n';
			summary.flush();
			std::fprintf(stderr, "[UI3MultiTarget] %s scale=%.2f run=%u batches=%zu p95=%.3f p99=%.3f mainCommits=%llu visible=%u culled=%llu failures=%d\n",
				Name(workload), scale, run, batchMs.size(), p95, p99,
				static_cast<unsigned long long>(mainCommits), visibleMainFrames,
				static_cast<unsigned long long>(mainCulledDraws), failures);
			return failures;
		}
	}

	int RunUi3MultiTargetOffscreenBenchmark()
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(com)) return 2;
		if (FAILED(Pipeline::Initialize())) { CoUninitialize(); return 3; }
		IdtFontFileLoader::IsLoaderInitialized(); IdtFontCollectionLoader::IsLoaderInitialized();
		const std::array<UINT, 4> fonts{ IDR_TTF1, IDR_TTF7, IDR_TTF3, IDR_TTF8 };
		int failures = FAILED(Pipeline::InitializeFontCollection(IdtFontFileLoader::GetLoader(),
			IdtFontCollectionLoader::GetLoader(), fonts)) || !I18n::load(1, L"JSON", L"zh-CN") ? 1 : 0;
		const std::filesystem::path directory = L"TestResults/ui3-shared-runtime";
		std::filesystem::create_directories(directory);
		// 真实 UI 配置归一化仍可写盘，但基准不能改动应用或源码目录中的配置。
		const auto originalGlobalPath=globalPath;
		globalPath=(std::filesystem::absolute(directory)/L"runtime-config").wstring()+L"\\";
		std::filesystem::create_directories(std::filesystem::path(globalPath)/L"Inkeys/Config");
		std::ofstream summary(directory / L"summary.csv");
		summary << "workload,scale,run,batches,main_commits,all_commits,batch_p95_ms,batch_p99_ms,main_p95_ms,compute_p95_ms,draw_p95_ms,getdc_p95_ms,main_commit_interval_p95_ms,main_commit_interval_p99_ms,warm_mask_creates,dropped,idle_extra_callbacks,input_events,main_culled_draws,main_visible_frames,page0_commits,page1_commits,page2_commits,page3_commits,failures\n";
		BarUiAnimationEnabled = true; BarUiAnimationSpeedRate = 1.0;
		BarUiEdgeLightingEnabled = true; BarUiDynamicEdgeLightingEnabled = true;
		BarUiDebugModeEnabled = false;
		bool presetsInitialized=false;
		for (const double scale : { 1.0, 1.5, 2.0 })
			for (const auto workload : { Workload::Main, Workload::Ppt, Workload::Whiteboard })
				for (unsigned run = 1; run <= 3; ++run)
					failures += MeasureRun(workload, scale, run, summary, directory, presetsInitialized);
		barUISet.spec.DiscardDeviceResources();
		Pipeline::Shutdown();
		globalPath=originalGlobalPath;
		CoUninitialize();
		return failures ? 1 : 0;
	}

#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	namespace
	{
		constexpr double WorkspaceCenteringTolerance = 0.000001;
		struct WorkspaceCenteringUlw
		{
			POINT destination{};
			POINT source{};
			SIZE size{};
			RECT dirty{};
			bool fullReplacement = false;
		};
		struct WorkspaceWindowEvent
		{
			unsigned long long order = 0;
			bool directMove = false;
			POINT position{};
			unsigned long long transitionSerial = 0;
			unsigned long long deferredSerial = 0;
			unsigned long long presentedSerial = 0;
			std::string phase;
		};
		struct WorkspaceCenteringStats
		{
			unsigned presented = 0;
			unsigned intermediatePresented = 0;
			unsigned mappingFailures = 0;
			unsigned worstFrame = 0;
			double maximumError = 0.0;
			double maximumIntermediateError = 0.0;
			double minimumWidth = std::numeric_limits<double>::infinity();
			double maximumWidth = 0.0;
			double minimumY = std::numeric_limits<double>::infinity();
			double maximumY = 0.0;
			unsigned verticalMovingFrames = 0;
			double firstIdleYJump = 0.0, maximumYStep = 0.0;
		};

		class WorkspaceCenteringFixture
		{
		public:
			WorkspaceCenteringFixture(double scale, bool left, bool& presetsInitialized,
				std::ofstream& samples, bool negativeOrigin = false)
				: scale_(scale), left_(left), negativeOrigin_(negativeOrigin),
					presetsInitialized_(presetsInitialized), samples_(samples),
					caseName_(std::string(negativeOrigin ? "negative-" : "")
						+ (left ? "left-" : "right-")
						+ std::to_string(static_cast<unsigned>(scale * 100.0)))
			{
			}

			int Run()
			{
				if (negativeOrigin_)
				{
					const auto actual = Inkeys::Display::GetSnapshot();
					if (actual && actual->Primary())
					{
						std::fprintf(stderr,
							"[UI3WorkspaceCentering] case=%s result=SKIP reason=real-display-service\n",
							caseName_.c_str());
						return 0;
					}
				}
				if (!Initialize()) return failures_;
				if (!RunFreezeHideTransition()) return failures_;
				if (!RunPhase("DesktopToPpt", [&] { PublishPptSession(1, true, nullptr); }, true, true)) return failures_;
				if (!RunPhase("LeavePpt", [&] { PublishPptSession(1, false, nullptr); }, true)) return failures_;
				if (!RunPhase("DesktopToWhiteboard", [&] { SetWhiteboardActive(true); }, true)) return failures_;
				if (!RunPhase("LeaveWhiteboard", [&] { SetWhiteboardActive(false); }, true)) return failures_;
				if (!RunPhase("DesktopToPptBeforeSwitch", [&] { PublishPptSession(1, true, nullptr); }, true)) return failures_;
				if (!RunPhase("PptToWhiteboard", [&] { SetWhiteboardActive(true); }, false)) return failures_;
				if (!RunPhase("WhiteboardToPpt", [&] { SetWhiteboardActive(false); }, false)) return failures_;
				if (!RunPhase("FinalLeavePpt", [&] { PublishPptSession(1, false, nullptr); }, true)) return failures_;
				if (!RunRapidReverse() || !RunRestoreAnimationsOff()) return failures_;
				std::fprintf(stderr, "[UI3WorkspaceCentering] case=%s complete failures=%d\n",
					caseName_.c_str(), failures_);
				return failures_;
			}

			int RunDockTransitions(unsigned workspace = 0)
			{
				if (negativeOrigin_)
				{
					const auto actual = Inkeys::Display::GetSnapshot();
					if (actual && actual->Primary())
					{
						std::fprintf(stderr, "[UI3BottomDock] case=%s result=SKIP reason=real-display-service\n", caseName_.c_str());
						return 0;
					}
				}
				if (!Initialize()) return failures_;
				if (!RunRebaseRoundTrip()) return failures_;
				if (!BarUiAnimationEnabled) caseName_ += "-animations-off";
				if (workspace)
				{
					caseName_ += workspace == 1 ? "-ppt" : "-whiteboard";
					SetCurrentPageHasContent(true);
					if (!SetTool(false) || !Settle()) return failures_;
					if (workspace == 1) PublishPptSession(1, true, nullptr);
					else SetWhiteboardActive(true);
					SetCurrentPageHasContent(false);
					if (!SetTool(true)) return failures_;
					for (unsigned frame = 0; frame < 5; ++frame)
						if (!Step("CombinedEnter", nullptr, frame, true)) return failures_;
				}
				const std::array<double, 3> grabRatios{ 33.0 / 81.0, 38.0 / 81.0, 0.5 };
				grabRatio_ = grabRatios[workspace % grabRatios.size()];
				if (!left_ && workspace == 2) grabRatio_ = 1.0;
				if (left_ && workspace == 2) grabRatio_ = 0.0;
				if (negativeOrigin_ && workspace == 2) grabRatio_ = 1.0;
				if (!BarUiAnimationEnabled) grabRatio_ = 38.0 / 81.0;
				activeGrabRatio_ = grabRatio_;
				const auto mainButton = owner_->superellipseMap[BarUISetSuperellipseEnum::MainButton];
				const double initialStroke = mainButton->ft.has_value()
					? (std::max)(0.0, static_cast<double>(mainButton->ft->val)) : 0.0;
				presentedVisibleHeight_ = mainButton->h.val + initialStroke;
				windowEvents_.clear();
				windowEventOrdinal_ = 0;
				activeWindowPhase_ = nullptr;
				directMoveCount_ = 0;
				prematureDirectMoves_ = 0;
				globalBarrierInjections_ = 0;
				unsigned captured = 0, detached = 0, absorbed = 0, shownIndicators = 0, successes = 0;
				struct FixedSurfaceFrame
				{
					unsigned long long session = 0, displaySerial = 0;
					double zoom = 0.0;
					WorkspaceCenteringUlw ulw;
					SIZE target{};
					POINT pointer{};
					bool pointerUnclamped = false;
				};
				std::optional<FixedSurfaceFrame> fixedSurface;
				unsigned fixedFrames = 0, fixedOriginChecks = 0, fixedDownPrepares = 0;
				unsigned fixedPartialPresents = 0, fixedBothTransitions = 0, fixedReleaseCommits = 0;
				double downGrabOffsetX = 0.0, downGrabOffsetY = 0.0;
				double downHalfWidth = 0.0, downHalfHeight = 0.0;
				double downTopPointerOffset = 0.0;
				double lastSuccessfulPointerY = 0.0, lastSuccessfulTop = 0.0;
				const auto effectivePointerY = [&](LONG rawY, const RECT& monitorBounds)
				{
					// 独立按未形变主体和 Down 抓点计算屏幕钳制，不从已呈现映射反推输入。
					const double minimum = monitorBounds.top + downHalfHeight;
					const double maximum = (std::max)(minimum, monitorBounds.bottom - downHalfHeight);
					return std::clamp(static_cast<double>(rawY) - downGrabOffsetY, minimum, maximum)
						+ downGrabOffsetY;
				};
				bool retryNeedsFullReplacement = false;
				const auto screenY = [](const auto& snapshot, double y)
					{ return snapshot.monitorOrigin.y + y * snapshot.zoom + snapshot.directTranslation.y; };
				const auto screenX = [](const auto& snapshot, double x)
					{ return snapshot.monitorOrigin.x + x * snapshot.zoom + snapshot.directTranslation.x; };
				const auto directMove = [&](HWND, int x, int y) -> BOOL
				{
					RecordWindowEvent(true, POINT{ x, y });
					return TRUE;
				};
				const auto beforeMove = [&](const char* phase)
				{
					activeWindowPhase_ = phase;
					if (std::string_view(phase) == "DirectMoveGlobalBarrier")
					{
						// 本手势没有 awaited；另一个写者只发布全局 deferred，不改变两轴候选。
						Access::PublishDeferred(*owner_);
						++globalBarrierInjections_;
					}
				};
				const bool sequence = RunUi3BottomDockInteractionSequence(*owner_, [&](const char* phase, POINT pointer)
				{
					activeWindowPhase_ = phase;
					const auto before = owner_->BottomDockPresentedSnapshot();
					const double beforeRoot = (before.mapping.baseTopDip + before.mapping.baseBottomDip) / 2.0;
					const double beforeTop = screenY(before, before.mapping.visualTopDip);
					if (std::string_view(phase) == "Begin" || std::string_view(phase) == "ReleaseRetry")
					{
						const auto actualAnchor = ResolveBarBottomDockGrabAnchor(before.mapping,
							beforeRoot, presentedVisibleHeight_, pointer.y, before.monitorOrigin.y,
							before.directTranslation.y, before.zoom);
						if (!actualAnchor.valid) return Fail("dock Down did not resolve the actual normalized grab");
						activeGrabRatio_ = actualAnchor.normalizedY;
						fixedSurface.reset();
						downGrabOffsetX = pointer.x - before.mainCenterScreenX;
						const double stroke = mainButton->ft.has_value()
							? (std::max)(0.0, static_cast<double>(mainButton->ft->tar)) : 0.0;
						downHalfWidth = (mainButton->GetW() + stroke) * before.zoom / 2.0;
						downHalfHeight = (before.mapping.baseBottomDip - before.mapping.baseTopDip) * before.zoom / 2.0;
						downGrabOffsetY = (actualAnchor.normalizedY - 0.5) * downHalfHeight * 2.0;
						// 独立保存 Down 顶边相对有效输入的偏移；贴屏幕边缘时 raw pointer 可能已被钳制。
						const double effectiveDownY = effectivePointerY(pointer.y, before.monitorBounds);
						downTopPointerOffset = beforeTop - effectiveDownY;
						lastSuccessfulPointerY = effectiveDownY;
						lastSuccessfulTop = beforeTop;
					}
					const bool injecting = std::string_view(phase).ends_with("Failure");
					failNextPresent_ = injecting;
					presentedUlw_.reset(); attemptedUlw_.reset();
					owner_->UpdateRendering(false);
					lastResult_ = mainFrame_(Pipeline::FrameContext{
						Pipeline::GetDeviceEpoch(), Pipeline::GetSharedAssets(), frameTime_ });
					frameTime_ += std::chrono::milliseconds(16);
					const auto after = owner_->BottomDockPresentedSnapshot();
					if (lastResult_ == Pipeline::FrameResult::DeviceLost || rejectedMappingAttempts_)
						return Fail("dock frame lost device or submitted invalid source/target");
					if (injecting && failNextPresent_) return Fail("requested failure never reached the present sink");
					if (presentedUlw_ && lastResult_ != Pipeline::FrameResult::Continue)
						return Fail("successful sink did not complete the D2D presentation transaction");
					const bool successful = presentedUlw_.has_value()
						&& lastResult_ == Pipeline::FrameResult::Continue && after.serial != before.serial;
					if (injecting)
					{
						if ((std::string_view(phase) != "ReleaseAbsorbFailure" && after.serial != before.serial)
							|| after.mode != before.mode || after.transitionSerial != before.transitionSerial)
							return Fail("failed candidate advanced the successful shape/serial");
						retryNeedsFullReplacement = true;
					}
					if (successful && retryNeedsFullReplacement)
					{
						// 映射稳定时允许完整 client dirty；映射变化才必须 prcDirty=nullptr。
						const auto& retry = *presentedUlw_;
						const bool fullCoverage = retry.fullReplacement || (retry.dirty.left == 0 && retry.dirty.top == 0
							&& retry.dirty.right == retry.size.cx && retry.dirty.bottom == retry.size.cy);
						if (!fullCoverage) return Fail("dock failure retry did not repaint the full window");
						retryNeedsFullReplacement = false;
					}

					if (successful) ++successes;
					const double top = screenY(after, after.mapping.visualTopDip);
					const double bottom = screenY(after, after.mapping.visualBottomDip);
					const double oldHeight = (before.mapping.visualBottomDip - before.mapping.visualTopDip) * before.zoom;
					const double heightError = bottom - top - oldHeight;
					const auto afterButton = owner_->superellipseMap[BarUISetSuperellipseEnum::MainButton];
					const double afterStroke = afterButton->ft.has_value()
						? (std::max)(0.0, static_cast<double>(afterButton->ft->val)) : 0.0;
					const double afterVisibleHeight = afterButton->h.val + afterStroke;
					const double effectiveY = effectivePointerY(pointer.y, after.monitorBounds);
					const double expectedTop = effectiveY + downTopPointerOffset;
					const double topError = top - expectedTop;
					const bool dragFrame = std::string_view(phase) != "ReleaseAbsorbFailure"
						&& std::string_view(phase) != "FinalRelease";
					const bool downFrame = std::string_view(phase) == "Begin"
						|| std::string_view(phase) == "ReleaseRetry";
					if (downFrame && (!successful || !after.dragActive))
						return Fail("Down did not commit the prepared fixed drag surface");
					if (successful && dragFrame)
					{
						if (!after.dragActive) return Fail("held frame lost its active drag identity");
						const auto& ulw = *presentedUlw_;
						const auto pixels = owner_->spec.GetTargetBitmapSize();
						const SIZE target{ static_cast<LONG>(pixels.width), static_cast<LONG>(pixels.height) };
						// 原点期望只取独立输入差；屏幕钳制段不套用未经钳制的 pointer 公式。
						const double rawCenterX = pointer.x - downGrabOffsetX;
						const double rawCenterY = pointer.y - downGrabOffsetY;
						const bool unclamped = rawCenterX >= after.monitorBounds.left + downHalfWidth
							&& rawCenterX <= after.monitorBounds.right - downHalfWidth
							&& rawCenterY >= after.monitorBounds.top + downHalfHeight
							&& rawCenterY <= after.monitorBounds.bottom - downHalfHeight;
						const bool sameSurface = fixedSurface && fixedSurface->session == after.dragSession
							&& fixedSurface->displaySerial == after.displaySerial
							&& std::abs(fixedSurface->zoom - after.zoom) <= 0.000001;
						if (!sameSurface)
							fixedSurface = FixedSurfaceFrame{ after.dragSession, after.displaySerial,
								after.zoom, ulw, target, pointer, unclamped };
						else
						{
							const auto& first = *fixedSurface;
							if (ulw.source.x != first.ulw.source.x || ulw.source.y != first.ulw.source.y
								|| ulw.size.cx != first.ulw.size.cx || ulw.size.cy != first.ulw.size.cy
								|| target.cx != first.target.cx || target.cy != first.target.cy)
								return Fail("held source, window size or target capacity changed within one fixed surface");
							if (unclamped && first.pointerUnclamped)
							{
								const long long expectedX = static_cast<long long>(first.ulw.destination.x)
									+ pointer.x - first.pointer.x;
								const long long expectedY = static_cast<long long>(first.ulw.destination.y)
									+ pointer.y - first.pointer.y;
								if (ulw.destination.x != expectedX || ulw.destination.y != expectedY)
								{
									std::fprintf(stderr, "[UI3BottomDock] case=%s phase=%s fixedOriginError=(%lld,%lld)\n",
										caseName_.c_str(), phase, ulw.destination.x - expectedX, ulw.destination.y - expectedY);
									return Fail("native origin did not follow independent pointer displacement");
								}
								++fixedOriginChecks;
							}
							if (!ulw.fullReplacement) ++fixedPartialPresents;
						}
						++fixedFrames;
						if (downFrame) ++fixedDownPrepares;
						if (std::string_view(phase) == "BothDetach") fixedBothTransitions |= 1u;
						if (std::string_view(phase) == "BothCapture") fixedBothTransitions |= 2u;
					}
					if (successful && std::string_view(phase) == "FinalRelease")
					{
						if (after.dragActive) return Fail("Up did not commit the released surface identity");
						++fixedReleaseCommits;
					}
					if (successful && dragFrame && std::abs(topError) > WorkspaceCenteringTolerance)
					{
						++failures_;
						std::fprintf(stderr, "[UI3BottomDock] case=%s phase=%s grab=%.9f topError=%.9f\n",
							caseName_.c_str(), phase, activeGrabRatio_, topError);
					}
					double transitionTopError = 0.0, transitionBottomError = 0.0;
					if (successful && before.mode != after.mode)
					{
						const bool detaching = before.mode == BarBottomDockMode::BottomDocked
							&& after.mode == BarBottomDockMode::Floating;
						if (detaching) ++detached;
						else if (before.mode == BarBottomDockMode::Floating
							&& after.mode == BarBottomDockMode::BottomDocked) ++captured;
						// before 可能已随 SetWindowPos 平移；仅补上尚未显示的真实指针位移。
						const double inputShift = effectiveY - lastSuccessfulPointerY;
						const double displayedShift = beforeTop - lastSuccessfulTop;
						const double effectiveShift = inputShift - displayedShift;
						transitionTopError = top - (beforeTop + effectiveShift);
						transitionBottomError = bottom - (screenY(before, before.mapping.visualBottomDip) + effectiveShift);
						const double expectedError = BarUiAnimationEnabled
							? (std::max)(std::abs(transitionTopError), std::abs(transitionBottomError))
							: detaching ? std::abs(bottom - top
								- (after.mapping.baseBottomDip - after.mapping.baseTopDip) * after.zoom) : 0.0;
						if (expectedError > WorkspaceCenteringTolerance)
						{
							++failures_;
							std::fprintf(stderr, "[UI3BottomDock] case=%s phase=%s transitionTopError=%.9f transitionBottomError=%.9f heightError=%.9f\n",
								caseName_.c_str(), phase, transitionTopError, transitionBottomError, heightError);
						}
					}
					if (successful && !BarUiAnimationEnabled
						&& std::string_view(phase) == "FinalRelease")
					{
						const bool settled = after.phase == BarBottomDockPhase::Stable
							&& !after.recoveryActive && !after.dragActive
							&& std::abs(after.mapping.visualTopDip - after.mapping.baseTopDip)
								<= WorkspaceCenteringTolerance
							&& std::abs(after.mapping.visualBottomDip - after.mapping.baseBottomDip)
								<= WorkspaceCenteringTolerance
							&& std::abs(after.elasticOffsetDip) <= WorkspaceCenteringTolerance;
						if (!settled) return Fail("animations-off release did not settle to the base mapping");
					}
					if (std::string_view(phase) == "ReleaseAbsorbFailure")
					{
						++absorbed;
						const double error = (std::max)({
							std::abs(top - screenY(before, before.mapping.visualTopDip)),
							std::abs(bottom - screenY(before, before.mapping.visualBottomDip)),
							std::abs(screenX(after, after.horizontalMapping.visualLeftDip) - screenX(before, before.horizontalMapping.visualLeftDip)),
							std::abs(screenX(after, after.horizontalMapping.visualRightDip) - screenX(before, before.horizontalMapping.visualRightDip)) });
						if (error > WorkspaceCenteringTolerance)
						{
							++failures_;
							std::fprintf(stderr, "[UI3BottomDock] case=%s absorbScreenError=%.9f\n", caseName_.c_str(), error);
						}
					}
					if (successful && after.indicatorVisible)
					{
						++shownIndicators;
						const auto& ulw = *presentedUlw_;
						const RECT window{ ulw.destination.x, ulw.destination.y,
							ulw.destination.x + ulw.size.cx, ulw.destination.y + ulw.size.cy };
						if (!ContainsBarWindowRect(window, TranslateBarWindowRect(
							after.indicatorBounds, POINT{ after.monitorOrigin.x + after.directTranslation.x,
								after.monitorOrigin.y + after.directTranslation.y })))
							return Fail("visible indicator escapes submitted window");
					}
					if (successful)
					{
						presentedVisibleHeight_ = afterVisibleHeight;
						lastSuccessfulPointerY = effectiveY;
						lastSuccessfulTop = top;
					}
					const auto target = owner_->spec.GetTargetBitmapSize();
					const auto ulw = attemptedUlw_.value_or(WorkspaceCenteringUlw{});
					samples_ << caseName_ << ',' << phase << ',' << after.serial << ',' << after.transitionSerial
						<< ',' << successful << ',' << static_cast<unsigned>(after.mode) << ',' << static_cast<unsigned>(after.centerMode)
						<< ',' << after.elasticOffsetDip << ',' << top << ',' << bottom << ',' << heightError
						<< ',' << pointer.x << ',' << pointer.y << ',' << after.directTranslation.x << ',' << after.directTranslation.y
						<< ',' << ulw.source.x << ',' << ulw.source.y << ',' << ulw.size.cx << ',' << ulw.size.cy
						<< ',' << target.width << ',' << target.height << ',' << ulw.fullReplacement
						<< ',' << ulw.destination.x << ',' << ulw.destination.y << ',' << ulw.dirty.left << ',' << ulw.dirty.top
						<< ',' << ulw.dirty.right << ',' << ulw.dirty.bottom << ',' << attemptedUlw_.has_value()
						<< ',' << static_cast<unsigned>(after.phase) << ',' << static_cast<unsigned>(after.centerPhase)
						<< ',' << std::chrono::duration<double, std::milli>(frameTime_ - frameOrigin_).count() - 16.0
						<< ',' << static_cast<double>(owner_->shapeMap[BarUISetShapeEnum::MainBar]->w.val)
						<< ',' << static_cast<bool>(BarUiAnimationEnabled)
						<< ',' << activeGrabRatio_ << ',' << expectedTop << ',' << topError
						<< ',' << transitionTopError << ',' << transitionBottomError << '\n';
					return true;
				}, grabRatio_, directMove, beforeMove);
				std::ofstream events(std::filesystem::path(L"TestResults/ui3-bottom-dock")
					/ (caseName_ + "-window-events.csv"));
				events << "order,kind,x,y,transition_serial,deferred_serial,presented_serial,phase\n";
				for (const auto& event : windowEvents_)
					events << event.order << ',' << (event.directMove ? "SetWindowPos" : "ULW")
						<< ',' << event.position.x << ',' << event.position.y << ',' << event.transitionSerial
						<< ',' << event.deferredSerial << ',' << event.presentedSerial << ',' << event.phase << '\n';
				const auto directAfterBarrier = std::count_if(windowEvents_.begin(), windowEvents_.end(),
					[](const auto& event)
					{ return event.directMove && event.phase == "DirectMoveAfterBarrier"; });
				if (!sequence || !captured || detached < 2 || absorbed != 1 || !shownIndicators || successes < 20
					|| injectedPresentFailures_ != 3 || retryNeedsFullReplacement || !events
					|| !directMoveCount_ || !directAfterBarrier || prematureDirectMoves_
					|| globalBarrierInjections_ != 1 || fixedDownPrepares != 2 || fixedFrames < 20
					|| fixedOriginChecks < 10 || !fixedPartialPresents || fixedBothTransitions != 3
					|| fixedReleaseCommits != 1)
					Fail("dock production sequence did not cover its required stages");
				std::fprintf(stderr, "[UI3FixedDrag] case=%s frames=%u down=%u pointerChecks=%u partial=%u both=%u up=%u\n",
					caseName_.c_str(), fixedFrames, fixedDownPrepares, fixedOriginChecks,
					fixedPartialPresents, fixedBothTransitions, fixedReleaseCommits);
				std::fprintf(stderr, "[UI3BottomDock] case=%s capture=%u detach=%u absorb=%u indicator=%u direct=%u premature=%u successes=%u failures=%d\n",
					caseName_.c_str(), captured, detached, absorbed, shownIndicators, directMoveCount_,
					prematureDirectMoves_, successes, failures_);
				return failures_;
			}

		private:
			struct Access : BarUISetClass
			{
				static void PublishDeferred(BarUISetClass& bar)
				{
					(bar.*&Access::BeginBottomDockTransition)();
					(void)(bar.*&Access::FinishBottomDockTransition)(true);
				}
				static unsigned long long Transition(BarUISetClass& bar)
				{
					return (bar.*&Access::bottomDockTransitionSerial).load(std::memory_order_acquire);
				}
				static unsigned long long Deferred(BarUISetClass& bar)
				{
					return (bar.*&Access::bottomDockDeferredTransitionSerial).load(std::memory_order_acquire);
				}
				static unsigned long long Presented(BarUISetClass& bar)
				{
					return (bar.*&Access::bottomDockPresentedTransitionSerial).load(std::memory_order_acquire);
				}
			};

			void RecordWindowEvent(bool directMove, POINT position)
			{
				WorkspaceWindowEvent event;
				event.order = ++windowEventOrdinal_;
				event.directMove = directMove;
				event.position = position;
				event.transitionSerial = Access::Transition(*owner_);
				event.deferredSerial = Access::Deferred(*owner_);
				event.presentedSerial = Access::Presented(*owner_);
				event.phase = activeWindowPhase_ ? activeWindowPhase_ : "";
				windowEvents_.push_back(event);
				if (!directMove) return;
				++directMoveCount_;
				if (event.deferredSerial > event.presentedSerial)
				{
					++prematureDirectMoves_;
					++failures_;
					std::fprintf(stderr, "[UI3BottomDock] case=%s prematureSetWindowPos order=%llu deferred=%llu presented=%llu\n",
						caseName_.c_str(), event.order, event.deferredSerial, event.presentedSerial);
				}
			}

			bool Fail(const char* reason)
			{
				++failures_;
				std::fprintf(stderr, "[UI3WorkspaceCentering] case=%s error=%s\n",
					caseName_.c_str(), reason);
				return false;
			}

			bool RunRebaseRoundTrip()
			{
				struct Access : BarUISetClass
				{
					static void Rebase(BarUISetClass& bar, POINT translation, POINT screen, POINT layout)
					{
						std::lock_guard lock(bar.*&Access::directWindowDragMutex);
						(bar.*&Access::RebaseBottomDockPresentedWindow)(translation, screen, layout);
					}
				};
				const auto before = owner_->BottomDockPresentedSnapshot();
				auto* draw = owner_->barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Draw)];
				if (!draw) return Fail("rebase hit fixture missing");
				const double hitX = draw->button.inhX + draw->button.w.val / 2.0;
				const double hitY = draw->button.inhY + draw->button.h.val / 2.0;
				if (!owner_->HitTestPresentedButton(*draw, hitX, hitY)) return Fail("rebase initial hit missing");
				for (const POINT absorbed : { POINT{ 31, -47 }, POINT{ -53, 29 } })
				{
					{
						Access::Rebase(*owner_, POINT{ before.directTranslation.x - absorbed.x,
							before.directTranslation.y - absorbed.y }, {}, absorbed);
					}
					const auto after = owner_->BottomDockPresentedSnapshot();
					const auto y = [](const auto& snapshot, double dip)
						{ return snapshot.monitorOrigin.y + dip * snapshot.zoom + snapshot.directTranslation.y; };
					const auto x = [](const auto& snapshot, double dip)
						{ return snapshot.monitorOrigin.x + dip * snapshot.zoom + snapshot.directTranslation.x; };
					const bool matches = std::abs(y(after, after.mapping.visualTopDip) - y(before, before.mapping.visualTopDip)) < WorkspaceCenteringTolerance
						&& std::abs(y(after, after.mapping.visualBottomDip) - y(before, before.mapping.visualBottomDip)) < WorkspaceCenteringTolerance
						&& std::abs(x(after, after.horizontalMapping.visualLeftDip) - x(before, before.horizontalMapping.visualLeftDip)) < WorkspaceCenteringTolerance
						&& std::abs(x(after, after.horizontalMapping.visualRightDip) - x(before, before.horizontalMapping.visualRightDip)) < WorkspaceCenteringTolerance
						&& owner_->HitTestPresentedButton(*draw, hitX + absorbed.x / before.zoom, hitY + absorbed.y / before.zoom);
					Access::Rebase(*owner_, before.directTranslation, {}, POINT{ -absorbed.x, -absorbed.y });
					if (!matches) return Fail("layout absorption changed physical endpoints or button hit");
				}
				const POINT moved{ 17, -23 };
				Access::Rebase(*owner_, POINT{ before.directTranslation.x + moved.x,
					before.directTranslation.y + moved.y }, moved, {});
				const auto afterMove = owner_->BottomDockPresentedSnapshot();
				const bool movedCenter = std::abs(afterMove.visualBodyCenterScreenX - before.visualBodyCenterScreenX - moved.x) < WorkspaceCenteringTolerance;
				Access::Rebase(*owner_, before.directTranslation, POINT{ -moved.x, -moved.y }, {});
				return movedCenter || Fail("physical move did not translate the successful visual center");
			}

			bool RunFreezeHideTransition()
			{
				const int originalMode = GetSelectAndClickThroughMode();
				auto* freeze = owner_->barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Freeze)];
				std::ofstream trace(std::filesystem::path(L"TestResults/ui3-workspace-centering")
					/ ("freeze-" + caseName_ + ".csv"));
				trace << "order,mode,frame,x,y,target_x,target_y,anchor_x,anchor_y,alpha,center_error\n";
				if (!freeze || !trace) return Fail("freeze fixture missing");
				const auto originalMembers = owner_->barButtonSet.buttonList.Snapshot();
				for (const bool reverseA2 : { false, true })
				{
					if (reverseA2)
					{
						auto reordered = originalMembers;
						const auto board = std::find_if(reordered.begin(), reordered.end(), [](const auto& b)
							{ return b && b->preset == BarButtonPresetEnum::Whiteboard; });
						const auto freezeAt = std::find_if(reordered.begin(), reordered.end(), [&](const auto& b)
							{ return b.get() == freeze; });
						if (board == reordered.end() || freezeAt == reordered.end()) return Fail("A2 order fixture missing");
						if (board < freezeAt) std::rotate(board, board + 1, freezeAt + 1);
						else
						{
							auto first = freezeAt;
							if (first != reordered.begin() && (*(first - 1))->preset == BarButtonPresetEnum::Pierce) --first;
							std::rotate(first, board, board + 1);
						}
						owner_->barButtonSet.buttonList.Replace(std::move(reordered));
					}
				for (const int mode : { 0, 1 })
				{
					SetSelectAndClickThroughMode(mode);
					SetCurrentPageHasContent(true);
					if (!SetTool(false) || !Settle()) return false;
					const auto members = owner_->barButtonSet.buttonList.Snapshot();
					if (!freeze->IsVisible()) return Fail("desktop Freeze must start visible");
					PublishPptSession(1, true, nullptr);
					SetCurrentPageHasContent(false);
					if (!SetTool(true)) return false;
					unsigned fadingFrames = 0;
					for (unsigned frame = 0; frame < 52; ++frame)
					{
						if (!Step("FreezeHide", nullptr, frame, frame < 25)) return false;
						if (owner_->barButtonSet.buttonList.Snapshot() != members)
							return Fail("Freeze hide changed list membership/order");
						BarButtonClass* anchor = nullptr;
						for (const auto& member : members)
						{
							if (member.get() == freeze) break;
							if (member && member->IsVisible()) anchor = member.get();
						}
						if (!anchor || freeze->IsVisible()) return Fail("PPT Freeze hide/anchor missing");
						const auto presented = owner_->BottomDockPresentedSnapshot();
						const double centerError = presented.visualBodyCenterScreenX
							- ResolveBarBottomDockMonitorCenterScreenX(presented.monitorBounds);
						trace << reverseA2 << ',' << mode << ',' << frame << ',' << freeze->button.x.val << ',' << freeze->button.y.val
							<< ',' << freeze->button.x.tar << ',' << freeze->button.y.tar
							<< ',' << anchor->button.x.tar << ',' << anchor->button.y.tar
							<< ',' << freeze->icon.pct.val << ',' << centerError << '\n';
						if (std::abs(freeze->button.x.tar - anchor->button.x.tar) > WorkspaceCenteringTolerance
							|| std::abs(freeze->button.y.tar - anchor->button.y.tar) > WorkspaceCenteringTolerance)
						{
							std::fprintf(stderr, "[UI3WorkspaceCentering] case=%s freezeMode=%d target=(%.6f,%.6f) anchor=(%.6f,%.6f)\n",
								caseName_.c_str(), mode, static_cast<double>(freeze->button.x.tar), static_cast<double>(freeze->button.y.tar),
								static_cast<double>(anchor->button.x.tar), static_cast<double>(anchor->button.y.tar));
							return Fail("hidden Freeze did not collapse to its visible predecessor");
						}
						if (freeze->icon.pct.val > 0.000001 && freeze->icon.pct.val < 0.999999) ++fadingFrames;
						if (std::abs(centerError) > WorkspaceCenteringTolerance)
							return Fail("Freeze hide lost centered root");
					}
					if (!fadingFrames || freeze->icon.pct.val > 0.000001)
						return Fail("Freeze did not use the existing fade animation");
					PublishPptSession(1, false, nullptr);
					if (!SetTool(false) || !Settle()) return false;
				}
				}
				owner_->barButtonSet.buttonList.Replace(originalMembers);
				SetSelectAndClickThroughMode(originalMode);
				SetCurrentPageHasContent(false);
				owner_->UpdateRendering();
				std::fprintf(stderr, "[UI3WorkspaceCentering] case=%s freezeHide=PASS\n", caseName_.c_str());
				return Settle();
			}

			bool SetTool(bool selection)
			{
				const auto expected = selection ? StateModeSelectEnum::IdtSelection
					: StateModeSelectEnum::IdtPen;
				if (GetStateModeSnapshot().StateModeSelect != expected)
				{
					const bool changed = selection ? ChangeStateModeToSelection() : ChangeStateModeToPen();
					if (!changed && GetStateModeSnapshot().StateModeSelect != expected)
						return Fail(selection ? "selection transition rejected" : "pen transition rejected");
				}
				owner_->UpdateRendering(false);
				return true;
			}

			bool Initialize()
			{
				owner_ = &barUISet;
				SetWhiteboardActive(false);
				PublishPptSession(1, false, nullptr);
				UpdateSceneMonitor(nullptr);
				const auto actualDisplay = Inkeys::Display::GetSnapshot();
				const bool syntheticDisplay = !actualDisplay || !actualDisplay->Primary();
				if (!syntheticDisplay) monitor_ = *actualDisplay->Primary();
				else
				{
					// 无显示服务的严格无窗口环境使用确定的单屏；场景查询不到快照时不会覆盖它。
					const LONG left = negativeOrigin_ ? -1920 : 0;
					const LONG top = negativeOrigin_ ? -240 : 0;
					monitor_.bounds = { left, top, left + 1920, top + 1080 };
					monitor_.workArea = { left, top, left + 1920, top + 1040 };
					monitor_.pixelWidth = 1920;
					monitor_.pixelHeight = 1080;
					monitor_.effectiveDpiX = monitor_.effectiveDpiY = static_cast<UINT>(
						std::lround(USER_DEFAULT_SCREEN_DPI * scale_));
					monitor_.primary = monitor_.fallback = true;
				}
				auto display = std::make_shared<Inkeys::Display::Snapshot>();
				display->generation = actualDisplay ? actualDisplay->generation : 1;
				display->monitors = { monitor_ };
				display->primaryIndex = 0;
				display->virtualBounds = monitor_.bounds;
				display->topology = actualDisplay ? actualDisplay->topology
					: Inkeys::Display::DisplayTopology::Unknown;
				display->fallback = actualDisplay ? actualDisplay->fallback : true;

				owner_->spec.DiscardDeviceResources();
				owner_->barWindow.x = monitor_.bounds.left;
				owner_->barWindow.y = monitor_.bounds.top;
				owner_->barWindow.w = static_cast<unsigned>(monitor_.bounds.right - monitor_.bounds.left);
				owner_->barWindow.h = static_cast<unsigned>(monitor_.bounds.bottom - monitor_.bounds.top);
				owner_->barState.drawAttribute = owner_->barState.moreExpanded = false;
				(void)owner_->InitializeStartupPlacement(
					Inkeys::UI::StartupPlacement::CenteredBottomBar);
				InitializeUI(*owner_);
				owner_->barStyle.dpiZoom = 1.0;
				owner_->barStyle.configZoom = syntheticDisplay ? 1.0 : scale_;
				owner_->barStyle.zoom = scale_;
				owner_->barState.widgetPosition.mainBar = !left_;
				owner_->barState.widgetPosition.primaryBar = false;
				if (!presetsInitialized_)
				{
					owner_->barButtonSet.PresetInitialization();
					owner_->barButtonSet.RegisterBuiltInComponents();
					presetsInitialized_ = true;
				}
				owner_->barButtonSet.Load();
				owner_->barButtonSet.StateUpdate();
				owner_->barMedia.LoadFormat();
				owner_->spec.ConfigureLocalizedTypography();
				SetContentStateUpdatesReady(true);
				SetCurrentPageHasContent(false);
				if (!SetTool(false)) return false;
				(void)owner_->InitializeStartupPlacement(
					Inkeys::UI::StartupPlacement::CenteredBottomBar);
				owner_->barState.widgetPosition.mainBar = !left_;
				owner_->barState.widgetPosition.primaryBar = false;
				owner_->PublishDisplaySnapshot(display);
				serialAtInitialize_ = owner_->BottomDockPresentedSnapshot().serial;
				mainFrame_ = CreateBarOffscreenRenderCallback(*owner_, [&](const UPDATELAYEREDWINDOWINFO& info)
					{
						if (!ReceivePresentation(info) || !info.pptDst)
						{
							++rejectedMappingAttempts_;
							return FALSE;
						}
						WorkspaceCenteringUlw sample;
						sample.destination = *info.pptDst;
						sample.source = *info.pptSrc;
						sample.size = *info.psize;
						sample.fullReplacement = info.prcDirty == nullptr;
						if (info.prcDirty) sample.dirty = *info.prcDirty;
						const auto target = owner_->spec.GetTargetBitmapSize();
						const bool sourceValid = sample.source.x >= 0 && sample.source.y >= 0
							&& static_cast<unsigned long long>(sample.source.x)
								+ static_cast<unsigned long long>(sample.size.cx) <= target.width
							&& static_cast<unsigned long long>(sample.source.y)
								+ static_cast<unsigned long long>(sample.size.cy) <= target.height;
						if (!sourceValid)
						{
							++rejectedMappingAttempts_;
							return FALSE;
						}
						attemptedUlw_ = sample;
						RecordWindowEvent(false, sample.destination);
						if (failNextPresent_)
						{
							failNextPresent_ = false;
							++injectedPresentFailures_;
							return FALSE;
						}
						presentedUlw_ = sample;
						return TRUE;
					});
				if (!mainFrame_) return Fail("offscreen callback creation failed");
				frameTime_ = Clock::now();
				frameOrigin_ = frameTime_;
				if (!Settle()) return false;
				if (left_ && !PrepareLeftBaseline(display)) return false;
				const auto snapshot = owner_->BottomDockPresentedSnapshot();
				const double expected = ResolveBarBottomDockMonitorCenterScreenX(snapshot.monitorBounds);
				if (!snapshot.serial || snapshot.serial == serialAtInitialize_
					|| snapshot.mode != BarBottomDockMode::BottomDocked
					|| snapshot.centerMode != BarBottomDockCenterMode::Centered
					|| snapshot.centerPhase != BarBottomDockPhase::Stable || owner_->barState.fold
					|| owner_->barState.widgetPosition.mainBar != !left_
					|| std::abs(snapshot.visualBodyCenterScreenX - expected) > WorkspaceCenteringTolerance)
					return Fail("initial centered expanded baseline is invalid");
				std::fprintf(stderr, "[UI3WorkspaceCentering] case=%s baseline center=%.9f zoom=%.6f serial=%llu\n",
					caseName_.c_str(), snapshot.visualBodyCenterScreenX, snapshot.zoom,
					static_cast<unsigned long long>(snapshot.serial));
				return true;
			}

			bool PrepareLeftBaseline(const Inkeys::Display::SnapshotPtr& display)
			{
				if (!display || display->monitors.size() != 1)
					return Fail("left setup requires one known monitor");
				const auto mainButton = owner_->superellipseMap[BarUISetSuperellipseEnum::MainButton];
				const auto mainBar = owner_->shapeMap[BarUISetShapeEnum::MainBar];
				const auto presented = owner_->BottomDockPresentedSnapshot();
				if (!mainButton || !mainBar || !presented.serial)
					return Fail("left setup lacks a presented right baseline");
				const double zoom = (std::max)(0.000001, presented.zoom);
				const double buttonStroke = mainButton->ft.has_value()
					? (std::max)(0.0, static_cast<double>(mainButton->ft->val)) : 0.0;
				const double barStroke = mainBar->ft.has_value()
					? (std::max)(0.0, static_cast<double>(mainBar->ft->val)) : 0.0;
				const double monitorCenterLocal = (
					ResolveBarBottomDockMonitorCenterScreenX(presented.monitorBounds)
					- presented.monitorOrigin.x - presented.directTranslation.x) / zoom;
				const auto leftPlacement = ResolveBarBottomDockCenteredRootPlacement(
					monitorCenterLocal, mainButton->w.val + buttonStroke,
					-mainBar->x.val, mainBar->w.val + barStroke);
				if (!leftPlacement.valid) return Fail("left setup target is invalid");
				const double targetRootScreen = presented.monitorOrigin.x
					+ leftPlacement.mainCenterDip * zoom;
				if (targetRootScreen <= presented.mainCenterScreenX)
					return Fail("left setup does not require a rightward free move");

				// Free 下复用生产换屏钳制移动根，再恢复原显示器并由既有自动居中捕获接管。
				owner_->ClearBottomDockCenterForFold();
				owner_->UpdateRendering(false);
				auto shifted = std::make_shared<Inkeys::Display::Snapshot>(*display);
				auto& monitor = shifted->monitors.front();
				const double halfWidth = (mainButton->w.val + buttonStroke) * zoom / 2.0;
				const LONG newLeft = static_cast<LONG>(std::ceil(targetRootScreen - halfWidth));
				const LONG delta = newLeft - monitor.bounds.left;
				const auto shift = [delta](RECT& rect)
					{
						rect.left += delta; rect.right += delta;
					};
				shift(monitor.bounds); shift(monitor.workArea); shift(shifted->virtualBounds);
				owner_->PublishDisplaySnapshot(shifted);
				if (!Settle()) return false;
				owner_->PublishDisplaySnapshot(display);
				if (!Settle()) return false;
				owner_->barState.widgetPosition.mainBar = false;
				owner_->RequestBottomDockCenterAfterExpand();
				owner_->UpdateRendering(false);
				return Settle();
			}

			bool Step(const char* phase, WorkspaceCenteringStats* stats,
				unsigned phaseFrame, bool intermediate)
			{
				presentedUlw_.reset(); attemptedUlw_.reset();
				const auto before = owner_->BottomDockPresentedSnapshot();
				const unsigned presentFailuresBefore = injectedPresentFailures_;
				const bool resumedFromIdle = lastResult_ == Pipeline::FrameResult::Idle;
				const double frameTimeMs = std::chrono::duration<double, std::milli>(
					frameTime_ - frameOrigin_).count();
				const auto result = mainFrame_(Pipeline::FrameContext{
					Pipeline::GetDeviceEpoch(), Pipeline::GetSharedAssets(), frameTime_ });
				frameTime_ += std::chrono::milliseconds(16);
				++frameOrdinal_;
				if (result == Pipeline::FrameResult::DeviceLost)
					return Fail("render device lost");
				lastResult_ = result;
				const auto after = owner_->BottomDockPresentedSnapshot();
				if (injectedPresentFailures_ != presentFailuresBefore)
				{
					if (after.serial != before.serial)
						return Fail("failed present advanced the successful snapshot");
					++observedFailureSerialHolds_;
					verifyFullReplacementRetry_ = true;
					std::fprintf(stderr,
						"[UI3WorkspaceCentering] case=%s phase=%s injectedFailureSerialHeld=%llu\n",
						caseName_.c_str(), phase, static_cast<unsigned long long>(after.serial));
				}
				if (rejectedMappingAttempts_)
				{
					const unsigned rejected = std::exchange(rejectedMappingAttempts_, 0u);
					if (stats) stats->mappingFailures += rejected;
					return Fail("present sink rejected an invalid ULW/source tuple");
				}
				if (after.serial == before.serial) return true;
				if (after.serial < before.serial || !presentedUlw_)
					return Fail("successful serial lacks its ULW tuple");
				const auto target = owner_->spec.GetTargetBitmapSize();
				const auto& ulw = *presentedUlw_;
				const bool sourceValid = ulw.source.x >= 0 && ulw.source.y >= 0
					&& ulw.size.cx > 0 && ulw.size.cy > 0
					&& static_cast<unsigned long long>(ulw.source.x)
						+ static_cast<unsigned long long>(ulw.size.cx) <= target.width
					&& static_cast<unsigned long long>(ulw.source.y)
						+ static_cast<unsigned long long>(ulw.size.cy) <= target.height;
				if (!sourceValid)
				{
					if (stats) ++stats->mappingFailures;
					std::fprintf(stderr,
						"[UI3WorkspaceCentering] case=%s phase=%s frame=%u serial=%llu source=(%ld,%ld) size=(%ld,%ld) target=(%u,%u)\n",
						caseName_.c_str(), phase, phaseFrame,
						static_cast<unsigned long long>(after.serial), ulw.source.x, ulw.source.y,
						ulw.size.cx, ulw.size.cy, target.width, target.height);
					return Fail("pptSrc + psize escapes the real target");
				}
				const bool dirtyValid = ulw.fullReplacement
					|| (ulw.dirty.left >= 0 && ulw.dirty.top >= 0
						&& ulw.dirty.right > ulw.dirty.left && ulw.dirty.bottom > ulw.dirty.top
						&& ulw.dirty.right <= ulw.size.cx && ulw.dirty.bottom <= ulw.size.cy);
				if (!dirtyValid) return Fail("partial present dirty escapes the submitted source");
				if (verifyFullReplacementRetry_)
				{
					if (!ulw.fullReplacement) return Fail("present failure retry was not a full replacement");
					verifyFullReplacementRetry_ = false;
				}
				const auto presentedRootXDip = [](const auto& snapshot)
					{
						return (snapshot.mainCenterScreenX - snapshot.monitorOrigin.x
							- snapshot.directTranslation.x) / snapshot.zoom
							- snapshot.horizontalMapping.rigidGripTranslationXDip;
					};
				if (stats && std::abs(presentedRootXDip(after) - presentedRootXDip(before))
					> WorkspaceCenteringTolerance && !ulw.fullReplacement)
					return Fail("root X movement did not use a full window replacement");
				if (!stats) return true;
				if (after.mode != BarBottomDockMode::BottomDocked
					|| after.centerMode != BarBottomDockCenterMode::Centered
					|| after.centerPhase != BarBottomDockPhase::Stable || owner_->barState.fold)
					return Fail("workspace phase left the expanded stable centered bottom dock");
				const auto mainButton = owner_->superellipseMap[BarUISetSuperellipseEnum::MainButton];
				const auto mainBar = owner_->shapeMap[BarUISetShapeEnum::MainBar];
				if (!mainButton || !mainBar) return Fail("main geometry missing after present");
				const double yStep = std::abs(after.mainCenterScreenY - before.mainCenterScreenY);
				if (stats->presented == 0 && resumedFromIdle) stats->firstIdleYJump = yStep;
				if (yStep > 0.01) ++stats->verticalMovingFrames;
				stats->maximumYStep = (std::max)(stats->maximumYStep, yStep);
				++stats->presented;
				const double expected = ResolveBarBottomDockMonitorCenterScreenX(after.monitorBounds);
				const double error = after.visualBodyCenterScreenX - expected;
				if (std::abs(error) > stats->maximumError)
				{
					stats->maximumError = std::abs(error);
					stats->worstFrame = phaseFrame;
				}
				if (intermediate)
				{
					++stats->intermediatePresented;
					stats->maximumIntermediateError = (std::max)(
						stats->maximumIntermediateError, std::abs(error));
				}
				stats->minimumWidth = (std::min)(stats->minimumWidth,
					static_cast<double>(mainBar->w.val));
				stats->maximumWidth = (std::max)(stats->maximumWidth,
					static_cast<double>(mainBar->w.val));
				stats->minimumY = (std::min)(stats->minimumY, after.mainCenterScreenY);
				stats->maximumY = (std::max)(stats->maximumY, after.mainCenterScreenY);
				samples_ << caseName_ << ',' << phase << ',' << phaseFrame << ',' << frameOrdinal_
					<< ',' << frameTimeMs << ',' << (resumedFromIdle ? 0.0 : 0.016)
					<< ',' << after.serial << ',' << after.transitionSerial << ',' << after.displaySerial
					<< ',' << static_cast<unsigned>(after.mode) << ',' << static_cast<unsigned>(after.centerMode)
					<< ',' << static_cast<unsigned>(after.centerPhase) << ',' << error
					<< ',' << after.visualBodyCenterScreenX << ',' << expected
					<< ',' << after.mainCenterScreenX << ',' << after.mainCenterScreenY
					<< ',' << mainButton->x.val << ',' << mainBar->x.val << ',' << mainBar->w.val
					<< ',' << after.zoom << ',' << after.directTranslation.x << ',' << after.directTranslation.y
					<< ',' << ulw.destination.x << ',' << ulw.destination.y
					<< ',' << ulw.source.x << ',' << ulw.source.y << ',' << ulw.size.cx << ',' << ulw.size.cy
					<< ',' << target.width << ',' << target.height << ',' << ulw.fullReplacement
					<< ',' << ulw.dirty.left << ',' << ulw.dirty.top << ',' << ulw.dirty.right << ',' << ulw.dirty.bottom << '\n';
				return true;
			}

			bool Settle()
			{
				for (unsigned frame = 0; frame < 180; ++frame)
				{
					if (!Step("Settle", nullptr, frame, false)) return false;
					if (frame > 0 && lastResult_ == Pipeline::FrameResult::Idle) return true;
				}
				return Fail("production animation did not settle");
			}

			void Report(const char* phase, const WorkspaceCenteringStats& stats,
				bool requireGeometry, bool expectVerticalMovement)
			{
				const double widthTravel = stats.presented
					? stats.maximumWidth - stats.minimumWidth : 0.0;
				const double verticalTravel = stats.presented
					? stats.maximumY - stats.minimumY : 0.0;
				const bool passed = stats.presented >= (requireGeometry ? 12u : 1u)
					&& stats.maximumError <= WorkspaceCenteringTolerance
					&& (!requireGeometry || stats.intermediatePresented > 0)
					&& (!requireGeometry || widthTravel > 0.01)
					&& (!expectVerticalMovement || (verticalTravel > 0.01
						&& stats.verticalMovingFrames >= 2
						&& stats.firstIdleYJump <= WorkspaceCenteringTolerance
						&& stats.maximumYStep <= verticalTravel * 0.25 + WorkspaceCenteringTolerance));
				if (!passed) ++failures_;
				std::fprintf(stderr,
					"[UI3WorkspaceCentering] case=%s phase=%s presents=%u intermediatePresents=%u widthTravel=%.6f yTravel=%.6f yMovingFrames=%u firstIdleYJump=%.6f maxYStep=%.6f maxCenterError=%.9f intermediateError=%.9f worstFrame=%u mappingFailures=%u result=%s\n",
					caseName_.c_str(), phase, stats.presented, stats.intermediatePresented,
					widthTravel, verticalTravel, stats.verticalMovingFrames, stats.firstIdleYJump,
					stats.maximumYStep, stats.maximumError, stats.maximumIntermediateError,
					stats.worstFrame, stats.mappingFailures, passed ? "PASS" : "FAIL");
			}

			bool RunPhase(const char* phase, const std::function<void()>& activate,
				bool expectVerticalMovement, bool injectPresentFailure = false)
			{
				if (!SetTool(false) || !Settle()) return false;
				WorkspaceCenteringStats stats;
				// 行程从最后成功帧开始；还须检查多个 Y 移动帧，不能把首帧瞬移算作动画。
				const auto before = owner_->BottomDockPresentedSnapshot();
				stats.minimumY = stats.maximumY = before.mainCenterScreenY;
				if (const auto mainBar = owner_->shapeMap[BarUISetShapeEnum::MainBar])
					stats.minimumWidth = stats.maximumWidth = static_cast<double>(mainBar->w.val);
				activate();
				for (unsigned frame = 0; frame < 52; ++frame)
				{
					if (frame == 6 && !SetTool(true)) return false;
					if (frame == 18 && !SetTool(false)) return false;
					if (frame == 10 && injectPresentFailure) failNextPresent_ = true;
					if (!Step(phase, &stats, frame, frame < 25)) return false;
				}
				if (injectPresentFailure && (failNextPresent_ || injectedPresentFailures_ != 1
					|| observedFailureSerialHolds_ != 1 || verifyFullReplacementRetry_))
					return Fail("injected present failure did not complete full retry recovery");
				Report(phase, stats, true, expectVerticalMovement);
				return true;
			}

			bool RunRapidReverse()
			{
				if (!SetTool(false) || !Settle()) return false;
				const auto runShort = [&](const char* phase, const std::function<void()>& activate,
					unsigned frames)
					{
						WorkspaceCenteringStats stats;
						activate();
						for (unsigned frame = 0; frame < frames; ++frame)
							if (!Step(phase, &stats, frame, true)) return false;
						const bool finalLeave = std::string_view(phase).starts_with("RapidFinalLeave");
						Report(phase, stats, false, finalLeave);
						return true;
					};
				if (!runShort("RapidEnterPpt", [&] { PublishPptSession(1, true, nullptr); }, 6)) return false;
				if (!runShort("RapidPptToWhiteboard", [&] { SetWhiteboardActive(true); }, 6)) return false;
				if (!runShort("RapidWhiteboardToPpt", [&] { SetWhiteboardActive(false); }, 6)) return false;
				if (!runShort("RapidFinalLeave", [&] { PublishPptSession(1, false, nullptr); }, 52)) return false;
				if (!runShort("RapidReenterPpt", [&] { PublishPptSession(1, true, nullptr); }, 6)) return false;
				if (!runShort("RapidLeaveBeforeReenter", [&] { PublishPptSession(1, false, nullptr); }, 6)) return false;
				if (!runShort("RapidReenterDuringLeave", [&] { PublishPptSession(1, true, nullptr); }, 6)) return false;
				if (!runShort("RapidFinalLeaveAfterReenter", [&] { PublishPptSession(1, false, nullptr); }, 52)) return false;
				return SetTool(false) && Settle();
			}

			bool RunRestoreAnimationsOff()
			{
				if (!SetTool(false) || !Settle()) return false;
				const double desktopY = owner_->BottomDockPresentedSnapshot().mainCenterScreenY;
				struct RestoreAnimation
				{
					bool enabled = static_cast<bool>(BarUiAnimationEnabled);
					~RestoreAnimation() { BarUiAnimationEnabled = enabled; }
				} restoreAnimation;
				BarUiAnimationEnabled = false;
				PublishPptSession(1, true, nullptr);
				if (!Settle()) return false;
				PublishPptSession(1, false, nullptr);
				if (!Step("AnimationsOffLeave", nullptr, 0, false)) return false;
				if (std::abs(owner_->BottomDockPresentedSnapshot().mainCenterScreenY - desktopY)
					> WorkspaceCenteringTolerance) return Fail("animations-off Leave did not restore Y immediately");
				return Settle();
			}

			double scale_ = 1.0;
			bool left_ = false;
			bool negativeOrigin_ = false;
			bool& presetsInitialized_;
			std::ofstream& samples_;
			std::string caseName_;
			BarUISetClass* owner_ = nullptr;
			Inkeys::Display::MonitorInfo monitor_{};
			std::function<Pipeline::FrameResult(const Pipeline::FrameContext&)> mainFrame_;
			std::optional<WorkspaceCenteringUlw> presentedUlw_, attemptedUlw_;
			Clock::time_point frameTime_{};
			Clock::time_point frameOrigin_{};
			Pipeline::FrameResult lastResult_ = Pipeline::FrameResult::Idle;
			unsigned frameOrdinal_ = 0;
			unsigned rejectedMappingAttempts_ = 0;
			unsigned injectedPresentFailures_ = 0;
			unsigned observedFailureSerialHolds_ = 0;
			unsigned long long serialAtInitialize_ = 0;
			int failures_ = 0;
			bool failNextPresent_ = false;
			bool verifyFullReplacementRetry_ = false;
			double grabRatio_ = 0.5;
			double activeGrabRatio_ = 0.5;
			double presentedVisibleHeight_ = 81.0;
			std::vector<WorkspaceWindowEvent> windowEvents_;
			unsigned long long windowEventOrdinal_ = 0;
			const char* activeWindowPhase_ = nullptr;
			unsigned directMoveCount_ = 0;
			unsigned prematureDirectMoves_ = 0;
			unsigned globalBarrierInjections_ = 0;
		};
	}

	static int RunUi3GeometryTests(bool bottomDock)
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(com)) return 2;
		if (FAILED(Pipeline::Initialize())) { CoUninitialize(); return 3; }
		IdtFontFileLoader::IsLoaderInitialized();
		IdtFontCollectionLoader::IsLoaderInitialized();
		const std::array<UINT, 4> fonts{ IDR_TTF1, IDR_TTF7, IDR_TTF3, IDR_TTF8 };
		const HRESULT fontResult = Pipeline::InitializeFontCollection(
			IdtFontFileLoader::GetLoader(), IdtFontCollectionLoader::GetLoader(), fonts);
		const std::filesystem::path directory = bottomDock
			? L"TestResults/ui3-bottom-dock" : L"TestResults/ui3-workspace-centering";
		std::filesystem::create_directories(directory);
		const auto originalGlobalPath = globalPath;
		globalPath = (std::filesystem::absolute(directory) / L"runtime-config").wstring() + L"\\";
		std::filesystem::create_directories(std::filesystem::path(globalPath) / L"Inkeys/Config");
		std::ofstream samples(directory / L"samples.csv");
		if (bottomDock) samples << "case,phase,serial,transition_serial,successful,mode,center_mode,elastic,top,bottom,height_error,pointer_x,pointer_y,direct_x,direct_y,source_x,source_y,window_w,window_h,target_w,target_h,full_replacement,dest_x,dest_y,dirty_left,dirty_top,dirty_right,dirty_bottom,present_attempted,dock_phase,center_phase,frame_time_ms,main_bar_width,animations_enabled,grab_ratio,expected_top,top_error,transition_top_error,transition_bottom_error\n";
		else samples << "case,phase,phase_frame,attempt,frame_time_ms,animation_dt_s,presented_serial,transition_serial,display_serial,mode,center_mode,center_phase,center_error_px,visual_center_px,expected_center_px,main_center_x,main_center_y,main_button_x,main_bar_x,main_bar_width,zoom,direct_x,direct_y,dest_x,dest_y,source_x,source_y,window_w,window_h,target_w,target_h,full_replacement,dirty_left,dirty_top,dirty_right,dirty_bottom\n";
		BarUiAnimationEnabled = true;
		BarUiAnimationSpeedRate = 1.0;
		BarUiEdgeLightingEnabled = true;
		BarUiDynamicEdgeLightingEnabled = true;
		BarUiDebugModeEnabled = false;
		bool presetsInitialized = false;
		int failures = FAILED(fontResult) || !I18n::load(1, L"JSON", L"zh-CN") || !samples ? 1 : 0;
		if (!failures)
			for (const double scale : { 1.0, 1.5 })
				for (const bool left : { false, true })
				{
					WorkspaceCenteringFixture fixture(scale, left, presetsInitialized, samples);
					if (bottomDock)
					{
						for (unsigned workspace = 0; workspace < 3; ++workspace)
						{
							WorkspaceCenteringFixture dockFixture(scale, left, presetsInitialized, samples);
							failures += dockFixture.RunDockTransitions(workspace);
						}
					}
					else failures += fixture.Run();
				}
		if (!failures)
			for (const bool left : { false, true })
			{
				WorkspaceCenteringFixture fixture(1.0, left, presetsInitialized, samples, true);
				if (bottomDock)
				{
					for (unsigned workspace = 0; workspace < 3; ++workspace)
					{
						WorkspaceCenteringFixture dockFixture(1.0, left, presetsInitialized, samples, true);
						failures += dockFixture.RunDockTransitions(workspace);
					}
				}
				else failures += fixture.Run();
			}
		if (bottomDock && !failures)
		{
			BarUiAnimationEnabled = false;
			WorkspaceCenteringFixture fixture(1.0, false, presetsInitialized, samples);
			failures += fixture.RunDockTransitions();
			BarUiAnimationEnabled = true;
		}
		barUISet.spec.DiscardDeviceResources();
		Pipeline::Shutdown();
		globalPath = originalGlobalPath;
		CoUninitialize();
		std::fprintf(stderr, "[UI3WorkspaceCentering] summary failures=%d result=%s\n",
			failures, failures ? "FAIL" : "PASS");
		return failures ? 1 : 0;
	}
	int RunUi3WorkspaceCenteringTest() { return RunUi3GeometryTests(false); }
	int RunUi3BottomDockTransitionTest() { return RunUi3GeometryTests(true); }
#else
	int RunUi3BottomDockTransitionTest() { return 2; }
	int RunUi3WorkspaceCenteringTest()
	{
		return 2;
	}
#endif
}
