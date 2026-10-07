// Playdate's original viewer: prefiltered panorama + SH diffuse + split-sum
// BRDF. Input colors stay in the source's gamma space until its conversion.
// B^-1 maps UE world directions back to the original Three world directions.
struct DeviceLighting {
    float3 Decode(float4 p) {
        float Le=p.z*255.0+p.w;
        float Y=exp2((Le-127.0)*.5);
        float Z=Y/max(p.y,1e-6);
        float X=p.x*Z;
        return max(float3(6.0013*X-1.332*Y+.3007*Z,
                         -2.700*X+3.1029*Y-1.088*Z,
                         -1.7995*X-5.7720*Y+5.6268*Z),0);
    }
    float2 AtlasUV(float level, float2 baseUV) {
        float width=exp2(10.0-level);
        float height=width*.5;
        return float2((1.0+baseUV.x*(width-2.0))/1024.0,
                      (1.0+(1.0-baseUV.y)*(height-2.0))/1024.0+exp2(-level)*.5);
    }
    float3 Prefilter(Texture2D env, SamplerState samp, float3 dir, float rough) {
        float x=clamp(dir.x/max(length(dir.xz),1e-7),-1.0,.999999);
        float2 uv=acos(float2(x,clamp(dir.y,-1.0,1.0)))/3.14159265359;
        uv.x=dir.z>0 ? uv.x*.5 : 1.0-uv.x*.5;
        uv.x=frac(uv.x+.75); uv.y=1.0-uv.y;
        float lod=sqrt(saturate(rough))*5.0;
        float3 a=Decode(Texture2DSampleLevel(env,samp,AtlasUV(floor(lod),uv),0));
        float3 b=Decode(Texture2DSampleLevel(env,samp,AtlasUV(ceil(lod),uv),0));
        return lerp(a,b,frac(lod));
    }
};
DeviceLighting lighting;
float3 n=normalize(float3(N.y,N.z,-N.x));
float3 ng=normalize(float3(Ng.y,Ng.z,-Ng.x));
float3 v=normalize(float3(V.y,V.z,-V.x));
// The source replaces the yellow areas of the albedo with #F7B529.
float yellowMask=step(.5,1.0-distance(Albedo,float3(1,1,0)));
float3 gammaAlbedo=lerp(Albedo,float3(247,181,41)/255.0,yellowMask);
float3 albedo=gammaAlbedo*(gammaAlbedo*(gammaAlbedo*.305306011+.682171111)+.012522878);
float metal=saturate(Packed.r*MetalFactor);
float rough=max(.0001,1.0-Packed.b*GlossFactor);
float ao=lerp(1.0,saturate(Packed.g),.8);
// SH_COEFFICIENTS is replaced with the original nine normalized coefficients.
SH_COEFFICIENTS
float3 irradiance=max(sh[0]+sh[1]*n.y+sh[2]*n.z+sh[3]*n.x+
    sh[4]*n.y*n.x+sh[5]*n.y*n.z+sh[6]*(3*n.z*n.z-1)+
    sh[7]*n.z*n.x+sh[8]*(n.x*n.x-n.y*n.y),0);
float3 diffuse=albedo*(1-metal)*irradiance*ao;
float NoV=saturate(dot(n,v));
float3 r=normalize(2.0*NoV*n-v);
float smooth=1.0-rough;
r=lerp(n,r,smooth*(sqrt(smooth)+rough));
float3 prefiltered=lighting.Prefilter(Environment,EnvironmentSampler,r,rough);
float frontFactor=clamp(1.0+1.3*dot(r,ng),.1,1.0);
float4 brdf=rough*float4(-1,-.0275,-.572,.022)+float4(1,.0425,1.04,-.04);
float a004=min(brdf.x*brdf.x,exp2(-9.28*NoV))*brdf.x+brdf.y;
float2 AB=float2(-1.04,1.04)*a004+brdf.zw;
float3 f0=lerp(float3(.04,.04,.04),albedo,metal);
float3 specular=prefiltered*(f0*AB.x+AB.y)*frontFactor*frontFactor*pow(ao,4);
float frontLight=max(dot(n,float3(-.5416752,.7071067,-.4545194)),0)*.25;
return saturate((diffuse+specular)*Exposure+albedo*(1-metal)*frontLight);
