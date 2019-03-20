#version 330 core
//Based on http://learnopengl.com/#!Advanced-Lighting/Bloom
out vec4 fragColor;
in vec2 fragUV;

uniform sampler2D image;
uniform bool horizontal;

uniform float weight[5] = float[] (0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162);

void main()
{
     vec2 uvOffset = 1.0 / textureSize(image, 0); // gets size of single texel
     vec3 result = texture(image, fragUV).rgb * weight[0];
     if(horizontal)
     {
         for(int i = 1; i < 5; ++i)
         {
            result += texture(image, fragUV + vec2(uvOffset.x * i, 0.0)).rgb * weight[i];
            result += texture(image, fragUV - vec2(uvOffset.x * i, 0.0)).rgb * weight[i];
         }
     }
     else
     {
         for(int i = 1; i < 5; ++i)
         {
             result += texture(image, fragUV + vec2(0.0, uvOffset.y * i)).rgb * weight[i];
             result += texture(image, fragUV - vec2(0.0, uvOffset.y * i)).rgb * weight[i];
         }
     }
     fragColor = vec4(result, 1.0);
}