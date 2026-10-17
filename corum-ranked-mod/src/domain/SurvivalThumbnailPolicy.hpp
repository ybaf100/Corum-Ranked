#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
namespace corum::ranked {
inline constexpr std::string_view SurvivalThumbnailOrigin="https://levelthumbs.prevter.me";
inline constexpr std::size_t SurvivalThumbnailMaxBytes=2*1024*1024;
inline std::optional<std::uint64_t> survivalThumbnailRedirect(std::string_view location) {
    if(location.starts_with(SurvivalThumbnailOrigin))location.remove_prefix(SurvivalThumbnailOrigin.size());
    constexpr std::string_view prefix="/thumbnail/",suffix="/small";
    if(!location.starts_with(prefix)||!location.ends_with(suffix))return {};
    auto digits=location.substr(prefix.size(),location.size()-prefix.size()-suffix.size());
    if(digits.empty() || digits.size()>10 || digits.front()=='0')return {};
    std::uint64_t id=0;for(auto ch:digits){if(ch<'0'||ch>'9')return {};id=id*10+static_cast<unsigned>(ch-'0');}
    return id?std::optional(id):std::nullopt;
}
inline std::string survivalThumbnailUrl(std::uint64_t id,bool medium) {
    if(!id || id>9999999999ULL)return {};
    return std::string(SurvivalThumbnailOrigin)+"/thumbnail/"+std::to_string(id)+(medium?"/medium":"/small");
}
// Validate every dimension-bearing RIFF chunk before a decoder can allocate.
// Reject animation and conflicting canvas/bitstream dimensions, not just the
// first VP8X header. Limits match the official medium endpoint (1280x720).
inline std::optional<std::pair<unsigned,unsigned>> survivalWebpSize(std::span<std::uint8_t const> data) {
    if(data.size()<20 || data.size()>SurvivalThumbnailMaxBytes)return {};
    auto tag=[&](std::size_t p,std::string_view text){if(p+text.size()>data.size())return false;for(std::size_t i=0;i<text.size();++i)if(data[p+i]!=static_cast<unsigned char>(text[i]))return false;return true;};
    auto le=[&](std::size_t p,unsigned n){std::uint32_t value=0;for(unsigned i=0;i<n;++i)value|=static_cast<std::uint32_t>(data[p+i])<<(8*i);return value;};
    if(!tag(0,"RIFF") || !tag(8,"WEBP") || static_cast<std::size_t>(le(4,4))+8!=data.size())return {};
    std::optional<std::pair<unsigned,unsigned>> size;unsigned streams=0;
    std::size_t at=12;unsigned chunks=0;
    for(;at+8<=data.size();){
        if(++chunks>128)return {};
        auto length=static_cast<std::size_t>(le(at+4,4)),start=at+8;
        if(length>data.size()-start)return {};
        std::optional<std::pair<unsigned,unsigned>> next;
        if(tag(at,"ANIM") || tag(at,"ANMF"))return {};
        if(tag(at,"VP8X")){if(length<10 || (data[start]&2))return {};next={{le(start+4,3)+1,le(start+7,3)+1}};}
        if(tag(at,"VP8 ")){if(++streams>1 || length<10 || !tag(start+3,std::string_view("\x9d\x01\x2a",3)))return {};next={{le(start+6,2)&0x3fff,le(start+8,2)&0x3fff}};}
        if(tag(at,"VP8L")){if(++streams>1 || length<5 || data[start]!=0x2f)return {};auto bits=le(start+1,4);next={{(bits&0x3fff)+1,((bits>>14)&0x3fff)+1}};}
        if(next){auto [w,h]=*next;if(!w||!h||w>1280||h>720||w*h>1280*720 || (size && size!=next))return {};size=next;}
        at=start+length+(length&1);
        if(at>data.size())return {};
    }
    return streams==1 && at==data.size()?size:std::nullopt;
}
}
