# OpenGL sprite smoothing

Enable **Sprite smoothing (OpenGL)** under Options > Rendering, or set
`sprite_smoothing = true` in the `[general]` configuration section. It is off
by default and the checkbox is disabled for software rendering.

At viewport zoom-in levels (2x and 4x), the OpenGL sprite shader applies one
coverage-preserving Scale2x pass to the original sprite indices. At 4x, each Scale2x
output pixel is doubled; there is no recursive second pass. Neighbour comparisons
are exact palette-index comparisons, before recolouring and transparency.
Only existing source colours are selected: there is no blur, palette replacement,
new artwork, asset conversion, or final-frame resampling.

Sprite origins, draw bounds, clipping, depth, animation and palette remaps are
unchanged. Neighbours are clamped to the individual sprite's native dimensions,
including when its draw rectangle is partly clipped. Transparent source pixels
participate in neighbour comparisons, but a selected index cannot change a source
pixel between transparent and opaque. Such a replacement retains the original
index. This preserves tile joins, path boundaries and layered water masks rather
than independently rounding or eroding the pieces that meet there. Ordinary sprite
draws, including blended sprite masks and water surfaces, use the same rule;
water coverage tags are unchanged, without filtering metadata.

Native-size and zoomed-out views, alternate zoom-out sprites, UI icons, bitmap
glyphs, TTF text and scrolling banner text retain their existing rendering.
Raw-masked draws (including terrain masks) and solid silhouette draws
intentionally remain nearest-neighbour:
they combine separate images or use different coordinate conventions. This can
leave some edges unfiltered. Scale2x is a conservative pixel-art rule, not
antialiasing for every diagonal; opaque colour edges are smoothed, but transparent
silhouettes keep nearest-neighbour coverage. Fine patterns can change shape, and
independently assembled sprites are still filtered within their original bounds.
Lighting, weather, window scaling and indexed screenshot export are unchanged.
Use an actual OpenGL RGB capture to inspect the displayed result with RGB effects.
