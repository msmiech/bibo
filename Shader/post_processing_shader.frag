#version 430 core
/*Based on http://learnopengl.com/#!Advanced-Lighting/Bloom
and based on OpenGL SuperBible 5th Edition and https://github.com/openglsuperbible/sb6code/blob/master/bin/media/shaders/hdrtonemap/tonemap_adaptive.fs.glsl
Used for tone mapping and adding bloom to the scene
*/
in vec2 fragUV;

layout(binding = 0) uniform sampler2D sceneTex; //Our rendered scene from main shader as texture (in this sampler)
layout(binding = 1) uniform sampler2D bloomTex;
//uniform vec2 uvOffset[25];
uniform bool hdrEnabled;
uniform bool bloomEnabled;

out vec4 fragColor;

void main()
{
	const float gamma = 2.15;
	vec3 sceneColor = texture(sceneTex, fragUV).rgb;
	if(hdrEnabled) 
	{
		vec2 uvOffsetFactor = vec2(1.0) / textureSize(sceneTex, 0);

		float luminance[25];
		for (int i = 0; i < 25; ++i)
		{
			vec2 uv = (gl_FragCoord.xy + 3.5 * vec2(i % 5 - 2, i / 5 - 2));
			vec3 color = texture(sceneTex, uv * uvOffsetFactor).rgb;
			luminance[i] = dot(color, vec3(0.3, 0.59, 0.11));
		}

		
		// Calculate weighted color of region
		vec3 vColor = texelFetch(sceneTex, 2 * ivec2(gl_FragCoord.xy), 0).rgb;

		float kernelLuminance = (
			(1.0 * (luminance[0] + luminance[4] + luminance[20] + luminance[24])) +
			(4.0 * (luminance[1] + luminance[3] + luminance[5] + luminance[9] + luminance[15] + luminance[19] + luminance[21] + luminance[23])) +
			(7.0 * (luminance[2] + luminance[10] + luminance[14] + luminance[22])) +
			(16.0 * (luminance[6] + luminance[8] + luminance[16] + luminance[18])) +
			(26.0 * (luminance[7] + luminance[11] + luminance[13] + luminance[17])) +
			(41.0 * luminance[12])
		) / 273.0;

		if(bloomEnabled) 
		{	//Additive blending
			vec3 bloomColor = texture(bloomTex, fragUV).rgb;
			sceneColor += bloomColor;
		}
		// Reinhard tone mapping
		float exposure = sqrt(8.0 / (kernelLuminance + 0.25));
		exposure = clamp(exposure, 0.02f, 20.0f);

		vec4 hdrResult = vec4(1.0);
		hdrResult.rgb = 1.0 - exp2(-sceneColor * exposure);
		hdrResult.a = 1.0;
		// Gamma correction  
		hdrResult = pow(hdrResult, vec4(1.0 / gamma));
		fragColor = hdrResult, 1.0f;
	}
	else
    {
		//Only gamma correction
        vec3 result = pow(sceneColor, vec3(1.0 / gamma));
        fragColor = vec4(result, 1.0);
    }
}