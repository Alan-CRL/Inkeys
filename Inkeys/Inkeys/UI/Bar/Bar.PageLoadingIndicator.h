#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace Inkeys::UI::Bar
{
    // 页码控件的加载状态；与 BarPageLoadState 数值一致，由 Scene 在边界转换。
    enum class PageLoadState : std::uint8_t { None, Loading, RetryableFailed, FinalFailed };
    namespace PageLoadingStyle
    {
        inline constexpr auto Delay=std::chrono::milliseconds(500);
        inline constexpr double FadeInSeconds=.180,FadeOutSeconds=.140;
        // 环沿页码按钮边框内侧：线宽、与边框内缘的间隙、抗锯齿外扩（均为 DIP）。
        inline constexpr float Width=2.5F,Gap=1.0F,AntialiasOutset=1.5F;
        // 非线性环：一个周期内头部先行、尾部追赶，弧长在 [MinArc,MaxArc] 之间伸缩，
        // 同时叠加缓慢的整体旋转；全部以周长比例表示。
        inline constexpr double CycleSeconds=1.4,CycleTravel=.75,BaseTurnsPerCycle=.3,MinArc=.10,StaticArc=.25;
    }
    struct PageLoadingArc { double start=0,length=0; }; // 起点与弧长，均为周长比例，start∈[0,1)
    // 首尾缓动（三次 in-out）；两端导数为0，周期交界处头尾速度连续。
    inline double PageLoadingEase(double x) noexcept
    {
        x=(std::clamp)(x,0.0,1.0);
        if(x<.5)return 4*x*x*x;
        const double y=-2*x+2;return 1-y*y*y/2;
    }
    // phase 为已累计的旋转秒数；animations=false 时给出固定的静态弧。
    inline PageLoadingArc PageLoadingArcAt(double phase,bool animations) noexcept
    {
        using namespace PageLoadingStyle;
        if(!animations)return {0,StaticArc};
        phase=(std::max)(0.0,phase);
        const double cycles=phase/CycleSeconds,whole=std::floor(cycles),u=cycles-whole;
        const double head=PageLoadingEase(u/.7),tail=PageLoadingEase((u-.3)/.7);
        const double tailAbs=whole*CycleTravel+tail*CycleTravel+cycles*BaseTurnsPerCycle;
        const double length=MinArc+(head-tail)*CycleTravel;
        return {tailAbs-std::floor(tailAbs),length};
    }
    // 等待截止使用绝对单调时间；文字、变浅、环共用同一截止，分别消费公共动画dt。
    struct PageLoadingIndicator
    {
        using Clock=std::chrono::steady_clock;
        using Time=Clock::time_point;
        using Curve=double(*)(bool,double);
        struct Track
        {
            bool target=false,moving=false;
            double start=0,value=0,progress=1;
        };
        std::uint64_t session=0,episode=0;
        PageLoadState state=PageLoadState::None;
        bool requested=false,reached=false;
        Time deadline=Time::max();
        Track dim,ring;
        double phase=0;
        void Reset() noexcept { *this={}; }
        // 变浅进度与环进度；两者在 Reset 之外只由 Advance 改变。
        [[nodiscard]] double Dim() const noexcept { return dim.value; }
        [[nodiscard]] double Ring() const noexcept { return ring.value; }
        static bool Step(Track& track,bool target,bool animations,double dt,double speed,Curve curve) noexcept
        {
            const auto old=track.value;
            const bool retarget=target!=track.target;
            if(retarget){track.target=target;track.start=track.value;track.progress=0;track.moving=true;}
            if(!animations){track.value=target?1:0;track.progress=1;track.moving=false;}
            else if(track.moving){
                if(!retarget)track.progress=(std::min)(1.0,track.progress+(std::max)(0.0,dt)*(std::max)(.01,speed)
                    /(target?PageLoadingStyle::FadeInSeconds:PageLoadingStyle::FadeOutSeconds));
                const double t=curve(target,track.progress);track.value=track.start+((target?1.0:0.0)-track.start)*t;
                track.moving=track.progress<1;
            }
            return old!=track.value;
        }
        bool Advance(Time now,std::uint64_t nextSession,bool allowed,PageLoadState next,
            double dt,double speed,bool animations,Curve curve) noexcept
        {
            const auto oldDim=dim.value,oldRing=ring.value,oldPhase=phase;const bool oldReached=reached;
            if(!allowed||!nextSession){Reset();return oldDim!=0||oldRing!=0||oldReached;}
            if(session!=nextSession){Reset();session=nextSession;}
            const bool loading=next!=PageLoadState::None;
            if(loading&&!requested){deadline=now+PageLoadingStyle::Delay;++episode;}
            if(!loading)deadline=Time::max();
            requested=loading;state=next;
            reached=loading&&now>=deadline;
            Step(dim,reached,animations,dt,speed,curve);
            Step(ring,reached&&next==PageLoadState::Loading,animations,dt,speed,curve);
            // 环可见期间才推进相位；退场后保留，使再次出现时从上次位置继续。
            if(animations&&ring.value>0)phase+=(std::max)(0.0,dt)*(std::max)(.01,speed);
            return oldDim!=dim.value||oldRing!=ring.value||oldPhase!=phase||oldReached!=reached;
        }
        [[nodiscard]] bool NeedsFrames(bool animations) const noexcept { return dim.moving||ring.moving||(animations&&ring.value>0); }
        [[nodiscard]] Time WakeDeadline() const noexcept { return requested&&!reached?deadline:Time::max(); }
    };
    // 与可替换的Scene/SurfaceState分离：输入worker只消费数值并请求渲染，不保存对象指针。
    struct PageLoadingDeadline
    {
        std::atomic<std::int64_t> ticks=0;
        bool Publish(PageLoadingIndicator::Time time) noexcept
        {
            const auto value=time==PageLoadingIndicator::Time::max()?0:time.time_since_epoch().count();
            return ticks.exchange(value,std::memory_order_acq_rel)!=value;
        }
        PageLoadingIndicator::Time Consume(PageLoadingIndicator::Time now,bool& due) noexcept
        {
            due=false;auto value=ticks.load(std::memory_order_acquire);
            if(!value)return PageLoadingIndicator::Time::max();
            const PageLoadingIndicator::Time time{PageLoadingIndicator::Clock::duration(value)};
            if(now<time)return time;
            if(ticks.compare_exchange_strong(value,0,std::memory_order_acq_rel))due=true;
            return PageLoadingIndicator::Time::max();
        }
    };
}
