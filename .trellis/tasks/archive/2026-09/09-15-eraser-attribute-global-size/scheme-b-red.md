# 方案B红灯记录

在af22b4a6产品逻辑上先加入方案B断言，然后运行完整ARM64 solution、headless及生产offscreen入口。
- 构建退出0：Build/eraser-b-red-build.log。
- headless退出1，新增12项失败：24/32/40、16/64按档位迁移、三个中心对齐、方案B顺序、完整自动按钮菜单锚点、紧凑两行、齿轮标题行、等圆边缘间隙、小圆有限命中。日志Build/eraser-b/red-headless.log。
- offscreen退出1，像素断言失败：stable preview uses opaque Contact white, not Hover alpha0.5。
- 原1728组精细量化与采样回归此时仍通过（worst sample/frame deviation=1.99186%）。
- 此后才修改预设/布局和生产组件。增加motion API后继续补充生产多帧与像素、反向、按钮归属检查；未把旧需求的70DIP槽位和左缘定位断言作为新规格保留。
