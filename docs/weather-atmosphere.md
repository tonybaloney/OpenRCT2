# OpenGL weather atmosphere

In **Options > Display**, select **OpenGL**. In **Options > Rendering**, enable
**Render weather effects**, then **Weather atmosphere (OpenGL)**.
The atmosphere option is off by default and is saved as `weather_atmosphere`
in the `[general]` section of `config.ini`.

Rain adds sparse, translucent foreground streaks at two speeds and slowly
drifting patches of mist. Heavy precipitation increases the effect. Snow uses
a slightly paler mist without rain streaks, retaining the existing snowflakes.
Dry weather is unchanged. Animation follows simulation time, so it pauses with
the park. Mist follows the current palette's luminance to remain subdued at
night without changing the day/night cycle or light balance.

Atmosphere uses the same visible viewport regions as precipitation, excluding
interface windows and toolbars. Turning off weather effects also turns off the
atmosphere. Software rendering is unchanged, and the preference is retained
when switching renderers.

This is a screen-space effect, not distance fog: sprites do not provide geometric
depth for volumetric mist or shelter occlusion. Like the existing rain, it can
appear over roofs. The existing indexed screenshot exporter does not capture
the final OpenGL lighting or atmosphere pass.
