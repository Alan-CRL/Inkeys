# 本轮追加调整契约（优先于原设计相关段落）

用户授权继续同任务三项调整；基线79e5fe0835b39c482cd7c87724c4a8ffa21e2ed8。保持in_progress，不commit/push/归档。上一轮人工确认仅是上一轮通过，本轮触摸修补另行验收。

1. 墨迹粗细调节器的“粗细”数字及相关候选读数仅显示安全四舍五入整数，不带DIP、不带≈。本轮仍采用已有最小显示1避免有效sub1显示0；整数只是展示，不回写实际DIP、不改变连续Draw3。三语言源模板与损坏格式fallback同步，走i18n sync/check。
2. 当前发布隐藏且暂不支持启动宽度记忆/旧启动默认设置。以注释禁用生产启动恢复入口及其旧启动偏好解析/应用，不让残留MemoryWidth/AutoDefaultWidth/旧DefaultBrush/Highlighter或新Startup在启动时覆盖共享策略Medium。不恢复记忆颜色等隐藏偏好、不误禁用Draw3文档/uink历史恢复。保留安全配置codec/旧值信息供将来兼容，不做配置清空或保存架构重构。启动仍在显示就绪后解析同一中档；显示变更与工具切换不得重新设默认。
3. 在墨迹预览视图下方直接触摸滑动调粗细时，预览/候选数字同帧消费真实gesture candidate（经过统一DIP->canvas转换），不等待权威宽度提交或缓动追赶。先调查具体手势入口和RenderLoop candidate/animation链路，修真实根因，不每帧写业务/配置。保留按钮/程序化动画、FineDial激活动画/锁定/取消/过期revision和软笔/激光视觉规则。

最小范围：Core只修改IdtConfiguration.cpp/.h及其中真实启动probe，必要Preferences头；UI独占Bar I18nFormat/Layout/Interaction/RenderLoop、三语言源与snapshot、animation_tests测试。主会话拥有任务文档、规范、工程集成与完整build。两个worker互不改对方文件，不单独完整构建、不递归派agent。

测试：既有policy/codec矩阵不变；新增真实生产启动fixture同时有残留旧启动/新Startup/memory时仍Medium（普通和大屏/laser）、后续初始化不覆盖手动状态；真实Bar触摸candidate发布/preview选择路径验证同帧、取消不写、提交revision语义保留；headless整数/半整数/sub1/fallback文案；Solution Debug|ARM64、headless --no-window、width/state/Draw3生产probes、离屏Bar、i18n/diff。只做无可见GUI验证。生产probe不把模拟helper等同真实触摸体验。

回退：只撤销此次增量，保留上一轮commit及已有41项现场，不改DIP单位策略/绘制引擎/橡皮/历史。独立trellis-check后更新规范；原PRD中恢复优先级和≈DIP显示仅为上一版本，按本轮用户明确调整替代。

## 已确认触摸根因与修补边界

实际触摸ProjectRelativePreviewWidth→ApplyCandidateWidth已即时发布candidate并请求渲染；旧SubmitTargetsAndLayout只对数字SetDirect，预览core/outer仍SetTar并由draw读取.val，形成缓动滞后。现共用candidate metrics与width animation seam，活动候选core/outer SetDirect；真实AdvanceAnimationsAndDeriveLayout也显式计入previewDragging MarkChanged，避免同整数读数时无脏区。非交互SetTar、morph/颜色与Laser hold保持。生产CPU测试覆盖真实session发布→metrics→立即val/取消/锁定/修订；完整frame/触摸消息实际运行仍为人工验证边界。
