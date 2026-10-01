#include "Bar.PresentationProbe.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace Inkeys::UI::Bar
{
	namespace
	{
		thread_local Ui3FiniteOwnerContext* ownerContext = nullptr;
		thread_local Ui3FinitePublication::Mutation* currentMutation = nullptr;
		thread_local Ui3SvgScopeState* svgScope = nullptr;

		bool SvgRectValid(const std::uint32_t bits[4]) noexcept
		{
			for (unsigned i = 0; i < 4; ++i) if (!std::isfinite(std::bit_cast<float>(bits[i]))) return false;
			return std::bit_cast<float>(bits[0]) <= std::bit_cast<float>(bits[2])
				&& std::bit_cast<float>(bits[1]) <= std::bit_cast<float>(bits[3]);
		}
		bool SvgRectEmpty(const std::uint32_t bits[4]) noexcept
		{
		return !SvgRectValid(bits) || std::bit_cast<float>(bits[0]) == std::bit_cast<float>(bits[2])
			|| std::bit_cast<float>(bits[1]) == std::bit_cast<float>(bits[3]);
		}
		void SvgRectIntersect(const std::uint32_t a[4], const std::uint32_t b[4], std::uint32_t out[4]) noexcept
		{
			const float left = (std::max)(std::bit_cast<float>(a[0]), std::bit_cast<float>(b[0]));
			const float top = (std::max)(std::bit_cast<float>(a[1]), std::bit_cast<float>(b[1]));
			const float right = (std::max)(left, (std::min)(std::bit_cast<float>(a[2]), std::bit_cast<float>(b[2])));
			const float bottom = (std::max)(top, (std::min)(std::bit_cast<float>(a[3]), std::bit_cast<float>(b[3])));
			out[0] = std::bit_cast<std::uint32_t>(left); out[1] = std::bit_cast<std::uint32_t>(top);
			out[2] = std::bit_cast<std::uint32_t>(right); out[3] = std::bit_cast<std::uint32_t>(bottom);
		}
		bool SvgRectContains(const std::uint32_t outer[4], const std::uint32_t inner[4]) noexcept
		{
			return SvgRectValid(outer) && !SvgRectEmpty(inner)
				&& std::bit_cast<float>(outer[0]) <= std::bit_cast<float>(inner[0])
				&& std::bit_cast<float>(outer[1]) <= std::bit_cast<float>(inner[1])
				&& std::bit_cast<float>(outer[2]) >= std::bit_cast<float>(inner[2])
				&& std::bit_cast<float>(outer[3]) >= std::bit_cast<float>(inner[3]);
		}
		bool SvgMappedRect(const std::uint32_t rect[4], const std::uint32_t matrix[6],
			std::uint32_t out[4], float padding = 0.0f) noexcept
		{
			if (!SvgRectValid(rect)) return false;
			float m[6];
			for (unsigned i = 0; i < 6; ++i)
			{
				m[i] = std::bit_cast<float>(matrix[i]);
				if (!std::isfinite(m[i])) return false;
			}
			const float determinant = m[0] * m[3] - m[1] * m[2];
			if (!std::isfinite(determinant) || determinant == 0.0f) return false;
			float left = (std::numeric_limits<float>::max)(), top = left, right = -left, bottom = -left;
			for (unsigned i = 0; i < 4; ++i)
			{
				const float x = std::bit_cast<float>(rect[(i & 1) ? 2 : 0]);
				const float y = std::bit_cast<float>(rect[(i & 2) ? 3 : 1]);
				const float px = x * m[0] + y * m[2] + m[4], py = x * m[1] + y * m[3] + m[5];
				if (!std::isfinite(px) || !std::isfinite(py)) return false;
				left = (std::min)(left, px); top = (std::min)(top, py);
				right = (std::max)(right, px); bottom = (std::max)(bottom, py);
			}
			out[0] = std::bit_cast<std::uint32_t>(left - padding); out[1] = std::bit_cast<std::uint32_t>(top - padding);
			out[2] = std::bit_cast<std::uint32_t>(right + padding); out[3] = std::bit_cast<std::uint32_t>(bottom + padding);
			return SvgRectValid(out);
		}
	}

	Ui3SvgScopeState* CurrentUi3SvgScope() noexcept { return svgScope; }
	SvgObservationScope::SvgObservationScope(Ui3SvgProbe* probe, bool clocks, bool ownedInit) noexcept
		: state_{ probe, clocks, ownedInit }, previous_(svgScope)
	{
		// 空scope不隐藏已有owner；普通产品也不建立观察对象/读钟。
		if (probe) svgScope = &state_;
	}
	SvgObservationScope::~SvgObservationScope() { if (state_.probe) svgScope = previous_; }
	Ui3SvgCounters& Ui3SvgProbe::CurrentCounters() noexcept
	{
		return svgScope && svgScope->probe == this && svgScope->ownedInitialization ? initializationCounters_ : counters_;
	}
	void Ui3SvgProbe::Increment(std::uint64_t& value) noexcept
	{
		if (value != (std::numeric_limits<std::uint64_t>::max)()) ++value;
		else { frameInvalid_ = true; if (counters_.invalid != (std::numeric_limits<std::uint64_t>::max)()) ++counters_.invalid; }
	}
	Ui3SvgProbe::Slot* Ui3SvgProbe::Find(std::uint32_t tag) noexcept
	{
		for (std::size_t i = 0; i < slotCount_; ++i) if (slots_[i].tag == tag) return &slots_[i];
		return nullptr;
	}
	const Ui3SvgProbe::Slot* Ui3SvgProbe::Find(std::uint32_t tag) const noexcept
	{
		for (std::size_t i = 0; i < slotCount_; ++i) if (slots_[i].tag == tag) return &slots_[i];
		return nullptr;
	}
	void Ui3SvgProbe::NoteValueWrite(Ui3SvgObjectObservation& object, bool init) noexcept
	{
		if (object.ownerSerial == 0 && init)
		{
			object.ownerSerial = ownerSerial_; object.initializedObserved = ownerSerial_ != 0;
			return;
		}
		if (object.ownerSerial != ownerSerial_) { object.semanticKnown = false; return; }
		if (object.tag == 0) { object.initializedObserved |= init; return; }
		if (object.valueRevision == (std::numeric_limits<std::uint64_t>::max)())
			{ object.semanticKnown = false; Increment(counters_.invalid); return; }
		++object.valueRevision;
	}
	bool Ui3SvgProbe::Bind(Ui3SvgObjectObservation& object, const void* key, std::uint32_t tag,
		bool hasBitmap, std::uint32_t width, std::uint32_t height) noexcept
	{
		const auto family = tag >> 16;
		if (!key || ownerSerial_ == 0 || (family != 1 && family != 2)
			|| (tag & 0xFFFFu) > (family == 1 ? Ui3SvgMapOrdinalMax : Ui3SvgCapacity - 1))
		{ mappingInvalid_ = true; Increment(counters_.invalid); return false; }
		for (std::size_t i = 0; i < slotCount_; ++i)
			if (slots_[i].tag == tag || slots_[i].object == key)
			{
				const bool same = slots_[i].tag == tag && slots_[i].object == key && object.ownerSerial == ownerSerial_;
				if (!same) { mappingInvalid_ = true; object.semanticKnown = false; Increment(counters_.invalid); }
				return same;
			}
		if (slotCount_ == Ui3SvgCapacity) { mappingInvalid_ = true; object.semanticKnown = false; Increment(counters_.invalid); return false; }
		auto& slot = slots_[slotCount_++]; slot.object = key; slot.tag = tag;
		const bool known = svgScope && svgScope->probe == this && svgScope->ownedInitialization
			&& object.ownerSerial == ownerSerial_ && object.initializedObserved && !hasBitmap;
		object.ownerSerial = ownerSerial_; object.tag = tag; object.valueRevision = known ? 1 : 0;
		object.semanticKnown = known; object.bitmap = {}; object.bitmap.tag = tag;
		if (hasBitmap)
		{
			object.bitmap.ready = true; object.bitmap.pixelWidth = width; object.bitmap.pixelHeight = height;
			slot.bitmap = object.bitmap;
			const std::uint64_t pixels = std::uint64_t{ width } * height;
			if (width == 0 || height == 0 || pixels > (std::numeric_limits<std::uint64_t>::max)() / 4) { slot.bytes = 0; Increment(counters_.invalid); }
			else slot.bytes = pixels * 4;
			Increment(counters_.readyEntries); Increment(counters_.unknownReadyEntries);
			if (slot.bytes <= (std::numeric_limits<std::uint64_t>::max)() - counters_.logicalReadyBytes) counters_.logicalReadyBytes += slot.bytes;
			else Increment(counters_.invalid);
		}
		return true;
	}
	void Ui3SvgProbe::NoteCacheAttempt(Ui3SvgObjectObservation& object) noexcept
	{
		Increment(counters_.createAttempt); object.lastFailure = Ui3SvgFailure::None;
	}
	void Ui3SvgProbe::NoteCacheResult(Ui3SvgObjectObservation& object, const Ui3SvgBitmapProof& proof, Ui3SvgFailure failure) noexcept
	{
		object.lastFailure = failure;
		if (failure != Ui3SvgFailure::None) { Increment(counters_.createFailure); return; }
		Increment(counters_.createSuccess);
		object.bitmap = proof;
		auto* slot = Find(object.tag);
		if (!slot) return;
		if (slot->bitmap.ready)
		{
			Increment(counters_.replacement);
			counters_.logicalReadyBytes -= (std::min)(counters_.logicalReadyBytes, slot->bytes);
			if (!slot->bitmap.semanticKnown && counters_.unknownReadyEntries != 0) --counters_.unknownReadyEntries;
		}
		else Increment(counters_.readyEntries);
		slot->bitmap = proof;
		const std::uint64_t pixels = std::uint64_t{ proof.pixelWidth } * proof.pixelHeight;
		if (pixels > (std::numeric_limits<std::uint64_t>::max)() / 4)
			{ slot->bytes = 0; slot->bitmap.semanticKnown = false; Increment(counters_.invalid); }
		else slot->bytes = pixels * 4;
		if (slot->bytes <= (std::numeric_limits<std::uint64_t>::max)() - counters_.logicalReadyBytes) counters_.logicalReadyBytes += slot->bytes;
		else { slot->bitmap.semanticKnown = false; Increment(counters_.invalid); }
		if (!slot->bitmap.semanticKnown) Increment(counters_.unknownReadyEntries);
	}
	void Ui3SvgProbe::NoteCacheReset(Ui3SvgObjectObservation& object, bool hadBitmap) noexcept
	{
		if (hadBitmap) Increment(counters_.invalidation);
		if (auto* slot = Find(object.tag); slot && slot->bitmap.ready)
		{
			if (counters_.readyEntries != 0) --counters_.readyEntries;
			if (!slot->bitmap.semanticKnown && counters_.unknownReadyEntries != 0) --counters_.unknownReadyEntries;
			counters_.logicalReadyBytes -= (std::min)(counters_.logicalReadyBytes, slot->bytes);
			slot->bitmap = {}; slot->bytes = 0;
		}
		object.bitmap = {}; object.bitmap.tag = object.tag;
	}
	void Ui3SvgProbe::NoteLookup(bool update, bool knownReady) noexcept
	{
		if (update) Increment(counters_.lookupMiss);
		else if (knownReady) Increment(counters_.lookupHit);
	}
	void Ui3SvgProbe::NoteOperation(Ui3SvgStage stage, Ui3SvgOperation operation) noexcept
	{
		auto& count = CurrentCounters();
		if (stage == Ui3SvgStage::Parse) { if (operation == Ui3SvgOperation::Begin) Increment(count.parseCalls); else if (operation == Ui3SvgOperation::Failure) Increment(count.parseFailure); }
		else if (stage == Ui3SvgStage::Raster) { if (operation == Ui3SvgOperation::Begin) Increment(count.rasterCalls); else if (operation == Ui3SvgOperation::Failure) Increment(count.rasterFailure); }
		else if (stage == Ui3SvgStage::Upload) { if (operation == Ui3SvgOperation::Begin) Increment(count.uploadCalls); else if (operation == Ui3SvgOperation::Failure) Increment(count.uploadFailure); }
		else if (stage == Ui3SvgStage::Draw && operation == Ui3SvgOperation::Begin) Increment(count.drawSubmit);
	}
	void Ui3SvgProbe::BeginFrame(std::uint64_t revision, std::uint64_t epoch, std::uint64_t attempt) noexcept
	{
		target_ = {}; target_.revision = revision; target_.epoch = epoch; target_.frameAttemptSerial = attempt;
		context_ = nullptr; drawing_ = clipKnown_ = clearObserved_ = fullBackingCleared_ = false; clipDepth_ = 0; unboundRequired_ = 0;
		frameInvalid_ = mappingInvalid_ || epoch == 0 || attempt == 0;
		for (std::size_t i = 0; i < slotCount_; ++i)
		{
			auto& slot = slots_[i]; slot.required = slot.expectedVisible = slot.semanticSettled = slot.submitted = slot.overwritten = false;
			slot.draw = {}; slot.expected = {};
			slot.clearedOld = false;
		}
	}
	void Ui3SvgProbe::Require(const void* key, const Ui3SvgObjectObservation& object, const Ui3SvgBitmapProof& expected,
		bool visible, bool settled, std::uint32_t opacity) noexcept
	{
		auto* slot = Find(object.tag);
		if (!slot || slot->object != key || object.ownerSerial != ownerSerial_)
			{ if (unboundRequired_ != (std::numeric_limits<std::uint32_t>::max)()) ++unboundRequired_; frameInvalid_ = true; return; }
		slot->required = true; slot->expected = expected; slot->expectedVisible = visible;
		slot->semanticSettled = settled && object.semanticKnown; slot->opacityBits = opacity;
	}
	bool Ui3SvgProbe::NeedsHiddenProof(std::uint32_t tag) const noexcept
	{
		const auto* slot = Find(tag); return slot && slot->possiblyVisible;
	}
	void Ui3SvgProbe::BeginBackingWrite(const void* context, const Ui3SvgFrameTarget& target) noexcept
	{
		if (target.surfaceSerial != previousSurface_ || target.epoch != previousEpoch_)
		{
			Increment(invalidationSerial_);
			for (std::size_t i = 0; i < slotCount_; ++i)
				{ slots_[i].oldBoundsKnown = slots_[i].possiblyVisible = slots_[i].hiddenLineageUnknown = false; }
		}
		previousSurface_ = target.surfaceSerial; previousEpoch_ = target.epoch;
		target_ = target; context_ = context; drawing_ = context != nullptr;
		Increment(mutationSerial_); // 失败/deferred也可能已经改写backing，旧paint从此未知。
		frameInvalid_ |= !drawing_ || target.epoch == 0 || target.surfaceSerial == 0 || target.frameAttemptSerial == 0
			|| target.width == 0 || target.height == 0 || target.backingWidth == 0 || target.backingHeight == 0
			|| target.dpi == 0 || target.windowAlpha > 255 || !SvgRectValid(target.viewportBits)
			|| !std::isfinite(std::bit_cast<double>(target.zoomBits)) || std::bit_cast<double>(target.zoomBits) <= 0.0;
	}
	void Ui3SvgProbe::PushClip(const void* context, const std::uint32_t rect[4], const std::uint32_t matrix[6]) noexcept
	{
		if (!drawing_ || context != context_) return;
		if (clipDepth_ == clips_.size()) { clipKnown_ = false; frameInvalid_ = true; return; }
		std::uint32_t mapped[4]{};
		// 只认证平移/轴缩放的矩形clip；其它变换不猜D2D内部裁剪形状。
		const bool known = std::bit_cast<float>(matrix[1]) == 0.0f && std::bit_cast<float>(matrix[2]) == 0.0f
			&& SvgMappedRect(rect, matrix, mapped);
		if (!known || (clipDepth_ != 0 && !clipKnown_)) { clipKnown_ = false; frameInvalid_ = true; return; }
		if (clipDepth_ != 0) SvgRectIntersect(mapped, clips_[clipDepth_ - 1].data(), mapped);
		std::copy_n(mapped, 4, clips_[clipDepth_++].data()); clipKnown_ = true;
	}
	void Ui3SvgProbe::PopClip(const void* context) noexcept
	{
		if (!drawing_ || context != context_) return;
		if (clipDepth_ == 0) { clipKnown_ = false; frameInvalid_ = true; return; }
		--clipDepth_; clipKnown_ = clipDepth_ != 0;
	}
	void Ui3SvgProbe::ObserveClear(const void* context) noexcept
	{
		if (!drawing_ || context != context_) return;
		Increment(mutationSerial_);
		clearObserved_ = clipKnown_ && clipDepth_ != 0;
		if (clearObserved_)
		{
			std::copy_n(clips_[clipDepth_ - 1].data(), 4, clearBounds_);
			const std::uint32_t backing[4]{ std::bit_cast<std::uint32_t>(0.0f), std::bit_cast<std::uint32_t>(0.0f),
				std::bit_cast<std::uint32_t>(static_cast<float>(target_.backingWidth)), std::bit_cast<std::uint32_t>(static_cast<float>(target_.backingHeight)) };
			fullBackingCleared_ |= SvgRectContains(clearBounds_, backing);
		}
		for (std::size_t i = 0; i < slotCount_; ++i)
		{
			auto& slot = slots_[i];
			if (slot.submitted) slot.overwritten = true;
			slot.clearedOld |= clearObserved_ && slot.oldBoundsKnown && SvgRectContains(clearBounds_, slot.oldBounds);
		}
	}
	void Ui3SvgProbe::ObserveUnknownWrite(const void* context) noexcept
	{
		if (!drawing_ || context != context_) return;
		// 未知写在全Clear之后出现时，只有它之后的全Clear才有恢复资格。
		fullBackingCleared_ = false;
		Increment(mutationSerial_);
		for (std::size_t i = 0; i < slotCount_; ++i)
			if (slots_[i].required) { slots_[i].overwritten = true; slots_[i].hiddenLineageUnknown = true; }
	}
	void Ui3SvgProbe::ObserveDraw(const void* context, const Ui3SvgObjectObservation& object, Ui3SvgDrawObservation draw) noexcept
	{
		if (!drawing_ || context != context_) return;
		auto* slot = Find(object.tag);
		if (!slot || !slot->required) { ObserveUnknownWrite(context); return; }
		Increment(mutationSerial_); slot->submitted = true; slot->overwritten = false;
		draw.expectedVisible = slot->expectedVisible; draw.windowPresentationAlpha = target_.windowAlpha;
		draw.frameAttemptSerial = target_.frameAttemptSerial; draw.bufferMutationSerial = mutationSerial_;
		draw.targetInvalidationSerial = invalidationSerial_;
		std::uint32_t bounds[4]{};
		if (SvgMappedRect(draw.destBits, draw.transformBits, bounds, 2.0f) && SvgRectValid(target_.viewportBits))
		{
			SvgRectIntersect(bounds, target_.viewportBits, draw.expectedVisibleBoundsBits);
			if (clipKnown_ && clipDepth_ != 0)
			{
				std::copy_n(clips_[clipDepth_ - 1].data(), 4, draw.effectiveClipBits);
				std::uint32_t intersection[4]{}; SvgRectIntersect(draw.expectedVisibleBoundsBits, draw.effectiveClipBits, intersection);
				draw.coverage = SvgRectEmpty(intersection) ? Ui3SvgCoverage::Empty
					: SvgRectContains(draw.effectiveClipBits, draw.expectedVisibleBoundsBits) ? Ui3SvgCoverage::FullVisibleCoverage : Ui3SvgCoverage::Partial;
			}
		}
		// 后SVG真实写域可能覆盖先前A；未知域不能假定不相交。
		std::uint32_t written[4]{};
		const bool writeKnown = clipKnown_ && clipDepth_ != 0 && SvgMappedRect(draw.destBits, draw.transformBits, written, 2.0f);
		if (writeKnown) SvgRectIntersect(written, clips_[clipDepth_ - 1].data(), written);
		else { ObserveUnknownWrite(context); draw.bufferMutationSerial = mutationSerial_; }
		for (std::size_t i = 0; i < slotCount_; ++i)
		{
			auto& previous = slots_[i]; if (&previous == slot || !previous.required || !previous.submitted) continue;
			std::uint32_t overlap[4]{};
			if (writeKnown) SvgRectIntersect(written, previous.draw.expectedVisibleBoundsBits, overlap);
			if (!writeKnown || !SvgRectEmpty(overlap)) previous.overwritten = true;
		}
		slot->draw = draw; slot->possiblyVisible = true;
	}
	void Ui3SvgProbe::ObserveRejected(const Ui3SvgObjectObservation& object, Ui3SvgFailure failure, bool qualityFallback) noexcept
	{
		if (!qualityFallback) Increment(counters_.drawRejected);
		if (auto* slot = Find(object.tag); slot && slot->required)
		{
			slot->draw.used = object.bitmap; slot->draw.failure = failure;
			slot->draw.use = qualityFallback ? Ui3SvgUse::QualityFallback : Ui3SvgUse::Missing;
		}
	}
	Ui3FiniteResourceProof Ui3SvgProbe::FinishDrawing() noexcept
	{
		Ui3FiniteResourceProof proof;
		proof.revision = target_.revision; proof.epoch = target_.epoch; proof.surfaceSerial = target_.surfaceSerial;
		proof.frameAttemptSerial = target_.frameAttemptSerial; proof.producerPresent = true;
		proof.required = proof.unverified = unboundRequired_;
		if (unboundRequired_ != 0) proof.firstUnverifiedReason = static_cast<std::uint32_t>(Ui3SvgProofReason::Unbound);
		for (std::size_t i = 0; i < slotCount_; ++i)
		{
			auto& slot = slots_[i]; if (!slot.required) continue;
			if (proof.required == (std::numeric_limits<std::uint32_t>::max)()) { Increment(counters_.invalid); break; }
			++proof.required;
			const bool failed = slot.expectedVisible && slot.draw.failure != Ui3SvgFailure::None && slot.draw.failure != Ui3SvgFailure::ProofUnknown;
			Ui3SvgProofReason reason = Ui3SvgProofReason::None;
			if (failed) reason = static_cast<Ui3SvgProofReason>(static_cast<unsigned>(Ui3SvgProofReason::ApiFailure) + static_cast<unsigned>(slot.draw.failure));
			else if (frameInvalid_ || mappingInvalid_ || !drawing_) reason = Ui3SvgProofReason::Invalid;
			else if (!slot.semanticSettled || !slot.expected.semanticKnown) reason = Ui3SvgProofReason::Semantic;
			else if (slot.overwritten) reason = Ui3SvgProofReason::Overwrite;
			else if (!slot.expectedVisible)
			{
				slot.draw.used = slot.expected; slot.draw.used.ready = false; // Hidden没有提交bitmap，记录当前纯值意图。
				slot.draw.frameAttemptSerial = target_.frameAttemptSerial; slot.draw.windowPresentationAlpha = target_.windowAlpha;
				slot.draw.bufferMutationSerial = mutationSerial_; slot.draw.targetInvalidationSerial = invalidationSerial_;
				if (clearObserved_) std::copy_n(clearBounds_, 4, slot.draw.effectiveClipBits);
				if (slot.oldBoundsKnown) std::copy_n(slot.oldBounds, 4, slot.draw.expectedVisibleBoundsBits);
				slot.draw.clearCoversOldBounds = slot.oldBoundsKnown && slot.clearedOld
					&& (!slot.hiddenLineageUnknown || fullBackingCleared_);
				if (!slot.oldBoundsKnown) reason = Ui3SvgProofReason::HiddenBoundsUnknown;
				else if (!slot.draw.clearCoversOldBounds) reason = Ui3SvgProofReason::HiddenNotCleared;
			}
			else if (!slot.submitted) reason = Ui3SvgProofReason::NotDrawn;
			else
			{
				const auto& used = slot.draw.used;
				const double width = std::bit_cast<double>(used.requestedWBits), height = std::bit_cast<double>(used.requestedHBits);
				const double zoom = std::bit_cast<double>(target_.zoomBits);
				const double expectedWidth = std::bit_cast<double>(slot.expected.requestedWBits) * zoom;
				const double expectedHeight = std::bit_cast<double>(slot.expected.requestedHBits) * zoom;
				const float opacity = std::bit_cast<float>(slot.draw.finalOpacityBits);
				if (!used.ready || !used.semanticKnown) reason = Ui3SvgProofReason::UnknownBitmap;
				else if (used.valueRevision == 0 || used.valueRevision != slot.expected.valueRevision || used.tag != slot.tag
					|| used.colorMask != slot.expected.colorMask || used.color1Rgb != slot.expected.color1Rgb || used.color2Rgb != slot.expected.color2Rgb)
					reason = Ui3SvgProofReason::Semantic;
				else if (used.epoch != target_.epoch || used.surfaceSerial != target_.surfaceSerial) reason = Ui3SvgProofReason::Epoch;
				else if (used.dpi != target_.dpi) reason = Ui3SvgProofReason::Dpi;
				else if (!slot.draw.qualityMatches) reason = Ui3SvgProofReason::Quality;
				else if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(expectedWidth) || !std::isfinite(expectedHeight)
					|| width < 1.0 || height < 1.0 || width > 0x7FFFFFFF || height > 0x7FFFFFFF
					|| std::abs(width - expectedWidth) > 0.01 || std::abs(height - expectedHeight) > 0.01
					|| used.pixelWidth != static_cast<std::uint32_t>(width) || used.pixelHeight != static_cast<std::uint32_t>(height))
					reason = Ui3SvgProofReason::Size;
				else if (!std::isfinite(opacity) || opacity <= 0.0f || opacity > 1.0f || slot.draw.finalOpacityBits != slot.opacityBits || target_.windowAlpha == 0)
					reason = Ui3SvgProofReason::Opacity;
				else if (slot.draw.coverage != Ui3SvgCoverage::FullVisibleCoverage) reason = Ui3SvgProofReason::Coverage;
			}
			if (reason == Ui3SvgProofReason::None)
			{
				++proof.verified; slot.draw.use = slot.expectedVisible ? Ui3SvgUse::DrawnVerified : Ui3SvgUse::HiddenExpected;
			}
			else
			{
				if (failed) ++proof.failed; else ++proof.unverified;
				if (!failed) slot.draw.use = Ui3SvgUse::Unverified;
			}
			if (reason != Ui3SvgProofReason::None && proof.firstUnverifiedReason == 0)
			{
				proof.firstUnverifiedSvgTag = slot.tag;
				proof.firstUnverifiedReason = static_cast<std::uint32_t>(reason);
			}
		}
		if (proof.required == 0 && proof.firstUnverifiedReason == 0) proof.firstUnverifiedReason = static_cast<std::uint32_t>(Ui3SvgProofReason::NotDrawn);
		return proof;
	}
	void Ui3SvgProbe::CompleteAttempt(bool committed) noexcept
	{
		// 失败/deferred已可能在旧bounds外写入：不能用上一成功bounds认证Hidden。
		if (drawing_ && !committed)
			for (std::size_t i = 0; i < slotCount_; ++i) { slots_[i].hiddenLineageUnknown = true; slots_[i].draw.use = Ui3SvgUse::Unverified; }
		if (drawing_ && committed)
			for (std::size_t i = 0; i < slotCount_; ++i)
			{
				auto& slot = slots_[i];
				if (fullBackingCleared_) slot.hiddenLineageUnknown = false;
				if (slot.draw.use == Ui3SvgUse::HiddenExpected) { slot.oldBoundsKnown = slot.possiblyVisible = false; continue; }
				if (slot.submitted && !SvgRectEmpty(slot.draw.expectedVisibleBoundsBits))
				{
					if (slot.oldBoundsKnown && !slot.clearedOld)
					{
						for (unsigned edge = 0; edge < 4; ++edge)
						{
							const float a = std::bit_cast<float>(slot.oldBounds[edge]), b = std::bit_cast<float>(slot.draw.expectedVisibleBoundsBits[edge]);
							slot.oldBounds[edge] = std::bit_cast<std::uint32_t>(edge < 2 ? (std::min)(a, b) : (std::max)(a, b));
						}
					}
					else std::copy_n(slot.draw.expectedVisibleBoundsBits, 4, slot.oldBounds);
					slot.oldBoundsKnown = slot.possiblyVisible = true;
				}
			}
		drawing_ = false; context_ = nullptr;
	}
	Ui3SvgDrawObservation Ui3SvgProbe::Observation(std::uint32_t tag) const noexcept
	{
		const auto* slot = Find(tag); return slot ? slot->draw : Ui3SvgDrawObservation{};
	}
	double* Ui3SvgProbe::ElapsedCounter(Ui3SvgStage stage) noexcept
	{
		auto& count = CurrentCounters();
		if (stage == Ui3SvgStage::Parse) return &count.parseMs;
		if (stage == Ui3SvgStage::Raster) return &count.rasterAndInternalGeometryMs;
		if (stage == Ui3SvgStage::Upload) return &count.uploadMs;
		return &count.drawSubmitMs;
	}
	std::int64_t Ui3SvgProbe::ReadTicks() noexcept
	{
		Increment(CurrentCounters().clockReads); return std::chrono::steady_clock::now().time_since_epoch().count();
	}
	Ui3SvgStageTimer::Ui3SvgStageTimer(Ui3SvgStage stage) noexcept
	{
		if (!svgScope || !svgScope->probe || !svgScope->clocksEnabled) return;
		probe_ = svgScope->probe; elapsed_ = probe_->ElapsedCounter(stage); started_ = probe_->ReadTicks();
	}
	Ui3SvgStageTimer::~Ui3SvgStageTimer()
	{
		if (elapsed_)
		{
			const auto ended = probe_->ReadTicks();
			if (ended >= started_) *elapsed_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::duration(ended - started_)).count();
		}
	}
	void ObserveUi3SvgOperation(Ui3SvgStage stage, Ui3SvgOperation operation) noexcept
	{
		if (svgScope && svgScope->probe) svgScope->probe->NoteOperation(stage, operation);
	}
	void ObserveUi3SvgClear(const void* context) noexcept { if (svgScope && svgScope->probe) svgScope->probe->ObserveClear(context); }
	void ObserveUi3SvgUnknownWrite(const void* context) noexcept { if (svgScope && svgScope->probe) svgScope->probe->ObserveUnknownWrite(context); }
	void ObserveUi3SvgClipPush(const void* context, const std::uint32_t rect[4], const std::uint32_t matrix[6]) noexcept
	{
		if (svgScope && svgScope->probe) svgScope->probe->PushClip(context, rect, matrix);
	}
	void ObserveUi3SvgClipPop(const void* context) noexcept { if (svgScope && svgScope->probe) svgScope->probe->PopClip(context); }
	bool FitsUi3SvgCaptureBudget(std::size_t capacity, std::size_t callbackBytes, std::size_t batchBytes) noexcept
	{
		constexpr std::size_t budget = 64 * 1024 * 1024;
		constexpr std::size_t fixed = sizeof(Ui3SvgProbe) + sizeof(Ui3FinitePublication) + sizeof(Ui3FiniteObserver)
			+ Ui3SvgCapacity * sizeof(Ui3SvgObjectObservation) + 1024 * 64;
		static_assert(fixed < budget);
		if (capacity > 65536 || callbackBytes > budget || batchBytes > budget - callbackBytes) return false;
		const auto stride = callbackBytes + batchBytes;
		return capacity == 0 || (stride != 0 && capacity <= (budget - fixed) / stride);
	}

	Ui3FinitePublication::Ui3FinitePublication(std::uint64_t runSerial,
		const Ui3FiniteSignature& initialStable) noexcept
		: initialStable_(initialStable), runSerial_(runSerial) {}

	void Ui3FinitePublication::Checkpoint(Ui3FiniteTestPoint point) noexcept
	{
		if (hooks_.checkpoint) hooks_.checkpoint(point, hooks_.context);
	}

	Ui3FinitePublication::Mutation Ui3FinitePublication::BeginMutation(std::uint64_t stepId,
		Ui3FiniteScene scene, std::uint64_t sourceSequence) noexcept
	{
		Mutation mutation;
		mutation.owner = this;
		mutation.stepId = stepId;
		mutation.sourceSequence = sourceSequence;
		mutation.scene = scene;
		if (counters_.seen == (std::numeric_limits<std::uint64_t>::max)())
		{
			// 有限协议不会到此；若数值耗尽，失去证明而不绕回零。
			goalKnown_ = false;
			payload_[AcceptedWords].store(0, std::memory_order_release);
			return mutation;
		}
		++counters_.seen;
		if (counters_.retained < Ui3FiniteCapacity)
		{
			mutation.recordIndex = static_cast<std::size_t>(counters_.retained++);
			auto& record = records_[mutation.recordIndex];
			record = {};
			record.accepted.runSerial = runSerial_;
			record.accepted.stepId = stepId;
			record.accepted.sourceSequence = sourceSequence;
			record.accepted.scene = scene;
		}
		else ++counters_.dropped;
		if (mutationOpen_)
		{
			// 仅一个Interaction writer；意外重入不偷走外层奇数区，外层也不能追认。
			mutationInterfered_ = true;
			goalKnown_ = false;
			++counters_.rejected;
			++counters_.ambiguous;
			if (mutation.recordIndex < Ui3FiniteCapacity)
				records_[mutation.recordIndex].terminalStatus = Ui3FiniteStatus::AmbiguousPublication;
			return mutation;
		}
		const auto serial = publicationSerial_.load(std::memory_order_relaxed);
		if ((serial & 1ULL) != 0 || serial > (std::numeric_limits<std::uint64_t>::max)() - 2)
		{
			goalKnown_ = false;
			payload_[AcceptedWords].store(0, std::memory_order_release);
			++counters_.invalid;
			++counters_.rejected;
			if (mutation.recordIndex < Ui3FiniteCapacity)
				records_[mutation.recordIndex].terminalStatus = Ui3FiniteStatus::Overflow;
			return mutation;
		}
		mutationOpen_ = true;
		mutationInterfered_ = false;
		mutation.serial = publicationSerial_.fetch_add(1, std::memory_order_acq_rel) + 1;
		mutation.active = true;
		Checkpoint(Ui3FiniteTestPoint::AfterOddBeforeFence);
		// 此屏障必须在odd之后、第一业务写/payload原子写之前，末尾屏障不能替代。
		std::atomic_thread_fence(std::memory_order_release);
		Checkpoint(Ui3FiniteTestPoint::AfterFence);
		return mutation;
	}

	bool Ui3FinitePublication::Owns(const Mutation& mutation) const noexcept
	{
		return mutation.owner == this && mutation.active && mutationOpen_
			&& mutation.serial == publicationSerial_.load(std::memory_order_relaxed);
	}
	void Ui3FinitePublication::MarkBusinessAccepted(Mutation& mutation) noexcept
	{
		if (Owns(mutation)) mutation.businessAccepted = true;
	}
	void Ui3FinitePublication::ObserveBusinessWrite(Mutation& mutation) noexcept
	{
		if (!Owns(mutation)) return;
		mutation.witness = Ui3BusinessWriteWitness::WriteOccurred;
		Checkpoint(Ui3FiniteTestPoint::AfterBusinessWrite);
	}

	namespace
	{
		constexpr std::uint32_t UnknownWriteFlag = 1u << 2;
		constexpr std::uint32_t InvalidSignatureFlag = 1u << 3;
		constexpr std::uint32_t InvalidTimeFlag = 1u << 4;

		bool FrozenFiniteInputsMatch(const Ui3FiniteSignature& left, const Ui3FiniteSignature& right) noexcept
		{
			return left.stateMode == right.stateMode && left.penMode == right.penMode
				&& left.penColorRgb == right.penColorRgb && left.penWidthBits == right.penWidthBits
				&& left.toolRevision == right.toolRevision && left.thicknessView == right.thicknessView
				&& left.darkStyle == right.darkStyle && left.dpi == right.dpi
				&& left.displaySerial == right.displaySerial && left.configZoomBits == right.configZoomBits;
		}
		bool FiniteSceneStateSupported(const Ui3FiniteSignature& signature) noexcept
		{
			// 本批只Main/Draw，其他浮层或gesture不能被当成已闭合aux状态。
			return (signature.flags & 64u) != 0 && (signature.flags & (4u | 8u | 16u | 32u)) == 0
				&& (signature.flags & 3u) != 3u && signature.thicknessView == 0;
		}
	}

	bool IsUi3FiniteSignatureValid(const Ui3FiniteSignature& signature) noexcept
	{
		const float width = std::bit_cast<float>(signature.penWidthBits);
		const double zoom = std::bit_cast<double>(signature.configZoomBits);
		return signature.validMask == Ui3FiniteRequiredMask && (signature.flags & ~127u) == 0
			&& signature.stateMode == 1 && signature.penMode <= 2 && signature.penColorRgb <= 0xFFFFFFu
			&& signature.mainSide <= 1 && signature.primarySide <= 1 && signature.thicknessView <= 2
			&& signature.darkStyle <= 1 && signature.dpi != 0 && signature.toolRevision != 0
			&& signature.displaySerial != 0 && (signature.displaySerial & 1ULL) == 0
			&& std::isfinite(width) && width > 0.0f && std::isfinite(zoom) && zoom > 0.0;
	}
	bool SameUi3FiniteSemanticSignature(const Ui3FiniteSignature& left, const Ui3FiniteSignature& right) noexcept
	{
		// side由render合法派生，不能把自动居中当新用户目标；完整候选另由P2验证。
		return IsUi3FiniteSignatureValid(left) && IsUi3FiniteSignatureValid(right)
			&& left.flags == right.flags && FrozenFiniteInputsMatch(left, right);
	}

	void Ui3FinitePublication::ReconcilePreviousGoal(Ui3FiniteStatus pendingStatus) noexcept
	{
		if (!goalKnown_) return;
		const bool completed = completedRevision_.load(std::memory_order_acquire) == goal_.revision;
		for (std::size_t i = 0; i < counters_.retained; ++i)
		{
			auto& record = records_[i];
			if (record.accepted.revision != goal_.revision || record.terminalStatus != Ui3FiniteStatus::Pending) continue;
			record.terminalStatus = completed
				? (record.accepted.status == Ui3FiniteStatus::AcceptedNoChange
					? Ui3FiniteStatus::AcceptedNoChange : Ui3FiniteStatus::CompletedLayoutAndSvg)
				: pendingStatus;
		}
	}

	void Ui3FinitePublication::PublishAndClose(Mutation& mutation) noexcept
	{
		goal_.publicationSerial = mutation.serial + 1;
		const auto words = std::bit_cast<std::array<std::uint64_t, AcceptedWords>>(goal_);
		for (std::size_t i = 0; i < words.size(); ++i)
		{
			payload_[i].store(words[i], std::memory_order_relaxed);
			if (i + 1 == AcceptedWords / 2) Checkpoint(Ui3FiniteTestPoint::AfterPayloadHalf);
		}
		payload_[AcceptedWords].store(goalKnown_ ? 1 : 0, std::memory_order_relaxed);
		Checkpoint(Ui3FiniteTestPoint::BeforeEven);
		publicationSerial_.store(mutation.serial + 1, std::memory_order_release);
		mutation.active = false;
		mutationOpen_ = false;
		Checkpoint(Ui3FiniteTestPoint::BeforeRenderRequest);
	}

	void Ui3FinitePublication::FinishAtRenderRequest(Mutation& mutation,
		const Ui3FiniteSignature& signature, std::int64_t acceptedTicks) noexcept
	{
		if (!Owns(mutation)) return;
		if (mutation.recordIndex < Ui3FiniteCapacity)
			records_[mutation.recordIndex].accepted.signature = signature; // 保留坏值，不clamp/补mask。
		if (!mutation.businessAccepted || mutationInterfered_)
		{
			FinishRejected(mutation, mutationInterfered_ ? Ui3FiniteStatus::AmbiguousPublication
				: mutation.rejectedStatus, mutation.witness);
			return;
		}
		const bool knownScene = mutation.scene == Ui3FiniteScene::MainFold || mutation.scene == Ui3FiniteScene::DrawAttribute;
		if (runSerial_ == 0 || mutation.stepId == 0 || mutation.sourceSequence == 0 || !knownScene
			|| !IsUi3FiniteSignatureValid(signature) || !IsUi3FiniteSignatureValid(initialStable_)
			|| !FrozenFiniteInputsMatch(signature, initialStable_) || !FiniteSceneStateSupported(signature)
			|| !FiniteSceneStateSupported(initialStable_)
			|| (mutation.scene == Ui3FiniteScene::DrawAttribute && (signature.flags & 1u) != 0))
		{
			++counters_.invalid;
			if (mutation.recordIndex < Ui3FiniteCapacity)
				records_[mutation.recordIndex].failureFlags |= InvalidSignatureFlag;
			FinishRejected(mutation, Ui3FiniteStatus::UnsupportedState, Ui3BusinessWriteWitness::Unknown);
			return;
		}
		const bool noChange = goalKnown_ && SameUi3FiniteSemanticSignature(goal_.signature, signature);
		if (!noChange && nextRevision_ == (std::numeric_limits<std::uint64_t>::max)())
		{
			++counters_.invalid;
			FinishRejected(mutation, Ui3FiniteStatus::Overflow, Ui3BusinessWriteWitness::Unknown);
			return;
		}
		Ui3FiniteAccepted accepted;
		accepted.runSerial = runSerial_;
		accepted.stepId = mutation.stepId;
		accepted.sourceSequence = mutation.sourceSequence;
		accepted.publicationSerial = mutation.serial + 1;
		accepted.scene = mutation.scene;
		accepted.ownerReceiveTicks = mutation.ownerReceiveTicks;
		accepted.acceptedTicks = acceptedTicks;
		accepted.signature = signature;
		accepted.status = noChange ? Ui3FiniteStatus::AcceptedNoChange : Ui3FiniteStatus::Accepted;
		++counters_.accepted;
		if (noChange)
		{
			++counters_.noChange;
			accepted.revision = goal_.revision;
			ReconcilePreviousGoal(Ui3FiniteStatus::Pending);
		}
		else
		{
			ReconcilePreviousGoal(Ui3FiniteStatus::Superseded);
			accepted.revision = ++nextRevision_;
			goal_ = accepted;
			goalKnown_ = true;
		}
		if (mutation.recordIndex < Ui3FiniteCapacity)
		{
			auto& record = records_[mutation.recordIndex];
			record.accepted = accepted;
			if (noChange)
			{
				const bool completed = completedRevision_.load(std::memory_order_acquire) == goal_.revision;
				record.reusedRevision = goal_.revision;
				record.failureFlags |= completed ? Ui3FiniteReusedCompleted : Ui3FiniteReusedPending;
				record.terminalStatus = completed ? Ui3FiniteStatus::AcceptedNoChange : Ui3FiniteStatus::Pending;
				// 即使旧goal已完成，也不造新commit/timing=0ms。
				record.timingValid = false;
			}
			else
			{
				record.timingValid = mutation.ownerReceiveTicks > 0 && acceptedTicks >= mutation.ownerReceiveTicks;
				if (acceptedTicks < 0 || mutation.ownerReceiveTicks < 0
					|| (mutation.ownerReceiveTicks > 0 && acceptedTicks != 0 && acceptedTicks < mutation.ownerReceiveTicks))
				{
					record.failureFlags |= InvalidTimeFlag;
					++counters_.invalid;
				}
			}
		}
		PublishAndClose(mutation);
	}

	void Ui3FinitePublication::FinishRejected(Mutation& mutation, Ui3FiniteStatus status,
		Ui3BusinessWriteWitness witness) noexcept
	{
		if (!Owns(mutation)) return;
		const bool noWrite = !mutationInterfered_ && mutation.witness != Ui3BusinessWriteWitness::WriteOccurred
			&& witness == Ui3BusinessWriteWitness::NoBusinessWrite;
		++counters_.rejected;
		if (!noWrite)
		{
			ReconcilePreviousGoal(Ui3FiniteStatus::ResourceUnverified);
			goalKnown_ = false;
			if (status == Ui3FiniteStatus::RejectedByBusiness || status == Ui3FiniteStatus::AmbiguousPublication)
			{
				status = Ui3FiniteStatus::AmbiguousPublication;
				++counters_.ambiguous;
			}
		}
		else ReconcilePreviousGoal(Ui3FiniteStatus::Pending);
		if (mutation.recordIndex < Ui3FiniteCapacity)
		{
			auto& record = records_[mutation.recordIndex];
			record.accepted.status = status;
			record.accepted.publicationSerial = mutation.serial + 1;
			record.accepted.ownerReceiveTicks = mutation.ownerReceiveTicks;
			record.terminalStatus = status;
			if (!noWrite) record.failureFlags |= UnknownWriteFlag;
		}
		PublishAndClose(mutation);
	}

	bool Ui3FinitePublication::TryReadStable(Ui3FiniteAccepted& out) const noexcept
	{
		const auto before = publicationSerial_.load(std::memory_order_acquire);
		if ((before & 1ULL) != 0) return false;
		std::array<std::uint64_t, AcceptedWords> words;
		for (std::size_t i = 0; i < words.size(); ++i) words[i] = payload_[i].load(std::memory_order_relaxed);
		const auto known = payload_[AcceptedWords].load(std::memory_order_relaxed);
		// 与写侧前置release fence经读取到的atomic字段同步，再复核serial，只读一次。
		std::atomic_thread_fence(std::memory_order_acquire);
		const auto after = publicationSerial_.load(std::memory_order_acquire);
		if (before != after || (after & 1ULL) != 0 || known != 1) return false;
		const auto accepted = std::bit_cast<Ui3FiniteAccepted>(words);
		if (accepted.publicationSerial != after || accepted.runSerial == 0 || accepted.stepId == 0
			|| accepted.revision == 0 || !IsUi3FiniteSignatureValid(accepted.signature)) return false;
		out = accepted;
		return true;
	}
	bool Ui3FinitePublication::StillSameSemanticGoal(const Ui3FiniteAccepted& candidate,
		Ui3FiniteAccepted& current) const noexcept
	{
		Ui3FiniteAccepted stable;
		if (!TryReadStable(stable) || candidate.runSerial != stable.runSerial
			|| candidate.revision != stable.revision || candidate.stepId != stable.stepId
			|| candidate.sourceSequence != stable.sourceSequence || candidate.scene != stable.scene
			|| !SameUi3FiniteSemanticSignature(candidate.signature, stable.signature)) return false;
		current = stable;
		return true;
	}
	bool Ui3FinitePublication::NoteCompletedGoal(std::uint64_t revision) noexcept
	{
		Ui3FiniteAccepted stable;
		if (revision == 0 || !TryReadStable(stable) || stable.revision != revision) return false;
		auto previous = completedRevision_.load(std::memory_order_acquire);
		if (previous > revision) return false;
		if (previous == revision) return true;
		// render只发布receipt，不并发改Interaction ledger/普通goal，CAS也不忙等。
		return completedRevision_.compare_exchange_strong(previous, revision,
			std::memory_order_release, std::memory_order_relaxed);
	}

	void Ui3FinitePublication::AbsorbAfterOwnersStopped(std::span<const Ui3FiniteTargetRecord> outcomes) noexcept
	{
		// 明确all owners joined的调用阶段；不由render修改Interaction普通ledger。
		if (mutationOpen_) return;
		for (std::size_t i = 0; i < counters_.retained; ++i)
		{
			auto& row = records_[i];
			if (row.accepted.status != Ui3FiniteStatus::Accepted && row.accepted.status != Ui3FiniteStatus::AcceptedNoChange) continue;
			for (const auto& outcome : outcomes)
			{
				if (row.accepted.runSerial != outcome.accepted.runSerial || row.accepted.revision != outcome.accepted.revision
					|| !SameUi3FiniteSemanticSignature(row.accepted.signature, outcome.accepted.signature)) continue;
				row.requiredSvg = outcome.requiredSvg; row.verifiedSvg = outcome.verifiedSvg;
				row.failedSvg = outcome.failedSvg; row.unverifiedSvg = outcome.unverifiedSvg;
				row.firstUnverifiedSvgTag = outcome.firstUnverifiedSvgTag; row.firstUnverifiedReason = outcome.firstUnverifiedReason;
				row.failureFlags |= outcome.failureFlags;
				if (row.accepted.status == Ui3FiniteStatus::AcceptedNoChange)
				{
					row.terminalStatus = outcome.terminalStatus == Ui3FiniteStatus::CompletedLayoutAndSvg
						? Ui3FiniteStatus::AcceptedNoChange : outcome.terminalStatus;
					row.failureFlags &= ~(Ui3FiniteReusedPending | Ui3FiniteReusedCompleted);
					row.failureFlags |= outcome.terminalStatus == Ui3FiniteStatus::CompletedLayoutAndSvg
						? Ui3FiniteReusedCompleted : Ui3FiniteReusedPending;
					row.timingValid = false;
					continue;
				}
				row.terminalStatus = outcome.terminalStatus;
				row.epoch = outcome.epoch; row.surfaceSerial = outcome.surfaceSerial;
				row.trueBarAttemptSerial = outcome.trueBarAttemptSerial;
				row.consumedTicks = outcome.consumedTicks; row.settledTicks = outcome.settledTicks;
				row.finalCommitTicks = outcome.finalCommitTicks; row.timingValid = outcome.timingValid;
				row.pendingRoles = outcome.pendingRoles; row.proofMask = outcome.proofMask;
				row.failureFlags |= outcome.failureFlags;
			}
		}
	}

	namespace { std::atomic<Ui3FiniteObserver*> activeObserver = nullptr; }
	Ui3FiniteObserver* SetActiveUi3FiniteObserver(Ui3FiniteObserver* observer) noexcept
	{
		return activeObserver.exchange(observer, std::memory_order_acq_rel);
	}
	Ui3FiniteObserver* ActiveUi3FiniteObserver() noexcept { return activeObserver.load(std::memory_order_acquire); }
	void NotifyFixtureInteractionReady() noexcept
	{
		if (auto* observer = ActiveUi3FiniteObserver()) observer->NotifyInteractionReady();
	}
	Ui3FiniteObserver::Ui3FiniteObserver(Ui3FinitePublication& publication) noexcept : publication_(&publication)
	{
		ready_.generation = publication.RunSerial();
	}
	bool Ui3FiniteObserver::SnapshotAccepted(Ui3FiniteAccepted& out) const noexcept { return publication_->TryReadStable(out); }
	bool Ui3FiniteObserver::EnableBootstrapBaselineBeforeOwnersStart() noexcept
	{
		if (bootstrapEnabled_ || sealed_ || !frameClosed_ || counters_.frames != 0
			|| ownerReadyFlags_.load(std::memory_order_acquire) != 0
			|| renderOwnerThread_.load(std::memory_order_acquire) != 0
			|| publication_->PublicationSerial() != 0 || publication_->mutationOpen_
			|| publication_->goalKnown_ || publication_->nextRevision_ != 0
			|| publication_->counters_.seen != 0 || publication_->CompletedRevision() != 0
			|| !IsUi3FiniteSignatureValid(publication_->initialStable_)
			|| !FiniteSceneStateSupported(publication_->initialStable_)) return false;
		bootstrapEnabled_ = true;
		return true;
	}
	bool Ui3FiniteObserver::FreezeBootstrapSignature(const Ui3FiniteSignature& signature) noexcept
	{
		if (!bootstrapEnabled_ || bootstrapFrozen_ || !initialPublication_ || !consumed_
			|| publication_->PublicationSerial() != 0 || publication_->mutationOpen_
			|| publication_->goalKnown_ || publication_->nextRevision_ != 0
			|| publication_->counters_.seen != 0 || publication_->CompletedRevision() != 0
			|| (ownerReadyFlags_.load(std::memory_order_acquire) & Ui3FiniteReadyInteraction) != 0
			|| !IsUi3FiniteSignatureValid(signature) || !FiniteSceneStateSupported(signature)) return false;
		// 唯一可变项是实际 Fit 的 zoom；颜色/工具/显示等外来变化不能吸收为基线。
		auto expected = publication_->initialStable_;
		expected.configZoomBits = signature.configZoomBits;
		if (signature.flags != expected.flags || !FrozenFiniteInputsMatch(signature, expected)) return false;
		publication_->initialStable_ = signature;
		bootstrapFrozen_ = true;
		return true;
	}
	bool Ui3FiniteObserver::CopyCompletedOutcomeForCurrentOwner(std::uint64_t run, std::uint64_t step,
		std::uint64_t source, std::uint64_t revision, Ui3FiniteTargetRecord& out) const noexcept
	{
		out = {};
		const auto owner = renderOwnerThread_.load(std::memory_order_acquire);
		if (owner == 0 || owner != GetCurrentThreadId() || sealed_ || !frameClosed_
			|| run == 0 || step == 0 || source == 0 || revision == 0 || run != publication_->RunSerial()) return false;
		for (std::size_t i = 0; i < counters_.retained; ++i)
		{
			const auto& row = records_[i];
			if (row.accepted.runSerial != run || row.accepted.stepId != step
				|| row.accepted.sourceSequence != source || row.accepted.revision != revision) continue;
			const bool scene = row.accepted.scene == Ui3FiniteScene::MainFold || row.accepted.scene == Ui3FiniteScene::DrawAttribute;
			const auto roles = row.accepted.scene == Ui3FiniteScene::DrawAttribute
				? Ui3FiniteAllLayoutRoles & ~Ui3FiniteRoleMask(Ui3PropertyRole::MainClickPulse) : Ui3FiniteAllLayoutRoles;
			if (!scene || row.terminalStatus != Ui3FiniteStatus::CompletedLayoutAndSvg
				|| row.accepted.status != Ui3FiniteStatus::Accepted || row.epoch == 0 || row.surfaceSerial == 0
				|| row.trueBarAttemptSerial == 0 || row.pendingRoles != 0 || row.proofMask != roles
				|| row.requiredSvg == 0 || row.requiredSvg > Ui3SvgCapacity || row.verifiedSvg != row.requiredSvg
				|| row.failedSvg != 0 || row.unverifiedSvg != 0
				|| !IsUi3FiniteSignatureValid(row.accepted.signature)) return false;
			out = row;
			return true;
		}
		return false;
	}
	std::uint32_t Ui3FiniteObserver::RequiredRoles() const noexcept
	{
		return candidate_.accepted.scene == Ui3FiniteScene::DrawAttribute && !initialPublication_
			? Ui3FiniteAllLayoutRoles & ~Ui3FiniteRoleMask(Ui3PropertyRole::MainClickPulse) : Ui3FiniteAllLayoutRoles;
	}
	void Ui3FiniteObserver::BeginFrame(const Ui3FiniteAccepted& accepted, std::uint64_t epoch,
		std::uint64_t attempt, bool initialPublication) noexcept
	{
		if (sealed_) return;
		if (renderOwnerThread_.load(std::memory_order_relaxed) == 0)
			renderOwnerThread_.store(GetCurrentThreadId(), std::memory_order_release);
		if (!frameClosed_) AbortFrame(Ui3FiniteStatus::ResourceUnverified);
		candidate_ = {};
		resource_ = {}; consumedSignature_ = {}; seenRoles_ = lifecycle_ = 0;
		consumed_ = false; frameClosed_ = false; lastOutcome_ = Ui3FiniteStatus::Pending;
		++counters_.frames;
		candidate_.accepted = accepted;
		candidate_.epoch = epoch; candidate_.frameAttemptSerial = attempt;
		initialPublication_ = initialPublication && accepted.stepId == 0 && publication_->PublicationSerial() == 0;
		candidate_.stablePublication = accepted.stepId != 0 && accepted.runSerial == publication_->RunSerial()
			&& accepted.revision != 0 && (accepted.publicationSerial & 1ULL) == 0 && IsUi3FiniteSignatureValid(accepted.signature);
		if (epoch == 0 || attempt == 0) lifecycle_ |= Ui3FiniteLifecycleTargetLost;
		if (previousEpoch_ != 0 && epoch != previousEpoch_)
		{
			ready_.flags &= ~(Ui3FiniteReadyTransaction | Ui3FiniteReadyLayoutStable | Ui3FiniteReadyAnchors);
			ready_.epoch = epoch; ready_.surfaceSerial = 0;
			ready_.timingValid = false; ready_.commitTicks = 0;
			PublishReady(); // 新epoch不能读取旧anchor/像素身份。
		}
		previousEpoch_ = epoch;
		if (candidate_.stablePublication && (trackedRun_ != accepted.runSerial || trackedRevision_ != accepted.revision))
		{
			if (recordIndex_ < counters_.retained && records_[recordIndex_].terminalStatus != Ui3FiniteStatus::CompletedLayoutAndSvg
				&& records_[recordIndex_].terminalStatus != Ui3FiniteStatus::Superseded)
			{
				records_[recordIndex_].terminalStatus = Ui3FiniteStatus::Superseded;
				++counters_.superseded;
			}
			trackedRun_ = accepted.runSerial; trackedRevision_ = accepted.revision; trackedCompleted_ = false;
			++counters_.goalsSeen;
			if (counters_.retained < Ui3FiniteCapacity)
			{
				recordIndex_ = static_cast<std::size_t>(counters_.retained++);
				records_[recordIndex_] = {}; records_[recordIndex_].accepted = accepted;
			}
			else { recordIndex_ = Ui3FiniteCapacity; ++counters_.dropped; }
		}
		else if (candidate_.stablePublication && recordIndex_ == Ui3FiniteCapacity)
		{
			// odd/unknown帧没有slot；同goal恢复后重新绑定旧prefix，不静默丢后续完成。
			for (std::size_t i = 0; i < counters_.retained; ++i)
				if (records_[i].accepted.runSerial == accepted.runSerial && records_[i].accepted.revision == accepted.revision)
				{ recordIndex_ = i; break; }
		}
		else if (!candidate_.stablePublication)
		{
			recordIndex_ = Ui3FiniteCapacity;
			if (!initialPublication_) ++counters_.unverified;
		}
	}
	bool Ui3FiniteObserver::MarkConsumed(const Ui3FiniteSignature& signature, std::uint64_t rootBatch,
		std::uint64_t drawBatch, std::int64_t ticks) noexcept
	{
		if (sealed_ || frameClosed_) return false;
		candidate_.rootBatchRevision = rootBatch; candidate_.drawBatchRevision = drawBatch;
		candidate_.targetConsumedTicks = ticks; consumedSignature_ = signature;
		auto initial = publication_->InitialStableSignature();
		if (initialPublication_ && bootstrapEnabled_ && !bootstrapFrozen_)
			initial.configZoomBits = signature.configZoomBits;
		if (!IsUi3FiniteSignatureValid(signature) || !FiniteSceneStateSupported(signature)
			|| !IsUi3FiniteSignatureValid(initial) || !FrozenFiniteInputsMatch(signature, initial))
		{
			lifecycle_ |= Ui3FiniteLifecycleInterference; ++counters_.unverified; return false;
		}
		if (initialPublication_)
			consumed_ = publication_->PublicationSerial() == 0;
		else
		{
			Ui3FiniteAccepted current;
			consumed_ = candidate_.stablePublication && publication_->StillSameSemanticGoal(candidate_.accepted, current)
				&& current.publicationSerial == candidate_.accepted.publicationSerial
				&& SameUi3FiniteSemanticSignature(signature, candidate_.accepted.signature);
		}
		if (!consumed_) { lifecycle_ |= Ui3FiniteLifecycleInterference; ++counters_.unverified; }
		candidate_.renderDpi = consumed_ ? signature.dpi : 0;
		return consumed_;
	}
	void Ui3FiniteObserver::ObserveProperty(Ui3PropertyRole role, bool active, bool same) noexcept
	{
		if (sealed_ || frameClosed_) return;
		const auto mask = Ui3FiniteRoleMask(role);
		seenRoles_ |= mask;
		if (role == Ui3PropertyRole::Feedback || role == Ui3PropertyRole::Lighting) return;
		if (active || !same) candidate_.pendingRoles |= mask;
	}
	void Ui3FiniteObserver::ObserveLifecycle(std::uint32_t flags) noexcept
	{
		if (sealed_ || frameClosed_) return;
		lifecycle_ |= flags;
		if ((flags & 0xFFFF0000u) != 0)
		{
			ready_.flags &= ~(Ui3FiniteReadyLayoutStable | Ui3FiniteReadyAnchors);
			ready_.timingValid = false; ready_.commitTicks = 0;
			PublishReady();
		}
	}
	void Ui3FiniteObserver::ObserveResources(const Ui3FiniteResourceProof& proof) noexcept
	{
		if (!sealed_ && !frameClosed_) resource_ = proof;
	}
	void Ui3FiniteObserver::ApplyResourceProof() noexcept
	{
		candidate_.requiredSvg = resource_.required; candidate_.verifiedSvg = resource_.verified;
		candidate_.failedSvg = resource_.failed; candidate_.unverifiedSvg = resource_.unverified;
		candidate_.firstUnverifiedSvgTag = resource_.firstUnverifiedSvgTag;
		candidate_.firstUnverifiedReason = resource_.firstUnverifiedReason;
		candidate_.svgProofComplete = resource_.producerPresent && resource_.required > 0 && resource_.required <= Ui3SvgCapacity
			&& resource_.verified == resource_.required && resource_.failed == 0 && resource_.unverified == 0
			&& resource_.revision == candidate_.accepted.revision && resource_.epoch == candidate_.epoch
			&& resource_.surfaceSerial == candidate_.surfaceSerial && resource_.frameAttemptSerial == candidate_.frameAttemptSerial;
	}
	Ui3FiniteCandidate Ui3FiniteObserver::FinalizeResources(const Ui3FiniteCandidate& supplied,
		const Ui3FiniteResourceProof& proof) noexcept
	{
		if (sealed_ || frameClosed_) return candidate_;
		// 这里只能补已经锁存的同一次候选，不能重跑Settle或倒读新业务值。
		const bool same = supplied.accepted.runSerial == candidate_.accepted.runSerial
			&& supplied.accepted.stepId == candidate_.accepted.stepId && supplied.accepted.revision == candidate_.accepted.revision
			&& supplied.epoch == candidate_.epoch && supplied.surfaceSerial == candidate_.surfaceSerial
			&& supplied.frameAttemptSerial == candidate_.frameAttemptSerial && supplied.targetWidth == candidate_.targetWidth
			&& supplied.targetHeight == candidate_.targetHeight && supplied.rootBatchRevision == candidate_.rootBatchRevision
			&& supplied.renderDpi == candidate_.renderDpi
			&& supplied.drawBatchRevision == candidate_.drawBatchRevision && supplied.pendingRoles == candidate_.pendingRoles
			&& supplied.mismatchRoles == candidate_.mismatchRoles && supplied.settled == candidate_.settled;
		resource_ = same ? proof : Ui3FiniteResourceProof{};
		if (!same) ++counters_.invalid;
		ApplyResourceProof();
		return candidate_;
	}
	Ui3FiniteCandidate Ui3FiniteObserver::SettleCandidate(std::uint64_t surface,
		std::uint32_t width, std::uint32_t height, std::int64_t ticks) noexcept
	{
		if (sealed_ || frameClosed_) return candidate_;
		candidate_.surfaceSerial = surface; candidate_.targetWidth = width; candidate_.targetHeight = height;
		candidate_.settledTicks = ticks;
		const bool geometry = surface != 0 && width != 0 && height != 0
			&& width <= 0x7FFFFFFFu && height <= 0x7FFFFFFFu && candidate_.epoch != 0;
		if (!geometry) lifecycle_ |= Ui3FiniteLifecycleTargetLost;
		if (previousSurface_ != 0 && surface != previousSurface_)
		{
			ready_.flags &= ~(Ui3FiniteReadyTransaction | Ui3FiniteReadyLayoutStable | Ui3FiniteReadyAnchors);
			ready_.surfaceSerial = surface; ready_.timingValid = false; ready_.commitTicks = 0;
			PublishReady();
		}
		previousSurface_ = surface;
		const auto required = RequiredRoles();
		candidate_.mismatchRoles = required & ~seenRoles_;
		candidate_.pendingRoles &= required;
		if (lifecycle_ & (Ui3FiniteLifecycleDockPending | Ui3FiniteLifecycleDisplayPending | Ui3FiniteLifecycleInitialPending))
			candidate_.pendingRoles |= Ui3FiniteRoleMask(Ui3PropertyRole::DockDisplay);
		candidate_.settled = consumed_ && geometry && candidate_.mismatchRoles == 0 && candidate_.pendingRoles == 0
			&& (lifecycle_ & 0xFFFF0000u) == 0;
		ApplyResourceProof(); // producer缺席或0 required都不构成SVG证明。
		if (candidate_.settled) ++counters_.layoutSettled;
		return candidate_;
	}
	void Ui3FiniteObserver::StoreOutcome(Ui3FiniteStatus status, bool timed, std::int64_t ticks) noexcept
	{
		lastOutcome_ = status;
		// fixed prefix满后仍保留结果分母；同goal成功不能按每callback重复计数。
		if (status == Ui3FiniteStatus::CompletedLayoutAndSvg && !trackedCompleted_)
			{ trackedCompleted_ = true; ++counters_.completed; }
		else if (status == Ui3FiniteStatus::ResourceUnverified) ++counters_.resourceUnverified;
		else if (status == Ui3FiniteStatus::Superseded) ++counters_.superseded;
		if (recordIndex_ >= counters_.retained) return;
		auto& row = records_[recordIndex_];
		if (row.terminalStatus == Ui3FiniteStatus::CompletedLayoutAndSvg) return;
		row.terminalStatus = status; row.epoch = candidate_.epoch; row.surfaceSerial = candidate_.surfaceSerial;
		row.trueBarAttemptSerial = candidate_.frameAttemptSerial; row.pendingRoles = candidate_.pendingRoles | candidate_.mismatchRoles;
		row.consumedTicks = candidate_.targetConsumedTicks; row.settledTicks = candidate_.settledTicks;
		row.requiredSvg = candidate_.requiredSvg; row.verifiedSvg = candidate_.verifiedSvg;
		row.failedSvg = candidate_.failedSvg; row.unverifiedSvg = candidate_.unverifiedSvg;
		row.firstUnverifiedSvgTag = candidate_.firstUnverifiedSvgTag; row.firstUnverifiedReason = candidate_.firstUnverifiedReason;
		row.failureFlags |= lifecycle_;
		if (status == Ui3FiniteStatus::CompletedLayoutAndSvg)
		{
			row.proofMask = RequiredRoles(); row.finalCommitTicks = timed ? ticks : 0; row.timingValid = timed;
		}
	}
	Ui3FiniteStatus Ui3FiniteObserver::CompleteAttempt(const Ui3FiniteCandidate& supplied, bool committed,
		bool timed, std::int64_t ticks, const Ui3FiniteCommitIdentity& identity) noexcept
	{
		if (sealed_ || frameClosed_) return lastOutcome_;
		frameClosed_ = true;
		if (!committed) { StoreOutcome(Ui3FiniteStatus::Pending); return lastOutcome_; }
		++counters_.commits;
		const bool sameCandidate = supplied.accepted.runSerial == candidate_.accepted.runSerial
			&& supplied.accepted.stepId == candidate_.accepted.stepId && supplied.accepted.revision == candidate_.accepted.revision
			&& supplied.epoch == candidate_.epoch && supplied.surfaceSerial == candidate_.surfaceSerial
			&& supplied.frameAttemptSerial == candidate_.frameAttemptSerial && supplied.targetWidth == candidate_.targetWidth
			&& supplied.targetHeight == candidate_.targetHeight && supplied.pendingRoles == candidate_.pendingRoles
			&& supplied.rootBatchRevision == candidate_.rootBatchRevision && supplied.drawBatchRevision == candidate_.drawBatchRevision
			&& supplied.mismatchRoles == candidate_.mismatchRoles && supplied.stablePublication == candidate_.stablePublication
			&& (initialPublication_ || SameUi3FiniteSemanticSignature(supplied.accepted.signature, candidate_.accepted.signature))
			&& supplied.settled == candidate_.settled && supplied.svgProofComplete == candidate_.svgProofComplete;
		const bool identityValid = identity.epoch == candidate_.epoch && identity.surfaceSerial == candidate_.surfaceSerial
			&& identity.frameAttemptSerial == candidate_.frameAttemptSerial && identity.targetWidth == candidate_.targetWidth
			&& identity.targetHeight == candidate_.targetHeight && identity.epoch != 0 && identity.surfaceSerial != 0;
		bool goalCurrent = false;
		Ui3FiniteStatus outcome = Ui3FiniteStatus::ResourceUnverified;
		if (initialPublication_)
			goalCurrent = publication_->PublicationSerial() == 0;
		else
		{
			Ui3FiniteAccepted current;
			if (!publication_->TryReadStable(current)) outcome = Ui3FiniteStatus::AmbiguousPublication;
			else if (current.runSerial != candidate_.accepted.runSerial || current.revision != candidate_.accepted.revision
				|| current.stepId != candidate_.accepted.stepId || current.sourceSequence != candidate_.accepted.sourceSequence
				|| current.scene != candidate_.accepted.scene)
				outcome = Ui3FiniteStatus::Superseded;
			else goalCurrent = SameUi3FiniteSemanticSignature(current.signature, candidate_.accepted.signature);
		}
		const bool anchors = identity.anchorsValid && identity.anchorMappingSerial != 0
			&& std::isfinite(std::bit_cast<double>(identity.mainAnchorBits[0])) && std::isfinite(std::bit_cast<double>(identity.mainAnchorBits[1]))
			&& std::isfinite(std::bit_cast<double>(identity.drawAnchorBits[0])) && std::isfinite(std::bit_cast<double>(identity.drawAnchorBits[1]));
		bool layoutCurrent = sameCandidate && goalCurrent && candidate_.settled && (lifecycle_ & 0xFFFF0000u) == 0;
		if (initialPublication_ && bootstrapEnabled_ && !bootstrapFrozen_)
			layoutCurrent = layoutCurrent && identityValid && anchors && FreezeBootstrapSignature(consumedSignature_);
		bool validTiming = timed && ticks > 0 && candidate_.targetConsumedTicks > 0 && candidate_.settledTicks > 0
			&& candidate_.accepted.acceptedTicks <= candidate_.targetConsumedTicks
			&& candidate_.targetConsumedTicks <= candidate_.settledTicks && candidate_.settledTicks <= ticks;
		if (timed && !validTiming) ++counters_.invalid;
		if (identityValid)
		{
			++ready_.committedCount;
			ready_.lastCommittedAttempt = identity.frameAttemptSerial; ready_.epoch = identity.epoch;
			ready_.surfaceSerial = identity.surfaceSerial; ready_.targetWidth = identity.targetWidth; ready_.targetHeight = identity.targetHeight;
			ready_.flags = Ui3FiniteReadyTransaction | (layoutCurrent ? Ui3FiniteReadyLayoutStable : 0u);
			ready_.timingValid = validTiming; ready_.commitTicks = validTiming ? ticks : 0;
			ready_.anchorMappingSerial = identity.anchorMappingSerial;
			for (unsigned i = 0; i < 2; ++i)
			{
				ready_.mainAnchorBits[i] = identity.mainAnchorBits[i]; ready_.drawAnchorBits[i] = identity.drawAnchorBits[i];
			}
			if (anchors) ready_.flags |= Ui3FiniteReadyAnchors;
			if (initialPublication_ && layoutCurrent) ready_.initialStableSignature = consumedSignature_;
			PublishReady();
		}
		if (outcome == Ui3FiniteStatus::Superseded || outcome == Ui3FiniteStatus::AmbiguousPublication)
		{
			StoreOutcome(outcome); return outcome;
		}
		if (!layoutCurrent) outcome = candidate_.pendingRoles != 0 ? Ui3FiniteStatus::Pending : Ui3FiniteStatus::ResourceUnverified;
		else if (!initialPublication_ && candidate_.svgProofComplete && identityValid
			&& publication_->NoteCompletedGoal(candidate_.accepted.revision)) outcome = Ui3FiniteStatus::CompletedLayoutAndSvg;
		StoreOutcome(outcome, validTiming, ticks);
		return outcome;
	}
	void Ui3FiniteObserver::AbortFrame(Ui3FiniteStatus status) noexcept
	{
		if (sealed_ || frameClosed_) return;
		frameClosed_ = true; ++counters_.unverified; StoreOutcome(status);
	}
	void Ui3FiniteObserver::NotifyRegistered() noexcept { ownerReadyFlags_.fetch_or(Ui3FiniteReadyRegistered, std::memory_order_release); }
	void Ui3FiniteObserver::NotifyInteractionReady() noexcept { ownerReadyFlags_.fetch_or(Ui3FiniteReadyInteraction, std::memory_order_release); }
	void Ui3FiniteObserver::PublishReady() noexcept
	{
		const auto serial = readySerial_.fetch_add(1, std::memory_order_acq_rel);
		std::atomic_thread_fence(std::memory_order_release);
		std::array<std::uint64_t, ReadyWords> words{};
		words[0] = ready_.generation; words[1] = ready_.committedCount; words[2] = ready_.lastCommittedAttempt;
		words[3] = ready_.epoch; words[4] = ready_.surfaceSerial;
		words[5] = ready_.mainAnchorBits[0]; words[6] = ready_.mainAnchorBits[1];
		words[7] = ready_.drawAnchorBits[0]; words[8] = ready_.drawAnchorBits[1]; words[9] = ready_.anchorMappingSerial;
		words[10] = ready_.targetWidth | (std::uint64_t{ready_.targetHeight} << 32);
		words[11] = ready_.flags | (std::uint64_t{ready_.timingValid ? 1u : 0u} << 32);
		const auto signature = std::bit_cast<std::array<std::uint64_t, 9>>(ready_.initialStableSignature);
		for (std::size_t i = 0; i < signature.size(); ++i) words[12 + i] = signature[i];
		words[21] = std::bit_cast<std::uint64_t>(ready_.commitTicks);
		for (std::size_t i = 0; i < words.size(); ++i) readyWords_[i].store(words[i], std::memory_order_relaxed);
		readySerial_.store(serial + 2, std::memory_order_release);
	}
	bool Ui3FiniteObserver::TryReadReady(Ui3FixtureReadyValue& out) const noexcept
	{
		const auto before = readySerial_.load(std::memory_order_acquire);
		if (before == 0 || (before & 1ULL) != 0) return false;
		std::array<std::uint64_t, ReadyWords> words;
		for (std::size_t i = 0; i < words.size(); ++i) words[i] = readyWords_[i].load(std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_acquire);
		if (readySerial_.load(std::memory_order_acquire) != before) return false;
		Ui3FixtureReadyValue value;
		value.generation = words[0]; value.committedCount = words[1]; value.lastCommittedAttempt = words[2];
		value.epoch = words[3]; value.surfaceSerial = words[4];
		value.mainAnchorBits[0] = words[5]; value.mainAnchorBits[1] = words[6];
		value.drawAnchorBits[0] = words[7]; value.drawAnchorBits[1] = words[8]; value.anchorMappingSerial = words[9];
		value.targetWidth = static_cast<std::uint32_t>(words[10]); value.targetHeight = static_cast<std::uint32_t>(words[10] >> 32);
		value.flags = static_cast<std::uint32_t>(words[11]) | ownerReadyFlags_.load(std::memory_order_acquire);
		value.timingValid = (words[11] >> 32) != 0;
		std::array<std::uint64_t, 9> signature;
		for (std::size_t i = 0; i < signature.size(); ++i) signature[i] = words[12 + i];
		value.initialStableSignature = std::bit_cast<Ui3FiniteSignature>(signature);
		value.commitTicks = std::bit_cast<std::int64_t>(words[21]);
		out = value; return value.generation != 0;
	}
	void Ui3FiniteObserver::SealAfterRenderStopped(Ui3FiniteStatus status) noexcept
	{
		if (sealed_) return;
		if (!frameClosed_) AbortFrame(status);
		for (std::size_t i = 0; i < counters_.retained; ++i)
			if (records_[i].terminalStatus == Ui3FiniteStatus::Pending) records_[i].terminalStatus = status;
		sealed_ = true; ready_.flags |= Ui3FiniteReadyStopped;
		ready_.flags &= ~(Ui3FiniteReadyLayoutStable | Ui3FiniteReadyAnchors);
		PublishReady();
	}

	Ui3FiniteOwnerContext* SetUi3FiniteOwnerContext(Ui3FiniteOwnerContext* next) noexcept
	{
		auto* previous = ownerContext;
		ownerContext = next;
		return previous;
	}
	Ui3FinitePublication::Mutation* CurrentUi3FiniteMutation() noexcept
	{
		return currentMutation && currentMutation->active ? currentMutation : nullptr;
	}
	void MarkCurrentUi3FiniteBusinessAccepted() noexcept
	{
		if (auto* mutation = CurrentUi3FiniteMutation()) mutation->owner->MarkBusinessAccepted(*mutation);
	}
	void RejectCurrentUi3FiniteBusiness(Ui3FiniteStatus status) noexcept
	{
		// 拒绝可能在ClosePenTypeMenu写入之后；延至规范化后的原Request前封口。
		if (auto* mutation = CurrentUi3FiniteMutation()) mutation->rejectedStatus = status;
	}
	void FinishCurrentUi3FiniteAtRenderRequest(const Ui3FiniteSignature& signature) noexcept
	{
		auto* mutation = CurrentUi3FiniteMutation();
		if (!mutation) return;
		if (!mutation->businessAccepted)
		{
			mutation->owner->FinishRejected(*mutation, mutation->rejectedStatus, mutation->witness);
			return;
		}
		std::int64_t ticks = 0;
		if (ownerContext && ownerContext->clocksEnabled)
			ticks = ownerContext->readTicks ? ownerContext->readTicks()
				: std::chrono::steady_clock::now().time_since_epoch().count();
		mutation->owner->FinishAtRenderRequest(*mutation, signature, ticks);
	}

	Ui3FiniteMutationScope::Ui3FiniteMutationScope(Ui3FiniteScene scene, bool enabled) noexcept
	{
		if (!enabled || !ownerContext || !ownerContext->publication || !ownerContext->requestAvailable
			|| ownerContext->request.scene != scene) return;
		const auto request = ownerContext->request;
		ownerContext->requestAvailable = false;
		mutation_ = ownerContext->publication->BeginMutation(request.stepId, scene, request.sourceSequence);
		mutation_.ownerReceiveTicks = request.ownerReceiveTicks;
		previous_ = currentMutation;
		currentMutation = &mutation_;
		installed_ = true;
	}
	Ui3FiniteMutationScope::~Ui3FiniteMutationScope()
	{
		if (!installed_) return;
		if (mutation_.active)
			mutation_.owner->FinishRejected(mutation_, Ui3FiniteStatus::AmbiguousPublication);
		currentMutation = previous_;
	}
	void Ui3FiniteMutationScope::MarkBusinessAccepted() noexcept
	{
		if (mutation_.active) mutation_.owner->MarkBusinessAccepted(mutation_);
	}
	void Ui3FiniteMutationScope::ObserveBusinessWrite() noexcept
	{
		if (mutation_.active) mutation_.owner->ObserveBusinessWrite(mutation_);
	}
	void Ui3FiniteMutationScope::FinishRejected(Ui3FiniteStatus status, Ui3BusinessWriteWitness witness) noexcept
	{
		if (mutation_.active) mutation_.owner->FinishRejected(mutation_, status, witness);
	}
}
