# dev 中转分支验证

日期：2026-10-18；分支 codex/draw3-compatibility-fixes；基底 dev 5d319bbf5c520dec97dcb37ad2fcc7e26d029bbb。GitHub只读REST已核实远端dev SHA与本地origin/dev相同；SSH fetch曾因agent签名通信失败退出，未修改SSH/Git签名配置。

## 提取范围

基于dev手工移植22f7f3c9的产品设备偏好/兆芯结构上传/独立合成资源和e8dd090c的RTS/鼠标回退；没有继承诊断提交42745199，亦不整份cherry-pick含诊断依赖的源提交。

保留硬件优先或直接WARP、实际Hardware+VendorId0x1D17兼容开关、结构上传head维护、256B合成buffer、启动偏好跨恢复快照、Extended→BasicXY→Win32Mouse及安全清理/停止保存合同。保留必要普通错误日志和dev原有诊断，不迁新增PerformanceSession/InputTrace/Launch/ResourceProbe、ConsoleOutput.Performance、诊断包脚本及诊断任务历史。

独立研究确认42745199中ContactInput、DrawingController、InkHistoryGpu、LaserParticleSystem/LaserParticles和RendererSnapshot等修改仅为采集接线/包装，本次保留这些文件的dev版本。

## 实际验证结果

| 项目 | 结果 | 日志（仓库忽略的TestResults） |
|---|---|---|
| InkeysRepo.sln Debug/ARM64，动态定位ARM64原生MSBuild | build02退出0，0错误，85条warning，00:00:52.63 | draw3-compatibility-dev-build-02.log |
| 完整Headless --no-window | 退出0，最终PASS animation correctness，新RTS/鼠标测试均运行通过 | draw3-compatibility-dev-headless-01.log |
| --draw3-device-policy-test | 退出0，硬件/WARP选择顺序通过 | draw3-compatibility-dev-device-policy.out.log |
| --draw3-renderer-upload-test | 退出0，dynamic_discard/dynamic_append/DEFAULT三模式实际WARP上传、像素读回通过，no_hwnd=1/no_swapchain=1 | draw3-compatibility-dev-renderer-upload.err.log |
| --config-boundary-test | 退出0，隔离配置默认值/四组合/NoUpload/非法类型验证通过 | draw3-compatibility-dev-config-boundary.err.log |
| 两个新增测试入口带多余参数 | 各退出2，未进入产品初始化 | draw3-compatibility-dev-cli-results.json |
| 官方i18n sync/check | 通过，三语364键一致 | 实现代理检查记录 |
| 独立完整审查、diff/格式/XML | 通过，38个生产/测试/规范文件原BOM/纯CRLF；3工程XML有效 | product-transfer-review.md |
| task上下文验证 | 7条implement与7条check全部通过；大规格文件注入有截断提示，相关章节均显式读取 | task.py validate |

首轮build01实际编译产品与headless，发现根新增CLI缺两条直接module import（GraphicsInitialization/Renderer）；补齐后build02通过。未更换SDK/依赖/项目构建系统。MSBuild使用同一PowerShell中先保存Path、Remove Env:PATH、再仅写一份Path及MSBUILDDISABLENODEREUSE=1的已验证runner环境；不运行GUI。

Headless在build01生成，build02仅产品代码及依赖增量编译，未重链接headless；完整测试使用的就是移植到dev后的新二进制。未将原诊断分支的测试通过作为此轮证据。

## 未运行的验证

未运行主窗口、隐藏HWND harness、Computer Use、真实RTS/provider故障、实际窗口捕获、故障设备和兆芯硬件实测。WARP离屏通过不能替代兆芯驱动或真实RTS/触摸提升验收；ARM64通过不能外推Win32/x64/Win7。设备偏好设置页的真实布局也未GUI验证。

## 交接

本地代码和PR正文已准备，base=dev。尚未push/创建远端PR；用户本轮用语为准备开PR，仓库规则要求push单独明确授权。既有SSH agent通信失败也需在授权发布时留意。原diagnostic/performance分支保留，不自动merge。
