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

#pragma once

#include <ints.h>
#include <string.h> // memcmp

typedef struct String_View {
    const i8* data;
    i32       len;
} String_View;


// _Constructors_

// from token no null termination ( pointer + len )
static inline String_View sv_from_token(const i8* data, i32 len) {
    return (String_View){ .data = data, .len = len };
}

// from a C string literal - ONLY for compiler internals 
#define sv_lit(s) ((String_View){.data = (s), .len = (i32)(sizeof(s) -1)})


// _Operations_

static inline b8 sv_eq(String_View a, String_View b) {
    if(a.len != b.len) return false;
    return memcmp (a.data, b.data, (size_t)a.len) == 0;
}

static inline b8 sv_eq_lit(String_View a, const i8* lit, i32 lit_len) {
    if(a.len != lit_len) return false;
    return memcmp(a.data, lit, (size_t)lit_len) == 0;
}

static inline b8 sv_empty(String_View sv) {
    return sv.len == 0 || sv.data == nullptr;
}

