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
    if(!t) { pfrintf(OUT, "void"); return; }
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
static i32 emit_stmt (Codegen* cg, const Ast_Node* node);
static i32 emit_block(Codegen* cg, const Ast_Node* block);

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
            fprintf(OUT, "  %%r%d = call %i32 @%.*s(",
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

    }
}