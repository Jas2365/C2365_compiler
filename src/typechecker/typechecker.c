/**
 * typechecker.c
 * Type system and semantic analysis - Stage 2
 * 
 * three passes:
 *  1. Register all type definitions
 *  2. Resolve all type references
 *  3. Type check expressions and fill resolved_type
 */

 #include <typechecker/typechecker.h>

 #include <stdio.h>
 #include <string.h>

 // _Type_Utility_Function_

b8 Type_Equals(const Ast_Type* a, const Ast_Type* b) {
   if(!a || !b) return a == b;
   if(a->kind != b->kind) return false;
   switch(a->kind) {
        case TYPE_NULL: case TYPE_BOOL:
        case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
        case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
        case TYPE_F32: case TYPE_F64:
        case TYPE_STRING: case TYPE_CHAR:
            return true;
       
        case TYPE_POINTER:
            return a->ptr.star_count == b->ptr.star_count && Type_Equals(a->ptr.base, b->ptr.base);
       
        case TYPE_ARRAY_FIXED:
            return a->array_fixed.size == b->array_fixed.size && Type_Equals(a->array_fixed.elem, b->array_fixed.elem);
       
        case TYPE_ARRAY_DYNAMIC:
            return Type_Equals(a->array_dynamic.elem, b->array_dynamic.elem);
      
        case TYPE_NAMED:
            return sv_eq(a->named.name, b->named.name);
       
        default:
            return false;
    }
}

b8 Type_IsInt(const Ast_Type* t) {
    if(!t) return false;
    return t->kind >= TYPE_I8 && t->kind <= TYPE_U64;
}

b8 Type_IsFloat(const Ast_Type* t) {
    if(!t) return false;
    return t->kind == TYPE_F32 || t->kind == TYPE_F64;
}

b8 Type_IsPointer(const Ast_Type* t) {
    if(!t) return false;
    return t->kind == TYPE_POINTER;
}

b8 Type_IsAggregate(const Ast_Type* t) {
    if(!t) return false;
    return t->kind == TYPE_NAMED; // actual check after resolution
}

i32 Type_SizeOf(const Ast_Type* t) {
    if(!t) return 0;
    switch(t->kind) {
        case TYPE_NULL: return 0;
        case TYPE_BOOL: return 1;
        case TYPE_I8:  case TYPE_U8: case TYPE_CHAR: return 1;
        case TYPE_I16: case TYPE_U16:                return 2;
        case TYPE_I32: case TYPE_U32: case TYPE_F32: return 4;
        case TYPE_I64: case TYPE_U64: case TYPE_F64: return 3;
        case TYPE_STRING: return 12; // { ptr, i32 } = 8 + 4
        case TYPE_POINTER: return 8;
        case TYPE_ARRAY_FIXED:
            return (i32)t->array_fixed.size * Type_SizeOf(t->array_fixed.elem);
        case TYPE_ARRAY_DYNAMIC:
            return 24; // { ptr, i64, i64} = 8 + 8 + 8
        default: return 4; // i32;
    }
}

i32 Type_AlignOf(const Ast_Type* t) {
    if(!t) return 1;
    switch(t->kind) {
        case TYPE_NULL: return 1;
        case TYPE_BOOL: case TYPE_I8: case TYPE_U8: case TYPE_CHAR: return 1; 
        case TYPE_I16: case TYPE_U16:                               return 2; 
        case TYPE_I32: case TYPE_U32: case TYPE_F32:                return 4; 
        case TYPE_I64: case TYPE_U64: case TYPE_F64:                return 8; 
        case TYPE_STRING: case TYPE_POINTER: case TYPE_ARRAY_DYNAMIC:return 8;
        case TYPE_ARRAY_FIXED:
            return Type_AlignOf(t->array_fixed.elem);
        default: return 4; 
    }
}

// _Forward_Decl_

static null pass1_register_types(TypeChecker* tc, Ast_Node* program);
static null pass2_resolve_types(TypeChecker* tc, Ast_Node* program);
static null pass3_typecheck(TypeChecker* tc, Ast_Node* program);

static Ast_Type* resolve_type(TypeChecker* tc, Ast_Type* t);
static Ast_Type* check_expr(TypeChecker* tc, Ast_Node* node);
static null check_stmt(TypeChecker* tc, Ast_Node* node);

// Pass 1: Register all type definitions

static null register_struct_or_union(TypeChecker* tc, const Ast_Node* node, String_View name) {
    Symbol* sym = SymTable_Define(tc->st, name, SYM_TYPE, (Ast_Node*)node, node->line);
    if(!sym) {
        tc->errors++;
        return;
    }

    // create a type_named that points to this struct/ union definition
    Ast_Type* t = Ast_Arena_Type(tc->arena, TYPE_NAMED, node->line);
    t->named.name = name;
    sym->type = t;

    // compute field offsets (simple sequential layout, no padding for now)
    i32 offset = 0;
    for(i32 i = 0; i< node->node_struct_decl.field_count; i++){
        Ast_Field* f = &node->node_struct_decl.fields[i];
        // field offset stored in a map- fornow we'll compute on deman in codegen
        i32 align = Type_AlignOf(f->type_node);
        offset = (offset + align -1) & ~(align -1); // alighn up
        // todo: store offset somewhere - for now codegen will recompute
        offset += Type_SizeOf(f->type_node);
    }
}

static null pass1_register_types(TypeChecker* tc, Ast_Node* program) {
    if(!program || program->kind != NODE_PROGRAM) return;

    for(i32 i = 0; i < program->node_program.count; i++) {
        Ast_Node* decl = program->node_program.decls[i];
        if(!decl) continue;

        switch(decl->kind) {

            // type vec2 = struct { ... };
            case NODE_TYPE_ALIAS : {
                // if alias points to inline struct/union, register the struct first
                if(decl->node_type_alias.alias_typs &&
                   decl->node_type_alias.alias_typs->kind == TYPE_NAMED) {
                    // alias to existing type resolve in pass 2
                }
                // register the alias itself
                Symbol* sym = SymTable_Lookup(tc->st, decl->node_type_alias.name);
                if(!sym) {
                    sym = SymTable_Define(tc->st, decl->node_type_alias.name, SYM_TYPE, decl, decl->line);
                }
                if(sym) {
                    sym->type = decl->node_type_alias.alias_typs;
                    sym->is_internal = decl->node_type_alias.is_internal;
                }
                break;

            }

            // standalone struct/union (rare - usually wrapped in type alias
            case NODE_STRUCT_DECL:
            case NODE_UNION_DECL: {
                // anonymous strct - give it a generated name
                i8 name[64];
                snprintf(name, 64, "annon_struct_%d", decl->line);
                register_struct_or_union(tc, decl, sv_from_token(name, (i32)strlen(name)));
                break;
            }

            // fn declarations register their symbols but types resolve in pass 2
            case NODE_FN_DECL: {
                Symbol* sym = SymTable_Lookup(tc->st, decl->node_fn_decl.name);
                if(sym) {
                    // alredy registerd by main.c register_symbols
                    // just ensure is_internal flag is set
                    sym->is_internal = decl->node_fn_decl.is_internal;
                }
                break;
            }

            default: break;

        }
    }
}

// Pass 2: Resolve all type references

static Ast_Type* resolve_type(TypeChecker* tc, Ast_Type* t) {
    if(!t) return nullptr;

    switch(t->kind) {
        // primitive - already resolved
        case TYPE_NULL: case TYPE_BOOL:
        case TYPE_I8:
        case TYPE_I16:
        case TYPE_I32:
        case TYPE_I64:
        case TYPE_U8:
        case TYPE_U16:
        case TYPE_U32:
        case TYPE_U64:
        case TYPE_F32:
        case TYPE_F64:
        case TYPE_STRING:
        case TYPE_CHAR:
            return t;

        // typenamed loop up in symbol table
        case  TYPE_NAMED: {
            Symbol* sym = SymTable_Lookup(tc->st, t->named.name);
            if(!sym || sym->kind != SYM_TYPE) {
                fprintf(stderr, "[line %d] error: undefined type '%.*s'\n",
                    t->line, t->named.name.len, t->named.name.data);
                tc->errors++;
                // fallback to i32
                Ast_Type* fallback = Ast_Arena_Type(tc->arena, TYPE_I32, t->line);
                return fallback;
            }

            // return the resolved type from symbol table
            return sym->type ? sym->type : t;
        }

        // type pointer - resolve base recursively
        case TYPE_POINTER: {
            t->ptr.base = resolve_type(tc, t->ptr.base);
            return t;
        }

        // type array fixed/dynamic - resolve element type
        case TYPE_ARRAY_FIXED: {
            t->array_fixed.elem = resolve_type(tc, t->array_fixed.elem);
            return t;
        }
        case TYPE_ARRAY_DYNAMIC: {
            t->array_dynamic.elem = resolve_type(tc, t->array_dynamic.elem);
            return t;
        }

        // type_fn - resolve param and return types
        case TYPE_FN: {
            for(i32 i = 0; i < t->fn.param_count; i++) {
                t->fn.params[i] = resolve_type(tc, t->fn.params[i]);
            }
            t->fn.ret = resolve_type(tc, t->fn.ret);
            return t;
        }

        // type typeof - resolv the expression's type, replace node
        case TYPE_TYPEOF: {
            Ast_Type* expr_type = check_expr(tc, t->typeof_expr.expr);
            return expr_type;
        }

        default:
            return t;
    }    
}

static null pass2_resolve_types(TypeChecker* tc, Ast_Node* program) {
    if(!program || program->kind != NODE_PROGRAM) return;

    for(i32 i = 0; i< program->node_program.count; i++) {
        Ast_Node* decl = program->node_program.decls[i];
        if(!decl) continue;

        switch(decl->kind) {
            case NODE_FN_DECL: {
                // resolve param types
                for(i32 j = 0; j < decl->node_fn_decl.param_count; j++) {
                    Ast_Param* p = &decl->node_fn_decl.params[j];
                    p->type_node = resolve_type(tc, p->type_node);
                }

                // resolve return type
                decl->node_fn_decl.ret_type = resolve_type(tc, decl->node_fn_decl.ret_type);
                break;
            }

            case NODE_TYPE_ALIAS: {
                decl->node_type_alias.alias_typs = resolve_type(tc, decl->node_type_alias.alias_typs);
                break;
            }

            case NODE_VAR_DECL: {
                decl->node_var_decl.type_node = resolve_type(tc, decl->node_var_decl.type_node);
                break;
            }

            case NODE_STRUCT_DECL:
            case NODE_UNION_DECL: {
                for(i32 j = 0; j < decl->node_struct_decl.field_count; j++) {
                    Ast_Field* f = &decl->node_struct_decl.fields[j];
                    f->type_node = resolve_type(tc, f->type_node);
                }
                break;
            }

            default: break;
        }
    }
}

// Pass 3: type check expressions

static Ast_Type* check_expr(TypeChecker* tc, Ast_Node* node) {
    if(!node) return nullptr;

    switch(node->kind) {
        
        case NODE_LIT_INT: {
            node->resolved_type = Ast_Arena_Type(tc->arena, TYPE_I32, node->line);
            return node->resolved_type;
        }
        
        case NODE_LIT_FLOAT: {
            node->resolved_type = Ast_Arena_Type(tc->arena, TYPE_F32, node->line);
            return node->resolved_type;
        }

        case NODE_LIT_BOOL: {
            node->resolved_type = Ast_Arena_Type(tc->arena, TYPE_BOOL, node->line);
            return node->resolved_type;
        }

        case NODE_LIT_NULL: {
            node->resolved_type = Ast_Arena_Type(tc->arena, TYPE_POINTER, node->line);
            node->resolved_type->ptr.base = Ast_Arena_Type(tc->arena, TYPE_NULL, node->line);
            node->resolved_type->ptr.star_count = 1;
            return node->resolved_type;
        }

        case NODE_LIT_STRING: {
            node->resolved_type = Ast_Arena_Type(tc->arena, TYPE_STRING, node->line);
            return node->resolved_type;
        }

        case NODE_LIT_CHAR: {
            node->resolved_type = Ast_Arena_Type(tc->arena, TYPE_I8, node->line);
            return node->resolved_type;
        }

        case NODE_IDENT: {
            Symbol* sym = SymTable_Lookup(tc->st, node->node_ident.name);
            if(!sym) {
                fprintf(stderr, "[line %d] error: undefined '%.*s'\n", node->line, node->node_ident.name.len, node->node_ident.name.data);
                tc->errors++;
                node->resolved_type = Ast_Arena_Type(tc->arena, TYPE_I32, node->line);
                return node->resolved_type;
            }
            node->resolved_type = sym->type;
            return node->resolved_type;
        }

        case NODE_BINARY: {
            Ast_Type* lhs = check_expr(tc, node->node_binary.left);
            
            // rhs unused for compatible check
            Ast_Type* rhs = check_expr(tc, node->node_binary.right);

            // comp ops return bool
            if(node->node_binary.op >= BINOP_EQ && node->node_binary.op <= BINOP_GTE) {
                node->resolved_type = Ast_Arena_Type(tc->arena, TYPE_BOOL, node->line);
                return node->resolved_type;
            }

            // arithmetic result type = lhs type (simplified)
            node->resolved_type = lhs;
            return node->resolved_type;
        }

        case NODE_UNARY: {
            Ast_Type* operand = check_expr(tc, node->node_unary.operand);
            node->resolved_type = operand;
            return node->resolved_type;
        }

        case NODE_ASSIGN: {
            Ast_Type* target = check_expr(tc, node->node_assign.target);
            
            Ast_Type* value  = check_expr(tc, node->node_assign.value );
            //  value unused for check type compatiblity
            // todo: check type compatibility
            
            node->resolved_type = target;
            return node->resolved_type;
        }

        case NODE_CALL: {
            // check callee
            check_expr(tc, node->node_call.callee);
            // check args
            for(i32 i = 0; i<node->node_call.arg_count; i++)
                check_expr(tc, node->node_call.args[i]);
            
            // lookup function symbol to get return type
            if(node->node_call.callee->kind == NODE_IDENT) {
                Symbol* sym = SymTable_Lookup(tc->st, node->node_call.callee->node_ident.name);
                if(sym && sym->kind == SYM_FN && sym->decl) {
                    node->resolved_type = sym->decl->node_fn_decl.ret_type;
                    return node->resolved_type;
                }
            }

            // fallback
            node->resolved_type = Ast_Arena_Type(tc->arena, TYPE_I32, node->line);
            return node->resolved_type;
        }

        case NODE_MEMBER: {
            
            Ast_Type* obj = check_expr(tc, node->node_member.object);
            //  obj unusesd for loopup field
            // todo: stage 2.5: lookup field in struct, compute offset
            // for now assume i32
            node->resolved_type = Ast_Arena_Type(tc->arena, TYPE_I32, node->line);
            return node->resolved_type;
        }

        case NODE_INDEX: {
            check_expr(tc, node->node_index.object);
            check_expr(tc, node->node_index.index);
            node->resolved_type = Ast_Arena_Type(tc->arena, TYPE_I32, node->line);
            return node->resolved_type;
        }

        case NODE_CAST: {
            check_expr(tc, node->node_cast.expr);
            node->node_cast.target_type = resolve_type(tc, node->node_cast.target_type);
            node->resolved_type = node->node_cast.target_type;
            return node->resolved_type;
        }

        case NODE_TYPEOF: {
            Ast_Type* t = check_expr(tc, node->node_typeof.expr);
            node->resolved_type = t;
            return t;
        }

        default:
            node->resolved_type = Ast_Arena_Type(tc->arena, TYPE_I32, node->line);
            return node->resolved_type;
    }
}

static null check_stmt(TypeChecker* tc, Ast_Node* node) {
    if(!node) return;

    switch(node->kind) {
        case NODE_RETURN:
            if(node->node_return.value) check_expr(tc, node->node_return.value);
            break;
        
        case NODE_VAR_DECL:
            node->node_var_decl.type_node = resolve_type(tc, node->node_var_decl.type_node);
            if(node->node_var_decl.init) check_expr(tc, node->node_var_decl.init);
            break;
        
        case NODE_PERSIST_DECL:
            node->node_persist_decl.type_node = resolve_type(tc, node->node_persist_decl.type_node);
            if(node->node_persist_decl.init) check_expr(tc, node->node_persist_decl.init);
            break;
        
        case NODE_IF:
            check_expr(tc, node->node_if.cond);
            check_stmt(tc, node->node_if.then_block);
            if(node->node_if.else_block)
                check_stmt(tc, node->node_if.else_block);
            break;
        
        case NODE_WHILE:
            check_expr(tc, node->node_while.cond);
            check_stmt(tc, node->node_while.body);
            break;
        
        case NODE_FOR:
            if(node->node_for.init) check_stmt(tc, node->node_for.init);
            if(node->node_for.cond) check_expr(tc, node->node_for.cond);
            if(node->node_for.step) check_stmt(tc, node->node_for.step);
            check_stmt(tc, node->node_for.body);
            break;
        
        case NODE_SWITCH:
            check_expr(tc, node->node_switch.expr);
            for(i32 i = 0; i< node->node_switch.case_count; i++) {
                if(node->node_switch.cases[i].value)
                    check_expr(tc, node->node_switch.cases[i].value);
                check_stmt(tc, node->node_switch.cases[i].body);
            }
            break;
        
        case NODE_BLOCK:
            for(i32 i = 0; i < node->node_block.count; i++)
                check_stmt(tc, node->node_block.stmts[i]);
            break;
        
        case NODE_EXPR_STMT:
            check_expr(tc, node->node_expr_stmt.expr);
            break;
        
        default: break;
    
    }
}

static null pass3_typecheck(TypeChecker* tc, Ast_Node* program) {
    if(!program || program->kind != NODE_PROGRAM) return;

    for(i32 i = 0; i < program->node_program.count; i++) {
        Ast_Node* decl = program->node_program.decls[i];
        if(!decl) continue;

        if(decl->kind == NODE_FN_DECL && decl->node_fn_decl.body) {
            // push scope for function body
            SymTable_Push(tc->st);

            // register params as locals
            for(i32 j = 0; j < decl->node_fn_decl.param_count; j++){
                Ast_Param* p = &decl->node_fn_decl.params[j];
                Symbol* sym = SymTable_Define(tc->st, p->name, SYM_PARAM, decl, p->line);
                if(sym) {
                    sym->type       = p->type_node;
                    sym->is_mutable = p->is_mutable;
                }
            }

            // type check body
            check_stmt(tc, decl->node_fn_decl.body);

            SymTable_Pop(tc->st);
        }

        if(decl->kind == NODE_VAR_DECL) {
            check_stmt(tc, decl);
        }
    }
}


// _Public_API_


null TypeChecker_Init(TypeChecker* tc, SymTable* st, Ast_Arena* arena) {
    tc->st = st;
    tc->arena = arena;
    tc->errors = 0;
}

null TypeChecker_Check(TypeChecker* tc, Ast_Node* program) {
    pass1_register_types(tc, program);
    if(tc->errors) return;

    pass2_resolve_types(tc, program);
    if(tc->errors) return;

    pass3_typecheck(tc, program);
}