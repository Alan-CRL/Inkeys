# 放大圆角：独立增量审查（2026-10-19）

按cursor-geometry-revision.md最新末节和PRD AC14，在owner最终freeze后只读审查。仅写本文，未修改源码、构建/执行EXE、启动GUI/GPU或进行Git写入；前轮结果不作为本轮通过证据。

- 对本轮baseline逐字节反向还原，正式PS确实仅两系数和两注释变化：半径0.125→0.25、切线cap 0.25→0.4。其余圆心/扇区/退化保护、尖端索引0跳过、type21 branch、荧光笔内轮廓和alpha均原样保持。baseline列出的PenCursor/WindowControl/RendererPrimitives/VS四份hash未变。
- 普通24 DIP半尺寸12，目标r=3。肩角切线约0.831，小于短边sqrt(68)的2/5≈3.298；尾角切线3，小于sqrt(72)的2/5≈3.394。四角实际都为3 DIP；若只改半径却沿用旧1/4上限，尾角会被夹成约2.121，本次同步放宽已避免该情况。相邻角切线总占边长至多4/5，仍留至少1/5；尖端继续不圆化。此为代数检查，不是镜像像素测试。
- 最终guarded独立提取36380 bytes，SHA256 F216A0BEAF1C552A91013174795DEA65DF3E6E4BC7EC8E1B43BA31DCC1B91094，与当前CSO/header/证据一致，未沿用前轮F61B身份。
- 正式源SHA256 321D2E9B75F12ABB68359BA4080759410066FDDA14AFB54002518CA970ECAF42；参考PS CSO SHA256 BB33FF5079FD1E56769CDD63ECC8AD91F45185B4FB2FBE8CFFCC0DB57F90A7D4。正式/guarded ISGN、OSGN、RDEF独立解析一致；DefaultInk原/还原尾部仍84B1C4CF3F07A25ACBB5B9C081A91B2269AA62C47B67A0AA83BFB049BD2096A5，gate仍>21。实际ASM的25条derivative/discard均有条件guard，分支平衡。
- 两变动文件保留原UTF-8/BOM/纯CRLF；git diff --ignore-submodules=all --check退出0。

未发现阻断问题，源码继续冻结。FXC由owner记录；root的本轮完整solution/最终PE资源核验另记，不预先宣布成功。本轮纯参数未执行headless/CLI/i18n；真实圆角可见程度和Win7实际像素仍待目标设备验收，新字节身份不能继承旧硬件通过。
