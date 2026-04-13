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

// function decl

static Ast_Node* parse_fn_decl(Parser* p, b8 is_internal, b8 is_inline, b8 is_export) {
    i32 line = cur(p)->line;
    expect(p, TOKEN_FN, "'fn'");

    const Token* name_tok = expect(p, TOKEN_IDENT, "function name");

    Ast_Node* node = Ast_Arena_Node(p->arena, NODE_FN_DECL, line);
    node->node_fn_decl.name = name_tok->text;
    node->node_fn_decl.is_internal  = is_internal;
    node->node_fn_decl.is_inline    = is_inline;
    node->node_fn_decl.is_export    = is_export;

    // generic type params: fn push(T)(...)
    if(at(p, TOKEN_LPAREN) && peek_at(p,1)->kind == TOKEN_IDENT &&
        peek_at(p,2)->kind == TOKEN_RPAREN) {

        advance(p);
        String_View* tp_buf = (String_View*)Ast_Arena_Array(
            p->arena, (i64)sizeof(String_View), 8
        );
        i32 tp_count = 0;
        while(!at(p, TOKEN_RPAREN) && !at(p, TOKEN_EOF)) {
            if(tp_count > 0) expect (p, TOKEN_COMMA, "','");
            tp_buf[tp_count++] = cur(p)->text;
            advance(p);
        }
        expect(p, TOKEN_RPAREN, "')'");
        node->node_fn_decl.type_params = tp_buf;
        node->node_fn_decl.type_param_count = tp_count;
    }

    // params
    expect(p, TOKEN_LPAREN, "'('");
    Ast_Param param_buf[64];
    i32 param_count = 0;
    while(!at(p, TOKEN_RPAREN) && !at(p, TOKEN_EOF)) {
        if(param_count > 0) expect(p, TOKEN_COMMA, "','");
        param_buf[param_count++] = parse_param(p);
    }
    expect(p, TOKEN_RPAREN, "')'");

    if(param_count > 0) {
        node->node_fn_decl.params = (Ast_Param*)Ast_Arena_Array(
            p->arena, (i64)sizeof(Ast_Param), param_count
        );
        for(i32 i = 0; i< param_count; i++)
            node->node_fn_decl.params[i] = param_buf[i];
    }
    node->node_fn_decl.param_count = param_count;

    // return type: -> T
    expect(p, TOKEN_ARROW, "'->'");
    node->node_fn_decl.ret_type = parse_type(p);

    // .cm mode: declaration only - body is a hard error
    if(p->mode == PARSE_MODE_CM) {
        expect(p, TOKEN_SEMICOLON, "';' (interface file - no function body allowed)");
        node->node_fn_decl.body = nullptr;
        return node;
    }

    // .ci mode: body required unless followed by ;
    if(at(p, TOKEN_SEMICOLON)) {
        advance(p);
        node->node_fn_decl.body = nullptr; // forward decl in .ci
    } else {
        node->node_fn_decl.body = parse_block(p);
    }
    return node;
}

// type alias
// type vec2 = struct { f32 x, y};

static Ast_Node* parse_struct_or_union( Parser* p) {
    i32 line = cur(p)->line;
    Node_Kind aggregate = at(p,TOKEN_STRUCT) ? NODE_STRUCT_DECL : NODE_UNION_DECL;
    advance(p);
    expect(p, TOKEN_LBRACE, "'{'");

    Ast_Field field_buf[128];
    i32 field_count = 0;

    while(!at(p, TOKEN_RBRACE) && !at(p, TOKEN_EOF)) {
        b8 is_private = match(p, TOKEN_PRIVATE);
        Ast_Type* ft = parse_type(p);
        const Token* fname = expect(p, TOKEN_IDENT, "field name");
        expect(p, TOKEN_SEMICOLON, "';'");
        field_buf[field_count++] = (Ast_Field) {
            .name = fname->text,
            .type_node = ft,
            .is_private = is_private,
            .line = fname->line,
        };
    }
    expect(p, TOKEN_RBRACE, "'}'");

    Ast_Node* node = Ast_Arena_Node(p->arena, aggregate, line);
    if(field_count > 0) {
        node->node_struct_decl.fields = (Ast_Field*)Ast_Arena_Array(
            p->arena, (i64)sizeof(Ast_Field), field_count
        );
        for(i32 i = 0; i<field_count; i++)
            node->node_struct_decl.fields[i] = field_buf[i];
    }

    node->node_struct_decl.field_count = field_count;
    return node;
}

static Ast_Node* parse_type_alias(Parser* p, b8 is_internal) {
    i32 line = cur(p)->line;
    expect(p, TOKEN_TYPE, "'type'");
    const Token* name_tok = expect(p, TOKEN_IDENT, "type name");
    expect(p, TOKEN_ASSIGN, "'='");

    Ast_Node* node = Ast_Arena_Node(p->arena, NODE_TYPE_ALIAS, line);
    node->node_type_alias.name = name_tok->text;
    node->node_type_alias.is_internal = is_internal;

    if(at(p, TOKEN_STRUCT) || at(p, TOKEN_UNION)) {
        Ast_Node* agg = parse_struct_or_union(p);
        Ast_Type* alias_type = Ast_Arena_Type(p->arena, TYPE_NAMED, line);
        alias_type->named.name = name_tok->text;

        node->node_type_alias.alias_typs = alias_type;
        (void)agg;

        /**  // type Vec3 = struct { ... };
        // wrap struct/union node inside the alias_type as a named type
        // pointing to the inline definition
        AstNode *agg = parse_struct_or_union(p);
        // store the struct/union as a named type pointing to inline def
        AstType *alias_type = AstArena_Type(p->arena, TYPE_NAMED, line);
        alias_type->named.name = name_tok->text;
        // smuggle the aggregate node in — typechecker resolves it
        // by checking the type_alias node's alias_type.kind == TYPE_NAMED
        // and finding the struct body in the symbol table
        node->type_alias.alias_type = alias_type;
        // also store the aggregate as a child via a helper field
        // we reuse the fn_decl.body slot pattern — store in alias_type expr
        // Actually cleanest: store inline aggregate as a separate top-level node
        // The parser returns two nodes — caller handles this.
        // For now, alias_type points to the aggregate name and the aggregate
        // node is returned via a side channel through parse_type_alias_with_body.
        // Simpler: just set the alias_type to the struct node directly.
        // We add a TYPE_STRUCT_INLINE kind for this.
        (void)agg;  // TODO Stage 2 — typechecker handles inline struct
         */
    } else {
        node->node_type_alias.alias_typs = parse_type(p);
    }
    expect(p, TOKEN_SEMICOLON, "';'");
    return node;
}

// variable declaration
// i32 x = 5;
// i32 x = noinit;
// string& x = other; -- share semantics
// const i32 x = 4;

static Ast_Node* parse_var_decl(Parser* p, b8 is_const) {
    i32 line = cur(p)->line;
    Ast_Type* type_node = parse_type(p);

    // string& - share semantics, no copy
    b8 is_share = false;
    if(type_node->kind == TYPE_STRING && at(p, TOKEN_AMP)) {
        advance(p);
        is_share = true;
    }

    const Token* name_tok = expect(p, TOKEN_IDENT, "variable name");
    Ast_Node* node = Ast_Arena_Node(p->arena, NODE_VAR_DECL, line);
    node->node_var_decl.name = name_tok->text;
    node->node_var_decl.type_node = type_node;
    node->node_var_decl.is_share = is_share;
    node->node_var_decl.is_const = is_const;

    if(match(p, TOKEN_ASSIGN)) {
        if(at(p, TOKEN_NOINIT)) {
            advance(p);
            node->node_var_decl.is_noinit = true;
            node->node_var_decl.init = nullptr;
        } else {
            node->node_var_decl.init = parse_expr(p);
        }
    }

    expect(p, TOKEN_SEMICOLON, "';'");
    return node;
}

// Persist declaration
// persist i32 count = 0;

static Ast_Node* parse_persist_decl(Parser* p) {
    i32 line = cur(p)->line;
    expect(p, TOKEN_PERSIST, "'persist'");
    Ast_Type* type_node = parse_type(p);

    const Token* name_tok = expect(p, TOKEN_IDENT, "variable name");
    expect(p, TOKEN_ASSIGN, "'='");
    Ast_Node* init = parse_expr(p);
    expect(p, TOKEN_SEMICOLON, "';'");

    Ast_Node* node = Ast_Arena_Node(p->arena, NODE_PERSIST_DECL, line);
    node->node_persist_decl.name = name_tok->text;
    node->node_persist_decl.type_node = type_node;
    node->node_persist_decl.init = init;

    return node;
}

// Expressions (Pratt parser)

typedef enum Precedence {
    
    PREC_NONE,
    PREC_ASSIGN,    // =
    PREC_OR,        // ||
    PREC_AND,       // &&
    PREC_BIT_OR,    // !
    PREC_BIT_XOR,   // ^
    PREC_BIT_AND,   // &
    PREC_EQ,        // == !=
    PREC_CMP,       // < > <= >=
    PREC_SHIFT,     // << >>
    PREC_ADD,       // + -
    PREC_MUL,       // * / %
    PREC_UNARY,     // - ! ~ *
    PREC_POSTFIX,   // . [] ()

} Precedence;

static Precedence token_prec(Token_Type t) {
    switch(t) {
        case TOKEN_ASSIGN:      return PREC_ASSIGN;
        case TOKEN_OR:          return PREC_OR;
        case TOKEN_AND:         return PREC_AND;
        case TOKEN_PIPE:        return PREC_BIT_OR;
        case TOKEN_CARET:       return PREC_BIT_XOR;
        case TOKEN_AMP:         return PREC_BIT_AND;
        case TOKEN_EQ:
        case TOKEN_NEQ:         return PREC_EQ;
        case TOKEN_LT:
        case TOKEN_GT:
        case TOKEN_LTE:
        case TOKEN_GTE:         return PREC_CMP;
        case TOKEN_LSHIFT: 
        case TOKEN_RSHIFT:      return PREC_SHIFT;
        case TOKEN_PLUS:
        case TOKEN_MINUS:       return PREC_ADD;
        case TOKEN_STAR:
        case TOKEN_SLASH:
        case TOKEN_PERCENT:     return PREC_MUL;
        case TOKEN_DOT:
        case TOKEN_LBRACKET:
        case TOKEN_LPAREN:      return PREC_POSTFIX;
        default:                return PREC_NONE;
    }
}

static Binary_Op token_to_binop(Token_Type t) {
    switch(t) {
        case TOKEN_PLUS: return BINOP_ADD;
        case TOKEN_MINUS: return BINOP_SUB;
        case TOKEN_STAR: return BINOP_MUL;
        case TOKEN_SLASH: return BINOP_DIV;
        case TOKEN_PERCENT: return BINOP_MOD;
        case TOKEN_EQ: return BINOP_EQ;
        case TOKEN_NEQ: return BINOP_NEQ;
        case TOKEN_LT: return BINOP_LT;
        case TOKEN_GT: return BINOP_GT;
        case TOKEN_LTE: return BINOP_LTE;
        case TOKEN_GTE: return BINOP_GTE;
        case TOKEN_AND: return BINOP_AND;
        case TOKEN_OR:  return BINOP_OR;
        case TOKEN_AMP: return BINOP_BIT_AND;
        case TOKEN_PIPE: return BINOP_BIT_OR;
        case TOKEN_CARET: return BINOP_BIT_XOR;
        case TOKEN_LSHIFT: return BINOP_LSHIFT;
        case TOKEN_RSHIFT: return BINOP_RSHIFT;
        default:                    return BINOP_ADD; // unknown is ad, ad is unknown
    }
}

static Ast_Node* parse_expr_prec(Parser* p, Precedence min_prec);

static Ast_Node* parse_primary(Parser* p) {
    i32 line = cur(p)->line;

    // integer literal
    if(at(p, TOKEN_INT_LIT)) {
        const Token* t = advance(p);
        // copy to null-terminated buf for stroll           [should remove after secure arena is implemented]
        i8 buf[32];
        i32 len = t->text.len < 31 ? t->text.len : 31;
        for(i32 i = 0; i < len; i++) buf[i] = t->text.data[i];
        buf[len] = '\0';
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_LIT_INT, line);
        n->node_lit_int.value = (i64)stroll(buf, nullptr, 10);
        return n;
    }

    // float literal
    if(at(p, TOKEN_FLOAT_LIT)) {
        const Token* t = advance(p);
        i8 buf[64];
        i32 len = t->text.len < 63 ? t->text.len : 63;
        for(i32 i = 0; i<len; i++) buf[i] = t->text.data[i];
        buf[len] = '\0';
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_LIT_FLOAT, line);
        n->node_lit_float.value = strtod(buf, nullptr);
        return n;
    }

    // string literal
    if(at(p, TOKEN_STRING_LIT)) {
        const Token* t = advance(p);
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_LIT_STRING, line);
        // strip surround quotes from the view
        n->node_lit_string.text = sv_from_token(t->text.data + 1, t->text.len -2 );
        return n;
    }

    // char literal
    if(at(p, TOKEN_CHAR_LIT)) {
        const Token* t = advance(p);
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_LIT_CHAR, line);
        // text is 'c' or '\n' - extract the char value
        if(t->text.len >= 3 && t->text.data[1] == '\\') {
            switch(t->text.data[2]){
                case 'n' : n->node_lit_char.value = '\n'; break;
                case 't' : n->node_lit_char.value = '\t'; break;
                case 'r' : n->node_lit_char.value = '\r'; break;
                case '0' : n->node_lit_char.value = '\0'; break;
                case '\\' : n->node_lit_char.value = '\\'; break;
                case '\'' : n->node_lit_char.value = '\''; break;
                default: n->node_lit_char.value = t->text.data[2]; break;
            }
        } else {
            n->node_lit_char.value = t->text.data[1];
        }
        return n;
    }

    // bool literals
    if(at(p, TOKEN_TRUE)) {
        advance(p);
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_LIT_BOOL, line);
        n->node_lit_bool.value = true;
        return n;
    }
    if(at(p, TOKEN_FALSE)) {
        advance(p);
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_LIT_BOOL, line);
        n->node_lit_bool.value = false;
        return n;
    }

    // nullptr
    if(at(p, TOKEN_NULL)) {
        // keyword or lieral
        // token null covers both type and nullptr 
        // if * its a type, else its nullptr literal
        advance(p);
        if(!at(p, TOKEN_STAR)) {
            Ast_Node* n = Ast_Arena_Node(p->arena, NODE_LIT_NULL, line);
            return n;
        }
        // if * its  expr context (cast)
        // back up and get type parsing
        p->pos--;
    }

    // typeof(expr)
    if(at(p, TOKEN_TYPEOF)) {
        advance(p);
        expect(p, TOKEN_LPAREN, "'('");
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_TYPEOF, line);
        n->node_typeof.expr = parse_expr(p);
        expect(p, TOKEN_RPAREN, "')'");
        return n;
    }

    // alloc(type, count) or alloc(type)                // should make an array
    if(at(p, TOKEN_IDENT) && sv_eq_lit(cur(p)->text, "alloc", 5)) {
        advance(p);
        expect(p, TOKEN_LPAREN, "'('");
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_ALLOC, line);
        n->node_alloc.type_node = parse_type(p);
        n->node_alloc.count = nullptr;
        if(match(p, TOKEN_COMMA)) 
            n->node_alloc.count = parse_expr(p);
        expect(p, TOKEN_RPAREN, "')'");
        return n;
    }

    // free(ptr)
    if(at(p,TOKEN_IDENT) && sv_eq_lit(cur(p)->text, "free", 4)) {
        advance(p);
        expect(p, TOKEN_LPAREN, "'('");
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_FREE, line);
        n->node_free.ptr = parse_expr(p);
        expect(p, TOKEN_RPAREN, "')'");
        return n;
    }

    // type cast: i32(expr) - type keyword followd by ( 
    if(is_type_start(p) && peek_at(p,1)->kind == TOKEN_LPAREN
        && cur(p)->kind != TOKEN_IDENT  ) {

        Ast_Type* target = parse_type(p);
        expect(p, TOKEN_LPAREN, "'('");
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_CAST, line);
        n->node_cast.target_type = target;
        n->node_cast.expr = parse_expr(p);
        expect(p, TOKEN_RPAREN, "')'");
        return n;
    }

    // identifier - could be a plain ident, a call or a generic call
    if(at(p, TOKEN_IDENT)) {
        const Token* t = advance(p);
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_IDENT, line);
        n->node_ident.name = t->text;
        return n;
    }

    // grouped expresion expr
    if(at(p, TOKEN_LPAREN)) {
        advance(p);
        Ast_Node* n = parse_expr(p);
        expect(p, TOKEN_RPAREN, "')'");
        return n;
    }

    fprintf(stderr, "[line %d] error: unexpected token '%.*s' in expression\n",
        line, cur(p)->text.len, cur(p)->text.data
    );
    p->errors++;
    advance(p); // skip 
    Ast_Node* err = Ast_Arena_Node(p->arena, NODE_LIT_INT, line);

    return err;
}

// parse argumnet list for a call - returns arg count, fills buf
static i32 parse_args(Parser* p, Ast_Node** buf, i32 max) {
    i32 count = 0;
    while(!at(p, TOKEN_RPAREN) && !at(p, TOKEN_EOF) && count < max) {
        if(count > 0) expect(p, TOKEN_COMMA, "','");
        buf[count++] = parse_expr(p);
    }
    return count;
}

static Ast_Node* parse_expr_prec(Parser* p, Precedence min_prec) {
    i32 line = cur(p)->line;
    Ast_Node* left = parse_primary(p);

    for(;;) {
        Token_Type op_tok = cur(p)->kind;
        Precedence prec = token_prec(op_tok);
        if(prec <= min_prec) break;

        // assignment
        if(op_tok == TOKEN_ASSIGN) {
            advance(p);
            Ast_Node* n = Ast_Arena_Node(p->arena, NODE_ASSIGN, line);
            n->node_assign.target = left;
            n->node_assign.value = parse_expr_prec(p, PREC_ASSIGN -1); // why -1
            left = n;
            continue;
        }

        // member access: obj.field
        if(op_tok == TOKEN_DOT) {
            advance(p);
            const Token* field = expect(p, TOKEN_IDENT, "field name");
            Ast_Node* n = Ast_Arena_Node(p->arena, NODE_MEMBER, line);
            n->node_member.object = left;
            n->node_member.field = field->text;
            left = n;
            continue;
        }

        // index: ar[i]
        if(op_tok == TOKEN_LBRACKET) {
            advance(p);
            Ast_Node* n = Ast_Arena_Node(p->arena, NODE_INDEX, line);
            n->node_index.object = left;
            n->node_index.index = parse_expr(p);
            expect(p, TOKEN_RBRACKET, "']'");
            left = n;
            continue;
        }

        // call or generic call 
        // generic: push(i32)(args) - ident followed by (type)(
        // normal: add(args)        - ident followed by (expr
        if(op_tok == TOKEN_LPAREN) {
            advance(p);
            Ast_Node* arg_buf[64];
            i32 arg_count = parse_args(p, arg_buf, 64);
            expect(p, TOKEN_RPAREN, "')'");

            // check if followed by another ( - generic all
            if(at(p, TOKEN_LPAREN)) {
                advance(p);
                Ast_Node* n = Ast_Arena_Node(p->arena, NODE_GENERIC_CALL, line);
                n->node_generic_call.callee = left;
                // type args alredy parsed above 
                // stage 5 for proper generics
                
                n->node_generic_call.type_arg_count = 0;
                n->node_generic_call.type_args = nullptr;
                Ast_Node* arg_buf_2[64];
                i32 arg_count_2 = parse_args(p, arg_buf_2, 64);
                expect(p, TOKEN_RPAREN, "')'");
                n->node_generic_call.arg_count = arg_count_2;
                if(arg_count_2 > 0) {
                    n->node_generic_call.args = (Ast_Node**)Ast_Arena_Array(p->arena, (i64)sizeof(Ast_Node*), arg_count_2);
                    for(i32 i = 0; i < arg_count_2; i++)
                        n->node_generic_call.args[i] = arg_buf_2[i];
                }
                left = n;            
            } else {
                Ast_Node* n = Ast_Arena_Node(p->arena, NODE_CALL, line);
                n->node_call.callee  = left;
                n->node_call.arg_count = arg_count;
                if(arg_count > 0) {
                    n->node_call.args = (Ast_Node**)Ast_Arena_Array(
                        p->arena, (i64)sizeof(Ast_Node*), arg_count
                    );
                    for(i32 i = 0; i < arg_count; i++)
                        n->node_call.args[i] = arg_buf[i];
                }
                left  = n;
            }
            continue;
        }

        // binary operators
        advance(p);
        Ast_Node* right = parse_expr_prec(p, prec); // left associative
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_BINARY, line);
        n->node_binary.op = token_to_binop(op_tok);
        n->node_binary.left = left;
        n->node_binary.right = right;
        left = n;
    }

    return left;
}

static Ast_Node* parse_unary(Parser* p) {
    i32 line = cur(p)->line;

    if(at(p, TOKEN_MINUS)) {
        advance(p);
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_UNARY, line);
        n->node_unary.op = UNOP_NEG;
        n->node_unary.operand = parse_unary(p);
        return n;
    }
    if(at(p, TOKEN_BANG)) {
        advance(p);
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_UNARY, line);
        n->node_unary.op = UNOP_NOT;
        n->node_unary.operand = parse_unary(p);
        return n;
    }
    if(at(p, TOKEN_TILDE)) {
        advance(p);
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_UNARY, line);
        n->node_unary.op = UNOP_BIT_NOT;
        n->node_unary.operand = parse_unary(p);
        return n;
    }
    if(at(p, TOKEN_STAR)) {
        advance(p);
        Ast_Node* n = Ast_Arena_Node(p->arena, NODE_UNARY, line);
        n->node_unary.op = UNOP_DEREF;
        n->node_unary.operand = parse_unary(p);
        return n;
    }

    return parse_expr_prec(p, PREC_NONE);
}

static Ast_Node* parse_expr(Parser* p) {
    return parse_unary(p);
}

// statements

static Ast_Node* parse_block(Parser* p) {
    i32 line = cur(p)->line;
    expect(p, TOKEN_LBRACE, "'{'");

    Ast_Node* stmts_buf[512];
    i32 count = 0;

    while(!at(p, TOKEN_RBRACE) && !at(p, TOKEN_EOF))
        stmts_buf[count++] = parse_stmt(p);
    
    expect(p, TOKEN_RBRACE, "'}'");

    Ast_Node* node = Ast_Arena_Node(p->arena, NODE_BLOCK, line);
    if(count > 0) {
        node->node_block.stmts = (Ast_Node**)Ast_Arena_Array(
            p->arena, (i64)sizeof(Ast_Node*), count
        );
        for(i32 i = 0; i < count; i++) 
            node->node_block.stmts[i] = stmts_buf[i];
    }

    node->node_block.count = count;
    return node;
}

static Ast_Node* parse_if(Parser* p) {
    i32 line = cur(p)->line;
    expect(p, TOKEN_IF, "'if'");
    expect(p, TOKEN_LPAREN, "'('");
    Ast_Node* cond = parse_expr(p);
    expect(p, TOKEN_RPAREN, "')'");
    Ast_Node* then_block = parse_block(p);
    Ast_Node* else_block = nullptr;
    if(match(p, TOKEN_ELSE)) {
        if(at(p, TOKEN_IF))
            else_block = parse_if(p); // else if chain
        else
            else_block = parse_block(p);
    }
    Ast_Node* node = Ast_Arena_Node(p->arena, NODE_IF, line);
    node->node_if.cond = cond;
    node->node_if.then_block = then_block;
    node->node_if.else_block = else_block;
    return node;
    
}

static Ast_Node* parse_while(Parser* p) {
    i32 line = cur(p)->line;
    expect(p, TOKEN_WHILE, "'while'");
    expect(p, TOKEN_LPAREN, "'('");
    Ast_Node* cond = parse_expr(p);
    expect(p, TOKEN_RPAREN, "')'");
    
    Ast_Node* body = parse_block(p);
    Ast_Node* node = Ast_Arena_Node(p->arena, NODE_WHILE, line);
    node->node_while.cond = cond;
    node->node_while.body = body;
    return node;
}







