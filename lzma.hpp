#ifndef __BEE_LZMA__
#define __BEE_LZMA__
#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace BeeCode {

bool LZMADecodeStream(uint8_t* in, std::size_t inSize, std::size_t* out, uint8_t& outSize);

#ifdef LZMA_IMPLEMENTATION

template<typename T>
T swap(T v){
    T value = v;
    T swapped = 0;
    for(std::size_t i = 0; i < sizeof(v); i++){ swapped = (v & 0xFF) | swapped << 8; v >>= 8; } 
    return swapped;
}

template<uint32_t numBits, uint32_t size>
struct ProbTable {
    uint16_t data[size] = { 0 };
    ProbTable(){
        for (size_t i = 0; i < size; i++){ data[i] = ((1 << numBits) / 2); }
    }
};

template<uint32_t totalModelBits, uint32_t numMoveBits>
struct RangeCoder {
    uint8_t* streamPtr { nullptr };
    std::size_t streamSize { 0 };
    uint32_t range { 0xFFFFFFFF }, code { 0 };
    uint8_t corrupted { false };

    void Normalize(){
        if(range < (1 << 24)){
            range <<= 8;
            code = (code << 8) | *streamPtr;
            streamPtr++;
        }
    }

    uint32_t DecodeDirect(uint8_t* in, uint16_t numBits){
        uint32_t data { 0 };

        do {
            range >>= 1;
            code -= range;
            
            uint16_t t = 0 - (code >> 31);
            code += range & t;

            if(code == range){
                corrupted = true;
            }

            Normalize();

            data <<= 1;
            data += t + 1;

        } while(--numBits);

        return data;
    }

    uint16_t DecodeBit(uint16_t* probablity){
        uint32_t value = static_cast<uint32_t>(*probablity);
        uint32_t bound = (range >> totalModelBits) * value;
        uint16_t symbol { 0 };
        if(code < bound){
            value += ((1 << totalModelBits) - value) >> numMoveBits;
            range = bound;
            symbol = 0;
        } else {
            value -= value >> numMoveBits;
            code -= bound;
            range -= bound;
            symbol = 1;
        }
        *probablity = static_cast<uint16_t>(value);
        Normalize();
        return symbol;
    }

    template<uint32_t bits, uint32_t size>
    uint16_t DecodeBitTree(uint8_t* in, uint16_t numBits, ProbTable<bits, size>& table){
        uint16_t m = 1;
        for (uint16_t i = 0; i < numBits; i++) {
            m = (m << 1) + DecodeBit(&table.data[m]);
        }
        return m - ((uint16_t)1 << numBits);
    }

    template<uint32_t bits, uint32_t size>
    uint16_t DecodeBitTreeReverse(uint8_t* in, uint16_t numBits, ProbTable<bits, size>& table){
        uint16_t m = 1;
        uint16_t symb = 0;
        for (uint16_t i = 0; i < numBits; i++){
            uint16_t b = DecodeBit(&table.data[m]);
            m = (m << 1) + b;
            symb |= b << i;
        }
        return symb;   
    }

    RangeCoder(uint8_t* stream, std::size_t size){
        streamSize = size;
        streamPtr = stream;

        uint8_t b = *streamPtr;
        streamPtr++;

        for(std::size_t i = 0; i < 4; i++)
            code = (code << 8) | *streamPtr;

        if(b != 0 || code == range)
            corrupted = true;

    }

};

struct LiteralCoder {
    uint16_t state { 0 };
    ProbTable<11, 0x300>* literalProbabilities { nullptr };
    
    void DecodeLiteral(uint8_t* outBuffer, std::size_t outSize, uint32_t rep){
        uint16_t prevByte = 0;
    
        if(outSize != 0){
            prevByte = *(outBuffer-1); // not sure if this is entirely right
        }
    
    }

    LiteralCoder(int count){
        literalProbabilities = new ProbTable<11, 0x300>[count];
    }

    ~LiteralCoder(){
        delete[] literalProbabilities;
    }
};

struct LZMADecoderInfo {
    uint64_t decompressedSize { 0 };
    uint32_t dictSize { 1 << 12 };
    uint8_t literalContextBits { 0 }, literalPositionBits { 0 }, numPosBits { 0 };

    void InitProps(uint8_t* in){
        uint8_t props = *in; in++;
        literalContextBits = props % 9; props /= 9;
        numPosBits = props / 5;
        literalPositionBits = props % 5;
        for (std::size_t i = 0; i < sizeof(uint32_t); i++){ dictSize |= static_cast<uint32_t>(*in) << (8 * i); in++; }
        dictSize = std::max(static_cast<uint32_t>(1<<12), dictSize);

        decompressedSize = *reinterpret_cast<uint64_t*>(in);
    }

};

bool LZMADecompress(uint8_t* in, std::size_t inSize, uint8_t* out, std::size_t& outSize){    
    uint8_t* reader = in;

    LZMADecoderInfo decoder;
    RangeCoder<11, 5> rangeCoder(in, inSize);
    LiteralCoder literals(decoder.literalPositionBits + decoder.literalContextBits);

    // Decode Stream Header
    decoder.InitProps(in);
    reader += 5; // skip past 

    return rangeCoder.corrupted;
}
#endif

};

#endif