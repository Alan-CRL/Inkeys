#pragma once
#include <algorithm>
#include <array>
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
    // 胶囊与方块均为圆角矩形；Tile 为只含图标的方块（停靠、飞行、同心弧）。
    enum class StatusIndicatorShape : std::uint8_t { Capsule, Tile };
    // 命中角色：胶囊点击收起；停靠方块点击重新展开/打开弧；弧上方块点选。
    enum class StatusIndicatorRole : std::uint8_t { None, Capsule, Dock, Fan };

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

    // 尺寸为 DIP，时长为秒；2026-11-04 v2 预览确认值（research/indicator-v2-preview.html）。
    namespace StatusIndicatorStyle
    {
        inline constexpr std::size_t Capacity=25;
        inline constexpr double Hold=3.0;
        // 胶囊同“底栏模式”指示器：高 30、圆角 6、13 DIP 粗体；内边距取高度扣除行高后的单侧余量。
        inline constexpr double CapsuleHeight=30,CapsuleRadius=6,CapsuleIcon=18,CapsuleGap=6,TextSize=13,TextLineHeight=17;
        inline constexpr double CapsulePadding=(CapsuleHeight-TextLineHeight)/2,CapsuleMaxWidth=160,TextFade=8;
        inline constexpr double StackStep=4,StackLayers=3,EnterScale=.35,EnterPeakScale=1.12;
        inline constexpr double DockSize=18,DockRadius=4,DockIcon=12,DockStep=6,DockInset=14,DockOpacity=.5,DockHitMin=24,MainButtonYield=3;
        inline constexpr double FanSize=28,FanRadius=6,FanIcon=17,FanGap=4,FanClear=6,FanLayerGap=32,FanSpanDegrees=130;
        inline constexpr double FrameWidth=1,RingWidth=1.5,BreathPeriod=1.6,ScrollPause=.3;
        inline constexpr double Enter=.42,Dismiss=.30,Exit=.28,Fan=.32,Select=.24,RingFade=.24,Presence=.28;
        inline constexpr double FanAutoClose=3.0,ProgressCompleteLinger=3.0,ProgressRate=8;
        // 收起：文字段欠阻尼收进图标，方块延迟后沿弧线飞向停靠槽，着陆轻弹。
        inline constexpr double FlightDelay=.10,FlightLift=12,LandAt=.97,LandBumpVelocity=1.6;
        inline constexpr double SpringStep=1.0/120,SpringMaxDt=.5;
        inline constexpr double Pi=3.14159265358979323846;
    }

    // 正式状态源的固定 key 与强调色（PowerPoint 橘红、WPS 红、超级置顶深蓝、新版本绿）。
    namespace StatusIndicatorSources
    {
        inline constexpr std::uint64_t SuperTopKey=0x5354000000000001ULL,NewVersionKey=0x4E56000000000001ULL,PptKeyBase=0x5050000000000000ULL;
        inline constexpr std::uint32_t PowerPointArgb=0xFFC43E1Cu,WpsArgb=0xFFE8352Bu,SuperTopArgb=0xFF1F4FB8u,NewVersionArgb=0xFF2EA043u;
        inline constexpr std::uint64_t PptKey(std::uint64_t session) noexcept { return PptKeyBase|(session&0x0000FFFFFFFFFFFFULL); }
    }

    struct StatusIndicatorSpringParams { double omega=18,damping=.72; };
    namespace StatusIndicatorSprings
    {
        // 槽位与底栏吸附弹簧同参数。
        inline constexpr StatusIndicatorSpringParams Slot{18,.72},Morph{20,.62},Flight{13,.74},Bump{26,.35},Orientation{16,.8};
    }

    namespace StatusIndicatorCurve
    {
        inline double Clamp01(double t) noexcept { return std::isfinite(t)?std::clamp(t,0.0,1.0):0.0; }
        inline double OutSine(double t) noexcept { return std::sin(Clamp01(t)*StatusIndicatorStyle::Pi/2); }
        inline double InSine(double t) noexcept { return 1-std::cos(Clamp01(t)*StatusIndicatorStyle::Pi/2); }
        inline double InOutSine(double t) noexcept { return (1-std::cos(StatusIndicatorStyle::Pi*Clamp01(t)))/2; }
        inline double OutBack(double t) noexcept { const double s=Clamp01(t)-1;return 1+2.1*s*s*s+1.1*s*s; }
        inline double InBack(double t) noexcept { t=Clamp01(t);return 2.1*t*t*t-1.1*t*t; }
        inline double SmoothStep(double t) noexcept { t=Clamp01(t);return t*t*(3-2*t); }
        inline double SmoothRange(double a,double b,double x) noexcept { return SmoothStep((x-a)/(b-a)); }
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

    // 强调显示色：在 OKLCH 中按主按钮材质校正明度，深色底提亮、浅色底压暗，保证图标/文字/边框可读。
    namespace StatusIndicatorColor
    {
        inline double ToLinear(double c) noexcept { c/=255;return c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4); }
        inline double ToSrgb(double c) noexcept
        {
            c=c<=.0031308?12.92*c:1.055*std::pow((std::max)(0.0,c),1/2.4)-.055;
            return std::round(std::clamp(c,0.0,1.0)*255);
        }
        inline std::array<double,3> ToOklch(std::uint32_t argb) noexcept
        {
            const double r=ToLinear((argb>>16)&255),g=ToLinear((argb>>8)&255),b=ToLinear(argb&255);
            const double l=std::cbrt(.4122214708*r+.5363325363*g+.0514459929*b);
            const double m=std::cbrt(.2119034982*r+.6806995451*g+.1073969566*b);
            const double s=std::cbrt(.0883024619*r+.2817188376*g+.6299787005*b);
            const double L=.2104542553*l+.7936177850*m-.0040720468*s;
            const double A=1.9779984951*l-2.4285922050*m+.4505937099*s;
            const double B=.0259040371*l+.7827717662*m-.8086757660*s;
            return {L,std::hypot(A,B),std::atan2(B,A)};
        }
        inline std::uint32_t FromOklch(std::array<double,3> lch,std::uint32_t alpha) noexcept
        {
            const double A=lch[1]*std::cos(lch[2]),B=lch[1]*std::sin(lch[2]);
            const double l=std::pow(lch[0]+.3963377774*A+.2158037573*B,3);
            const double m=std::pow(lch[0]-.1055613458*A-.0638541728*B,3);
            const double s=std::pow(lch[0]-.0894841775*A-1.2914855480*B,3);
            const auto r=static_cast<std::uint32_t>(ToSrgb(4.0767416621*l-3.3077115913*m+.2309699292*s));
            const auto g=static_cast<std::uint32_t>(ToSrgb(-1.2684380046*l+2.6097574011*m-.3413193965*s));
            const auto b=static_cast<std::uint32_t>(ToSrgb(-.0041960863*l-.7034186147*m+1.7076147010*s));
            return (alpha<<24)|(r<<16)|(g<<8)|b;
        }
        inline std::uint32_t Display(std::uint32_t argb,double lightMaterial) noexcept
        {
            const auto lch=ToOklch(argb);const std::uint32_t alpha=(argb>>24)&255;
            const auto dark=FromOklch({(std::max)(lch[0],.72),(std::min)(lch[1],.16),lch[2]},alpha);
            const auto light=FromOklch({(std::min)(lch[0],.52),(std::min)(lch[1],.17),lch[2]},alpha);
            return StatusIndicatorCurve::LerpArgb(dark,light,lightMaterial);
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

    // 120Hz 子步弹簧，帧率无关；单位由调用方决定（槽位、DIP 或 0..1 进度）。
    struct StatusIndicatorSpring
    {
        double position=0,velocity=0;
        void Snap(double target) noexcept { position=target;velocity=0; }
        bool Settled(double target,double epsilon=.002) const noexcept
        { return std::abs(position-target)<=epsilon&&std::abs(velocity)<=epsilon*10; }
        bool Advance(double target,double scaledDt,bool animations,
            StatusIndicatorSpringParams params=StatusIndicatorSprings::Slot,double epsilon=.002) noexcept
        {
            const double oldPosition=position,oldVelocity=velocity;
            if(!animations||!std::isfinite(position)||!std::isfinite(velocity))Snap(target);
            else
            {
                using namespace StatusIndicatorStyle;
                double remaining=std::clamp(std::isfinite(scaledDt)?scaledDt:0.0,0.0,SpringMaxDt);
                while(remaining>0)
                {
                    const double step=(std::min)(remaining,SpringStep);
                    velocity+=(params.omega*params.omega*(target-position)-2*params.damping*params.omega*velocity)*step;
                    position+=velocity*step;remaining-=step;
                }
                if(Settled(target,epsilon))Snap(target);
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
        bool landed=false;   // 飞行方块已着陆并占据停靠槽
        std::uint64_t order=0,dockSerial=0;
        double holdStart=0,holdEnd=0,scrollElapsed=0,selectStart=0;
        // 0..1 线性进度，曲线在布局中解析，中途反向保持连续。
        double appear=0,vanish=0,presence=1,select=0;
        double stackTarget=0,dockTarget=0,frozenFanSlot=0;
        // morph：0 完整胶囊 → 1 只剩图标方块；flight：0 锚点 → 1 停靠槽。两者为弹簧，可轻微过冲。
        double morphTarget=0,flightTarget=0,morphDelay=0,flightDelay=0;
        StatusIndicatorSpring stack,dock,width,morph,flight,bump{1,0};
        double textWidth=0;  // 渲染器测量后写入（DIP）
        bool Leaving() const noexcept { return phase==StatusIndicatorPhase::Dismissing||phase==StatusIndicatorPhase::Exiting; }
        bool InDock() const noexcept
        {
            return visible&&(phase==StatusIndicatorPhase::Docked||(phase==StatusIndicatorPhase::Collapsing&&landed));
        }
        // 正在收起/重新展开的过渡几何（胶囊↔方块）。
        bool Morphing() const noexcept
        {
            return phase==StatusIndicatorPhase::Collapsing||morphTarget<1||morph.position<1-1e-6||flight.position<1-1e-6;
        }
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

    inline double StatusIndicatorTextLimit() noexcept
    {
        using namespace StatusIndicatorStyle;
        return CapsuleMaxWidth-2*CapsulePadding-CapsuleIcon-CapsuleGap;
    }
    inline double StatusIndicatorCapsuleWidth(double textWidth) noexcept
    {
        using namespace StatusIndicatorStyle;
        textWidth=std::isfinite(textWidth)?(std::max)(0.0,textWidth):0.0;
        return std::clamp(2*CapsulePadding+CapsuleIcon+CapsuleGap+textWidth,CapsuleHeight,CapsuleMaxWidth);
    }
    inline double StatusIndicatorTextOverflow(double textWidth) noexcept
    {
        return std::isfinite(textWidth)?(std::max)(0.0,textWidth-StatusIndicatorTextLimit()):0.0;
    }
    // 两端各停顿 ScrollPause，中段平滑滚完，保证 Hold 内结束。
    inline double StatusIndicatorScrollOffset(double overflow,double elapsed) noexcept
    {
        using namespace StatusIndicatorStyle;
        if(!(overflow>0))return 0;
        return overflow*StatusIndicatorCurve::SmoothStep((elapsed-ScrollPause)/(Hold-2*ScrollPause));
    }
    // 超椭圆 |x/a|^n+|y/b|^n=1 在方向 theta 上的半径与横坐标 dx 处的半高。
    inline double StatusIndicatorSuperRadius(double theta,double a,double b,double n) noexcept
    {
        const double c=std::abs(std::cos(theta))/(std::max)(a,1e-6),s=std::abs(std::sin(theta))/(std::max)(b,1e-6);
        return 1/std::pow(std::pow(c,n)+std::pow(s,n),1/n);
    }
    inline double StatusIndicatorSuperHalfHeight(double dx,double a,double b,double n) noexcept
    {
        const double t=(std::min)(1.0,std::abs(dx)/(std::max)(a,1e-6));
        return b*std::pow((std::max)(0.0,1-std::pow(t,n)),1/n);
    }
    // 同心弧第 layer 层半径：首层紧贴主按钮外侧（取 ±50° 处超椭圆外径，避开圆角）。
    inline double StatusIndicatorFanLayerRadius(double mainWidth,double mainHeight,double n,std::size_t layer) noexcept
    {
        using namespace StatusIndicatorStyle;
        const double a=(std::isfinite(mainWidth)?(std::max)(0.0,mainWidth):0.0)/2,b=(std::isfinite(mainHeight)?(std::max)(0.0,mainHeight):0.0)/2;
        const double r0=(a>0&&b>0?StatusIndicatorSuperRadius(50*Pi/180,a,b,std::isfinite(n)&&n>0?n:3):0)+FanClear+FanSize/2;
        return r0+static_cast<double>(layer)*FanLayerGap;
    }
    inline std::size_t StatusIndicatorFanLayerCapacity(double radius) noexcept
    {
        using namespace StatusIndicatorStyle;
        if(!(radius>0))return 1;
        const double step=(FanSize+FanGap)/radius;
        return static_cast<std::size_t>(std::floor(FanSpanDegrees*Pi/180/step))+1;
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
                if(e.phase!=StatusIndicatorPhase::Docked)SnapDocked(e);
                if(e.presence<=0)e.snapDock=true;
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
                    e->vanish=0;
                    if(e->landed||e->dockSerial)e->phase=StatusIndicatorPhase::Docked;else e->phase=StatusIndicatorPhase::Collapsing;
                }
                return StatusIndicatorPublishResult::Updated;
            }
            const bool visible=Allows(spec.kind);
            if(!visible&&spec.lifetime==StatusIndicatorLifetime::Notification)return StatusIndicatorPublishResult::Rejected;
            if(entries_.size()>=StatusIndicatorStyle::Capacity&&!Evict())return StatusIndicatorPublishResult::Rejected;
            StatusIndicatorEntry e;e.spec=spec;e.order=++order_;e.visible=visible;
            if(visible){e.phase=StatusIndicatorPhase::Expanded;e.snapStack=true;}
            else{SnapDocked(e);e.presence=0;}
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

        // 返回需要由调用方执行的动作。
        StatusIndicatorAction ClickDocked(std::uint64_t key,double now) noexcept
        {
            now_=now;
            auto* e=FindMutable(key);
            if(!e||!e->visible||e->phase!=StatusIndicatorPhase::Docked)return StatusIndicatorAction::None;
            if(!fanOpen_)
            {
                const auto docked=DockedCount();
                if(docked==1)
                {
                    if(e->spec.action!=StatusIndicatorAction::None)return e->spec.action;
                    // 单个方块就地重新展开胶囊；再点或 Hold 到期收回。
                    if(selected_==key){CloseReexpand(*e);return StatusIndicatorAction::None;}
                    selected_=key;e->selectStart=now;
                    e->flightTarget=0;e->flightDelay=0;e->morphTarget=0;e->morphDelay=StatusIndicatorStyle::FlightDelay;
                    return StatusIndicatorAction::None;
                }
                if(docked>=2){fanOpen_=true;frozenFanCount_=0;fanDeadline_=now+StatusIndicatorStyle::FanAutoClose;}
                return StatusIndicatorAction::None;
            }
            fanDeadline_=now+StatusIndicatorStyle::FanAutoClose;
            if(e->spec.action!=StatusIndicatorAction::None){fanOpen_=false;selected_=0;return e->spec.action;}
            if(selected_){selected_=0;return StatusIndicatorAction::None;}
            selected_=key;e->selectStart=now;
            return StatusIndicatorAction::None;
        }
        bool ClickExpanded(std::uint64_t key,double now) noexcept
        {
            now_=now;
            auto* e=FindMutable(key);
            if(!e||!e->visible)return false;
            if(e->phase==StatusIndicatorPhase::Docked&&selected_==key)
            {
                if(Reexpanded())CloseReexpand(*e);
                else{selected_=0;fanDeadline_=now+StatusIndicatorStyle::FanAutoClose;}
                return true;
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
            if(auto* e=Reexpanded()?FindMutable(selected_):nullptr)CloseReexpand(*e);
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
            if(auto* e=Reexpanded()?FindMutable(selected_):nullptr;e&&now>=e->selectStart+Hold){CloseReexpand(*e);changed=true;}
            for(auto& e:entries_)
            {
                if(e.holding)e.scrollElapsed=now-e.holdStart;
                if(e.phase==StatusIndicatorPhase::Expanded||e.phase==StatusIndicatorPhase::Collapsing)
                    changed|=StepStatusIndicatorProgress(e.appear,1,sdt,Enter,animations);
                changed|=AdvanceMorph(e,sdt,animations);
                changed|=StepStatusIndicatorProgress(e.vanish,e.Leaving()?1.0:0.0,sdt,
                    e.phase==StatusIndicatorPhase::Dismissing?Dismiss:Exit,animations);
                changed|=StepStatusIndicatorProgress(e.presence,e.visible?1.0:0.0,sdt,Presence,animations);
                if(!e.visible&&e.presence<=0&&!e.Leaving()&&e.spec.lifetime==StatusIndicatorLifetime::Persistent
                    &&e.phase!=StatusIndicatorPhase::Docked)
                {
                    SnapDocked(e);changed=true;
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
            for(auto& e:entries_)
                changed|=StepStatusIndicatorProgress(e.select,e.spec.key==selected_&&fanOpen_?1.0:0.0,sdt,Select,animations);

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
                if(e.vanish!=0||e.presence!=(e.visible?1.0:0.0))return true;
                if(e.select!=(e.spec.key==selected_&&fanOpen_?1.0:0.0))return true;
                if(e.morph.position!=e.morphTarget||e.flight.position!=e.flightTarget||e.morph.velocity!=0||e.flight.velocity!=0)return true;
                if(e.bump.position!=1||e.bump.velocity!=0)return true;
                if(e.visible&&e.phase==StatusIndicatorPhase::Expanded&&(!e.stack.Settled(e.stackTarget)||e.width.velocity!=0))return true;
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
            if(Reexpanded())if(const auto* e=Find(selected_))deadline=(std::min)(deadline,e->selectStart+StatusIndicatorStyle::Hold);
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
        // 单方块就地重新展开（无同心弧时的选中）。
        bool Reexpanded() const noexcept { return selected_&&!fanOpen_&&fan_<=0; }
        const StatusIndicatorRingState& Ring() const noexcept { return ring_; }
        const StatusIndicatorProgressState& Progress() const noexcept { return progress_; }
        double Now() const noexcept { return now_; }
        bool Animations() const noexcept { return animations_; }
        bool DrawingActive() const noexcept { return drawing_; }
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

        void SnapDocked(StatusIndicatorEntry& e) noexcept
        {
            e.phase=StatusIndicatorPhase::Docked;e.appear=1;e.holding=false;e.landed=true;
            e.morphTarget=e.flightTarget=1;e.morph.Snap(1);e.flight.Snap(1);e.bump.Snap(1);
            e.morphDelay=e.flightDelay=0;
            if(!e.dockSerial)e.dockSerial=++dockSerial_;
            e.snapDock=true;
        }
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
            e.phase=StatusIndicatorPhase::Collapsing;e.landed=false;
            e.morphTarget=1;e.morphDelay=0;e.flightTarget=1;e.flightDelay=StatusIndicatorStyle::FlightDelay;
        }
        void CloseReexpand(StatusIndicatorEntry& e) noexcept
        {
            if(selected_==e.spec.key)selected_=0;
            e.morphTarget=1;e.morphDelay=0;e.flightTarget=1;e.flightDelay=StatusIndicatorStyle::FlightDelay;
        }
        bool AdvanceMorph(StatusIndicatorEntry& e,double sdt,bool animations) noexcept
        {
            using namespace StatusIndicatorStyle;
            bool changed=false;
            const auto step=[&](StatusIndicatorSpring& spring,double target,double& delay,StatusIndicatorSpringParams params)
            {
                if(!animations||(spring.position==target&&spring.velocity==0))delay=0;
                // 延迟在帧内到期时只推进剩余时间，保证不同帧率同一时刻几何一致。
                double remaining=sdt;
                if(delay>0){const double used=(std::min)(delay,remaining);delay-=used;remaining-=used;if(delay>0)return;}
                changed|=spring.Advance(target,remaining,animations,params);
            };
            const double before=e.flight.position;
            step(e.morph,e.morphTarget,e.morphDelay,StatusIndicatorSprings::Morph);
            step(e.flight,e.flightTarget,e.flightDelay,StatusIndicatorSprings::Flight);
            // 向停靠槽越过着陆点：新收起者此刻才占槽（其余方块随即让位），并轻弹一次。
            if(e.flightTarget>=1&&before<LandAt&&e.flight.position>=LandAt)
            {
                if(!e.landed){e.landed=true;e.dockSerial=++dockSerial_;e.snapDock=true;}
                if(animations)e.bump.velocity+=LandBumpVelocity;
                changed=true;
            }
            changed|=e.bump.Advance(1,sdt,animations,StatusIndicatorSprings::Bump);
            if(e.phase==StatusIndicatorPhase::Collapsing&&e.landed&&e.morph.position==1&&e.flight.position==1
                &&e.morph.velocity==0&&e.flight.velocity==0)
            {
                e.phase=StatusIndicatorPhase::Docked;changed=true;
            }
            return changed;
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
                // 注销会改变弧的离散容量；退场期间沿用原槽位，避免方块先跳到新角度再收起。
                for(auto& e:entries_)if(e.InDock()){e.frozenFanSlot=e.dock.position;++frozenFanCount_;}
            }
            fanOpen_=false;selected_=0;
        }
        bool AdvanceSlots(double sdt,bool animations) noexcept
        {
            using namespace StatusIndicatorStyle;
            bool changed=false;double rank=0;
            const auto* front=Front();
            const double frontWidth=front?StatusIndicatorCapsuleWidth(front->textWidth):CapsuleHeight;
            for(auto& e:entries_)
            {
                if(!e.visible||e.phase!=StatusIndicatorPhase::Expanded)continue;
                e.stackTarget=rank++;
                // 后排与队首等宽，只露出外侧一条；成为队首后展开到自身宽度。
                const double widthTarget=&e==front?StatusIndicatorCapsuleWidth(e.textWidth):frontWidth;
                if(e.snapStack){e.stack.Snap(e.stackTarget);e.width.Snap(widthTarget);e.snapStack=false;changed=true;}
                else
                {
                    changed|=e.stack.Advance(e.stackTarget,sdt,animations);
                    changed|=e.width.Advance(widthTarget,sdt,animations,StatusIndicatorSprings::Slot,.05);
                }
            }
            // 停靠：dockSerial 越大越新，槽位 0 在最右；只有已着陆者占槽。
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
                if(StatusIndicatorTextOverflow(e.textWidth)<=0)continue;
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
        double now_=0,fan_=0,fanDeadline_=0;
        bool fanOpen_=false,drawing_=false,animations_=true;
    };

    // 新版本状态源：把更新模块的轮询快照归一为发布/进度/注销动作。
    // state 取 AutomaticUpdateStateEnum 的整数值，避免纯策略依赖更新模块。
    struct StatusIndicatorUpdateAction
    {
        bool publish=false,retire=false,progress=false,complete=false;
        double value=-1; // progress 为 true 时有效；<0 表示本周期结束（失败/未静默）
    };
    class StatusIndicatorUpdateTracker
    {
    public:
        enum State : int { NotStarted=0,Downloading=5,DownloadFail=6,DownloadDamage=7,Restart=8,Latest=9,Newer=10,New=11 };
        bool Published() const noexcept { return published_; }
        bool IsDownloading() const noexcept { return lastState_==Downloading; }
        StatusIndicatorUpdateAction Step(int state,std::uint64_t downloaded,std::uint64_t size) noexcept
        {
            StatusIndicatorUpdateAction a;
            const auto publish=[&]{ if(!published_){a.publish=true;published_=true;} };
            switch(state)
            {
            case New:
                // 未开启静默更新：检测到即弹出，没有进度环。
                publish();
                if(lastValue_>=0||completeSent_){a.progress=true;a.value=-1;lastValue_=-1;completeSent_=false;}
                break;
            case Downloading:
                // 静默下载：开始且进度大于 0 时才出现；同周期只上报增长。
                if(size>0&&downloaded>0)
                {
                    const double value=(std::min)(1.0,static_cast<double>(downloaded)/static_cast<double>(size));
                    publish();
                    if(lastValue_<0||value-lastValue_>=.002){a.progress=true;a.value=value;lastValue_=value;}
                }
                break;
            case Restart:
                // 已下载待重启安装：新版本仍存在，进度完成后 3s 淡出由模型处理。
                publish();
                if(!completeSent_){a.progress=true;a.complete=true;a.value=1;completeSent_=true;}
                break;
            case DownloadFail:
            case DownloadDamage:
                if(published_&&lastValue_>=0){a.progress=true;a.value=-1;lastValue_=-1;}
                break;
            case Latest:
            case Newer:
                if(published_){a.retire=true;published_=false;lastValue_=-1;completeSent_=false;}
                break;
            default:break; // 检查中/信息失败：保持现状
            }
            lastState_=state;
            return a;
        }
    private:
        bool published_=false,completeSent_=false;
        int lastState_=-1;
        double lastValue_=-1;
    };

    // ---------------- 纯布局 ----------------

    struct StatusIndicatorRect
    {
        double left=0,top=0,right=0,bottom=0;
        double Width() const noexcept { return right-left; }
        double Height() const noexcept { return bottom-top; }
        bool Empty() const noexcept { return !(right>left&&bottom>top); }
        bool Intersects(const StatusIndicatorRect& o) const noexcept
        { return !Empty()&&!o.Empty()&&left<o.right&&o.left<right&&top<o.bottom&&o.top<bottom; }
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
    inline StatusIndicatorRect OutsetStatusIndicatorRect(const StatusIndicatorRect& r,double d) noexcept
    {
        return r.Empty()?r:StatusIndicatorRect{r.left-d,r.top-d,r.right+d,r.bottom+d};
    }

    struct StatusIndicatorLayoutInput
    {
        double mainCenterX=0,mainCenterY=0,mainWidth=80,mainHeight=80,mainN=3; // 主按钮当前动画值
        double below=0;                 // 倒转（主栏在屏幕上方）：0 正常向上，1 向下；过渡值用于换边动画
        StatusIndicatorRect baseBounds; // 不含指示器的正常态内容边界
        StatusIndicatorRect work;       // 显示器工作区
    };

    struct StatusIndicatorEntryLayout
    {
        std::uint64_t key=0;
        StatusIndicatorKind kind=StatusIndicatorKind::Debug;
        StatusIndicatorIcon icon=StatusIndicatorIcon::Info;
        StatusIndicatorShape shape=StatusIndicatorShape::Tile;
        StatusIndicatorRole role=StatusIndicatorRole::None;
        std::uint32_t accentArgb=0;
        StatusIndicatorRect bounds;   // 圆角矩形外形（已含缩放）
        double radius=0;              // 圆角
        double iconCenterX=0,iconCenterY=0,iconSize=0;
        StatusIndicatorRect textClip; // 文字区（胶囊内侧），宽度为 0 时不绘制
        double textX=0,textScale=1,textOpacity=0,scrollOffset=0;
        bool fadeLeft=false,fadeRight=false; // 滚动时两端渐隐
        double opacity=0;             // 整项（含文字、图标、边框）单次合成透明度
        int z=0;                      // 绘制顺序（升序），上层外形遮挡下层
        bool overflow=false;          // 允许越过 baseBounds（胶囊、同心弧、飞行中）
    };

    struct StatusIndicatorRingLayout { std::uint64_t owner=0;std::uint32_t argb=0;double opacity=0,width=StatusIndicatorStyle::RingWidth; };
    struct StatusIndicatorProgressLayout { std::uint64_t key=0;std::uint32_t argb=0;double value=0,opacity=0,width=StatusIndicatorStyle::RingWidth; };

    struct StatusIndicatorFanPlacement { double x=0,y=0;std::size_t layer=0;bool mirrored=false; };

    struct StatusIndicatorLayout
    {
        std::vector<StatusIndicatorEntryLayout> entries; // 已按 z 升序
        StatusIndicatorRingLayout ring;
        StatusIndicatorProgressLayout progress;
        StatusIndicatorRect normalBounds,overflowBounds;
        StatusIndicatorRect reserveBounds; // 越界几何的动画峰值包络（首帧即预留）
        bool fanMirrored=false;
    };

    // 点是否位于主按钮超椭圆内且距边框超过 inset（停靠方块的命中让给主按钮）。
    inline bool StatusIndicatorInsideMainButton(const StatusIndicatorLayoutInput& input,double x,double y,double inset) noexcept
    {
        const double a=input.mainWidth/2-inset,b=input.mainHeight/2-inset,n=input.mainN>0?input.mainN:3;
        if(!(a>0&&b>0))return false;
        return std::pow(std::abs(x-input.mainCenterX)/a,n)+std::pow(std::abs(y-input.mainCenterY)/b,n)<1;
    }
    inline double StatusIndicatorDirection(const StatusIndicatorLayoutInput& input) noexcept
    { return input.below>=.5?1.0:-1.0; }
    // 胶囊竖直中心：主按钮上边框（倒转为下边框），换边时插值。
    inline double StatusIndicatorEdgeY(const StatusIndicatorLayoutInput& input) noexcept
    {
        const double b=StatusIndicatorCurve::Clamp01(input.below);
        return input.mainCenterY+StatusIndicatorCurve::Lerp(-1.0,1.0,b)*input.mainHeight/2;
    }

    // 以主按钮中心为圆心的紧凑同心弧；以正上（倒转正下）为中心对称，index 0 为最新、位于最内层最右。
    inline StatusIndicatorFanPlacement ResolveStatusIndicatorFanPlacement(
        std::size_t index,std::size_t count,const StatusIndicatorLayoutInput& input) noexcept
    {
        using namespace StatusIndicatorStyle;
        count=(std::max)(count,std::size_t{1});index=(std::min)(index,count-1);
        StatusIndicatorFanPlacement out;
        std::size_t layerIndex=0,layerCount=1;
        double radius=StatusIndicatorFanLayerRadius(input.mainWidth,input.mainHeight,input.mainN,0),outer=radius;
        for(std::size_t layer=0,begin=0;begin<count;++layer)
        {
            const double r=StatusIndicatorFanLayerRadius(input.mainWidth,input.mainHeight,input.mainN,layer);
            const std::size_t n=(std::min)(StatusIndicatorFanLayerCapacity(r),count-begin);
            if(index>=begin&&index<begin+n){out.layer=layer;layerIndex=index-begin;layerCount=n;radius=r;}
            outer=r;begin+=n;
        }
        double dir=StatusIndicatorDirection(input);
        const bool workValid=!input.work.Empty();
        // 朝向侧工作区不足时整体镜像。
        if(workValid&&((dir<0&&input.mainCenterY-outer-FanSize/2<input.work.top)||(dir>0&&input.mainCenterY+outer+FanSize/2>input.work.bottom)))
        {dir=-dir;out.mirrored=true;}
        const double step=(FanSize+FanGap)/radius;
        const double theta=Pi/2-((static_cast<double>(layerCount)-1)/2-static_cast<double>(layerIndex))*step;
        out.x=input.mainCenterX+radius*std::cos(theta);
        out.y=input.mainCenterY+dir*radius*std::sin(theta);
        if(workValid)
        {
            const double r=FanSize/2;
            if(input.work.Width()>=FanSize)out.x=std::clamp(out.x,input.work.left+r,input.work.right-r);
            if(input.work.Height()>=FanSize)out.y=std::clamp(out.y,input.work.top+r,input.work.bottom-r);
        }
        return out;
    }

    namespace StatusIndicatorLayoutDetail
    {
        struct Point { double x=0,y=0; };
        inline Point LerpPoint(Point a,Point b,double t) noexcept { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t}; }
        inline Point Bezier(Point a,Point c,Point b,double t) noexcept
        {
            const double u=1-t;
            return {u*u*a.x+2*u*t*c.x+t*t*b.x,u*u*a.y+2*u*t*c.y+t*t*b.y};
        }
        inline StatusIndicatorRect Box(Point c,double w,double h) noexcept { return {c.x-w/2,c.y-h/2,c.x+w/2,c.y+h/2}; }
        inline StatusIndicatorRect ScaleRect(const StatusIndicatorRect& r,double s,Point pivot) noexcept
        {
            return {pivot.x+(r.left-pivot.x)*s,pivot.y+(r.top-pivot.y)*s,pivot.x+(r.right-pivot.x)*s,pivot.y+(r.bottom-pivot.y)*s};
        }
        inline void ScaleLayout(StatusIndicatorEntryLayout& l,double s,Point pivot) noexcept
        {
            l.bounds=ScaleRect(l.bounds,s,pivot);l.textClip=ScaleRect(l.textClip,s,pivot);
            l.iconCenterX=pivot.x+(l.iconCenterX-pivot.x)*s;l.iconCenterY=pivot.y+(l.iconCenterY-pivot.y)*s;
            l.textX=pivot.x+(l.textX-pivot.x)*s;l.radius*=s;l.iconSize*=s;l.textScale*=s;l.scrollOffset*=s;
        }
        // 胶囊：左侧图标方块，右侧文字区；文字窄时在文字区居中、超限时按 scroll 左移。
        inline void Capsule(StatusIndicatorEntryLayout& l,const StatusIndicatorRect& rect,double textWidth,double fullWidth,double scroll) noexcept
        {
            using namespace StatusIndicatorStyle;
            const double h=rect.Height(),s=h/CapsuleHeight,cy=(rect.top+rect.bottom)/2;
            l.shape=StatusIndicatorShape::Capsule;l.bounds=rect;l.radius=CapsuleRadius*s;
            l.iconSize=CapsuleIcon*s;l.iconCenterX=rect.left+h/2;l.iconCenterY=cy;
            const double clipLeft=rect.left+(CapsulePadding+CapsuleIcon+CapsuleGap)*s,clipRight=rect.right-CapsulePadding*s;
            const double fullClip=(std::max)(0.0,(fullWidth-2*CapsulePadding-CapsuleIcon-CapsuleGap)*s);
            l.textClip={clipLeft,rect.top,(std::max)(clipLeft,clipRight),rect.bottom};
            l.textScale=s;
            const double overflow=(std::max)(0.0,textWidth-(fullWidth-2*CapsulePadding-CapsuleIcon-CapsuleGap));
            l.scrollOffset=overflow>0?scroll*s:0;
            l.textX=overflow>0?clipLeft-scroll*s:clipLeft+(fullClip-textWidth*s)/2;
            l.fadeLeft=overflow>0&&scroll>.5;l.fadeRight=overflow>0&&scroll<overflow-.5;
        }
    }

    inline StatusIndicatorLayout ResolveStatusIndicatorLayout(const StatusIndicatorModel& model,const StatusIndicatorLayoutInput& input)
    {
        using namespace StatusIndicatorStyle;
        using namespace StatusIndicatorCurve;
        using namespace StatusIndicatorLayoutDetail;
        StatusIndicatorLayout out;
        const auto& base=input.baseBounds;
        const bool baseValid=!base.Empty();
        const double cx=input.mainCenterX,a=input.mainWidth/2,b=input.mainHeight/2,n=input.mainN>0?input.mainN:3;
        const double dir=StatusIndicatorDirection(input),edgeY=StatusIndicatorEdgeY(input);
        const double now=model.Now();

        // 停靠位：竖直中心落在超椭圆边框上、靠右端；旧的向左错开，夹入原窗口。
        const double dockX0=cx+a-DockInset;
        const double dockY0=input.mainCenterY+Lerp(-1.0,1.0,Clamp01(input.below))*StatusIndicatorSuperHalfHeight(dockX0-cx,a,b,n);
        auto dockPoint=[&](double slot)
        {
            Point p{dockX0-(std::max)(0.0,slot)*DockStep,dockY0};
            if(baseValid)
            {
                p.x=std::clamp(p.x,base.left+DockSize/2,(std::max)(base.left+DockSize/2,base.right-DockSize/2));
                p.y=std::clamp(p.y,base.top+DockSize/2,(std::max)(base.top+DockSize/2,base.bottom-DockSize/2));
            }
            return p;
        };
        std::size_t dockCount=0;
        for(const auto& e:model.Entries())if(e.InDock())++dockCount;
        if(model.FrozenFanCount())dockCount=model.FrozenFanCount();
        const double fanP=model.FanProgress(),fanBlend=InOutSine(fanP);
        auto fanPoint=[&](double slot)
        {
            slot=std::clamp(std::isfinite(slot)?slot:0.0,0.0,static_cast<double>((std::max)(dockCount,std::size_t{1})-1));
            const auto lo=static_cast<std::size_t>(std::floor(slot)),hi=static_cast<std::size_t>(std::ceil(slot));
            const auto pa=ResolveStatusIndicatorFanPlacement(lo,dockCount,input),pb=ResolveStatusIndicatorFanPlacement(hi,dockCount,input);
            out.fanMirrored=pa.mirrored;
            return LerpPoint({pa.x,pa.y},{pb.x,pb.y},slot-static_cast<double>(lo));
        };
        auto scrollFor=[&](const StatusIndicatorEntry& e,double elapsed)
        { return StatusIndicatorScrollOffset(StatusIndicatorTextOverflow(e.textWidth),elapsed); };

        std::vector<StatusIndicatorEntryLayout> items;items.reserve(model.Entries().size());
        for(const auto& e:model.Entries())
        {
            const double presence=InOutSine(e.presence);
            if(presence<=0)continue;
            StatusIndicatorEntryLayout l;
            l.key=e.spec.key;l.kind=e.spec.kind;l.icon=e.spec.icon;l.accentArgb=e.spec.accentArgb;
            const double fullWidth=StatusIndicatorCapsuleWidth(e.textWidth);
            double opacity=1;int z=0;bool overflow=false;
            Point pivot{cx,edgeY};double scale=1;

            if(e.phase==StatusIndicatorPhase::Expanded||(e.phase==StatusIndicatorPhase::Dismissing)
                ||(e.phase==StatusIndicatorPhase::Exiting&&!e.dockSerial))
            {
                // 展开堆叠：队首中心压在边框上，后排向外错开到 StackLayers 层为止，更多完全重叠。
                const double layer=(std::min)((std::max)(0.0,e.stack.position),StackLayers);
                const double cy=edgeY+dir*layer*StackStep;
                const double width=e.phase==StatusIndicatorPhase::Expanded?(std::max)(CapsuleHeight,e.width.position):fullWidth;
                Capsule(l,Box({cx,cy},width,CapsuleHeight),e.textWidth,fullWidth,scrollFor(e,e.scrollElapsed));
                l.textOpacity=Clamp01(1-e.stack.position);
                scale=Lerp(EnterScale,1,OutBack(e.appear));opacity=OutSine(e.appear);
                l.role=e.phase==StatusIndicatorPhase::Expanded?StatusIndicatorRole::Capsule:StatusIndicatorRole::None;
                z=3000-static_cast<int>(std::lround(e.stack.position*10));overflow=true;
                if(e.phase==StatusIndicatorPhase::Dismissing){scale*=(std::max)(0.0,1-InBack(e.vanish));opacity*=1-InOutSine(e.vanish);}
                if(e.phase==StatusIndicatorPhase::Exiting){scale*=1-.5*InOutSine(e.vanish);opacity*=1-InOutSine(e.vanish);}
            }
            else if(e.Morphing()&&!(e.phase==StatusIndicatorPhase::Exiting)&&!(fanP>0&&e.phase==StatusIndicatorPhase::Docked))
            {
                // 收起/重新展开：形变（宽度收进图标，左端不动）+ 沿二次贝塞尔飞行（控制点向外抬起）。
                const double m=e.morph.position,f=e.flight.position;
                const double left0=cx-fullWidth/2;
                const double morphWidth=(std::max)(CapsuleHeight*.9,Lerp(fullWidth,CapsuleHeight,m));
                const Point start{left0+CapsuleHeight/2,edgeY};
                const Point end=dockPoint(e.landed?e.dock.position:0);
                const double outer=dir<0?(std::min)(start.y,end.y):(std::max)(start.y,end.y);
                const Point control{(start.x+end.x)/2,outer+dir*FlightLift};
                const double q=std::clamp(f,0.0,1.15);
                const Point at=Bezier(start,control,end,q);
                const double size=Lerp(CapsuleHeight,DockSize,Clamp01(f))*(std::max)(0.0,e.bump.position);
                const double width=(std::max)(size,Lerp(morphWidth,size,SmoothRange(0,.3,f)));
                StatusIndicatorRect rect{at.x-size/2,at.y-size/2,at.x-size/2+width,at.y+size/2};
                Capsule(l,rect,e.textWidth,fullWidth,scrollFor(e,e.phase==StatusIndicatorPhase::Docked?now-e.selectStart:e.scrollElapsed));
                l.radius=Lerp(CapsuleRadius,DockRadius,Clamp01(f))*(std::max)(0.0,e.bump.position);
                l.iconSize=Lerp(CapsuleIcon,DockIcon,Clamp01(f))*(std::max)(0.0,e.bump.position);
                l.iconCenterX=at.x;l.iconCenterY=at.y;
                l.textOpacity=1-SmoothRange(0,.7,m);
                if(width<=size+1e-6)l.shape=StatusIndicatorShape::Tile;
                opacity=Lerp(1,DockOpacity,SmoothRange(.55,1,f));
                // 已在锚点的重新展开胶囊可点击收回。
                l.role=e.phase==StatusIndicatorPhase::Docked&&model.Selected()==e.spec.key&&f<.05?StatusIndicatorRole::Capsule:StatusIndicatorRole::None;
                const bool settledInDock=e.landed&&f>=LandAt;
                z=settledInDock?2000+static_cast<int>(e.dockSerial%1000):3500;overflow=!settledInDock;
            }
            else
            {
                // 停靠方块 / 同心弧 / 弧上选中胶囊。
                Point p=dockPoint(e.dock.position);double size=DockSize,radius=DockRadius,icon=DockIcon;
                opacity=DockOpacity;z=1000+static_cast<int>(e.dockSerial%1000);l.role=StatusIndicatorRole::Dock;
                if(fanP>0)
                {
                    const double slot=model.FrozenFanCount()?e.frozenFanSlot:e.dock.position;
                    // 逐项轻微错峰，Back 曲线带出弹性。
                    const double k=Clamp01(fanP*1.15-slot*.012);const double g=OutBack(k);
                    p=LerpPoint(p,fanPoint(slot),g);
                    size=Lerp(DockSize,FanSize,g);radius=Lerp(DockRadius,FanRadius,Clamp01(g));icon=Lerp(DockIcon,FanIcon,Clamp01(g));
                    opacity=Lerp(DockOpacity,1,fanBlend);z=4000+static_cast<int>(slot*10);overflow=true;
                    l.role=fanP>.5?StatusIndicatorRole::Fan:StatusIndicatorRole::Dock;
                }
                l.shape=StatusIndicatorShape::Tile;l.bounds=Box(p,size,size);l.radius=radius;
                l.iconSize=icon;l.iconCenterX=p.x;l.iconCenterY=p.y;
                if(e.select>0)
                {
                    // 弧上选中：以方块为中心左右展开为胶囊，越界夹入工作区。
                    const double s=OutBack(e.select);
                    const double w=Lerp(size,fullWidth,s),h=Lerp(size,CapsuleHeight,Clamp01(s));
                    StatusIndicatorRect rect=Box(p,w,h);
                    if(!input.work.Empty())
                    {
                        const double dx=rect.left<input.work.left?input.work.left-rect.left:rect.right>input.work.right?input.work.right-rect.right:0;
                        rect.left+=dx;rect.right+=dx;
                    }
                    Capsule(l,rect,e.textWidth,fullWidth,scrollFor(e,now-e.selectStart));
                    l.textOpacity=SmoothRange(.3,1,e.select);opacity=1;z=6000;overflow=true;
                }
                if(e.phase==StatusIndicatorPhase::Exiting)
                {
                    // 退场中只绘制，不再接受点击。
                    l.role=StatusIndicatorRole::None;
                    scale=1-.5*InOutSine(e.vanish);pivot={(l.bounds.left+l.bounds.right)/2,(l.bounds.top+l.bounds.bottom)/2};
                    opacity*=1-InOutSine(e.vanish);
                }
                else if(e.bump.position!=1)
                {
                    scale=(std::max)(0.0,e.bump.position);pivot={(l.bounds.left+l.bounds.right)/2,(l.bounds.top+l.bounds.bottom)/2};
                }
            }
            if(scale!=1)ScaleLayout(l,scale,pivot);
            // 着陆轻弹/弹簧过冲可能短暂探出原窗口，计入越界包络而不是裁切。
            if(!overflow&&baseValid&&!StatusIndicatorRectContains(base,l.bounds))overflow=true;
            l.opacity=Clamp01(opacity*presence);l.z=z;l.overflow=overflow;
            if(l.opacity<=0||l.bounds.Empty())continue;
            items.push_back(std::move(l));
        }
        std::stable_sort(items.begin(),items.end(),[](const auto& x,const auto& y){ return x.z<y.z; });
        out.entries.reserve(items.size());
        for(auto& item:items)
        {
            auto& bounds=item.overflow?out.overflowBounds:out.normalBounds;
            bounds=UnionStatusIndicatorRect(bounds,item.bounds);
            item.z=static_cast<int>(out.entries.size());
            out.entries.push_back(std::move(item));
        }

        // 越界包络：出现回弹峰值的完整胶囊、飞行弧线外沿与同心弧（含选中胶囊），首帧即完整预留。
        for(const auto& e:model.Entries())
        {
            if(e.presence<=0)continue;
            const double fullWidth=StatusIndicatorCapsuleWidth(e.textWidth);
            const bool capsule=e.phase==StatusIndicatorPhase::Expanded||e.phase==StatusIndicatorPhase::Collapsing||e.Morphing();
            if(!capsule)continue;
            const double layer=(std::min)((std::max)(0.0,e.stack.position),StackLayers);
            const auto rect=Box({cx,edgeY+dir*layer*StackStep},fullWidth,CapsuleHeight);
            out.reserveBounds=UnionStatusIndicatorRect(out.reserveBounds,ScaleRect(rect,EnterPeakScale,{cx,edgeY}));
            const Point end=dockPoint(0);
            out.reserveBounds=UnionStatusIndicatorRect(out.reserveBounds,
                OutsetStatusIndicatorRect(Box({(rect.left+CapsuleHeight/2+end.x)/2,edgeY+dir*FlightLift/2},
                    std::abs(end.x-rect.left)+CapsuleHeight,CapsuleHeight+FlightLift),2));
        }
        if(model.FanOpen()||fanP>0)
        {
            for(std::size_t i=0;i<dockCount;++i)
            {
                const auto place=ResolveStatusIndicatorFanPlacement(i,dockCount,input);
                const double pad=FanSize/2*EnterPeakScale+2;
                const double half=CapsuleMaxWidth/2*EnterPeakScale+2;
                out.reserveBounds=UnionStatusIndicatorRect(out.reserveBounds,{place.x-half,place.y-pad,place.x+half,place.y+pad});
            }
        }
        if(!input.work.Empty()&&!out.reserveBounds.Empty())
        {
            auto& r=out.reserveBounds;
            r.left=(std::max)(r.left,input.work.left);r.top=(std::max)(r.top,input.work.top);
            r.right=(std::min)(r.right,input.work.right);r.bottom=(std::min)(r.bottom,input.work.bottom);
            if(r.Empty())r={};
        }

        const auto& ring=model.Ring();
        const double ringFade=InOutSine(ring.opacity);
        out.ring.owner=ring.owner;out.ring.argb=model.RingColor();
        out.ring.opacity=ringFade*(model.Animations()?model.Breath():1.0);
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
