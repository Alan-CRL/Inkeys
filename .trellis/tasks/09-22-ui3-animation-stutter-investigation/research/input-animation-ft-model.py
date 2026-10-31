"""只模拟已读到的 UI3 颜色块关闭公式；不运行产品、GUI 或 D2D。

对应源码（当前工作区；父会话给定 HEAD 94e07b25）：
- Bar.RenderLoop.cpp:3126/3131, 4196：每帧两个不同的 ft 目标。
- Bar.RenderLoop.cpp:1325-1341, 3015-3064, 4250：同步批次剩余时间。
- Bar.Animation.cppm:419-433, 784-829：Back 续段与 SetTar 重启。
- Bar.Animation.cpp:119-178：实际数值推进公式。
- Bar.RenderLoop.cpp:7779：属性推进完成后才推进父时间轴。

运行：python input-animation-ft-model.py
输出写到同一 research 目录中的 input-animation-ft-trace.csv。
"""

import csv
from pathlib import Path


def clamp(value, lower=0.0, upper=1.0):
    return min(upper, max(lower, value))


def ease_in_back(progress):
    return (1.1 + 1.0) * progress * progress * progress - 1.1 * progress * progress


def repeated_ft_step(value, parent_progress, dt, duration, compact_scale):
    # SetTar(1) 后 SetTar(compact) 都从同一 val 重建：startV=val, progress=0。
    # 已稳定的隐藏形态在第二次 SetTar 后仍 val==tar，生产端 IsSame 跳过推进。
    if value == compact_scale:
        return value
    remaining = duration * (1.0 - parent_progress)
    progress = clamp(dt / remaining)
    # ApplyAnimationCurve + BarUiApplyCurveRange 对 Back 使用归一化剩余段。
    absolute_progress = parent_progress + (1.0 - parent_progress) * progress
    local_progress = (
        1.0
        if 1.0 - parent_progress <= 0.000001
        else (absolute_progress - parent_progress) / (1.0 - parent_progress)
    )
    next_value = value + (compact_scale - value) * ease_in_back(local_progress)
    return compact_scale if progress >= 1.0 else next_value


def main():
    duration = 0.4
    expanded_width = 370.0
    compact_width = 60.0
    compact_scale = compact_width / expanded_width
    dt = 1.0 / 60.0
    parent_progress = 0.0
    repeated_ft = 1.0
    rows = []
    for frame in range(1, 26):
        if parent_progress < 1.0:
            repeated_ft = repeated_ft_step(
                repeated_ft, parent_progress, dt, duration, compact_scale
            )
        parent_progress = clamp(parent_progress + dt / duration)
        curve = ease_in_back(parent_progress)
        panel_width = expanded_width + (compact_width - expanded_width) * curve
        panel_scale = panel_width / expanded_width
        single_ft = 1.0 + (compact_scale - 1.0) * curve
        if parent_progress >= 1.0:
            panel_scale = compact_scale
            single_ft = compact_scale
        rows.append(
            {
                "frame": frame,
                "time_s": frame * dt,
                "parent_progress": parent_progress,
                "panel_scale": panel_scale,
                "ft_repeated": repeated_ft,
                "ft_single": single_ft,
                "normalized_ft_repeated": repeated_ft / panel_scale,
            }
        )
    assert rows[22]["normalized_ft_repeated"] > 4.0
    assert repeated_ft == compact_scale
    assert all(abs(row["ft_single"] / row["panel_scale"] - 1.0) < 1e-12 for row in rows)
    output_path = Path(__file__).with_name("input-animation-ft-trace.csv")
    with output_path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"Wrote {len(rows)} formula-model frames: {output_path}")
    print("Maximum normalized ft:", max(row["normalized_ft_repeated"] for row in rows))
    print("Ordinary close settles exactly:", repeated_ft == compact_scale)


if __name__ == "__main__":
    main()
