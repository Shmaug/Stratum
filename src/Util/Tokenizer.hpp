#pragma once

#include "../Stratum.hpp"

namespace stm {

class Tokenizer {
public:
    STRATUM_API Tokenizer(const std::ifstream& stream, const std::set<char> delims);
    STRATUM_API Tokenizer(const std::string& buffer , const std::set<char> delims);
    STRATUM_API ~Tokenizer();

    STRATUM_API bool Next(std::string& token);
    STRATUM_API bool Next(float& token);
    STRATUM_API bool Next(int32_t& token);
    STRATUM_API bool Next(uint32_t& token);

private:
    char* mBuffer = nullptr;
    size_t mLength = 0;
    std::set<char> mDelimiters;
    size_t mCurrent = 0;
};

}