#pragma once
#include "xenos_vertex_fetch_layout.h"
#include <cstddef>
#include <cstring>

namespace recomp::gpu {
struct ScalarMulConst {
    std::uint32_t constantRegister=0,constantComponent=0,temporaryRegister=0,temporaryComponent=0;
    std::uint32_t destinationRegister=0,writeMask=0;
};
// Temporary destination only, scalar MUL_CONST_0/1. The opcode low bit and
// source3 select contribute to the temporary index; source3's register byte is
// still the constant index. No vector instruction is executed by this helper.
inline bool decodeScalarMulConst(const std::array<std::uint32_t,3>& words,ScalarMulConst& output) {
    const auto a=words[0],b=words[1],c=words[2],opcode=a>>26;
    if((opcode!=42 && opcode!=43) || (a&((1u<<6)|(1u<<7)|(1u<<14)|(1u<<15)|(3u<<24))) || b>>24)return false;
    ScalarMulConst candidate;
    candidate.constantRegister=c&255;candidate.constantComponent=(((b&255)>>6)+3)&3;
    candidate.temporaryRegister=(opcode&1)|(((c>>29)&1)<<1)|(b&0x3C);
    candidate.temporaryComponent=b&3;candidate.destinationRegister=(a>>8)&63;
    candidate.writeMask=(a>>20)&15;output=candidate;return true;
}
inline bool scalarNormalOrZero(float value) {
    return std::isfinite(value) && std::fpclassify(value)!=FP_SUBNORMAL;
}
// Computes the new previous-scalar value even with writeMask0. The caller must
// snapshot pre-instruction operands and commit scalar/vector results together.
// No destination-register or previous-scalar state is changed here.
inline bool evaluateScalarMulConst(const ScalarMulConst& plan,const std::uint8_t* constant,
        std::size_t size,const KnownVertexVector& temporary,float& output) {
    if(!constant || size!=16 || plan.constantRegister>=256 || plan.constantComponent>=4 ||
        plan.temporaryRegister>=64 || plan.temporaryComponent>=4 || plan.destinationRegister>=64 ||
        plan.writeMask>15 || (temporary.known&~15) || !(temporary.known&(1<<plan.temporaryComponent)))return false;
    const auto* at=constant+plan.constantComponent*4;
    const std::uint32_t word=(std::uint32_t{at[0]}<<24)|(std::uint32_t{at[1]}<<16)|(std::uint32_t{at[2]}<<8)|at[3];
    float left;std::memcpy(&left,&word,4);const float right=temporary.values[plan.temporaryComponent];
    if(!scalarNormalOrZero(left) || !scalarNormalOrZero(right))return false;
    // Explicit finite normal/zero profile, positivezero multiplication and one
    // float32 rounding step. Not a retail denormal/NaN/overflow implementation.
    volatile float product=(left==0 || right==0)?0.0f:left*right;
    if(!scalarNormalOrZero(product))return false;
    output=product;return true;
}
}
