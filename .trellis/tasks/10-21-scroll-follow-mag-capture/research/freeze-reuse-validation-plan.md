# 复用定格 Mag 的定向验证

> 历史提案，非当前实施范围：后续用户要求保持现有定格线程，只做原 Mag 的软件内 PrintWindow(2) 验证。此文中的 backend/reservation/通用销毁接线及回调尚不可用的初步判断均未作为当前实现合同；最新事实见 design.md 与 research/freeze-printwindow.md。

用户已选定 PrintWindow(2)，允许后续读到上一帧带来的延迟；不得将固定等待或重绘返回值描述为最新帧保证。只测试当前捕获层，尚不推定 Tracker 或产品穿透通过。

1. 生产共用 backend/reservation：捕获占用与 Freeze 请求串行，Freeze 到来先关发布再等实际读回结束；Stop 可打断首帧/稳态等待；旧会话不得重新发布；重复启动、取消与资源释放条件有明确断言。
2. 原生实路径：Window Service 在真实 STA owner 创建原定格 Host/WC_MAGNIFIER，使用实际共享 backend 更新/排除和 PrintWindow(2)。外部专用纹理图案持续变换，同进程蓝色覆盖窗必须真实被排除。没有额外 WGC 或禁止捕获属性。
3. 首帧等待较长且质量有效后才作为基准；黑帧/纯色/尺寸错误仍不能初始化。之后静止图像或上一帧可以接受；测试记录更新延迟，并用持续改变的已知内容证明图像最终更新，而非要求所有采样都严格匹配当前阶段。
4. 元数据与实际 child client、Source/WorkArea/Display generation 对齐，保留定格窗口避全屏少一像素的事实。分布采样点/纹理块验证映射，不能只看一个颜色点。
5. 先捕获→停止→原定格更新/显示→隐藏→再次捕获；Freeze 已显示时拒绝捕获；活动读取期间请求 Freeze 必须延后 Reveal，owner 仍能处理消息。
6. 正常关闭沿既有 coordinator stop/join→Window stop；动态 Destroy/异常清理前先取消发布，在操作真正结束前保留资源，覆盖 rollback。故障用例若仅阻塞回调或 backend，不声称注入了系统内部永久挂起。
7. 完整 solution Debug|ARM64、定向与 --no-window、diff/编码/CRLF/XML；编译只用现有 VS/SDK/依赖。真实 GUI 仅专用自动关闭窗口，未使用主产品窗口或用户文档；未经新的明确授权不延续上一轮 Computer Use 权限。

所有结果按实际退出码和像素记录；没有通过的门槛保持未完成。本轮不创建 commit 或 push。
