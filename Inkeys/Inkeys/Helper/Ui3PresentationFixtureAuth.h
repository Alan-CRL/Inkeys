#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

namespace Inkeys::UI::Bar
{
	enum class Ui3FiniteScene : std::uint32_t;
	inline constexpr std::uint32_t Ui3FixtureMagic = 0x1430FB21;
	inline constexpr std::uint32_t Ui3FixturePurpose = 0x55493301;
	inline constexpr std::uint32_t Ui3FixtureSourceVersion = 1;
	inline constexpr std::uint64_t Ui3FixtureExpectedSteps = 216;
	inline constexpr std::size_t Ui3FixtureFixedStorageBudget = 4 * 1024 * 1024;
	inline constexpr std::size_t Ui3FixtureTotalStorageBudget = 64 * 1024 * 1024;

	// stage只是child发布的观察值；Sealed必须在所有真实owner join后发布。
	enum class Ui3FixtureStage : std::uint32_t
	{
		None, Authorized, Initializing, Ready, Running, Closing, Joined, Sealed
	};
	enum class Ui3FixtureResult : std::uint32_t { Pending, Passed, Failed };

	struct alignas(8) Ui3FixturePacketV1
	{
		std::uint32_t magic, version, bytes, purpose;
		std::uint32_t scene, capture, capacity, round;
		std::uint32_t sourceVersion, trajectoryCount, authorized, stage;
		std::uint32_t result, received, enqueued, consumed;
		std::uint64_t nonceLo, nonceHi, sourceHash, expectedSteps;
		std::int64_t startedTicks, finishedTicks;
		std::uint64_t completedSteps, unverifiedSteps;
	};
	struct alignas(8) Ui3FixtureFrozenInputV1
	{
		std::uint32_t magic, version, bytes, purpose;
		std::uint32_t scene, capture, capacity, round;
		std::uint32_t sourceVersion, trajectoryCount;
		std::uint64_t nonceLo, nonceHi, sourceHash, expectedSteps;
	};
	struct Ui3FixtureSourceDescriptorV1
	{
		std::uint32_t sourceVersion, trajectoryCount;
		std::uint64_t sourceHash, expectedSteps;
	};

	static_assert(std::is_trivial_v<Ui3FixturePacketV1> && std::is_standard_layout_v<Ui3FixturePacketV1>);
	static_assert(sizeof(Ui3FixturePacketV1) == 128 && alignof(Ui3FixturePacketV1) == 8);
	static_assert(offsetof(Ui3FixturePacketV1, magic) == 0);
	static_assert(offsetof(Ui3FixturePacketV1, sourceVersion) == 32);
	static_assert(offsetof(Ui3FixturePacketV1, authorized) == 40);
	static_assert(offsetof(Ui3FixturePacketV1, stage) == 44);
	static_assert(offsetof(Ui3FixturePacketV1, result) == 48);
	static_assert(offsetof(Ui3FixturePacketV1, received) == 52);
	static_assert(offsetof(Ui3FixturePacketV1, enqueued) == 56);
	static_assert(offsetof(Ui3FixturePacketV1, consumed) == 60);
	static_assert(offsetof(Ui3FixturePacketV1, nonceLo) == 64);
	static_assert(offsetof(Ui3FixturePacketV1, nonceHi) == 72);
	static_assert(offsetof(Ui3FixturePacketV1, sourceHash) == 80);
	static_assert(offsetof(Ui3FixturePacketV1, expectedSteps) == 88);
	static_assert(offsetof(Ui3FixturePacketV1, startedTicks) == 96);
	static_assert(offsetof(Ui3FixturePacketV1, finishedTicks) == 104);
	static_assert(offsetof(Ui3FixturePacketV1, completedSteps) == 112);
	static_assert(offsetof(Ui3FixturePacketV1, unverifiedSteps) == 120);
	static_assert(std::is_trivial_v<Ui3FixtureFrozenInputV1> && std::is_standard_layout_v<Ui3FixtureFrozenInputV1>);
	static_assert(sizeof(Ui3FixtureFrozenInputV1) == 72 && alignof(Ui3FixtureFrozenInputV1) == 8);
	static_assert(offsetof(Ui3FixtureFrozenInputV1, nonceLo) == 40);
	static_assert(sizeof(LONG) == sizeof(std::uint32_t));

	namespace Detail { struct Ui3OwnedDirectoryProof; }

	// 普通进程内capability，不是OS沙箱；私有路径、冻结输入和租约保活到所有owner真join。
	class Ui3FixtureAuthorization final
	{
	public:
		~Ui3FixtureAuthorization();
		Ui3FixtureAuthorization(const Ui3FixtureAuthorization&) = delete;
		Ui3FixtureAuthorization& operator=(const Ui3FixtureAuthorization&) = delete;
		Ui3FixtureAuthorization(Ui3FixtureAuthorization&&) = delete;
		Ui3FixtureAuthorization& operator=(Ui3FixtureAuthorization&&) = delete;
		[[nodiscard]] const Ui3FixtureFrozenInputV1& Input() const noexcept { return input_; }
		[[nodiscard]] const std::wstring& Repository() const noexcept { return repository_; }
		[[nodiscard]] const std::wstring& PrivateRoot() const noexcept { return privateRoot_; }
		[[nodiscard]] const std::wstring& BinaryDirectory() const noexcept { return binaryDirectory_; }
		// 仅写mutable数字输出；业务权限始终消费Input()，不得重新信任shared输入。
		[[nodiscard]] Ui3FixturePacketV1& Packet() const noexcept { return *packet_; }
	private:
		friend struct Ui3FixtureAuthorizer;
		Ui3FixtureAuthorization(Ui3FixtureFrozenInputV1 input, std::wstring repository,
			std::wstring privateRoot, Ui3FixturePacketV1* packet,
			std::unique_ptr<Detail::Ui3OwnedDirectoryProof> proof);
		const Ui3FixtureFrozenInputV1 input_;
		const std::wstring repository_, privateRoot_, binaryDirectory_;
		Ui3FixturePacketV1* const packet_;
		std::unique_ptr<Detail::Ui3OwnedDirectoryProof> proof_;
	};

	[[nodiscard]] bool IsAuthorizedUi3Fixture(const Ui3FixtureAuthorization&) noexcept;
	// Main必须最早调用；已识别、坏内部参数和授权child均直接返回，绝不进入普通初始化。
	bool TryRunUi3PresentationFixtureEarly(LPCWSTR fullCommandLine, int& exitCode) noexcept;

	// 后续Bar F的普通链接依赖；头放在module global fragment，不能写成功stub。
	// hash来自真实immutable 64B表，显式LE/FNV1a64编码表、版本/scene/count/steps/clock period。
	Ui3FixtureSourceDescriptorV1 GetCompiledUi3FixtureSourceV1(Ui3FiniteScene) noexcept;
	// 返回前必须真join source/Interact/Render/Window并发布Sealed；未能停止须保留capability到死亡。
	// bootstrap核实际finite/SVG/source全部fixed bytes<=4MiB，capture-off时间输出为null。
	int RunAuthorizedPresentationFixture(const Ui3FixtureAuthorization&) noexcept;
}
