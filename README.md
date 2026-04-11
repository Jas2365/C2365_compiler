# Compiler

A modern, modular compiler for a statically-typed systems language designed as a better C.

## Overview

This project implements a compiler that builds a bridge between C's performance and simplicity, and modern language design. The language (tentatively `.ci` files) features a clean syntax inspired by Go and Rust, with explicit module boundaries, first-class strings, generics, and memory safety primitives—all while maintaining direct LLVM IR generation for optimal performance.

**Current Stage:** Foundation (Stage 1)

## Quick Start

### Building

```bash
make
```

This builds the compiler from the C source files into an executable.

### Running

```bash
./compiler <source.ci>
```

The compiler reads a `.ci` source file and generates LLVM IR.

## Language Features (Roadmap)

The language is built incrementally across 9 stages. Each stage builds on previous work:

### ⚫ Stage 1 — Foundation (Current)
The bare minimum to compile anything.

- **Lexer** — tokens for all keywords, operators, literals
- **AST nodes** — every construct in the language as a tree node
- **Parser** — recursive descent, `.cm` and `.ci` modes
- **Symbol table** — scoped, module-aware hash map
- **LLVM IR emitter** — basic functions, arithmetic, return

**Goal:** Compile and run `fn main() -> i32 { return 42; }`

### ⚪ Stage 2 — Type System
- Primitive types: `i8`, `i16`, `i32`, `i64`, `f32`, `f64`
- `null` type, `null*`, and `nullptr`
- `const` and `!` (mutable) parameters
- `typeof` — compile time resolution, zero IR cost
- Pointer types with star counting and array rank rules
- Struct and union types
- Type aliases

**Goal:** Declare and use structs, pointers, type aliases

### ⚪ Stage 3 — Module System
- `module` declaration and scoping
- `import` resolver — finds `.cm` files on module path
- `internal` keyword — module-private symbols
- `.cmi` emitter — compiles `.cm` + `.ci` → object + manifest
- `.cmi.meta` manifest — exports and type hashes
- Pre-link validator — manifest checking before linking

**Goal:** Two modules importing each other, linked into one binary

### ⚪ Stage 4 — Memory & Strings
- `string` builtin type — arena-backed fat pointer
- String literals — auto constructed via arena
- `string` vs `string&` — copy vs shared semantics
- Arena integration — `alloc()` builtin
- `noinit` keyword

**Goal:** Declare and manipulate strings, arena alloc/free

### ⚪ Stage 5 — Arrays & Generics
- `i32[N]` fixed arrays — stack allocated, `ArrayN(T, N)` internally
- `i32[]` dynamic arrays — `Array(T)` internally
- Array bounds tracking — fat pointer `{ T* data, s64 len }`
- Generic types like `List(T)` — monomorphized at compile time
- Generic functions: `fn push(T)(...)`
- Integer parameters for builtins only — `ArrayN` internal use

**Goal:** Generic `List` and `Array` working end-to-end

### ⚪ Stage 6 — Control Flow & Keywords
- `if` / `else` branching
- `while` and `for` loops
- `switch` statements
- `persist` — static local storage
- Function pointers — `fn(i32) -> i32` as type expressions
- Type aliases for function types

**Goal:** Non-trivial programs with loops and branching

### ⚪ Stage 7 — Visibility & Safety
- `private` struct members
- Member access enforcement in typechecker
- `.` member access — automatic pointer dereferencing
- Multi-star pointer → array rank resolution
- Bounds checking on `[]` access — optional, flag controlled

**Goal:** Full struct visibility and safe pointer member access

### ⚪ Stage 8 — Optimization & Polish
- `inline fn` — replaces function-like macros
- `typeof` caching — same expression never re-evaluated
- Constant folding — `const` expressions resolved at compile time
- Dead code elimination — `internal` symbols never exported
- Incremental compilation — `.cmi.meta` hash invalidation

**Goal:** Clean optimized output and fast incremental builds

### ⚪ Stage 9 — Standard Modules
Replace all C standard headers with native modules:

- `import io` — replaces `stdio.h`
- `import mem` — replaces `stdlib.h`, `string.h`
- `import math` — replaces `math.h`
- `import types` — all primitives, `null`, `string`
- `import arena` — arena allocator as first-class module

**Goal:** Compile real programs with zero C headers

## Project Structure

```
.
├── inc/                 # Header files (public interfaces)
│   ├── defs.h          # Core definitions
│   ├── ints.h          # Integer type definitions
│   ├── ast/            # AST node definitions
│   ├── codegen/        # Code generation interfaces
│   ├── lexer/          # Lexer interfaces
│   ├── parser/         # Parser interfaces
│   ├── symtable/       # Symbol table interfaces
│   └── StringView/     # String view utilities
├── src/                # Implementation files
│   ├── main.c          # Entry point
│   ├── ast/            # AST implementation
│   ├── codegen/        # LLVM IR code generation
│   ├── lexer/          # Tokenizer implementation
│   ├── parser/         # Parser implementation
│   └── symtable/       # Symbol table implementation
├── test/               # Test files
│   ├── lexer/          # Lexer tests
│   └── modules/        # Module test cases
├── stdlib/             # Standard library (future)
├── build/              # Compiled artifacts
├── Makefile            # Build configuration
└── README.md           # This file
```

## Key Design Decisions

### Arena Allocation
Memory is managed through arena allocators for predictable performance and simplified cleanup. AST nodes live in a single arena.

### Module-Aware Compilation
Unlike C, modules are first-class citizens. Each module can have:
- **Implementation** (`.ci`) — private code
- **Interface** (`.cm`) — public exports
- **Manifest** (`.cmi.meta`) — type hashes for incremental compilation

### Type Safety from Day 1
The type system is not an afterthought. Even Stage 1 foundations are built with type checking infrastructure in place.

### String as a Builtin
No more string.h or string manipulation pain. Strings are first-class with arena-backed fat pointers and automatic semantics (`string` vs `string&`).

### Zero Unsafe Code (Eventually)
Stages 7-8 add bounds checking and pointer safety. The language is designed to make unsafe operations explicit and rare.

## Building from Source

### Prerequisites
- GCC or Clang
- Make
- LLVM (for code generation, added in Stage 1)

### Compile
```bash
make
```

### Clean
```bash
make clean
```

## Example Program (Target)

```c
module math;

fn add(i32 a, i32! b) -> i32 {
    return a + b;
}
```

```c
import math;

fn main() -> i32 {
    return math.add(10, 20);
}
```

## Development Roadmap

| Stage | Focus | Status |
|-------|-------|--------|
| 1 | Foundation | 🟡 In Progress |
| 2 | Type System | ⚪ Planned |
| 3 | Module System | ⚪ Planned |
| 4 | Memory & Strings | ⚪ Planned |
| 5 | Arrays & Generics | ⚪ Planned |
| 6 | Control Flow | ⚪ Planned |
| 7 | Safety | ⚪ Planned |
| 8 | Optimization | ⚪ Planned |
| 9 | Standard Library | ⚪ Planned |

## Contributing

This project is in active development. Contributions are welcome for Stage 1 completion and beyond.

## License

See [LICENSE](LICENSE) for details.
