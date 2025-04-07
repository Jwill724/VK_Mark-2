//struct SceneData
//{
//    float4x4 view;
//    float4x4 proj;
//    float4x4 viewproj;
//    float4 ambientColor;
//    float4 sunlightDirection;
//    float4 sunlightColor;
//};

//struct GLTFMaterialData
//{
//    float4 colorFactors;
//    float4 metal_rough_factors;
//};

//StructuredBuffer<SceneData> sceneDataBuffer : register(t0, space0);

//SceneData GetSceneData()
//{
//    return sceneDataBuffer[0];
//}

//StructuredBuffer<GLTFMaterialData> materialDataBuffer : register(t0, space1);

//GLTFMaterialData GetMaterialData()
//{
//    return materialDataBuffer[0];
//}

//// Binding: set 1, binding 1
//Texture2D colorTex : register(t1, space1);
//SamplerState colorSampler : register(s1, space1);

//// Binding: set 1, binding 2
//Texture2D metalRoughTex : register(t2, space1);
//SamplerState metalRoughSampler : register(s2, space1);