# Touch末端平台新版本：手动复测

自动验证已通过：完整ARM64 Debug Solution、五专项、完整无窗口套件和独立检查；这不替代本机手感验收。

## 使用版本

先退出旧版，再运行 `D:/Project/Inkeys/Repo/Inkeys/Build/ARM64/Debug/PanTouchTerminalValidation/Inkeys.exe`。本轮是新Touch版本，勿混用上一版PanOverspeedValidation。

已将上一版用户测试目录的main.json和opt/deploy.json复制到本目录，原配置未修改、白板文档未复制。保留原交互模式和控制台诊断偏好，不改变笔尾惯性开关。最终文件SHA见validation.md/机器证据。

## 请分组测试

1. 笔远离，仅单指在白板移动模式做连续快甩；先屏幕中部，再靠近边缘。关注是否仍有正常动作几乎不滑，或突然异常加速。
2. 慢拖轻放、快滑后短暂停顿再抬手：短停允许续滑；检查尾距是否自然一致。
3. 快滑后明确停住约0.1秒，再抬手：应无明显惯性。当前策略是最后空间进展后≤30ms全速、30–70ms衰减、≥70ms归零，不承诺40ms短停立即刹车。
4. 用笔做几次原来正常的拖动，再重复同样Touch动作；确认笔仍正常，并形成更可比的使用前后对照。本轮没改笔尾接触/压力/1.5mm起步门槛，笔尾反馈单独记。

异常时一次只留一组短动作并标设备/是否有意停住，保留Down→Move→Up、release/release-quality/release-profile和后续帧。新release-quality含raw-points/motion-points、raw-last-qpc/motion-last-qpc及age-limited，可区分重复上报与运动时效。请勿只截末句generic stop或touchKnown缓存。

任务保持进行中，本轮未提交或归档。
