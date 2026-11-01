#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace Inkeys::UI::Bar
{
    namespace PageLoadingStyle
    {
        inline constexpr auto Delay=std::chrono::milliseconds(500);
        inline constexpr float Radius=5.4F,TrackWidth=.6F,ArcWidth=1.2F;
        inline constexpr float OuterRadius=Radius+ArcWidth/2,FromRight=15,FromTop=6.5F,AntialiasOutset=1.5F;
        inline constexpr float ArcEndX=Radius*-.9396926208F,ArcEndY=Radius*.3420201433F; // 250°圆弧端点
    }
    // 等待截止使用绝对单调时间；显隐和旋转单独消费公共动画dt。
    struct PageLoadingIndicator
    {
        using Clock=std::chrono::steady_clock;
        using Time=Clock::time_point;
        using Curve=double(*)(bool,double);
        std::uint64_t session=0,episode=0;
        bool requested=false,target=false,moving=false;
        Time deadline=Time::max();
        double opacity=0,scale=.75,angle=0,progress=1,startOpacity=0;
        void Reset() noexcept { *this={}; }
        bool Advance(Time now,std::uint64_t nextSession,bool allowed,bool loading,
            double dt,double speed,bool animations,Curve curve) noexcept
        {
            const auto oldOpacity=opacity,oldScale=scale,oldAngle=angle;
            if(!allowed||!nextSession){Reset();return oldOpacity!=0;}
            if(session!=nextSession){Reset();session=nextSession;}
            if(loading&&!requested){deadline=now+PageLoadingStyle::Delay;++episode;}
            if(!loading)deadline=Time::max();
            requested=loading;
            const bool visible=loading&&now>=deadline;
            const bool retarget=visible!=target;
            if(retarget){target=visible;startOpacity=opacity;progress=0;moving=true;}
            if(!animations){opacity=target?1:0;progress=1;moving=false;angle=0;}
            else{
                if(moving){
                    if(!retarget)progress=(std::min)(1.0,progress+(std::max)(0.0,dt)*(std::max)(.01,speed)/(target?.180:.140));
                    const double t=curve(target,progress);opacity=startOpacity+((target?1.0:0.0)-startOpacity)*t;
                    moving=progress<1;
                }
                if(opacity>0)angle=std::fmod(angle+(std::max)(0.0,dt)*(std::max)(.01,speed)*360.0,360.0);
            }
            scale=.75+.25*opacity;
            return oldOpacity!=opacity||oldScale!=scale||oldAngle!=angle;
        }
        bool NeedsFrames(bool animations) const noexcept { return moving||(animations&&opacity>0); }
        Time WakeDeadline() const noexcept { return requested&&!target?deadline:Time::max(); }
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
