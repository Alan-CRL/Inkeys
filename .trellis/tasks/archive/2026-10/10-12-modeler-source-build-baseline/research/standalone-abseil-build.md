> 历史方案：2026-10-12用户新授权后停止作为产品路线；当前权威路线见 vcpkg-provider.md。以下保留原构建来源与证据。

# 独立源码构建证据与已批准路线

2026-10-12。原目录legacy=用户compile_library/ink-stroke-modeler/ink-stroke-modeler。
三架构_deps/abseil-cpp-src完整1532文件，归一换行相同；HEAD bc257a88f7c1939f24e0379f14a3589e926c950c，config.h:120-121=20250512.0。现产品328个.h及22个.inc、1个.lds依赖文件与其同名源一致。
legacy18个Modeler头、13个runtime cc与原Inkeys3-Draw3来源全文相同，product18头相同；numbers.h本地兼容已包含于legacy。exclude internal/type_matchers.cc(GMock TESTONLY)。
原CMake MinSizeRel flags /O1 /Ob1 /DNDEBUG，默认 /DWIN32 /D_WINDOWS /GR /EHsc，CRT /MT、C++20、模块扫描false。Abseil自己的Debug变/MTd，所以本任务build MinSizeRel，不能以产品Debug名构建依赖Debug。
旧CMakeCache存在搬迁前绝对路径、VS18/Insiders，禁止复用。
本机VS由vswhere返回18/Community，v143默认14.44.35207，Hostarm64三个target cl均安装；MSBuild Current/Bin/arm64可用；配套cmake4.3.1。动态定位，不硬编码版本。
Abseil顶层测试关闭可离线；Modeler顶层仍Fetch GTest，所以不用该入口。install可能把options 2改1，仅staging副本，不覆盖现headers/options。
新build位于repo Build会继承Directory.Build.props/targets vcpkg unless生成工程ImportDirectoryBuildProps/Targets=false；CMAKE_VS_GLOBALS可设置。
父旧research/modeler-source-build-and-abseil-split.md中的/REMOVE方案已被用户明确替换，本文件是阶段1权威构建路线。新库全新build/merge，不要求152对象数/旧obj byte相同。

## Abseil upgrade evaluation
用户已批准继续20250512.0。20250512.1只GCC7/8 constexpr和-Wundef；.2 HexStringToBytes符号扩展，不在当前调用。新Status性能改动偏失败消息构造；StatusOr emplace异常重复析构修复没有当前emplace调用。Modeler直接API仅Status/StatusOr/string_view/Substitute/StrCat/StrFormat/AbslStringify；自身Duration/Time为double，不用Absl时间；core积分/Kalman用std容器。不把hash/random/mutex等archive库存当实际hot path。
官方支持: https://github.com/abseil/abseil-cpp/releases/tag/20250512.2 ; https://abseil.io/about/compatibility ; https://github.com/google/ink-stroke-modeler/blob/main/CMakeLists.txt (仍pin20250512.0)。此结论为依赖/commit研究，不是实测性能。
