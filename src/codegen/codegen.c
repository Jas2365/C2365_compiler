/**
 * codegen.c
 * LLVM IR text emitter Stage1
 * 
 * Emitstextual LLVM IR (.ll) consumed by clang as backend
 * 
 * SSA form: every computation produces a fresh %N register.
 * local variables use alloca + store + load pattern.
 * Mutable params (!) are passed as pointers caller auto takes address. 
 * Read-only borrows (&) are passed as const pointers.
 * 
 */

 #include <codegen/codegen.h>

 #include <stdio.h>  // fprintf
 #include <string.h> // memcpy

 // _Helpers_

 static inline i32 next_reg(Codegen* cg) { return cg->reg++; }
 static inline i32 next_label(Codegen* cg) { return cg->label++; }

 #define OUT cg->out

 // emit LLVM type string for an asttype

 static null emit_type(Codegen* cg, const Ast_Type* t) {
    if(!t) { fprintf(OUT, "void"); return; }
    switch(t->kind) {
        case TYPE_NULL: fprintf(OUT, "void");   break;
        case TYPE_BOOL: fprintf(OUT, "i1");     break;
        case TYPE_I8:   fprintf(OUT, "i8");     break;
        case TYPE_I16:  fprintf(OUT, "i16");     break;
        case TYPE_I32:  fprintf(OUT, "i32");     break;
        case TYPE_I64:  fprintf(OUT, "i64");     break;
        case TYPE_U8:   fprintf(OUT, "i8");     break;
        case TYPE_U16:  fprintf(OUT, "i16");     break;
        case TYPE_U32:  fprintf(OUT, "i32");     break;
        case TYPE_U64:  fprintf(OUT, "i64");     break;
        case TYPE_F32:  fprintf(OUT, "float");     break;
        case TYPE_F64:  fprintf(OUT, "double");     break;
        // case TYPE_CHAR:  fprintf(OUT, "i8");     break;  I8 = CHAR
        case TYPE_STRING: 
            // string = {i8*, i32 } fat pointer - stage4
            fprintf(OUT, "{ ptr, i32 }");
            break;
        case TYPE_POINTER:
            fprintf(OUT, "ptr");
            break;
        case TYPE_ARRAY_FIXED:
            fprintf(OUT, "[%lld x", (long long)t->array_fixed.size);
            emit_type(cg, t->array_fixed.elem);
            fprintf(OUT, "]");
            break;
        case TYPE_NAMED:
            fprintf(OUT, "%%struct.%.*s", t->named.name.len, t->named.name.data);
            break;
        default:
            fprintf(OUT, "i32"); // fallback
            break;
    }
 }


// is this type float
static b8 is_float_type(const Ast_Type* t) {
    if(!t) return false;
    return t->kind == TYPE_F32 || t->kind == TYPE_F64;
}

// forward decl
static i32 emit_expr (Codegen* cg, const Ast_Node* node);
static null emit_stmt (Codegen* cg, const Ast_Node* node);
static null emit_block(Codegen* cg, const Ast_Node* block);

// Local variable map

#define LOCAL_MAX 256

typedef struct Local {
    String_View name;
    i32 alloca_reg; // register holding the alloca ptr
    b8 is_float;
} Local;

static Local locals[LOCAL_MAX];
static i32 local_count = 0;

static null locals_reset(void) { local_count = 0; }

static null locals_add(String_View name, i32 alloca_reg, b8 is_float) {
    if(local_count < LOCAL_MAX) {
        locals[local_count++] = (Local) {
            .name = name,
            .alloca_reg = alloca_reg,
            .is_float = is_float,
        };
    }
}

static Local* locals_find(String_View name) {
    for(i32 i = local_count -1; i>=0; i--)
        if(sv_eq(locals[i].name, name)) return &locals[i];
    return nullptr;
}

// expr emitter
// return ssa register number holding the result.

static i32 emit_expr(Codegen* cg, const Ast_Node* node) {
    if(!node) return -1;

    switch (node->kind) {
    
        // integer lit
        case NODE_LIT_INT: {
            i32 r = next_reg(cg);
            fprintf(OUT, "  %%r%d = add i32 0, %lld\n", 
                r, (i64)node->node_lit_int.value);
            return r;
        }

        // float lit
        case NODE_LIT_FLOAT: {
            i32 r = next_reg(cg);
            fprintf(OUT, "  %%r%d = fadd double 0.0, %f\n",
                r, node->node_lit_float.value);
            return r;
        }

        // bool lit
        case NODE_LIT_BOOL: {
            i32 r = next_reg(cg);
            fprintf(OUT, "  %%r%d = add i1 0, %d\n",
                r, node->node_lit_bool.value ? 1 : 0);
            return r;
        }

        // null literal
        case NODE_LIT_NULL: {
            i32 r = next_reg(cg);
            fprintf(OUT, "  %%r%d = inttoptr i64 0 to ptr\n", r);
            return r;
        }

        // identifier load from alloca
        case NODE_IDENT: {
            Local* loc =locals_find(node->node_ident.name);
            if(!loc) {
                fprintf(stderr, "[line %d] error: undefined '%.*s\n",
                node->line, node->node_ident.name.len,node->node_ident.name.data);
                cg->errors++;
                i32 r = next_reg(cg);
                fprintf(OUT, "  %%r%d = add i32 0, 0\n", r);
                return r;
            }
            i32 r = next_reg(cg);
            if(loc->is_float)
                fprintf(OUT, "  %%r%d = load double, ptr %%r%d\n",
                r, loc->alloca_reg);
            else fprintf(OUT, "  %%r%d = load i32, ptr %%r%d\n",
            r, loc->alloca_reg);
            return r;
        }

        // binary expr
        case NODE_BINARY: {
            i32 lhs = emit_expr(cg, node->node_binary.left);
            i32 rhs = emit_expr(cg, node->node_binary.right);
            i32 r   = next_reg(cg);

            b8 flt = is_float_type(node->node_binary.left != nullptr && node->node_binary.left->kind == NODE_LIT_FLOAT ? nullptr : nullptr);
            (void)flt; // stage2 typechecker

            switch(node->node_binary.op) {
                case BINOP_ADD:     fprintf(OUT, "  %%r%d = add i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_SUB:     fprintf(OUT, "  %%r%d = sub i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_MUL:     fprintf(OUT, "  %%r%d = mul i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_DIV:     fprintf(OUT, "  %%r%d = sdiv i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_MOD:     fprintf(OUT, "  %%r%d = srem i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_EQ:      fprintf(OUT, "  %%r%d = icmp eq i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_NEQ:     fprintf(OUT, "  %%r%d = icmp ne i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_LT:      fprintf(OUT, "  %%r%d = icmp slt i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_GT:      fprintf(OUT, "  %%r%d = icmp sgt i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_LTE:     fprintf(OUT, "  %%r%d = icmp sle i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_GTE:     fprintf(OUT, "  %%r%d = icmp sge i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_AND:     fprintf(OUT, "  %%r%d = and i1 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_OR:      fprintf(OUT, "  %%r%d = or i1 %%r%d, %%r%d\n", r, lhs, rhs); break;
                
                case BINOP_BIT_AND: fprintf(OUT, "  %%r%d = and i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_BIT_OR:  fprintf(OUT, "  %%r%d = or i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_BIT_XOR:  fprintf(OUT, "  %%r%d = xor i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                
                case BINOP_LSHIFT:  fprintf(OUT, "  %%r%d = shl i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                case BINOP_RSHIFT:  fprintf(OUT, "  %%r%d = ashr i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
                default: fprintf(OUT, "  %%r%d = add i32 %%r%d, %%r%d\n", r, lhs, rhs); break;
            }
            return r;
        }

        // Unary expr
        case NODE_UNARY: {
            i32 val = emit_expr(cg, node->node_unary.operand);
            i32 r = next_reg(cg);

            switch (node->node_unary.op) {
                case UNOP_NEG:      fprintf(OUT, "  %%r%d = sub i32 0, %%r%d\n", r, val); break;
                case UNOP_NOT:      fprintf(OUT, "  %%r%d = xor i1 %%r%d, 1\n", r, val); break;
                case UNOP_BIT_NOT:  fprintf(OUT, "  %%r%d = xor i32 %%r%d, -1\n", r, val); break;
                case UNOP_DEREF:    fprintf(OUT, "  %%r%d = load i32, ptr %%r%d\n", r, val); break;
                
                default: break;
            }

            return r;
        }

        // assignment
        case NODE_ASSIGN: {
            i32 val = emit_expr(cg, node->node_assign.value);

            if(node->node_assign.target->kind == NODE_IDENT) {
                Local* loc = locals_find(node->node_assign.target->node_ident.name);
                if(loc) {
                    if(loc->is_float) {
                        fprintf(OUT, "  store double %%r%d, ptr %%r%d\n",
                            val, loc->alloca_reg);
                    } else {
                        fprintf(OUT, "  store i32 %%r%d, ptr %%r%d\n", val, loc->alloca_reg);
                    }
                }
            }
            return val;
        }

        // function cal
        case NODE_CALL: {
            i32 arg_regs[64];
            for(i32 i = 0; i < node->node_call.arg_count; i++)
                arg_regs[i] = emit_expr(cg, node->node_call.args[i]);
            
            i32 r = next_reg(cg);
            // stage 2 type checker here to provide real types
            fprintf(OUT, "  %%r%d = call i32 @%.*s(",
                r,
                node->node_call.callee->node_ident.name.len,
                node->node_call.callee->node_ident.name.data);
            for(i32 i =0; i< node->node_call.arg_count; i++) {
                if(i) fprintf(OUT, ", ");
                fprintf(OUT, "i32 %%r%d", arg_regs[i]);
            }
            fprintf(OUT, ")\n");
            return r;
            
        }

        // member access = stage2 (needs typechecker for struct layout)
        case NODE_MEMBER: {
            fprintf(stderr, "[line %d] member access not yet supported in Stage 1\n", node->line);
            cg->errors++;
            i32 r = next_reg(cg);
            fprintf(OUT, "  %%r%d = add i32 0, 0\n", r);
            return r;
        }
        
        default: {
            fprintf(stderr, "[line %d] codegen: unhandled expr node %s\n",
                node->line, Node_Kind_Name(node->kind));
            i32 r = next_reg(cg);
            fprintf(OUT, "  %%r%d = add i32 0, 0\n", r);
            return r;
        }
    }
}

// statement emit
static null emit_stmt(Codegen* cg, const Ast_Node* node) {
    if(!node) return;

    switch(node->kind) {

        // return
        case NODE_RETURN: {
            if(!node->node_return.value) {
                fprintf(OUT, "  ret void\n");
            } else {
                i32 r = emit_expr(cg, node->node_return.value);
                fprintf(OUT, "  ret i32 %%r%d\n", r);
            }
            break;
        }

        // var decl: alloca + optional store
        case NODE_VAR_DECL: {
            i32 alloca_r = next_reg(cg);
            b8 is_float = node->node_var_decl.type_node && 
                          is_float_type(node->node_var_decl.type_node);
            if(is_float)
                fprintf(OUT, "  %%r%d = alloca double\n", alloca_r);
            else 
                fprintf(OUT, "  %%r%d = alloca i32\n", alloca_r);
            
            locals_add(node->node_var_decl.name, alloca_r, is_float);

            if(node->node_var_decl.init && !node->node_var_decl.is_noinit) {
                i32 val = emit_expr(cg, node->node_var_decl.init);
                if(is_float)
                    fprintf(OUT, "  store double %%r%d, ptr %%r%d\n", val, alloca_r);
                else
                    fprintf(OUT, "  store i32 %%r%d, ptr %%r%d\n", val, alloca_r);
            }
            // noinit alloca butno stre , contents undefined
            break;
        }

        // if / else
        case NODE_IF: {
            i32 cond_r = emit_expr(cg, node->node_if.cond);
            i32 then_l = next_label(cg);
            i32 else_l = next_label(cg);
            i32 merge_l = next_label(cg);

            fprintf(OUT, "  br i1 %%r%d, label %%L%d, label %%L%d\n",
            cond_r, then_l, else_l);

            fprintf(OUT, "L%d:\n", then_l);
            emit_block(cg, node->node_if.then_block);
            fprintf(OUT, "  br label %%L%d\n", merge_l);

            fprintf(OUT, "L%d:\n", else_l);
            if(node->node_if.else_block)
                emit_block(cg, node->node_if.else_block);
            fprintf(OUT, "  br label %%L%d\n", merge_l);

            fprintf(OUT, "L%d:\n", merge_l);
            break;
        }

        // while
        case NODE_WHILE: {
            
            i32 cond_l = next_label(cg);
            i32 body_l = next_label(cg);
            i32 exit_l = next_label(cg);

            fprintf(OUT, "  br label %%L%d\n", cond_l);
            fprintf(OUT, "L%d:\n", cond_l);
            i32 cond_r = emit_expr(cg, node->node_while.cond);
            fprintf(OUT, "  br i1 %%r%d, label %%L%d, label %%L%d\n",
                cond_r, body_l, exit_l);
            fprintf(OUT, "L%d:\n", body_l);
            emit_block(cg, node->node_while.body);
            fprintf(OUT, "  br label %%L%d\n", cond_l);

            fprintf(OUT, "L%d:\n", exit_l);
            break;
        }

        // for
        case NODE_FOR: {
            if(node->node_for.init) emit_stmt(cg, node->node_for.init);

            i32 cond_l = next_label(cg);
            i32 body_l = next_label(cg);
            i32 exit_l = next_label(cg);
        
            fprintf(OUT, "  br label %%L%d\n", cond_l);
            fprintf(OUT, "L%d:\n", cond_l);

            if(node->node_for.cond) {
                i32 cond_r = emit_expr(cg, node->node_for.cond);
                fprintf(OUT, "  br i1 %%r%d, label %%L%d, label %%L%d\n",
                    cond_r, body_l, exit_l);
            } else {
                fprintf(OUT, "  br label %%L%d\n", body_l);
            }

            fprintf(OUT, "L%d:\n", body_l);
            emit_block(cg, node->node_for.body);
            if(node->node_for.step) emit_stmt(cg, node->node_for.step);
            fprintf(OUT, "  br label %%L%d\n", cond_l);

            fprintf(OUT, "L%d:\n", exit_l);
            break;
        }

        // break -continue stage6 needs loop label stack
        case NODE_BREAK:
        case NODE_CONTINUE:
            fprintf(stderr, "break/continue: requires loop label stack (stage6)\n");
            break;
        
        // expr stmt
        case NODE_EXPR_STMT:
            emit_expr(cg, node->node_expr_stmt.expr);
            break;
        
        // block
        case NODE_BLOCK:
            emit_block(cg, node);
            break;
        
        // persist - static local stage 6
        case NODE_PERSIST_DECL:
            fprintf(stderr, "persist: stage6\n");
            break;
        
        default:
            fprintf(stderr, "codegen: unhandled stmt node %s\n", 
                Node_Kind_Name(node->kind));
            break;
    }
}

static null emit_block(Codegen* cg, const Ast_Node* block) {
    if(!block || block->kind != NODE_BLOCK) return;
    for(i32 i = 0; i< block->node_block.count; i++) 
        emit_stmt(cg, block->node_block.stmts[i]);
}

// function emitter

static null emit_fn(Codegen* cg, const Ast_Node* fn) {
    if(fn->kind != NODE_FN_DECL) return;

    cg->reg = 0;
    cg->label = 0;
    locals_reset();

    // for .cm decl
    if(!fn->node_fn_decl.body) {
        fprintf(OUT, "declare ");
        emit_type(cg, fn->node_fn_decl.ret_type);
        fprintf(OUT, " @%.*s(",
            fn->node_fn_decl.name.len,fn->node_fn_decl.name.data 
        );
        for(i32 i = 0; i< fn->node_fn_decl.param_count; i++) {
            if(i) fprintf(OUT, ", ");
            const Ast_Param* p = &fn->node_fn_decl.params[i];
            if(p->is_mutable) 
                fprintf(OUT, "ptr"); // ! param -> pointer in IR
            else 
                emit_type(cg, p->type_node);
        }
        fprintf(OUT, ")\n\n");
        return;
    }

    // def
    const i8* linkage = fn->node_fn_decl.is_internal ? "internal" : "";
    fprintf(OUT, "define %s", linkage);
    emit_type(cg, fn->node_fn_decl.ret_type);
    fprintf(OUT, " @%.*s(", fn->node_fn_decl.name.len, fn->node_fn_decl.name.data);

    for(i32 i = 0; i< fn->node_fn_decl.param_count; i++) {
        if(i) fprintf(OUT, ", ");
        const Ast_Param* p = &fn->node_fn_decl.params[i];
        if(p->is_mutable)
            fprintf(OUT, "ptr %%p%d", i); // ! param - pointer
        else 
            fprintf(OUT, "i32 %%p%d", i); // const param - value, stage 2 real type
    }

    fprintf(OUT, ") {\nentry:\n");

    // alloca each param into local slot 


    for(i32 i = 0; i< fn->node_fn_decl.param_count; i++) {
        const Ast_Param* p = &fn->node_fn_decl.params[i];
        b8 is_float = is_float_type(p->type_node);

        if(p->is_mutable) {
            // mutable param - already pointer, registr it directly
            // alloca_reg points tothe incoming ptr argument
            i32 slot = next_reg(cg);
            fprintf(OUT, "  %%r%d = alloca ptr\n", slot);
            fprintf(OUT, "  store ptr %%p%d, ptr %%r%d\n", i, slot);
            locals_add(p->name, slot, false);
        } else {
            // const param - alloca a slot and store the value
            i32 slot = next_reg(cg);
            if(is_float) {
                fprintf(OUT, "  %%r%d = alloca double\n", slot);
                fprintf(OUT, "  store double, %%p%d, ptr %%r%d\n", i, slot);
            } else {
                fprintf(OUT, "  %%r%d = alloca i32\n", slot);
                fprintf(OUT, "  store i32 %%p%d, ptr %%r%d\n", i, slot);
            }
            locals_add(p->name, slot, is_float);
        }
    }

    // emit body
    emit_block(cg, fn->node_fn_decl.body);

    // if last null, emit ret void
    if(fn->node_fn_decl.ret_type && fn->node_fn_decl.ret_type->kind == TYPE_NULL)
        fprintf(OUT, "  ret void\n");

    fprintf(OUT, "}\n\n");
}

// top level emitter

null Codegen_Init(Codegen* cg, FILE* out, SymTable* st) {
    cg->out = out;
    cg->st = st;
    cg->reg = 0;
    cg->label = 0;
    cg->errors = 0;
}

null Codegen_Emit(Codegen* cg, const Ast_Node* program) {
    
    // ir file header
    fprintf(OUT, "; LLVM IR generated by compiler\n");
    fprintf(OUT, "target triple = \"x86_64-w64-windows-gnu\"\n\n");

    if(!program || program->kind != NODE_PROGRAM) return;

    for(i32 i = 0; i < program->node_program.count; i++) {
        const Ast_Node* decl = program->node_program.decls[i];
        if(!decl) continue;

        switch (decl->kind) {
            case NODE_FN_DECL:
                emit_fn(cg, decl);
                break;
            case NODE_MODULE:
            case NODE_IMPORT:
                fprintf(OUT, "; %s %.*s\n",
                decl->kind == NODE_MODULE ? "module" : "import", 
                decl->kind == NODE_MODULE ? decl->node_module.name.len : decl->node_import.name.len,
                decl->kind == NODE_MODULE ? decl->node_module.name.data : decl->node_import.name.data );
                break;

            case NODE_VAR_DECL:
                fprintf(stderr, "global var decl: stage 2\n");
                break;

            case NODE_TYPE_ALIAS:
                // struct type regis - stage 2
                // for nw emit comment
                fprintf(OUT, "; type %.*s\n",
                    decl->node_type_alias.name.len, decl->node_type_alias.name.data);
                break;
            
            default:
                break;
        }
    }
}