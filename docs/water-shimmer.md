# Water shimmer (OpenGL)

Select **OpenGL** in Options > Display, then enable **Water shimmer (OpenGL)**
under Options > Rendering > Effects. It is off by default and saved as
`water_shimmer` in the `[general]` configuration section. It is independent of
weather effects and lighting. Software rendering is unchanged.

The effect adds a gentle, animated, signed brightness modulation to visible
water surfaces, preserving their palette hue and night brightness. It does not
blur or displace pixels, sample neighbouring scenery, or simulate reflections.
The existing water palette animation and simulation are unchanged. Animation
uses simulation time, so it pauses with the game.

Coverage comes from the actual water mask and water overlay sprites, not their
colours. A coverage bit travels with each indexed pixel through normal sprite
occlusion and transparency compositing. Opaque scenery replaces that bit;
non-water transparent sprites clear it conservatively. Destination-only
rectangle filters, including weather gloom, retain coverage. The final palette
pass excludes the full bounds of interface windows, including transparent ones.
This protects objects above water and interface drawing even when they share the
water palette.
Transparent and opaque water, slope clipping, viewport clipping and zoom use the
same sprite sampling as the original renderer.

The OpenGL indexed buffers use wider integer formats to retain coverage alongside
palette indices (16-bit colour/composite buffers and a 32-bit transparency
buffer). Indexed screenshots strip coverage and continue to export the original
palette image; like lighting and weather atmosphere, the RGB-only shimmer is not
included in that exporter.
