#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace Inkeys::UI::Bar
{
    enum class StatusIndicatorKind : std::uint8_t { PptSlideshow, WpsSlideshow, SuperTop, NewVersion, Debug };
    enum class StatusIndicatorLifetime : std::uint8_t { Persistent, Notification };
    enum class StatusIndicatorAction : std::uint8_t { None, OpenSoftwareVersion };
    enum class StatusIndicatorIcon : std::uint8_t { Slideshow, Pin, NewVersion, Star, Bell, Info, Bolt };
    enum class StatusIndicatorPhase : std::uint8_t { Expanded, Collapsing, Docked, Dismissing, Exiting };
    enum class StatusIndicatorPublishResult : std::uint8_t { Added, Updated, Rejected };
    enum class StatusIndicatorShape : std::uint8_t { Capsule, Circle };

    struct StatusIndicatorSpec
    {
        std::uint64_t key=0; // 同 key 重复发布只更新字段，不重放出现
        StatusIndicatorKind kind=StatusIndicatorKind::Debug;
        StatusIndicatorLifetime lifetime=StatusIndicatorLifetime::Persistent;
        StatusIndicatorAction action=StatusIndicatorAction::None;
        std::uint32_t accentArgb=0xFFFFFFFFu;
        StatusIndicatorIcon icon=StatusIndicatorIcon::Info;
        std::wstring text;
    };

    struct StatusIndicatorOptions { bool enable=true,ppt=true,superTop=true,newVersion=true; };

    // 尺寸为 DIP，时长为秒；均为可调初值。
    namespace StatusIndicatorStyle
    {
        inline constexpr std::size_t Capacity=25;
        inline constexpr double Hold=3.0;
        // 宽度上限 = 主按钮宽 - 4（80 DIP 时 76）；文字区 76-2*4-22=46 DIP，可放下 11 DIP 粗体中文四字。
        inline constexpr double CapsuleHeight=24,CapsuleCircle=22,CapsuleIcon=13,TextSize=11,TextPadding=4;
        inline constexpr double CapsuleMaxWidthInset=4,CapsuleAnchorInset=4,ExpandedStep=4,EnterScale=.35;
        inline constexpr double DockedDiameter=16,DockedStep=5,DockedCornerInset=6,DockedOpacity=.5;
        inline constexpr double FanCircle=24,FanBaseGap=20,FanLayerGap=28,FanSlot=26,FanAngleMin=15,FanAngleMax=165;
        inline constexpr double RingBaseWidth=1,RingWidth=3,BreathPeriod=1.6,ScrollPause=.3;
        inline constexpr double Enter=.42,Collapse=.36,Dismiss=.30,Exit=.28,Fan=.32,Select=.24,RingFade=.24,Presence=.28;
        inline constexpr double FanAutoClose=3.0,ProgressCompleteLinger=3.0,ProgressRate=8;
        inline constexpr double SpringOmega=18,SpringDamping=.72,SpringStep=1.0/120,SpringMaxDt=.5;
        inline constexpr double Pi=3.14159265358979323846;
    }

    namespace StatusIndicatorCurve
    {
        inline double Clamp01(double t) noexcept { return std::isfinite(t)?std::clamp(t,0.0,1.0):0.0; }
        inline double OutSine(double t) noexcept { return std::sin(Clamp01(t)*StatusIndicatorStyle::Pi/2); }
        inline double InSine(double t) noexcept { return 1-std::cos(Clamp01(t)*StatusIndicatorStyle::Pi/2); }
        inline double InOutSine(double t) noexcept { return (1-std::cos(StatusIndicatorStyle::Pi*Clamp01(t)))/2; }
        inline double InOutCubic(double t) noexcept { t=Clamp01(t);const double u=-2*t+2;return t<.5?4*t*t*t:1-u*u*u/2; }
        inline double OutBack(double t) noexcept { const double s=Clamp01(t)-1;return 1+2.1*s*s*s+1.1*s*s; }
        inline double InBack(double t) noexcept { t=Clamp01(t);return 2.1*t*t*t-1.1*t*t; }
        inline double SmoothStep(double t) noexcept { t=Clamp01(t);return t*t*(3-2*t); }
        inline double Lerp(double a,double b,double t) noexcept { return a+(b-a)*t; }
        inline std::uint32_t LerpArgb(std::uint32_t a,std::uint32_t b,double t) noexcept
        {
            t=Clamp01(t);std::uint32_t out=0;
            for(int shift=0;shift<32;shift+=8)
            {
                const double x=static_cast<double>((a>>shift)&0xFFu),y=static_cast<double>((b>>shift)&0xFFu);
                out|=static_cast<std::uint32_t>(std::lround(x+(y-x)*t))<<shift;
            }
            return out;
        }
    }

    // 线性进度按 dt*speedRate 推进；动画关闭时直接到目标。
    inline bool StepStatusIndicatorProgress(double& value,double target,double scaledDt,double duration,bool animations) noexcept
    {
        const double old=value;
        if(!std::isfinite(value))value=target;
        if(!animations||duration<=0)value=target;
        else{const double step=(std::max)(0.0,scaledDt)/duration;value=value<target?(std::min)(target,value+step):(std::max)(target,value-step);}
        return value!=old;
    }

    // 与底栏吸附弹簧同参数（ω=18、ζ=0.72、120Hz 子步），单位为槽位。
    struct StatusIndicatorSpring
    {
        double position=0,velocity=0;
        void Snap(double target) noexcept { position=target;velocity=0; }
        bool Settled(double target) const noexcept { return std::abs(position-target)<=.002&&std::abs(velocity)<=.02; }
        bool Advance(double target,double scaledDt,bool animations) noexcept
        {
            const double oldPosition=position,oldVelocity=velocity;
            if(!animations||!std::isfinite(position)||!std::isfinite(velocity))Snap(target);
            else
            {
                double remaining=std::clamp(std::isfinite(scaledDt)?scaledDt:0.0,0.0,StatusIndicatorStyle::SpringMaxDt);
                using namespace StatusIndicatorStyle;
                while(remaining>0)
                {
                    const double step=(std::min)(remaining,SpringStep);
                    velocity+=(SpringOmega*SpringOmega*(target-position)-2*SpringDamping*SpringOmega*velocity)*step;
                    position+=velocity*step;remaining-=step;
                }
                if(Settled(target))Snap(target);
            }
            return oldPosition!=position||oldVelocity!=velocity;
        }
    };

    struct StatusIndicatorEntry
    {
        StatusIndicatorSpec spec;
        StatusIndicatorPhase phase=StatusIndicatorPhase::Expanded;
        bool visible=true;   // 配置过滤结果；隐藏条目保留注册但淡出
        bool holding=false;  // 仅队首计时
        bool snapStack=false,snapDock=false;
        std::uint64_t order=0,dockSerial=0;
        double holdStart=0,holdEnd=0,scrollElapsed=0,selectStart=0;
        // 均为 0..1 线性进度，曲线在布局中解析，中途反向保持连续。
        double appear=0,collapse=0,vanish=0,presence=1,select=0;
        double stackTarget=0,dockTarget=0,frozenFanSlot=0;
        StatusIndicatorSpring stack,dock;
        double textWidth=0;  // 渲染器测量后写入（DIP）
        bool Leaving() const noexcept { return phase==StatusIndicatorPhase::Dismissing||phase==StatusIndicatorPhase::Exiting; }
        bool InDock() const noexcept { return visible&&(phase==StatusIndicatorPhase::Docked||phase==StatusIndicatorPhase::Collapsing); }
    };

    struct StatusIndicatorRingState
    {
        std::uint64_t owner=0;
        std::uint32_t fromArgb=0,toArgb=0;
        double colorT=1,opacity=0,breathStart=0;
    };

    struct StatusIndicatorProgressState
    {
        std::uint64_t key=0;
        bool active=false,complete=false; // active=当前下载周期可见
        double target=0,display=0,opacity=0,completeAt=0;
    };

    inline double StatusIndicatorCapsuleMaxWidth(double mainWidth) noexcept
    {
        using namespace StatusIndicatorStyle;
        return (std::max)(CapsuleCircle,(std::isfinite(mainWidth)?mainWidth:0.0)-CapsuleMaxWidthInset);
    }
    inline double StatusIndicatorCapsuleWidth(double textWidth,double mainWidth) noexcept
    {
        using namespace StatusIndicatorStyle;
        textWidth=std::isfinite(textWidth)?(std::max)(0.0,textWidth):0.0;
        return std::clamp(2*TextPadding+textWidth+CapsuleCircle,CapsuleCircle,StatusIndicatorCapsuleMaxWidth(mainWidth));
    }
    inline double StatusIndicatorTextOverflow(double textWidth,double mainWidth) noexcept
    {
        using namespace StatusIndicatorStyle;
        const double limit=(std::max)(0.0,StatusIndicatorCapsuleMaxWidth(mainWidth)-2*TextPadding-CapsuleCircle);
        return std::isfinite(textWidth)?(std::max)(0.0,textWidth-limit):0.0;
    }
    // 两端各停顿 ScrollPause，中段平滑滚完，保证 Hold 内结束。
    inline double StatusIndicatorScrollOffset(double overflow,double elapsed) noexcept
    {
        using namespace StatusIndicatorStyle;
        if(!(overflow>0))return 0;
        return overflow*StatusIndicatorCurve::SmoothStep((elapsed-ScrollPause)/(Hold-2*ScrollPause));
    }
    inline double StatusIndicatorFanLayerRadius(double mainHeight,std::size_t layer) noexcept
    {
        using namespace StatusIndicatorStyle;
        return (std::isfinite(mainHeight)?(std::max)(0.0,mainHeight):0.0)/2+FanBaseGap+static_cast<double>(layer)*FanLayerGap;
    }
    inline std::size_t StatusIndicatorFanLayerCapacity(double radius) noexcept
    {
        using namespace StatusIndicatorStyle;
        const double slots=std::floor(Pi*(std::isfinite(radius)?(std::max)(0.0,radius):0.0)/FanSlot);
        return slots<1?1:static_cast<std::size_t>(slots);
    }

    class StatusIndicatorModel
    {
    public:
        void SetOptions(const StatusIndicatorOptions& options,double now) noexcept
        {
            options_=options;now_=now;
            for(auto& e:entries_)
            {
                const bool visible=Allows(e.spec.kind);
                if(visible==e.visible)continue;
                if(!visible&&e.InDock())FreezeFanForExit();
                e.visible=visible;
                if(!visible)
                {
                    e.holding=false;
                    if(selected_==e.spec.key)selected_=0;
                    // 隐藏的通知直接丢弃（淡出后移除）。
                    if(e.spec.lifetime==StatusIndicatorLifetime::Notification&&!e.Leaving())e.phase=StatusIndicatorPhase::Dismissing;
                    continue;
                }
                if(e.Leaving())continue;
                // 重新开启的持久状态以收起态直接出现，不重放胶囊。
                if(e.phase!=StatusIndicatorPhase::Docked){e.phase=StatusIndicatorPhase::Docked;e.collapse=e.appear=1;e.snapDock=true;}
                if(e.presence<=0)e.snapDock=true;
                if(!e.dockSerial)e.dockSerial=++dockSerial_;
            }
            RefreshFront(now);
        }
        const StatusIndicatorOptions& Options() const noexcept { return options_; }

        StatusIndicatorPublishResult Publish(const StatusIndicatorSpec& spec,double now)
        {
            now_=now;
            if(auto* e=FindMutable(spec.key))
            {
                e->spec.text=spec.text;e->spec.accentArgb=spec.accentArgb;e->spec.action=spec.action;e->spec.icon=spec.icon;
                if(e->phase==StatusIndicatorPhase::Exiting&&e->spec.lifetime==StatusIndicatorLifetime::Persistent)
                {
                    e->phase=e->collapse<1?StatusIndicatorPhase::Collapsing:StatusIndicatorPhase::Docked;
                    if(!e->dockSerial)e->dockSerial=++dockSerial_;
                }
                return StatusIndicatorPublishResult::Updated;
            }
            const bool visible=Allows(spec.kind);
            if(!visible&&spec.lifetime==StatusIndicatorLifetime::Notification)return StatusIndicatorPublishResult::Rejected;
            if(entries_.size()>=StatusIndicatorStyle::Capacity&&!Evict())return StatusIndicatorPublishResult::Rejected;
            StatusIndicatorEntry e;e.spec=spec;e.order=++order_;e.visible=visible;
            if(visible){e.phase=StatusIndicatorPhase::Expanded;e.snapStack=true;}
            else{e.phase=StatusIndicatorPhase::Docked;e.presence=0;e.appear=e.collapse=1;e.dockSerial=++dockSerial_;e.snapDock=true;}
            entries_.push_back(std::move(e));
            RefreshFront(now);
            return StatusIndicatorPublishResult::Added;
        }

        bool Retire(std::uint64_t key,double now)
        {
            now_=now;
            auto* e=FindMutable(key);
            if(!e)return false;
            if(!e->visible){Erase(key);RefreshFront(now);return true;}
            if(e->Leaving())return false;
            if(e->InDock())FreezeFanForExit();
            e->phase=e->spec.lifetime==StatusIndicatorLifetime::Notification?StatusIndicatorPhase::Dismissing:StatusIndicatorPhase::Exiting;
            e->holding=false;
            if(selected_==key)selected_=0;
            RefreshFront(now);
            return true;
        }

        // progress<0 表示无进度/失败：结束本周期（环淡出，条目保留）。
        bool SetProgress(std::uint64_t key,double progress,bool complete,double now) noexcept
        {
            now_=now;
            if(!Find(key))return false;
            auto& p=progress_;
            if(p.key!=key)p=StatusIndicatorProgressState{.key=key};
            if(complete)
            {
                const bool changed=!p.complete||!p.active;
                if(!p.active){p.active=true;p.display=(std::min)(p.display,1.0);}
                if(!p.complete){p.complete=true;p.completeAt=now;}
                p.target=1;
                return changed;
            }
            if(!(progress>=0))
            {
                const bool changed=p.active;
                p.active=p.complete=false;
                return changed;
            }
            progress=(std::min)(progress,1.0);
            if(!p.active||p.complete){p.active=true;p.complete=false;p.display=0;p.target=progress;return true;}
            // 同一周期只增不减，线路切换回零被吸收。
            if(progress<=p.target)return false;
            p.target=progress;
            return true;
        }

        bool SetDrawingActive(bool drawing) noexcept { const bool changed=drawing_!=drawing;drawing_=drawing;return changed; }
        bool SetTextWidth(std::uint64_t key,double width) noexcept
        {
            auto* e=FindMutable(key);width=std::isfinite(width)?(std::max)(0.0,width):0.0;
            if(!e||e->textWidth==width)return false;
            e->textWidth=width;return true;
        }
        void SetMainButtonWidth(double width) noexcept { if(std::isfinite(width)&&width>0)mainButtonWidth_=width; }

        // 返回需要由调用方执行的动作。
        StatusIndicatorAction ClickDocked(std::uint64_t key,double now) noexcept
        {
            now_=now;
            const auto* e=Find(key);
            if(!e||!e->visible||e->phase!=StatusIndicatorPhase::Docked)return StatusIndicatorAction::None;
            if(!fanOpen_)
            {
                const auto docked=DockedCount();
                if(docked==1)return e->spec.action;
                if(docked>=2){fanOpen_=true;fanDeadline_=now+StatusIndicatorStyle::FanAutoClose;}
                return StatusIndicatorAction::None;
            }
            fanDeadline_=now+StatusIndicatorStyle::FanAutoClose;
            if(e->spec.action!=StatusIndicatorAction::None){fanOpen_=false;selected_=0;return e->spec.action;}
            if(selected_){selected_=0;return StatusIndicatorAction::None;}
            selected_=key;FindMutable(key)->selectStart=now;
            return StatusIndicatorAction::None;
        }
        bool ClickExpanded(std::uint64_t key,double now) noexcept
        {
            now_=now;
            auto* e=FindMutable(key);
            if(!e||!e->visible)return false;
            if(e->phase==StatusIndicatorPhase::Docked&&selected_==key)
            {
                selected_=0;fanDeadline_=now+StatusIndicatorStyle::FanAutoClose;return true;
            }
            if(e->phase!=StatusIndicatorPhase::Expanded)return false;
            Expire(*e);
            RefreshFront(now);
            return true;
        }
        bool ClickOutside(double now) noexcept
        {
            now_=now;
            if(!fanOpen_&&!selected_)return false;
            fanOpen_=false;selected_=0;return true;
        }

        bool Advance(double now,double dt,double speedRate,bool animations)
        {
            using namespace StatusIndicatorStyle;
            const double previousNow=now_;
            now_=now;animations_=animations;
            const double sdt=(std::isfinite(dt)?(std::max)(0.0,dt):0.0)*(std::isfinite(speedRate)?(std::max)(.01,speedRate):1.0);
            bool changed=false;
            // 3s 到期用真实时间；可能连续到期多个（例如 Hold 为 0）。
            for(std::size_t guard=0;guard<=entries_.size();++guard)
            {
                RefreshFront(now);
                auto* front=FrontMutable();
                if(!front||!front->holding||now<front->holdEnd)break;
                Expire(*front);changed=true;
            }
            for(auto& e:entries_)
            {
                if(e.holding)e.scrollElapsed=now-e.holdStart;
                if(e.phase==StatusIndicatorPhase::Expanded||e.phase==StatusIndicatorPhase::Collapsing)
                    changed|=StepStatusIndicatorProgress(e.appear,1,sdt,Enter,animations);
                if(e.phase==StatusIndicatorPhase::Collapsing)
                {
                    changed|=StepStatusIndicatorProgress(e.collapse,1,sdt,Collapse,animations);
                    if(e.collapse>=1)e.phase=StatusIndicatorPhase::Docked;
                }
                changed|=StepStatusIndicatorProgress(e.vanish,e.Leaving()?1.0:0.0,sdt,
                    e.phase==StatusIndicatorPhase::Dismissing?Dismiss:Exit,animations);
                changed|=StepStatusIndicatorProgress(e.presence,e.visible?1.0:0.0,sdt,Presence,animations);
                if(!e.visible&&e.presence<=0&&!e.Leaving()&&e.spec.lifetime==StatusIndicatorLifetime::Persistent
                    &&e.phase!=StatusIndicatorPhase::Docked)
                {
                    e.phase=StatusIndicatorPhase::Docked;e.collapse=e.appear=1;e.holding=false;changed=true;
                }
            }
            const auto before=entries_.size();
            std::erase_if(entries_,[](const StatusIndicatorEntry& e){
                return (e.Leaving()&&e.vanish>=1)||(!e.visible&&e.presence<=0&&
                    (e.Leaving()||e.spec.lifetime==StatusIndicatorLifetime::Notification));});
            if(entries_.size()!=before){changed=true;DropStaleReferences();}
            RefreshFront(now);

            if(fanOpen_&&(DockedCount()<2||now>=fanDeadline_)){fanOpen_=false;selected_=0;changed=true;}
            changed|=StepStatusIndicatorProgress(fan_,fanOpen_?1.0:0.0,sdt,Fan,animations);
            if(fan_<=0)frozenFanCount_=0;
            for(auto& e:entries_)changed|=StepStatusIndicatorProgress(e.select,e.spec.key==selected_?1.0:0.0,sdt,Select,animations);

            changed|=AdvanceSlots(sdt,animations);
            changed|=AdvanceRing(now,sdt,animations);
            changed|=AdvanceProgress(now,sdt,animations);
            if(now!=previousNow&&TimeDrivenActive())changed=true;
            return changed;
        }

        bool NeedsFrames() const noexcept
        {
            if(TimeDrivenActive())return true;
            for(const auto& e:entries_)
            {
                if(e.snapStack||e.snapDock||e.phase==StatusIndicatorPhase::Collapsing||e.Leaving())return true;
                if(e.phase==StatusIndicatorPhase::Expanded&&e.appear<1)return true;
                if(e.vanish!=0||e.presence!=(e.visible?1.0:0.0)||e.select!=(e.spec.key==selected_?1.0:0.0))return true;
                if(e.visible&&e.phase==StatusIndicatorPhase::Expanded&&!e.stack.Settled(e.stackTarget))return true;
                if(e.InDock()&&!e.dock.Settled(e.dockTarget))return true;
            }
            if(fan_!=(fanOpen_?1.0:0.0)||ring_.colorT<1||ring_.opacity!=(RingWanted()?1.0:0.0))return true;
            if(progress_.opacity!=(ProgressWanted()?1.0:0.0)||progress_.display!=progress_.target)return true;
            return false;
        }

        // 下一个绝对截止（秒）；无则 +inf。
        double WakeDeadline() const noexcept
        {
            double deadline=std::numeric_limits<double>::infinity();
            if(const auto* front=Front();front&&front->holding)deadline=(std::min)(deadline,front->holdEnd);
            if(fanOpen_)deadline=(std::min)(deadline,fanDeadline_);
            if(progress_.active&&progress_.complete)deadline=(std::min)(deadline,progress_.completeAt+StatusIndicatorStyle::ProgressCompleteLinger);
            return deadline;
        }

        std::size_t Count() const noexcept { return entries_.size(); }
        std::size_t VisibleCount() const noexcept
        {
            return static_cast<std::size_t>(std::count_if(entries_.begin(),entries_.end(),
                [](const StatusIndicatorEntry& e){ return e.visible&&!e.Leaving(); }));
        }
        std::size_t DebugCount() const noexcept
        {
            return static_cast<std::size_t>(std::count_if(entries_.begin(),entries_.end(),
                [](const StatusIndicatorEntry& e){ return e.spec.kind==StatusIndicatorKind::Debug; }));
        }
        std::size_t DockedCount() const noexcept
        {
            return static_cast<std::size_t>(std::count_if(entries_.begin(),entries_.end(),
                [](const StatusIndicatorEntry& e){ return e.visible&&e.phase==StatusIndicatorPhase::Docked; }));
        }
        // 最新的未退场条目（调试页“删除一个”使用）。
        std::optional<std::uint64_t> NewestKey(StatusIndicatorKind kind,StatusIndicatorLifetime lifetime) const noexcept
        {
            for(auto it=entries_.rbegin();it!=entries_.rend();++it)
                if(it->spec.kind==kind&&it->spec.lifetime==lifetime&&!it->Leaving())return it->spec.key;
            return std::nullopt;
        }

        const std::vector<StatusIndicatorEntry>& Entries() const noexcept { return entries_; }
        const StatusIndicatorEntry* Find(std::uint64_t key) const noexcept
        {
            for(const auto& e:entries_)if(e.spec.key==key)return &e;
            return nullptr;
        }
        const StatusIndicatorEntry* Front() const noexcept
        {
            for(const auto& e:entries_)if(e.visible&&e.phase==StatusIndicatorPhase::Expanded)return &e;
            return nullptr;
        }
        std::uint64_t FrontKey() const noexcept { const auto* f=Front();return f?f->spec.key:0; }
        bool FanOpen() const noexcept { return fanOpen_; }
        double FanProgress() const noexcept { return fan_; }
        std::size_t FrozenFanCount() const noexcept { return frozenFanCount_; }
        double FanDeadline() const noexcept { return fanDeadline_; }
        std::uint64_t Selected() const noexcept { return selected_; }
        const StatusIndicatorRingState& Ring() const noexcept { return ring_; }
        const StatusIndicatorProgressState& Progress() const noexcept { return progress_; }
        double Now() const noexcept { return now_; }
        bool Animations() const noexcept { return animations_; }
        bool DrawingActive() const noexcept { return drawing_; }
        double MainButtonWidth() const noexcept { return mainButtonWidth_; }
        double Breath() const noexcept
        {
            using namespace StatusIndicatorStyle;
            return .775+.225*std::sin(2*Pi*(now_-ring_.breathStart)/BreathPeriod);
        }

    private:
        bool Allows(StatusIndicatorKind kind) const noexcept
        {
            if(!options_.enable)return false;
            switch(kind)
            {
            case StatusIndicatorKind::PptSlideshow:
            case StatusIndicatorKind::WpsSlideshow: return options_.ppt;
            case StatusIndicatorKind::SuperTop: return options_.superTop;
            case StatusIndicatorKind::NewVersion: return options_.newVersion;
            default: return true;
            }
        }
        StatusIndicatorEntry* FindMutable(std::uint64_t key) noexcept { return const_cast<StatusIndicatorEntry*>(Find(key)); }
        StatusIndicatorEntry* FrontMutable() noexcept { return const_cast<StatusIndicatorEntry*>(Front()); }

        // 队首（最旧可见展开态）到达最前位置时才开始 3s 计时。
        void RefreshFront(double now) noexcept
        {
            auto* front=FrontMutable();
            for(auto& e:entries_)if(&e!=front)e.holding=false;
            if(front&&!front->holding){front->holding=true;front->holdStart=now;front->holdEnd=now+StatusIndicatorStyle::Hold;front->scrollElapsed=0;}
        }
        void Expire(StatusIndicatorEntry& e) noexcept
        {
            e.holding=false;
            if(e.spec.lifetime==StatusIndicatorLifetime::Notification){e.phase=StatusIndicatorPhase::Dismissing;return;}
            e.phase=StatusIndicatorPhase::Collapsing;e.collapse=0;
            e.dockSerial=++dockSerial_;e.snapDock=true;
        }
        bool Evict() noexcept
        {
            const StatusIndicatorEntry* victim=nullptr;
            for(const auto& e:entries_)if(e.spec.lifetime==StatusIndicatorLifetime::Notification){victim=&e;break;}
            if(!victim)for(const auto& e:entries_)if(e.spec.kind==StatusIndicatorKind::Debug){victim=&e;break;}
            if(!victim)return false;
            Erase(victim->spec.key);
            return true;
        }
        void Erase(std::uint64_t key) noexcept
        {
            if(const auto* e=Find(key);e&&e->InDock())FreezeFanForExit();
            std::erase_if(entries_,[key](const StatusIndicatorEntry& e){ return e.spec.key==key; });
            DropStaleReferences();
        }
        void DropStaleReferences() noexcept
        {
            if(selected_&&!Find(selected_))selected_=0;
            if(progress_.key&&!Find(progress_.key))progress_={};
            if(ring_.owner&&!Find(ring_.owner))ring_.owner=0;
        }
        void FreezeFanForExit() noexcept
        {
            if(!(fanOpen_||fan_>0))return;
            if(!frozenFanCount_)
            {
                // 注销会改变弧的离散容量；退场期间沿用原槽位，避免圆形先跳到新角度再收起。
                for(auto& e:entries_)if(e.InDock()){e.frozenFanSlot=e.dock.position;++frozenFanCount_;}
            }
            fanOpen_=false;selected_=0;
        }
        bool AdvanceSlots(double sdt,bool animations) noexcept
        {
            bool changed=false;double rank=0;
            for(auto& e:entries_)
            {
                if(!e.visible||e.phase!=StatusIndicatorPhase::Expanded)continue;
                e.stackTarget=rank++;
                if(e.snapStack){e.stack.Snap(e.stackTarget);e.snapStack=false;changed=true;}
                else changed|=e.stack.Advance(e.stackTarget,sdt,animations);
            }
            // 收起态：dockSerial 越大越新，槽位 0 在右上角。
            std::vector<StatusIndicatorEntry*> dock;
            for(auto& e:entries_)if(e.InDock())dock.push_back(&e);
            std::sort(dock.begin(),dock.end(),[](const auto* a,const auto* b){ return a->dockSerial>b->dockSerial; });
            for(std::size_t i=0;i<dock.size();++i)
            {
                auto& e=*dock[i];e.dockTarget=static_cast<double>(i);
                if(e.snapDock){e.dock.Snap(e.dockTarget);e.snapDock=false;changed=true;}
                else changed|=e.dock.Advance(e.dockTarget,sdt,animations);
            }
            for(auto& e:entries_)
            {
                // 首帧前就被过滤或收起的条目不再参与展开槽位，清掉待初始化标记以免空闲续帧。
                if(e.snapStack&&(!e.visible||e.phase!=StatusIndicatorPhase::Expanded))e.snapStack=false;
                if(e.snapDock&&!e.InDock())e.snapDock=false;
            }
            return changed;
        }
        bool RingWanted() const noexcept { return Front()&&!drawing_; }
        bool ProgressWanted() const noexcept
        {
            const auto* e=progress_.key?Find(progress_.key):nullptr;
            return progress_.active&&e&&e->visible&&!e->Leaving()&&!drawing_;
        }
        bool AdvanceRing(double now,double sdt,bool animations) noexcept
        {
            using namespace StatusIndicatorStyle;
            bool changed=false;
            const auto* front=Front();
            if(front)
            {
                const auto accent=front->spec.accentArgb;
                if(ring_.owner!=front->spec.key||ring_.toArgb!=accent)
                {
                    if(ring_.opacity<=0){ring_.fromArgb=ring_.toArgb=accent;ring_.colorT=1;}
                    else{ring_.fromArgb=RingColor();ring_.toArgb=accent;ring_.colorT=0;}
                    ring_.owner=front->spec.key;changed=true;
                }
            }
            else if(ring_.owner){ring_.owner=0;changed=true;}
            const bool want=RingWanted();
            if(want&&ring_.opacity<=0)ring_.breathStart=now;
            changed|=StepStatusIndicatorProgress(ring_.opacity,want?1.0:0.0,sdt,RingFade,animations);
            changed|=StepStatusIndicatorProgress(ring_.colorT,1,sdt,RingFade,animations);
            return changed;
        }
        bool AdvanceProgress(double now,double sdt,bool animations) noexcept
        {
            using namespace StatusIndicatorStyle;
            auto& p=progress_;bool changed=false;
            if(p.active&&p.complete&&now>=p.completeAt+ProgressCompleteLinger){p.active=false;changed=true;}
            changed|=StepStatusIndicatorProgress(p.opacity,ProgressWanted()?1.0:0.0,sdt,RingFade,animations);
            const double old=p.display;
            if(!animations)p.display=p.target;
            else
            {
                p.display+=(p.target-p.display)*(1-std::exp(-ProgressRate*sdt));
                if(std::abs(p.target-p.display)<.001)p.display=p.target;
            }
            return changed||old!=p.display;
        }
    public:
        std::uint32_t RingColor() const noexcept
        {
            return StatusIndicatorCurve::LerpArgb(ring_.fromArgb,ring_.toArgb,StatusIndicatorCurve::InOutSine(ring_.colorT));
        }
        // 呼吸、文字滚动与 100% 校验阶段属于时间驱动画面，显示期内需要连续帧。
        bool TimeDrivenActive() const noexcept
        {
            using namespace StatusIndicatorStyle;
            if(animations_&&ring_.opacity>0)return true;
            if(animations_&&progress_.opacity>0&&progress_.display>=1&&!progress_.complete)return true;
            for(const auto& e:entries_)
            {
                if(StatusIndicatorTextOverflow(e.textWidth,mainButtonWidth_)<=0)continue;
                if(e.holding&&now_<e.holdStart+Hold-ScrollPause)return true;
                if(e.spec.key==selected_&&now_<e.selectStart+Hold-ScrollPause)return true;
            }
            return false;
        }

    private:
        std::vector<StatusIndicatorEntry> entries_;
        StatusIndicatorOptions options_;
        StatusIndicatorRingState ring_;
        StatusIndicatorProgressState progress_;
        std::uint64_t order_=0,dockSerial_=0,selected_=0;
        std::size_t frozenFanCount_=0;
        double now_=0,fan_=0,fanDeadline_=0,mainButtonWidth_=80;
        bool fanOpen_=false,drawing_=false,animations_=true;
    };

    // ---------------- 纯布局 ----------------

    struct StatusIndicatorRect
    {
        double left=0,top=0,right=0,bottom=0;
        double Width() const noexcept { return right-left; }
        double Height() const noexcept { return bottom-top; }
        bool Empty() const noexcept { return !(right>left&&bottom>top); }
    };
    inline StatusIndicatorRect UnionStatusIndicatorRect(const StatusIndicatorRect& a,const StatusIndicatorRect& b) noexcept
    {
        if(a.Empty())return b;
        if(b.Empty())return a;
        return {(std::min)(a.left,b.left),(std::min)(a.top,b.top),(std::max)(a.right,b.right),(std::max)(a.bottom,b.bottom)};
    }
    inline bool StatusIndicatorRectContains(const StatusIndicatorRect& outer,const StatusIndicatorRect& inner,double epsilon=1e-6) noexcept
    {
        return inner.Empty()||(inner.left>=outer.left-epsilon&&inner.top>=outer.top-epsilon
            &&inner.right<=outer.right+epsilon&&inner.bottom<=outer.bottom+epsilon);
    }

    struct StatusIndicatorLayoutInput
    {
        double mainCenterX=0,mainCenterY=0,mainWidth=80,mainHeight=80; // 主按钮当前动画值
        StatusIndicatorRect baseBounds; // 不含指示器的正常态内容边界
        StatusIndicatorRect work;       // 显示器工作区
    };

    struct StatusIndicatorEntryLayout
    {
        std::uint64_t key=0;
        StatusIndicatorKind kind=StatusIndicatorKind::Debug;
        StatusIndicatorIcon icon=StatusIndicatorIcon::Info;
        StatusIndicatorShape shape=StatusIndicatorShape::Circle;
        std::uint32_t accentArgb=0;
        StatusIndicatorRect bounds;   // 胶囊或圆形外接矩形（已含缩放）
        StatusIndicatorRect textClip; // 文字段裁剪区
        double textX=0,scrollOffset=0,textOpacity=0;
        double iconCenterX=0,iconCenterY=0,iconRadius=0;
        double opacity=0,scale=1;
        int z=0;            // 绘制顺序（升序）
        bool overflow=false; // 允许越过 baseBounds（轨道展开/选中）
    };

    struct StatusIndicatorRingLayout { std::uint64_t owner=0;std::uint32_t argb=0;double opacity=0,width=StatusIndicatorStyle::RingBaseWidth; };
    struct StatusIndicatorProgressLayout { std::uint64_t key=0;std::uint32_t argb=0;double value=0,opacity=0,width=StatusIndicatorStyle::RingWidth; };

    struct StatusIndicatorFanPlacement { double x=0,y=0;std::size_t layer=0;bool mirrored=false; };

    struct StatusIndicatorLayout
    {
        std::vector<StatusIndicatorEntryLayout> entries; // 已按 z 升序
        StatusIndicatorRingLayout ring;
        StatusIndicatorProgressLayout progress;
        StatusIndicatorRect normalBounds,overflowBounds;
        bool fanMirrored=false;
    };

    // 以主按钮中心为圆心的上半平面多层同心弧；index 0 为最新，位于最内层右端。
    inline StatusIndicatorFanPlacement ResolveStatusIndicatorFanPlacement(
        std::size_t index,std::size_t count,const StatusIndicatorLayoutInput& input) noexcept
    {
        using namespace StatusIndicatorStyle;
        count=(std::max)(count,std::size_t{1});index=(std::min)(index,count-1);
        StatusIndicatorFanPlacement out;
        std::size_t layerIndex=0,layerCount=1;double radius=StatusIndicatorFanLayerRadius(input.mainHeight,0),outer=radius;
        for(std::size_t layer=0,begin=0;begin<count;++layer)
        {
            const double r=StatusIndicatorFanLayerRadius(input.mainHeight,layer);
            const std::size_t n=(std::min)(StatusIndicatorFanLayerCapacity(r),count-begin);
            if(index>=begin&&index<begin+n){out.layer=layer;layerIndex=index-begin;layerCount=n;radius=r;}
            outer=r;begin+=n;
        }
        const bool workValid=!input.work.Empty();
        out.mirrored=workValid&&input.mainCenterY-outer-FanCircle/2<input.work.top;
        const double degrees=layerCount<=1?90.0:FanAngleMin+(FanAngleMax-FanAngleMin)*static_cast<double>(layerIndex)/static_cast<double>(layerCount-1);
        const double radians=degrees*Pi/180;
        out.x=input.mainCenterX+radius*std::cos(radians);
        out.y=input.mainCenterY+(out.mirrored?1:-1)*radius*std::sin(radians);
        if(workValid)
        {
            const double r=FanCircle/2;
            if(input.work.Width()>=FanCircle)out.x=std::clamp(out.x,input.work.left+r,input.work.right-r);
            if(input.work.Height()>=FanCircle)out.y=std::clamp(out.y,input.work.top+r,input.work.bottom-r);
        }
        return out;
    }

    namespace StatusIndicatorLayoutDetail
    {
        struct Point { double x=0,y=0; };
        // 局部几何，统一缩放/平移后输出。
        struct Geometry
        {
            StatusIndicatorShape shape=StatusIndicatorShape::Circle;
            StatusIndicatorRect rect,textClip;
            double textX=0,cx=0,cy=0,radius=0,scale=1;
            void Transform(double s,double px,double py,double dx,double dy) noexcept
            {
                auto mx=[&](double x){ return px+(x-px)*s+dx; };
                auto my=[&](double y){ return py+(y-py)*s+dy; };
                rect={mx(rect.left),my(rect.top),mx(rect.right),my(rect.bottom)};
                textClip={mx(textClip.left),my(textClip.top),mx(textClip.right),my(textClip.bottom)};
                textX=mx(textX);cx=mx(cx);cy=my(cy);radius*=s;scale*=s;
            }
            void Scale(double s) noexcept { Transform(s,(rect.left+rect.right)/2,(rect.top+rect.bottom)/2,0,0); }
            // 先等比缩小到可容纳，再平移进入边界。
            void FitInto(const StatusIndicatorRect& bounds) noexcept
            {
                if(bounds.Empty()||rect.Empty())return;
                const double s=(std::min)({1.0,bounds.Width()/rect.Width(),bounds.Height()/rect.Height()});
                if(s<1)Scale(s);
                double dx=0,dy=0;
                if(rect.left<bounds.left)dx=bounds.left-rect.left;else if(rect.right>bounds.right)dx=bounds.right-rect.right;
                if(rect.top<bounds.top)dy=bounds.top-rect.top;else if(rect.bottom>bounds.bottom)dy=bounds.bottom-rect.bottom;
                Transform(1,0,0,dx,dy);
            }
        };
        inline Geometry Circle(Point c,double diameter) noexcept
        {
            Geometry g;g.shape=StatusIndicatorShape::Circle;g.cx=c.x;g.cy=c.y;g.radius=diameter/2;
            g.rect={c.x-g.radius,c.y-g.radius,c.x+g.radius,c.y+g.radius};g.textClip={c.x,c.y,c.x,c.y};g.textX=c.x;
            return g;
        }
        // 右端圆固定，左段宽 width；文字窄时居中、超限时按 scroll 左移。
        inline Geometry Capsule(double right,double centerY,double width,double fullWidth,double textWidth,double scroll) noexcept
        {
            using namespace StatusIndicatorStyle;
            Geometry g;g.shape=StatusIndicatorShape::Capsule;
            const double h=CapsuleHeight,top=centerY-h/2;
            width=(std::max)(width,CapsuleCircle);
            g.rect={right-width,top,right,top+h};
            g.cx=right-CapsuleCircle/2;g.cy=centerY;g.radius=CapsuleCircle/2;
            // 文字位置基于完整宽度，收起时只缩短裁剪区。
            const double fullLeft=right-fullWidth,clipLeft=fullLeft+TextPadding,clipRight=right-CapsuleCircle-TextPadding;
            const double visibleLeft=(std::max)(clipLeft,g.rect.left+TextPadding);
            g.textClip={visibleLeft,top,(std::max)(visibleLeft,clipRight),top+h};
            const double clipWidth=(std::max)(0.0,clipRight-clipLeft);
            g.textX=textWidth<=clipWidth?clipLeft+(clipWidth-textWidth)/2:clipLeft-scroll;
            return g;
        }
        inline Point LerpPoint(Point a,Point b,double t) noexcept { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t}; }
    }

    inline StatusIndicatorLayout ResolveStatusIndicatorLayout(const StatusIndicatorModel& model,const StatusIndicatorLayoutInput& input)
    {
        using namespace StatusIndicatorStyle;
        using namespace StatusIndicatorCurve;
        using namespace StatusIndicatorLayoutDetail;
        StatusIndicatorLayout out;
        const auto& base=input.baseBounds;
        const bool baseValid=!base.Empty();
        const double cx=input.mainCenterX,mainTop=input.mainCenterY-input.mainHeight/2,mainRight=cx+input.mainWidth/2;
        const double capsuleTop0=mainTop+CapsuleAnchorInset;
        const double stackLimit=baseValid?(std::max)(0.0,capsuleTop0-base.top):1e9;
        const double r=DockedDiameter/2;
        double cornerX=mainRight-DockedCornerInset,cornerY=mainTop+DockedCornerInset;
        if(baseValid){cornerX=(std::min)(cornerX,base.right-r);cornerY=(std::max)(cornerY,base.top+r);}
        const double dockLimit=baseValid?(std::max)(0.0,cornerX-r-base.left):1e9;
        const double fanP=model.FanProgress(),fanGeometry=OutBack(fanP),fanBlend=InOutSine(fanP);
        std::size_t dockCount=0;
        for(const auto& e:model.Entries())if(e.InDock())++dockCount;
        if(model.FrozenFanCount())dockCount=model.FrozenFanCount();
        const double now=model.Now(),mainWidth=input.mainWidth;

        auto dockPoint=[&](double slot){ return Point{cornerX-(std::min)((std::max)(0.0,slot)*DockedStep,dockLimit),cornerY}; };
        auto fanPoint=[&](double slot)
        {
            slot=std::clamp(std::isfinite(slot)?slot:0.0,0.0,static_cast<double>((std::max)(dockCount,std::size_t{1})-1));
            const auto lo=static_cast<std::size_t>(std::floor(slot)),hi=static_cast<std::size_t>(std::ceil(slot));
            const auto a=ResolveStatusIndicatorFanPlacement(lo,dockCount,input),b=ResolveStatusIndicatorFanPlacement(hi,dockCount,input);
            out.fanMirrored=a.mirrored;
            return LerpPoint({a.x,a.y},{b.x,b.y},slot-static_cast<double>(lo));
        };
        // 收起态圆形：右上角堆叠与轨道展开之间按 fan 进度混合。
        auto dockedVisual=[&](const StatusIndicatorEntry& e,double& opacity)
        {
            Point p=dockPoint(e.dock.position);double d=DockedDiameter;opacity=DockedOpacity;
            if(fanP>0){p=LerpPoint(p,fanPoint(model.FrozenFanCount()?e.frozenFanSlot:e.dock.position),fanGeometry);d=Lerp(DockedDiameter,FanCircle,fanGeometry);opacity=Lerp(DockedOpacity,1,fanBlend);}
            return Circle(p,(std::max)(0.0,d));
        };
        auto stackCenterY=[&](const StatusIndicatorEntry& e)
        {
            return capsuleTop0-(std::min)((std::max)(0.0,e.stack.position)*ExpandedStep,stackLimit)+CapsuleHeight/2;
        };

        struct Item { StatusIndicatorEntryLayout layout;int layer=0;double sub=0; };
        std::vector<Item> items;items.reserve(model.Entries().size());
        for(const auto& e:model.Entries())
        {
            const double presence=InOutSine(e.presence);
            if(presence<=0)continue;
            const double fullWidth=StatusIndicatorCapsuleWidth(e.textWidth,mainWidth);
            const double overflowText=StatusIndicatorTextOverflow(e.textWidth,mainWidth);
            const bool selected=e.phase==StatusIndicatorPhase::Docked&&e.select>0;
            const double scroll=StatusIndicatorScrollOffset(overflowText,selected?now-e.selectStart:e.scrollElapsed);
            const double frontness=Clamp01(1-e.stack.position);
            Geometry g;double opacity=1,textOpacity=0;int layer=3;double sub=-static_cast<double>(e.order);
            bool overflow=false;
            const bool fromDock=e.phase==StatusIndicatorPhase::Docked||(e.phase==StatusIndicatorPhase::Exiting&&e.collapse>=1);

            if(fromDock)
            {
                g=dockedVisual(e,opacity);layer=1;sub=static_cast<double>(e.dockSerial);overflow=fanP>0;
                if(selected)
                {
                    // 选中：图标圆落在轨道圆上，文字段向左展开，可越界但夹在工作区内。
                    const double s=e.select;
                    const double width=Lerp(CapsuleCircle,fullWidth,OutBack(s));
                    g=Capsule(g.cx+CapsuleCircle/2,g.cy,width,fullWidth,e.textWidth,scroll);
                    g.FitInto(input.work);
                    opacity=Lerp(opacity,1,InOutSine(s));textOpacity=InOutSine(s);layer=5;overflow=true;
                }
            }
            else
            {
                const double appearScale=Lerp(EnterScale,1,OutBack(e.appear));
                const double centerY=stackCenterY(e);
                const double capsuleRight=cx+fullWidth/2;
                opacity=OutSine(e.appear);textOpacity=frontness;
                if(e.collapse<=0)g=Capsule(capsuleRight,centerY,fullWidth,fullWidth,e.textWidth,scroll);
                else if(e.collapse<.5)
                {
                    // 第一段：文字段向右收进圆形下方并淡出。
                    const double u=InOutSine(e.collapse/.5);
                    g=Capsule(capsuleRight,centerY,Lerp(fullWidth,CapsuleCircle,u),fullWidth,e.textWidth,scroll);
                    textOpacity*=1-u;
                }
                else
                {
                    // 第二段：圆形移至右上角槽位，变小并降到半透明。
                    const double v=InOutCubic((e.collapse-.5)/.5);
                    double dockOpacity=DockedOpacity;const auto target=dockedVisual(e,dockOpacity);
                    g=Circle(LerpPoint({capsuleRight-CapsuleCircle/2,centerY},{target.cx,target.cy},v),Lerp(CapsuleCircle,target.radius*2,v));
                    opacity*=Lerp(1,dockOpacity,v);textOpacity=0;overflow=fanP>0;
                }
                g.Scale(appearScale);
                if(e.phase==StatusIndicatorPhase::Expanded)layer=3;
                else{layer=e.collapse>=.5?2:4;sub=static_cast<double>(e.order);}
            }
            if(e.phase==StatusIndicatorPhase::Dismissing)
            {
                // 通知：反向回弹缩小并渐隐。
                g.Scale((std::max)(0.0,1-InBack(e.vanish)));opacity*=1-InOutSine(e.vanish);
            }
            else if(e.phase==StatusIndicatorPhase::Exiting)
            {
                g.Scale(1-.5*InOutSine(e.vanish));opacity*=1-InOutSine(e.vanish);
            }
            if(!overflow&&baseValid)g.FitInto(base);
            opacity*=presence;
            if(opacity<=0||g.rect.Empty())continue;
            Item item;item.layer=layer;item.sub=sub;
            auto& l=item.layout;
            l.key=e.spec.key;l.kind=e.spec.kind;l.icon=e.spec.icon;l.accentArgb=e.spec.accentArgb;l.shape=g.shape;
            l.bounds=g.rect;l.textClip=g.textClip;l.textX=g.textX;l.scrollOffset=g.shape==StatusIndicatorShape::Capsule?scroll:0;
            l.textOpacity=g.shape==StatusIndicatorShape::Capsule?Clamp01(textOpacity):0;
            l.iconCenterX=g.cx;l.iconCenterY=g.cy;l.iconRadius=g.radius;l.opacity=Clamp01(opacity);l.scale=g.scale;l.overflow=overflow;
            items.push_back(std::move(item));
        }
        std::stable_sort(items.begin(),items.end(),[](const Item& a,const Item& b){ return a.layer!=b.layer?a.layer<b.layer:a.sub<b.sub; });
        out.entries.reserve(items.size());
        for(auto& item:items)
        {
            item.layout.z=static_cast<int>(out.entries.size());
            auto& bounds=item.layout.overflow?out.overflowBounds:out.normalBounds;
            bounds=UnionStatusIndicatorRect(bounds,item.layout.bounds);
            out.entries.push_back(std::move(item.layout));
        }

        const auto& ring=model.Ring();
        const double ringFade=InOutSine(ring.opacity);
        out.ring.owner=ring.owner;out.ring.argb=model.RingColor();
        out.ring.opacity=ringFade*(model.Animations()?model.Breath():1.0);
        out.ring.width=Lerp(RingBaseWidth,RingWidth,ringFade);
        const auto& progress=model.Progress();
        if(const auto* e=progress.key?model.Find(progress.key):nullptr)
        {
            out.progress.key=progress.key;out.progress.argb=e->spec.accentArgb;out.progress.value=Clamp01(progress.display);
            const bool verifying=progress.display>=1&&!progress.complete&&model.Animations();
            out.progress.opacity=InOutSine(progress.opacity)*(verifying?model.Breath():1.0);
        }
        return out;
    }
}
