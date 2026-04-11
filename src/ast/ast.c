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

#include <ast/ast.h>

#include <stdlib.h> // malloc, free
#include <stdio.h>  // printf
#include <string.h> // memset

// _Arena_

#define AST_ALIGN (8)

static i64 align_up(i64 n) {
    return (n + AST_ALIGN -1) & ~(AST_ALIGN -1);
}

null Ast_Arena_Init(Ast_Arena* arena, i64 capacity) {
    arena->base = (i8*)malloc((size_t)capacity);
    arena->cap = capacity;
    arena->used = 0;
    memset(arena->base, 0, (size_t)capacity);
}

null Ast_Arena_Free(Ast_Arena* arena) {
    free(arena->base);
    arena->base = nullptr;
    arena->cap = 0;
    arena->used = 0;
}

static null* arena_alloc(Ast_Arena* arena, i64 size) {
    i64 aligned = align_up(size);
    if(arena->used + aligned > arena->cap){
        fprintf(stderr, "ast arena out of memory\n");
        return nullptr;
    }

    null* ptr = arena->base + arena->used;
    arena->used += aligned;
    return ptr;
}

// _Node_Constructors_

Ast_Node* Ast_Arena_Node(Ast_Arena* arena, Node_Kind kind, i32 line) {
    Ast_Node* node = (Ast_Node*)arena_alloc(arena, (i64)sizeof(Ast_Node));
    if(!node) return nullptr;
    node->kind = kind;
    node->line = line;
    return node;
}

Ast_Type* Ast_Arena_Type(Ast_Arena* arena, Type_Kind kind, i32 line) {
    Ast_Type* t = (Ast_Type*)arena_alloc(arena, (i64)sizeof(Ast_Type));
    if(!t) return nullptr;
    t->kind = kind;
    t->line = line;
    return t;
}

// allocates a flat arryaof pointer from the arena
null** Ast_Arena_Array(Ast_Arena* arena, i64 elem_size, i32 count) {
    if(count == 0) return nullptr;
    return (null**) arena_alloc(arena, elem_size * count);
}

// _Debug_Printer_

static null print_indent(i32 indent) {
    for(i32 i = 0; i< indent; i++) printf(" ");
}

const i8* Node_Kind_Name(Node_Kind kind) {
    switch (kind)
    {
    case NODE_PROGRAM:      return "PROGRAM";
    case NODE_MODULE:       return "MODULE";
    case NODE_IMPORT:       return "IMPORT";
    case NODE_FN_DECL:      return "FN_DECL";
    case NODE_TYPE_ALIAS:   return "TYPE_ALIAS";
    case NODE_STRUCT_DECL:  return "STRUCT_DECL";
    case NODE_UNION_DECL:   return "UNION_DECL";
    case NODE_BLOCK:        return "BLOCK";
    case NODE_VAR_DECL:     return "VAR_DECL";
    case NODE_PERSIST_DECL: return "PERSIST_DECL";
    case NODE_RETURN:       return "RETURN";
    case NODE_IF:           return "IF";
    case NODE_WHILE:        return "WHILE";
    case NODE_FOR:          return "FOR";
    case NODE_SWITCH:       return "SWITCH";
    case NODE_BREAK:        return "BREAK";
    case NODE_CONTINUE:     return "CONTINUE";
    case NODE_EXPR_STMT:    return "EXPR_STMT";
    case NODE_BINARY:       return "BINARY";
    case NODE_UNARY:        return "UNARY";
    case NODE_ASSIGN:       return "ASSIGN";
    case NODE_CALL:         return "CALL";
    case NODE_GENERIC_CALL: return "GENERIC_CALL";
    case NODE_MEMBER:       return "MEMBER";
    case NODE_INDEX:        return "INDEX";
    case NODE_CAST:         return "CAST";
    case NODE_TYPEOF:       return "TYPEOF";
    case NODE_ALLOC:        return "ALLOC";
    case NODE_FREE:         return "FREE";
    case NODE_LIT_INT:      return "LIT_INT";
    case NODE_LIT_FLOAT:    return "LIT_FLOAT";
    case NODE_LIT_STRING:   return "LIT_STRING";
    case NODE_LIT_CHAR:     return "LIT_CHAR";
    case NODE_LIT_BOOL:     return "LIT_BOOL";
    case NODE_LIT_NULL:     return "LIT_NULL";
    case NODE_IDENT:        return "IDENT";
    default:                return "UNKNOWN";
    }
}

static null print_type(const Ast_Type* t) {
    if(!t) { printf("(no type)"); return; }
    switch(t->kind) {
        case TYPE_NULL: printf("null"); break;
        case TYPE_BOOL: printf("bool"); break;
        case TYPE_I8: printf("i8"); break;
        case TYPE_I16: printf("i16"); break;
        case TYPE_I32: printf("i32"); break;
        case TYPE_I64: printf("i64"); break;
        case TYPE_U8: printf("u8"); break;
        case TYPE_U16: printf("u16"); break;
        case TYPE_U32: printf("u32"); break;
        case TYPE_U64: printf("u64"); break;
        case TYPE_F32: printf("f32"); break;
        case TYPE_F64: printf("f64"); break;
        case TYPE_STRING: printf("string"); break;
        case TYPE_CHAR: printf("char"); break;
        case TYPE_POINTER: 
            print_type(t->ptr.base); 
            for(i32 i = 0; i< t->ptr.star_count; i++) printf("*");
            break;
        case TYPE_ARRAY_FIXED: 
            print_type(t->array_fixed.elem); 
            printf("[%lld]", (i64)t->array_fixed.size);
            break;
        case TYPE_ARRAY_DYNAMIC: 
            print_type(t->array_dynamic.elem); 
            printf("[]");
            break;
        case TYPE_FN: 
            printf("fn(");
            for(i32 i = 0; i< t->fn.param_count; i++){
                if(i) printf(", ");
                print_type(t->fn.params[i]);
            }
            printf(") -> ");
            print_type(t->fn.ret);
            break;
        case TYPE_NAMED: 
            printf("%.*s", t->named.name.len, t->named.name.data);
            break;
        case TYPE_GENERIC:
            printf("%.*s(", t->generic.name.len, t->generic.name.data);
            for(i32 i = 0; i< t->generic.arg_count; i++){
                if(i) printf(", ");
                print_type(t->generic.args[i]);
            }
            printf(")");
            break;
        case TYPE_TYPEOF:
            printf("typeof(...)");
            break;
        
        default:
            printf("?type");
            break;
    }
}

null Ast_Print(const Ast_Node* node, i32 indent) {
    if(!node) return;
    print_indent(indent);

    switch(node->kind) {

        case NODE_PROGRAM:
            printf("[PROGRAM]\n");
            for(i32 i = 0; i< node->node_program.count; i++)
                Ast_Print(node->node_program.decls[i], indent+1);
            break;

        case NODE_MODULE:
            printf("[MODULE] %.*s\n", node->node_module.name.len, node->node_module.name.data);
            break;
        case NODE_IMPORT:
            printf("[IMPORT] %.*s\n", node->node_import.name.len, node->node_import.name.data);
            break;
        case NODE_FN_DECL:
            printf("[FN] %s%s%.*s(",
                node->node_fn_decl.is_internal ? "internal " : "",
                node->node_fn_decl.is_inline ? "inline " : "",
                node->node_fn_decl.name.len, node->node_fn_decl.name.data
            );
            for(i32 i = 0; i< node->node_fn_decl.param_count; i++) {
                Ast_Param *p = &node->node_fn_decl.params[i];
                if(i) printf(", ");
                print_type(p->type_node);
                printf("%s %.*s",
                    p->is_mutable ? "!" : "",
                    p->name.len, p->name.data
                );
            }
            printf(") -> ");
            print_type(node->node_fn_decl.ret_type);
            printf("\n");
            if(node->node_fn_decl.body)
                Ast_Print(node->node_fn_decl.body, indent + 1);
            break;

        case NODE_TYPE_ALIAS:
            printf("[TYPE] %.*s = ", 
                node->node_type_alias.name.len,
                node->node_type_alias.name.data
            );
            print_type(node->node_type_alias.alias_typs);
            printf("\n");
            break;
        
        case NODE_BLOCK:
            printf("[BLOCK]\n");
            for(i32 i = 0; i< node->node_block.count; i++) 
                Ast_Print(node->node_block.stmts[i], indent +1);
            break;

        case NODE_VAR_DECL:
            printf("[VAR] %.*s%s", 
                node->node_var_decl.name.len, node->node_var_decl.name.data,
                node->node_var_decl.is_share ? "&" : ""
            );
            if(node->node_var_decl.type_node) {
                printf(" : ");
                print_type(node->node_var_decl.type_node);
            }
            if(node->node_var_decl.is_noinit) printf(" = noinit");
                printf("\n");
            if(node->node_var_decl.init)
                Ast_Print(node->node_var_decl.init, indent + 1);
            break;

        case NODE_PERSIST_DECL:
            printf("[PERSIST] %.*s\n",
                node->node_persist_decl.name.len, node->node_persist_decl.name.data
            );

            if(node->node_persist_decl.init)
                Ast_Print(node->node_persist_decl.init, indent + 1);
            break;

        case NODE_RETURN:
            printf("[RETURN]\n");
            if(node->node_return.value)
                Ast_Print(node->node_return.value, indent +1);
            break;

        case NODE_IF:
            printf("[IF]\n");
            print_indent(indent + 1); printf("cond:\n");
            Ast_Print(node->node_if.cond, indent + 2);
            print_indent(indent + 1); printf("then:\n");
            Ast_Print(node->node_if.then_block, indent +2);
            if(node->node_if.else_block) {
                print_indent(indent + 1); printf("else:\n");
                Ast_Print(node->node_if.else_block, indent +2);
            }
            break;

        case NODE_WHILE:
            printf("[WHILE]\n");
            Ast_Print(node->node_while.cond, indent + 1);
            Ast_Print(node->node_while.body, indent + 1);
            break;
        
        case NODE_FOR:
            printf("[FOR]\n");
            if(node->node_for.init) Ast_Print(node->node_for.init, indent +1);
            if(node->node_for.cond) Ast_Print(node->node_for.cond, indent +1);
            if(node->node_for.step) Ast_Print(node->node_for.step, indent +1);
            Ast_Print(node->node_for.body, indent +1);
            break;

        case NODE_SWITCH:
            printf("[SWITCH]\n");
            Ast_Print(node->node_switch.expr, indent +1);
            for(i32 i = 0; i< node->node_switch.case_count; i++) {
                Ast_Case* c = &node->node_switch.cases[i];
                print_indent(indent +1);
                if(c->value) {
                    printf("case:\n"); Ast_Print(c->value, indent+2);
                } else {
                    printf("default:\n");
                }
                Ast_Print(c->body, indent+2);
            }
            break;
        
        case NODE_BREAK: printf("[BREAK]\n"); break;
        case NODE_CONTINUE: printf("[CONTINUE]\n"); break;
        
        case NODE_EXPR_STMT:
            printf("[EXPR_STMT]\n");
            Ast_Print(node->node_expr_stmt.expr, indent +1);
            break;
        
        case NODE_BINARY: {
            const i8* ops[] = {
                "+","-","*","/","%",
                "==","!=","<", ">", "<=", ">=",
                "&&", "||",
                "&","|", "^", "<<", ">>"
            };

            printf("[BINARY %s]\n", ops[node->node_binary.op]);
            Ast_Print(node->node_binary.left, indent + 1);
            Ast_Print(node->node_binary.right, indent + 1);
            break;
        }

        case NODE_UNARY: {
            const i8* ops[] = { "-", "!", "~", "*" };
            printf("[UNARY %s]\n", ops[node->node_unary.op]);
            Ast_Print(node->node_unary.operand, indent + 1);
            break;
        }

        case NODE_ASSIGN:
            printf("[ASSIGN]\n");
            Ast_Print(node->node_assign.target, indent + 1);
            Ast_Print(node->node_assign.value,  indent + 1);
            break;

        case NODE_CALL:
            printf("[CALL]\n");
            Ast_Print(node->node_call.callee, indent +1);
            for(i32 i = 0; i< node->node_call.arg_count; i++)
                Ast_Print(node->node_call.args[i], indent + 1);
            break;

        case NODE_MEMBER:
            printf("[MEMBER .%.*s]\n",
                node->node_member.field.len, node->node_member.field.data
            );
            Ast_Print(node->node_member.object, indent + 1);
            break;
        
        case NODE_INDEX:
            printf("[INDEX]\n");
            Ast_Print(node->node_index.object, indent + 1);
            Ast_Print(node->node_index.index, indent + 1);
            break;
        
        case NODE_CAST:
            printf("[CAST -> ");
            print_type(node->node_cast.target_type);
            printf("]\n");
            Ast_Print(node->node_cast.expr, indent + 1);
            break;
        
        case NODE_TYPEOF:
            printf("[TYPEOF]\n");
            Ast_Print(node->node_typeof.expr, indent +1);
            break;

        case NODE_ALLOC:
            printf("[ALLOC ");
            print_type(node->node_alloc.type_node);
            printf("]\n");
            if(node->node_alloc.count)
                Ast_Print(node->node_alloc.count, indent +1);
            break;

        case NODE_FREE:
            printf("[FREE]\n");
            Ast_Print(node->node_free.ptr, indent + 1);
            break;
        
        case NODE_LIT_INT:
            printf("[INT %lld]\n", (i64)node->node_lit_int.value);
            break;
        
        case NODE_LIT_FLOAT:
            printf("[FLOAT %lf]\n", node->node_lit_float.value);
            break;
        
        case NODE_LIT_STRING:
            printf("[STRING \"%.*s\"]\n", 
                node->node_lit_string.text.len, node->node_lit_string.text.data);
            break;
        
        case NODE_LIT_CHAR:
            printf("[CHAR '%c']\n", node->node_lit_char.value);
            break;
        
        case NODE_LIT_BOOL:
            printf("[BOOL %s]\n", node->node_lit_bool.value ? "true": "false");
            break;
        
        case NODE_LIT_NULL:
            printf("[NULL]\n");
            break;
        
        case NODE_IDENT:
            printf("[IDENT %.*s]\n", node->node_ident.name.len, node->node_ident.name.data);
            break;

        default:
            printf("[%s]\n", Node_Kind_Name(node->kind));
            break;
    }
}