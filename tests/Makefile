CLANG ?= clang
OPT   ?= opt

# pass name 
PASS_OPT ?= svf-pass

PLUGIN := ../../install/lib/SVFTypeInferPass.so

# Exactly one C file in this directory
CFILE := $(wildcard *.c)
LL    := test.ll
OPTBC := test_opt.bc
OPTLL := test_opt.ll
OUT   := output.txt
BIN   := test
SUBDIRS := $(wildcard */)

all: pass bin 

pass: $(LL) 
	$(OPT) -enable-new-pm=0 -load $(PLUGIN) -$(PASS_OPT) $(LL) -o $(OPTBC) > $(OUT) 2>&1
	@echo "Wrote $(OUT)"
	cat $(OUT)
	llvm-dis $(OPTBC) -o $(OPTLL)

bin: $(BIN)

$(BIN): $(CFILE)
	$(CLANG) -O0 -g  -flegacy-pass-manager  -fno-discard-value-names \
	  -Xclang -load -Xclang $(PLUGIN) \
	  -o $(BIN) $(CFILE)

$(LL): $(CFILE)
	$(CLANG) -O0 -g -S -emit-llvm -o $(LL) $(CFILE)

clean:
	rm -f $(OPTBC) $(OPTLL) $(LL) $(OUT) $(BIN)
