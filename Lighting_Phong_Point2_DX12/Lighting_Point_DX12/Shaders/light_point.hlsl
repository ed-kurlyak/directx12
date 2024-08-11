cbuffer cbPerObject : register(b0)
{
	float4x4 gWorldView;
};

cbuffer cbPass : register(b1)
{
	float4x4 gWorldViewProj; 
};

struct VertexIn
{
	float3 PosL  : POSITION;
    float3 Normal : NORMAL;
};

struct VertexOut
{
	float4 PosH  : SV_POSITION;
    float3 tNormal : NORMAL;
	float3 PosW : POSITION;
	float3 LightPos: POSITION1;
};

static float3 LightPos = { 0.0f, 10.0f, 0.0f };
static float3 DiffuseLightColor = { 1.0f, 1.0f, 0.5f };
static float3 Att = {0.0f, 0.01f, 0.01f};

VertexOut VS(VertexIn vin)
{
	VertexOut vout;
	
	vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);
	
	float4 PosW = mul(float4(vin.PosL, 1.0f), gWorldView);
	vout.PosW = PosW.xyz;
	
    vout.tNormal = mul(vin.Normal, (float3x3)gWorldView);

    vout.LightPos = mul(float4(LightPos, 1.0f), gWorldView);
    
    return vout;
}

float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    return saturate((falloffEnd-d) / (falloffEnd - falloffStart));
}

float3 Get_Point_Light(float3 tNormal, float3 PosW, float3 LightPosW)
{

	float FallOfStart = 12;
	float FallOfEnd = 16;

	float3 ToLightPos = LightPosW - PosW;

	float Dist = length(ToLightPos);

	if(Dist > FallOfEnd)
		return 0;

	ToLightPos = normalize(ToLightPos);

	float3 Normal = normalize(tNormal);
	float Dot = max(dot(ToLightPos, Normal), 0.0f);

	float AttVal = CalcAttenuation(Dist, FallOfStart, FallOfEnd);

	return DiffuseLightColor * AttVal * Dot;
}

float4 PS(VertexOut pin) : SV_Target
{

	float3 ResColor = Get_Point_Light(pin.tNormal, pin.PosW, pin.LightPos);
	
	return float4(ResColor, 1.0f);

}


