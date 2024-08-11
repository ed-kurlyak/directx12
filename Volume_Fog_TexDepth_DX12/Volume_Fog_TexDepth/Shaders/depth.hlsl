Texture2D    gDiffuseMap : register(t0);

SamplerState gsamPointWrap  : register(s0);
SamplerState gsamPointClamp  : register(s1);
SamplerState gsamLinearWrap  : register(s2);
SamplerState gsamLinearClamp  : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp  : register(s5);

//static float fZFar = 100;

cbuffer cbPerObject : register(b0)
{
	float4x4 gWorld; 
};

cbuffer cbPass : register(b1)
{
	float4x4 gViewProj; 
	float gZFar;
};

struct VertexIn
{
	float3 PosL  : POSITION;
};

struct VertexOut
{
	float4 PosH  : SV_POSITION;
	float TexDepth : TEXCOORD;
};


VertexOut VS(VertexIn vin)
{
	VertexOut vout;
	
	float4 Pos = mul(float4(vin.PosL, 1.0f), gWorld);

	// Transform to homogeneous clip space.
	vout.PosH = mul(Pos, gViewProj);

	vout.TexDepth = vout.PosH.w / gZFar;
    
    return vout;
}


float PS(VertexOut pin) : SV_Depth
{
	return pin.TexDepth;
}

