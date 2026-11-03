#pragma once

#include "Setting.Design.h"
#include "imgui/imgui.h"
#include <cstdint>
#include <string>
#include <unordered_map>

struct ImGuiWindow;

namespace Inkeys::UI::Setting::Design
{
	struct ConditionalMotionState
	{
		NavigationTween reveal;
		std::uint64_t generation = 0;
		float height = 0.0F, occupied = 0.0F, predicted = 0.0F;
		ImGuiID parent = 0;
		int predictionFrame = -1;
		int frame = -1;
		std::unordered_map<ImGuiID, std::string> text;
	};

	// 只保存值和稳定 ID；实际 ImGui window 指针仅在本帧 scope 中借用。
	struct PageMotionState
	{
		std::uintptr_t context = 0;
		std::uint64_t route = 0, generation = 0, layoutRevision = 0;
		std::uintptr_t layoutAtlas = 0, layoutFont = 0;
		float layoutScale = 0.0F;
		float elapsed = 0.46F, lastBlockTop = -1.0e20F;
		int frame = -1, visibleOrder = 0, lastBlockOrder = 0;
		bool initialized = false, opening = false, hadBlocks = false, navigationFrame = false;
		bool pinnedBottom = false;
		ImGuiID owner = 0;
		ImVec2 viewport{};
		float contentHeight = 0.0F, predictedDelta = 0.0F;
		ImVec4 bounds{};
		std::unordered_map<ImGuiID, ConditionalMotionState> conditions;
		std::unordered_map<ImGuiID, float> blockProgress;
		std::unordered_map<ImGuiID, int> blockOccurrences;
	};

	void ConfigurePageMotion(PageMotionState& state, std::uint64_t routeKey, bool opening, bool reset = false, std::uint64_t layoutRevision = 0);
	void BeginPageMotion(PageMotionState& state);
	void EndPageMotion();
	void ResumeSkippedPageOpening();
	[[nodiscard]] float PageScrollbarAlpha(const PageMotionState& state);

	class PageBlockScope
	{
	public:
		explicit PageBlockScope(const char* id, bool section = false);
		PageBlockScope(PageBlockScope&& other) noexcept;
		PageBlockScope(const PageBlockScope&) = delete;
		PageBlockScope& operator=(const PageBlockScope&) = delete;
		~PageBlockScope();
		void Finish();
	private:
		PageMotionState* state_ = nullptr;
		ImGuiWindow* window_ = nullptr;
		ImVec2 origin_{};
		int firstVertex_ = 0, firstChild_ = 0;
		ImGuiID id_ = 0;
		bool section_ = false;
	};

	class ConditionalBlock
	{
	public:
		ConditionalBlock(const char* id, bool visible);
		ConditionalBlock(const ConditionalBlock&) = delete;
		ConditionalBlock& operator=(const ConditionalBlock&) = delete;
		~ConditionalBlock();
		explicit operator bool() const noexcept { return render_; }
		const char* RetainText(const char* key, const char* current);
	private:
		PageBlockScope block_;
		ConditionalMotionState* motion_ = nullptr;
		ImGuiWindow* window_ = nullptr;
		ImVec2 origin_{}, previousMaximum_{};
		int firstIndex_ = 0, firstChild_ = 0, firstVertex_ = 0;
		bool render_ = false, visible_ = false, disabled_ = false, accountHeight_ = false;
	};
}
