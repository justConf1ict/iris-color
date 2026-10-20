# Iris Color Body — implementation handoff

Read `DESIGN.md`, `design-contract.md`, `plugin/IrisEditorWin.cpp`, `plugin/IrisParameters.h`, and the shared modal-model header before changing the interface.

- Keep 1040×760 logical geometry and bundled DM Sans. Use the palette in DESIGN.md.
- Add Body between Main and Bands + EQ. Keep all old controls and parameter IDs intact.
- Bind material/knobs to IDs 77–82. Range/default authority is `kSpecs`.
- Main: compact clickable material and Amount summary in the spectrum heading row.
- Body: four material selectors, labeled model diagram, five real knobs, shared pitch strip.
- Extend focus order, shortcuts, direct entry, wheel increments, reset behavior, and host edit gestures.
- Use only vector geometry and existing fonts. No external runtime assets or web engine in the VST3.
- Reuse actual modal ratios/loss formulas for the guide; label it as modeled, never measured.
- `body07/index.html` should be self-contained with the bundled DM Sans fonts embedded, responsive to viewport size, and use the exact same units. Mark it as an interface preview with no audio engine.

The first native build must prove: Body controls change the actual parameter values; Amount zero restores the previous processing path; all four pages remain usable; hover and typed entry fit; old sessions keep their original sound. The HTML file is a design handoff, not a VST3 host test.
