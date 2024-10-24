#include <fstream>
#include <cstdint>
#include <vector>
#include <iostream>
#define LZMA_IMPLEMENTATION
#include "lzma.hpp"

int main(int argc, char* argv[]){
    if(argc < 2){
        return 1;
    }

    std::vector<uint8_t> fileBuffer, outputBuffer;

    std::fstream decompFile(std::string(argv[1]), std::ios_base::openmode::_S_in);
    
    decompFile.seekg(0, std::ios_base::end);
    fileBuffer.resize(decompFile.tellg());
    decompFile.seekg(0, std::ios_base::beg);

    decompFile.read(reinterpret_cast<char*>(fileBuffer.data()), fileBuffer.size());

    uint8_t* out;
    std::size_t outSize;

    if(BeeCode::LZMADecompress(fileBuffer.data(), fileBuffer.size(), out, outSize)){
        std::cout << "[BeeCode] Decompression success!" << std::endl; 
    } else {
        std::cout << "[BeeCode] Decompression fail!" << std::endl;
    }
    
    std::fstream outFile(std::string(argv[1])+".decompressed", std::ios_base::openmode::_S_out);
    outFile.write((char*)out, outSize);

    return 0;
}