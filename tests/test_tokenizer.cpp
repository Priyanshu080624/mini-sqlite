// ============================================================================
// test_tokenizer.cpp — Tests for the Tokenizer
// ============================================================================

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "common/token.h"
#include "frontend/tokenizer.h"

static int passed = 0, failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [" << msg << "] line " << __LINE__ << "\n"; \
            failed++; \
        } else { \
            passed++; \
        } \
    } while(0)

static std::vector<Token> lex(const std::string& sql) {
    Tokenizer tok(sql);
    return tok.tokenize();
}

// ── Individual tests ──────────────────────────────────────────────────────────

static void testKeywords() {
    auto tokens = lex("SELECT FROM WHERE INSERT INTO VALUES CREATE TABLE INDEX");
    CHECK(tokens[0].type == TokenType::SELECT,  "SELECT keyword");
    CHECK(tokens[1].type == TokenType::FROM,    "FROM keyword");
    CHECK(tokens[2].type == TokenType::WHERE,   "WHERE keyword");
    CHECK(tokens[3].type == TokenType::INSERT,  "INSERT keyword");
    CHECK(tokens[4].type == TokenType::INTO,    "INTO keyword");
    CHECK(tokens[5].type == TokenType::VALUES,  "VALUES keyword");
    CHECK(tokens[6].type == TokenType::CREATE,  "CREATE keyword");
    CHECK(tokens[7].type == TokenType::TABLE,   "TABLE keyword");
    CHECK(tokens[8].type == TokenType::INDEX,   "INDEX keyword");
}

static void testNewKeywords() {
    auto tokens = lex("NULL HAVING LIMIT ASC DESC PRIMARY KEY");
    CHECK(tokens[0].type == TokenType::NULL_KW, "NULL keyword");
    CHECK(tokens[1].type == TokenType::HAVING,  "HAVING keyword");
    CHECK(tokens[2].type == TokenType::LIMIT,   "LIMIT keyword");
    CHECK(tokens[3].type == TokenType::ASC,     "ASC keyword");
    CHECK(tokens[4].type == TokenType::DESC,    "DESC keyword");
    CHECK(tokens[5].type == TokenType::PRIMARY, "PRIMARY keyword");
    CHECK(tokens[6].type == TokenType::KEY,     "KEY keyword");
}

static void testCaseInsensitive() {
    auto tokens = lex("select Select SELECT sElEcT");
    for (int i = 0; i < 4; ++i)
        CHECK(tokens[i].type == TokenType::SELECT, "case-insensitive SELECT");
    // Lexeme should preserve original casing
    CHECK(tokens[0].lexeme == "select",  "lexeme preserves lowercase");
    CHECK(tokens[1].lexeme == "Select",  "lexeme preserves mixed case");
    CHECK(tokens[2].lexeme == "SELECT",  "lexeme preserves uppercase");
}

static void testIdentifiers() {
    auto tokens = lex("users user_id _temp table1");
    CHECK(tokens[0].type == TokenType::IDENTIFIER && tokens[0].lexeme == "users",  "identifier: users");
    CHECK(tokens[1].type == TokenType::IDENTIFIER && tokens[1].lexeme == "user_id","identifier: user_id");
    CHECK(tokens[2].type == TokenType::IDENTIFIER && tokens[2].lexeme == "_temp",  "identifier: _temp");
    CHECK(tokens[3].type == TokenType::IDENTIFIER && tokens[3].lexeme == "table1", "identifier: table1");
}

static void testIntegerLiteral() {
    auto tokens = lex("42 0 9999");
    CHECK(tokens[0].type == TokenType::INTEGER_LITERAL && tokens[0].lexeme == "42",   "int: 42");
    CHECK(tokens[1].type == TokenType::INTEGER_LITERAL && tokens[1].lexeme == "0",    "int: 0");
    CHECK(tokens[2].type == TokenType::INTEGER_LITERAL && tokens[2].lexeme == "9999", "int: 9999");
}

static void testRealLiteral() {
    auto tokens = lex("3.14 0.5 100.0");
    CHECK(tokens[0].type == TokenType::REAL_LITERAL && tokens[0].lexeme == "3.14",  "real: 3.14");
    CHECK(tokens[1].type == TokenType::REAL_LITERAL && tokens[1].lexeme == "0.5",   "real: 0.5");
    CHECK(tokens[2].type == TokenType::REAL_LITERAL && tokens[2].lexeme == "100.0", "real: 100.0");
}

static void testStringLiteral() {
    auto tokens = lex("'hello' 'world'");
    CHECK(tokens[0].type == TokenType::STRING_LITERAL && tokens[0].lexeme == "hello", "string: hello");
    CHECK(tokens[1].type == TokenType::STRING_LITERAL && tokens[1].lexeme == "world", "string: world");
}

static void testOperators() {
    auto tokens = lex("= != < > <= >=");
    CHECK(tokens[0].type == TokenType::EQUALS,         "op: =");
    CHECK(tokens[1].type == TokenType::NOT_EQUALS,     "op: !=");
    CHECK(tokens[2].type == TokenType::LESS_THAN,      "op: <");
    CHECK(tokens[3].type == TokenType::GREATER_THAN,   "op: >");
    CHECK(tokens[4].type == TokenType::LESS_EQUALS,    "op: <=");
    CHECK(tokens[5].type == TokenType::GREATER_EQUALS, "op: >=");
}

static void testPunctuation() {
    auto tokens = lex(", ; ( ) * .");
    CHECK(tokens[0].type == TokenType::COMMA,     "punct: ,");
    CHECK(tokens[1].type == TokenType::SEMICOLON, "punct: ;");
    CHECK(tokens[2].type == TokenType::LPAREN,    "punct: (");
    CHECK(tokens[3].type == TokenType::RPAREN,    "punct: )");
    CHECK(tokens[4].type == TokenType::STAR,      "punct: *");
    CHECK(tokens[5].type == TokenType::DOT,       "punct: .");
}

static void testComment() {
    auto tokens = lex("SELECT -- this is a comment\nFROM");
    CHECK(tokens[0].type == TokenType::SELECT, "comment: SELECT before --");
    CHECK(tokens[1].type == TokenType::FROM,   "comment: FROM after newline");
}

static void testEOF() {
    auto tokens = lex("SELECT");
    CHECK(tokens.back().type == TokenType::END_OF_FILE, "EOF at end");
}

static void testLineNumbers() {
    auto tokens = lex("SELECT\nFROM\nWHERE");
    CHECK(tokens[0].line == 1, "line 1: SELECT");
    CHECK(tokens[1].line == 2, "line 2: FROM");
    CHECK(tokens[2].line == 3, "line 3: WHERE");
}

static void testFullQuery() {
    auto tokens = lex("SELECT name FROM users WHERE id = 5;");
    // Should produce: SELECT IDENTIFIER FROM IDENTIFIER WHERE IDENTIFIER EQUALS INTEGER_LITERAL SEMICOLON EOF
    CHECK(tokens[0].type == TokenType::SELECT,          "query: SELECT");
    CHECK(tokens[1].type == TokenType::IDENTIFIER,      "query: name");
    CHECK(tokens[1].lexeme == "name",                   "query: name lexeme");
    CHECK(tokens[2].type == TokenType::FROM,            "query: FROM");
    CHECK(tokens[3].type == TokenType::IDENTIFIER,      "query: users");
    CHECK(tokens[4].type == TokenType::WHERE,           "query: WHERE");
    CHECK(tokens[5].type == TokenType::IDENTIFIER,      "query: id");
    CHECK(tokens[6].type == TokenType::EQUALS,          "query: =");
    CHECK(tokens[7].type == TokenType::INTEGER_LITERAL, "query: 5");
    CHECK(tokens[7].lexeme == "5",                      "query: 5 lexeme");
    CHECK(tokens[8].type == TokenType::SEMICOLON,       "query: ;");
    CHECK(tokens[9].type == TokenType::END_OF_FILE,     "query: EOF");
}

static void testUnexpectedChar() {
    bool threw = false;
    try { lex("SELECT @ name"); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw, "unexpected char throws");
}

static void testUnterminatedString() {
    bool threw = false;
    try { lex("'hello"); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw, "unterminated string throws");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    testKeywords();
    testNewKeywords();
    testCaseInsensitive();
    testIdentifiers();
    testIntegerLiteral();
    testRealLiteral();
    testStringLiteral();
    testOperators();
    testPunctuation();
    testComment();
    testEOF();
    testLineNumbers();
    testFullQuery();
    testUnexpectedChar();
    testUnterminatedString();

    std::cout << "Tokenizer: " << passed << " passed, " << failed << " failed.\n";
    return failed > 0 ? 1 : 0;
}
