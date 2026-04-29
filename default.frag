#version 330 core
// -----------------------------------------------------------------------------
// default.frag — textured surface with mask alpha and multi-light accumulation
// -----------------------------------------------------------------------------
// std140 blocks must match C++ Light layout and binding points set from CPU.

out vec4 FragColor;
in vec2 texCoord;
in vec2 texCoordMask;
in vec3 normal;
in vec3 FragPos;
in float ambientLight;

uniform sampler2D tex0;
uniform sampler2D mask0;

struct Light
{
   vec4 position;
   vec4 color;
   vec4 intensity;
};

layout (std140) uniform BrickLights
{
   Light brickLights[64];
};

layout (std140) uniform ScenarioLights
{
   Light scenarioLights[64];
};

uniform int numScenarioLights;
uniform int numBrickLights;

void main()
{
   vec3 diffuseLight = vec3(0.0);
   vec3 specularLight = vec3(0.0);
   float specularStrenght = 0.5;

   vec3 N1 = normalize(normal);

   vec3 lightVector = vec3(0.0);
   for (int i = 0; i < numScenarioLights; i++)
   {
      lightVector = scenarioLights[i].position.xyz - FragPos;
      vec3 lightDirect = normalize(lightVector);


      diffuseLight += scenarioLights[i].color.rgb *
                 scenarioLights[i].intensity.r *
                 max(dot(N1, lightDirect), 0) / dot(lightVector * 0.8, lightVector);

      vec3 viewDirect = normalize(scenarioLights[1].position.xyz - FragPos);
      vec3 reflectionDirect = reflect(-lightDirect, N1);
      specularLight += pow(max(dot(viewDirect, reflectionDirect), 0.0), 32);
   };
   for (int j = 0; j < numBrickLights-1; j++)
   {
      vec3 lightVector = brickLights[j].position.xyz - FragPos;
      vec3 lightDirect = normalize(lightVector);


      diffuseLight += brickLights[j].color.rgb *
                 brickLights[j].intensity.r *
                 max(dot(N1, lightDirect), 0) / dot(lightVector * 0.8, lightVector);

      vec3 viewDirect = normalize(scenarioLights[1].position.xyz - FragPos);
      vec3 reflectionDirect = reflect(-lightDirect, N1);
      specularLight += pow(max(dot(viewDirect, reflectionDirect), 0.0), 32);
   };


   float ambient = 0.0;
   vec3 finalLight = vec3(pow(ambient + diffuseLight.r*0.5 + specularLight.r*specularStrenght, ambientLight), pow(ambient + diffuseLight.g*0.5 + specularLight.g*specularStrenght, ambientLight), pow(ambient + diffuseLight.b*0.5 + specularLight.b*specularStrenght, ambientLight));

   vec4 textureColor = texture(tex0, texCoord);
   float maskOpacity = texture(mask0, texCoordMask).a;
   FragColor = vec4(textureColor.rgb * finalLight, textureColor.a * maskOpacity);
}
