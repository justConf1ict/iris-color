# Iris Color Body — design decision record

## Goal and target

Upgrade the existing Windows x64 VST3 effect with a clear and lively Body page while preserving familiar musical workflows. The audience is a musician adjusting a plugin inside a DAW. The native editor is the production surface. The HTML file is a reusable OpenDesign-compatible interface specification.

## Evidence

| Evidence | Confidence | Decision |
| --- | --- | --- |
| User asks for OpenDesign, simple lively color, clean typography, and Autochroma-like ease | Provided | Apply official portable OD skills; use restrained warm colors and direct controls |
| Official Autochroma product image | Observed | Warm neutral field, compact values, color tied to sound roles |
| Iris Color 0.6 editor and typography source | Observed | Preserve 1040×760 scaling, DM Sans resources, existing gestures, and pitch strip |
| Six new Body parameters supplied for 0.7 | Provided | Bind all new controls to actual IDs 77–82 |
| Model is a physically inspired modal bank | Provided | Explicit `Modeled partials` label and cautious material terminology |
| Mint rather than beige and violet Glass identity | Inferred | Give Iris Color its own light, musical visual identity |

## Reference boundaries

| Reference | Keep | Change | Do not copy |
| --- | --- | --- | --- |
| Autochroma | Warm base, compact numbers, quiet functional colors, friendly density | Iris brand, modal response, page organization, controls, typography details | Logo, screenshots, exact grain timeline, proprietary assets, product claims |
| Iris Color 0.6 | Existing gesture contracts, font files, fixed keyboard placement, all old controls | Color tokens, page toolbar, Main summary, dedicated Body page | No need to retain unnecessary visual noise |
| OpenDesign | Explicit design contract, real UI states, semantic tokens, review gates | Adapt web-specific defaults to native GDI+ | Do not claim the OD GUI/daemon was run |

## Final direction

A mint-and-cream desktop sound instrument with dark green ink, a quiet glass-violet response diagram, and five large, evenly spaced Body controls. Material selections are readable buttons rather than ornamental cards. Amount is the entry point to the layer. Main surfaces the layer state, and the shared pitch set anchors every page.

## Risks and unknowns

The model chart must agree with shared DSP formulas. If a response cannot be derived, show a labeled structural diagram instead of an invented measurement. New controls must preserve automation gestures and old project restoration. Native Windows / DAW behavior needs the parent task's build and host verification; a visual preview alone does not establish plugin correctness.

## Quality gates

- Nine required OpenDesign headings exist in DESIGN.md.
- Material names and five numeric controls match actual DSP units and defaults.
- All original pages and controls remain reachable.
- Empty / Amount-zero and active states communicate clearly.
- Text, values, and focus states fit at normal and supported host scales.
- Material view is described as a model, and HTML as a UI preview.
- Every new parameter reaches host begin/edit/end paths and keyboard navigation.
- Diagram uses real model data when describing partial frequencies or losses.
- The parent task receives implementation files and verification evidence before claiming completion.
