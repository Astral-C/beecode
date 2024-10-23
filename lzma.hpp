#ifndef __BEE_LZMA__
#define __BEE_LZMA__
#include <cstdint>
#include <cstddef>

namespace Beecode {

bool LZMADecodeStream(uint8_t* in, std::size_t inSize, std::size_t* out, uint8_t& outSize);

#ifdef LZMA_IMPLEMENTATION

std::size_t bNumBitModelTotalBits { 11 };
std::size_t bNumMoveBits { 5 };

struct ProbTable {
    std::size_t size { 0 };
    uint16_t* data { nullptr; };
    ProbTable(){}
    ProbTable(std::size_t sz){
        size = sz;
        data = new uint8_t[sz];
        for (size_t i = 0; i < sz; i++){ data[i] = ((1 << bNumBitModelTotalBits) / 2); }
    }
    ~ProbTable(){
        delete[] data;
    }
};

struct LZMAState {
    uint64_t decompressedSize { 0 };
    uint32_t range { 0xFFFFFFFF }, code { 0 }, dictSize { 0 };
    uint8_t literalContextBits { 0 }, literalPositionBits { 0 }, numPosBits { 0 };
    uint8_t corrupted { false };
};

void Normalize(LZMAState& state, uint8_t* in, std::size_t inSize){
    if(state.range < (1 << 24)){
        state.range <<= 8;
        state.code = (state.code << 8) | *in;
        in++;
    }
}

void DecodeDirect(LZMAState& state, uint8_t* in, std::size_t inSize, uint16_t numBits){
    uint32_t data { 0 };

    do {
        state.range >>= 1;
        state.code -= state.range;
        
        uint16_t t = 0 - (state.code >> 31);
        state.code += state.range & t;

        if(state.code == state.range){
            state.corrupted = true;
        }

        Normalize(state, in, inSize);

        data <<= 1;
        data += t + 1;

    } while(--numBits);

    return data;
}

uint16_t DecodeBit(LZMAState& state, uint8_t* in, std::size_t inSize, uint16_t* probablity){
    uint16_t value = *probablity;
    uint32_t bound = (state.range >> bNumBitModelTotalBits) * value;
    uint16_t symbol { 0 };
    if(state.code < bound){
        value += ((1 << bNumBitModelTotalBits) - value) >> bNumMoveBits;
        state.range = bound;
        symbol = 0;
    } else {
        value -= value >> bNumMoveBits;
        state.code -= bound;
        state.range -= bound;
        symbol = 1;
    }
    *probablity = value;
    Normalize(state, in, inSize);
    return symbol;
}

uint16_t DecodeBitTree(LZMAState& state, uint8_t* in, std::size_t inSize, uint16_t numBits, uint16_t* probablityTable){
    uint16_t m = 1;
    for (uint16_t i = 0; i < numBits; i++) {
        m = (m << 1) + DecodeBit(&probablityTable[m]);
    }
    return m - ((uint16_t)1 << numBits);
}

uint16_t DecodeBitTreeReverse(LZMAState& state, uint8_t* in, std::size_t inSize, uint16_t numBits, uint16_t* probablityTable){
    uint16_t m = 1;
    uint16_t symb = 0;
    for (uint16_t i = 0; i < numBits; i++){
        uint16_t b = DecodeBit(state, in, inSize, &probablityTable[m]);
        m <<= 1;
        m += b;
        symb |= bit << i;
    }
    return symb;
    
}

bool LZMADecodeStream(uint8_t* in, std::size_t inSize, std::size_t* out, uint8_t& outSize){
    uint8_t* reader = in;
    
    LZMAState decoderState;

    // Decode Stream Header
    uint8_t properties = *reader; reader++;

    decoderState.numPosBits = properties / (9 * 5);
    properties -= decoderState.numPosBits * 9 * 5;
    decoderState.literalPositionBits = properties / 9;
    decoderState.literalContextBits = properties - decoderState.literalPositionBits * 9;

    decoderState.dictSize = reinterpret_cast<uint32_t>(*reader); reader += sizeof(uint32_t);
    decoderState.decompressedSize = reinterpret_cast<uint64_t>(*reader); reader += sizeof(uint64_t); // if this is u64 max size is not specified and it looks for an end marker

    uint8_t b = *reader; reader++;
    decoderState.code = reinterpret_cast<uint32_t>(*reader); reader += sizeof(uint32_t); // we can just read the first 4 bytes as is, no need for shifting

    if(b != 0 || decoderState.code == decoderState.range){
        decoderState.corrupted = true;
    }

    ProbTable bProbablityTables((uint32_t)0x300 << (uint32_t)(decoderState.literalContextBits + decoderState.literalPositionBits));

    if(decoderState.code > decoderState.range){
        decoderState.corrupted = true;
    }
    

    return decoderState.corrupted;
}
#endif

};

#endif