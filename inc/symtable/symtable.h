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
 * symtable.h
 * scoped module-aware symbol table.
 * 
 * Design:
 *  - Each scop is a flat hash map of symbols
 *  - Scopes are chained - lookup walks up to parent untill found
 *  - module scop is the outermost scope - holds top-level decls
 *  - Block scope is pushed/popped for every { } block
 *  - internal symbols are not expoted to .cmi.meta
 *  - private struct members are only accesible within the module
 *  - imported module symbols are loaded into a dedicated import scope
 * 
 * Symbol kinds:
 *  SYM_VAR - local variable or global
 *  SYM_FN  - function declaration ordefinition
 *  SYM_TYPE - type alias (type vec2 = ... )
 *  SYM_PARAM - function parameter
 *  SYM_MODULE - imported module name
 *  SYM_FIELD - struct/union field (stored in type's own scope)
 */

 #pragma once

 #include <ints.h>
 #include <strview.h>
 #include <ast/ast.h>

 // symbol kind
 typedef enum SymKind {
    SYM_VAR,
    SYM_FN,
    SYM_TYPE,
    SYM_PARAM,
    SYM_MODULE,
    SYM_FIELD,
 } SymKind;

// symbol 

typedef struct Symbol {
    String_View name;
    SymKind kind;
    Ast_Type* type;
    Ast_Node* decl;

    // flags
    b8  is_internal;
    b8  is_mutable;
    b8  is_const;
    b8  is_private;
    b8  is_persist;

    i32 line;
} Symbol;

#define SCOPE_INITIAL_CAP 32

typedef struct Scope {
    Symbol** entries; // hash map slots - nullptr = empty
    i32 cap;
    i32 count;
    struct Scope* parent; // for module scope
} Scope;

// symbol table

typedef struct SymTable {
    Scope* current;
    Scope* module;
    Ast_Arena* arena;
    i32 errors;
} SymTable;

// api

null SymTable_Init( SymTable* st, Ast_Arena* arena);

null SymTable_Push (SymTable* st); // enter new block
null SymTable_Pop  (SymTable* st); // exit block scope

Symbol* SymTable_Define (SymTable* st, String_View name, SymKind kind, Ast_Node* decl, i32 line);

// lookup
Symbol* SymTable_Lookup         (SymTable* st, String_View name); // full chain
Symbol* SymTable_LookupLocal    (SymTable* st, String_View name); // current scope only
Symbol* SymTable_LookupModule   (SymTable* st, String_View name); // module scope only

b8 SymTable_IsDefined(SymTable* st, String_View name);

// debug

null SymTable_Print(const SymTable* st);
const i8* SymKind_Name (SymKind kind);

