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

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef void null;

// _Signed_
typedef char   i8;
typedef short i16;
typedef int i32;
typedef long long i64;

typedef const char   ci8;
typedef const short ci16;
typedef const int ci32;
typedef const long long ci64;

// _Unsigned_
typedef unsigned char   u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef const unsigned char   cu8;
typedef const unsigned short cu16;
typedef const unsigned int cu32;
typedef const unsigned long long cu64;

// _Signed_Pointer_
typedef char   *ip8;
typedef short *ip16;
typedef int *ip32;
typedef long long *ip64;

typedef const char   *cip8;
typedef const short *cip16;
typedef const int *cip32;
typedef const long long *cip64;

// _Unsigned_Pointer_
typedef unsigned char   *up8;
typedef unsigned short *up16;
typedef unsigned int *up32;
typedef unsigned long long *up64;

typedef const unsigned char   *cup8;
typedef const unsigned short *cup16;
typedef const unsigned int *cup32;
typedef const unsigned long long *cup64;

// _Size_
typedef u64   s64;
typedef u64 *sp64;

// _Bool_
typedef bool   b8;
typedef i32   b32;
typedef bool *bp8;
typedef i32 *bp32;

// _Float_
typedef float    f32;
typedef double   f64;
typedef float  *fp32;
typedef double *fp64;

