#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <ink_stroke_modeler/terminal_tail_reconstructor.h>
#include <json/json.h>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{
using namespace ink::stroke_model;
#include "fixtures/adaptive_tail_3032.inc"
struct Fixture
{
    std::vector<TerminalTailReferencePoint> reference;
    std::vector<TerminalTailRawPoint> raw;
    Vec2 endpoint;
    TerminalTailRequest Request() const
    {
        TerminalTailRequest r;
        r.reference = reference; r.raw = raw; r.endpoint = endpoint;
        r.replacement_range = {reference.front().source.first, reference.back().source.second + 1};
        r.baseline_token = r.replacement_range.end;
        r.config.max_raw_points = 64;
        return r;
    }
};
bool Same(Vec2 a, Vec2 b) { return std::bit_cast<std::uint32_t>(a.x) == std::bit_cast<std::uint32_t>(b.x) && std::bit_cast<std::uint32_t>(a.y) == std::bit_cast<std::uint32_t>(b.y); }
Json::Value Position(Vec2 p)
{
    Json::Value a(Json::arrayValue); a.append(p.x); a.append(p.y); return a;
}
Json::Value Outcome(const TerminalTailOutcome& o)
{
    Json::Value v;
    v["status"] = static_cast<int>(o.status); v["reason"] = static_cast<int>(o.reason);
    v["points"] = Json::Value(Json::arrayValue); v["sources"] = Json::Value(Json::arrayValue);
    for (const auto& p : o.patch.points)
    {
        v["points"].append(Position(p.position));
        Json::Value s(Json::arrayValue); s.append(Json::UInt64(p.source.first));
        s.append(Json::UInt64(p.source.second)); s.append(p.source.fraction); v["sources"].append(s);
    }
    const auto& d = o.diagnostics;
    auto& j = v["diagnostics"];
    j["reference_length"] = d.reference_length; j["candidate_length"] = d.candidate_length;
    j["max_displacement"] = d.max_displacement;
    j["raw_hint_length"] = d.raw_hint_length;
    j["reference_max_physical_turn"] = d.reference_max_physical_turn;
    j["candidate_max_physical_turn"] = d.candidate_max_physical_turn;
    j["reference_terminal_turn"] = d.reference_terminal_turn;
    j["candidate_terminal_turn"] = d.candidate_terminal_turn;
    j["repair_length"] = d.repair_length; j["window_attempts"] = Json::UInt64(d.window_attempts);
    j["output_points"] = Json::UInt64(d.output_points); j["sampling_error"] = d.sampling_error;
    j["max_curve_turn"] = d.max_curve_turn; j["changed_begin"] = Json::UInt64(d.changed_begin);
    j["validation_work"] = Json::UInt64(d.validation_work);
    return v;
}
// 几何比较按点到线段距离，避免用顶点索引比较不同采样密度。
double DistanceToPolyline(Vec2 p, std::span<const TerminalTailPoint> line)
{
    double best=std::numeric_limits<double>::infinity();
    for(size_t i=1;i<line.size();++i)
    {
        const auto a=line[i-1].position,b=line[i].position;
        const double dx=double(b.x)-a.x,dy=double(b.y)-a.y;
        const double norm=dx*dx+dy*dy;
        const double t=norm>0?std::clamp(((double(p.x)-a.x)*dx+(double(p.y)-a.y)*dy)/norm,0.0,1.0):0;
        best=std::min(best,std::hypot(double(p.x)-a.x-t*dx,double(p.y)-a.y-t*dy));
    }
    return best;
}
double DirectedDistance(std::span<const TerminalTailPoint> a,std::span<const TerminalTailPoint> b)
{
    double result=0;
    for(const auto& p:a) result=std::max(result,DistanceToPolyline(p.position,b));
    return result;
}
Fixture Captured(const Json::Value& v)
{
    Fixture f;
    for (const auto& p : v["model"])
    {
        const auto index = p[0].asUInt64();
        f.reference.push_back({{p[1].asFloat(), p[2].asFloat()}, {index,index,0}, false});
    }
    f.reference.back().weak_terminal = true;
    f.endpoint = {v["raw"][v["raw"].size()-1][1].asFloat(),v["raw"][v["raw"].size()-1][2].asFloat()};
    const double up = v["raw"][v["raw"].size()-1][3].asDouble();
    for (const auto& p : v["raw"])
        if (p[3].asDouble() >= up - .080)
            f.raw.push_back({{p[1].asFloat(),p[2].asFloat()},p[0].asUInt64()});
    return f;
}
Fixture Polyline(std::initializer_list<Vec2> positions, bool raw)
{
    Fixture f; std::uint64_t i = 0;
    for (auto p : positions) { f.reference.push_back({p,{i,i,0},false}); if (raw) f.raw.push_back({p,i}); ++i; }
    f.endpoint = f.reference.back().position; f.reference.back().weak_terminal = true; return f;
}
}

int RunModelerAdaptiveTailReplay(const wchar_t* reportPath)
{
    Json::CharReaderBuilder reader; Json::Value captured; std::string errors;
    std::string capturedText;
    for (const char* chunk : kCapturedTailChunks) capturedText += chunk;
    std::istringstream input(capturedText);
    if (!Json::parseFromStream(reader,input,&captured,&errors)) return 2;
    Json::Value report; report["schema"] = "adaptive-tail-replay-v1";
    report["fixture_sha256"] = "9205377833ae5c0b9d7b202e1fcdfd3bac0bea62445cc108a95fa79ad9324ed5";
    report["scope"] = "bounded captured tails, not full stroke/controller/GPU replay";
    report["captured"] = captured;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const std::string& message) { ++checks; if (!ok) { ++failures; report["failures"].append(message); std::cerr << message << '\n'; } };
    auto run = [&](const std::string& name, const Fixture& f, int expected, int oldReason = -1,
                   AdaptiveTerminalTailConfig config = {}, int expectedReason = -1)
    {
        const auto request = f.Request(); const auto before = f.reference;
        TerminalTailScratch oldScratch, scratch;
        const auto old = ReconstructLocalTerminalTail(request, oldScratch);
        const auto result = ReconstructAdaptiveTerminalTail(request, scratch, config);
        Json::Value c; c["name"] = name; c["replacement_begin"] = Json::UInt64(request.replacement_range.begin); c["replacement_end"] = Json::UInt64(request.replacement_range.end); c["old"] = Outcome(old); c["new"] = Outcome(result);
        for (auto p : f.reference) c["reference"].append(Position(p.position));
        for (auto p : f.raw) c["raw"].append(Position(p.position));
        if (oldReason >= 0) check(static_cast<int>(old.reason) == oldReason,name+" legacy failure reproduced");
        if (expected == -2) check(result.status != TerminalTailStatus::kApply,name+" preserve intentional geometry");
        if (expected >= 0) check(static_cast<int>(result.status) == expected,name+" expected status");
        if (expectedReason >= 0) check(static_cast<int>(result.reason) == expectedReason,name+" expected reason");
        check(f.reference.size() == before.size(),name+" input count unchanged");
        for (size_t i=0;i<before.size();++i) check(Same(before[i].position,f.reference[i].position),name+" input unchanged");
        if (result.status == TerminalTailStatus::kApply)
        {
            check(!result.patch.points.empty(),name+" nonempty applied patch");
            check(Same(result.patch.points.back().position,f.endpoint),name+" exact Up");
            check(Same(result.patch.points.front().position,f.reference.front().position),name+" exact entry");
            check(result.patch.baseline_token == request.baseline_token,name+" token");
            check(result.patch.replacement_range.begin == request.replacement_range.begin && result.patch.replacement_range.end == request.replacement_range.end,name+" range");
            check(result.diagnostics.window_attempts <= config.max_window_attempts,name+" attempts bounded");
            check(result.diagnostics.validation_work <= request.config.max_validation_work,name+" validation bounded");
            check(result.patch.points.size() <= config.max_output_points,name+" output bounded");
            double previous = -1;
            for (const auto& p : result.patch.points)
            {
                check(std::isfinite(p.position.x)&&std::isfinite(p.position.y),name+" finite");
                const auto& s=p.source;
                check(s.first>=request.replacement_range.begin && s.second<request.replacement_range.end &&
                    s.first<=s.second && s.fraction>=0 && s.fraction<=1,name+" source valid");
                const double source=s.first+(s.second-s.first)*s.fraction;
                check(source>=previous,name+" source ordered"); previous=source;
            }
            // 改动入口之前必须逐点保留；不拿重采样后的同索引当属性来源。
            const auto prefix=std::min<std::size_t>(result.diagnostics.changed_begin,f.reference.size());
            for (size_t i=0;i<prefix && i<result.patch.points.size();++i)
                check(Same(result.patch.points[i].position,f.reference[i].position),name+" prefix exact");
        }
        else check(result.patch.points.empty(),name+" atomic empty fallback");
        report["cases"].append(c);
    };
    std::vector<Fixture> fixtures;
    for (Json::ArrayIndex i=0;i<captured.size();++i)
    {
        fixtures.push_back(Captured(captured[i]));
        run("captured_"+std::to_string(i+1),fixtures.back(),0,i<3?2:8);
    }
    auto noRaw=fixtures.back(); noRaw.raw.clear(); run("captured_8_no_raw",noRaw,0);
    auto single=fixtures.back(); single.raw={single.raw.back()}; run("captured_8_up_only",single,0);
    auto sparse=fixtures.back(); sparse.raw={sparse.raw.front(),sparse.raw.back()}; run("captured_8_sparse",sparse,0);
    auto dense=noRaw; dense.reference.clear();
    for (size_t i=0;i+1<noRaw.reference.size();++i)
    {
        const auto a=noRaw.reference[i].position,b=noRaw.reference[i+1].position;
        dense.reference.push_back({a,{2*i,2*i,0},false});
        dense.reference.push_back({{(a.x+b.x)*.5f,(a.y+b.y)*.5f},{2*i+1,2*i+1,0},false});
    }
    const auto end=dense.reference.size(); dense.reference.push_back({dense.endpoint,{end,end,0},true});
    run("captured_8_densified",dense,0);
    {
        TerminalTailScratch a,b;
        const auto original=ReconstructAdaptiveTerminalTail(noRaw.Request(),a);
        const auto densified=ReconstructAdaptiveTerminalTail(dense.Request(),b);
        check(original.status==TerminalTailStatus::kApply && densified.status==TerminalTailStatus::kApply,"sampling density preserves repair decision");
        if(original.status==TerminalTailStatus::kApply && densified.status==TerminalTailStatus::kApply)
        {
            const double distance=std::max(DirectedDistance(original.patch.points,densified.patch.points),DirectedDistance(densified.patch.points,original.patch.points));
            report["density_geometry_distance"]=distance;
            const double tolerance=AdaptiveTerminalTailConfig{}.sampling_tolerance*4 + original.diagnostics.repair_length*.02;
            check(distance<=tolerance,"sampling density does not substantially change fitted geometry");
        }
    }
    // 现场向内偏离应让过渡前移；缺少辅助点不能凭空给出扩大窗口的证据。
    for(size_t index:{size_t(5),size_t(7)})
    {
        auto plain=fixtures[index]; plain.raw.clear();
        auto upOnly=plain; upOnly.raw={{plain.endpoint,0}};
        TerminalTailScratch a,b,c;
        const auto assisted=ReconstructAdaptiveTerminalTail(fixtures[index].Request(),a);
        const auto unaided=ReconstructAdaptiveTerminalTail(plain.Request(),b);
        const auto up=ReconstructAdaptiveTerminalTail(upOnly.Request(),c);
        const auto label="captured_"+std::to_string(index+1);
        check(assisted.status==TerminalTailStatus::kApply && unaided.status==TerminalTailStatus::kApply && up.status==TerminalTailStatus::kApply,label+" raw optional");
        check(assisted.diagnostics.raw_hint_length>0,label+" inward raw has window evidence");
        check(assisted.diagnostics.repair_length>=assisted.diagnostics.raw_hint_length*1.25,label+" transition includes entry margin before inward raw hint");
        if(assisted.status==TerminalTailStatus::kApply && unaided.status==TerminalTailStatus::kApply)
        {
            double guidedError=0,modelError=0; size_t count=0;
            const auto& raw=fixtures[index].raw;
            for(size_t j=raw.size()>5?raw.size()-5:0;j<raw.size();++j)
            {
                if(Same(raw[j].position,fixtures[index].endpoint)) continue;
                guidedError+=DistanceToPolyline(raw[j].position,assisted.patch.points);
                modelError+=DistanceToPolyline(raw[j].position,unaided.patch.points); ++count;
            }
            check(count>0 && guidedError<=modelError,label+" tail raw guidance improves positional agreement");
            report["raw_guidance_error"][label]["guided_sum"]=guidedError;
            report["raw_guidance_error"][label]["model_only_sum"]=modelError;
        }
        check(unaided.diagnostics.raw_hint_length==0 && up.diagnostics.raw_hint_length==0,label+" no raw/Up alone cannot imply earlier deviation");
        Json::Value comparison; comparison["case"]=Json::UInt64(index+1);
        comparison["with_raw"]=Outcome(assisted); comparison["without_raw"]=Outcome(unaided); comparison["up_only"]=Outcome(up);
        report["raw_window_comparisons"].append(comparison);
    }
    Fixture freeDirection;
    for (size_t i=0;i<80;++i) freeDirection.reference.push_back({{float(i),0},{i,i,0},false});
    freeDirection.endpoint={79,3}; freeDirection.reference.push_back({freeDirection.endpoint,{80,80,0},true});
    run("free_terminal_direction",freeDirection,0);
    {
        TerminalTailScratch scratch; const auto result=ReconstructAdaptiveTerminalTail(freeDirection.Request(),scratch);
        if(result.status==TerminalTailStatus::kApply && result.patch.points.size()>1) {
            const auto a=result.patch.points[result.patch.points.size()-2].position,b=result.patch.points.back().position;
            check(b.y-a.y > .05*std::hypot(double(b.x-a.x),double(b.y-a.y)),"terminal direction can rotate away from old horizontal tangent");
        } else check(false,"free terminal direction publishes");
    }
    {
        auto outlier=freeDirection;
        outlier.raw={{{20,1000},0},{outlier.endpoint,1}};
        run("solitary_raw_outlier",outlier,-1);
        TerminalTailScratch a,b;
        const auto badRaw=ReconstructAdaptiveTerminalTail(outlier.Request(),a);
        const auto modelOnly=ReconstructAdaptiveTerminalTail(freeDirection.Request(),b);
        check(badRaw.diagnostics.raw_hint_length==0,"isolated remote raw point does not expand window");
        if(badRaw.status==TerminalTailStatus::kApply)
            check(badRaw.diagnostics.repair_length<=modelOnly.diagnostics.repair_length,"outlier cannot require a longer transition");
    }
    {
        // 离轨只有1px但沿轨很远的单点也不是连续偏离证据，不能把整笔拉入尾窗。
        Fixture nearTrack;
        for(size_t i=0;i<=200;++i)
            nearTrack.reference.push_back({{float(i*5),0},{i,i,0},false});
        nearTrack.endpoint={1000,10};
        nearTrack.reference.push_back({nearTrack.endpoint,{201,201,0},true});
        auto modelOnly=nearTrack;
        nearTrack.raw={{{100,1},0},{nearTrack.endpoint,1}};
        run("solitary_near_track_far_raw",nearTrack,0);
        TerminalTailScratch a,b;
        const auto assisted=ReconstructAdaptiveTerminalTail(nearTrack.Request(),a);
        const auto plain=ReconstructAdaptiveTerminalTail(modelOnly.Request(),b);
        check(assisted.diagnostics.raw_hint_length==0,"single near-track remote raw point is not continuous deviation evidence");
        check(plain.status==TerminalTailStatus::kApply,"near-track control publishes");
        if(assisted.status==TerminalTailStatus::kApply && plain.status==TerminalTailStatus::kApply)
        {
            check(assisted.diagnostics.repair_length<=plain.diagnostics.repair_length,"single near-track remote raw cannot widen repair to whole stroke");
            check(assisted.diagnostics.changed_begin>1,"single remote point keeps stable prefix");
        }
    }
    // 现场33568日志第26笔：短直线回退含倒数第二点侧抖，固定原坐标和来源。
    {
        static constexpr char kRemainingStraight26[] = R"tail({"record":26,"header":"decision=applied status=0 reason=0(applied) points=47 raw=14 window=0 changed=14 candidate_metrics=computed displacement=5.067170 turn=2.925->0.035 attempts=5 repair=97.418309 raw_hint=0.000000 output=47 sampling_error=0.007099 curve_turn=0.009302","model":[[0.0,104.995277,1645.98425,3.0,0.0],[1.0,104.995277,1645.98438,2.99995732,0.0002246999938506633],[2.0,104.995277,1646.03699,2.98154306,0.0023177999537438154],[3.0,104.995277,1646.17126,2.96270514,0.004410900175571442],[4.0,104.995277,1646.41187,2.94386721,0.006504000164568424],[5.0,104.995277,1646.77844,2.92522144,0.008597100153565407],[6.0,104.992035,1647.30371,2.90700841,0.01071465015411377],[7.0,104.982841,1648.00244,2.88942695,0.012832200154662132],[8.0,104.965477,1648.88879,2.87245512,0.014949750155210495],[9.0,104.938171,1649.97205,2.85607171,0.017067300155758858],[10.0,104.859444,1651.74597,2.83565211,0.01981566660106182],[11.0,104.718033,1653.8866,2.81614685,0.022564033046364784],[12.0,104.499771,1656.38477,2.79751468,0.025312399491667747],[13.0,104.237755,1659.03809,2.78074932,0.027897832915186882],[14.0,103.928299,1661.98474,2.76469111,0.030483266338706017],[15.0,103.57988,1665.21094,2.74931026,0.0330687016248703],[16.0,103.160812,1668.85425,2.73395753,0.035765498876571655],[17.0,102.677277,1672.74353,2.71927953,0.03846229985356331],[18.0,102.123543,1676.84753,2.70524669,0.04115910083055496],[19.0,101.631325,1680.34814,2.69410658,0.04338980093598366],[20.0,101.114021,1683.93396,2.68337297,0.045620501041412354],[21.0,100.579643,1687.58643,2.67303109,0.04785120114684105],[22.0,100.035164,1691.28894,2.66306663,0.050081901252269745],[23.0,99.3710251,1695.79517,2.65154457,0.05276913195848465],[24.0,98.706871,1700.30627,2.64052725,0.05545636638998985],[25.0,98.0493317,1704.78931,2.62999249,0.058143600821495056],[26.0,97.5407257,1708.2887,2.62197804,0.060271698981523514],[27.0,97.0491638,1711.72156,2.61424279,0.06239980086684227],[28.0,96.5785904,1715.07544,2.60677719,0.06452789902687073],[29.0,96.1320724,1718.34094,2.5995717,0.06665600091218948],[30.0,95.7040634,1721.56213,2.59248161,0.06882636994123459],[31.0,95.3050919,1724.66052,2.58564353,0.0709967315196991],[32.0,94.9361572,1727.62622,2.5790484,0.0731671005487442],[33.0,94.6200638,1730.25427,2.57313871,0.07518086582422256],[34.0,94.3301697,1732.75342,2.56742406,0.07719463109970093],[35.0,94.0661392,1735.12048,2.56189799,0.07920839637517929],[36.0,93.7679901,1737.93506,2.56189799,0.07920839637517929],[37.0,93.5098343,1740.50354,2.56189799,0.07920839637517929],[38.0,93.2893143,1742.82092,2.56189799,0.07920839637517929],[39.0,93.103775,1744.88818,2.56189799,0.07920839637517929],[40.0,92.9503555,1746.71106,2.56189799,0.07920839637517929],[41.0,92.8458786,1748.02466,2.56189799,0.07920839637517929],[42.0,92.7587509,1749.19238,2.56189799,0.07920839637517929],[43.0,92.6874924,1750.22131,2.56189799,0.07920839637517929],[44.0,92.6306305,1751.11902,2.56189799,0.07920839637517929],[45.0,92.9763794,1752.03784,2.56189799,0.07920839637517929],[46.0,91.0110245,1749.01416,2.56189799,0.1369079053401947]],"raw":[[0.0,104.995277,1645.98425,0.0,0.0],[1.0,104.995277,1646.96692,0.0002247,0.0],[2.0,104.995277,1658.98584,0.0085971,1.0],[3.0,104.012596,1674.02832,0.0170673,5.0],[4.0,100.006302,1688.99524,0.0253124,9.0],[5.0,99.0236206,1704.03784,0.0330687,12.0],[6.0,95.0173187,1718.02209,0.0411591,15.0],[7.0,94.0346451,1731.02356,0.0500819,18.0],[8.0,92.9763794,1740.01892,0.0581436,22.0],[9.0,92.9763794,1746.97327,0.066656,25.0],[10.0,92.9763794,1749.99683,0.0731671,29.0],[11.0,92.9763794,1752.03784,0.0792084,32.0],[12.0,91.0110245,1749.99683,0.1332302,45.0],[13.0,91.0110245,1749.01416,0.1369079,45.0]]})tail";
        Json::Value v; std::istringstream capture(kRemainingStraight26);
        if (!Json::parseFromStream(reader,capture,&v,&errors)) return 2;
        report["remaining_straight_capture"]=v;
        AdaptiveTerminalTailConfig physical;
        double diameter=0;
        for (const auto& p:v["model"]) diameter=std::max(diameter,2*p[3].asDouble());
        physical.visibility_distance=std::clamp(diameter*.1,.5,1.0);
        physical.sampling_tolerance=std::clamp(diameter*.03,.1,.2);
        const auto actual=Captured(v);
        run("captured_33568_26",actual,1,-1,physical);
        auto noRaw=actual; noRaw.raw.clear();
        run("captured_33568_26_no_raw",noRaw,0,-1,physical);
        auto upOnly=actual; upOnly.raw={upOnly.raw.back()};
        run("captured_33568_26_up_only",upOnly,0,-1,physical);
        auto twoRaw=actual; twoRaw.raw={twoRaw.raw[twoRaw.raw.size()-3],twoRaw.raw.back()};
        run("captured_33568_26_two_raw",twoRaw,0,-1,physical);
        auto rotate=[](Vec2 p){return Vec2{p.y,-p.x};};
        auto rotated=actual;
        for(auto& p:rotated.reference) p.position=rotate(p.position);
        for(auto& p:rotated.raw) p.position=rotate(p.position);
        rotated.endpoint=rotate(rotated.endpoint);
        run("captured_33568_26_rotated",rotated,1,-1,physical);
        auto scaled=actual;
        for(auto& p:scaled.reference) {p.position.x*=3;p.position.y*=3;}
        for(auto& p:scaled.raw) {p.position.x*=3;p.position.y*=3;}
        scaled.endpoint.x*=3;scaled.endpoint.y*=3;
        auto scaledConfig=physical;
        scaledConfig.visibility_distance*=3;scaledConfig.sampling_tolerance*=3;scaledConfig.min_repair_length*=3;
        run("captured_33568_26_scaled",scaled,1,-1,scaledConfig);
        // 证据检查也消耗匹配预算；超限不能把没有验证完的路径当成吻合。
        auto limited=actual.Request(); limited.config.max_match_work=35;
        TerminalTailScratch scratch;
        const auto exhausted=ReconstructAdaptiveTerminalTail(limited,scratch,physical);
        check(exhausted.status==TerminalTailStatus::kRejected && exhausted.reason==TerminalTailReason::kBudgetExceeded
            && exhausted.patch.points.empty() && exhausted.diagnostics.match_work<=limited.config.max_match_work,
            "raw support verification budget exhausted atomically");
    }
    // 现场2484日志第55笔：173度近共线倒退，横向仅0.47px，应保持原轨迹。
    {
        Fixture captured55;
        captured55.reference.push_back({{1502.96692f,1177.02051f},{0,0,0},false});
        captured55.reference.push_back({{1502.96692f,1177.02673f},{1,1,0},false});
        captured55.reference.push_back({{1502.96692f,1177.04456f},{2,2,0},false});
        captured55.reference.push_back({{1502.96692f,1177.07825f},{3,3,0},false});
        captured55.reference.push_back({{1502.96692f,1177.13123f},{4,4,0},false});
        captured55.reference.push_back({{1502.96692f,1177.24841f},{5,5,0},false});
        captured55.reference.push_back({{1502.96692f,1177.44397f},{6,6,0},false});
        captured55.reference.push_back({{1502.96692f,1177.73547f},{7,7,0},false});
        captured55.reference.push_back({{1502.96692f,1178.13562f},{8,8,0},false});
        captured55.reference.push_back({{1502.97424f,1178.76538f},{9,9,0},false});
        captured55.reference.push_back({{1502.99463f,1179.62585f},{10,10,0},false});
        captured55.reference.push_back({{1503.03259f,1180.745f},{11,11,0},false});
        captured55.reference.push_back({{1503.09058f,1182.17212f},{12,12,0},false});
        captured55.reference.push_back({{1503.17041f,1183.94446f},{13,13,0},false});
        captured55.reference.push_back({{1503.27307f,1186.08362f},{14,14,0},false});
        captured55.reference.push_back({{1503.37292f,1188.19885f},{15,15,0},false});
        captured55.reference.push_back({{1503.48132f,1190.61804f},{16,16,0},false});
        captured55.reference.push_back({{1503.59509f,1193.35339f},{17,17,0},false});
        captured55.reference.push_back({{1503.71179f,1196.41052f},{18,18,0},false});
        captured55.reference.push_back({{1503.85571f,1200.42432f},{19,19,0},false});
        captured55.reference.push_back({{1504.00964f,1204.85107f},{20,20,0},false});
        captured55.reference.push_back({{1504.17517f,1209.65869f},{21,21,0},false});
        captured55.reference.push_back({{1504.3623f,1214.84436f},{22,22,0},false});
        captured55.reference.push_back({{1504.57617f,1220.33203f},{23,23,0},false});
        captured55.reference.push_back({{1504.82153f,1226.0813f},{24,24,0},false});
        captured55.reference.push_back({{1505.0426f,1231.04382f},{25,25,0},false});
        captured55.reference.push_back({{1505.27246f,1236.08875f},{26,26,0},false});
        captured55.reference.push_back({{1505.50623f,1241.18311f},{27,27,0},false});
        captured55.reference.push_back({{1505.73975f,1246.29883f},{28,28,0},false});
        captured55.reference.push_back({{1505.98083f,1251.65344f},{29,29,0},false});
        captured55.reference.push_back({{1506.2146f,1256.95764f},{30,30,0},false});
        captured55.reference.push_back({{1506.43848f,1262.1825f},{31,31,0},false});
        captured55.reference.push_back({{1506.64783f,1267.2196f},{32,32,0},false});
        captured55.reference.push_back({{1506.84424f,1272.10181f},{33,33,0},false});
        captured55.reference.push_back({{1507.02673f,1276.80371f},{34,34,0},false});
        captured55.reference.push_back({{1507.20947f,1281.70337f},{35,35,0},false});
        captured55.reference.push_back({{1507.37439f,1286.3042f},{36,36,0},false});
        captured55.reference.push_back({{1507.51245f,1290.29333f},{37,37,0},false});
        captured55.reference.push_back({{1507.6355f,1293.97778f},{38,38,0},false});
        captured55.reference.push_back({{1507.74402f,1297.34839f},{39,39,0},false});
        captured55.reference.push_back({{1507.83875f,1300.40271f},{40,40,0},false});
        captured55.reference.push_back({{1507.92053f,1303.14392f},{41,41,0},false});
        captured55.reference.push_back({{1507.99011f,1305.58008f},{42,42,0},false});
        captured55.reference.push_back({{1508.04248f,1307.49023f},{43,43,0},false});
        captured55.reference.push_back({{1508.08679f,1309.1842f},{44,44,0},false});
        captured55.reference.push_back({{1508.12378f,1310.67285f},{45,45,0},false});
        captured55.reference.push_back({{1508.15405f,1311.9679f},{46,46,0},false});
        captured55.reference.push_back({{1508.03149f,1313.00781f},{47,47,0},false});
        captured55.reference.push_back({{1508.03149f,1309.00159f},{48,48,0},false});
        captured55.raw.push_back({{1508.03149f,1309.00159f},11});
        captured55.endpoint=captured55.raw.back().position;
        captured55.reference.back().weak_terminal=true;
        run("captured_2484_55",captured55,1);
    }
    run("horizontal_reverse",Polyline({{0,0},{10,0},{20,0},{30,0},{26,0}},false),1);
    run("vertical_reverse",Polyline({{0,0},{0,10},{0,20},{0,30},{0,26}},false),1);
    run("slanted_reverse",Polyline({{0,0},{10,10},{20,20},{30,30},{27,27}},false),1);
    run("reverse_multi_edge",Polyline({{0,0},{10,0},{20,0},{30,0},{28,0},{26,0}},false),1);
    run("reverse_subpixel_lateral",Polyline({{0,0},{10,0},{20,0},{30,0},{26,.47f}},false),1);
    run("visible_oblique_reverse",Polyline({{0,0},{10,0},{20,0},{30,0},{26,3}},false),0);
    // 两端重合不代表中间几何相同，原始输入的可见绕行不能被顶点检查掩盖。
    {
        auto detour=Polyline({{0,0},{10,0},{20,0},{30,0},{26,3}},false);
        detour.raw={{{30,0},0},{{30,10},1},{{26,3},2}};
        run("raw_terminal_visible_detour",detour,-1);
        TerminalTailScratch scratch;
        const auto result=ReconstructAdaptiveTerminalTail(detour.Request(),scratch);
        check(result.status!=TerminalTailStatus::kNoChange,"visible raw detour is not supported by endpoint coincidence");
        auto displaced=detour; displaced.raw={{{30,0},0},{{28,1.6f},1},{{26,3},2}};
        auto request=displaced.Request(); request.endpoint={26,4};
        const auto needsEndpoint=ReconstructAdaptiveTerminalTail(request,scratch);
        check(needsEndpoint.status!=TerminalTailStatus::kNoChange,"raw support cannot leave a mismatched Up unrepaired");
    }
    run("normal_curve_no_raw",Polyline({{0,0},{10,0},{20,1},{30,3},{40,6},{50,10}},false),1);
    run("tiny_invisible",Polyline({{0,0},{2,0},{4,0},{6,0},{6,.1f}},false),1);
    run("straight",Polyline({{0,0},{2,0},{4,0},{6,0},{8,0}},false),1);
    run("true_terminal_corner",Polyline({{0,0},{10,0},{20,0},{30,0},{30,10}},true),1);
    run("true_corner",Polyline({{0,0},{10,0},{20,0},{30,0},{30,10},{30,20}},true),-2);
    run("normal_u",Polyline({{0,0},{0,10},{3,17},{10,20},{17,17},{20,10},{20,0}},true),-2);
    run("normal_s",Polyline({{0,0},{10,3},{20,10},{30,10},{40,3},{50,0},{60,3}},true),-2);
    AdaptiveTerminalTailConfig budget; budget.max_output_points=4;
    run("output_budget",fixtures.back(),2,-1,budget);
    AdaptiveTerminalTailConfig validBudget; validBudget.max_output_points=fixtures.back().reference.size();
    validBudget.sampling_tolerance=0.00001;
    run("valid_output_budget",fixtures.back(),2,-1,validBudget,static_cast<int>(TerminalTailReason::kBudgetExceeded));
    {
        auto request=fixtures.back().Request(); request.config.max_validation_work=1;
        TerminalTailScratch scratch; const auto result=ReconstructAdaptiveTerminalTail(request,scratch);
        check(result.status==TerminalTailStatus::kRejected && result.reason==TerminalTailReason::kBudgetExceeded && result.patch.points.empty(),"valid validation work budget exhausted atomically");
    }
    // 正常弯曲后带人工末钩，不能只靠raw完全重合的NoChange覆盖形状保护。
    Fixture curved;
    for (size_t i=0;i<=40;++i) {
        const double angle=3.141592653589793*(1-double(i)/40);
        curved.reference.push_back({{float(40+40*std::cos(angle)),float(40*std::sin(angle))},{i,i,0},false});
    }
    curved.endpoint={76,-2}; curved.reference.back().position=curved.endpoint; curved.reference.back().weak_terminal=true;
    run("u_with_pin_no_raw",curved,0);
    {
        TerminalTailScratch scratch; const auto result=ReconstructAdaptiveTerminalTail(curved.Request(),scratch);
        if(result.status==TerminalTailStatus::kApply)
        {
            check(DistanceToPolyline({40,40},result.patch.points)<2,"U arch apex preserved instead of shortcut");
            double length=0;
            for(size_t i=1;i<result.patch.points.size();++i)
            {
                const auto a=result.patch.points[i-1].position,b=result.patch.points[i].position;
                length+=std::hypot(double(b.x)-a.x,double(b.y)-a.y);
                check(b.x>=-1 && b.x<=84 && b.y>=-5 && b.y<=42,"U correction stays on original branch");
            }
            check(length>110,"U arc keeps its bend instead of endpoint chord");
        }
    }
    auto bad=fixtures.back(); bad.reference[4].position.x=std::numeric_limits<float>::quiet_NaN(); run("nonfinite",bad,2);
    // 仅测求解与其验证，不包含完成态属性转移、磁盘诊断或 GPU。
    using Clock=std::chrono::steady_clock;
    for (size_t i=0;i<fixtures.size();++i)
    {
        TerminalTailScratch scratch; auto r=fixtures[i].Request();
        for(int warm=0;warm<5;++warm) (void)ReconstructAdaptiveTerminalTail(r,scratch);
        Json::Value timing; timing["case"]=Json::UInt64(i+1); timing["rounds"]=30;
        for(int round=0;round<30;++round)
        {
            auto start=Clock::now(); auto result=ReconstructAdaptiveTerminalTail(r,scratch);
            double us=std::chrono::duration<double,std::micro>(Clock::now()-start).count();
            timing["samples"].append(us); check(result.status==TerminalTailStatus::kApply,"benchmark Apply");
        }
        report["benchmark_us"].append(timing);
    }
    report["checks"]=checks; report["failure_count"]=failures;
    std::ofstream stream(std::filesystem::path(reportPath),std::ios::binary);
    Json::StreamWriterBuilder writer; writer["indentation"]="  "; stream<<Json::writeString(writer,report); stream.flush();
    if(!stream) return 2;
    std::cout<<"Adaptive tail replay: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
