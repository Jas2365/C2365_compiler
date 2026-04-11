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

#pragma once 

#include <ints.h>
#include <defs.h>
#include <strview.h>

// _Token_Types_

typedef enum Token_Type {
    
    // _Literals_
    TOKEN_INT_LIT,          // 23
    TOKEN_FLOAT_LIT,        // 3.14
    TOKEN_STRING_LIT,       // "hello"
    TOKEN_CHAR_LIT,         // 'j'

    // _Identifiers_
    TOKEN_IDENT,            // add, num, etc

    // _Keywords
    TOKEN_FN,               // fn
    TOKEN_MODULE,           // module
    TOKEN_IMPORT,           // import
    TOKEN_INTERNAL,         // internal
    TOKEN_RETURN,           // return
    TOKEN_NULL,             // null
    TOKEN_EXPORT,           // export (might remove it and make everything public)
    TOKEN_TYPE,             // type (replace typedef)
    TOKEN_STRUCT,           // struct
    TOKEN_UNION,            // union
    TOKEN_PRIVATE,          // private
    TOKEN_PERSIST,          // persist
    TOKEN_INLINE,           // inline
    TOKEN_TYPEOF,           // typeof
    TOKEN_CONST,            // const
    TOKEN_IF,               // if
    TOKEN_ELSE,             // else
    TOKEN_WHILE,            // while
    TOKEN_FOR,              // for
    TOKEN_SWITCH,           // switch
    TOKEN_NOINIT,           // noinit
    TOKEN_TRUE,             // true
    TOKEN_FALSE,            // false
    TOKEN_BREAK,            // break
    TOKEN_CONTINUE,         // continue


    // _Type_Keywords_
    TOKEN_KW_I8,            // i8
    TOKEN_KW_I16,           // i16
    TOKEN_KW_I32,           // i32
    TOKEN_KW_I64,           // i64
    TOKEN_KW_U8,            // u8
    TOKEN_KW_U16,           // u16
    TOKEN_KW_U32,           // u32
    TOKEN_KW_U64,           // u64
    TOKEN_KW_F32,           // f32
    TOKEN_KW_F64,           // f64
    TOKEN_KW_STRING,        // string
    TOKEN_KW_BOOL,          // bool

    // _Symbols_
    TOKEN_LPAREN,           // (
    TOKEN_RPAREN,           // )
    TOKEN_LBRACE,           // {
    TOKEN_RBRACE,           // }
    TOKEN_LBRACKET,         // [
    TOKEN_RBRACKET,         // ]
    TOKEN_COMMA,            // ,
    TOKEN_SEMICOLON,        // ;
    TOKEN_COLON,            // :
    TOKEN_DOT,              // .
    TOKEN_ARROW,            // ->
    TOKEN_ASSIGN,           // =

    // _Arithmetic_
    TOKEN_PLUS,             // +
    TOKEN_MINUS,            // -
    TOKEN_STAR,             // *
    TOKEN_SLASH,            // /
    TOKEN_PERCENT,          // %

    // _Comparison_
    TOKEN_EQ,               // ==
    TOKEN_NEQ,              // !=
    TOKEN_LT,               // <
    TOKEN_GT,               // >
    TOKEN_LTE,              // <=
    TOKEN_GTE,              // >=

    // _Logical_
    TOKEN_AND,              // &&
    TOKEN_OR,               // ||
    TOKEN_BANG,             // !
    
    // _Bitwise_
    TOKEN_AMP,              // &
    TOKEN_PIPE,             // |
    TOKEN_TILDE,            // ~
    TOKEN_CARET,            // ^
    TOKEN_LSHIFT,           // <<
    TOKEN_RSHIFT,           // >>

    // _Special_
    TOKEN_EOF,              // end of file
    TOKEN_ERROR,            // Unrecognised character

} Token_Type;

// _Token_

typedef struct Token {

    Token_Type  kind;
    String_View text; // slice into the source buffer - never null terminated
    
    i32         line; // 1-based line   number
    i32         col;  // 1-based column number

} Token;

// _Lexer_State_

typedef struct Lexer {
    
    const i8* source;   // full source text
    const i8* start;    // start of the current token
    const i8* current;  // current scan position

    i32       line;
    i32       col;

} Lexer;

// _Token_Array_

typedef struct Token_Array {

    Token*  tokens;
    i32     count;
    i32     capacity;

} Token_Array;

// _API_

null Lexer_Init (Lexer* lexer, const i8* source);
Token_Array Lexer_Tokenize(Lexer* lexer);
null TokenArray_Free(Token_Array* arr);
const i8* Token_TypeName(Token_Type kind);