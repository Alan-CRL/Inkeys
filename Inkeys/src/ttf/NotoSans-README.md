# Embedded Noto Sans JP / KR

The four `.ttf` files are static TrueType instances at weight 400 (Regular) and
700 (Bold). They retain the upstream `Noto Sans JP` / `Noto Sans KR` family names
and are used by the existing DirectWrite and ImGui font loaders.

## Sources and license

- JP: [Google Fonts, commit 66a36c8c](https://github.com/google/fonts/tree/66a36c8c94b1a5d992ee4e7f392fccfe4945767c/ofl/notosansjp).
- KR: [Google Fonts, commit b38c5c93](https://github.com/google/fonts/tree/b38c5c93af322c45f633e17ac440ec1e6c94d489/ofl/notosanskr).
- Each family includes its upstream SIL Open Font License 1.1 in
  `NotoSansJP-OFL.txt` / `NotoSansKR-OFL.txt`; keep those notices with redistributed
  font assets. Copyright and license information are also retained in each font.

## Reproduction

Using fontTools 4.60.1, instantiate the pinned upstream `NotoSansJP[wght].ttf`
and `NotoSansKR[wght].ttf` with `wght=400` and `wght=700`, updating the instance
names. No glyph subsetting is applied. All variation axes are pinned; the output
uses `glyf` outlines with no `fvar` or `gvar` table. This asset preparation is
independent of the application's build dependencies.

The task's `research/build_noto_static.py` reproduces the files;
`research/noto-font-manifest.json` records source and output SHA-256 hashes, sizes,
weights and names under `.trellis/tasks/archive/2026-11/11-01-i18n-eight-locales/`.
