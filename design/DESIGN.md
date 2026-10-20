# Iris Color — Body 0.7 design system

This contract applies the official OpenDesign `reference-design-contract` and `frontend-design` skills to Iris Color's existing Windows VST3 editor. It is a native GDI+ implementation with an accompanying HTML specification. The OpenDesign desktop app and daemon were not launched in this environment.

## 1. Visual Theme & Atmosphere

A small, inviting sound instrument: warm mint and cream, precise numbers, quiet botanical colors, and one material diagram with a clear relationship to the controls. The window should feel easy to play with during a music session. Preserve the existing Iris Color identity and the familiar three pages while giving the new Body layer its own page.

Reference evidence: the official Autochroma image uses a warm neutral field, dark legible copy, compact numeric controls, and restrained colors that identify signals. Borrow the color temperature, low ornament, and direct interaction. Iris Color keeps its own topology, brand, copy, signal visual, and DSP semantics.

## 2. Color

| Role | Hex | Use |
| --- | --- | --- |
| Background | `#F0F4EB` | Warm mint window shell |
| Surface | `#FAFAF4` | Cream plotting and control fields |
| Inset | `#E6EEE0` | Quiet substructure and value entry |
| Divider | `#C7D4C5` | Nonessential separating lines |
| Primary ink | `#283C34` | Every primary label and numeric value |
| Secondary ink | `#54685B` | Secondary labels and help |
| Quiet ink | `#5A6B60` | Units, captions, and scale labels |
| Signal | `#48795E` | Processed spectrum and active musical state |
| Signal tint | `#77A289` | Restrained filled areas |
| Glass | `#746492` | Glass model and Body accent |
| Strike | `#A65746` | Strike / metallic response detail |
| Warm response | `#A5833B` | Gate, wood, and response markers |

Use neutral surfaces for at least 85% of visible pixels. The material hues are functional identities, overriding the generic OpenDesign one-accent guideline for this explicitly colorful musical tool. Numeric values remain primary ink. Labels must not depend on hue to identify a state. Pale divider lines are never the only sign that a control can be used; focused controls get a clear dark outline and readable label.

## 3. Typography

Use the existing privately bundled DM Sans Regular / Medium. Keep the current Segoe UI → Arial → Liberation Sans fallback chain. Do not add a font download or a new runtime dependency.

Use the editor's existing type roles: 29 px brand/page title, 21 px parameter values, 17 px subheading, 14–15 px labels, 13 px supporting copy, 44 px only for the primary Color macro. Body Amount may use the 29 px value role. Favor sentence case; avoid decorative all-caps copy. Keep units separate from the value and allow enough horizontal room for `1800 ms`, `100%`, and `Inharmonicity`.

## 4. Spacing & Grid

The logical canvas remains **1040 × 760**. The host scale multiplies the entire layout; pointer coordinates map back to the same logical space. Use 24–32 px outside padding, 8 px small gaps, and 16–24 px between related groups. Corners are 4–7 px, with no pill-shaped card stack or drop-shadow hierarchy.

| Area | Logical region |
| --- | --- |
| Brand, pages, preset, bypass | y 23–86 |
| Main / Bands / Motion visual | Existing coordinates preserved |
| Body title and material selector | y 100–163 |
| Body material and partials diagram | x 24, y 178, w 992, h 234 |
| Five Body controls | y 438–586 |
| Shared pitch controls and keyboard | y 610–696 |
| Contextual help and Soft limit | y 721–750 |

## 5. Layout & Composition

The top row exposes four pages: **Main · Body · Bands + EQ · Motion**. Main retains its primary Color macro and every original sound control. A compact link in the spectrum header summarizes the Body material and Amount so an active layer is visible from Main.

Body is a single focused screen. Four equal material selectors sit above one model diagram. A five-control row contains Amount, Decay, Damping, Inharmonicity, and Strike. Amount is slightly larger because it brings the independent layer into the sound. The shared pitch set stays in the same position on every page.

The diagram identifies itself as a model view. It may show actual material mode relationships from the shared DSP model and the current selected pitch set. It must not pretend to be a measured Body spectrum, a time trace of a simulation, or a calibrated meter. Main continues to show the existing measured spectrum. No free-running decorative animation is needed.

## 6. Components

| Control | Parameter | Range / default | Behavior |
| --- | --- | --- | --- |
| Material | `kBodyMaterial` (77) | Glass / Metal / Wood / String; Glass | Four-way selector with readable selected state |
| Amount | `kBodyMix` (78) | 0–100%; 0% | Set the additive Body strength; 0 preserves the prior sound |
| Decay | `kBodyDecayMs` (79) | 40–1800 ms; 360 ms | Modal ring time |
| Damping | `kBodyDamping` (80) | 0–100%; 45% | Higher partial loss |
| Inharmonicity | `kBodyInharmonicity` (81) | 0–100%; 25% | Mode spacing / stretch |
| Strike | `kBodyStrike` (82) | 0–100%; 40% | Excitation character |

The old parameter IDs and controls remain intact. Body is optional; do not invent a second global Mix control because Color already blends dry/wet up to 100%. Use real parameter units in both native UI and HTML preview. Keep controls adjustable at Amount 0 so the user can prepare a sound before bringing it in.

## 7. Motion & Interaction

Continue the existing mouse drag, wheel, Shift fine adjustment, double-click reset, right-click numeric entry, Enter to apply, and Escape to cancel. The new controls use the same host begin/edit/end gestures. Add the Body material selector to keyboard focus order. Arrow keys choose an adjacent material, while Enter opens its menu. Ctrl+1 / 2 / 3 / 4 select Main / Body / Bands + EQ / Motion. Page switches cancel unfinished input and end active gestures.

Show hover/focus feedback immediately. Body diagrams redraw only when parameters, tuning, or existing visual data change. Do not add elastic transitions, continuous particles, mouse-follow effects, or fake audio motion. Native value entry must fit at 100%, 125%, and 150% scale.

## 8. Voice & Brand

Use short, musical, concrete copy: `Body`, `Glass`, `Metal`, `Wood`, `String`, `Modeled partials`, `Amount 0% · ready`, `Choose your notes`. Use the footer to explain the focused control in one sentence.

Describe the sound as a **physically inspired modal model**. String uses a harmonic modal approximation. Do not claim a full physical simulation, wave-equation solver, measured material response, or complete instrument model. The HTML companion must clearly state that it is an interactive interface preview without an audio engine.

## 9. Anti-patterns

No neon or purple-to-blue gradients; no generic glass cards; no enormous headline above a working plugin; no decorative wave background; no SaaS feature tiles or fabricated metrics; no 3D-looking rotary knobs; no rasterized text; no colored values with insufficient contrast; no fake controls; no idle waveform that implies audio is playing. Do not copy Autochroma's screenshot, logo, exact grain display, or proprietary UI assets into Iris Color.

### Source provenance

- Canonical OpenDesign source: https://open-design.ai/official/
- Official repository: https://github.com/nexu-io/open-design — **102,580 stars**, observed through the GitHub repository API on 2026-10-20.
- Applied design contract skill: https://github.com/nexu-io/open-design/blob/main/skills/reference-design-contract/SKILL.md
- Applied frontend skill: https://github.com/nexu-io/open-design/blob/main/skills/frontend-design/SKILL.md
- Required craft references: https://github.com/nexu-io/open-design/blob/main/craft/typography.md ; https://github.com/nexu-io/open-design/blob/main/craft/color.md ; https://github.com/nexu-io/open-design/blob/main/craft/anti-ai-slop.md
- Reference product: https://imagi.ro/product/autochroma
- Inspected reference image: https://imagi.ro/img/autochroma/main.png
- Existing implementation evidence: `plugin/IrisEditorWin.cpp`, `plugin/IrisTypography.h`, and `plugin/IrisParameters.h` from Iris Color 0.6.

The portable OpenDesign skills were downloaded and read directly from the official repository. The `design-md` entry was inspected but is only a catalog pointer; its full upstream workflow was not executed. OpenDesign's closed-vocabulary `design-brief` was inspected but not applied because this existing musical interface has its own binding palette and parameter schema.
