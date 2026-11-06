# 第二批Setting实现边界

当前只负责Setting图片恢复副本及本批CLI注册。普通显示继续持有同一批SRV，字体/ImGui/共享WARP epoch和窗口线程模型不变。

- Base保留仅用于运行时HICON的64×64原始像素（16KiB），PNG资源改为稳定资源名/请求解码尺寸/像素变换配方；不保存已解码BGRA。
- 将原有资源图片初始化收束为同一份资源清单与解码/像素转换入口，用于首次上传与epoch恢复，避免两条路径的资源名/缩放/alpha规则漂移。保留现有GDI+加载与逐通道整数算式，QR/GitHub继续白色alpha遮罩，演示atlas继续原尺寸直通道BGRA。
- 上传候选成功后才发布尺寸与SRV；已成功解码的轻量配方也允许可选12/13槽首次上传失败后在后续epoch重试。重建有来源的必需槽失败时释放半成品并保留配方，12/13失败仍降级；空来源沿用原skip规则。Shutdown真正归还raw缓存capacity并清配方。
- 新增RunTextureRecoveryProductionTest / --setting-texture-recovery-test：早于窗口与配置，真实WARP设备、全部11个活跃槽及3个未使用槽，CPU参考像素/GPU读回、两设备重建、必需失败/可选失败和清理。它不依赖字体测试或屏幕交互。
- 本代理只做静态和格式检查，不构建或运行测试，交由主会话统一执行。IdtMain新CLI注册由本代理独占，其他代理提供声明和flag。

静态检查：4个拥有文件均保持原UTF-8 BOM和CRLF，git diff --check通过。PNG像素只存在Decode/Load临时vector中；常驻像素数组仅slot0最多64×64×4=16KiB。新增CLI全部先识别argv，argc错误或harness关闭返回2，不落普通产品启动。未构建或运行，待主会话统一验证。
