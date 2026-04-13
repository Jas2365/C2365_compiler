
#include <stdio.h>
#include <stdlib.h>

#include <lexer/lexer.h>
#include <ast/ast.h>

// read entier file

#define AST_ARENA_SIZE (1024 * 64) 

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
        fprintf(stderr, "usage: compiler <source.ci>" endl);
        return 1;
    }

    i8* source  = read_file(argv[1]);
    if(!source) return 1;
    

    printf("==============================" endl);
    printf("source:" endl "%s" endl, source);
    printf("==============================" endl);

    // _Lexer_
    Lexer lexer;
    Lexer_Init(&lexer, source);
    Token_Array tokens = Lexer_Tokenize(&lexer);
    
    printf("Lexer Init" endl);
    printf("Array init" endl);
    
    print_token(&tokens);
    

    // _AST_
    Ast_Arena arena;
    Ast_Arena_Init(&arena, AST_ARENA_SIZE);
    
    // the module
    // module math;
    // fn add(i32 a, i32! b) -> i32 { return a + b; }



    printf("Free" endl);
    TokenArray_Free(&tokens);
    free(source);
    printf("End" endl);
    return 0;
}