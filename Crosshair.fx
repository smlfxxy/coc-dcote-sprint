// Crosshair.fx — simple centered crosshair / aim dot with a built-in toggle key.
// Made for Call of Cthulhu: DCotE (works in any game via ReShade).
// Toggle key default = X. Change `keycode` below to another VK code if needed
// (e.g. 0x43='C', 0x56='V', 0x42='B', 0x09=Tab). VK list: docs.microsoft.com Virtual-Key Codes.

#include "ReShade.fxh"

uniform float Toggle <
    source = "key"; keycode = 0x58; mode = "toggle";   // 0x58 = X
>;

uniform bool AlwaysOn <
    ui_label = "Always On (ignore toggle key)";
> = false;

uniform int Shape <
    ui_type = "combo";
    ui_items = "Dot\0Cross\0Dot + Cross\0";
    ui_label = "Shape";
> = 0;

uniform float Size <
    ui_type = "slider"; ui_min = 1.0; ui_max = 24.0; ui_step = 0.5;
    ui_label = "Size (pixels)";
> = 3.0;

uniform float Thickness <
    ui_type = "slider"; ui_min = 1.0; ui_max = 8.0; ui_step = 0.5;
    ui_label = "Cross thickness (pixels)";
> = 1.5;

uniform float Opacity <
    ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
    ui_label = "Opacity";
> = 0.6;

uniform float3 Color <
    ui_type = "color";
    ui_label = "Color";
> = float3(1.0, 1.0, 1.0);

float3 PS_Crosshair(float4 vpos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 col = tex2D(ReShade::BackBuffer, uv).rgb;

    if (!AlwaysOn && Toggle < 0.5)
        return col;

    float2 px = uv * ReShade::ScreenSize;
    float2 c  = ReShade::ScreenSize * 0.5;
    float2 d  = abs(px - c);

    float mask = 0.0;

    // Dot
    if (Shape == 0 || Shape == 2)
    {
        float dist = length(px - c);
        mask = max(mask, 1.0 - smoothstep(Size - 1.0, Size + 1.0, dist));
    }
    // Cross
    if (Shape == 1 || Shape == 2)
    {
        float h = (1.0 - smoothstep(Thickness, Thickness + 1.0, d.y)) *
                  (1.0 - smoothstep(Size, Size + 1.0, d.x));
        float v = (1.0 - smoothstep(Thickness, Thickness + 1.0, d.x)) *
                  (1.0 - smoothstep(Size, Size + 1.0, d.y));
        mask = max(mask, max(h, v));
    }

    return lerp(col, Color, saturate(mask) * Opacity);
}

technique Crosshair <
    ui_label = "Crosshair (center aim dot)";
    ui_tooltip = "Centered aim dot/cross. Press X in-game to toggle (configurable).";
>
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader  = PS_Crosshair;
    }
}
