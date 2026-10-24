#!/usr/bin/env python3
"""Compile and render production shaders through surfaceless EGL ES2 (Mesa + Pillow)."""
import ctypes as c, re, pathlib, json, sys
from PIL import Image,ImageDraw,ImageFont,ImageChops
root=pathlib.Path(__file__).resolve().parents[2]
source=(root/'corum-ranked-mod/src/ui/AchievementEffects.cpp').read_text()
vertex=re.search(r'char Vertex\[\]=R"GLSL\((.*?)\)GLSL"',source,re.S)[1]
fragment=re.search(r'char Fragment\[\]=R"GLSL\((.*?)\)GLSL"',source,re.S)[1]
e=c.CDLL('libEGL.so.1'); I=c.c_int; U=c.c_uint; F=c.c_float; P=c.c_void_p
for name,result,args in [('eglGetPlatformDisplay',P,[U,P,P]),('eglInitialize',U,[P,P,P]),('eglBindAPI',U,[U]),('eglChooseConfig',U,[P,P,P,I,P]),('eglCreatePbufferSurface',P,[P,P,P]),('eglCreateContext',P,[P,P,P,P]),('eglMakeCurrent',U,[P,P,P,P]),('eglGetProcAddress',P,[c.c_char_p])]:
 fn=getattr(e,name);fn.restype=result;fn.argtypes=args
attrs=lambda *a:(I*len(a))(*a)
d=e.eglGetPlatformDisplay(0x31DD,None,None);major=I();minor=I();assert e.eglInitialize(d,c.byref(major),c.byref(minor))
assert e.eglBindAPI(0x30A0)
config=P();count=I();assert e.eglChooseConfig(d,attrs(0x3033,1,0x3040,4,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038),c.byref(config),1,c.byref(count)) and count.value
W,H=512,64
surface=e.eglCreatePbufferSurface(d,config,attrs(0x3057,W,0x3056,H,0x3038));ctx=e.eglCreateContext(d,config,None,attrs(0x3098,2,0x3038));assert surface and ctx and e.eglMakeCurrent(d,surface,surface,ctx)
def gl(name,result,args):
 address=e.eglGetProcAddress(name.encode());assert address,name
 return c.CFUNCTYPE(result,*args)(address)
createShader=gl('glCreateShader',U,[U]); shaderSource=gl('glShaderSource',None,[U,I,P,P]);compileShader=gl('glCompileShader',None,[U]);getShaderiv=gl('glGetShaderiv',None,[U,U,P]);shaderLog=gl('glGetShaderInfoLog',None,[U,I,P,P])
def shader(kind,text):
 s=createShader(kind);b=text.encode();strings=(c.c_char_p*1)(b);shaderSource(s,1,strings,None);compileShader(s);okay=I();getShaderiv(s,0x8B81,c.byref(okay));buf=c.create_string_buffer(8192);shaderLog(s,8192,None,buf);assert okay.value,buf.value.decode();return s
v=shader(0x8B31,'uniform mat4 CC_MVPMatrix;\n'+vertex);f=shader(0x8B30,fragment)
program=gl('glCreateProgram',U,[])();attach=gl('glAttachShader',None,[U,U]);attach(program,v);attach(program,f)
bind=gl('glBindAttribLocation',None,[U,U,c.c_char_p]);
for i,name in enumerate([b'a_position',b'a_texCoord',b'a_color']):bind(program,i,name)
gl('glLinkProgram',None,[U])(program);okay=I();gl('glGetProgramiv',None,[U,U,P])(program,0x8B82,c.byref(okay));assert okay.value
use=gl('glUseProgram',None,[U]);use(program)
loc=gl('glGetUniformLocation',I,[U,c.c_char_p]);u1=gl('glUniform1f',None,[I,F]);
for key,val in [('u_width',W),('u_solid',0)]:u1(loc(program,key.encode()),val)
gl('glUniform1i',None,[I,I])(loc(program,b'CC_Texture0'),0)
m=(F*16)(2/W,0,0,0,0,2/H,0,0,0,0,1,0,-1,-1,0,1);gl('glUniformMatrix4fv',None,[I,I,U,P])(loc(program,b'CC_MVPMatrix'),1,0,m)
fontpath=root/'corum-ranked-mod/resources/fonts/CorumSans-Black.ttf'
if not fontpath.exists():fontpath=next((root/'corum-ranked-mod').rglob('*Black*.ttf'))
im=Image.new('RGBA',(W,H));draw=ImageDraw.Draw(im);font=ImageFont.truetype(str(fontpath),43);draw.text((8,-2),'CORUM Mythic V',font=font,fill='white',stroke_width=2,stroke_fill='black')
texture=U();gl('glGenTextures',None,[I,P])(1,c.byref(texture));gl('glBindTexture',None,[U,U])(0x0DE1,texture.value)
for a,b in [(0x2801,0x2601),(0x2800,0x2601),(0x2802,0x812F),(0x2803,0x812F)]:gl('glTexParameteri',None,[U,U,I])(0x0DE1,a,b)
gl('glTexImage2D',None,[U,I,I,I,I,I,U,U,P])(0x0DE1,0,0x1908,W,H,0,0x1908,0x1401,im.tobytes())
vertices=(F*16)(0,0,0,1,W,0,1,1,0,H,0,0,W,H,1,0)
attribute=gl('glVertexAttribPointer',None,[U,I,U,U,I,P]);enable=gl('glEnableVertexAttribArray',None,[U]);
attribute(0,2,0x1406,0,16,c.addressof(vertices));attribute(1,2,0x1406,0,16,c.addressof(vertices)+8);enable(0);enable(1);gl('glVertexAttrib4f',None,[U,F,F,F,F])(2,1,1,1,1)
read=gl('glReadPixels',None,[I,I,I,I,U,U,P]);drawArrays=gl('glDrawArrays',None,[U,I,I]);clear=gl('glClear',None,[U]);gl('glViewport',None,[I,I,I,I])(0,0,W,H)
frames=[];out=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else '/tmp/corum-achievement-effects');out.mkdir(exist_ok=True)
for mythic in [0,1]:
 images=[]
 for frame in range(60):
  u1(loc(program,b'u_mythic'),mythic);u1(loc(program,b'u_phase'),frame/60);clear(0x4000);drawArrays(0x0005,0,4)
  buf=(c.c_ubyte*(W*H*4))();read(0,0,W,H,0x1908,0x1401,buf);image=Image.frombytes('RGBA',(W,H),bytes(buf)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
  assert image.getextrema()[3][1]>0
  images.append(image)
 images[0].save(out/('mythic' if mythic else 'legendary').__add__('.png'))
 images[0].convert('RGB').save(out/('mythic' if mythic else 'legendary').__add__('.gif'),save_all=True,append_images=[x.convert('RGB') for x in images[1:]],duration=50 if mythic else 33,loop=0)
 # A transparent texel must stay transparent and outlined text must retain opaque black pixels.
 assert images[0].getpixel((511,63))[3]==0
 assert any(px[0]==px[1]==px[2]==0 and px[3]==255 for px in (images[0].get_flattened_data() if hasattr(images[0],"get_flattened_data") else images[0].getdata()))
# CCLayerColor progress bars bypass glyph texture sampling. Verify both the
# earned Legendary gold sweep and the existing Mythic rainbow on solid quads.
u1(loc(program,b'u_solid'),1)
bar_frames=0
for mythic in [0,1]:
 images=[]
 for frame in range(60):
  u1(loc(program,b'u_mythic'),mythic);u1(loc(program,b'u_phase'),frame/60);clear(0x4000);drawArrays(0x0005,0,4)
  buf=(c.c_ubyte*(W*H*4))();read(0,0,W,H,0x1908,0x1401,buf)
  image=Image.frombytes('RGBA',(W,H),bytes(buf)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
  assert image.getextrema()[3]==(255,255)
  images.append(image);bar_frames+=1
 assert ImageChops.difference(images[0].convert('RGB'),images[30].convert('RGB')).getbbox()
 images[15].save(out/('mythic-progress.png' if mythic else 'legendary-progress.png'))
 images[0].convert('RGB').save(out/('mythic-progress.gif' if mythic else 'legendary-progress.gif'),save_all=True,append_images=[x.convert('RGB') for x in images[1:]],duration=50,loop=0)
print(json.dumps({'renderer':'EGL surfaceless OpenGL ES 2','shaderCompile':True,'shaderLink':True,'renderedFrames':120,'solidProgressFrames':bar_frames,'glyphMaskPreserved':True,'blackOutlinePreserved':True,'artifacts':str(out)}))
