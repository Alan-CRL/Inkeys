module;

#include "../../../IdtMain.h"
#include "../../../IdtConfiguration.h"
#include "../../../IdtState.h"
#include "../../../IdtI18n.h"
#include "../../../resource.h"
#include "../StartupPlacement.h"
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
#include <string>
#include <thread>
#include <vector>

module Inkeys.UI.Bar;
import :Main;
import :Atomic;
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
			Inkeys::Testing::WorkspaceSceneScope workspaceScope(workload == Workload::Whiteboard ? WorkspaceScene::Whiteboard :
				workload == Workload::Ppt ? WorkspaceScene::Presentation : WorkspaceScene::Desktop);
			if (!workspaceScope) return 1;
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
}
