#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

import Inkeys.UI.UI3.VisualDirty;

using namespace Inkeys::UI::UI3;

namespace
{
	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cout << "[VisualDirty] FAIL: " << name << "\n";
	}

	bool SameRect(const RECT& a, const RECT& b)
	{
		return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
	}

	// 测试用视觉对象：字段即采样输入，模拟宿主每帧直接写值。
	struct FakeVisual
	{
		bool visible = true;
		RECT bounds{ 10, 10, 20, 20 };
		double opacity = 1.0;
		std::wstring_view text = L"A";
	};
	VisualSampler SamplerFor(FakeVisual& visual)
	{
		return [&visual](VisualSample& sample)
			{
				sample.visible = visual.visible;
				sample.bounds = visual.bounds;
				sample.state.Real(visual.opacity);
				sample.state.Text(visual.text);
			};
	}
	struct FakeGroup
	{
		bool visible = true;
		double opacity = 1.0;
	};
	VisualGroupSampler GroupSamplerFor(FakeGroup& group)
	{
		return [&group](VisualGroupSample& sample)
			{
				sample.visible = group.visible;
				sample.state.Real(group.opacity);
			};
	}

	void PrepareAndCommit(VisualDirtyRegistry& registry)
	{
		const auto attempt = registry.Prepare();
		Check(registry.Commit(attempt.serial), "baseline commit succeeds");
	}

	void TestStableStateHasNoDamage()
	{
		VisualDirtyRegistry registry;
		FakeVisual a;
		registry.Add({}, SamplerFor(a));
		const auto first = registry.Prepare();
		Check(SameRect(first.damage, a.bounds), "first appearance damages current bounds");
		registry.Commit(first.serial);
		const auto second = registry.Prepare();
		Check(!second.HasDamage() && second.changedCount == 0, "identical state produces no damage");
	}

	void TestDirectWriteAndMoveDamage()
	{
		VisualDirtyRegistry registry;
		FakeVisual a, b;
		b.bounds = { 100, 100, 110, 110 };
		registry.Add({}, SamplerFor(a));
		registry.Add({}, SamplerFor(b));
		PrepareAndCommit(registry);

		a.opacity = 0.5; // 直接写值，不经过任何动画回调
		auto attempt = registry.Prepare();
		Check(SameRect(attempt.damage, a.bounds) && attempt.changedCount == 1,
			"direct state write damages only that visual");
		registry.Commit(attempt.serial);

		a.bounds = { 30, 10, 40, 20 };
		attempt = registry.Prepare();
		Check(SameRect(attempt.damage, RECT{ 10, 10, 40, 20 }), "move damages old union new bounds");
		registry.Commit(attempt.serial);

		a.text = L"A"; // 写入相同值
		attempt = registry.Prepare();
		Check(!attempt.HasDamage(), "rewriting the same value produces no damage");
	}

	void TestAppearanceAndDisappearance()
	{
		VisualDirtyRegistry registry;
		FakeVisual a;
		a.visible = false;
		registry.Add({}, SamplerFor(a));
		auto attempt = registry.Prepare();
		Check(!attempt.HasDamage(), "invisible visual contributes nothing");
		registry.Commit(attempt.serial);

		a.visible = true;
		attempt = registry.Prepare();
		Check(SameRect(attempt.damage, a.bounds), "appearance damages new bounds");
		registry.Commit(attempt.serial);

		a.visible = false;
		attempt = registry.Prepare();
		Check(SameRect(attempt.damage, RECT{ 10, 10, 20, 20 }), "disappearance damages old bounds");
		registry.Commit(attempt.serial);

		a.bounds = { 50, 50, 60, 60 }; // 隐藏期间的变化不产生脏区
		attempt = registry.Prepare();
		Check(!attempt.HasDamage(), "changes while hidden produce no damage");
	}

	void TestFailedPresentKeepsComparingWithCommitted()
	{
		VisualDirtyRegistry registry;
		FakeVisual a;
		registry.Add({}, SamplerFor(a));
		PrepareAndCommit(registry);

		a.bounds = { 30, 10, 40, 20 };
		const auto failed = registry.Prepare(); // 呈现失败：不提交
		Check(SameRect(failed.damage, RECT{ 10, 10, 40, 20 }), "attempt damages old union new");
		const auto retry = registry.Prepare();
		Check(SameRect(retry.damage, RECT{ 10, 10, 40, 20 }),
			"retry after failure still includes the committed bounds");
		registry.Commit(retry.serial);
		Check(!registry.Prepare().HasDamage(), "after a successful retry the snapshot advances");
	}

	void TestDeferredAndStaleCommit()
	{
		VisualDirtyRegistry registry;
		FakeVisual a;
		registry.Add({}, SamplerFor(a));
		PrepareAndCommit(registry);

		a.bounds = { 30, 10, 40, 20 };
		const auto first = registry.Prepare();
		a.bounds = { 50, 10, 60, 20 };
		const auto second = registry.Prepare();
		Check(SameRect(second.damage, RECT{ 10, 10, 60, 20 }),
			"unconfirmed attempts keep comparing with the committed snapshot");
		// 回执按顺序晚到：先确认第一个候选，第二个仍可确认。
		Check(registry.Commit(first.serial), "older candidate commits when its receipt arrives");
		const auto third = registry.Prepare();
		Check(SameRect(third.damage, RECT{ 30, 10, 60, 20 }),
			"after the older commit the snapshot is the older candidate");
		Check(registry.Commit(second.serial), "newer candidate commits after the older one");
		Check(!registry.Commit(first.serial), "stale receipt is ignored");
		Check(!registry.Commit(second.serial), "duplicate receipt is ignored");
		Check(SameRect(registry.Prepare().damage, RECT{}), "state equals the committed newer candidate");
	}

	// 等待回执期间：同一包络内的新变化也必须报告「相对上一次准备有变化」，宿主据此作废旧候选。
	void TestChangedSincePrevious()
	{
		VisualDirtyRegistry registry;
		FakeVisual a;
		FakeGroup g;
		const auto group = registry.AddGroup(GroupSamplerFor(g));
		const auto id = registry.Add(group, SamplerFor(a));
		const auto baseline = registry.Prepare();
		Check(baseline.changedSincePrevious, "first prepare reports a change against the empty snapshot");
		Check(registry.Commit(baseline.serial), "baseline commits");
		Check(!registry.Prepare().changedSincePrevious, "unchanged state reports no change");
		a.opacity = 0.5;
		const auto hover = registry.Prepare();
		Check(hover.changedSincePrevious && SameRect(hover.damage, a.bounds), "first change is reported");
		const auto repeated = registry.Prepare();
		Check(!repeated.changedSincePrevious && repeated.serial == hover.serial,
			"repeated prepare of the same pending state reports no change and reuses the pending serial");
		for (int i = 0; i < 8; ++i) (void)registry.Prepare();
		Check(registry.Commit(hover.serial), "idle prepares do not evict the pending candidate");
		a.opacity = 0.25;
		const auto press = registry.Prepare();
		Check(press.changedSincePrevious && SameRect(press.damage, hover.damage),
			"a new change inside the same damage is still reported");
		g.opacity = 0.5;
		Check(registry.Prepare().changedSincePrevious, "group state change is reported");
		Check(registry.Commit(press.serial), "older pending candidate commits");
		Check(!registry.Prepare().changedSincePrevious, "state equal to the latest pending attempt reports no change");
		registry.Remove(id);
		Check(registry.Prepare().changedSincePrevious, "removal tombstone is reported");
	}

	void TestGroupPropagation()
	{
		VisualDirtyRegistry registry;
		FakeGroup outer, inner;
		const auto outerId = registry.AddGroup(GroupSamplerFor(outer));
		const auto innerId = registry.AddGroup(GroupSamplerFor(inner), outerId);
		FakeVisual a, b, c;
		b.bounds = { 30, 10, 40, 20 };
		c.bounds = { 100, 100, 110, 110 };
		registry.Add(innerId, SamplerFor(a));
		registry.Add(innerId, SamplerFor(b));
		registry.Add({}, SamplerFor(c));
		PrepareAndCommit(registry);

		a.opacity = 0.3;
		auto attempt = registry.Prepare();
		Check(SameRect(attempt.damage, RECT{ 10, 10, 20, 20 }), "member change does not spread to siblings");
		registry.Commit(attempt.serial);

		outer.opacity = 0.5;
		attempt = registry.Prepare();
		Check(SameRect(attempt.damage, RECT{ 10, 10, 40, 20 }) && attempt.changedCount == 2,
			"ancestor group change marks every nested member");
		registry.Commit(attempt.serial);

		inner.visible = false;
		attempt = registry.Prepare();
		Check(SameRect(attempt.damage, RECT{ 10, 10, 40, 20 }), "hiding a group erases its members");
		registry.Commit(attempt.serial);
		Check(!registry.Prepare().HasDamage(), "hidden group stays quiet");
	}

	void TestRemovalTombstone()
	{
		VisualDirtyRegistry registry;
		FakeVisual a;
		const auto id = registry.Add({}, SamplerFor(a));
		PrepareAndCommit(registry);

		registry.Remove(id);
		Check(!registry.IsLive(id), "removed id is no longer live");
		const auto failed = registry.Prepare();
		Check(SameRect(failed.damage, RECT{ 10, 10, 20, 20 }), "removal damages the presented bounds");
		const auto retry = registry.Prepare();
		Check(SameRect(retry.damage, RECT{ 10, 10, 20, 20 }), "tombstone survives a failed present");
		registry.Commit(retry.serial);
		Check(!registry.Prepare().HasDamage(), "tombstone clears after a successful present");

		FakeVisual b;
		const auto reused = registry.Add({}, SamplerFor(b));
		Check(reused.index == id.index && reused.generation != id.generation && !registry.IsLive(id),
			"reused slot gets a new generation and the old id stays dead");
	}

	void TestTranslateCommitted()
	{
		VisualDirtyRegistry registry;
		FakeVisual a;
		registry.Add({}, SamplerFor(a));
		PrepareAndCommit(registry);

		a.bounds = { 15, 13, 25, 23 };
		registry.TranslateCommitted({ 5, 3 });
		Check(!registry.Prepare().HasDamage(), "rebased snapshot matches translated pixels");
	}

	void TestPurityCheckAndVolatileBytes()
	{
		VisualDirtyRegistry registry;
		FakeGroup group;
		const auto groupId = registry.AddGroup(GroupSamplerFor(group));
		FakeVisual a;
		double advancing = 1.0; // 模拟绘制中按设计推进的字号修正
		registry.Add(groupId, [&](VisualSample& sample)
			{
				sample.visible = a.visible;
				sample.bounds = a.bounds;
				sample.state.Real(a.opacity);
				sample.state.BeginVolatile();
				sample.state.Real(advancing);
				sample.state.EndVolatile();
			});
		auto attempt = registry.Prepare();
		Check(registry.VerifyPurity().Pure(), "untouched draw is pure");
		advancing = 1.5;
		Check(registry.VerifyPurity().Pure(), "volatile bytes advancing during draw stay pure");
		registry.Commit(attempt.serial);
		Check(registry.Prepare().HasDamage(), "volatile bytes still count for normal comparison");

		a.opacity = 0.2; // 绘制阶段改写稳定输入
		const auto dirty = registry.VerifyPurity();
		Check(dirty.mismatched.size() == 1, "draw-time write to a stable input is reported");
		a.opacity = 1.0;
		registry.Prepare();
		group.opacity = 0.4;
		Check(registry.VerifyPurity().mismatchedGroups.size() == 1, "draw-time write to group state is reported");
	}

	void TestReviewRegressions()
	{
		// 多层组注销：根、子、孙组与其成员一起注销。
		{
			VisualDirtyRegistry registry;
			FakeGroup g;
			const auto root = registry.AddGroup(GroupSamplerFor(g));
			const auto child = registry.AddGroup(GroupSamplerFor(g), root);
			const auto grandchild = registry.AddGroup(GroupSamplerFor(g), child);
			FakeVisual a;
			const auto member = registry.Add(grandchild, SamplerFor(a));
			PrepareAndCommit(registry);
			registry.RemoveGroup(root);
			Check(!registry.IsLive(child) && !registry.IsLive(grandchild) && !registry.IsLive(member),
				"removing a root group removes the whole subtree");
			Check(SameRect(registry.Prepare().damage, a.bounds), "removed subtree members are erased");
		}
		// 已送出候选在注销后才确认：其包络也要擦除。
		{
			VisualDirtyRegistry registry;
			FakeVisual a;
			const auto id = registry.Add({}, SamplerFor(a));
			PrepareAndCommit(registry);
			a.bounds = { 30, 10, 40, 20 };
			const auto sent = registry.Prepare();
			registry.Remove(id);
			registry.Commit(sent.serial);
			Check(SameRect(registry.Prepare().damage, RECT{ 10, 10, 40, 20 }),
				"removal after a sent candidate erases both the committed and the sent bounds");
		}
		// 候选被淘汰后才确认：作废快照，按全部重新出现计算。
		{
			VisualDirtyRegistry registry;
			FakeVisual a;
			registry.Add({}, SamplerFor(a));
			PrepareAndCommit(registry);
			a.opacity = 0.5;
			const auto first = registry.Prepare();
			// 每次准备都有真实变化才会占用新候选槽（与上一候选相同的准备复用其序号）。
			for (int i = 0; i < 5; ++i) { a.opacity = 0.4 - 0.05 * i; registry.Prepare(); }
			a.opacity = 1.0;
			Check(!registry.Commit(first.serial), "evicted candidate cannot commit");
			Check(SameRect(registry.Prepare().damage, a.bounds), "evicted receipt invalidates the snapshot instead of skipping damage");
		}
		// 被淘汰的候选已上屏且对象随后移走：迟到确认后，屏幕上可能残留的旧位置也必须擦除。
		{
			VisualDirtyRegistry registry;
			FakeVisual a;
			registry.Add({}, SamplerFor(a));
			PrepareAndCommit(registry);
			// 被确认的（已淘汰）候选独占纵向极值：漏掉它时 bottom 达不到 90。
			a.bounds = { 30, 80, 40, 90 };
			const auto sent = registry.Prepare();
			for (int i = 0; i < 5; ++i) { a.bounds = { 50 + 10 * i, 10, 60 + 10 * i, 20 }; registry.Prepare(); }
			a.bounds = { 200, 10, 210, 20 };
			Check(!registry.Commit(sent.serial), "evicted moved candidate cannot commit");
			const auto damage = registry.Prepare().damage;
			Check(damage.left <= 10 && damage.right >= 210 && damage.top <= 10 && damage.bottom >= 90,
				"late evicted receipt erases committed, sent, evicted and current bounds");
			Check(registry.Commit(registry.Prepare().serial) && !registry.Prepare().HasDamage(),
				"stale tombstones clear after the next successful present");
		}
		// 首段即易变：掩码正确建立，纯度检查不误报。
		{
			VisualDirtyRegistry registry;
			double advancing = 1.0;
			registry.Add({}, [&](VisualSample& sample)
				{
					sample.visible = true; sample.bounds = { 0, 0, 5, 5 };
					sample.state.BeginVolatile(); sample.state.Real(advancing); sample.state.EndVolatile();
				});
			registry.Prepare();
			advancing = 2.0;
			Check(registry.VerifyPurity().Pure(), "leading volatile bytes are masked");
		}
	}

	void TestNormalizationAndInvalidate()
	{
		VisualDirtyRegistry registry;
		FakeVisual a;
		a.opacity = 0.0;
		registry.Add({}, SamplerFor(a));
		PrepareAndCommit(registry);
		a.opacity = -0.0;
		Check(!registry.Prepare().HasDamage(), "-0 equals 0");

		a.opacity = std::numeric_limits<double>::quiet_NaN();
		auto attempt = registry.Prepare();
		registry.Commit(attempt.serial);
		a.opacity = -std::numeric_limits<double>::quiet_NaN();
		Check(!registry.Prepare().HasDamage(), "all NaN values compare equal");

		registry.InvalidateCommitted();
		Check(SameRect(registry.Prepare().damage, a.bounds), "invalidated snapshot treats visuals as new");
	}
}

int RunVisualDirtyTests()
{
	failureCount = 0;
	TestStableStateHasNoDamage();
	TestDirectWriteAndMoveDamage();
	TestAppearanceAndDisappearance();
	TestFailedPresentKeepsComparingWithCommitted();
	TestDeferredAndStaleCommit();
	TestChangedSincePrevious();
	TestGroupPropagation();
	TestRemovalTombstone();
	TestTranslateCommitted();
	TestPurityCheckAndVolatileBytes();
	TestReviewRegressions();
	TestNormalizationAndInvalidate();
	std::cout << "[VisualDirty] failures=" << failureCount << "\n";
	return failureCount;
}
