/*
 * Copyright 2026 Jas2365
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES
 * OR CONDITIONS OF ANY KIND, either express or implied.
 */

 /**
  * lexer.c
  * Lexer implementation - conversts source text into a flat token array
  */

#include <lexer/lexer.h>

#include <stdio.h>  // printf
#include <stdlib.h> // malloc, realloc, free
#include <string.h> // memcmp


// _Keyword_Table_

typedef struct Keyword {
    const i8*  word;
    i32        length;
    Token_Type kind;
} Keyword;

static const Keyword keywords[] = {
    { "fn",        2,  TOKEN_FN        },
    { "module",    6,  TOKEN_MODULE    },
    { "import",    6,  TOKEN_IMPORT    },
    { "internal",  8,  TOKEN_INTERNAL  },
    { "return",    6,  TOKEN_RETURN    },
    { "null",      4,  TOKEN_NULL      },
    { "export",    6,  TOKEN_EXPORT    },
    { "type",      4,  TOKEN_TYPE      },
    { "struct",    6,  TOKEN_STRUCT    },
    { "union",     5,  TOKEN_UNION     },
    { "private",   7,  TOKEN_PRIVATE   },
    { "persist",   7,  TOKEN_PERSIST   },
    { "inline",    6,  TOKEN_INLINE    },
    { "typeof",    6,  TOKEN_TYPEOF    },
    { "const",     5,  TOKEN_CONST     },
    { "if",        2,  TOKEN_IF        },
    { "else",      4,  TOKEN_ELSE      },
    { "while",     5,  TOKEN_WHILE     },
    { "for",       3,  TOKEN_FOR       },
    { "switch",    6,  TOKEN_SWITCH    },
    { "noinit",    6,  TOKEN_NOINIT    },
    { "true",      4,  TOKEN_TRUE      },
    { "false",     5,  TOKEN_FALSE     },
    { "break",     5,  TOKEN_BREAK     },
    { "continue",  8,  TOKEN_CONTINUE  },
    { "i8",        2,  TOKEN_KW_I8     },
    { "i16",       3,  TOKEN_KW_I16    },
    { "i32",       3,  TOKEN_KW_I32    },
    { "i64",       3,  TOKEN_KW_I64    },
    { "u8",        2,  TOKEN_KW_U8     },
    { "u16",       3,  TOKEN_KW_U16    },
    { "u32",       3,  TOKEN_KW_U32    },
    { "u64",       3,  TOKEN_KW_U64    },
    { "f32",       3,  TOKEN_KW_F32    },
    { "f64",       3,  TOKEN_KW_F64    },
    { "string",    6,  TOKEN_KW_STRING },
    { "bool",      4,  TOKEN_KW_BOOL   },
};


static const i32 keyword_count = sizeof(keywords) / sizeof(keywords[0]);

// _Helpers_

// can optimise here for strings
static inline b8 is_at_end(const Lexer* l) {
    return *l->current == '\0'; 
}

static inline i8 peek(const Lexer *l) {
    return *l->current;
}

// can also optimise here 
static inline i8 peek_next(const Lexer* l) {
    if(is_at_end(l)) return '\0';
    return *(l->current +1);
}

static inline i8 advance(Lexer* l) {
    i8 c = *l->current++;
    l->col++;
    return c;
}

// can optimise here with arrays
static inline b8 match(Lexer* l, i8 expected) {
    if(is_at_end(l)) return false;
    if(*l->current != expected) return false;
    l->current++;
    l->col++;
    return true;
}

static inline b8 is_digit(i8 c) {
    return c>= '0' && c <= '9';
}

static inline b8 is_alpha(i8 c) {
    return (c >= 'a' && c <= 'z' ) || 
           (c >= 'A' && c <= 'Z' ) || 
            c == '_';
}

static inline b8 is_alnum(i8 c) {
    return is_alpha(c) || is_digit(c);
}


// _Token_Array_Helper_
// can use generic here for optimisation

#define TOKEN_ARRAY_INITIAL_CAP 256

static null TokenArray_Push(Token_Array* arr, Token token) {
    if(arr->count >= arr->capacity) {
        arr->capacity *= 2;
        arr->tokens = realloc(arr->tokens, sizeof(Token) * (size_t)arr->capacity);
    }
    arr->tokens[arr->count++] = token;
}

static Token Make_Token(const Lexer* l,  Token_Type kind) {
    return (Token) {
        .kind = kind,
        .text = sv_from_token(l->start, (i32)(l->current - l->start)),
        .line = l->line,
        .col = l->col - (i32)(l->current - l->start),
    };
}

static Token Error_Token(const Lexer* l, i8* msg) {
    return (Token) {
        .kind = TOKEN_ERROR,
        .text = sv_lit(msg),
        .line = l->line,
        .col = l->col,
    };
}

// _String_Processing_Helpers_
// ensure string buffer has space fo n more bytes
static null ensure_string_space(Lexer* l, i32 needed) {
    if(l->string_len + needed >= l->string_cap) {
        i32 new_cap = (l->string_cap * 2) > (l->string_len + needed + 256) 
                    ? (l->string_cap * 2)
                    : (l->string_len + needed + 256);
        l->string_buf = realloc(l->string_buf, (size_t)new_cap);
        l->string_cap = new_cap;
    }
}

// process a single escape sequence, return the character to emit
// advances the lexer past the escape sequence
static i8 process_escape(Lexer* l) {
    if(is_at_end(l)) return '\0'; //
    i8 c = advance(l);
    switch (c) {
        case 'n' :   return '\n';
        case 'r' :   return '\r';
        case 't' :   return '\t';
        case '\\':   return '\\';
        case '"' :   return '\"';
        case '\'':   return '\'';
        case '0' :   return '\0';
        // todo: stage 4: /xHH hex escapes, \uXXXX unicode
        default: return c;
    }
}

// _Whitespaces_And_Comments_

// def need enums and better code formatting and smaler functions

static null skip_whitespaces(Lexer* l) {
    for(;;) {
        switch (peek(l))    {
            case ' ':
            case '\r':
            case '\t':
                advance(l);
                break;
            case '\n':
                l->line++;
                l->col = 0;
                advance(l);
                break;
            case '/':
                // single line comment
                if(peek_next(l) == '/') {
                    while(peek(l) != '\n' && !is_at_end(l)) {
                        advance(l);
                    }
                // block comment /* */
                } else if(peek_next(l) == '*') {
                    advance(l);  // consume /
                    advance(l); // consume *
                    while(!is_at_end(l)) {
                        if(peek(l) == '\n') { l->line++; l->col = 0; }
                        if(peek(l) == '*' && peek_next(l) == '/') {
                            advance(l); 
                            advance(l); // consumes */
                            break;
                        }
                        advance(l);
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

// _Scan_


static Token scan_token(Lexer* l) {
    skip_whitespaces(l);
    l->start = l->current;

    if(is_at_end(l)) return Make_Token(l, TOKEN_EOF); // end of file

    i8 c = advance(l);                   // current char

    // _identifier_or_keyword_

    if(is_alpha(c)) {
        while(is_alnum(peek(l))) advance(l);

        i32 len = (i32)(l->current - l->start);
        for(i32 i = 0; i< keyword_count; i++){
            if(keywords[i].length == len && 
                memcmp(l->start, keywords[i].word, (size_t)len) == 0) {
                    return Make_Token(l, keywords[i].kind);
            }
        }
        return Make_Token(l, TOKEN_IDENT);
    }


    // _Integer_or_float_literal_

    if(is_digit(c)) {
        while(is_digit(peek(l))) advance(l);
        if(peek(l) == '.' && is_digit(peek_next(l))) {
            advance(l); // consume '.' decimal
            while(is_digit(peek(l))) advance(l);   // Note: this can cause bugs
            return Make_Token(l, TOKEN_FLOAT_LIT);
        }
        return Make_Token(l, TOKEN_INT_LIT);
    }

    // _Char_literal_

    if(c == '\'') {
        if(peek(l) == '\\') advance(l); // escape sequence '\n'
        if(is_at_end(l)) return Error_Token(l, "unterminated char literal");
        advance(l);     // the char
        if(peek(l) != '\'') return Error_Token(l, "char literal too long");
        advance(l);     // the closing '
        return Make_Token(l, TOKEN_CHAR_LIT);
    }

    // _String_literal_
    if( c == '"' ) {
        // save start position in string sequeces
        i32 str_start = l->string_len;

        // process string contents, handling escape sequences
        while(peek(l) != '"' && !is_at_end(l)) {
            if(peek(l) == '\n') { l->line++; l->col = 0; } // should make this into a functon
            
            if(peek(l) == '\\') {
                advance(l); // skip the backslash

                i8 escaped = process_escape(l);
                ensure_string_space(l, 1);
                l->string_buf[l->string_len++] = escaped;
            } else {
                ensure_string_space(l, 1);
                l->string_buf[l->string_len++] = advance(l);
            }
        }
        if(is_at_end(l)) return Error_Token(l, "unterminated string");
        advance(l); // closing "

        Token tok = {
            .kind = TOKEN_STRING_LIT,
            .text = sv_from_token(l->string_buf + str_start,
                                  l->string_len - str_start),
            .line = l->line,
            .col  = l->col - (i32)(l->current - l->start),
        };

        return tok;
    }

    // _Symbols_

    switch (c) {

        case '(': return Make_Token(l, TOKEN_LPAREN);
        case ')': return Make_Token(l, TOKEN_RPAREN);
        case '{': return Make_Token(l, TOKEN_LBRACE);
        case '}': return Make_Token(l, TOKEN_RBRACE);
        case '[': return Make_Token(l, TOKEN_LBRACKET);
        case ']': return Make_Token(l, TOKEN_RBRACKET);
        case ',': return Make_Token(l, TOKEN_COMMA);
        case ';': return Make_Token(l, TOKEN_SEMICOLON);
        case ':': return Make_Token(l, TOKEN_COLON);
        case '.': return Make_Token(l, TOKEN_DOT);
        case '%': return Make_Token(l, TOKEN_PERCENT);
        case '+': return Make_Token(l, TOKEN_PLUS);
        case '/': return Make_Token(l, TOKEN_SLASH);
        case '*': return Make_Token(l, TOKEN_STAR);

        case '-': return match(l, '>')? Make_Token(l, TOKEN_ARROW)  : Make_Token(l, TOKEN_MINUS);
        case '=': return match(l, '=')? Make_Token(l, TOKEN_EQ)     : Make_Token(l, TOKEN_ASSIGN);
        case '!': return match(l, '=')? Make_Token(l, TOKEN_NEQ)    : Make_Token(l, TOKEN_BANG);
        case '&': return match(l, '&')? Make_Token(l, TOKEN_AND)    : Make_Token(l, TOKEN_AMP);
        case '|': return match(l, '|')? Make_Token(l, TOKEN_OR)     : Make_Token(l, TOKEN_PIPE);
        case '<': return match(l, '<')? Make_Token(l, TOKEN_LSHIFT) :
                         match(l, '=')? Make_Token(l, TOKEN_LTE)    : Make_Token(l, TOKEN_LT);
        case '>': return match(l, '>')? Make_Token(l, TOKEN_RSHIFT) :
                         match(l, '=')? Make_Token(l, TOKEN_GTE)    : Make_Token(l, TOKEN_GT);
        case '~': return Make_Token(l, TOKEN_TILDE);
        case '^': return Make_Token(l, TOKEN_CARET);

        default: return Error_Token(l, "unexpected character");
    
    }
}


// _API_
// need string here 

null Lexer_Init(Lexer* lexer, const i8* source) {
    lexer->source   = source;
    lexer->start    = source;
    lexer->current  = source;
    lexer->line     = 1;
    lexer->col      = 1;

    // init string buf for processed string literals
    lexer->string_cap = 1024; // start with 1kb
    lexer->string_len = 0;
    lexer->string_buf = malloc((size_t)lexer->string_cap);
}

Token_Array Lexer_Tokenize(Lexer* lexer) {
    Token_Array arr = {
        .tokens     = malloc(sizeof(Token) * TOKEN_ARRAY_INITIAL_CAP),
        .count      = 0,
        .capacity   = TOKEN_ARRAY_INITIAL_CAP,
        .string_buf = nullptr, // will transfer from lexer
    };
    
    for(;;) {
        Token t = scan_token(lexer);
        TokenArray_Push(&arr, t);
        if(t.kind == TOKEN_EOF || t.kind == TOKEN_ERROR) break;
    }

    // transfer string buffer ownership to token array
    arr.string_buf = lexer->string_buf;
    lexer->string_buf = nullptr;

    return arr;
}

null TokenArray_Free(Token_Array* arr) {
    free(arr->tokens);
    free(arr->string_buf);
    arr->tokens     = nullptr;
    arr->string_buf = nullptr;
    arr->count      = 0;
    arr->capacity   = 0;
}

// _Debug_

const i8* Token_TypeName(Token_Type kind) {
    switch (kind) {
        case TOKEN_INT_LIT:    return "INT_LIT";
        case TOKEN_FLOAT_LIT:  return "FLOAT_LIT";
        case TOKEN_STRING_LIT: return "STRING_LIT";
        case TOKEN_CHAR_LIT:   return "CHAR_LIT";
        case TOKEN_IDENT:      return "IDENT";
        case TOKEN_FN:         return "FN";
        case TOKEN_MODULE:     return "MODULE";
        case TOKEN_IMPORT:     return "IMPORT";
        case TOKEN_INTERNAL:   return "INTERNAL";
        case TOKEN_RETURN:     return "RETURN";
        case TOKEN_NULL:       return "NULL";
        case TOKEN_EXPORT:     return "EXPORT";
        case TOKEN_TYPE:       return "TYPE";
        case TOKEN_STRUCT:     return "STRUCT";
        case TOKEN_UNION:      return "UNION";
        case TOKEN_PRIVATE:    return "PRIVATE";
        case TOKEN_PERSIST:    return "PERSIST"; 
        case TOKEN_INLINE:     return "INLINE";
        case TOKEN_TYPEOF:     return "TYPEOF";
        case TOKEN_CONST:      return "CONST";
        case TOKEN_IF:         return "IF";
        case TOKEN_ELSE:       return "ELSE";
        case TOKEN_WHILE:      return "WHILE";
        case TOKEN_FOR:        return "FOR";
        case TOKEN_SWITCH:     return "SWITCH";
        case TOKEN_NOINIT:     return "NOINIT";
        case TOKEN_TRUE:       return "TRUE";
        case TOKEN_FALSE:      return "FALSE";
        case TOKEN_BREAK:      return "BREAK";
        case TOKEN_CONTINUE:   return "CONTINUE";
        case TOKEN_KW_I8:      return "I8";
        case TOKEN_KW_I16:     return "I16";
        case TOKEN_KW_I32:     return "I32";
        case TOKEN_KW_I64:     return "I64";
        case TOKEN_KW_U8:      return "U8";
        case TOKEN_KW_U16:     return "U16";
        case TOKEN_KW_U32:     return "U32";
        case TOKEN_KW_U64:     return "U64";
        case TOKEN_KW_F32:     return "F32";
        case TOKEN_KW_F64:     return "F64";
        case TOKEN_KW_STRING:  return "STRING";
        case TOKEN_KW_BOOL:    return "BOOL";
        case TOKEN_LPAREN:     return "LPAREN";
        case TOKEN_RPAREN:     return "RPAREN";
        case TOKEN_LBRACE:     return "LBRACE";
        case TOKEN_RBRACE:     return "RBRACE";
        case TOKEN_LBRACKET:   return "LBRACKET";
        case TOKEN_RBRACKET:   return "RBRACKET";
        case TOKEN_COMMA:      return "COMMA";
        case TOKEN_SEMICOLON:  return "SEMICOLON";
        case TOKEN_COLON:      return "COLON";
        case TOKEN_DOT:        return "DOT";
        case TOKEN_ARROW:      return "ARROW";
        case TOKEN_ASSIGN:     return "ASSIGN";
        case TOKEN_PLUS:       return "PLUS";
        case TOKEN_MINUS:      return "MINUS";
        case TOKEN_STAR:       return "STAR";
        case TOKEN_SLASH:      return "SLASH";
        case TOKEN_PERCENT:    return "PERCENT";
        case TOKEN_EQ:         return "EQ";
        case TOKEN_NEQ:        return "NEQ";
        case TOKEN_LT:         return "LT";
        case TOKEN_GT:         return "GT";
        case TOKEN_LTE:        return "LTE";
        case TOKEN_GTE:        return "GTE";
        case TOKEN_AND:        return "AND";
        case TOKEN_OR:         return "OR";
        case TOKEN_BANG:       return "BANG";
        case TOKEN_AMP:        return "AMP";
        case TOKEN_PIPE:       return "PIPE";
        case TOKEN_TILDE:      return "TILDE";
        case TOKEN_CARET:      return "CARET";
        case TOKEN_LSHIFT:     return "LSHIFT";
        case TOKEN_RSHIFT:     return "RSHIFT";
        case TOKEN_EOF:        return "EOF";
        case TOKEN_ERROR:      return "ERROR";
        default:               return "UNKNOWN";
    }
}


