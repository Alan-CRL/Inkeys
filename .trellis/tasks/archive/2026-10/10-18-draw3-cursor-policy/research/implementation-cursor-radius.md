# 放大笔形圆角实施（2026-10-19）

以 cursor-geometry-revision.md 最新增量为准。本次只改正式PS的两个系数和对应两条中文注释：半径系数0.125→0.25（24 DIP图标从1.5→3 DIP），切线限制0.25→0.4（相邻短边1/4→2/5）。同步同源完整guarded数组；不改变其它公式、type21 branch、荧光笔边缘、设备策略、热点/外框或输入/配置。开始前已核六份baseline哈希相符。

普通24 DIP图标半尺寸12，目标半径3 DIP。两肩角的切线距离约0.831 DIP，小于短边√68的2/5（约3.298 DIP）；两尾角的切线距离3 DIP，小于尾边√72的2/5（约3.394 DIP）。因此四角均实际保留目标3 DIP，不被保护上限夹小。相邻两角切线之和最多占原边4/5，仍至少留1/5直边；尖端索引0依然跳过。

仅执行静态差异和允许的FXC参考PS/guarded编译反汇编核验；不执行solution、产品/测试EXE、GUI/GPU测试或Git写入。本轮不新增测试/字段/接口，也不重复上一轮完整几何推理。

## 生成与静态核验结果

- 正式PS反向替换这两常量/两注释后，与本轮baseline文本逐字符相同；没有其它公式或branch变化。正式PS和guarded头保留UTF-8 BOM/CRLF，git diff --ignore-submodules=all --check通过。
- 动态定位ARM64 SDK FXC（10.0.26100.0），参考PS/guarded均以ps_5_0 /E main /O3编译和反汇编退出0；原生成器不变，输出位于 Inkeys/Cache/CursorRadiusShader/generated/，包含完整guarded-source-evidence.json，旧baseline/前轮证据保留。
- 正式源SHA256：321D2E9B75F12ABB68359BA4080759410066FDDA14AFB54002518CA970ECAF42。
- 正式参考PS CSO SHA256：BB33FF5079FD1E56769CDD63ECC8AD91F45185B4FB2FBE8CFFCC0DB57F90A7D4。
- 新guarded：**36380 bytes**，SHA256 **F216A0BEAF1C552A91013174795DEA65DF3E6E4BC7EC8E1B43BA31DCC1B91094**；头文件数组重新提取核验长度/SHA一致。
- 正式参考与guarded ISGN=4493E3D3258BE71AEC723F26979FE3A407C4ECCFAE277B41BFD046DAC55B19E6，OSGN=F9355CCAD20A6284685169A043421A10B2BDC8395B02145240D94A39524F3128，签名保持；DefaultInk原/还原尾部SHA均为84B1C4CF3F07A25ACBB5B9C081A91B2269AA62C47B67A0AA83BFB049BD2096A5，gate仍>21；25条derivative/discard指令最小guard深度1，math_preserved/unknown_type_preserved为true。

源码与同步数组已冻结。未执行solution、产品/测试EXE、headless、GUI/GPU测试或Git写入；root负责本轮统一solution/最终资源核验。当前只确认常量落入源码/兼容字节及静态边界，真实可见效果与Win7目标设备仍待验收。
