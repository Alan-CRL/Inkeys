#define NOMINMAX
#include <windows.h>
#include "../Inkeys/Inkeys/CV/VisualMotionTracker.h"
#include <chrono>
#include <json/json.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Inkeys::CV;
namespace {
constexpr std::uint64_t PixelLimit = 384ull * 1024 * 1024;
void Require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
std::uint64_t Decimal(const Json::Value& value) {
    Require(value.isString(), "decimal string missing");
    const auto s = value.asString(); Require(!s.empty() && s.size() <= 20, "decimal size");
    std::uint64_t n = 0;
    for (const auto c : s) {
        Require(c >= '0' && c <= '9', "decimal character");
        const auto digit = static_cast<unsigned>(c - '0');
        Require(n <= (std::numeric_limits<std::uint64_t>::max() - digit) / 10, "decimal overflow");
        n = n * 10 + digit;
    }
    return n;
}
double Number(const Json::Value& value) {
    Require(value.isNumeric(), "numeric field missing");
    const auto n = value.asDouble(); Require(std::isfinite(n), "nonfinite field"); return n;
}
bool Near(double a, double b) { return std::abs(a - b) <= 1e-6 + 1e-8 * std::max(std::abs(a), std::abs(b)); }
struct Rect { int left, top, right, bottom; bool operator==(const Rect&) const = default; };
Rect ReadRect(const Json::Value& value) {
    Require(value.isObject() && value.size() == 4, "rectangle fields");
    for (const auto* key : {"left", "top", "right", "bottom"}) Require(value[key].isInt(), "rectangle integer");
    Rect r{value["left"].asInt(), value["top"].asInt(), value["right"].asInt(), value["bottom"].asInt()};
    Require(r.right > r.left && r.bottom > r.top, "rectangle extent"); return r;
}
std::vector<std::uint8_t> ReadPgm(const std::filesystem::path& file, int width, int height) {
    Require(!std::filesystem::is_symlink(file) && std::filesystem::is_regular_file(file), "PGM file type");
    std::ifstream stream(file, std::ios::binary); Require(stream.good(), "PGM open");
    std::string magic; int w = 0, h = 0, maximum = 0;
    stream >> magic >> w >> h >> maximum;
    Require(magic == "P5" && w == width && h == height && maximum == 255, "PGM header");
    const auto separator = stream.get();
    if (separator == '\r') Require(stream.get() == '\n', "PGM CRLF");
    else Require(separator == '\n', "PGM data separator");
    const auto bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> pixels(bytes);
    stream.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(bytes));
    Require(static_cast<std::size_t>(stream.gcount()) == bytes && stream.peek() == EOF, "PGM payload size");
    return pixels;
}
int Compare(const MotionResult& result, const Json::Value& frame, std::string& first, double& maxTotalError) {
    int bad = 0;
    const auto check = [&](bool okay, const char* name) { if (!okay) { ++bad; if (first.empty()) first = name; } };
    check(frame["accepted"].isBool() && result.accepted == frame["accepted"].asBool(), "accepted");
    check(frame["state"].isUInt() && static_cast<unsigned>(result.state) == frame["state"].asUInt(), "state");
    check(frame["reason"].isString() && frame["reason"].asString() == MotionReasonName(result.reason), "reason");
    check(Near(result.delta.x, Number(frame["deltaX"])), "deltaX");
    check(Near(result.delta.y, Number(frame["deltaY"])), "deltaY");
    const auto x = Number(frame["totalX"]), y = Number(frame["totalY"]);
    maxTotalError = std::max(maxTotalError, std::hypot(result.total.x - x, result.total.y - y));
    check(Near(result.total.x, x), "totalX"); check(Near(result.total.y, y), "totalY");
    check(Near(result.confidence, Number(frame["confidence"])), "confidence");
    const auto& stats = frame["statistics"]; Require(stats.isObject(), "statistics object");
#define CHECK_U64(name) check(result.statistics.name == Decimal(stats[#name]), #name)
    CHECK_U64(inputSequence); CHECK_U64(referenceSequence); CHECK_U64(referenceGeneration); CHECK_U64(relayGeneration);
#undef CHECK_U64
#define CHECK_INT(name) check(stats[#name].isInt() && result.statistics.name == stats[#name].asInt(), #name)
    CHECK_INT(attempted); CHECK_INT(forward); CHECK_INT(backward); CHECK_INT(fbValid); CHECK_INT(inliers);
    CHECK_INT(referenceCells); CHECK_INT(occupiedCells); CHECK_INT(inlierCells); CHECK_INT(interiorCells);
    CHECK_INT(added); CHECK_INT(retained); CHECK_INT(retired); CHECK_INT(initialSurvivors); CHECK_INT(consecutiveRejects);
#undef CHECK_INT
#define CHECK_DOUBLE(name) check(Near(result.statistics.name, Number(stats[#name])), #name)
    CHECK_DOUBLE(pointInlierRatio); CHECK_DOUBLE(spatialInlierRatio); CHECK_DOUBLE(survivalRatio);
    CHECK_DOUBLE(coverageX); CHECK_DOUBLE(coverageY); CHECK_DOUBLE(runnerUpRatio);
    CHECK_DOUBLE(fbMedian); CHECK_DOUBLE(fbP95); CHECK_DOUBLE(residualMedian); CHECK_DOUBLE(residualP95);
    CHECK_DOUBLE(affineVariation); CHECK_DOUBLE(affineResidual);
#undef CHECK_DOUBLE
    return bad;
}
}
int LegacyMotionAudit(const std::filesystem::path& manifest) {
    try {
        Require(!std::filesystem::is_symlink(manifest) && std::filesystem::is_regular_file(manifest), "manifest file type");
        Require(std::filesystem::file_size(manifest) > 0 && std::filesystem::file_size(manifest) <= 1024 * 1024, "manifest bound");
        std::ifstream stream(manifest, std::ios::binary);
        Json::CharReaderBuilder builder; builder["rejectDupKeys"] = true; builder["failIfExtra"] = true; builder["stackLimit"] = 32;
        Json::Value trace; std::string errors;
        Require(Json::parseFromStream(builder, stream, &trace, &errors), "manifest JSON");
        Require(trace["schema"].isUInt() && trace["schema"].asUInt() == 1 && trace["kind"].isString() && trace["kind"].asString() == "actual-tracker-input-fullres-gray8", "trace schema");
        Require(trace["complete"].isBool() && trace["complete"].asBool(), "trace incomplete");
        Require(trace["originalThreadsDrained"].isBool() && trace["originalThreadsDrained"].asBool(), "trace not drained");
        const auto& frames = trace["frames"];
        Require(frames.isArray() && frames.size() > 0 && frames.size() <= 64, "frame count bound");
        Require(trace["recordedCount"].isUInt() && trace["recordedCount"].asUInt() == frames.size() && Decimal(trace["skippedInputs"]) == 0 && Decimal(trace["observedCount"]) == frames.size(), "trace skipped/count");
        Require(trace["frameLimit"].isUInt() && trace["frameLimit"].asUInt() == 64 && (Decimal(trace["pixelByteLimit"]) == PixelLimit || (Decimal(trace["pixelByteLimit"]) == 256ull*1024*1024 && trace["secondsLimit"].asInt()==10)), "trace configured limits");
        VisualMotionTracker tracker;
        std::uint64_t previous = 0, bytes = 0, session = 0, captureSession = 0, displayGeneration = 0;
        double previousTime = -std::numeric_limits<double>::infinity(), maxTotalError = 0;
        int failures = 0; unsigned initialized = 0, processed = 0, skipped = 0;
        Rect firstSource{}, firstWork{}; int firstWidth = 0, firstHeight = 0, firstSourceWidth = 0, firstSourceHeight = 0;
        std::cout << std::setprecision(17);
        for (Json::ArrayIndex i = 0; i < frames.size(); ++i) {
            const auto& f = frames[i];
            const auto sequence = Decimal(f["sequence"]);
            Require(sequence > 0 && (i == 0 || sequence == previous + 1), "sequence prefix gap");
            const auto time = Number(f["timestampSeconds"]); Require(time > previousTime, "timestamp order");
            const auto s = Decimal(f["session"]), c = Decimal(f["captureSession"]), d = Decimal(f["displayGeneration"]);
            Require(s > 0 && c > 0 && d > 0, "session identity");
            if (i == 0) { session = s; captureSession = c; displayGeneration = d; }
            Require(s == session && c == captureSession && d == displayGeneration, "trace identity changed");
            Require(f["width"].isInt() && f["height"].isInt(), "image dimensions");
            const auto w = f["width"].asInt(), h = f["height"].asInt();
            Require(w >= 160 && h >= 120 && w <= 16384 && h <= 16384, "image bounds");
            const auto source = ReadRect(f["sourceRect"]), work = ReadRect(f["workArea"]);
            for (const auto* key : {"sourceLeft", "sourceTop", "sourceWidth", "sourceHeight", "roiLeft", "roiTop"}) Require(f[key].isInt(), "image geometry integer");
            const auto sw = f["sourceWidth"].asInt(), sh = f["sourceHeight"].asInt();
            Require(sw >= w && sh >= h && sw <= 16384 && sh <= 16384 && f["sourceLeft"].asInt() == source.left && f["sourceTop"].asInt() == source.top, "source buffer geometry");
            const auto sourceW = static_cast<std::int64_t>(source.right) - source.left, sourceH = static_cast<std::int64_t>(source.bottom) - source.top;
            Require(sourceW > 0 && sourceW <= sw && sw - sourceW <= 1 && sourceH > 0 && sourceH <= sh && sh - sourceH <= 1, "source rectangle/buffer crop");
            const auto left = std::max<std::int64_t>(0, static_cast<std::int64_t>(work.left) - source.left);
            const auto top = std::max<std::int64_t>(0, static_cast<std::int64_t>(work.top) - source.top);
            const auto right = std::min<std::int64_t>(sw, static_cast<std::int64_t>(work.right) - source.left);
            const auto bottom = std::min<std::int64_t>(sh, static_cast<std::int64_t>(work.bottom) - source.top);
            Require(right > left && bottom > top && right - left == w && bottom - top == h && source.left + left == f["roiLeft"].asInt() && source.top + top == f["roiTop"].asInt(), "production ROI clamp");
            Require(Number(f["sourcePixelsPerPixelX"]) == 1 && Number(f["sourcePixelsPerPixelY"]) == 1, "source mapping");
            const auto stride = Decimal(f["originalStrideBytes"]), originalBytes = Decimal(f["originalSizeBytes"]);
            Require(stride == static_cast<std::uint64_t>(sw) * 4 && originalBytes == static_cast<std::uint64_t>(sh - top) * stride - static_cast<std::uint64_t>(left) * 4, "original BGRA view layout");
            Require(static_cast<std::uint64_t>(h - 1) * stride + static_cast<std::uint64_t>(w) * 4 <= originalBytes, "original available rows");
            if (i == 0) { firstSource = source; firstWork = work; firstWidth = w; firstHeight = h; firstSourceWidth = sw; firstSourceHeight = sh; }
            Require(source == firstSource && work == firstWork && w == firstWidth && h == firstHeight && sw == firstSourceWidth && sh == firstSourceHeight, "trace image geometry changed");
            const auto count = static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h);
            Require(count <= PixelLimit - bytes, "aggregate pixel bound"); bytes += count;
            Require(f["fileComplete"].isBool() && f["fileComplete"].asBool() && f["captureValid"].isBool() && f["captureValid"].asBool() && f["originalFormat"].isString() && f["originalFormat"].asString() == "Bgra32" && f["recordedFormat"].isString() && f["recordedFormat"].asString() == "Gray8-full-resolution", "recorded pixel contract");
            Require(f["file"].asString() == "motion-" + std::to_string(i + 1) + ".pgm", "PGM filename");
            const auto file = manifest.parent_path() / std::filesystem::path(f["file"].asString());
            auto pixels = ReadPgm(file, w, h);
            MotionFrame input;
            input.image = {pixels.data(), pixels.size(), static_cast<std::size_t>(w), w, h, MotionPixelFormat::Gray8};
            input.sequence = sequence; input.timestampSeconds = time;
            MotionResult result;
            const auto operation = f["operation"].asString();
            Require((i == 0) == (operation == "Initialize"), "Initialize must occur exactly once at prefix start");
            if (operation == "Initialize") { result = tracker.Initialize(input); ++initialized; }
            else if (operation == "Process") { Require(initialized > 0, "Process before Initialize"); result = tracker.Process(input); ++processed; }
            else if (operation == "AwaitTracking") { Require(initialized > 0, "Await before Initialize"); ++skipped; }
            else throw std::runtime_error("unknown operation");
            std::string first;
            const auto bad = Compare(result, f, first, maxTotalError); failures += bad;
            std::cout << "frame=" << sequence << " operation=" << operation << " expected=" << f["reason"].asString()
                      << " actual=" << MotionReasonName(result.reason) << " accepted=" << result.accepted
                      << " total=(" << result.total.x << ',' << result.total.y << ") spatial=" << result.statistics.spatialInlierRatio
                      << " differences=" << bad << " first=" << first << '\n';
            previous = sequence; previousTime = time;
        }
        Require(bytes == Decimal(trace["pixelBytes"]), "trace aggregate byte count");
        std::cout << "REPLAY_SUMMARY frames=" << frames.size() << " initialize=" << initialized << " process=" << processed
                  << " await=" << skipped << " differences=" << failures << " max_total_difference=" << maxTotalError
                  << " timing_fields_ignored=preprocessMs,opticalFlowMs,fitMs,refillMs\n";
        return failures ? 1 : 0;
    } catch (const std::exception& e) { std::cerr << "REPLAY_INVALID " << e.what() << '\n'; return 2; }
}


namespace {
Json::Value ReadJson(const std::filesystem::path& path) {
    Require(!std::filesystem::is_symlink(path) && std::filesystem::is_regular_file(path) &&
        std::filesystem::file_size(path) <= 2 * 1024 * 1024, "JSON bound/type");
    std::ifstream stream(path, std::ios::binary);
    Json::CharReaderBuilder builder; builder["rejectDupKeys"] = true; builder["failIfExtra"] = true; builder["stackLimit"] = 32;
    Json::Value value; std::string errors;
    Require(Json::parseFromStream(builder, stream, &value, &errors), "JSON parse"); return value;
}
std::uint64_t Id(const Json::Value& value) { return value.isUInt64() ? value.asUInt64() : Decimal(value); }
double Percentile(std::vector<double> values, double q) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end()); return values[static_cast<std::size_t>(q * (values.size()-1))];
}
const char* StateName(MotionState state) {
    return state==MotionState::Tracking ? "Tracking" : state==MotionState::Paused ? "Paused" : "Uninitialized";
}
bool SameRecordedNumber(double actual, const Json::Value& expected) {
    if (std::isfinite(actual)) return expected.isNumeric() && Near(actual, Number(expected));
    // 导出器保留非有限诊断值的类型，不能把缺字段/null当作相同的零。
    return expected.isString() && expected.asString()==(std::isnan(actual)?"NaN":actual<0?"-Infinity":"+Infinity");
}
unsigned CompareModern(const MotionResult& result, const Json::Value& expected, std::string& first) {
    Require(expected.isObject() && expected["statistics"].isObject(), "modern motion/statistics missing");
    unsigned differences=0;
    const auto check=[&](bool same,const char* name) { if(!same) {++differences; if(first.empty()) first=name;} };
    check(expected["accepted"].isBool() && expected["accepted"].asBool()==result.accepted,"accepted");
    check(expected["state"].isString() && expected["state"].asString()==StateName(result.state),"state");
    check(expected["reason"].isString() && expected["reason"].asString()==MotionReasonName(result.reason),"reason");
    check(SameRecordedNumber(result.delta.x,expected["delta"]["x"]),"deltaX");
    check(SameRecordedNumber(result.delta.y,expected["delta"]["y"]),"deltaY");
    check(SameRecordedNumber(result.total.x,expected["total"]["x"]),"totalX");
    check(SameRecordedNumber(result.total.y,expected["total"]["y"]),"totalY");
    check(SameRecordedNumber(result.confidence,expected["confidence"]),"confidence");
    const auto& stats=expected["statistics"];
#define CHECK_ID(name) check(result.statistics.name==Id(stats[#name]),#name)
    CHECK_ID(inputSequence); CHECK_ID(referenceSequence); CHECK_ID(referenceGeneration); CHECK_ID(relayGeneration);
#ifndef INKEYS_VISUAL_REPLAY_BASELINE
    CHECK_ID(targetId);
#endif
#undef CHECK_ID
#define CHECK_INT(name) check(stats[#name].isInt() && result.statistics.name==stats[#name].asInt(),#name)
    CHECK_INT(attempted); CHECK_INT(forward); CHECK_INT(backward); CHECK_INT(fbValid); CHECK_INT(inliers);
    CHECK_INT(referenceCells); CHECK_INT(occupiedCells); CHECK_INT(inlierCells); CHECK_INT(interiorCells);
    CHECK_INT(added); CHECK_INT(retained); CHECK_INT(retired); CHECK_INT(initialSurvivors); CHECK_INT(consecutiveRejects);
#ifndef INKEYS_VISUAL_REPLAY_BASELINE
    CHECK_INT(targetPoints); CHECK_INT(provisionalPoints); CHECK_INT(templateCount); CHECK_INT(matchedTemplates);
    CHECK_INT(independentBlocks); CHECK_INT(candidateCount); CHECK_INT(expectedBlocks); CHECK_INT(verifiedBlocks);
    CHECK_INT(identityTiles); CHECK_INT(identityContradictions);
#endif
#undef CHECK_INT
#define CHECK_DOUBLE(name) check(SameRecordedNumber(result.statistics.name,stats[#name]),#name)
    CHECK_DOUBLE(pointInlierRatio); CHECK_DOUBLE(spatialInlierRatio); CHECK_DOUBLE(survivalRatio);
    CHECK_DOUBLE(coverageX); CHECK_DOUBLE(coverageY); CHECK_DOUBLE(runnerUpRatio);
    CHECK_DOUBLE(fbMedian); CHECK_DOUBLE(fbP95); CHECK_DOUBLE(residualMedian); CHECK_DOUBLE(residualP95);
    CHECK_DOUBLE(affineVariation); CHECK_DOUBLE(affineResidual);
#ifndef INKEYS_VISUAL_REPLAY_BASELINE
    // 年龄由录制输入的时间戳决定，是语义结果；只排除实际运行的六项耗时。
    CHECK_DOUBLE(referenceAgeSeconds); CHECK_DOUBLE(recoverySeconds); CHECK_DOUBLE(ambiguityMargin); CHECK_DOUBLE(identityScore);
    check(stats["targetPhase"].isString() && stats["targetPhase"].asString()==MotionTargetPhaseName(result.statistics.targetPhase),"targetPhase");
    check(stats["primaryReason"].isString() && stats["primaryReason"].asString()==MotionReasonName(result.statistics.primaryReason),"primaryReason");
    check(stats["usedKeyframe"].isBool() && stats["usedKeyframe"].asBool()==result.statistics.usedKeyframe,"usedKeyframe");
    check(stats["lowPointFallback"].isBool() && stats["lowPointFallback"].asBool()==result.statistics.lowPointFallback,"lowPointFallback");
    check(stats["identityDisambiguated"].isBool() && stats["identityDisambiguated"].asBool()==result.statistics.identityDisambiguated,"identityDisambiguated");
#endif
#undef CHECK_DOUBLE
    return differences;
}
// 独立GT只读绝对位置；旧文件中的Tracker输出仅用于历史精确审计，绝不当作真值。
int EvaluateRecording(const std::filesystem::path& manifest, const std::filesystem::path& truthPath) {
    const auto trace=ReadJson(manifest); const auto& frames=trace["frames"];
    const bool modern=trace["schema"].isString() && trace["schema"].asString()=="Inkeys.ScrollFollowRecording.v1";
    Require(modern || (trace["schema"].isUInt() && trace["schema"].asUInt()==1 && trace["kind"].asString()=="actual-tracker-input-fullres-gray8"), "recording schema");
    Require(modern ? trace["completePrefix"].isBool() && trace["completePrefix"].asBool() && trace["state"].asString()=="Complete"
        : trace["complete"].asBool() && trace["originalThreadsDrained"].asBool() && Decimal(trace["skippedInputs"])==0, "recording prefix incomplete");
    Require(frames.isArray() && !frames.empty() && frames.size()<=64, "recording frame bound");
    if(modern) {
        Require(Id(trace["frameCount"])==frames.size() && Id(trace["limits"]["frames"])==64 &&
            Id(trace["limits"]["pixelBytes"])==256ULL*1024*1024 && Number(trace["limits"]["seconds"])==10,"recording count/limits");
    } else Require(Id(trace["recordedCount"])==frames.size() && Id(trace["observedCount"])==frames.size(),"legacy recording count");
    const bool evaluate=!truthPath.empty(); Json::Value truth;
#ifdef INKEYS_VISUAL_REPLAY_BASELINE
    Require(!modern || evaluate,"modern full-statistics audit requires current tracker; use --gt for baseline comparison");
#endif
    if(evaluate) {
        truth=ReadJson(truthPath);
        Require(truth["schema"].asString()=="Inkeys.ScrollFollowGroundTruth.v1" && truth["source"].isString() && !truth["source"].asString().empty(), "independent GT source required");
        Require(truth["frames"].isArray() && truth["frames"].size()==frames.size(), "GT must cover every input");
    }
    VisualMotionTracker tracker;
    std::uint64_t previous=0, bytes=0;
    double previousTime=-std::numeric_limits<double>::infinity(), startTime=0, lastUpdate=0, maxGap=0, finalError=0;
    double lastAllCorrect=0, allMaxGap=0, allFinalError=0;
    unsigned processed=0, awaited=0, accepted=0, correct=0, identifiable=0, unsafe=0, held=0, longestHeld=0, paused=0;
    unsigned allHeld=0, allLongestHeld=0, allWithinTolerance=0, auditFailures=0, transactionFailures=0, invalidResults=0;
    bool initialAccepted=false; int width=0,height=0; double previousTruthX=0, previousTruthY=0;
    MotionResult heldResult; MotionVector trustedTotal{};
    std::vector<double> errors, processErrors, acceptedErrors, times;
    for(Json::ArrayIndex i=0;i<frames.size();++i) {
        const auto& f=frames[i]; const auto sequence=Id(f[modern?"seq":"sequence"]); const auto time=Number(f["timestampSeconds"]);
        Require(sequence>0 && (i==0 || (previous<std::numeric_limits<std::uint64_t>::max() && sequence==previous+1)) && time>previousTime, "input sequence/time continuity");
        Require(f["width"].isInt() && f["height"].isInt() && f["captureValid"].isBool(),"input dimension/quality types");
        if(i==0) { startTime=lastUpdate=lastAllCorrect=time; width=f["width"].asInt(); height=f["height"].asInt(); }
        Require(width>=160 && height>=120 && width<=16384 && height<=16384 && f["width"].asInt()==width && f["height"].asInt()==height, "input geometry");
        if(modern) {
            Require(time-startTime<=10.000001 && f["format"].asString()=="Gray8" && Id(f["strideBytes"])==static_cast<unsigned>(width) && f["inputLowInformation"].isBool(), "recording image/time contract");
            const auto& identity=f["identity"]; const auto& firstIdentity=frames[0]["identity"];
            for(const auto* key:{"windowGeneration","businessSession","captureSession","displayGeneration"})
                Require(Id(identity[key])>0 && Id(identity[key])==Id(firstIdentity[key]), "recording identity changed");
            Require(Id(identity["sequence"])==sequence && Id(identity["businessSession"])==Id(trace["businessSession"]),"recording frame identity");
            const auto format=f["originalFormat"].asString();
            Require((format=="BGRA32" || format=="Gray8") && format==frames[0]["originalFormat"].asString(),"original pixel format changed/unknown");
            const std::uint64_t row=static_cast<std::uint64_t>(width)*(format=="BGRA32"?4:1);
            const auto stride=Id(f["originalStrideBytes"]), available=Id(f["originalSizeBytes"]);
            Require(stride>=row && available>=row && static_cast<std::uint64_t>(height-1)<=(available-row)/stride,"original pixel view layout");
        } else {
            for(const auto* key:{"session","captureSession","displayGeneration"})
                Require(Id(f[key])>0 && Id(f[key])==Id(frames[0][key]),"legacy recording identity changed");
            Require(f["fileComplete"].isBool() && f["fileComplete"].asBool() && f["originalFormat"].asString()=="Bgra32" &&
                f["recordedFormat"].asString()=="Gray8-full-resolution","legacy pixel contract");
        }
        const std::uint64_t count=static_cast<std::uint64_t>(width)*height;
        const auto limit=modern ? 256ULL*1024*1024 : PixelLimit;
        Require(bytes<=limit && count<=limit-bytes, "recording pixel bound"); bytes+=count;
        Require(f["file"].asString()==(modern?"input-":"motion-")+std::to_string(i+1)+".pgm", "recording filename");
        auto pixels=ReadPgm(manifest.parent_path()/f["file"].asString(),width,height);
        MotionFrame input{{pixels.data(),pixels.size(),static_cast<std::size_t>(width),width,height,MotionPixelFormat::Gray8},time,sequence,f["captureValid"].asBool()};
#ifndef INKEYS_VISUAL_REPLAY_BASELINE
        input.inputLowInformation=modern && f["inputLowInformation"].asBool();
#endif
        const auto operation=f["operation"].asString();
        Require((i==0)==(operation=="Initialize"),"Initialize must occur exactly once at prefix start");
        const bool process=operation=="Process", await=operation=="AwaitTracking";
        Require(operation=="Initialize" || process || await,"unknown operation");
        if(modern) Require(await ? f["motion"].isNull() : f["motion"].isObject(),"operation/motion association");
        MotionResult result; double ms=0;
        if(await) {
            // 等待没有运行Tracker；保留公开位置/状态，不能伪造一次零坐标结果或一次Process。
            result=heldResult; result.accepted=false; result.delta={}; ++awaited;
            held=0; lastUpdate=time; // 用户尚未启动Tracking的时间不计入算法连续性门。
        } else {
            const auto before=std::chrono::steady_clock::now();
            result=process ? tracker.Process(input) : tracker.Initialize(input);
            ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();
            if(process) { ++processed; times.push_back(ms); paused+=result.state==MotionState::Paused; }
            else initialAccepted=result.accepted;
            if(!result.accepted && (result.delta.x!=0 || result.delta.y!=0 || result.total.x!=trustedTotal.x || result.total.y!=trustedTotal.y)) ++transactionFailures;
            if(result.accepted) trustedTotal=result.total;
            heldResult=result;
        }
        const bool finiteResult=std::isfinite(result.total.x) && std::isfinite(result.total.y) && std::isfinite(result.delta.x) &&
            std::isfinite(result.delta.y) && std::isfinite(result.confidence);
        if(!finiteResult || (result.accepted && result.state!=MotionState::Tracking)) ++invalidResults;
        double error=0; bool locatable=false; unsigned differences=0; std::string firstDifference;
        if(evaluate) {
            const auto& gt=truth["frames"][i]; Require(Id(gt["sequence"])==sequence && gt["locatable"].isBool(),"GT input association");
            const auto x=Number(gt["totalX"]), y=Number(gt["totalY"]);
            error=finiteResult ? std::hypot(result.total.x-x,result.total.y-y) : std::numeric_limits<double>::infinity();
            locatable=gt["locatable"].asBool(); errors.push_back(error); allFinalError=error;
            // 所有输入的位置误差/停滞都报告；通过门另只评价可定位的Process，避免把等待当计算失败。
            if(error<=2 && result.state==MotionState::Tracking) {++allWithinTolerance; allHeld=0; lastAllCorrect=time;}
            else {allLongestHeld=std::max(allLongestHeld,++allHeld); allMaxGap=std::max(allMaxGap,time-lastAllCorrect);}
            if(process) {
                processErrors.push_back(error); finalError=error;
                if(result.accepted) { ++accepted; acceptedErrors.push_back(error); }
                if(locatable) {
                    ++identifiable;
                    if(result.state==MotionState::Tracking && error<=2 && (result.accepted || (Near(x,previousTruthX) && Near(y,previousTruthY))))
                        {++correct; held=0; lastUpdate=time;}
                    else {longestHeld=std::max(longestHeld,++held); maxGap=std::max(maxGap,time-lastUpdate);}
                } else {
                    // 不可定位的歧义帧连accepted零更新也不应发布；真实保持误差仍在all指标中。
                    unsafe+=result.accepted; held=0; lastUpdate=time;
                }
            }
            previousTruthX=x; previousTruthY=y;
        } else if(modern && !await) {
            differences=CompareModern(result,f["motion"],firstDifference); auditFailures+=differences;
        }
        std::cout<<std::setprecision(12)<<"REPLAY_FRAME sequence="<<sequence<<" operation="<<operation<<" has_tracker_result="<<!await
            <<" accepted="<<result.accepted<<" state="<<StateName(result.state)<<" reason="<<MotionReasonName(result.reason)
            <<" total_x="<<result.total.x<<" total_y="<<result.total.y<<" gt_error="<<error<<" locatable="<<locatable
            <<" process_eligible="<<(evaluate && process && locatable)<<" process_ms="<<(process?ms:0)
            <<" differences="<<differences<<" first="<<firstDifference<<'\n';
        previous=sequence; previousTime=time;
    }
    Require(bytes==Id(trace["pixelBytes"]),"recording byte count");
    if(!evaluate) {
        std::cout<<"RECORDING_AUDIT differences="<<auditFailures<<" transaction_failures="<<transactionFailures<<" invalid_results="<<invalidResults
            <<" timing_fields_ignored=preprocessMs,opticalFlowMs,fitMs,refillMs,coarseMatchMs,verificationMs\n";
        return auditFailures || transactionFailures || invalidResults ? 1 : 0;
    }
    const bool sufficient=processed>0 && identifiable>0;
    const double rate=identifiable?double(correct)/identifiable:0;
    const double maximum=acceptedErrors.empty()?0:*std::max_element(acceptedErrors.begin(),acceptedErrors.end());
    const bool pass=sufficient && initialAccepted && paused==0 && transactionFailures==0 && invalidResults==0 && rate>=.95 &&
        Percentile(acceptedErrors,.95)<=2 && maximum<=5 && finalError<=5 && unsafe==0 && longestHeld<=2 && maxGap<=.350001;
    std::cout<<"GT_SUMMARY frames="<<frames.size()<<" initialized="<<initialAccepted<<" processed="<<processed<<" await="<<awaited
        <<" identifiable="<<identifiable<<" correct="<<correct<<" accepted="<<accepted<<" paused="<<paused<<" correct_rate="<<rate
        <<" all_p95="<<Percentile(errors,.95)<<" all_within_tolerance="<<allWithinTolerance<<" all_final_error="<<allFinalError
        <<" all_max_held="<<allLongestHeld<<" all_max_gap_s="<<allMaxGap<<" process_p95="<<Percentile(processErrors,.95)
        <<" accepted_p95="<<Percentile(acceptedErrors,.95)<<" accepted_max="<<maximum<<" final_error="<<finalError<<" unsafe="<<unsafe
        <<" transaction_failures="<<transactionFailures<<" invalid_results="<<invalidResults<<" max_held="<<longestHeld<<" max_gap_s="<<maxGap
        <<" process_p95_ms="<<Percentile(times,.95)<<" status="<<(!sufficient?"insufficient-data":pass?"pass":"fail")<<" pass="<<pass<<'\n';
    return !sufficient ? 2 : pass ? 0 : 1;
}
}
int RunVisualMotionReplay(int argc, char** argv) {
    if(argc!=2 && !(argc==4 && std::string(argv[2])=="--gt")) return 2;
    try {
        const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(argv[1])));
        if(argc==4) return EvaluateRecording(path,std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(argv[3]))));
        if(ReadJson(path)["schema"].isString()) return EvaluateRecording(path,{});
        return LegacyMotionAudit(path);
    } catch(const std::exception& e) {std::cerr<<"REPLAY_INVALID "<<e.what()<<'\n';return 2;}
}
#ifdef INKEYS_VISUAL_REPLAY_STANDALONE
int main(int argc,char** argv) {return RunVisualMotionReplay(argc,argv);}
#endif
