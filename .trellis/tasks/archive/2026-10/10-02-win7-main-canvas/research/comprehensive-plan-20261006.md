# Win7 单入口全面分界候选（2026-10-06）

用户明确要求修复；若仍缺根因，交付全面单CMD新包，尽量一次/少次定位。继续原任务in_progress，未授权commit/push/archive。已读before-dev/spec和最新三份224620研究；本轮最小行为缺口是18项未控制类型路由/资源绑定及full程序功能群，且仅独立WARP与不完整提交不能解释硬件光学刷新。

## 实现范围和所有权

- GPU实现者：Draw3.HiddenWindowTest.cpp/.h、Draw3.PixelDiagnosticShaders.h、Generate-Win7-PixelDiagnostic.ps1、Win7-PixelDiagnostic.hlsl及必要research。只扩显式永不显示的独立fixture；正式HLSL/8CSO/301–304/普通输入渲染不变。先保留原18项/production4 exit，再单列matrix结果。增加RunRendererHardwarePixelTest() noexcept共享fixture，原RunRendererPixelTest()仍WARP；硬件探针禁止暗中WARP fallback。确实无硬件FL11时记录HRESULT/status=unavailable并exit3，不把SKIP当PASS；其它运行错误保留失败。
- 采集实现者：IdtMain.cpp早退硬件CLI、Collect-Win7.ps1、Run-Win7-Diagnostics.cmd/.ps1及必要research。新CLI --draw3-renderer-pixel-test-hardware按原argv count==2识别、产品初始化前早退，普通入口不变。仅一CMD，一根时间目录三阶段：probe一次阶段内WARP+Hardware各独立精确子进程→普通ULW→full ULW。新EXE日志自动开启；Hardware unavailable单列，其它失败如实保留且继续两正常轮；默认/ProbeOnly/SkipProbe旧合同保留。探针每driver最多600秒，单GPU等待仍3秒，完整回传根路径突出。更新PS2实际mock覆盖两probe、退出3、超时标记、路径空间/大流/非零继续/环境恢复；不实际等待600秒、不启产品GUI。
- 主会话：协调冻结、ARM64 Debug/x64 Release完整Solution、两WARP矩阵及硬件CLI、Headless、真实WinPS ProbeOnly、来源/8CSO/旧资料/EXE/ZIP核验，唯一CMD新包与README/manifest/summary判断合同。记录spec/session，任务不关闭。

## 重点矩阵

1. 真实compiled compact整数转换/4..6/type4路由哨兵和float路由对照；成功/错误路径都非透明，输出可判别颜色。必须dump核ftoi/if/guard存在，不能优化成常色。full入口在类型转换之前以运行期b0条件输出常色、full float-type route对照，保留其它功能/声明；不把这些变体当生产替换。
2. 同原PS/VS/目标/上传/混合，sampler、b1..3、8个Texture2D、4个Texture2DArray四组16种组合（含baseline），每组测试后恢复原实际绑定并重测原PS，形成A/B/A。Dummy已初始化、维度/CB112/48/48正确、不与任何RTV别名；查询PS实际绑定而非只打印setter。全零组对应NULL零语义；额外fully-bound finite-valid Laser常量对照，分开潜在未选分支NaN。所有新结果与old production退出分离。
3. 当前生产完整PS从main top-level范围拆成Cursor4..6（始终保留）及六群：Laser7..13（不含Cursor）、Operators1/2、History14/15、TrustedSnapshot20、Shape16..19、DefaultInk0/3。各生成FullWithout群、CursorWith单群、CursorFamilyOnly共13变体；保持每群原逻辑/顺序，fallback仅对移除的类型透明。每variant baseline/fullybound各测一次。必须保存实际FXC/资源表/指令/签名/profile/source hashes，明确删除/保留的群，不凭变体名字推断compiled范围。
4. 末尾原PS及Cursor-only控制，确保全矩阵后的原管线有可解释反向结果。逐项像素/中心/角、invocation/绑定/设备错误，matrix摘要数不覆盖旧production4判断。保守600秒按最坏GPU等待和最终实际case总数验证充足。

## 判别和验收

矩阵是诊断，不保证一次唯一定位；如compact路由失败→小程序转换/gate分界；若原字节随绑定恢复且反向失败→具体绑定组强因果；若某功能群加入触发/移除恢复→完整程序具体群执行差异；交互群仍可能混杂，报告不越界。原PS始终fail也应留下逐步可比较结果，而非停止所有后续采集。硬件normal有非零源时才解释ULW光学A/B；WARP透明源不能靠full参数生成墨迹。

使用上轮已验证构建脚本/依赖及900秒build限制；原生成3small+3full arrays须逐字节不变。全矩阵可只在非GUI早退fixture内运行；硬件不可用独立SKIP，不伪造Hardware PASS。C++/PS/HLSL保持原BOM/CRLF，最小diff。不永久加入dummy资源/诊断shader给生产渲染，不更新工具链/工程/功能开关，不清理其他发布任务变更。

新artifact根：C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-comprehensive-20261006。保留旧OneClick ZIP和原G六文件，记录来源baseline与未提交delta。仅一个CMD；README突出两机各一次、全部三个阶段根目录、正常退出/观察、probe FAIL仍继续。
