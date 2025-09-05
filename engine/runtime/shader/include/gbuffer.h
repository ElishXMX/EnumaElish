struct PGBufferData
{
    highp vec3  worldNormal;
    highp vec3  baseColor;
    highp float metallic;
    highp float specular;
    highp float roughness;
    highp uint  shadingRenderObjectID;
};

#define SHADINGRenderObjectID_UNLIT 0U
#define SHADINGRenderObjectID_DEFAULT_LIT 1U

highp vec3 EncodeNormal(highp vec3 N) { return N * 0.5 + 0.5; }

highp vec3 DecodeNormal(highp vec3 N) { return N * 2.0 - 1.0; }

highp vec3 EncodeBaseColor(highp vec3 baseColor)
{
    // we use sRGB on the render target to give more precision to the darks
    return baseColor;
}

highp vec3 DecodeBaseColor(highp vec3 baseColor)
{
    // we use sRGB on the render target to give more precision to the darks
    return baseColor;
}

highp float EncodeShadingRenderObjectId(highp uint ShadingRenderObjectId)
{
    highp uint Value = ShadingRenderObjectId;
    return (float(Value) / float(255));
}

highp uint DecodeShadingRenderObjectId(highp float InPackedChannel) { return uint(round(InPackedChannel * float(255))); }

void EncodeGBufferData(PGBufferData   InGBuffer,
                       out highp vec4 OutGBufferA,
                       out highp vec4 OutGBufferB,
                       out highp vec4 OutGBufferC)
{
    OutGBufferA.rgb = EncodeNormal(InGBuffer.worldNormal);

    OutGBufferB.r = InGBuffer.metallic;
    OutGBufferB.g = InGBuffer.specular;
    OutGBufferB.b = InGBuffer.roughness;
    OutGBufferB.a = EncodeShadingRenderObjectId(InGBuffer.shadingRenderObjectID);

    OutGBufferC.rgb = EncodeBaseColor(InGBuffer.baseColor);
}

void DecodeGBufferData(out PGBufferData OutGBuffer, highp vec4 InGBufferA, highp vec4 InGBufferB, highp vec4 InGBufferC)
{
    OutGBuffer.worldNormal = DecodeNormal(InGBufferA.xyz);

    OutGBuffer.metallic       = InGBufferB.r;
    OutGBuffer.specular       = InGBufferB.g;
    OutGBuffer.roughness      = InGBufferB.b;
    OutGBuffer.shadingRenderObjectID = DecodeShadingRenderObjectId(InGBufferB.a);

    OutGBuffer.baseColor = DecodeBaseColor(InGBufferC.rgb);
}