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

typedef void null;

// _Signed_
typedef char   i8;
typedef short i16;
typedef int i32;

#if defined (_WIN64) || defined(__LP64__)
typedef long long i64;
#else
typedef int i64;
#endif

typedef const i8   ci8;
typedef const i16 ci16;
typedef const i32 ci32;
typedef const i64 ci64;

// _Unsigned_
typedef unsigned char   u8;
typedef unsigned short u16;
typedef unsigned int u32;

#if defined (_WIN64) || defined(__LP64__)
typedef unsigned long long u64;
#else
typedef unsigned int u64;
#endif

typedef const u8   cu8;
typedef const u16 cu16;
typedef const u32 cu32;
typedef const u64 cu64;

// _Signed_Pointer_
typedef i8*   ip8;
typedef i16* ip16;
typedef i32* ip32;
typedef i64* ip64;

typedef const i8*   cip8;
typedef const i16* cip16;
typedef const i32* cip32;
typedef const i64* cip64;

// _Unsigned_Pointer_
typedef u8*   up8;
typedef u16* up16;
typedef u32* up32;
typedef u64* up64;

typedef const u8*   cup8;
typedef const u16* cup16;
typedef const u32* cup32;
typedef const u64* cup64;

// _Size_
typedef u64   s64;
typedef u64* sp64;
// _Pointer_
typedef u64   p64;
typedef u64* pp64;

// _Bool_
typedef i8   b8;
typedef i32  b32;
typedef i8*  bp8;
typedef i32* bp32;

// _Float_
typedef float    f32;
typedef double   f64;
typedef float*   fp32;
typedef double*  fp64;
