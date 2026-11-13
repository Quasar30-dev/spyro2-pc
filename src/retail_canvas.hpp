#pragma once
#include "spyro/interface.hpp"
#include "imgui.h"
// The draw list is a transport for decoded PS1 sprites. No ImGui font, widget
// chrome or text metrics participate in the retail interface.
struct RetailCanvas {
    const spyro::InterfaceAssets& assets;ImDrawList* draw;ImTextureID texture;
    float left{},top{},sx{},sy{},opacity=1;
    RetailCanvas(const spyro::InterfaceAssets& a,ImDrawList* d,ImTextureID t,ImVec2 origin,ImVec2 size):assets(a),draw(d),texture(t) {
        sy=size.y/240;sx=std::min(size.x/512,sy*0.625f);left=origin.x+(size.x-512*sx)/2;top=origin.y;
    }
    ImVec2 point(float x,float y)const{return {left+x*sx,top+y*sy};}
    void image(const spyro::HudSprite& s,float x,float y,ImU32 tint=IM_COL32_WHITE,float scale=1)const {
        if(!s.width || !s.height)return;
        const auto alpha=static_cast<unsigned>(((tint>>24)&255)*opacity);tint=(tint&0xffffff)|(alpha<<24);
        draw->AddImage(texture,point(x,y),point(x+s.width*scale,y+s.height*scale),{float(s.x)/assets.width,float(s.y)/assets.height},{float(s.x+s.width)/assets.width,float(s.y+s.height)/assets.height},tint);
    }
    void text(std::string_view text,float x,float y,unsigned color=0,bool dense=false,float scale=1)const {
        const auto layout=spyro::layout_retail_text(assets,text,color,0,dense);
        for(const auto& g:layout.glyphs)image(assets.font[g.character].colors[g.color],x+g.x*scale,y+g.y*scale,IM_COL32_WHITE,scale);
    }
    void centered(std::string_view text,float x,float y,unsigned color=0,bool dense=false,float scale=1)const {const auto l=spyro::layout_retail_text(assets,text,color,0,dense);this->text(text,x-l.width*scale/2,y,color,dense,scale);}
    void right(std::string_view text,float x,float y,unsigned color=0)const {const auto l=spyro::layout_retail_text(assets,text,color);this->text(text,x-l.width,y,color);}
    void rect(float x,float y,float w,float h,ImU32 c)const {draw->AddRectFilled(point(x,y),point(x+w,y+h),(c&0xffffff)|(unsigned(((c>>24)&255)*opacity)<<24));}
    void resident(unsigned id,float x,float y)const {image(assets.resident.at(id),x,y);}
    void stamps(unsigned table,bool resident_sprites,float x=0,float y=0)const {
        for(const auto& s:assets.layouts.at(table))image(resident_sprites?assets.resident.at(s.sprite):assets.pause.at(s.sprite),x+s.x,y+s.y);
    }
};
