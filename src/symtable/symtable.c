#include <symtable/symtable.h>

#include <stdio.h>
#include <string.h>

// hash
// fnv-1a, fast 

static u32 fnv1a(String_View sv) {
    u32 hash = 2166136261u;
    for(i32 i = 0; i< sv.len; i++) {
        hash ^= (u8)sv.data[i];
        hash *= 16777619u;
    }
    return hash;
}

// scope internals

static Scope* scope_alloc(Ast_Arena* arena, i32 cap) {
    Scope* s = (Scope*)Ast_Arena_Array(arena, (i64)sizeof(Scope), 1);
    s->entries = (Symbol**)Ast_Arena_Array(arena, (i64)sizeof(Symbol*), cap);
    s->cap = cap;
    s->count = 0;
    s->parent = nullptr;
    return s;
}

// insert 
static null scope_insert(Scope* s, Symbol* sym) {
    u32 hash = fnv1a(sym->name);
    i32 idx = (i32)(hash & (u32)(s->cap -1));
    while(s->entries[idx] != nullptr)
        idx = (idx + 1) & (s->cap -1);
    s->entries[idx] = sym;
    s->count++;
}

// grow scope. if load factor > 0.7

static null scope_maybe_grow(Scope* s, Ast_Arena* arena) {
    if(s->count * 10 < s->cap * 7) return;

    i32 new_cap = s->cap * 2;
    Symbol** new_entries = (Symbol**)Ast_Arena_Array(
        arena, (i64)sizeof(Symbol*), new_cap
    );

    Symbol** old = s->entries;
    i32 old_cap = s->cap;
    s->entries = new_entries;
    s->cap = new_cap;
    s->count = 0;

    for(i32 i = 0; i< old_cap; i++) {
        if(old[i]) scope_insert(s, old[i]);
    }
}

// lookup single scope
static Symbol* scope_find(const Scope* s, String_View name) {
    if(!s || s->count == 0) return nullptr;
    u32 hash = fnv1a(name);
    i32 idx = (i32)(hash & (u32)(s->cap -1));
    i32 probed = 0;

    while(s->entries[idx] != nullptr && probed < s->cap) {
        if(sv_eq(s->entries[idx]->name, name))
            return s->entries[idx];
        idx = (idx + 1) & (s->cap -1);
        probed++;
    }
    return nullptr;
}


// api

null SymTable_Init(SymTable* st, Ast_Arena* arena) {
    st->arena = arena;
    st->errors = 0;
    st->module = scope_alloc(arena, SCOPE_INITIAL_CAP);
    st->current = st->module;
}

null SymTable_Push(SymTable* st) {
    Scope* s = scope_alloc(st->arena, SCOPE_INITIAL_CAP);
    s->parent = st->current;
    st->current = s;
}

null SymTable_Pop(SymTable* st) {
    if(st->current == st->module) {
        fprintf(stderr, "symtable error: cannot pop module scope\n");
        return;
    }
    st->current = st->current->parent;
}

Symbol* SymTable_Define(SymTable* st, String_View name, SymKind kind, Ast_Node* decl, i32 line) {
    // duplicate check
    if(scope_find(st->current, name)) {
        fprintf(stderr, "[line %d] error: '%.*s' already defined in this scope\n",
        line, name.len, name.data);
        st->errors++;
        return nullptr;
    }

    scope_maybe_grow(st->current, st->arena);

    Symbol* sym = (Symbol*)Ast_Arena_Array(st->arena, (i64)sizeof(Symbol), 1);
    sym->name = name;
    sym->kind = kind;
    sym->type = nullptr; // for typecheker to fill
    sym->decl = decl;
    sym->line = line;
    
    // flags default to false - caller sets them after define returns

    scope_insert(st->current, sym);
    return sym;
}

// chain lookup
Symbol* SymTable_Lookup(SymTable* st, String_View name) {
    Scope* s = st->current;
    while(s) {
        Symbol* sym = scope_find(s, name);
        if(sym) return sym;
        s = s->parent;
    }
    return nullptr;
}

// current scope
Symbol* SymTable_LookupLocal(SymTable* st, String_View name) {
    return scope_find(st->current, name);
}
// module scope
Symbol* SymTable_LookupModule(SymTable* st, String_View name) {
    return scope_find(st->module, name);
}

b8 SymTable_IsDefined(SymTable* st, String_View name) {
    return scope_find(st->current, name) != nullptr;
}

// Debug

const i8* SymKind_Name(SymKind kind) {
    switch (kind) {

    case SYM_VAR: return "VAR";
    case SYM_FN: return "FN";
    case SYM_TYPE: return "TYPE";
    case SYM_PARAM: return "PARAM";
    case SYM_MODULE: return "MODULE";
    case SYM_FIELD: return "FIELD";
    default: return "UNKNOWN";
    }
}


static null print_scope(const Scope* s, i32 depth) {
    for(i32 i = 0; i< s->cap ; i++) {
        Symbol* sym = s->entries[i];
        if(!sym) continue;
        for(i32 d = 0; d < depth; d++) printf("  ");
        printf("[%s] %.*s%s%s%s%s%s (line %d)\n",
            SymKind_Name(sym->kind),
            sym->name.len, sym->name.data,
            sym->is_internal ? " internal": "",
            sym->is_mutable ? " mutable": "",
            sym->is_const ? " const": "",
            sym->is_private ? " private": "",
            sym->is_persist ? " persist": "",
            sym->line
        );
    }
}


null SymTable_Print(const SymTable* st) {
    printf("[module scope]\n");
    print_scope(st->module, 1);

    // walk scope chain from current back to module
    i32 depth = 0;
     Scope* s = st->current;
     while(s && s != st->module) { depth++; s = s->parent; }

     // print each inner scope
     s = st->current;
     i32 d = depth;
     while(s && s != st->module) {
        printf("[block scope depth=%d]\n", d--);
        print_scope(s, 1);
        s = s->parent;
     }
}

