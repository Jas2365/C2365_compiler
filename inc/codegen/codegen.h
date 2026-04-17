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
 * codegen.h
 * LLVM IR text emitter.
 *
 * Walks the AST and writes a .ll file that clang can compile.
 * No LLVM C API — just fprintf to a file.
 *
 * Stage 1 covers:
 *   - fn declarations and definitions
 *   - return statements
 *   - integer/float literals
 *   - binary arithmetic and comparison
 *   - local variables (alloca + load + store)
 *   - function calls
 *   - const and mutable params (! → pointer pass in IR)
 *   - read-only borrow (& → const ptr in IR)
 *
 * LLVM IR is SSA — every value has a unique register number %0, %1 ...
 * We use a monotonic counter (reg) per function for this.
 *
 * Target: x86_64-w64-mingw32 (Windows + mingw clang)
 */
#pragma once

#include <ast/ast.h>
#include <symtable/symtable.h>

#include <stdio.h> // FILE

// _Codegen_State_
typedef struct Codegen {
    FILE* out;
    SymTable* st;
    i32 reg;
    i32 label;
    i32 errors;
} Codegen;

// _API_

null Codegen_Init(Codegen* cg, FILE* out, SymTable* st);
null Codegen_Emit(Codegen* cg, const Ast_Node* program);