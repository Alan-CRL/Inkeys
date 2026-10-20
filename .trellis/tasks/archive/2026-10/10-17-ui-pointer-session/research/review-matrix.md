# Integration review targets

1. Raw release can arrive before Bar consumes legacy Down; Begin must not clear it or fabricate held forever. Raw Down/Up order must distinguish quick next gesture from old late legacy Up; Down hit only own window, no global background-click actions.
2. Mouse->Touch takeover cancels old business before new touch hit. Mouse movement/up during Touch is ignored. Touch promoted raw/legacy duplicates cannot start second Mouse session; valid null-device precision-touchpad raw must remain accepted.
3. WM_CANCELMODE/destroy/overflow/no secure desktop may not be delivered as normal successful Up. Audit old loops that commit on !lbutton without explicit cancel test; pressed visuals always clear even if wait returns false.
4. Existing QueueBarThicknessSliderEnd/QueueBarColorPickerEnd synthesize normal mouse Up from cursor; must not accidentally commit mouse or touch on capture cancellation. Raw data must never travel through Window::Enqueue.
5. PageControl has distinct committed and pending pair position; new Up must preserve final pending fallback, cancellation restores only correct committed layout. No save-on-Up, no new blocking owner lock in presentation path.
6. Scene/Whiteboard/hide modes may change on rendering side while input source stays down. Cancel/wake must close raw ownership/repeat without waiting for next input. Capture release reenters WM_CAPTURECHANGED; clear state and release locks before it, don't cancel newly created session.
7. FineDial backlog must use sample time for speed, wall clock for idle hold/dwell. Release emits inertia after ownership freed. Move merge cannot erase max excursion then reinterpret drag as tap.
8. Finishing/cancelling prior interaction must preserve next queued Down; no blanket EM_MOUSE Clear. Terminal screen point comes from accepted source sample (including touch), not current system cursor.
9. Shared registration independent of light observer. Light dormancy must still avoid global rendering wake; provider only wakes active input consumer. Shutdown/sink replacement unregister once; no callback under provider mutex; post failures cancel bounded state.
10. No edits to MouseHook/keyboard hook or their install/uninstall calls, no Draw3/COM/config behavior changes; existing fixture path remains coherent (without running GUI fixtures).
