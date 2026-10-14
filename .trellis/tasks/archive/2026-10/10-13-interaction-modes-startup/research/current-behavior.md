# 当前模式与启动行为

- IdtMain.cpp:2408 读取 SM_DIGITIZER；NID_READY 且具备 NID_INTEGRATED_TOUCH 或 NID_EXTERNAL_TOUCH 才算触摸设备。
- 无触摸设备 => PaintDevice=1（桌面）；有触摸且主屏物理面积 <1200 cm² => 1；有触摸且面积 >=1200 cm² => 0（大屏）；有触摸但主屏不存在/物理尺寸不可用 => 0。两个 0 分支还开启 liftStraighten。
- IdtMain.cpp:2519 后读取 opt/deploy.json；IdtConfiguration.cpp:289 存在整数 PaintDevice 时覆盖自动判定；555 保存原字段。不改数字语义和旧配置键。
- 1200 cm² 约为 16:9 显示器 20.8 英寸；这是物理面积而非分辨率、缩放、投影画面大小。
- Display.cpp:650 的不可用情况包括复制/混合拓扑、EDID 缺失/失败、映射不唯一、尺寸缺失等。
- 系统级触摸检测没有关联到主显示器，外接触摸设备配合非触摸主屏可能误判；不能承诺当前自动识别始终符合场景定义。
- Setting.cpp:1265 缓存选择，3789 建组合框，3801 修改 setlist，随后同步 Draw3 和写配置。Widgets.cpp:171-192 封装标准 ImGui combo；尚无证据证明具体故障点，实施前继续追踪。
- 三语文案入口：Inkeys/src/i18n/{zh-CN,zh-TW,en-US}.jsonc 的 SettingsUI.Draw.Effect.Device。
- i18n 由 Scripts/i18n.ps1 sync/check 管理；生成 IdtI18nKeys.g.h 不可手改。
- 初始 git status --ignore-submodules 无改动；普通 status 因外部 HiMsg submodule git 元数据权限失败。系统 python alias 不可运行，使用已有 bundled Python。
