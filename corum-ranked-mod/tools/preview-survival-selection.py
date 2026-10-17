#!/usr/bin/env python3
"""Static layout preview using the production C++ rectangles and Corum font.

Requires g++ and Pillow. This is a typography/layout aid, not a GD/GL screenshot.
Optional external images stay in the requested output directory, never packaged.
"""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FIELDS = 'safe header subtitle carousel numberFinal panel thumbnail title creator difficulty metadata ldm shake jukebox ready readiness hint'.split()
TEXT=(242,247,255); MUTED=(164,181,205); BLUE=(77,135,255); CYAN=(64,224,255); GOLD=(255,207,74); GREEN=(90,238,151); INK=(5,10,22)
S=3
FONT = ROOT/'resources/CorumSansBlack.ttf'

def production_layout(binary,w,h,thumbnail,ldm):
    return json.loads(subprocess.check_output([str(binary),str(w),str(h),str(int(thumbnail)),str(int(ldm))]))

def render(layout,w,h,thumbnail,ldm,korean,image=None):
    canvas=Image.new('RGB',(w*S,h*S),(4,9,22));d=ImageDraw.Draw(canvas)
    def box(rect,fill=(15,28,50),accent=BLUE):
        x,y,rw,rh=rect;bounds=(int(x*S),int((h-y-rh)*S),int((x+rw)*S),int((h-y)*S))
        d.rectangle(bounds,fill=fill,outline=accent,width=max(1,int(S*.7)))
        d.line((bounds[0]+4*S,bounds[1]+3*S,bounds[2]-4*S,bounds[1]+3*S),fill=accent,width=S)
    def wrap(text,font,width):
        out=[]
        for paragraph in text.split('\n'):
            line=''
            for ch in paragraph:
                if line and d.textlength(line+ch,font=font)>width:out.append(line);line=''
                line+=ch
            out.append(line)
        return out
    def label(text,rect,color=TEXT,maximum=26,ellipsis=False,single=False):
        x,y,rw,rh=rect
        for px in range(int(maximum*S),3,-1):
            font=ImageFont.truetype(str(FONT),px);ascent,descent=font.getmetrics();lineh=ascent+descent
            lines=[text] if ellipsis or single else wrap(text,font,rw*S)
            if len(lines)*lineh<=rh*S and (not single or d.textlength(text,font=font)<=rw*S):break
        if ellipsis:
            while len(text)>3 and d.textlength(text,font=font)>rw*S:text=text[:-4]+'...'
            lines=[text]
        top=(h-y-rh/2)*S-lenh(lines,lineh)/2
        for i,line in enumerate(lines):
            left=(x+rw/2)*S-d.textlength(line,font=font)/2
            d.text((left,top+i*lineh),line,font=font,fill=color)
    def switch(name,rect,on):
        x,y,rw,rh=rect;box(rect,accent=GREEN if on else MUTED)
        label(name,(x+6,y+4,rw-54,rh-8),maximum=24)
        box((x+rw-42,y+(rh-20)/2,36,20),(27,112,68) if on else (44,56,75),GREEN if on else MUTED)
        label('ON' if on else 'OFF',(x+rw-39,y+(rh-14)/2,30,14),maximum=22)
    box(layout['panel'],(10,19,36))
    header=list(layout['header']);header[2]-=152*layout['scale']
    label('선정된 맵' if korean else 'SELECTED MAP',header,CYAN,42)
    x,y,rw,rh=layout['header'];label('ROUND 2\nSTART IN 00:10',(x+rw-142,y-2,142,rh+4),GOLD,24)
    label('SURVIVAL',layout['subtitle'],CYAN,24)
    if thumbnail:
        rect=layout['thumbnail'];box(rect,accent=GOLD)
        if image:
            x,y,rw,rh=rect;photo=Image.open(image).convert('RGB');photo=photo.resize((int(rw*S)-4,int(rh*S)-4));canvas.paste(photo,(int(x*S)+2,int((h-y-rh)*S)+2))
        else:label('16:9 THUMBNAIL',rect,CYAN,22)
    else:label('2147483647',layout['numberFinal'],GOLD,60)
    label('VeryLongMixedCaseLevelName To Check Layout',layout['title'],TEXT,39)
    label('by MiXeDCaSeCreatorNameWithAVeryLongSuffix',layout['creator'],MUTED,24,True)
    label('GD FACE / EXTREME',layout['difficulty'],TEXT,22)
    label('GD #2147483647\n10 stars · LONG',layout['metadata'],CYAN,22)
    if ldm:switch('LDM',layout['ldm'],False)
    switch('화면 흔들림 끄기' if korean else 'Disable Shake',layout['shake'],True)
    box(layout['jukebox']);label('Jukebox',layout['jukebox'],TEXT,24,single=True)
    box(layout['ready'],GOLD,(255,237,139));label('준비' if korean else 'READY',layout['ready'],INK,34,single=True)
    label('맵 준비 / 오디오 준비' if korean else 'MAP READY / AUDIO READY',layout['readiness'],GREEN,21)
    if not thumbnail:label('(i) Level Thumbnails 모드를 설치하면 맵 썸네일과 더 화려한 선정 연출을 볼 수 있습니다.' if korean else '(i) Install Level Thumbnails for map previews and a richer selection reveal.',layout['hint'],MUTED,22)
    box((10,8,116,28));label('현황판 보기' if korean else 'VIEW STATUS',(16,12,104,20),TEXT,25)
    box((w-126,8,116,28),accent=(255,91,111));label('항복' if korean else 'SURRENDER',(w-120,12,104,20),TEXT,25)
    return canvas

def lenh(lines,height):return len(lines)*height

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--image',type=Path);args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='corum-layout-') as tmp:
        cpp=Path(tmp)/'layout.cpp';binary=Path(tmp)/'layout'
        fields=';'.join(f'emit("{field}",r.{field})' for field in FIELDS)
        cpp.write_text('#include "domain/SurvivalSelectionLayout.hpp"\n#include <iostream>\n#include <cstdlib>\nint main(int argc,char**a){if(argc!=5)return 1;auto r=corum::ranked::survivalSelectionLayout(std::atof(a[1]),std::atof(a[2]),std::atoi(a[3]),std::atoi(a[4]));bool first=true;std::cout<<"{";auto emit=[&](char const*k,corum::ranked::SurvivalRect b){if(!first)std::cout<<",";first=false;std::cout<<"\\\""<<k<<"\\\":["<<b.x<<","<<b.y<<","<<b.w<<","<<b.h<<"]";};'+fields+';std::cout<<",\\\"scale\\\":"<<r.scale<<"}";}')
        subprocess.run(['g++','-std=c++23','-I',str(ROOT/'src'),str(cpp),'-o',str(binary)],check=True)
        cases=[(568,320,True,True,False),(568,426,True,False,True),(400,280,True,True,False),(520,320,True,True,True),(568,320,False,True,True),(1024,576,False,False,False)]
        for w,h,thumb,ldm,ko in cases:
            layout=production_layout(binary,w,h,thumb,ldm);image=render(layout,w,h,thumb,ldm,ko,args.image)
            name=f'{w}x{h}-'+('thumbnail' if thumb else 'numeric')+('-ko' if ko else '-en');image.save(args.output/(name+'.png'));(args.output/(name+'.json')).write_text(json.dumps(layout,indent=2))
            print(args.output/(name+'.png'))
if __name__=='__main__':main()
