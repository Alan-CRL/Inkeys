# Touch释放修复：真机交接

新版本：`D:/Project/Inkeys/Repo/Inkeys/Build/ARM64/Debug/PanTouchReleaseValidation/Inkeys.exe`。先退出旧版，再运行本目录版本，勿混用PanTouchTerminalValidation。已复制上一测试目录的main.json和opt/deploy.json，原配置未改、白板文档未复制。

## 已完成

- 历史六版真实ARM64核回放确认短拖硬零由较早稳定化资格引入；旧版同时有过大/错向，未整套回滚。
- Touch集中释放决策：充分速度、可靠短证据、只续合法当前残余、距离有界降级、停止。raw新鲜末平台不再机械折半，明确停住约70ms仍退出。
- 极短且无可靠时间的数据可按已确认实际位移保守续滑；这不是测得手速，尾距有意较短。同帧尚无Apply确认等缺证仍可Stop。
- Pen/PenTail/Indirect与默认开关保持；RTS只增加实际属性/原始值日志，未改输入协商或正式时钟来源。尚未证实系统API故障。

## 请复测

1. 单指：正常快甩、很短的快划、连续同向与反向接手。关注是否仍突然硬零、错向或无故暴增。
2. 双指/多指：一起拖动后先抬一指再全部抬起；另试一指移动、另一指保持。观察退出交接和增减指是否跳变。
3. 快滑后刻意慢拖一点再松手、轻点抓停、明确停住约0.1秒再抬手：不能把旧高速重新加回。
4. 用笔做几次原来正常的拖动，再重复Touch，确认兼容。笔尾压力/1.5mm起步门槛本轮未改，按住与尾滑问题分开记。

请一次保存一组短动作并标注单/多指、是否主动停住。异常日志保留input Down/Move/Up、touch-decision、release-quality、release-profile和随后帧；启动处rts-clock-capability/rts-packet-property也请保留。新日志可说明选择了measured/short-supported/residual-only/distance-bounded/stop，以及实际预算/候选来源。

短平台与真实短暂停手不能仅凭当前坐标完美区分；本轮不在仍held时自动滑行，也不倒回动画补算Up前未交付位置。低置信短划与正常快甩的尾距可能不同，需你确认这项保守取舍是否合适。

## 验证边界

完整ARM64 Solution和最终五项无窗口专项通过。完整无窗口套件在green4通过（444.156s）；之后两项Touch保护和一项probe采样条件以最终完整构建、pan/control/feel/pen-width/diagnostic专项复验，复用未变模块的全套结果，未冒称最终字节再次跑完整WARP/设置套件。详情见validation.md与机器证据。

任务继续in_progress，未commit/push/归档。
