# Ink Stroke Modeler provenance

This directory contains the Google Ink Stroke Modeler headers used by Draw3.

- Source repository: D:\Project\Inkeys\inkStrokeModelerTest\Inkeys3-Draw3
- Source revision: 8d04529 (feat(eraser): add speed eraser OC smoothing)
- Upstream license: Apache License 2.0 (see ThirdpartyLicenses/Apache License 2.0)
- The product links the fixed-version architecture-specific static libraries
  under inkStrokeModelerTest/lib; modeler and Abseil .cc files are not built.
- The vendored absl headers beside this directory are retained because the
  public Ink Stroke Modeler headers include them directly.

Draw3 product sources are under Inkeys/Inkeys/Drawing/Draw3. The standalone
demo entry point and performance HUD are intentionally not registered in the
Inkeys product project.
