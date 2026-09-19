# Extension icon

`icon.png` is the extension icon shown in the VS Code / VS Codium Extensions
view. It is rendered from `extension.svg` (the design source).

VS Code requires the `icon` in `package.json` to be a PNG of at least
128×128 px, so the SVG cannot be referenced directly. To regenerate the PNG:

```sh
# Source Code Pro is not installed here, so the monospace fallback is used
# for the "dino.dn" wordmark. White background matches the design canvas
# (the SVG fades to transparent at the bottom and the wordmark is black).
sed 's/Source Code Pro/Noto Sans Mono/g' extension.svg > /tmp/dino-icon.svg
rsvg-convert -w 256 -h 256 -b white /tmp/dino-icon.svg -o icon.png
```

`rsvg-convert` (librsvg) is used because it renders the SVG's `<text>`
element; `resvg` drops it.
