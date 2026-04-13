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

 /**
  * parser.h 
  * Recursive descent parser.
  * Takes a Tokenarray from the lexer and builds AstNode tree
  * using Ast Arena
  * 
  * Two parse modes:
  *     PARSE_MODE_CM - .cm interface files, declarations only, no bodies
  *     PARSE_MODE_CI - .ci implementation files, full definitions allowed
  * 
  * Expression parsing uses a pratt parser topdown operator precedence so 
  * opertor precedence is handled cleanly without grammar recursion.
  * 
  */


  #pragma once

  #include <lexer/lexer.h>
  #include <ast/ast.h>

  // _Parse_Mode_

  typedef enum Parse_Mode {
    PARSE_MODE_CI, // .ci 
    PARSE_MODE_CM, // .cm
  } Parse_Mode;

  // _Parse_State_

  typedef struct Parser {
    const Token*    tokens; 
    i32             count;
    i32             pos; // current pos
    Parse_Mode      mode;
    Ast_Arena*      arena; // all nodes here
    i32             errors; // error count parser keep going after errors
  } Parser;

  // _API_

  null Parser_Init (Parser* p, const Token_Array* tokens, Ast_Arena* arena, Parse_Mode mode);
  Ast_Node* Parser_Run(Parser* p); // returns NODE_PROGRAM or nullptr on fatal error

