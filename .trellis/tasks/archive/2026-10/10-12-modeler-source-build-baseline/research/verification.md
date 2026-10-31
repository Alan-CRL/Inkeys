# 阶段1验收：vcpkg Abseil + Modeler source（PASS）

2026-10-12，用户批准实施并随后授权改用vcpkg20250512.1#0；本阶段完成。尾段算法、公共API、模型参数和Draw3几何接纳不变，末钩未在本阶段修复。

## Provider and ABI
根manifest精确20250512.1#0，port tree2999911d2655e693394770d9dd7600b277a86f37；baseline99a97de2cb371449d4fb9dc970f2ac562d689ec2及其他override不变。三个static-v143 triplet只给Abseil指定C++20/REQUIRED/EXTENSIONS OFF。
VS由vswhere定位，v14314.44.35207 / MSVC19.44.35229、SDK10.0.26100.0；使用ARM64原生MSBuild。Modeler13TU /O1 /Ob1 /MT C++20 NDEBUG iterator0 /EHsc /GR /fp:precise；Abseil vcpkg Release /MT /O2 /Oi /Gy /DNDEBUG /Z7 /utf-8。
三架构各88Release Absl库，AA64/8664/14C、无Modeler定义、无MTd/MD混入、lts_20250512、头probe的std string_view/ordering与options=1一致。部分纯实现库无FAILIFMISMATCH标记，按真实LIBCMT directives及实际编译命令核验；std别名string_view archive无public symbols是正常状态。
ABSL_BUILD_TESTING/HELPERS OFF且无test/helper目标；generic BUILD_TESTING ON如实记录。匹配头/库来自同一installed包，全部消费者统一Release；没有用保留的.0头或merge。
证据：Build/modeler-baseline/vcpkg-abseil/{ARM64,x64,Win32}/abi，ARM64/provider-validation.json，保留的CMakeCache/build.ninja/port-snapshot。

## Build and replay
旧四配置在切换前完整保存于Build/modeler-baseline/legacy。新程序独立obj/bin路径，使用既有demo/test solution和完整InkeysRepo.sln，实际退出码检查。3600s恢复监督（早期1800s），同PowerShell唯一Path、禁node reuse；无GUI。

| 配置 | 新demo/tests exit / 耗时 | 主solution exit / 耗时 | 回放cases / assertions / failures |
|---|---|---|---|
| ARM64-Debug | 0 / 367.17s | 0 / 536.78s | 19 / 128412 / 0 |
| ARM64-Release | 0 / 306.33s | 0 / 649.45s | 19 / 128412 / 0 |
| x64-Release | 0 / 1091.86s | 0 / 691.42s | 19 / 128412 / 0 |
| Win32-Release | 0 / 407.58s | 0 / 351.22s | 19 / 128412 / 0 |

四组新回放SHA均0119fd2a0edfdefa055ac3b5e1fde5aeadb4c693bd67f7157b55c09dae02b646；旧SHA均d7e7832437593002f4809dbddb86138c9f9ad8b5259ef234c903c719ae1d182a。
唯一报告差异是根$.abseil_version的20250512.0→20250512.1（显式披露）。ABI/输入/参数/状态/计数/时间/全部Result字段与IEEE bits精确一致，无容差放宽。覆盖两预测器、直线/圆弧/急弯Up/单点/重复点/停笔/长笔、重复Predict、Save/Restore、非法输入、时间倒退和预算。
Headless --no-window exit0，pen-width/codec、EraserAttribute216 layouts失败0、animation PASS；其MTd工程未修改。

## Performance
所有编译进程结束后测量；三个Release架构各30轮交替成对，共90pairs。相同EXE预先replay确认版本/语义，预热后8项操作（两预测器×Move/Predict/Up/Reset），程序SHA匹配build-result；全部summary passed=true。
规则预先固定：paired median log-ratio 95%CI下界>0、前后半均为正且效果超出组合MAD噪声才判退化。没有操作触发该规则；不把噪声内的小波动解释为性能收益。
Reset范围：完成Up后64次无参Reset批次均摊（保留参数，轻量状态清理），有100ns计时量化；Reset(params)/新Down初始化仅功能覆盖，未单独计时。不据此宣称所有初始化成本已验证。

| 架构 | predictor | operation | paired source/legacy | combined noise | repeatable regression |
|---|---|---|---:|---:|---|
| ARM64-Release | stroke_end | move | 0.98887 | 0.02697 | false |
| ARM64-Release | stroke_end | predict | 1.02044 | 0.06845 | false |
| ARM64-Release | stroke_end | up | 1.00000 | 0.07488 | false |
| ARM64-Release | stroke_end | reset | 1.00000 | 0.00000 | false |
| ARM64-Release | kalman | move | 0.99138 | 0.01325 | false |
| ARM64-Release | kalman | predict | 0.99547 | 0.01614 | false |
| ARM64-Release | kalman | up | 1.00000 | 0.07766 | false |
| ARM64-Release | kalman | reset | 1.00000 | 0.00000 | false |
| x64-Release | stroke_end | move | 0.99900 | 0.01121 | false |
| x64-Release | stroke_end | predict | 0.99309 | 0.00279 | false |
| x64-Release | stroke_end | up | 1.00000 | 0.06552 | false |
| x64-Release | stroke_end | reset | 1.00000 | 0.00000 | false |
| x64-Release | kalman | move | 0.98134 | 0.01013 | false |
| x64-Release | kalman | predict | 0.99891 | 0.00389 | false |
| x64-Release | kalman | up | 1.00000 | 0.06552 | false |
| x64-Release | kalman | reset | 1.00000 | 0.00000 | false |
| Win32-Release | stroke_end | move | 1.00648 | 0.01511 | false |
| Win32-Release | stroke_end | predict | 0.99748 | 0.02536 | false |
| Win32-Release | stroke_end | up | 1.00183 | 0.03449 | false |
| Win32-Release | stroke_end | reset | 1.00000 | 0.52418 | false |
| Win32-Release | kalman | move | 1.00244 | 0.01512 | false |
| Win32-Release | kalman | predict | 1.00294 | 0.01191 | false |
| Win32-Release | kalman | up | 1.00458 | 0.03058 | false |
| Win32-Release | kalman | reset | 1.00000 | 0.52418 | false |

原始每轮报告/分布/CI/mean/p95/MAD在Build/modeler-baseline/benchmarks/{ARM64-Release,x64-Release,Win32-Release}。

## Actual integration and compatibility
12个consumer/config组合真实CL/header/tlog/map：每目标13个Modeler源码对象；113个Absl头来自installed包；Absl定义由vcpkgRelease archives提供，无旧merge/custom/debug混链。业务cpp/cppm obj独立，Debug /Od、Release /GL保持。numbers.h及13cc/18Modeler头原bytes保留。
四主产品PE均以data-only方式核验资源301-304，全部有效DXBC；shader编译/资源链完整，未执行GUI。x64/Win32新旧模块测试imports新增0/删除0；主产品已筛查的常见Win8+静态API候选为空。这是静态边界证据，不等于Win7真机兼容证明。
741个原头/旧merge hash变化0；17 tracked文件BOM/CRLF保持，git diff --check、Python AST/XML及Trellis context validate PASS。

## Failures, correction and retained evidence
- 首次旧ARM64 sandbox C1902 PDB环境错误，按既有证据用正常本机环境/项目输出隔离验证成功，不改toolchain或源码绕过。
- 早期清掉全部PATH导致vcpkg Value-null，改同invocation保留原值并恢复唯一Path键成功。
- 首次source配置Target内ClCompile Update范围误扩张，把业务cpp/cppm写同obj并产生链接错误；只修共享metadata标记/逐项Condition，未改算法。后续完整Rebuild/真实scope检查通过。
- 两个Win32首次因D盘满而原版OpenSSL/install/PptCOM文件写入失败；supervisor的result持久化也失败，因此未冒称原MSBuild退出码已保存。first-no-space日志/摘要保留；释放本任务缓存并在用户清理后恢复，两个Rebuild与回放最终exit0，未升级版本/工具链或改生产配置。ignored监督扩到3600s。

## Cleanup and delivery
清理仅本任务可重建中间产物、failed custom中间输出、旧手工Abseil/source副本、独立ARM provider buildtrees/packages、本次x64/x86 Absl staging/build目录及4个未跟踪demo cso。约12.08GiB已释放；保留旧/新EXE/PDB、replay/benchmark报告、map/tlog/13Modelerobj、版本/flags/hash/port/ABI证据。
正式三架构VcpkgInstalled包、binary cache/downloads、无关旧cache、原merge/原头及用户compile_library资料保留。root原脚本/库只保留小型历史metadata；当前没有手工库导入路线。
Trellis最终checker未发现尚未解决的代码缺陷；只需本报告/checkbox/归档/日志收尾。GUI/D3D运行期、实体笔、真实Windows7均未执行。无commit/push，后续尾段重构继续父任务planning。
