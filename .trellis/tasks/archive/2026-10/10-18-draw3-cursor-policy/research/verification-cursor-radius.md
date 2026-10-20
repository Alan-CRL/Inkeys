# 放大笔形圆角验证（2026-10-19）

用户反馈前次1.5 DIP不明显。本次把24 DIP笔形的四个非笔尖角目标半径加倍至3 DIP；实际只改PS的半径因子0.125→0.25、切线限制0.25→0.4及两条中文注释，再同步生成Win7兼容数组。正式PS反向还原后与本轮baseline逐字节相同，其余源SHA不变。

普通图标的肩部切线约0.831 DIP，小于2/5短边约3.298 DIP；尾角切线3 DIP，小于上限约3.394 DIP，因此四角实际均达到3 DIP。相邻圆角最多占4/5边长，仍留间隔；尖端继续跳过。荧光笔左右边缘、来源高亮、type21分支、热点/外框、配置和输入均沿用前轮合同。

## 本轮实际验证

- 独立Trellis check通过，无阻断问题，记录check-cursor-radius.md；仅2系数+2注释及同步字节得到独立确认。
- ARM64 FXC参考PS和完整guarded编译/反汇编退出0；ISGN/OSGN一致，DefaultInk原/还原尾部84B1C4CF3F07A25ACBB5B9C081A91B2269AA62C47B67A0AA83BFB049BD2096A5不变，正向门>21，25条derivative/discard最小条件深度1。
- 完整InkeysRepo.sln Debug|ARM64构建退出0，stderr 0 bytes。动态定位ARM64原生MSBuild，同一PowerShell调用规范化PATH并禁node reuse，/m:1 /nr:false、600秒上限。因前两轮已复现相同增量链接器内部错误，本轮事先核对绝对路径，仅将Build/ARM64/Debug/Inkeys.ilk移入本轮日志目录留存，以原配置直接完整链接；没有修改源码/项目或全局工具链。本轮没有触发LNK1000。既有第三方C4267与pwsh.exe查找提示仍保留，未扩大修改。
- 最终EXE仅以AS_DATAFILE|AS_IMAGE_RESOURCE数据映射核验，无入口执行。SHADER301–304逐字节等于当前四份项目CSO；guarded数组36380 bytes在EXE恰出现一次，SHA声明匹配，实际项目PS的ISGN/OSGN与兼容体一致。新PS和EXE身份均不同于本轮修改前，排除沿用旧资源。
- 原UTF-8 BOM/CRLF保持，diff --check、任务JSON/JSONL与文档检查。没有新增CPU镜像数学或测试入口。

本次只是shader参数变化，headless不包含该shader数学，因此没有重复上一轮已通过的headless、状态CLI、pen-width CLI或i18n生成。它们没有被写作本轮执行结果。未启动GUI/HWND/GPU harness或Computer Use，未commit/push。可见圆角效果和Win7 WARP仍待实际设备，不能由FXC/静态检查代替。

## 身份与日志

本轮本地ignored日志：Build/verification/cursor-radius-20261019/，含baseline/、baseline-hashes.json、previous-artifacts.json、Inkeys.prebuild-incremental.ilk、build.stdout.log/.stderr.log及shader-resources.json。FXC证据在Inkeys/Cache/CursorRadiusShader/generated/，细节见implementation-cursor-radius.md。

| 产物 | SHA256 |
| --- | --- |
| ARM64 Debug EXE | B35807E4DE79629FADE53F06AEA489D9A0B3EFED69CE5A26C97888FC8DFF326B |
| 正式PS源 | 321D2E9B75F12ABB68359BA4080759410066FDDA14AFB54002518CA970ECAF42 |
| 实际项目PS CSO，36148 bytes | C23556A1103308C123FD8937DA0B6E28639D1E302B3BE6C68C314BE7B62C4032 |
| /O3参考PS | BB33FF5079FD1E56769CDD63ECC8AD91F45185B4FB2FBE8CFFCC0DB57F90A7D4 |
| Win7 guarded，36380 bytes | F216A0BEAF1C552A91013174795DEA65DF3E6E4BC7EC8E1B43BA31DCC1B91094 |

项目实际与/O3参考编译选项不同，分别记录身份，不强制同SHA。本机ARM64 Debug产物不是Win7运行包；旧1.5 DIP字节及其验证只作历史。任务保持in_progress，等待原Win7硬件/视觉验收。
