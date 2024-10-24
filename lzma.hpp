#ifndef __BEE_LZMA__
#define __BEE_LZMA__
#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace BeeCode {

bool LZMADecodeStream(uint8_t* in, std::size_t inSize, std::size_t* out, uint8_t& outSize);

#ifdef LZMA_IMPLEMENTATION

constexpr uint16_t kNumLenToPosStates = 4;
constexpr uint16_t kEndPosModelIndex  = 14;
constexpr uint16_t kNumFullDistances  = (1 << (kEndPosModelIndex >> 1));
constexpr uint16_t kNumAlignBits = 4;
constexpr uint16_t kMatchMinLen = 2;
constexpr uint16_t kNumStates = 12;
constexpr uint16_t kPosBitsMax = 4;

constexpr uint16_t InitProb(uint32_t numBits) { return (1 << numBits) / 2; } 

template<typename T>
T swap(T v){
    T value = v;
    T swapped = 0;
    for(std::size_t i = 0; i < sizeof(v); i++){ swapped = (v & 0xFF) | swapped << 8; v >>= 8; } 
    return swapped;
}

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

struct DecoderOutput {
    uint8_t* buffer { nullptr };
    uint8_t* position { nullptr };
    std::size_t size { 0 };
    uint16_t runningTotal { 0 };

    uint8_t* stream { nullptr };
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
        return position == buffer || position != (buffer + size);
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


template<uint32_t numBits, uint32_t size, uint16_t overrideProb=0xFFFF>
struct ProbTable {
    uint16_t data[size] = { 0 };
    ProbTable(){
        for (size_t i = 0; i < size; i++){ if(overrideProb != 0xFFFF) data[i] = InitProb(11); else data[i] = overrideProb;  }
    }
};

template<uint32_t totalModelBits, uint32_t numMoveBits>
struct RangeCoder {
    uint8_t* streamPtr { nullptr };
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

uint16_t DecodeBitTreeReverse(uint16_t* probs, uint32_t bits, RangeCoder<11, 5>& range){
    uint16_t m = 1;
    uint16_t symb = 0;
    for (uint16_t i = 0; i < bits; i++){
        uint16_t b = range.DecodeBit(&probs[m]);
        m = (m << 1) + b;
        symb |= b << i;
    }
    return symb;   
}


template<uint32_t bits>
struct BitTreeCoder {
    ProbTable<11, 1 << bits> probs;

    uint16_t DecodeBitTree(RangeCoder<11, 5>& range){
        uint16_t m = 1;
        for (uint16_t i = 0; i < bits; i++) {
            m = (m << 1) + range.DecodeBit(&probs.data[m]);
        }
        return m - ((uint16_t)1 << bits);
    }

    uint16_t DecodeBitTreeReverse(RangeCoder<11, 5>& range){
        return DecodeBitTreeReverse(&probs.data[0], bits, range);
    }
};

struct LiteralCoder {
    ProbTable<11, 0x300>* literalProbabilities { nullptr };
    
    void DecodeLiteral(LZMADecoderInfo info, uint16_t state, uint8_t*& stream, DecoderOutput& output, uint32_t rep){
        uint16_t prevByte = 0;
    
        if(!output.IsEmpty()){
            prevByte = output[1];
        }

        uint16_t symbol = 1;
        uint16_t literalState = (output.runningTotal & ((1 << info.literalPositionBits) - 1) << info.literalContextBits) + (prevByte >> (8 - info.literalContextBits));

        ProbTable<11, 0x300> table = literalProbabilities[literalState];
    
        RangeCoder<11, 5> rangeCoder(stream);

        if(state >= 7){
            uint16_t matchbyte = output[rep0 + 1];
            do {
                uint16_t matchBit = (matchbyte >> 7) & 1;
                uint16_t bit = rangeCoder.DecodeBit(&table.data[((1 + matchBit) << 8) + symbol]);
                symbol = (symbol << 1) | bit:
                if(matchBit != bit) break;
            } while(symbol < 0x100);
        }
        
        while(symbol < 0x100) symbol = (symbol << 1) | rangeCoder.DecodeBit(&table[symbol]);

        output += static_cast<uint8_t>(symbol - 0x100);

    }

    LiteralCoder(){}

    LiteralCoder(LZMADecoderInfo info, uint8_t*& stream, std::size_t size){
        literalProbabilities = new ProbTable<11, 0x300>[info.literalContextBits + info.literalPositionBits];
    }

    ~LiteralCoder(){
        if(literalProbabilities != nullptr) delete[] literalProbabilities;
    }
};

struct LenCoder {
    uint16_t c1 { InitProb(11) };
    uint16_t c2 { InitProb(11) };
    BitTreeCoder<3> lowCoder[1 << 4];
    BitTreeCoder<3> midCoder[1 << 4];
    BitTreeCoder<8> highCoder;

    void Decode(RangeCoder<11, 5> coder, uint16_t pstate){
        if(coder.DecodeBit(&c1) == 0)
            return lowCoder[pstate].DecodeBitTree(coder);
        if(coder.DecodeBit(&c2) == 0)
            return 8 + midCoder[pstate].DecodeBitTree(coder);
        return 16 + highCoder.DecodeBitTree(coder);
    }
}

struct LZMADecoder {
    uint16_t state { 0 };
    LenCoder lenDecoder;
    LenCoder repLenDecoder;

    LiteralCoder literalDecoder;
    RangeCoder<11, 5> rangeDecoder;

    BitTreeCoder<kNumAlignBits> alignDecoder;
    BitTreeCoder<6> posSlotDecoder[kNumLenToPosStates];
    uint16_t posDecoders[1 + kNumFullDistances - kEndPosModelIndex];

    LZMADecoderInfo decoderInfo;
    DecoderOutput decoderOutput;
    // Decode Stream Header

    uint16_t DecodeDist(uint16_t len){
        uint16_t lenState = len;

        if(lenState > kNumLenToPosStates - 1){
            lenState = kNumLenToPosStates - 1;
        }

        uint16_t posSlot = PosSlotDecoder[lenState].DecodeBitTree(rangeDecoder);
    
        if(posSlot < 4) return posSlot;

        uint16_t numDirectBits = static_cast<uint16_t>((posSlot >> 1) - 1);
        uint32_t dist = ((2 | (posSlot & 1)) << numDirectBits);
        
        if(posSlot < kEndPosModelIndex){
            dist += DecodeBitTreeReverse(posDecoders + dist - posSlot, numDirectBits, rangeDecoder);
        } else {
            dist += rangeDecoder.DecodeDirect(numDirectBits - kNumAlignBits) << kNumAlignBits;
            dist += alignDecoder.DecodeBitTreeReverse(rangeDecoder);
        }

        return dist;
    }

    LZMADecoder(uint8_t* in, std::size_t inSize, uint8_t* out, std::size_t& outSize){
        uint8_t* reader = in;
    
        // set up some prob tables
        ProbTable<11, kNumStates << kPosBitsMax> isMatch;
        ProbTable<11, kNumStates> isRep;
        ProbTable<11, kNumStates> isRepG0;
        ProbTable<11, kNumStates> isRepG1;
        ProbTable<11, kNumStates> isRepG2;
        ProbTable<11, kNumStates << kPosBitsMax> isRep0Long;

        decoderInfo.InitProps(in);
        reader += 5; // skip past 
    
        if(decoderInfo.decompressedSize != 0xFFFFFFFFFFFFFFFF){
            out = new uint8_t[decoderInfo.decompressedSize];
            outSize = decoderInfo.decompressedSize;
        } else {
            out = new uint8_t[inSize*2];
            outSize = 0;

        }
    
        decoderOutput = DecoderOutput(decoderInfo.dictSize, out, outSize);
        literalDecoder = LiteralCoder(decoderInfo, reader, inSize);
    }

    uint16_t UpdateLiteral() {
        if(state < 4) return 0;
        else if(state < 10) return state - 3;
        else return state - 6;
    }

    uint16_t UpdateMatch()    { return state < 7 ? 7 : 10; }
    uint16_t UpdateRep()      { return state < 7 ? 8 : 11; }
    uint16_t UpdateShortRep() { return state < 7 ? 9 : 11; }

    void Decode(){

    }

};

bool LZMADecompress(uint8_t* in, std::size_t inSize, uint8_t* out, std::size_t& outSize){    
    LZMADecoder decoder(in, inSize, out, outSize);

    decoder.Decode();

    return false;
}
#endif

};

#endif