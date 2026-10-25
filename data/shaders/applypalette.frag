#version 330 core

uniform vec4 uPalette[256];
uniform usampler2D uTexture;
uniform usampler2D uLightTexture;
uniform usampler2D uLightPalette;
uniform bool uLightingEnabled;
uniform usampler2D uWeatherTexture;
// Mist opacity, foreground rain opacity, simulation time, snow.
uniform vec4 uWeather;
// Enabled, simulation time.
uniform vec2 uWaterShimmer;

in vec2 fTextureCoordinate;

out vec4 oColour;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(cell), hash(cell + vec2(1.0, 0.0)), f.x),
               mix(hash(cell + vec2(0.0, 1.0)), hash(cell + vec2(1.0, 1.0)), f.x), f.y);
}

float rainLayer(vec2 pixel, float scale, float speed)
{
    vec2 p = vec2(pixel.x + pixel.y * 0.24, pixel.y - uWeather.z * speed) / scale;
    vec2 cell = floor(p / vec2(29.0, 71.0));
    vec2 f = mod(p, vec2(29.0, 71.0));
    float seed = hash(cell);
    float streak = 1.0 - smoothstep(0.25, 1.1, abs(f.x - (3.0 + seed * 23.0)));
    streak *= smoothstep(0.0, 3.0, f.y) * (1.0 - smoothstep(5.0, 11.0, f.y));
    return streak * step(0.55, seed);
}

void main()
{
    uint surface = texture(uTexture, fTextureCoordinate).r;
    uint index = surface & 0xffu;
    vec4 colour = uPalette[index];
    if (uLightingEnabled)
    {
        // Light-map rows start at the top of the screen, unlike framebuffer textures.
        uint intensity = texture(uLightTexture, vec2(fTextureCoordinate.x, 1.0 - fTextureCoordinate.y)).r;
        uvec3 light = texelFetch(uLightPalette, ivec2(int(index), 0), 0).rgb;
        uvec3 base = uvec3(round(colour.rgb * 255.0));
        uvec3 exposure = (light * intensity * 6u) >> 8u;
        uvec3 lit = base + ((uvec3(255u) - base) * exposure) / (uvec3(255u) + exposure);
        colour.rgb = vec3(lit) / 255.0;
    }
    if (uWaterShimmer.x > 0.0 && (surface & 0x100u) != 0u)
    {
        vec2 pixel = fTextureCoordinate * vec2(textureSize(uTexture, 0));
        float time = uWaterShimmer.y;
        float wave = sin(pixel.y * 0.42 + sin(pixel.x * 0.065 + time * 0.7) * 1.7 - time * 1.5);
        float broken = smoothstep(0.42, 0.78, noise(pixel / vec2(23.0, 7.0) + vec2(time * 0.16, 0.0)));
        // Small signed modulation preserves the palette's hue and night brightness.
        float shimmer = wave * broken * 0.055;
        colour.rgb = clamp(colour.rgb * (1.0 + shimmer), 0.0, 1.0);
    }
    if (uWeather.x > 0.0)
    {
        vec2 uv = vec2(fTextureCoordinate.x, 1.0 - fTextureCoordinate.y);
        if (texture(uWeatherTexture, uv).r != 0u)
        {
            vec2 pixel = uv * vec2(textureSize(uWeatherTexture, 0));
            vec2 drift = vec2(uWeather.z * 0.045, -uWeather.z * 0.018);
            float wisps = noise(pixel / vec2(190.0, 85.0) + drift);
            wisps = wisps * 0.7 + noise(pixel / vec2(83.0, 47.0) - drift * 0.6) * 0.3;
            float mist = smoothstep(0.35, 0.85, wisps) * uWeather.x;
            // Follow the existing palette's luminance instead of turning night fog white.
            float luminance = dot(colour.rgb, vec3(0.2126, 0.7152, 0.0722));
            vec3 haze = mix(vec3(0.46, 0.52, 0.57), vec3(0.64, 0.68, 0.72), uWeather.w);
            colour.rgb = mix(colour.rgb, haze * (0.25 + luminance * 0.75), mist);
            if (uWeather.y > 0.0)
            {
                float rain = rainLayer(pixel, 1.0, 145.0) * 0.65 + rainLayer(pixel + 37.0, 1.6, 210.0);
                vec3 rainColour = vec3(0.60, 0.69, 0.76) * (0.35 + luminance * 0.65);
                colour.rgb = mix(colour.rgb, rainColour, min(1.0, rain) * uWeather.y);
            }
        }
    }
    oColour = colour;
}
