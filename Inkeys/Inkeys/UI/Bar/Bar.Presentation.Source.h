#pragma once

#include "Bar.PresentationProbe.h"
#include "../../Helper/Ui3PresentationFixtureAuth.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Inkeys::UI::Bar
{
	inline constexpr UINT Ui3FixtureIndexMessage = WM_APP + 0x4B0;
	inline constexpr std::uint32_t Ui3FixtureSetupStep = 0x10000;
	inline constexpr std::uint32_t Ui3FixtureWarmSteps = 16;
	inline constexpr std::size_t Ui3FixtureMaxInputs = 435;
	inline constexpr std::size_t Ui3FixturePixelPayloadLimit = 32 * 1024 * 1024;
	enum class Ui3FixtureInputPhase : std::uint32_t { Down = 1, Up = 3, Cancel = 4 };
	enum class Ui3FixtureRunPhase : std::uint32_t { Setup = 1, Warmup, Measurement, Teardown };
	enum class Ui3FixtureAnchor : std::uint32_t { MainGrip = 1, DrawButton = 2 };
	enum class Ui3FixturePointResult : std::uint32_t { NotInstalled, Available, Unavailable };
	enum class Ui3FixtureActionPoint : std::uint32_t { Down, Commit, Callback };
	enum class Ui3FixtureSourceFailure : std::uint32_t
	{
		None, Authorization, Index, Order, Ready, Point, Post, Enqueue, Wire,
		UnexpectedSource, Dropped, Action, UnsupportedState, Deadline, Target, Control, Output,
	};
	inline constexpr std::uint32_t Ui3FixtureSetupOnlyIfFolded = 1u << 3;

	struct alignas(8) Ui3FixtureInputV1
	{
		std::uint32_t index, stepId, phase, anchor;
		std::int32_t offsetXDip, offsetYDip;
		std::uint32_t flags, reserved0;
		std::int64_t dueOffsetTicks;
		std::uint64_t sourceSequence, expectedBaseCommitSerial, reserved1;
	};
	static_assert(std::is_trivial_v<Ui3FixtureInputV1> && std::is_standard_layout_v<Ui3FixtureInputV1>);
	static_assert(sizeof(Ui3FixtureInputV1) == 64 && alignof(Ui3FixtureInputV1) == 8);
	static_assert(offsetof(Ui3FixtureInputV1, dueOffsetTicks) == 32);
	static_assert(offsetof(Ui3FixtureInputV1, sourceSequence) == 40);
	static_assert(offsetof(Ui3FixtureInputV1, expectedBaseCommitSerial) == 48);
	static_assert(offsetof(Ui3FixtureInputV1, reserved1) == 56);

	struct Ui3FixtureBoundPoint
	{
		std::uint64_t committedCount = 0, attempt = 0, epoch = 0, surface = 0, mapping = 0;
		std::int16_t x = 0, y = 0;
	};
	// ExMessage 的原始数字指纹；不在普通头重声明 named-module 消息类型。
	struct Ui3FixtureWireValue
	{
		std::uintptr_t hwnd = 0;
		std::uint32_t message = 0, buttons = 0, modifiers = 0, category = 0;
		std::int16_t x = 0, y = 0, wheel = 0;
	};
	struct Ui3FixturePixelReceipt
	{
		std::uint64_t generation = 0, committedAttempt = 0, epoch = 0, surface = 0;
		std::uint64_t bufferMutationSerial = 0, targetInvalidationSerial = 0, pixelBytes = 0;
		std::uint32_t width = 0, height = 0, stride = 0, status = 0, presentationAlpha = 0;
		std::int32_t sourceX = 0, sourceY = 0;
	};

	[[nodiscard]] std::span<const Ui3FixtureInputV1> CompiledUi3FixtureInputs(Ui3FiniteScene) noexcept;
	[[nodiscard]] std::size_t CompiledUi3FixtureStorageBytes() noexcept;
	[[nodiscard]] std::uint64_t HashUi3FixtureInputs(Ui3FiniteScene, std::span<const Ui3FixtureInputV1>) noexcept;
	[[nodiscard]] bool IsUi3FixtureInputValid(Ui3FiniteScene, const Ui3FixtureInputV1&, std::size_t index) noexcept;
	[[nodiscard]] bool IsUi3FixtureActionAllowed(const Ui3FixtureInputV1&, Ui3FiniteScene, Ui3FixtureActionPoint) noexcept;
	[[nodiscard]] bool TryBindUi3FixturePoint(const Ui3FixtureInputV1&, const Ui3FixtureReadyValue&,
		std::uint64_t generation, Ui3FixtureBoundPoint&) noexcept;
	[[nodiscard]] bool SameUi3FixtureWire(const Ui3FixtureWireValue&, const Ui3FixtureWireValue&) noexcept;

	// 以下门仅由同一受鉴权 runner 安装；默认产品立即沿原路径返回。
	[[nodiscard]] bool AuthorizedUi3FixtureSourceInstalled() noexcept;
	[[nodiscard]] bool AuthorizedFixtureEquivalenceReady(const Ui3FixtureAuthorization&) noexcept;
	void EnterAuthorizedFixtureInteraction() noexcept;
	void LeaveAuthorizedFixtureInteraction() noexcept;
	[[nodiscard]] bool ReceiveAuthorizedFixtureIndex(HWND, WPARAM index, LPARAM, Ui3FixtureWireValue&) noexcept;
	void FinishAuthorizedFixtureEnqueue(std::uint32_t index, bool succeeded) noexcept;
	[[nodiscard]] bool ObserveAuthorizedFixtureDequeue(const Ui3FixtureWireValue&) noexcept;
	void ObserveAuthorizedFixtureClear(BYTE filter, std::size_t removed) noexcept;
	void RejectAuthorizedFixtureSource(Ui3FixtureSourceFailure) noexcept;
	[[nodiscard]] Ui3FixturePointResult ReadFixturePointerForCurrentOwner(POINT&, bool& leftDown) noexcept;
	[[nodiscard]] bool PermitAuthorizedFixtureAction(Ui3FiniteScene, Ui3FixtureActionPoint) noexcept;
	[[nodiscard]] bool PermitAuthorizedFixturePointerStages(const Ui3FiniteSignature&) noexcept;
	[[nodiscard]] bool AuthorizedFixtureWindowMessageMustBeHandled(HWND, UINT, WPARAM, LPARAM, LRESULT&) noexcept;

	// Root 提供真实 DPI 与 owner BGRA 转发，不能用桩冒充完成。
	[[nodiscard]] bool EnsureAuthorizedUi3FixtureDpiAwareness(const Ui3FixtureAuthorization&) noexcept;
	[[nodiscard]] bool CaptureAuthorizedFixtureFinalBgra(const Ui3FixtureAuthorization&,
		const Ui3FiniteTargetRecord& measuredEnd, std::span<std::uint8_t>, Ui3FixturePixelReceipt&) noexcept;
}
