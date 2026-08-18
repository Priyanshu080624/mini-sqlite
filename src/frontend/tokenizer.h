#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "common/token.h"

// Scans a raw SQL string into a flat vector of Tokens.
//
// Usage:
//   Tokenizer tok(sql_text);
//   std::vector<Token> tokens = tok.tokenize();
//
// The returned vector always ends with an END_OF_FILE token.
// Throws std::runtime_error on unrecognised input.
class Tokenizer {
public:
    explicit Tokenizer(std::string source);

    std::vector<Token> tokenize();

private:
    std::string source_;
    size_t      pos_  = 0;
    int         line_ = 1;

    static const std::unordered_map<std::string, TokenType> keywords_;

    bool isAtEnd()   const;
    char peek()      const;
    char peekNext()  const;
    char advance();
    bool match(char expected);

    void  skipWhitespaceAndComments();
    Token scanToken();
    Token scanIdentifierOrKeyword();
    Token scanNumber();
    Token scanString();
};
