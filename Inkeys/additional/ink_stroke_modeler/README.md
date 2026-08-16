# Ink Stroke Modeler provenance

This directory is an in-tree copy of the Google Ink Stroke Modeler sources
used by Draw3.

- Source repository: D:\Project\Inkeys\inkStrokeModelerTest\Inkeys3-Draw3
- Source revision: 8d04529 (feat(eraser): add speed eraser OC smoothing)
- Upstream license: Apache License 2.0 (see ThirdpartyLicenses/Apache License 2.0)
- The product project compiles the modeler and its vendored Abseil sources
  directly; no prebuilt architecture-specific library is required.
- The vendored absl headers beside this directory are retained because the
  public Ink Stroke Modeler headers include them directly.

Draw3 product sources are under Inkeys/Inkeys/Drawing/Draw3. The standalone
demo entry point and performance HUD are intentionally not registered in the
Inkeys product project.
