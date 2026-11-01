# 2026-11-01 继续修改与验证

## 完成的修改

- 性能/实验选项的侧栏glyph互换，路由和排列不变。
- 界面60 FPS、画布120 FPS和精准选项追加三语言本地化推荐后缀，数值和默认值不变。
- 原绘图效果优化分类改名绘图环境，其完整设备选项放在通用之前。
- 移除统一的新笔生效提示及废弃语言键；保留原新笔锁存逻辑。
- 粒子效果保留BETA禁用说明行，无toggle和配置编辑；Controller构造和setter均强制false，旧配置/启用请求不能绕过，重复请求不额外wake。
- 仅修改本轮约定7个UI/i18n文件和3个Draw3文件；保留原未提交实现，BOM/CRLF无变化。任务/spec同步最新要求。

## 通过的检查

| 验证 | 结果 | 日志 |
| --- | --- | --- |
| InkeysRepo.sln Debug / ARM64 | 退出码0，0错误，既有警告保留 | validation/refinement/build-arm64.* |
| Inkeys.exe --draw3-parked-desktop-exit-test | 退出码0；包含真实Controller使用粒子配置true构造、重复setter(true/false)、Get恒false且wake不变 | validation/refinement/drawing-particles.* |
| InkeysHeadlessTests.exe --setting-copy-only --no-window | 退出码0，三语言文案/卡片专项 | validation/refinement/setting-copy.* |
| Scripts/i18n.ps1 check | en-US、zh-TW均559/559，通过 | 本轮工具输出 |
| R7静态复核、BOM/CRLF与git diff --check | 通过，无源码复核阻塞 | research/refinement-review.md及工具输出 |

构建沿用动态发现的ARM64原生MSBuild、同invocation PATH规范化、正常Windows执行环境，并只清理此前确认会出问题的生成Inkeys.ilk缓存。未改工程配置、工具链或依赖。

## 完整设置专项的限制（未通过）

1. `--setting-home-only --no-window`（该旧名字实际调用完整RunSettingDesignTests）首轮退出码1，stderr有两条 `bounded EVENT confirms draw/copy execution completed`，来自本轮未修改的setting_home_warp_tests.cpp离屏D3D11 EVENT完成检查。
2. 排除其他并行测试后串行复跑同一入口，stderr为空，但达到300000 ms总时限，由测试包装器停止自己的进程，未获得成功退出码。最后输出在scale=1.5侧栏颜色/交互矩阵，之前WARP阶段已推进；不能将这次串行执行记为通过。
3. 未确定首轮EVENT失败的根因，未改相关离屏渲染实现或测试阈值。没有继续反复重启整套测试；使用现有无GPU基准的文案专项补充本轮相关显示验证，并如实保留完整专项的失败/超时。

记录：`validation/refinement/setting-design.*`、`setting-design-serial.*`；串行超时状态为 `.status.txt`，不是成功exitcode。

## 未执行

按用户要求未启动应用/设置GUI、Computer Use或hidden HWND fixture，未做真实粒子视觉/设备测试。未创建commit、push或自动归档。前一轮完整无窗口套件通过记录保留于validation.md，不冒充本轮完整专项通过。
