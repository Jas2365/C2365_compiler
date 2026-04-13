/**
 * parser.c
 * Recursive descent parser with pratt expression parsing
 * 
 * Grammar (informal)
 * 
 * program  → module_decl? import* top_decl*
 * top_decl → fn_decl | type_alias | var_decl
 * fn_decl  → modifier* 'fn' IDENT type_params? '(' params ')' '->' type body?
 * type_alias → modifier* 'type' IDENT '=' type ';'
 * var_decl → modifier* type IDENT ('&')? ('=' expr | '=' 'noinit')? ';'
 * persist_decl → 'persist' type IDENT '=' expr ';'
 * block → '{' stmt* '}'
 * stmt → var_decl | persist_decl | return | if | while | for | switch | break | continue | expr_stmt
 * expr → assignment
 * assignment → ident '=' expr | binary
 * binary → unary (op unary)* - pratt handles precedence
 * unary → ('-'|'!'|'~'|'*') unary | postfix
 * postfix → primary ('.' IDENT | '[' expr ']' | '(' args ')' )*
 * primary → INT | FLOAT | STRING | CHAR | 'true' | 'false' | 'nullptr' | IDENT 
 *           | '(' expr ')' | typeof '(' expr ')' | alloc '(' type '.' expr ')' | 
 *           free '('expr ')' | type_kw '(' expr ')' -- cast
 *           
 */

#include <parser/parser.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// _Internal_Helpers_

static inline const Token* cur(const Parser* p) {
    return &p->tokens[p->pos];
}

static inline const Token *peek_at(const Parser* p, i32 offset) {
    i32 idx = p->pos + offset;
    if(idx >= p->count) return &p->tokens[p->count -1]; // eof
    return &p->tokens[idx];
}

static inline b8 at(const Parser* p, Token_Type kind) {
    return cur(p)->kind == kind;
}


static inline b8 at2(const Parser* p, Token_Type a, Token_Type b) {
    return cur(p)->kind == a && peek_at(p, 1)-> kind == b;
}

static inline const Token* advance(Parser *p) {
    const Token* t = cur(p);
    if(t->kind != TOKEN_EOF) p->pos++;
    return t;
}

static const Token* expect(Parser *p, Token_Type kind, const i8* what) {
    if(cur(p)->kind != kind) {
        fprintf(stderr, "[line %d] error: expected %s, got '%.*s\n",
            cur(p)->line, what,
            cur(p)->text.len, cur(p)->text.data
        );
        p->errors++;
        return cur(p); // ret current - dont consume, keep parsing
    }
    return advance(p);
}

// consume if match, return boolean
static inline b8 match(Parser* p, Token_Type kind) {
    if(cur(p)->kind == kind) { advance(p); return true; }
    return false;
}

// check if current token is any primitive type
static b8 is_type_start(const Parser* p) {
    switch(cur(p)->kind) {
        case TOKEN_KW_I8:  case TOKEN_KW_I16: case TOKEN_KW_I32: case TOKEN_KW_I64:
        case TOKEN_KW_U8:  case TOKEN_KW_U16: case TOKEN_KW_U32: case TOKEN_KW_U64:
        case TOKEN_KW_F32: case TOKEN_KW_F64:
        case TOKEN_KW_STRING: case TOKEN_KW_BOOL:
        case TOKEN_NULL:
        case TOKEN_FN:     // fn(...) -> T  function pointer type
        case TOKEN_IDENT:  // named type: Vec3, MyAlias, List(T)
            return true;
        default:
            return false;
    }
}

// forawrd decl
static Ast_Node* parse_stmt (Parser* p);
static Ast_Node* parse_expr (Parser* p);
static Ast_Node* parse_block (Parser* p);
static Ast_Type* parse_type (Parser* p);
static Ast_Node* parse_fn_decl (Parser* p, b8 is_internal, b8 is_inline, b8 is_export);
static Ast_Node* parse_type_alias (Parser* p, b8 is_internal);
static Ast_Node* parse_var_decl (Parser* p, b8 is_const);
static Ast_Node* parse_persist_decl (Parser* p);

// type parsing
static Type_Kind token_to_primitive(Token_Type t) {
    switch(t) {
        case TOKEN_KW_I8:    return TYPE_I8;      
        case TOKEN_KW_I16:   return TYPE_I16;      
        case TOKEN_KW_I32:   return TYPE_I32;      
        case TOKEN_KW_I64:   return TYPE_I64;      
        case TOKEN_KW_U8:    return TYPE_U8;      
        case TOKEN_KW_U16:   return TYPE_U16;      
        case TOKEN_KW_U32:   return TYPE_U32;      
        case TOKEN_KW_U64:   return TYPE_U64;      
        case TOKEN_KW_F32:   return TYPE_F32;      
        case TOKEN_KW_F64:   return TYPE_F64;      
        case TOKEN_KW_STRING:return TYPE_STRING;      
        case TOKEN_KW_BOOL:  return TYPE_BOOL;      
        case TOKEN_NULL:     return TYPE_NULL;      
        default:             return TYPE_NAMED;
    }
}

static Ast_Type* parse_type(Parser* p) {
    i32 line = cur(p)->line;

    // fn(...) -> T 
    if(at(p, TOKEN_FN)) {
        advance(p);
        expect(p, TOKEN_LPAREN, "'('");
        Ast_Type* fn_type = Ast_Arena_Type(p->arena, TYPE_FN, line);

        // collect param types
        Ast_Type* param_buf[64];
        i32 param_count = 0;
        while(!at(p, TOKEN_RPAREN) && !at(p, TOKEN_EOF)) {
            if(param_count > 0) expect(p, TOKEN_COMMA, "','");
            param_buf[param_count++] = parse_type(p);
        }

        expect(p, TOKEN_RPAREN, "')'");
        expect(p, TOKEN_ARROW, "'->'");
        fn_type->fn.ret = parse_type(p);
        fn_type->fn.param_count = param_count;
        if(param_count > 0) {
            fn_type->fn.params = (Ast_Type**)Ast_Arena_Array(
                p->arena, (i64)sizeof(Ast_Type*), param_count
            );
            for(i32 i = 0; i < param_count; i++) 
                fn_type->fn.params[i] = param_buf[i];
        }
        return fn_type;
    }

    // typeof(expr)
    if(at(p, TOKEN_TYPEOF)) {
        advance(p);
        expect(p, TOKEN_LPAREN, "'('");
        Ast_Type* t = Ast_Arena_Type(p->arena, TYPE_TYPEOF, line);
        t->typeof_expr.expr = parse_expr(p);
        expect(p, TOKEN_RPAREN, "')'");
        return t;
    }

    // null* - null type used as pointer base
    if(at(p, TOKEN_NULL)) {
        advance(p);
        Ast_Type* base = Ast_Arena_Type(p->arena, TYPE_NULL, line);
        if(at(p, TOKEN_STAR)){
            i32 stars = 0;
            while(match(p, TOKEN_STAR)) stars++;
            Ast_Type *ptr = Ast_Arena_Type(p->arena, TYPE_POINTER, line);
            ptr->ptr.base = base;
            ptr->ptr.star_count = stars;
            return ptr;
        }
        return base;
    }

    // named type or generic
    if(at(p, TOKEN_IDENT)) {
        String_View name = cur(p)->text;
        advance(p);

        // generic: List(T)
        if(at(p, TOKEN_LPAREN)) {
            advance(p);
            Ast_Type* gt = Ast_Arena_Type(p->arena, TYPE_GENERIC, line);
            gt->generic.name = name;

            Ast_Type* arg_buf[16];
            i32 arg_count = 0;
            while(!at(p, TOKEN_RPAREN) && !at(p,TOKEN_EOF)) {
                if(arg_count >0) expect(p, TOKEN_COMMA, "','");
                arg_buf[arg_count++] = parse_type(p);
            }
            expect(p, TOKEN_RPAREN, "')'");
            gt->generic.arg_count = arg_count;
            if(arg_count > 0) {
                gt->generic.args = (Ast_Type**)Ast_Arena_Array(
                    p->arena, (i64)sizeof(Ast_Type*), arg_count
                );
                for(i32 i = 0; i< arg_count; i++) {
                    gt->generic.args[i] = arg_buf[i];
                }
            }
            return gt;
        }

        Ast_Type* nt = Ast_Arena_Type(p->arena, TYPE_NAMED, line);
        nt->named.name = name;

        // pointer suffix
        if(at(p, TOKEN_STAR)) {
            i32 stars = 0;
            while(match(p, TOKEN_STAR)) stars++;
            Ast_Type* ptr = Ast_Arena_Type(p->arena, TYPE_POINTER, line);
            ptr->ptr.base = nt;
            ptr->ptr.star_count = stars;

            // array suffix vec2*[n] vec2*[]
            if(at(p, TOKEN_LBRACKET)) {
                advance(p);
                if(at(p, TOKEN_RBRACKET)) {
                    advance(p);
                    Ast_Type* dyn = Ast_Arena_Type(p->arena, TYPE_ARRAY_DYNAMIC, line);
                    dyn->array_dynamic.elem = ptr;
                    return dyn;
                }
                Ast_Node* sz = parse_expr(p);
                expect(p, TOKEN_RBRACKET, "']'");
                Ast_Type* fix = Ast_Arena_Type(p->arena, TYPE_ARRAY_FIXED, line);
                fix->array_fixed.elem = ptr;
                fix->array_fixed.size = sz->node_lit_int.value;
                return fix;
            }
            return ptr;
        }

        // array suffix: vec3[n] or vec3[]
        if(at(p, TOKEN_LBRACKET)) {
            advance(p);
            if(at(p, TOKEN_RBRACKET)) {
                advance(p);
                Ast_Type* dyn = Ast_Arena_Type(p->arena, TYPE_ARRAY_DYNAMIC, line);
                dyn->array_dynamic.elem = nt;
                return dyn;
            }
            Ast_Node* sz = parse_expr(p);
            expect(p, TOKEN_RBRACKET, "']'");
            Ast_Type* fix = Ast_Arena_Type(p->arena, TYPE_ARRAY_FIXED, line);
            fix->array_fixed.elem = nt;
            fix->array_fixed.size = sz->node_lit_int.value;
            return fix;
        }
        return nt;
    }

    // primitive types: i32, f32, string, bool
    Type_Kind pk = token_to_primitive(cur(p)->kind);
    if(pk != TYPE_NAMED) {
        advance(p);
        Ast_Type* base = Ast_Arena_Type(p->arena, pk, line);

        // pointer suffix: i32*
        if(at(p, TOKEN_STAR)) {
            i32 stars = 0;
            while(match(p, TOKEN_STAR)) stars++;
            Ast_Type* ptr = Ast_Arena_Type(p->arena, TYPE_POINTER, line);
            ptr->ptr.base = base;
            ptr->ptr.star_count = stars;
            // array suffix: i32*[N] or i32*[]
            if(at(p, TOKEN_LBRACKET)) {
                advance(p);
                if(at(p, TOKEN_RBRACKET)) {
                    advance(p);
                    Ast_Type* dyn = Ast_Arena_Type(p->arena, TYPE_ARRAY_DYNAMIC, line);
                    dyn->array_dynamic.elem = ptr;
                    return dyn;
                }
                Ast_Node* sz = parse_expr(p);
                expect(p, TOKEN_RBRACKET, "']'");
                Ast_Type* fix = Ast_Arena_Type(p->arena, TYPE_ARRAY_FIXED, line);
                fix->array_fixed.elem = ptr;
                fix->array_fixed.size = sz->node_lit_int.value;
                return fix;
            }
            return ptr;
        }

        // array sufix i32[n] or i32[]
        if(at(p, TOKEN_LBRACKET)) {
            advance(p);
            if(at(p, TOKEN_RBRACKET)) {
                advance(p);
                Ast_Type* dyn = Ast_Arena_Type(p->arena, TYPE_ARRAY_DYNAMIC, line);
                dyn->array_dynamic.elem = base;
                return dyn;
            }
            Ast_Node *sz = parse_expr(p);
            expect(p, TOKEN_RBRACKET, "']'");
            Ast_Type* fix = Ast_Arena_Type(p->arena, TYPE_ARRAY_FIXED, line);
            fix->array_fixed.elem = base;
            fix->array_fixed.size = sz->node_lit_int.value;
            return fix;
        }
        return base;
    }

    fprintf(stderr, "[line %d] error:expected type, got '%.*s'\n",
        line, cur(p)->text.len, cur(p)->text.data
    );
    p->errors++;
    Ast_Type* err = Ast_Arena_Type(p->arena, TYPE_NULL, line);
    return err;
}

// Params
// params: type ident ('!')?
// readonly borrow: type& ident -> is_ref=true, no mutation

static Ast_Param parse_param(Parser* p) {
    i32 line = cur(p)->line;
    Ast_Type* type_node = parse_type(p);

    // read only borrow: vec2& v = const pointer, no mutation
    b8 is_ref = match(p, TOKEN_AMP);

    const Token* name_tok = expect(p, TOKEN_IDENT, "parameter name");

    // mutable i32! a compiler passes by pointer in IR
    b8 mutable = match(p, TOKEN_BANG);

    (null)is_ref;

    return (Ast_Param) {
        .name = name_tok->text,
        .type_node = type_node,
        .is_mutable = mutable,
        .line = line,
    };
}




