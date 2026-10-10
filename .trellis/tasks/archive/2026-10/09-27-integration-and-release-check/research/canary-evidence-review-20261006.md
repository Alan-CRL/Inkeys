# 0811可见产品对照有限复审（2026-10-06）

Active task：`.trellis/tasks/09-27-integration-and-release-check`。已读保存hook、prd/design/implement、适用native合同、Oct5增量复审、Oct6验证/窄研究与原始JSON/CSV/inspect/BMP/日志。本checker只写本文件，不改源码或其它记录、不启动GUI/构建/测试、不重审历史/Win7，不commit/push/归档。

## 结论

**证据汇总及排除口径通过；UI3完整性能“不倒退”未达到。** 无本轮确认的新源码缺陷。真实程序已自动操作并正常退出，不能把等工作量未成立的累计CPU样本记PASS。保持暂停优化、既有300ms门控和原动画效果。

## 直接核对

- HEAD `8d15d05119b85777d8b0aabac4c99930cf438045`，chore/publish。实际重算Oct5冻结Release及current复制均为SHA256 `F81F790F7DABEA30A1F230F58E03B5DFB991710A57DFCA02E2CDB0F52D5092E9`；精确0811为`81A3DBB26A845308EA2AAFE7869844B389968E31F3798AEE2E0987F947A6D07E`；原helper为`15D7E048EB57E5DB81EC1FED76F958698E23101C304BAC58125F2515A1A22740`。冻结PE不代表后来Win7脏源码。
- 证据根`Build/automation-perf/canary-0811-resume-20261006/`。current三轮PID24896/HWND1248298、0811三轮PID2116/HWND6949890；350ms续测PID11688/HWND136430，未混窗口统计。inspect核current私有路径、WinSta0/Default/session1。当前UI3日志WARP、zoom2、lightFlags3/ULW实际提交；旧版本轮无独立后端字段，WARP/ULW沿用既有证据，不推定Draw3设备。

| 150ms探索群体 | 三轮产品累计CPU(ms) | 中位/范围(ms) | WGC交付/ROI变化 |
| --- | --- | --- | --- |
| 当前F81F | 2140.625 / 3125 / 2531.25 | 2531.25；2140.625–3125 | 46/13、46/13、46/13 |
| 精确0811 | 2343.75 / 3250 / 2546.875 | 2546.875；2343.75–3250 | 39/37、41/38、40/37 |

原始CPU差额/10000及六份输入CSV与gui-closeout-summary相符，每轮约22秒/七次注入ok=1/captureError0；两个群体includedInEqualWorkConclusion均false。150ms反向违反现版已有300ms同通道合并合同，旧版没有该入口；最后完整截图current仅Main、旧版完整Selection栏，非共同工作量。WGC不是Present/FPS/光学延迟，captureDroppedFrames=null；累计CPU含闲置，不能据此算动画P95。短期GDI/USER稳定也不是无泄漏证明。

- current-final-r1七次实际输入1004.64/3005.13/5001.92/5360.90/7017.86/9015.35/20011.50ms均ok=1，第3/4间隔358.98ms；CPU2906.25ms、51/13交付/ROI变化、captureError/resize/actionFailed0。前后同HWND0x214ee、rect951,1644,1149,1842（宽198收起），七次有效toggle应末态展开（另截图宽978），端点不合格。画笔Main图形在完整展开截图也相同，不能由圆/方或图标单独判断fold。注入时刻不等于业务接受时刻，不能确认门控缺陷或输入丢失。Root排除该单轮，未继续旧版配对/调参，符合预算；状态准备样本亦排除。
- 本轮三份日志严格UTF-8可读且无replacement字节，GB18030 strict失败；终端乱码不等于原文件损坏，不重编码。current的idt1791282040803.log于18:27:10.493、canary的idt1791282431056.log于18:33:45.385、current的idt1791283130732.log于18:44:30.889均有“反初始化 COM 环境完成”“已结束智绘教所有线程并关闭程序”，前行有等待各函数线程结束。Root另查询最终无Inkeys进程；未记录产品原生exitcode，不推定exit0。正常退出后捕获器exit2，以及群体外More被遮挡guard exit4/退出准备ROI尺寸变化exit2，不能误归产品崩溃。

## Findings (fixed)

无生产代码修补。Root已原位更正旧“成功点击/动作”为“输入注入成功、业务接受未逐条核实”；Oct6无配置变化限定生产配置，私有配置另列。Session48也明确“未达目标/性能验收未通过、集成任务仍进行中”。本checker仅记录，不修改其它文件。

## Findings (not fixed)

UI3完整不退化仍缺有效等工作量业务接受及成功呈现/光学证据，不计全部完成。旧Oct3硬笔5 CPU5078.125→1359.375ms只适用于该场景，含旧Draw2 WARP/新Draw3 Hardware-DComp架构差异，不是最终F81F帧率/书写延迟认证。Win7最终源码重建/目标机验收及正式元数据/CI标记/对应架构/签名上传留给独立任务/发布流程；原保留HTTP/认证政策与未测故障整链不因工程收口消失，不扩大修复。

## Verification / Diff

- Lint：有限记录git diff --check退出0；仅LF/CRLF属性提示，无whitespace错误。本新增报告严格UTF-8无BOM/CRLF、无尾随空白。既存文件无整文件格式diff，日志不改。
- TypeCheck/Build、Tests：本checker未新跑；复用Oct5独立复审已确认完整Release最终exit0、state/width/draw3-width/headless/PptCOM及资源/本地包。只读六组和350ms原始数据一致性检查PASS，性能目标未PASS。
- 本checker唯一新增本report；Root本轮仅验证/窄research/交接/journal记录，生产/helper/测试源码修改0。实际仓库IdtMain/Draw3隐藏测试/presenter/input spec、Win7脚本/记录/cso等属于独立线程或预存工作，完整保留，不误计本轮。集成task保持in_progress。
