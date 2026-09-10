#include "Bar.BottomDockTrace.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <locale>
#include <limits>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

namespace Inkeys::UI::Bar::BottomDockTrace
{
	namespace
	{
		struct PreserveLastError
		{
			DWORD value = GetLastError();
			~PreserveLastError() { SetLastError(value); }
		};
		const char* Name(Event event) noexcept
		{
			constexpr const char* names[]{ "gesture_start", "gesture_basis", "pointer",
				"environment_rebase", "direct_move", "gesture_end", "frame_snapshot",
				"vertical_mapping", "invalidated", "submit", "present_result", "committed",
				"absorb_before", "absorb_after" };
			const auto index = static_cast<std::size_t>(event);
			return index < std::size(names) ? names[index] : "unknown";
		}
		void Number(std::ostream& out, double value)
		{
			if (std::isfinite(value)) out << value;
			else out << "null";
		}
		template<std::size_t N>
		void Array(std::ostream& out, const std::array<double, N>& values)
		{
			out << '[';
			for (std::size_t i = 0; i < N; ++i)
			{
				if (i) out << ',';
				Number(out, values[i]);
			}
			out << ']';
		}
		void Point(std::ostream& out, POINT value) { out << '[' << value.x << ',' << value.y << ']'; }
		void Size(std::ostream& out, SIZE value) { out << '[' << value.cx << ',' << value.cy << ']'; }
		void Rect(std::ostream& out, RECT value)
		{
			out << '[' << value.left << ',' << value.top << ',' << value.right << ',' << value.bottom << ']';
		}
	}

	std::int64_t Now() noexcept
	{
		PreserveLastError preserve;
		LARGE_INTEGER value{};
		QueryPerformanceCounter(&value);
		return value.QuadPart;
	}
	std::int64_t Frequency() noexcept
	{
		PreserveLastError preserve;
		static const auto frequency = []
		{
			LARGE_INTEGER value{};
			QueryPerformanceFrequency(&value);
			return std::max<std::int64_t>(1, value.QuadPart);
		}();
		return frequency;
	}
	bool ReadWindow(HWND window, RECT& bounds) noexcept
	{
		PreserveLastError preserve;
		return GetWindowRect(window, &bounds) != FALSE;
	}

	std::string Serialize(const Record& r, std::uint64_t runId)
	{
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << std::setprecision(17) << std::boolalpha
			<< "{\"schema\":1,\"run_id\":" << runId << ",\"gesture_id\":" << r.gesture
			<< ",\"seq\":" << r.sequence << ",\"event\":\"" << Name(r.event)
			<< "\",\"device_generation\":" << r.deviceGeneration
			<< ",\"qpc\":" << r.qpc << ",\"thread\":" << r.thread << ",\"frame\":" << r.frame
			<< ",\"snapshot_qpc\":" << r.snapshotQpc << ",\"submit_qpc\":" << r.submitQpc
			<< ",\"groups\":" << r.groups << ",\"flags\":" << r.flags << ",\"reason\":" << r.reason
			<< ",\"dropped\":" << r.dropped << ",\"serial\":{\"frame\":" << r.consumedSerial
			<< ",\"tagged\":" << r.taggedSerial << ",\"before\":" << r.observedBeforeSerial
			<< ",\"current\":" << r.currentSerial << ",\"deferred\":" << r.deferredSerial
			<< ",\"presented\":" << r.presentedSerial << ",\"display\":" << r.displaySerial << '}';
		if (r.groups & State)
		{
			out << ",\"state\":{\"mode\":" << r.mode << ",\"phase\":" << r.phase
			<< ",\"center_mode\":" << r.centerMode << ",\"center_phase\":" << r.centerPhase
			<< ",\"drag\":" << r.drag << ",\"recovery\":" << r.recovery
			<< ",\"tool\":" << r.tool << ",\"opens_right\":" << r.opensRight
				<< ",\"elastic_dip\":";
			Array(out, r.elasticInputDip); out << '}';
		}
		if (r.groups & Environment)
		{
			out << ",\"env\":{\"monitor\":"; Rect(out, r.monitor);
			out << ",\"work\":"; Rect(out, r.workArea);
			out << ",\"origin\":"; Point(out, r.monitorOrigin);
			out << ",\"dpi\":" << r.dpi << ",\"config_zoom\":"; Number(out, r.configZoom);
			out << ",\"zoom\":"; Number(out, r.zoom);
			out << ",\"dock_line\":"; Number(out, r.dockLine);
			out << ",\"inset_dip\":"; Number(out, r.insetDip);
			out << ",\"dpi_scale\":"; Number(out, r.dpiScale); out << '}';
		}
		if (r.groups & Input)
		{
			out << ",\"input\":{\"pointer\":"; Array(out, r.pointer);
			out << ",\"grab_offset\":"; Array(out, r.grabOffset);
			out << ",\"logical_down\":"; Array(out, r.logicalDown);
			out << ",\"normalized_down\":"; Array(out, r.normalizedDown);
			out << ",\"raw_grip\":"; Array(out, r.rawGrip);
			out << ",\"basis_frame\":" << r.basisFrame << ",\"basis_qpc\":" << r.basisQpc << '}';
		}
		if (r.groups & Geometry)
		{
			out << ",\"geometry\":{\"root\":"; Array(out, r.root);
			out << ",\"main_size\":"; Array(out, r.mainSize);
			out << ",\"base_size\":"; Number(out, r.baseSize);
			out << ",\"stroke\":"; Number(out, r.stroke);
			out << ",\"base_y\":"; Array(out, r.baseY);
			out << ",\"visual_y\":"; Array(out, r.visualY);
			out << ",\"scale_y\":"; Number(out, r.scaleY);
			out << ",\"translation_y\":"; Number(out, r.translationY);
			out << ",\"horizontal\":"; Array(out, r.horizontal);
			out << ",\"raw_grip_screen\":"; Array(out, r.rawGrip);
			out << ",\"bar_bounds\":"; Array(out, r.barBounds);
			out << ",\"bar_stroke\":"; Number(out, r.barStroke);
			out << ",\"grab_solver\":";
			if (r.grabSolverValid)
			{
				out << "{\"normalized_y\":"; Number(out, r.grabNormalizedY);
				out << ",\"raw_screen_y\":"; Number(out, r.grabRawPointerScreenY);
				out << ",\"logical_dip\":"; Number(out, r.grabSolverDip[0]);
				out << ",\"desired_dip\":"; Number(out, r.grabSolverDip[1]);
				out << ",\"effective_dip\":"; Number(out, r.grabSolverDip[2]);
				out << ",\"constrained\":" << r.grabConstrained << '}';
			}
			else out << "null";
			out << '}';
		}
		if (r.groups & Spring)
		{
			out << ",\"spring\":{\"grip\":"; Array(out, r.gripSpring);
			out << ",\"capture\":"; Array(out, r.captureSpring);
			out << ",\"grip_before\":"; Array(out, r.gripBefore);
			out << ",\"capture_before\":"; Array(out, r.captureBefore);
			out << ",\"seed_screen\":"; Array(out, r.seedScreen); out << '}';
		}
		if (r.groups & Timing)
		{
			out << ",\"timing\":{\"raw_dt\":"; Number(out, r.rawDt);
			out << ",\"frame_dt\":"; Number(out, r.frameDt);
			out << ",\"integrated_dt\":"; Number(out, r.integratedDt);
			out << ",\"grip_dt\":"; Number(out, r.gripDt);
			out << ",\"capture_dt\":"; Number(out, r.captureDt);
			out << ",\"animation_speed\":"; Number(out, r.animationSpeed); out << '}';
		}
		if (r.groups & Window)
		{
			out << ",\"window\":{\"desired\":"; Point(out, r.desired);
			out << ",\"actual\":"; Point(out, r.actual);
			out << ",\"frame_translation\":"; Point(out, r.frameTranslation);
			out << ",\"viewport\":"; Rect(out, r.viewport);
			out << ",\"capacity_origin\":"; Point(out, r.capacityOrigin);
			out << ",\"capacity_size\":"; Size(out, r.capacitySize);
			out << ",\"source\":"; Point(out, r.source);
			out << ",\"destination\":"; Point(out, r.destination);
			out << ",\"size\":"; Size(out, r.windowSize);
			out << ",\"cached\":"; Rect(out, r.cachedWindow);
			out << ",\"os\":"; Rect(out, r.osWindow); out << '}';
		}
		if (r.groups & PresentedState)
		{
			const auto& s = r.presentedState;
			out << ",\"presented_snapshot\":{\"origin\":"; Point(out, s.origin);
			out << ",\"zoom\":"; Number(out, s.zoom);
			out << ",\"direct_translation\":"; Point(out, s.directTranslation);
			out << ",\"base_y\":"; Array(out, s.baseY);
			out << ",\"visual_y\":"; Array(out, s.visualY);
			out << ",\"main_center_screen\":"; Array(out, s.mainCenterScreen);
			out << ",\"raw_main_center_screen_x\":"; Number(out, s.rawMainCenterScreenX);
			out << ",\"raw_body_center_screen_x\":"; Number(out, s.rawBodyCenterScreenX);
			out << ",\"transition_serial\":" << s.transitionSerial << ",\"mapping_serial\":" << s.mappingSerial
				<< ",\"display_serial\":" << s.displaySerial << ",\"main_height\":";
			if (s.mainHeightDip > 0.0) Number(out, s.mainHeightDip);
			else out << "null";
			out << '}';
		}
		if (r.groups & Result)
		{
			out << ",\"result\":{\"resource_hr\":" << r.resourceHr << ",\"get_dc\":" << r.getDc << ",\"ulw\":" << r.ulw
			<< ",\"win_error\":" << r.winError << ",\"release_dc\":" << r.releaseDc
			<< ",\"end_draw\":" << r.endDraw << ",\"committed\":" << r.committed
			<< ",\"stage_qpc\":[" << r.stageQpc[0] << ',' << r.stageQpc[1] << ','
			<< r.stageQpc[2] << ',' << r.stageQpc[3] << "]}";
		}
		out << "}\n";
		return out.str();
	}

	bool IsOwnedFilename(const std::filesystem::path& filename)
	{
		static const std::wregex pattern(
			LR"(^bar-bottom-dock-trace-[0-9]{8}T[0-9]{6}Z-p[0-9]+-r[0-9]+-[0-9]+\.jsonl$)");
		return std::regex_match(filename.filename().wstring(), pattern);
	}
	bool ResolveInitialGrab(Record& start, const Record& basis, const RECT& actualWindow) noexcept
	{
		if (!(basis.groups & Geometry) || !(basis.groups & Window) || basis.frame == 0
			|| !std::isfinite(basis.zoom) || basis.zoom <= 0.0
			|| !std::isfinite(basis.mainSize[0]) || !std::isfinite(basis.mainSize[1])
			|| basis.mainSize[0] <= 0.0 || basis.mainSize[1] <= 0.0
			|| !std::isfinite(basis.scaleY) || basis.scaleY <= 0.0
			|| actualWindow.right <= actualWindow.left || actualWindow.bottom <= actualWindow.top) return false;
		// 用真实 HWND 与成功位图 viewport 反推屏幕映射；不把 ABSORB 后的旧端点误作新布局。
		const double tx = actualWindow.left - basis.monitorOrigin.x - basis.capacityOrigin.x - basis.source.x;
		const double ty = actualWindow.top - basis.monitorOrigin.y - basis.capacityOrigin.y - basis.source.y;
		const double visualX = (start.pointer[0] - basis.monitorOrigin.x - tx) / basis.zoom;
		const double visualY = (start.pointer[1] - basis.monitorOrigin.y - ty) / basis.zoom;
		start.logicalDown = { visualX - basis.horizontal[5],
			basis.baseY[0] + (visualY - basis.visualY[0]) / basis.scaleY };
		start.normalizedDown = {
			(start.logicalDown[0] - basis.root[0]) / basis.mainSize[0] + 0.5,
			(start.logicalDown[1] - basis.root[1]) / basis.mainSize[1] + 0.5 };
		if (!std::isfinite(start.normalizedDown[0]) || !std::isfinite(start.normalizedDown[1])) return false;
		start.basisFrame = basis.frame;
		start.basisQpc = basis.qpc;
		start.flags |= BasisAvailable | GrabPointValid;
		return true;
	}

	struct Recorder::Impl
	{
		Buffer<2048> buffer;
		std::atomic<bool> accepting{ true };
		std::atomic<std::uint64_t> gesture{ 0 }, written{ 0 }, probeMisses{ 0 }, writerErrors{ 0 }, writerDropped{ 0 };
		std::atomic<std::int64_t> deadline{ 0 };
		std::atomic<std::uint32_t> producers{ 0 };
		std::atomic_flag probeLock = ATOMIC_FLAG_INIT;
		std::atomic<bool> probeTrusted{ false };
		Record probe{};
		bool probeValid = false;
		std::filesystem::path directory;
		Limits limits;
		std::uint64_t runId = static_cast<std::uint64_t>(Now());
		std::int64_t frequency = Frequency();
		std::mutex waitMutex;
		std::condition_variable wake;
		std::jthread writer;
		std::ofstream file;
		std::wstring prefix;
		std::uint32_t part = 0;
		std::uint64_t fileBytes = 0;
		std::uint64_t retentionRemoved = 0;

		void Retain()
		{
			std::vector<std::filesystem::directory_entry> files;
			std::error_code ec;
			for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
				if (!entry.is_symlink(ec) && entry.is_regular_file(ec) && IsOwnedFilename(entry.path())
					&& entry.path().filename() != prefix + std::to_wstring(part) + L".jsonl")
					files.push_back(entry);
			std::sort(files.begin(), files.end(), [](const auto& a, const auto& b)
				{ return a.last_write_time() < b.last_write_time(); });
			while (files.size() >= limits.retainedFiles)
			{
				// 只删已解析目录中的精确追踪文件名，不接触 idt 日志或其他文件。
				std::filesystem::remove(files.front().path(), ec);
				if (ec) writerErrors.fetch_add(1);
				else ++retentionRemoved;
				files.erase(files.begin());
			}
		}
		std::string StatusLine(const char* event, std::uint64_t gestureId = 0)
		{
			std::ostringstream out;
			out.imbue(std::locale::classic());
			out << "{\"schema\":1,\"event\":\"" << event << "\",\"run_id\":" << runId
				<< ",\"gesture_id\":" << gestureId << ",\"qpc\":" << Now()
				<< ",\"qpc_frequency\":" << frequency << ",\"buffer_capacity\":2048,\"tail_ms\":" << limits.tailMilliseconds
				<< ",\"file_limit\":" << limits.fileBytes << ",\"retained_files\":" << limits.retainedFiles
				<< ",\"part\":" << part << ",\"retention_removed\":" << retentionRemoved
				<< ",\"written\":" << written.load() << ",\"dropped\":" << buffer.Dropped() + writerDropped.load()
				<< ",\"writer_dropped\":" << writerDropped.load()
				<< ",\"overwritten\":0,\"probe_misses\":" << probeMisses.load()
				<< ",\"writer_errors\":" << writerErrors.load() << "}\n";
			return out.str();
		}
		void Status(const char* event, std::uint64_t gestureId = 0, bool closing = false)
		{
			if (!file.is_open() || !file) return;
			auto line = StatusLine(event, gestureId);
			// 尾段、停止及元数据也受同一容量限制；仅 part_end 消费预留的收尾空间。
			if (!closing && fileBytes + line.size() + 1024 > limits.fileBytes)
			{
				OpenPart();
				if (!file) return;
				line = StatusLine(event, gestureId);
			}
			if (fileBytes + line.size() > limits.fileBytes) { writerErrors.fetch_add(1); return; }
			file << line;
			if (!file) writerErrors.fetch_add(1);
			fileBytes += line.size();
		}
		void Flush()
		{
			if (!file.is_open()) return;
			file.flush();
			if (!file) writerErrors.fetch_add(1);
		}
		void OpenPart()
		{
			if (file.is_open())
			{
				Status("part_end", 0, true);
				file.close();
				if (file.fail()) writerErrors.fetch_add(1);
			}
			std::error_code ec;
			std::filesystem::create_directories(directory, ec);
			directory = std::filesystem::weakly_canonical(directory);
			file.clear();
			file.open(directory / (prefix + std::to_wstring(++part) + L".jsonl"), std::ios::binary);
			file.imbue(std::locale::classic());
			fileBytes = 0;
			if (!file) { writerErrors.fetch_add(1); return; }
			Status("run");
			Retain();
		}
		void WriteRecord(const Record& record)
		{
			if (!file.is_open()) OpenPart();
			Record emitted = record;
			emitted.dropped += writerDropped.load();
			const auto line = Serialize(emitted, runId);
			if (fileBytes + line.size() + 1024 > limits.fileBytes) OpenPart();
			// 极端数值展开后的单项也不能突破分段上限，丢失另计入最终元数据。
			if (!file || fileBytes + line.size() + 1024 > limits.fileBytes)
			{
				writerDropped.fetch_add(1);
				return;
			}
			file << line;
			if (!file) { writerErrors.fetch_add(1); writerDropped.fetch_add(1); }
			else written.fetch_add(1);
			fileBytes += line.size();
		}
		void Run(std::stop_token stop) noexcept
		{
			try
			{
				SYSTEMTIME utc{}; GetSystemTime(&utc);
				std::wostringstream name;
				name.imbue(std::locale::classic());
				name << L"bar-bottom-dock-trace-" << std::setfill(L'0') << std::setw(4) << utc.wYear
					<< std::setw(2) << utc.wMonth << std::setw(2) << utc.wDay << L'T'
					<< std::setw(2) << utc.wHour << std::setw(2) << utc.wMinute << std::setw(2) << utc.wSecond
					<< L"Z-p" << GetCurrentProcessId() << L"-r" << runId << L'-';
				prefix = name.str();
				std::uint64_t tailSaved = 0;
				for (;;)
				{
					Record record;
					bool consumed = false;
					while (buffer.TryPop(record))
					{
						WriteRecord(record);
						consumed = true;
					}
					const auto end = deadline.load();
					const auto id = gesture.load();
					if (end != 0 && Now() > end && tailSaved != id)
					{
						Status("tail_complete", id);
						tailSaved = id;
						consumed = true;
					}
					if (consumed) Flush();
					if (stop.stop_requested() && producers.load() == 0)
					{
						if (buffer.TryPop(record))
						{
							// Stop 与最后一个生产者交错时，把已入队的末项留给下一轮。
							WriteRecord(record);
							continue;
						}
						break;
					}
					std::unique_lock lock(waitMutex);
					wake.wait_for(lock, std::chrono::milliseconds(50), [&] { return stop.stop_requested(); });
				}
				Status("stop", gesture.load());
				Flush();
				if (file.is_open())
				{
					file.close();
					if (file.fail()) writerErrors.fetch_add(1);
				}
			}
			catch (...)
			{
				writerErrors.fetch_add(1); accepting.store(false);
				// 文件仍可写时保留终止原因；诊断异常不能向产品线程传播。
				try { Status("writer_error", gesture.load(), true); Flush(); if (file.is_open()) file.close(); }
				catch (...) {}
			}
		}
	};

	Recorder::Recorder() = default;
	Recorder::~Recorder() { Stop(); }
	bool Recorder::Start(const std::filesystem::path& directory, Limits limits, bool enabled) noexcept
	{
		PreserveLastError preserve;
		if (!Enabled || !enabled) return false;
		if (impl_) return impl_->accepting.load();
		try
		{
			impl_ = std::make_unique<Impl>();
			impl_->directory = directory;
			limits.fileBytes = std::max<std::uint64_t>(4096, limits.fileBytes);
			limits.retainedFiles = std::clamp<std::size_t>(limits.retainedFiles, 1, 16);
			impl_->limits = limits;
			impl_->writer = std::jthread([state = impl_.get()](std::stop_token stop) { state->Run(stop); });
			// 初始化完成后再向早到的输入/渲染生产者发布，Stop 不回收这块存储。
			published_.store(impl_.get(), std::memory_order_release);
			return true;
		}
		catch (...) { if (impl_) impl_->accepting.store(false); return false; }
	}
	void Recorder::Stop() noexcept
	{
		PreserveLastError preserve;
		if (!impl_) return;
		impl_->accepting.store(false);
		if (impl_->writer.joinable())
		{
			impl_->writer.request_stop(); impl_->wake.notify_all(); impl_->writer.join();
		}
	}
	std::uint64_t Recorder::ActiveGesture() const noexcept
	{
		auto* state = published_.load(std::memory_order_acquire);
		if (!state || !state->accepting.load(std::memory_order_relaxed)) return 0;
		const auto deadline = state->deadline.load(std::memory_order_relaxed);
		return deadline != 0 && Now() > deadline ? 0 : state->gesture.load(std::memory_order_relaxed);
	}
	void Recorder::Push(Record record) noexcept
	{
		auto* state = published_.load(std::memory_order_acquire);
		PreserveLastError preserve;
		if (!state) return;
		state->producers.fetch_add(1);
		if (state->accepting.load())
		{
			// 帧在快照处固定手势；不能把上一手势的在途位图归到后来按下的新手势。
			if (record.gesture == 0 && record.frame == 0) record.gesture = ActiveGesture();
			if (record.gesture != 0)
			{
				record.qpc = Now(); record.thread = GetCurrentThreadId();
				state->buffer.TryPush(record);
			}
		}
		state->producers.fetch_sub(1);
	}
	void Recorder::Presented(Record probe) noexcept
	{
		auto* state = published_.load(std::memory_order_acquire);
		PreserveLastError preserve;
		if (!state || !state->accepting.load()) return;
		probe.qpc = Now();
		state->probeTrusted.store(false);
		if (!state->probeLock.test_and_set(std::memory_order_acquire))
		{
			state->probe = probe; state->probeValid = true;
			state->probeTrusted.store(true);
			state->probeLock.clear(std::memory_order_release);
		}
		else state->probeMisses.fetch_add(1);
		Push(probe);
	}
	void Recorder::InvalidatePresented() noexcept
	{
		auto* state = published_.load(std::memory_order_acquire);
		if (state) state->probeTrusted.store(false);
	}
	std::uint64_t Recorder::BeginGesture(Record& start, const RECT& actualWindow) noexcept
	{
		auto* state = published_.load(std::memory_order_acquire);
		PreserveLastError preserve;
		if (!state || !state->accepting.load()) return 0;
		start.event = Event::GestureStart;
		start.flags &= ~(BasisAvailable | GrabPointValid);
		start.basisFrame = 0; start.basisQpc = 0;
		start.logicalDown.fill(std::numeric_limits<double>::quiet_NaN());
		start.normalizedDown.fill(std::numeric_limits<double>::quiet_NaN());
		start.gesture = state->gesture.fetch_add(1) + 1;
		state->deadline.store(0);
		Record basis;
		bool found = false;
		if (!state->probeLock.test_and_set(std::memory_order_acquire))
		{
			found = state->probeValid && state->probeTrusted.load(); basis = state->probe;
			state->probeLock.clear(std::memory_order_release);
		}
		if (found && ResolveInitialGrab(start, basis, actualWindow))
		{
			basis.event = Event::GestureBasis; basis.gesture = start.gesture;
			basis.osWindow = actualWindow; basis.flags |= OsWindowValid | BasisAvailable;
			Push(basis);
		}
		else state->probeMisses.fetch_add(1);
		Push(start);
		return start.gesture;
	}
	void Recorder::EndGesture(Record end) noexcept
	{
		auto* state = published_.load(std::memory_order_acquire);
		PreserveLastError preserve;
		if (!state || end.gesture == 0) return;
		end.event = Event::GestureEnd; Push(end);
		if (state->gesture.load() == end.gesture)
			state->deadline.store(Now() + state->frequency * state->limits.tailMilliseconds / 1000);
	}
	Statistics Recorder::Stats() const noexcept
	{
		auto* state = published_.load(std::memory_order_acquire);
		return state ? Statistics{ state->written.load(), state->buffer.Dropped() + state->writerDropped.load(),
			state->probeMisses.load(), state->writerErrors.load(), state->runId, state->writerDropped.load() } : Statistics{};
	}
	Recorder& Get() noexcept { static Recorder recorder; return recorder; }
}
