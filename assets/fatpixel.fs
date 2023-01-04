#version 330

uniform vec2 resolution;
uniform float scale;
// Input vertex attributes (from vertex shader)
in vec2 fragTexCoord;
in vec4 fragColor;

// Input uniform values
uniform sampler2D texture0;
uniform vec4 colDiffuse;

//uniform vec4 colDiffuse;

// Output fragment color
out vec4 finalColor;

// NOTE: Add here your custom variables

void main()
{
    vec2 pixel = fragTexCoord * resolution;

    //float scale = 2.0;
    //pixel = ((pixel + offset) - center) / scale + center;
    
    // emulate point sampling
    vec2 uv = floor(pixel) + 0.5;
    
    // subpixel aa algorithm (COMMENT OUT TO COMPARE WITH POINT SAMPLING)
    uv += 1.0 - clamp((1.0 - fract(pixel)) * scale, 0.0, 1.0);

    // output
   	finalColor = texture(texture0, uv/resolution);
    //finalColor = texture(texture0, uv/fragTexCoord);
}