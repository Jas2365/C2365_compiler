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

/*
 * ast.h
 * AST node definitions.
 * Every node is arena-allocated — no individual frees needed.
 *
 * Design decisions encoded here:
 *   - fn + -> for functions
 *   - type  replaces typedef
 *   - module / import / internal / export for module system
 *   - . for all member access — compiler handles pointer deref
 *   - * for pointers, star count encodes array rank
 *   - null type, null*, nullptr
 *   - const default on params, ! for mutable
 *   - persist for static locals
 *   - private for struct members
 *   - string as builtin fat pointer type
 *   - noinit to skip zero-init
 *   - typeof is compile-time only — resolved before codegen
 *   - generics: List(T), fn push(T)(...)
 *   - arrays: i32[N] fixed stack, i32[] dynamic heap/arena
 *   - inline fn
 *   - string& for share (no copy)
 */

#pragma once 

#include <ints.h>
#include <strview.h>


// _Forward_Declarations_

typedef struct Ast_Node  Ast_Node;
typedef struct Ast_Type  Ast_Type;
typedef struct Ast_Param Ast_Param;
typedef struct Ast_Field Ast_Field;
typedef struct Ast_Case  Ast_Case;

// _Node_Kind_

typedef enum Node_Kind {

    // _Top_Level_
    NODE_PROGRAM,               // root - list of top level decls
    NODE_MODULE,                // module math;
    NODE_IMPORT,                // import math;
    NODE_FN_DECL,               // fn add(i32 a, i32 b) -> i32 { ... }
    NODE_TYPE_ALIAS,            // type vec2 = struct { ... }
    NODE_STRUCT_DECL,           // struct { fields }
    NODE_UNION_DECL,            // union { fields }

    // _Statements_
    NODE_BLOCK,                 // { stmt* }
    NODE_VAR_DECL,              // i32 x = 5; / i32 x = noinit;
    NODE_PERSIST_DECL,          // persist i32 count = 0;
    NODE_RETURN,                // return expr;
    NODE_IF,                    // if ( cond ) { } else { }
    NODE_WHILE,                 // while ( cond ) { }
    NODE_FOR,                   // for (init; cond; step) { }
    NODE_SWITCH,                // switch (expr) { case ... }
    NODE_BREAK,                 // break;
    NODE_CONTINUE,              // continue;
    NODE_EXPR_STMT,             // expr;

    // _Expressions_
    NODE_BINARY,                // a + b, a == b, a & b ...
    NODE_UNARY,                 // -a, !a, ~a, *a
    NODE_ASSIGN,                // a = b
    NODE_CALL,                  // add(2, 3)
    NODE_GENERIC_CALL,          // push(i32)(&list, 42)
    NODE_MEMBER,                // obj.field - compiler auto-deref pointers
    NODE_INDEX,                 // arr[i]
    NODE_CAST,                  // i32(expr) - explicit cast
    NODE_TYPEOF,                // typeof(expr) - compile- time only, no IR emitted
    NODE_ALLOC,                 // alloc(i32, n)
    NODE_FREE,                  // free(ptr)

    // _Literals_
    NODE_LIT_INT,               // 23
    NODE_LIT_FLOAT,             // 23.6
    NODE_LIT_STRING,            // "hello" - arena backed fat pointer
    NODE_LIT_CHAR,              // 'a'
    NODE_LIT_BOOL,              // true / false
    NODE_LIT_NULL,              // nullptr

    // _Identifier_
    NODE_IDENT,                 // any named reference

} Node_Kind;

// _Type_Kind_

typedef enum Type_Kind {

    TYPE_NULL,                              // null 
    TYPE_BOOL,                              // bool             
    TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64,  // signed ints     
    TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64,  // unsigned ints
    TYPE_F32, TYPE_F64,                     // floats
    TYPE_STRING,                            // string builtin fat pointer ( i8* data, i32 len)   
    TYPE_CHAR,                              // i8 used a character
    TYPE_POINTER,                           // T* star_count = 1      
    TYPE_ARRAY_FIXED,                       // T[N] - stack allocated, size baked in    
    TYPE_ARRAY_DYNAMIC,                     // T[] - fat pointer T* data, s64 len, s64 cap
    TYPE_FN,                                // fn(i32, i32) -> i32
    TYPE_NAMED,                             // vec2, myalias resolved by typechecker
    TYPE_GENERIC,                           // List(T), Array(T)
    TYPE_TYPEOF,                            // typeof(expr) - replaced during typecheck pass
    
} Type_Kind;

// _Ast_Type_

struct Ast_Type {
    Type_Kind   kind;
    i32         line;

    union {
        // TYPE_POINTER
        struct {
            Ast_Type* base;         // what is being pointed to
            i32       star_count;   // number of stars - encodes array rank
                                    // star_count =1 -> T* (single pointer)
                                    // star_count =2 -> T** (1D array of pointers)
                                    // star_count =3 -> T*** (2D array)
        } ptr;

        // TYPE_ARRAY_FIXED: T[N]
        struct {
            Ast_Type* elem;
            i64       size; // compile-time constant N
        } array_fixed;

        // TYPE_ARRAY_DYNAMIC: T[]
        struct {
            Ast_Type* elem;   
        } array_dynamic;

        // TYPE_FN: fn(params) -> ret
        struct {
            Ast_Type** params;    // array of param types
            i32        param_count;
            Ast_Type*  ret;
        } fn;

        // TYPE_NAMED: vec3, myalias
        struct {
            String_View name;
        } named;

        // TYPE_GENERIC: List(T), Array(T)
        struct {
            String_View name;
            Ast_Type**  args; // type arguments
            i32         arg_count;
        } generic;

        // TYPE_TYPEOF: typeof(expr)
        struct {
            Ast_Node* expr; // expression to infer type from
        } typeof_expr;
    };
};

// _Ast_Param_

// function Parameter.
// Params are const by default - muatable = true when marked with !
// ex (i32! a, i32! b) -> (i32* a, i32* b) gets the expr's address

struct Ast_Param {
    String_View name;
    Ast_Type*   type_node;
    b8          is_mutable;      // false = const (default), true = marked with !
    i32         line; 
};

// _Ast_Field_

// struct or union field.
// Fields are public by default - private = true when marked with private.

struct Ast_Field {
    String_View name;
    Ast_Type*   type_node;
    b8          is_private; // private keyword
    i32         line;
};

// _Ast_Case_

// one arm of a switch statement.

struct Ast_Case {
    Ast_Node* value; // nullptr = default case
    Ast_Node* body;  // NODE_BLOCK
};

// _Binary_Op_
typedef enum Binary_Op {

    BINOP_ADD,
    BINOP_SUB,
    BINOP_MUL,
    BINOP_DIV,
    BINOP_MOD,

    BINOP_EQ,
    BINOP_NEQ,
   
    BINOP_LT,
    BINOP_GT,
    BINOP_LTE,
    BINOP_GTE,
   
    BINOP_AND,
    BINOP_OR,
   
    BINOP_BIT_AND,
    BINOP_BIT_OR,
    BINOP_BIT_XOR,
    BINOP_LSHIFT,
    BINOP_RSHIFT,

} Binary_Op;

// _Unary_Op_
typedef enum Unary_Op {

    UNOP_NEG,       // -a
    UNOP_NOT,       // !a
    UNOP_BIT_NOT,   // ~a
    UNOP_DEREF,     // *a explicit pointer derefrence

} Unary_Op;

// _Ast_Node_

// Tagged union one struct covers all node kinds
// all nodes are allocated from a singe arena - never freed individually

struct Ast_Node {
    Node_Kind kind;
    i32       line;

    union {

        // NODE_PROGRAM
        struct {
            Ast_Node** decls;
            i32        count; 
        } node_program;

        // NODE_MODULE
        struct {
            String_View name;   // module math;
        } node_module;

        // NODE_IMPORT
        struct {
            String_View name;   // import math;
        } node_import;

        // NODE_FN_DECL
        struct {
            String_View name;
            Ast_Param*  params;
            i32         param_count;
            Ast_Type*   ret_type;       // null type if -> null
            Ast_Node*   body;           // NODE_BLOCK, nullptr for .cm declarations
            b8          is_internal;    // internal keyword
            b8          is_inline;      // inline keyword 
            b8          is_export;      // export keyword (for .cm files)
            // generics: fn push(T)(...) - type_params holds ["T"]
            String_View* type_params;
            i32          type_param_count;
        } node_fn_decl;

        // NODE_TYPE_ALIAS
        struct {
            String_View name;
            Ast_Type*   alias_typs;
            b8          is_internal;
        } node_type_alias;

        // NODE_STRUCT_DECL / NODE_UNION_DECL
        struct {
            Ast_Field*  fields;
            i32         field_count;
        } node_struct_decl, node_union_decl;

        // NODE_BLOCK
        struct {
            Ast_Node** stmts;
            i32        count;
        } node_block;

        // NODE_VAR_DECL
        // i32 x = expr; / i32 x = noinit; / string& x = other;
        struct {
            String_View name;
            Ast_Type*   type_node;      // nullptr = infer from init expr
            Ast_Node*   init;           // nullptr = noinit
            b8          is_noinit;
            b8          is_share;       // string& share semantics, no copy
            b8          is_const;       // const keyword
        } node_var_decl;

        // NODE_PERSIST_DECL persist i32 count = 0;
        struct {
            String_View name;
            Ast_Type*   type_node;
            Ast_Node*   init;
        } node_persist_decl;

        // NODE_RETURN
        struct {
            Ast_Node* value; // nullptr for bare return;
        } node_return;

        // NODE_IF
        struct {
            Ast_Node* cond;
            Ast_Node* then_block; // NODE_BLOCK
            Ast_Node* else_block; // NODE_BLOCK or NODE_IF (else if), nullable
        } node_if;

        // NODE_WHILE
        struct {
            Ast_Node* cond;
            Ast_Node* body;
        } node_while;

        // NODE_FOR
        struct {
            Ast_Node* init; // NODE_VAR_DECL or NODE_EXPR_STMT, nullable
            Ast_Node* cond; // nullable = infinite loop
            Ast_Node* step; // NODE_EXPR_STMT, nullable
            Ast_Node* body; 
        } node_for;

        // NODE_SWITCH
        struct {
            Ast_Node* expr;
            Ast_Case* cases;
            i32       case_count;
        } node_switch;

        // NODE_EXPR_STMT
        struct {
            Ast_Node* expr;
        } node_expr_stmt;

        // NODE_BINARY
        struct {
            Binary_Op op;
            Ast_Node* left;
            Ast_Node* right;
        } node_binary;

        // NODE_UNARY
        struct {
            Unary_Op  op;
            Ast_Node* operand;
        } node_unary;

        // NODE_ASSIGN
        struct {
            Ast_Node* target;       // lvalue 
            Ast_Node* value;
        } node_assign;

        // NODE_CALL
        struct {
            Ast_Node*   callee;   // NODE_IDENT or NODE_MEMBER
            Ast_Node**  args;
            i32         arg_count;
        } node_call;

        // NODE_GENERIC_CALL 
        struct {
            Ast_Node*   callee;
            Ast_Type**  type_args;
            i32         type_arg_count;
            Ast_Node**  args;
            i32         arg_count;
        } node_generic_call;

        // NODE_MEMBER - obj.field (. always, compiler handles deref)
        struct {
            Ast_Node*   object;
            String_View field;
        } node_member;

        // NODE_INDEX - arr[i]
        struct {
            Ast_Node* object;
            Ast_Node* index;
        } node_index;

        // NODE_CAST - i32(expr)
        struct {
            Ast_Type* target_type;
            Ast_Node* expr;
        } node_cast;

        // NODE_TYPEOF 
        struct {
            Ast_Node* expr;
        } node_typeof;

        // NODE_ALLOC - alloc(i32, n)
        struct {
            Ast_Type* type_node;
            Ast_Node* count;        // nullptr = alloc single
        } node_alloc;

        // NODE_FREE - free(ptr)
        struct {
            Ast_Node* ptr;
        } node_free;

        // NODE_LIT_INT
        struct {
            i64 value;
        } node_lit_int;

        // NODE_LIT_FLOAT
        struct {
            f64 value;
        } node_lit_float;

        // NODE_LIT_STRING - text is a stringview into sourcebuffer
        // arena copy happens in codegen / stage 4
        struct {
            String_View text;
        } node_lit_string;

        // NODE_LIT_CHAR
        struct {
            i8 value;
        } node_lit_char;

        // NODE_LIT_BOOL
        struct {
            b8 value;
        } node_lit_bool;

        // NODE_IDENT
        struct {
            String_View name;
        } node_ident;

    };
};

// _Arena_ for AST nodes

// all AST nodes and types are bump allocated rom this arena 
// nothing is freed idividually the whole arena is freed after codegen

typedef struct Ast_Arena {
    i8* base;
    i64 cap;
    i64 used;
} Ast_Arena;

// _API_

null      Ast_Arena_Init  (Ast_Arena* arena, i64 capacity);
null      Ast_Arena_Free  (Ast_Arena* arena);
Ast_Node* Ast_Arena_Node  (Ast_Arena* arena, Node_Kind kind, i32 line);
Ast_Type* Ast_Arena_Type  (Ast_Arena* arena, Type_Kind kind, i32 line);
null**    Ast_Arena_Array (Ast_Arena* arena, i64 elem_size, i32 count);

// _Debug_

null      Ast_Print       (const Ast_Node* node, i32 indent);
const i8* Node_Kind_Name  (Node_Kind kind);

