// BattleSpades presentation-only compatibility API. Coordinates match OpenSpades.
namespace spades {
const float PiF=3.14159265358979323846f;
float Min(float a,float b){return a<b?a:b;}
float Max(float a,float b){return a>b?a:b;}
float Clamp(float a,float lo,float hi){return Min(Max(a,lo),hi);}
float Mix(float a,float b,float f){return a+(b-a)*f;}
float SmoothStep(float a){a=Clamp(a,0,1);return a*a*(3-2*a);}
uint GetRandom(uint limit){return limit==0?0:NativeRandom()%limit;}
float GetRandom(){return float(NativeRandom()%1000000)/1000000.f;}
class Vector2 {
 float x,y;
 Vector2(){x=y=0;}
 Vector2(float a,float b){x=a;y=b;}
 Vector2 opAdd(const Vector2 &in b)const{return Vector2(x+b.x,y+b.y);}
 Vector2 opSub(const Vector2 &in b)const{return Vector2(x-b.x,y-b.y);}
 Vector2 opMul(float b)const{return Vector2(x*b,y*b);}
}
class Vector3 {
 float x,y,z;
 Vector3(){x=y=z=0;}
 Vector3(float a,float b,float c){x=a;y=b;z=c;}
 Vector3 opAdd(const Vector3 &in b)const{return Vector3(x+b.x,y+b.y,z+b.z);}
 Vector3 opSub(const Vector3 &in b)const{return Vector3(x-b.x,y-b.y,z-b.z);}
 Vector3 opMul(float b)const{return Vector3(x*b,y*b,z*b);}
 Vector3 opMul(const Vector3 &in b)const{return Vector3(x*b.x,y*b.y,z*b.z);}
 Vector3 opMul_r(float b)const{return opMul(b);}
 Vector3 opDiv(float b)const{return opMul(1/b);}
 Vector3 opNeg()const{return Vector3(-x,-y,-z);}
 Vector3 &opAddAssign(const Vector3 &in b){x+=b.x;y+=b.y;z+=b.z;return this;}
 Vector3 &opSubAssign(const Vector3 &in b){x-=b.x;y-=b.y;z-=b.z;return this;}
 Vector3 &opMulAssign(float b){x*=b;y*=b;z*=b;return this;}
 float Length { get const {return sqrt(x*x+y*y+z*z);} }
 float GetLength()const{return Length;}
 Vector3 Normalize()const{return Length>0?opDiv(Length):Vector3();}
 Vector3 Normalized {get const{return Normalize();}}
}
Vector3 Mix(const Vector3 &in a,const Vector3 &in b,float f){return a+(b-a)*f;}
float Dot(const Vector3 &in a,const Vector3 &in b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vector3 Cross(const Vector3 &in a,const Vector3 &in b){return Vector3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
class Vector4 {
 float x,y,z,w;
 Vector4(){x=y=z=w=0;}
 Vector4(float a,float b,float c,float d){x=a;y=b;z=c;w=d;}
 Vector4(Vector3 a,float d){x=a.x;y=a.y;z=a.z;w=d;}
 Vector4 opMul(float f)const{return Vector4(x*f,y*f,z*f,w*f);}
 Vector3 xyz {get const {return Vector3(x,y,z);}}
}
class Matrix4 {
 array<float> m(16);
 Matrix4(){for(uint i=0;i<16;i++)m[i]=(i%5==0)?1:0;}
 Matrix4 opMul(const Matrix4 &in b)const{
  Matrix4 c;for(uint col=0;col<4;col++)for(uint row=0;row<4;row++){
   c.m[col*4+row]=0;for(uint k=0;k<4;k++)c.m[col*4+row]+=m[k*4+row]*b.m[col*4+k];
  }return c;
 }
 Vector3 opMul(const Vector3 &in p)const{return Vector3(m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]);}
 Vector3 GetAxis(int i)const{return Vector3(m[i*4],m[i*4+1],m[i*4+2]);}
 Vector3 GetOrigin()const{return GetAxis(3);}
 Matrix4 &opMulAssign(const Matrix4 &in b){this=opMul(b);return this;}
}
Matrix4 CreateTranslateMatrix(Vector3 p){Matrix4 m;m.m[12]=p.x;m.m[13]=p.y;m.m[14]=p.z;return m;}
Matrix4 CreateTranslateMatrix(float x,float y,float z){return CreateTranslateMatrix(Vector3(x,y,z));}
Matrix4 CreateScaleMatrix(Vector3 s){Matrix4 m;m.m[0]=s.x;m.m[5]=s.y;m.m[10]=s.z;return m;}
Matrix4 CreateScaleMatrix(float s){return CreateScaleMatrix(Vector3(s,s,s));}
Matrix4 CreateScaleMatrix(float x,float y,float z){return CreateScaleMatrix(Vector3(x,y,z));}
Matrix4 CreateRotateMatrix(Vector3 a,float r){
 a=a.Normalize();float c=cos(r),s=sin(r),t=1-c;Matrix4 m;
 m.m[0]=t*a.x*a.x+c;m.m[4]=t*a.x*a.y-s*a.z;m.m[8]=t*a.x*a.z+s*a.y;
 m.m[1]=t*a.x*a.y+s*a.z;m.m[5]=t*a.y*a.y+c;m.m[9]=t*a.y*a.z-s*a.x;
 m.m[2]=t*a.x*a.z-s*a.y;m.m[6]=t*a.y*a.z+s*a.x;m.m[10]=t*a.z*a.z+c;return m;
}
class Model {int id;Model(string p){id=NativeResource(p,0);}}
class Image {int id;float Width=32,Height=32;Image(string p){id=NativeResource(p,1);Width=NativeImageSize(id,false);Height=NativeImageSize(id,true);}}
class AudioChunk {int id;AudioChunk(string p){id=NativeResource(p,2);}}
class ModelRenderParam {Matrix4 matrix;Vector3 customColor;bool depthHack=true;}
class AudioParam {float volume=1,pitch=1,referenceDistance=1;}
class AABB2 {float x,y,w,h;AABB2(float a,float b,float c,float d){x=a;y=b;w=c;h=d;}}
class ConfigItem {
 string value="1";
 ConfigItem(string name){value=NativeConfig(name,name=="r_renderer"?"gl":name=="cg_fov"||name=="cg_fov2"||name=="cg_runFov"?"75":name=="cg_zoomFov"?"37.5":"1");}
 ConfigItem(string name,string initial){value=NativeConfig(name,initial);}
 int IntValue {get const{return int(parseInt(value));}set{this.value=""+value;}}
 float FloatValue {get const{return float(parseFloat(value));}set{this.value=""+value;}}
 string StringValue {get const{return value;}set{this.value=value;}}
}
class Renderer {
 float ScreenWidth=1280,ScreenHeight=720;
 Vector4 color(1,1,1,1);
 Vector4 ColorP {set{color=value;}}
 Vector4 Color {set{color=value;}}
 Vector4 ColorNP {set{color=value;}}
 Model@ RegisterModel(string name){return Model(name);}
 Image@ RegisterImage(string name){return Image(name);}
 void AddModel(Model@ model,ModelRenderParam param){if(model !is null)NativeModel(model.id,param.matrix.m);}
 // OpenSpades Sprite.vs and SoftSprite.vs multiply radius twice.
 void AddSprite(Image@ image,Vector3 p,float radius,float angle){if(image !is null)NativeSprite(image.id,p.x,p.y,p.z,radius*radius,angle,color.x,color.y,color.z,color.w,false);}
 void AddLongSprite(Image@ image,Vector3 a,Vector3 b,float radius){Vector3 p=(a+b)*0.5f;if(image !is null)NativeSprite(image.id,p.x,p.y,p.z,radius,0,color.x,color.y,color.z,color.w,false);}
 void DrawImage(Image@ image,AABB2 box){DrawImage(image,Vector2(box.x,box.y),Vector2(box.w,box.h));}
 void DrawImage(Image@ image,Vector2 p){if(image !is null)NativeSprite(image.id,p.x,p.y,0,image.Width,0,color.x,color.y,color.z,color.w,true);}
 void DrawImage(Image@ image,Vector2 p,Vector2 size){if(image !is null)NativeSprite(image.id,p.x,p.y,size.y,size.x,0,color.x,color.y,color.z,color.w,true);}
}
class AudioDevice {
 AudioChunk@ RegisterSound(string name){return AudioChunk(name);}
 void PlayLocal(AudioChunk@ sound,Vector3 p,AudioParam param){if(sound !is null)NativeSound(sound.id,param.volume,param.pitch);}
 void Play(AudioChunk@ sound,Vector3 p,AudioParam param){PlayLocal(sound,p,param);}
}
interface IToolSkin {void Update(float dt);void AddToScene();}
interface IViewToolSkin {void Draw2D();}
interface IWeaponSkin {}
interface IWeaponSkin2 {}
interface IWeaponSkin3 {}
interface ISpadeSkin {}
enum SpadeActionType { Idle, Bash, DigStart, Dig }
}
