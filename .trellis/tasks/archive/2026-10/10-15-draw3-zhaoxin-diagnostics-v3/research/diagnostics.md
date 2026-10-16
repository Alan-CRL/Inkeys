# v3 诊断与包实现记录

## 修改边界

- 所有权为 `Draw3.ResourceProbe.cpp`、`Draw3.Performance.cpp` 的导出元数据，以及 `Scripts/Draw3Performance/**`；不修改 Renderer/Host/settings 的生产行为。
- `--draw3-resource-probe-upload-test` 在已有早期 dispatcher 调用 renderer 提供的 `RunRendererUploadValidation()`。与纯 CPU list/test 分开，此入口只允许 WARP 离屏资源，无 HWND/Host/RTS。
- 新包标记 `INKEYS_DRAW3_DIAGNOSTICS_V3`，package/probe revision=3、profile=`draw3-resource-v3-fixed`；产品原始 schema 和 probe CSV schema 保持原值。原始 v1/v2 可由新分析器读取。
- Build-Package 仍只交付 EXE、CMD、三个用户 PS1、README、manifest 共 7 文件；原有 PE/EXE/脚本 SHA、最终进程身份、停止门和资源预算继续使用。

## 成对工作量

原20 profiles×hardware/WARP的40项语义保留，追加8 profiles×2 backend共16项。四个 paired_workload 为 ink64、ink4k、highlighter64、highlighter4k；每组普通动态与 DEFAULT 两个 upload_policy。每轮均主资源上传、6顶点Draw、operator add复制、256 B专用合成资源DISCARD写32 B、6顶点全目标矩形合成。普通墨迹按cap追加NO_OVERWRITE（首轮DISCARD），不支持时逐批DISCARD；普通荧光笔与生产一致逐批DISCARD。兼容主资源使用DEFAULT字节box、offset0；两侧小合成都相同。

大小目标为64×64、3840×2160；主几何固定为从8,8到width-8,height-8的墨迹/荧光笔段，合成矩形为0,0到width,height；主写32 B墨迹或48 B荧光笔。荧光笔写入两个相同24 B原语，但固定6顶点Draw只取首原语，属于固定离屏合同，不能声称完整生产批次重放。

每项仍cold1/warmup1/measure4，原生8s、父worker20s、matrix480s、4096calls、单raw4MiB、矩阵64MiB、双流各保留1MiB并继续排空。unsupported/partial/timeout不回退、不补0、不推胜者。

## 分析与公平性

新版 `resource-upload-pairs.csv` 和报告按同backend/workload比较；只有完整身份、同adapter/driver/shader、同尺寸/字节/Draw负载才给CPU叶调用四轮合计比值。逐轮raw验证两次实际Draw、6顶点/1实例、主资源和小合成实际写入字节、专用256 B描述与DEFAULT offset0；定义中的update_method刻意不同，其余工作量匹配。CPU、四个measure GPU命令区间合计与drain分列；GPU缺样保持null，GPU窗口含CPU提交空隙，CPU更快不等于完整产品获胜。

Performance从两个初始化事件导出hardware acceleration requested、compat requested/effective、actual upload policy、adapter known及原因；可区分用户关闭、WARP、非兆芯和未知adapter。旧包缺事件保持未知。

README要求兆芯保持硬件加速开，compat开/关分别正常退出并重启、相同动作两轮真实书写；后测离屏不能替代真实A/B。高通使用ARM64、实际普通路径并与用户补充v2比较。采集器不会改用户配置。

## 验证记录

- WinPS5.1首次全套：v1/v2分析、资源矩阵成功/身份/缺文件/原始损坏等通过；到v3成功夹具时一个fake WARP进程在并行构建负载下5s内尚未创建probe-start，被外部deadline正确终止。该夹具改用生产上限20s（测试矩阵120s），生产20/480s未改。
- 调整后隔离v3夹具WinPS5.1通过37项：success、partial、shader/adapter/work/upload mismatch、GPU unavailable。输出 `Build/Draw3PerformanceV3Tests/isolated/offline-b3a6c7b5aa9e467b8fad05da2e7570f9/v3-test-results.json`。
- 最终全套WinPS5.1、pwsh各通过267项，分别日志 `Build/Draw3-v3-windows-test.log`、`Build/Draw3-v3-pwsh-test.log`；结果位于windows-final/offline-969d12623a13403996d07694de1e7276、pwsh-final/offline-bfc32cdb1e8b4d24ba9d3dfa64dfcf7d。
- 全部拥有文件 AST解析和 git diff --check通过；已核对源BOM（Performance.cpp有、ResourceProbe.cpp无）并保留CRLF。最终构建、实际离屏D3D、双架构打包由主会话执行，不启动GUI。
- reviewer补齐EVENT缺失导致未drain仍complete的门禁后，追加drain_incomplete夹具；WinPS5.1/pwsh各通过5项，输出drain-windows/offline-047dff9a2ebe41f9b2da08460fb61afc和drain-pwsh/offline-1680f3b075b541e9bbaf96273bb39baf。随后补正报告/采集器/CMD可见v3版本文案并加轻量断言。

- 最终v3可见文案+drain轻量回归pwsh通过7项，输出 "Build/Draw3PerformanceV3Tests/final-title-pwsh/offline-ec0c39dcd38f412087b6cdd33d50894c/v3-test-results.json"。再次搜索无旧用户可见包2标记；历史v2夹具保持。
