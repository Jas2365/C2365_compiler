# --- Compiler ---
cxx      = gcc
std     = -std=c23
warns   = -Wall -Wextra -Wpedantic
opt     = -O0 -g
iflags  = -Iinc \
		  -Iinc/ast \
		  -Iinc/lexer \
		  -Iinc/parser \
		  -Iinc/codegen \
		  -Iinc/symtable \
		  -Iinc/StringView \
		  -Iinc/typechecker


cflags  = $(std) $(warns) $(opt) $(iflags)

src_dir = src
out_dir = out

inc = -Iinc -Iinc/strings -Iinc/lists
srcs = $(wildcard $(src_dir)/*.c) $(wildcard $(src_dir)/**/*.c)

objs = $(patsubst $(src_dir)/%.c, $(out_dir)/%.o, $(srcs))

# Detect OS
ifeq ($(OS),Windows_NT)
	MKDIR = if not exist "$(subst /,\,$@)" mkdir $(subst /,\,$@)"
	RM    = if exist "$(out_dir)" rd /s /q $(out_dir)
else
	MKDIR = mkdir -p $@
	RM    = rm -rf $(out_dir)
endif

dirs := $(sort $(dir $(objs)))

target = out/Compiler.exe

all: clean build run

build: $(objs) $(target)

#dirs
$(dirs):
	@$(MKDIR)

#  linking 
$(target): $(objs)
	@echo Linking $@ ...
	@$(cxx) $(objs) -o $(target)

# compilation
$(out_dir)/%.o : $(src_dir)/%.c | $(dirs)
	@echo Compiling $< ...
	@$(cxx) $(cflags) $(inc) -c $< -o $@

run:
	@echo  ---- Running Project ---- 
	@./$(target)
	@echo  ---- Ending  Project ---- 
	
clean:
	@echo ---- Cleaning up ----
	@$(RM)
