# Russian translation handoff

## Delivered

- `Inkeys/src/i18n/ru-RU.jsonc`: complete Russian translation of all **603** current leaf strings, including the eight newly localized active Setting texts.
- Root `Language` is `ru-RU`. Product naming uses `Inkeys`; community content points to GitHub. Attribution retains the existing author names and dates, with the surrounding text translated. Licensing and platform requirements keep their existing meaning.
- The new file retains the JSONC layout/key order, UTF-8 without BOM and CRLF. No existing language, generated header, snapshot or script was modified in this translation substage. No `sync`, build or GUI was run.

## Terminology

| Concept | Russian |
|---|---|
| Undo / Redo | Отмена / Повтор |
| Restore | Восстановить |
| Freeze | Стоп-кадр; compact main label Стоп |
| Click-through | Сквозной ввод; compact two-line label Пропуск / кликов |
| Whiteboard | Доска |
| Return to whiteboard / slideshow | К доске / К показу |
| End slideshow | Конец (compact button in the presentation context) |
| Hard pen / Soft pen / Highlighter | Ручка / Кисть / Маркер |
| Canvas / Ink annotations | Холст / Рукописные пометки |
| Palm eraser | Стирание ладонью |
| Pen haptic feedback | Тактильный отклик пера |
| Dirty regions | Области перерисовки |
| Rendering | Отрисовка |

Settings explanations are complete sentences; short main-bar labels use familiar compact nouns/actions rather than mechanically expanding English phrases. Only Select/Clear and Click-through use the existing language-specific two-line allowance; the legacy Thickness split follows its baseline newline.

## Checks completed

- Standard `Scripts/i18n.ps1 check`: **ru-RU 603/603 translated, PASS**.
- Additional source-to-output validation: same 603 keys/order as current zh-CN, no missing/extra keys, no unfinished markers or invalid empty translations, identical ordered format tokens, baseline newline counts (except the two permitted short labels), UTF-8 without BOM, CRLF only.
- Recall label is `Восстановить`: no `%` conversions and enough room for the existing fixed timestamp buffer.
- Compared all values to en-US: only **16** are identical. Those are RGB letters, punctuation/connector strings, the official GNU license name, GitHub/GitCode, community path, native-name language-menu labels, Arm64 and plugin product names; no untranslated English prose is used as filler.
- Decimal commas appear only in ordinary explanatory prose where natural in Russian. Actual formatting tokens keep syntax such as `{:.2f}` / `{:.0f}` unchanged.

## Follow-up

- Actual C++ format and DWrite/ImGui slot/glyph checks are prepared in the runtime substage and must run after the full integration build. The translation/script checks do not substitute for those compiled tests.
- If measured bounds expose a short-label overflow, prefer a precise local wording adjustment and record it; do not enlarge the global toolbar grid.
