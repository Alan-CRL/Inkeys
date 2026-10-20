# PPT 存储锁等待修复边界（2026-10-17）

生产 NamedMutexGuard::Acquire 使用 INFINITE，唯一串行 worker 的 Load 或 Save 可永远等待活着的锁拥有者。加载没有 completion 时 Controller 原有 IoError 重试无法进入。最小修复仅把同一命名锁的一次获取限制为 1000ms；失败仍返回既有 IoError，WAIT_ABANDONED 仍按已获取处理，未获取不能读取/修改索引或 UInk。

修改文件：Draw3.PresentationAutoSave.cpp（唯一等待点），presentation_autosave_tests.cpp（真实 worker、隔离 root 命名锁故障回归）。不改存储格式、自动重试预算、未读槽准入或 CloseAndDrain 的保存语义。每次锁等待有界不等于任意磁盘驱动 I/O 或大量队列整体退出有界。

先加入测试用原生产实现取得红结果，再修改等待点并重跑。测试由主线程持有隔离 root 锁，工作线程先 Save 后 Load；限定等待后先释放锁再 drain，即使原实现失败也可清理。断言持锁时返回两个 IoError、无加载快照、旧文件内容不变；释放后同一服务新 Load 读回旧墨迹。无窗口、无真实用户存储操作。
