#pragma once
#include <array>
#include <cstddef>
#include <string_view>

namespace corum::ranked {
// Collection previews have their own order; result cards retain their existing
// rank order, layout, colors and animation.
inline constexpr std::array<std::string_view,5> SurvivalCollectionGrades{
    "RATED","FEATURED","EPIC","LEGENDARY","MYTHIC"};
struct SurvivalCollectionGradeCell {float x,y,side,countY;};
inline SurvivalCollectionGradeCell survivalCollectionGradeCell(std::size_t index){
    auto const top=index<3;
    return {top?91.f+114.f*index:148.f+114.f*(index-3),top?164.f:69.f,74.f,top?115.f:20.f};
}
}
