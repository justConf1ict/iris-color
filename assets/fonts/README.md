# Iris Color typography

Iris Color bundles the unmodified static DM Sans Regular and DM Sans Medium
TrueType fonts from the official DM Fonts project. Both are distributed under
the SIL Open Font License 1.1; see `OFL.txt` for the copyright notice and license.

Source revision: `d0520ba03bd780f5dccb3024854463d44f699b78`

- https://raw.githubusercontent.com/googlefonts/dm-fonts/d0520ba03bd780f5dccb3024854463d44f699b78/Sans/fonts/ttf/DMSans-Regular.ttf
- https://raw.githubusercontent.com/googlefonts/dm-fonts/d0520ba03bd780f5dccb3024854463d44f699b78/Sans/fonts/ttf/DMSans-Medium.ttf
- https://raw.githubusercontent.com/googlefonts/dm-fonts/d0520ba03bd780f5dccb3024854463d44f699b78/Sans/OFL.txt

SHA-256:

```text
ea5c558ae17a64d656ae271c3b16ba1de09a9987fb74630297c8b779d566fd4a  DMSans-Regular.ttf
7b7bc054b9e596c49faac2a7606e6319678a565c976dcb2a190e04c1b31beaa1  DMSans-Medium.ttf
9af36190332437f5ecd09974de43c1f7c77a310a996cdd8ceb25628b458840e1  OFL.txt
```

The Windows bundle must include these files in `Contents/Resources/fonts/`.
The editor loads the fonts into a GDI+ private collection; they are not installed
into the user's system. If the resource files cannot be loaded, the editor uses
Segoe UI, Arial, or the existing system fallback. Native number-entry fields use
the system UI font.
