/*
 * main.c
 * Stage 1 — full pipeline: lex -> parse -> symtable -> codegen (.ll)
 *
 * Usage:
 *   compiler <source.ci>              -- compile .ci, emit source.ll
 *   compiler --cm <source.cm>         -- compile .cm interface
 *   compiler --emit-ast <source.ci>   -- print AST only, no codegen
 */
 
#include <stdio.h>
#include <stdlib.h>

#include <symtable/symtable.h>
#include <lexer/lexer.h>
#include <parser/parser.h>
#include <ast/ast.h>
#include <codegen/codegen.h>
#include <typechecker/typechecker.h>

// read entier file

#define AST_ARENA_SIZE (1024 * 256)  // 256kb

static i8* read_file(const i8* path) {
    FILE* f = fopen(path, "rb");
    if(!f) {
        fprintf(stderr, "error cannot open path %s" endl, path);
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    i64 size = ftell(f);
    fseek(f, 0, SEEK_SET);

    i8* buffer = malloc((size_t)size +1);
    if(!buffer) {
        fprintf(stderr, "error outof mme"endl);
        fclose(f);
        return nullptr;
    }

    fread(buffer, 1, (size_t)size, f);
    buffer[size] = '\0';
    fclose(f);

    return buffer;
}

// build output path: replace extension with .ll
static null make_ll_path(const i8* src, i8* out, i32 out_cap) {
    i32 len = (i32)strlen(src);
    i32 dot = len;
    for(i32 i = len -1; i>=0; i--) {
        if(src[i] == '.') {dot = i; break; }
    }
    i32 copy = dot < out_cap -4 ? dot:out_cap -4;
    memcpy(out, src, (size_t)copy);
    memcpy(out + copy, ".ll", 4);
}

static null register_top_level(SymTable* st, const Ast_Node* program) {
    for(i32 i = 0; i< program->node_program.count; i++) {
        const Ast_Node* decl = program->node_program.decls[i];
        if(!decl) continue;

        switch (decl->kind) {

        case NODE_FN_DECL: {
            Symbol* sym = SymTable_Define(st,
            decl->node_fn_decl.name, SYM_FN,
            (Ast_Node*)decl, decl->line);
            if(sym) {
            sym->is_internal = decl->node_fn_decl.is_internal;
            }
            break;
        }
        
        case NODE_TYPE_ALIAS: {
            Symbol* sym = SymTable_Define(st,
            decl->node_type_alias.name, SYM_TYPE, 
            (Ast_Node*)decl, decl->line);
            if(sym) {
                sym->is_internal = decl->node_type_alias.is_internal;
            }
            break;
        }

        case NODE_VAR_DECL: {
            Symbol* sym = SymTable_Define(st,
            decl->node_var_decl.name, SYM_VAR,
            (Ast_Node*)decl, decl->line);
            if(sym) {
                sym->is_const = decl->node_var_decl.is_const;
            }
            break;
        }

        default:break;
        }
    }
}

static null print_token(const Token_Array* arr) {
    printf("%-5s %-14s %s" endl, "LINE", "TYPE", "TEXT");
    printf("----- -------------- ---------------------"endl);

    for(i32 i = 0; i< arr->count; i++) {
        const Token* t = &arr->tokens[i];
        printf("%4d   %-14s   '%.*s'"endl,
            t->line,
            Token_TypeName(t->kind),
            t->text.len,
            t->text.data
        );
    }
}


// _entry_

i32 main(i32 argc, i8** argv) {

    if(argc < 2) {
        fprintf(stderr, "usage: compiler [--cm] [--emit-ast] <source.ci>" endl);
        return 1;
    }

    // parse flags
    Parse_Mode mode     = PARSE_MODE_CI;
    b8         emit_ast = false;
    const i8*  path     = nullptr;

    for(i32 i = 1; i<argc; i++){
        if(strcmp(argv[i], "--cm") == 0) mode = PARSE_MODE_CM;
        else if(strcmp(argv[i], "--emit-ast") == 0) emit_ast = true;
        else path = argv[i];
    }

    if(!path) { fprintf(stderr, "error: no source file"endl); return 1; }

    // auto-detect .cm from extension
    i32 plen = (i32)strlen(path);
    if(plen >= 3 && strcmp(path + plen -3, ".cm") == 0)
        mode = PARSE_MODE_CM;

    // read source
    i8* source  = read_file(path);
    if(!source) return 1;
    
    // src
    printf("==============================" endl);
    printf("source:" endl "%s" endl, source);
    printf("==============================" endl);

    // _Lexer_
    Lexer lexer;
    Lexer_Init(&lexer, source);
    Token_Array tokens = Lexer_Tokenize(&lexer);
    
    printf("Lexer Init" endl);
    print_token(&tokens);
    
    // parse
    printf("\n=== Parser (%s mode) ===\n", mode == PARSE_MODE_CM ? "cm" : "ci");

    Ast_Arena arena;
    Ast_Arena_Init(&arena, AST_ARENA_SIZE);

    Parser parser;
    Parser_Init(&parser, &tokens, &arena, mode);
    Ast_Node* program = Parser_Run(&parser);

    // print ast
    if(!program) {
        fprintf(stderr, "parse failed - aborting"endl);
        Ast_Arena_Free(&arena);
        TokenArray_Free(&tokens);
        free(source);
        return 1;
    }

    if(emit_ast) {
        Ast_Print(program, 0);
        Ast_Arena_Free(&arena);
        TokenArray_Free(&tokens);
        free(source);
        return 0;
    }

    // symbol table
    printf(endl "=== Symbol Table ==="endl);
    SymTable st;
    SymTable_Init(&st, &arena);
    register_top_level(&st, program);
    // SymTable_Print(&st);

    if(st.errors > 0) {
        fprintf(stderr, "%d symbol error(s)"endl, st.errors);
        Ast_Arena_Free(&arena);
        TokenArray_Free(&tokens);
        free(source);
        return 1;
    }

    // type checker
    TypeChecker tc;
    TypeChecker_Init(&tc, &st, &arena);
    TypeChecker_Check(&tc, program);

    if(tc.errors > 0) {
        fprintf(stderr, "%d type error(s)"endl, tc.errors);
        Ast_Arena_Free(&arena);
        TokenArray_Free(&tokens);
        free(source);
        return 1;
    }

    // codegen
    i8 ll_path[512];
    make_ll_path(path, ll_path, 512);

    FILE* ll = fopen(ll_path, "w");
    if(!ll) {
        fprintf(stderr, "error: cannot write '%.s'"endl,ll_path);
        Ast_Arena_Free(&arena);
        TokenArray_Free(&tokens);
        free(source);
        return 1;
    }

    Codegen cg;
    Codegen_Init(&cg, ll, &st);
    Codegen_Emit(&cg, program);
    fclose(ll);

    if(cg.errors > 0) {
        fprintf(stderr, "%d codegen error(s)"endl, cg.errors);
    } else {
        printf("emitted: %s"endl, ll_path);
        printf("compile: clang %s -o program.exe"endl, ll_path);
    }

    //cleanup
    Ast_Arena_Free(&arena);
    TokenArray_Free(&tokens);
    free(source);
    return (parser.errors + st.errors + tc.errors + cg.errors) ? 1 : 0;
}