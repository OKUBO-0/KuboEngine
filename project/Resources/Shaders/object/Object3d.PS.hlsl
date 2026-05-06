#include "Object3d.hlsli"

struct Material
{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;
    float shininess;
};
ConstantBuffer<Material> gMaterial : register(b0); //マテリアルの情報

struct DirectionalLight
{
    //ライトのオンオフ
    float4 color; // ライトの色
    float3 direction; // ライトの向き
    float intensity;
    int enable;
    
};
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1); //ディレクショナルライトの情報

struct pointLight
{
   
    float4 color; //ライトの色
    float3 position; //ライトの位置
    float intensity; //ライトの強さ
    float radius; //ライトの半径
    float decay; //減衰率
    int enable;
       
    
};
ConstantBuffer<pointLight> gPointLight : register(b3); //ポイントライトの情報

struct SpotLght
{
    float4 color; //ライトの色
    float3 position; //ライトの位置
    float intensity; //ライトの強さ
    float3 direction; //ライトの向き
    float distance; //ライトの距離
    float decay; //減衰率
    float consAngle; //スポットライトの余弦
    float cosFalloffstrt;
    int enable;
    
};
ConstantBuffer<SpotLght> gSpotLight : register(b4); //スポットライトの情報

struct Camera
{
    float3 worldPosition;
};
ConstantBuffer<Camera> gCamera : register(b2); //カメラの情報

struct EnvironmentReflectionSetting
{
    float reflectionStrength; // 反射の強さ（0〜1）
    float roughness; // 粗さ → SampleLevel用
    float2 padding; // アライメント調整
};
ConstantBuffer<EnvironmentReflectionSetting> gEnvReflection : register(b5);

TextureCube<float4> gEnvironmentTexture : register(t1); //環境マップのテクスチャ
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    
    float4 transformedUV = mul(float4(input.texcoord, 0.0, 1.0f), gMaterial.uvTransform); //テクスチャ座標の変換
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy); //テクスチャの色を取得
    
    if (textureColor.a <= 0.5)
    {
        discard;
    }
    if (textureColor.a <= 0.0)
    {
        discard;
    }
    
    PixelShaderOutput output;
    
    if (gMaterial.enableLighting != 0)
    {
        
        
        
       //ディレクショナルライト
        // Diffuse
        float NdotL = dot(normalize(input.normal), normalize(-gDirectionalLight.direction)); //ライトの方向と法線ベクトルの内積
        float cos = pow(NdotL * 0.5f + 0.5f, 2.0f); //ライトの方向と法線ベクトルの内積を0~1に変換
        // Specular
        float3 toEye = normalize(gCamera.worldPosition - input.worldPosition); //カメラの位置から頂点の位置へのベクトル
        float3 halfVector = normalize(-gDirectionalLight.direction + toEye); //ハーフベクトル
        float NDotH = dot(normalize(input.normal), halfVector); //ハーフベクトルと法線ベクトルの内積
        float specularPOW = pow(saturate(NDotH), gMaterial.shininess); //ハーフベクトルと法線ベクトルの内積を0~1に変換
        float3 directionLightdiffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity; //ライトの色とテクスチャの色を乗算
        float3 directionLightspecular = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPOW * float3(1.0f, 1.0f, 1.0f); //ライトの色とハーフベクトルと法線ベクトルの内積を乗算

        
        
       //ポイントライト
        // Diffuse
        float3 pointLightDirection = normalize(input.worldPosition - gPointLight.position);
        // 拡散光の計算 (法線とライト方向の内積)
        float NdotL_point = dot(normalize(input.normal), normalize(-pointLightDirection));
        // 0～1の範囲に変換 (負の値は0にする)
        float pointCos = pow(NdotL_point * 0.5f + 0.5f, 2.0f);
        // スペキュラの計算
        // ハーフベクトルを計算 (ライト方向 + 視線方向)
        float3 halfVectorPoint = normalize(-pointLightDirection + toEye);;
        // 法線とハーフベクトルの内積を計算
        float NDotH_point = dot(normalize(input.normal), halfVectorPoint);
        // 0～1の範囲に変換
        float specularPOW_point = pow(saturate(NDotH_point), gMaterial.shininess);
        // ライトからの距離を計算
        float pointlightdistance = length(gPointLight.position - input.worldPosition);
        //逆二乗則による減衰
        float factor = pow(saturate(-pointlightdistance / gPointLight.radius + 1.0), gPointLight.decay);
        float3 pointLightdiffuse = gMaterial.color.rgb * textureColor.rgb * gPointLight.color.rgb * pointCos * gPointLight.intensity * factor;
        float3 pointLightspecular = gPointLight.color.rgb * gPointLight.intensity * specularPOW_point * float3(1.0f, 1.0f, 1.0f) * factor;
        
        //spotlight
        // ディレクションライトの方向
        float3 spotLightDirectionOnSuface = normalize(input.worldPosition - gSpotLight.position);
        // ライトの方向と法線ベクトルの内積
        float NdotL_spot = dot(normalize(input.normal), normalize(-spotLightDirectionOnSuface));
        // ライトの方向と法線ベクトルの内積を0~1に変換
        float spotCos = pow(NdotL_spot * 0.5f + 0.5f, 2.0f);
        // ハーフベクトル
        float3 halfVectorSpot = normalize(-spotLightDirectionOnSuface + toEye);
        // ハーフベクトルと法線ベクトルの内積
        float NDotH_spot = dot(normalize(input.normal), halfVectorSpot);
        // ハーフベクトルと法線ベクトルの内積を0~1に変換
        float specularPOW_spot = pow(saturate(NDotH_spot), gMaterial.shininess);
        // ライトからの距離を計算
        float spotlightdistance = length(gSpotLight.position - input.worldPosition);
        float factor_spot = pow(saturate(-spotlightdistance / gSpotLight.distance + 1.0), gSpotLight.decay);
        // スポットライトの方向とライトの方向の内積を計算
        float cosAngle = dot(spotLightDirectionOnSuface,gSpotLight.direction);
        float falloffFactor = saturate((cosAngle - gSpotLight.consAngle) / (gSpotLight.cosFalloffstrt - gSpotLight.consAngle));
        //スポットライトの減衰率を計算
        float3 spotLightdiffuse = gMaterial.color.rgb * textureColor.rgb * gSpotLight.color.rgb * spotCos * gSpotLight.intensity * factor_spot*falloffFactor;
        float3 spotLightspecular = gSpotLight.color.rgb * gSpotLight.intensity * specularPOW_spot * float3(1.0f, 1.0f, 1.0f) * factor_spot*falloffFactor;
        
        
        // 環境マップのサンプリング
        float3 cameraToPosition = normalize(input.worldPosition - gCamera.worldPosition);
        float3 reflectedVector = reflect(cameraToPosition, normalize(input.normal));
        float lod = saturate(gEnvReflection.roughness) * 6.0f;
        float4 environmentColor = gEnvironmentTexture.SampleLevel(gSampler, reflectedVector, lod); //環境マップの色を取得
       
        
        float3 lighting = float3(0, 0, 0);
        
        if (gSpotLight.enable != 0)
        {
            lighting += spotLightdiffuse + spotLightspecular;
        }

        if (gDirectionalLight.enable != 0)
        {
            lighting += directionLightdiffuse + directionLightspecular;
        }

        if (gPointLight.enable != 0)
        {
            lighting += pointLightdiffuse + pointLightspecular;
        }
        
        

        output.color.rgb = lighting;
        output.color.a = gMaterial.color.a * textureColor.a;

        output.color.rgb += environmentColor.rgb*gEnvReflection.reflectionStrength; //環境マップの色を加算
        
        
        
    }
    else
    {
        output.color = gMaterial.color * textureColor;
    }

    return output;
}
