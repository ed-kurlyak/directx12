cbuffer cbPerObject : register(b0)
{
	float4x4 gWorldViewPlane;
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

static float3 DiffuseLightColor = { 1.0f, 1.0f, 0.5f };
static float SpotPower = 75.0f;
static float3 LightDirection = { 0.0f, 0.0f, 1.0f };
static float3 LightPosition = { 0.0f, 0.0f, -20.0f };

VertexOut VS(VertexIn vin)
{
	VertexOut vout;
	
	vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);
	
	float4 PosW = mul(float4(vin.PosL, 1.0f), gWorldViewPlane);
	vout.PosW = PosW.xyz;

    //vout.tNormal = mul(vin.Normal, (float3x3)gWorldViewPlane);

   
    return vout;
}

float3 Get_Spot_Light(float3 tNormal, float3 PosW, float3 LightPosW)
{

	float3 ToLightPos = LightPosition - PosW;

	ToLightPos = normalize(ToLightPos);

	LightDirection = normalize(LightDirection);
	
    float SpotFactor = pow(max(dot(-ToLightPos, LightDirection), 0.0f), SpotPower);

	return DiffuseLightColor * SpotFactor;
}

float4 PS(VertexOut pin) : SV_Target
{

	float3 ResColor = Get_Spot_Light(pin.tNormal, pin.PosW, pin.LightPos);
	
	return float4(ResColor, 1.0f);

}


