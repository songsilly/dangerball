#version 330

// Input vertex attributes (from vertex shader)
in vec2 fragTexCoord;
in vec4 fragColor;

// Input uniform values
uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Output fragment color
out vec4 finalColor;

// NOTE: Add here your custom variables

// NOTE: Render size values must be passed from code
const float renderWidth = 1280;
const float renderHeight = 512;

vec4 AntiAliasPointSampleTexture_None(vec2 uv, vec2 texsize) {	
	return texture(texture0, (floor(uv+0.5)+0.5) / texsize, -99999.0);
}

vec4 AntiAliasPointSampleTexture_Smoothstep(vec2 uv, vec2 texsize) {	
	vec2 w=fwidth(uv);
	return texture(texture0, (floor(uv)+0.5+smoothstep(0.5-w,0.5+w,fract(uv))) / texsize, -99999.0);	
}

vec4 AntiAliasPointSampleTexture_Linear(vec2 uv, vec2 texsize) {	
	vec2 w=fwidth(uv);
	return texture(texture0, (floor(uv)+0.5+clamp((fract(uv)-0.5+w)/w,0.,1.)) / texsize, -99999.0);	
}

void main()
{
	vec2 uv = fragTexCoord.xy * vec2(renderHeight);


    finalColor =  AntiAliasPointSampleTexture_Smoothstep(uv, vec2(512.0));
}