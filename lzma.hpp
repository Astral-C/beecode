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

struct DecoderOutput {
    uint8_t* buffer { nullptr };
    uint8_t* position { nullptr };
    std::size_t size { 0 };
    uint16_t runningTotal { 0 };

    uint8_t*& stream { nullptr };
    uint32_t maxStreamSize { 0 };

    void operator+=(uint8_t b) { // put
        runningTotal++;
        *position = b; position++;
        if(position == buffer + size){
            position = buffer;
        }
        *stream = b;
        stream++;
    }

    uint8_t operator[](std::size_t dist) const { // get
        return dist <= static_cast<std::size_t>(position - buffer) ? *(position - dist) : *(position + (size - dist));
    }

    void PreformCopy(std::size_t dist, uint16_t len){
        while(len >  0){
            (*this) += (*this)[dist];
            len--;
        }
    }

    bool ValidateDist(std::size_t dist) {
        return dist <= static_cast<std::size_t>(position - buffer) || position == (buffer + size);
    }

    bool IsEmpty() {
        return pos == buffer || position != (buffer + size);
    }

    DecoderOutput(std::size_t dictSize, uint8_t*& strm, uint32_t streamMax){
        buffer = new uint8_t[size];
        maxStreamSize = streamMax;
        position = buffer;
        stream = strm;
        size = dictSize;
    }

    ~DecoderOutput(){
        delete[] buffer;
    }
};

template<uint32_t numBits, uint32_t size>
struct ProbTable {
    uint16_t data[size] = { 0 };
    ProbTable(){
        for (size_t i = 0; i < size; i++){ data[i] = ((1 << numBits) / 2); }
    }
};

template<uint32_t totalModelBits, uint32_t numMoveBits>
struct RangeCoder {
    uint8_t*& streamPtr { nullptr };
    uint32_t range { 0xFFFFFFFF }, code { 0 };
    uint8_t corrupted { false };

    void Normalize(){
        if(range < (1 << 24)){
            range <<= 8;
            code = (code << 8) | *streamPtr;
            streamPtr++;
        }
    }

    uint32_t DecodeDirect(uint16_t numBits){
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
    uint16_t DecodeBitTree(uint16_t numBits, ProbTable<bits, size>& table){
        uint16_t m = 1;
        for (uint16_t i = 0; i < numBits; i++) {
            m = (m << 1) + DecodeBit(&table.data[m]);
        }
        return m - ((uint16_t)1 << numBits);
    }

    template<uint32_t bits, uint32_t size>
    uint16_t DecodeBitTreeReverse(uint16_t numBits, ProbTable<bits, size>& table){
        uint16_t m = 1;
        uint16_t symb = 0;
        for (uint16_t i = 0; i < numBits; i++){
            uint16_t b = DecodeBit(&table.data[m]);
            m = (m << 1) + b;
            symb |= b << i;
        }
        return symb;   
    }

    RangeCoder(uint8_t*& stream){
        streamPtr = stream;

        uint8_t b = *streamPtr;
        streamPtr++;

        for(std::size_t i = 0; i < 4; i++){
            code = (code << 8) | *streamPtr;
            streamPtr++;
        }

        if(b != 0 || code == range)
            corrupted = true;

    }

};

struct LiteralCoder {
    uint16_t state { 0 };
    ProbTable<11, 0x300>* literalProbabilities { nullptr };
    
    void DecodeLiteral(uint8_t* in, DecoderOutput& output, uint32_t rep){
        uint16_t prevByte = 0;
    
        if(!output.IsEmpty()){
            prevByte = output[1];
        }

        uint16_t symbol = 1;
        uint16_t literalState = (output.runningTotal & ((1 << lp) - 1) << lc) + (prevByte >> (8 - lc));

        ProbTable<11, 0x300> table = literalProbabilities[literalState];
    
        RangeCoder<11, 5> rangeCoder(in);

        if(state >= 7){
            uint16_t matchbyte = output[rep0 + 1];
            do {
                uint16_t matchBit = (matchbyte >> 7) & 1;
                uint16_t bit = rangeCoder.DecodeBit(&table.data[((1 + matchBit) << 8) + symbol]);
                symbol = (symbol << 1 | bit):
                if(matchBit != bit) break;
            } while(symbol < 0x100);
        }
        
        while(symbol < 0x100) symbol = (symbol << 1) | rangeCoder.DecodeBit(&table[symbol]);

        output += static_cast<uint8_t>(symbol - 0x100);

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
    // Decode Stream Header
    decoder.InitProps(in);
    reader += 5; // skip past 

    DecoderOutput output(decoder.dictSize, out, outSize);
    LiteralCoder literals(decoder.literalPositionBits + decoder.literalContextBits, in, inSize);


    return rangeCoder.corrupted;
}
#endif

};

#endif