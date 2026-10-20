# Iris Color Body — portable interface specification

`index.html` is a self-contained HTML/CSS/JavaScript artifact with the existing DM Sans fonts embedded. Open it directly in a browser, or use the HTML as a prototype artifact in an OpenDesign project together with `../DESIGN.md`.

This companion previews the Body page. Material selection, five real parameter ranges, direct value entry, fine adjustment, reset, named preset Body values, pitch-set keys, and the displayed bypass state work locally. Other page labels are disabled because those pages belong to the native editor; this file does not pretend to be a complete VST3 host.

The plugin's shared `src/IrisBody.h` formulas are transcribed into the ratio/relative-decay plot. It is a target-model diagram without measured audio or sample-rate exclusions. The material illustration is static. The file does not create, play, record, or upload audio.

The five numeric ranges are Amount 0–100%, Decay 40–1800 ms, Damping 0–100%, Inharmonicity 0–100%, and Strike 0–100%. Reset returns Amount to exactly zero. The internal host parameter remains `kBodyMix` for the visible Amount control.

The four named preview preset values match the Body fields in `src/IrisPresets.h`: Crystal Punch, Chrome Growl, Wooden Pluck, and String Halo. Selecting one here changes only the Body controls represented in the preview.

The official OpenDesign skills used and source provenance are recorded in `../DESIGN.md` and `../design-contract.md`. This artifact was authored using those portable skills; it was not produced by launching the OpenDesign desktop app or daemon.

Embedded fonts: DM Sans Regular / Medium, from the plugin's existing `assets/fonts` directory, licensed under the SIL Open Font License in `assets/fonts/OFL.txt`.
