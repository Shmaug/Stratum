#pragma once

#include <Util/Util.hpp>

class Tokenizer {
public:
    STRATUM_API Tokenizer(const std::ifstream& stream, const std::set<char> delims);
    STRATUM_API Tokenizer(const std::string& buffer , const std::set<char> delims);
    STRATUM_API ~Tokenizer();

    STRATUM_API bool Next(std::string& token);
    STRATUM_API bool Next(float& token);
    STRATUM_API bool Next(int& token);
    STRATUM_API bool Next(unsigned int& token);

private:
    char* mBuffer;
    size_t mLength;
    
    std::set<char> mDelimiters;

    size_t mCurrent;
};