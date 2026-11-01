#pragma once

#include "PptNavigationDiagnostics.h"
#include <algorithm>
#include <memory>
#include <new>

namespace Inkeys::Business::PptWaitTrace
{
	// 渲染线程仅认领独立数值槽；只有全部 producer 真正停止后才可读取/输出。
	enum class Stage : unsigned { Publish, Snapshot, Complete, Clear, Session, Model, Configure, RenderAttempt, Draw, Pixels, NativeReceipt };
	enum class Result : unsigned { Accepted, Uninitialized, PositionMismatch, TrackerRejected, Unchanged, Reset, MergeRejected, Configured, ConfigureFailed, Drawn, NotDrawn, Submitted, NoPixels, Retry, DeviceLost, ReceiptCommitted, ReceiptRejected, CandidateStale };
	enum Flag : unsigned { Initialized=1, PositionActive=2, Waiting=4, Visible=8, Changed=16, PixelsSubmitted=32, ShouldShow=64, DrawInvoked=128, BrushFailed=256, Offscreen=512, Preparing=1024 };
	struct Fields
	{
		std::uint64_t session=0, epoch=0, positionSession=0, activeSession=0, activeEpoch=0, completedEpoch=0;
		std::uint64_t layoutSession=0, layoutEpoch=0, publication=0, previousPublication=0, revision=0, previousRevision=0;
		std::uint64_t candidate=0, frame=0, attempt=0, pixelRevision=0, sceneSerial=0;
		int surface=-1;
		unsigned direction=0, mode=0, flags=0, branches=0, dots=0;
		long hr=0;
		unsigned long error=0;
	};
	struct Record
	{
		Fields fields;
		std::int64_t qpc=0, frequency=0;
		unsigned long pid=0, tid=0;
		Stage stage=Stage::Publish;
		Result result=Result::Unchanged;
	};
	struct Counts { std::uint64_t seen=0, retained=0, dropped=0, invalid=0; };
	struct LastErrorGuard
	{
		DWORD saved=GetLastError();
		~LastErrorGuard() noexcept { SetLastError(saved); }
	};
	class Recorder
	{
	public:
		Recorder(Record* slots, std::size_t capacity, PptPerf::Clock clock={PptPerf::Now,PptPerf::Frequency,PptPerf::Process,PptPerf::Thread}) noexcept
			: slots_(slots), capacity_(slots?capacity:0), clock_(clock) {}
		bool Enabled() const noexcept { return capacity_!=0; }
		std::uint64_t NewAttempt() noexcept { return attempts_.fetch_add(1,std::memory_order_relaxed)+1; }
		void Capture(Stage stage, Fields fields, Result result) noexcept
		{
			if(!Enabled())return;
			LastErrorGuard guard;
			const auto index=seen_.fetch_add(1,std::memory_order_relaxed);
			if(index>=capacity_)return;
			Record value;value.fields=fields;value.stage=stage;value.result=result;
			try { value.qpc=clock_.now();value.frequency=clock_.frequency();value.pid=clock_.process();value.tid=clock_.thread(); } catch(...) {}
			if(value.qpc<=0||value.frequency<=0||!value.pid||!value.tid)invalid_.fetch_add(1,std::memory_order_relaxed);
			slots_[index]=value;
		}
		// 此函数不负责等待线程：调用方必须先完成真实 join，而非仅关闭开关。
		Counts AfterProducersJoined() const noexcept
		{
			const auto seen=seen_.load(std::memory_order_relaxed);
			const auto retained=(std::min)(seen,static_cast<std::uint64_t>(capacity_));
			return {seen,retained,seen-retained,invalid_.load(std::memory_order_relaxed)};
		}
		const Record& AtAfterProducersJoined(std::size_t index) const noexcept { return slots_[index]; }
		std::size_t Capacity() const noexcept { return capacity_; }
	private:
		Record* slots_;
		std::size_t capacity_;
		PptPerf::Clock clock_;
		std::atomic_uint64_t seen_=0,invalid_=0,attempts_=0;
	};
	inline constinit std::atomic<Recorder*> prepared=nullptr;
	inline std::unique_ptr<Record[]> storage;
	inline std::unique_ptr<Recorder> owner;
	inline constexpr std::size_t Capacity=16384;
	inline Recorder* Current() noexcept { return prepared.load(std::memory_order_acquire); }
	inline bool Enabled() noexcept { return Current()!=nullptr; }
	inline void Prepare() noexcept
	{
		LastErrorGuard guard;
		if(!PptPerf::Enabled()||Current())return;
		try {
			storage=std::make_unique<Record[]>(Capacity);
			owner=std::make_unique<Recorder>(storage.get(),Capacity);
			prepared.store(owner.get(),std::memory_order_release);
		} catch(...) { storage.reset();owner.reset(); }
	}
	inline void Capture(Stage stage, Fields fields, Result result) noexcept
	{
		if(!fields.epoch&&!(fields.flags&Preparing)&&stage!=Stage::Session&&stage!=Stage::Clear)return;
		if(auto* recorder=Current())recorder->Capture(stage,fields,result);
	}
	inline const char* StageName(Stage value) noexcept
	{
		switch(value){case Stage::Publish:return "publish-decision";case Stage::Snapshot:return "ppt-snapshot";case Stage::Complete:return "complete-decision";
		case Stage::Clear:return "clear-decision";case Stage::Session:return "session-reset";case Stage::Model:return "model-decision";
		case Stage::Configure:return "scene-configure";case Stage::RenderAttempt:return "render-attempt";case Stage::Draw:return "scene-draw";
		case Stage::Pixels:return "pixels-result";case Stage::NativeReceipt:return "native-receipt";}return "unknown";
	}
	inline const char* ResultName(Result value) noexcept
	{
		switch(value){case Result::Accepted:return "accepted";case Result::Uninitialized:return "uninitialized";case Result::PositionMismatch:return "position-mismatch";
		case Result::TrackerRejected:return "tracker-rejected";case Result::Unchanged:return "unchanged";case Result::Reset:return "reset";
		case Result::MergeRejected:return "merge-rejected";case Result::Configured:return "configured";case Result::ConfigureFailed:return "configure-failed";
		case Result::Drawn:return "drawn";case Result::NotDrawn:return "not-drawn";case Result::Submitted:return "submitted";case Result::NoPixels:return "no-pixels";
		case Result::Retry:return "retry";case Result::DeviceLost:return "device-lost";case Result::ReceiptCommitted:return "receipt-committed";
		case Result::ReceiptRejected:return "receipt-rejected";case Result::CandidateStale:return "candidate-stale";}return "unknown";
	}
	inline int Format(char* line,std::size_t size,const Record& record,std::uint64_t ordinal) noexcept
	{
		const auto& f=record.fields;
		const int n=std::snprintf(line,size,"[PptWait] v=1 event=record ordinal=%llu pid=%lu tid=%lu qpc=%lld frequency=%lld stage=%s result=%s session=%llu epoch=%llu direction=%u surface=%d mode=%u position_session=%llu active_session=%llu active_epoch=%llu completed_epoch=%llu layout_session=%llu layout_epoch=%llu publication=%llu previous_publication=%llu model_revision=%llu previous_model_revision=%llu candidate=%llu frame=%llu attempt=%llu pixel_revision=%llu scene_serial=%llu flags=%u branches=%u dots=%u hr=%ld error=%lu\n",
			static_cast<unsigned long long>(ordinal),record.pid,record.tid,static_cast<long long>(record.qpc),static_cast<long long>(record.frequency),StageName(record.stage),ResultName(record.result),
			static_cast<unsigned long long>(f.session),static_cast<unsigned long long>(f.epoch),f.direction,f.surface,f.mode,static_cast<unsigned long long>(f.positionSession),static_cast<unsigned long long>(f.activeSession),static_cast<unsigned long long>(f.activeEpoch),static_cast<unsigned long long>(f.completedEpoch),static_cast<unsigned long long>(f.layoutSession),static_cast<unsigned long long>(f.layoutEpoch),static_cast<unsigned long long>(f.publication),static_cast<unsigned long long>(f.previousPublication),static_cast<unsigned long long>(f.revision),static_cast<unsigned long long>(f.previousRevision),static_cast<unsigned long long>(f.candidate),static_cast<unsigned long long>(f.frame),static_cast<unsigned long long>(f.attempt),static_cast<unsigned long long>(f.pixelRevision),static_cast<unsigned long long>(f.sceneSerial),f.flags,f.branches,f.dots,f.hr,f.error);
		return n>0&&static_cast<std::size_t>(n)<size?n:0;
	}
	using Output=bool(*)(const char*,std::size_t);
	inline bool RawOutput(const char* text,std::size_t length)
	{
		const HANDLE handle=GetStdHandle(STD_ERROR_HANDLE);DWORD written=0;
		return handle&&handle!=INVALID_HANDLE_VALUE&&WriteFile(handle,text,static_cast<DWORD>(length),&written,nullptr)&&written==length;
	}
	inline std::uint64_t FlushAfterProducersJoined(Recorder& recorder,Output output=RawOutput) noexcept
	{
		LastErrorGuard guard;
		std::uint64_t errors=0;const auto counts=recorder.AfterProducersJoined();char line[1024];
		const auto write=[&](int n){try {if(n<=0||!output||!output(line,static_cast<std::size_t>(n)))++errors;}catch(...){++errors;}};
		for(std::uint64_t i=0;i<counts.retained;++i)write(Format(line,sizeof(line),recorder.AtAfterProducersJoined(static_cast<std::size_t>(i)),i+1));
		const int n=std::snprintf(line,sizeof(line),"[PptWait] v=1 event=summary sealed=1 pid=%lu capacity=%llu seen=%llu retained=%llu dropped=%llu invalid=%llu output_errors=%llu\n",GetCurrentProcessId(),static_cast<unsigned long long>(recorder.Capacity()),static_cast<unsigned long long>(counts.seen),static_cast<unsigned long long>(counts.retained),static_cast<unsigned long long>(counts.dropped),static_cast<unsigned long long>(counts.invalid),static_cast<unsigned long long>(errors));
		write(n>0&&static_cast<std::size_t>(n)<sizeof(line)?n:0);return errors;
	}
	inline void FlushAfterProducersJoined() noexcept
	{
		// 只有真实join之后才退休入口；不能用此exchange代替等待producer完成。
		if(auto* recorder=prepared.exchange(nullptr,std::memory_order_acq_rel))
			(void)FlushAfterProducersJoined(*recorder);
	}

	struct DrawSample { Recorder* recorder=nullptr;Fields fields;bool invoked=false,ended=false; };
	inline thread_local DrawSample* drawSample=nullptr;
	class DrawScope
	{
	public:
		explicit DrawScope(Fields fields,Recorder* recorder=Current()) noexcept
		{
			if(!recorder||!recorder->Enabled()||(!fields.epoch&&!(fields.flags&Preparing))||!(fields.flags&(Waiting|Preparing))){
				if(drawSample){previous_=drawSample;drawSample=nullptr;restore_=true;}return;
			}
			sample_.recorder=recorder;sample_.fields=fields;sample_.fields.attempt=recorder->NewAttempt();
			previous_=drawSample;drawSample=&sample_;active_=true;restore_=true;
			recorder->Capture(Stage::RenderAttempt,sample_.fields,Result::Accepted);
		}
		~DrawScope() noexcept
		{
			if(active_)EndDraw();
			if(restore_)drawSample=previous_;
		}
		DrawScope(const DrawScope&)=delete;DrawScope& operator=(const DrawScope&)=delete;
		std::uint64_t Id() const noexcept { return active_?sample_.fields.attempt:0; }
		void Pixels(Result result,bool submitted,std::uint64_t revision,long hr,unsigned long error) noexcept
		{
			if(!active_)return;EndDraw();auto fields=sample_.fields;fields.pixelRevision=revision;fields.hr=hr;fields.error=error;
			if(submitted)fields.flags|=PixelsSubmitted;
			sample_.recorder->Capture(Stage::Pixels,fields,result);
		}
	private:
		void EndDraw() noexcept
		{
			if(sample_.ended)return;sample_.ended=true;
			sample_.recorder->Capture(Stage::Draw,sample_.fields,sample_.invoked?Result::Drawn:Result::NotDrawn);
		}
		DrawSample sample_;DrawSample* previous_=nullptr;bool active_=false,restore_=false;
	};
	inline void DrawStarted() noexcept { if(drawSample){drawSample->invoked=true;drawSample->fields.flags|=DrawInvoked;} }
	inline void DrawFinished() noexcept
	{
		if(drawSample&&!drawSample->ended){drawSample->ended=true;
			drawSample->recorder->Capture(Stage::Draw,drawSample->fields,drawSample->invoked?Result::Drawn:Result::NotDrawn);}
	}
	inline void DotBrush(bool available) noexcept { if(drawSample){++drawSample->fields.branches;if(!available)drawSample->fields.flags|=BrushFailed;} }
	inline void DotIssued() noexcept { if(drawSample)++drawSample->fields.dots; }
}
