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
 * typechecker.h
 * Type ststem and semantic analysis.
 * 
 * Stage2 responsibilities:
 *  - Register all type definitions (struct, union, type aliases)
 *  - Resolve TYPE_NAMED to concrete types
 *  - Expand typeof() expressions at compile time
 *  - Compute struct/union field offsets
 *  - Type check all expressions and statements
 *  - Fill Ast_Node.resolved_type for codegen
 *  - Validate function calls, assignments, operators
 *  - Handle global variables
 * 
 * Three-pass algorithm:
 *  Pass 1: Register all types in symbol table
 *  Pass 2: Resolve all type references (TYPE_NAMED -> concrete)
 *  Pass 3: Type check expressions, fill resolved_type
 */

#pragma once

#include <ast/ast.h>
#include <symtable/symtable.h>

// _Type_Checker_State_

typedef struct TypeChecker {
    SymTable* st;
    Ast_Arena* arena;
    i32 errors;
} TypeChecker;

// _API_

null TypeChecker_Init(TypeChecker* tc, SymTable* st, Ast_Arena* arena);
null TypeChecker_Check(TypeChecker* tc, Ast_Node* program);

// _Helpers_

b8  Type_Equals      (const Ast_Type* a, const Ast_Type* b);
i32 Type_SizeOf      (const Ast_Type* t);   // size in bytes
i32 Type_AlignOf     (const Ast_Type* t);   // alignment in bytes
b8  Type_IsInt       (const Ast_Type* t);
b8  Type_IsFloat     (const Ast_Type* t);
b8  Type_IsPointer   (const Ast_Type* t);
b8  Type_IsAggregate (const Ast_Type* t); // struct / union