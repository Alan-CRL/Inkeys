# 实施计划

## Ordered Work

1. **Preserve the current contracts before edits.** Re-read the relevant Slider/Overflow, Pen Type Menu and Color Picker blocks in `Bar.Main.cpp`; do not change `Bar.State.cppm` unless an additional short-lived state is proven necessary.
2. **Fix the pen-type extension divider.** Separate its render from the extension press transform, target its selected color to `Accent`, and keep its PointLight/third-light configuration unchanged. Verify capability loss still clears geometry, hover and press.
3. **Unify Slider Y without changing Hint lifecycle.** Delete only Position A/B selection state and use the old Position B formula unconditionally. Keep the session lock and Hint-creation gate; manually test all four Hint lifecycle rules before moving on.
4. **Handle Thumb pointer-down as a state transition.** Use the already-calculated Thumb hit in the Slider pointer loop to end the fixed triangle state at down. Adjust mouse-up bookkeeping so an unchanged Thumb click cannot re-pin it, without regressing track click, drag, capture loss or commit-on-release.
5. **Apply the highlighter preset revision.** Change the one DIP array to `35 / 50 / 70`; confirm `GetBarThicknessPresetPx()` remains the only writer conversion and manually compare 100%/high-DPI labels and ink diameter.
6. **Finish Color Picker layout and review its existing motion/lights.** Retain the existing Back/Cubic Popup timeline and local Light 1 exclusion. Replace footer outer padding/field placement with explicit fixed anchors and maximum value slots only where the current layout does not meet the measured padding requirement.
7. **Add triangle rotation.** Drive the thickness and pen-type SVG angle targets from their real stable state and expansion direction. Remove pen-type layout's direct angle overwrite, use the standard angle transition path, and cover mid-animation direction reversal.
8. **Run quality gates.** Inspect the diff for scope and whitespace, run the full ARM64 Solution build, then complete the manual checklist below. Do not modify legacy UI, SVG/resource/shader files as part of validation.

## Regression Checklist

### Pen Type Divider

- [ ] Selected divider is Accent in both themes.
- [ ] Main-area press continues to animate background/icon/text while the divider neither moves nor scales/fades/recolors.
- [ ] Hover, press, selected, disabled and loss of annotation capability do not flash white or leave stale hit regions.

### Thickness Slider And Overflow Hint

- [ ] Thumb press with no movement ends the triangle fixed-expanded state.
- [ ] Thumb press followed by drag works; track click still follows its existing value rule; release/capture cancel ends cleanly.
- [ ] Slider uses the old lower Position B with and without a pre-existing Overflow Hint.
- [ ] Slider cannot create a new Hint; a carried Hint may remain; a no-longer-possible overflow removes it; a removed Hint does not reappear until Preview or Slider -> Preview.
- [ ] Hint Tooltip hover and hit test remain above Slider where required; quick reverse has no visual overlap, stale timer or stale hit region.
- [ ] Quick buttons, divider, Slider endpoints and hold-lock prompt retain prior alignment.

### Highlighter

- [ ] 100% DPI values are `35 / 50 / 70`; 35 and 50 match the Inkeys2 baseline logic, 70 is documented as the new third tier.
- [ ] Values are width/diameter values passed to the existing stroke APIs, not radii.
- [ ] Higher DPI uses `round(DIP * dpiZoom)` once; UI zoom does not multiply the stored width.

### Color Picker

- [ ] Open/close remains a restrained large-Popup bounce rather than a tiny-origin expansion; interrupt/reopen continues from the current progress.
- [ ] R, G, B and Alpha column starts/ends remain fixed as values change; numeric slots remain aligned and adjacent columns do not move.
- [ ] Footer left/right outer padding visually matches its vertical padding, including translated/scaled layouts.
- [ ] Light 1 remains disabled only for this panel; third light remains visible; unrelated Popup lighting remains unchanged.

### Triangle Animation

- [ ] Thickness and pen-type triangles move exactly 180 degrees on each state transition and return on the reverse route.
- [ ] Collapsed points toward the actual Slider/Menu expansion direction; expanded points toward retraction.
- [ ] Pen-type direction uses locked `penTypeMenuOpenBelow`; it remains correct above and below the toolbar.
- [ ] Rapid repeated clicks, mid-animation reverse, side switch and forced Popup close retarget from the current angle without snap, stale animation or 360-degree spin.

## Validation Commands

```powershell
git diff --check
where.exe MSBuild.exe
# Use the ARM64-host MSBuild found above.
<ARM64-MSBuild.exe> InkeysRepo.sln /m /t:Build /p:Configuration=Debug /p:Platform=ARM64
```

Use a timeout of at least five minutes for the full Solution build. The repository has no automated UI animation test target, so record the manual visual/input checks above with the build result.

## Rollback Points

- Keep changes localized to the draw-attribute sections of `Bar.Main.cpp`; no new resource registration or public API is expected.
- If Slider behavior regresses, revert the pointer-state and Position B changes together while retaining the previously verified Overflow Hint lifecycle.
- If an angle target disagrees with visual direction, correct the direction-to-base-angle mapping rather than adding coordinate compensation or 360-degree accumulation.
