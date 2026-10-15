# makefile for NY8 16 bit with clang/LLVM
###############################################################################
# .c (C source) ------> .o (LLVM bit code) ---+
#                clang                        |
#                                             |
# .s (assembly) ------> .o (ELF) -------------+---> project.elf
#                clang -cc1as               clang(ld.lld)
###############################################################################
# make target:
#  1. make all
#     Output a elf file
#  2. make asm
#     Output a assembly file. You can add it to NYIDE for debug.
#  3. make clean
###############################################################################

NY8_LLVM_ROOT = 

CPU := ny8bm84a

C_SRCS = User\adc.c User\battery.c User\display.c User\gpio.c User\input.c User\main.c User\system.c User\temp.c  
AS_SRCS =  
LIB_FILES = 
OUT_ELF = DL-14542.elf
TARGET = DL-14542

CC = clang
AS = clang
LD = clang

CFLAGS  := $(CFLAGS) -mcpu=$(CPU) -Wno-gcc-compat -Wno-nonportable-include-path \
           -ffunction-sections -fdata-sections \
           -g -flto -Os
LDFLAGS := $(CFLAGS)
CFLAGS  := $(strip $(CFLAGS) -I"Include" -I"C:\Nyquest\NYIDE\include\NY8 C" )
ASFLAGS := $(ASFLAGS) -mcpu=$(CPU) -g

LDFLAGS := $(LDFLAGS)   -mny8-bak-int-pch -mno-ny8-bak-int-tbhp -mno-ny8-bak-int-selcon 
LDFLAGS := $(strip $(LDFLAGS))
# LDFLAGS := $(LDFLAGS) --print-memory-usage

OBJS_BC  := $(patsubst %.c,OBJ/%.o,$(C_SRCS))
OBJS_LS  := $(patsubst %.s,OBJ/%.o,$(filter %.s,$(AS_SRCS)))
OBJS_BS  := $(patsubst %.S,OBJ/%.o,$(filter %.S,$(AS_SRCS)))
OBJS_ELF := $(strip $(OBJS_LS) $(OBJS_BS))
OBJS     := $(strip $(OBJS_BC) $(OBJS_ELF))
DEPS     := $(patsubst %.o,%.d,$(OBJS_BC))
OUT_LST  := $(OUT_ELF:.elf=.lst)
OUT_ASM  := $(OUT_ELF:.elf=.s)

OBJ_DIRS := $(sort $(dir $(OBJS)))
OBJ_DIRS := $(OBJ_DIRS:%\=%)

### silent command line
ifneq ($(SHOW_CMD),1)
  L = @
endif

.SUFFIXES:
.SECONDARY:
.PHONY: all asm clean

all: $(OUT_ELF) $(OUT_LST)

asm: $(OUT_ASM)

clean:
	$(L)echo Clean Project...
	-$(L)1>nul 2>nul rmdir /s /q OBJ
	-$(L)1>nul 2>nul del "$(TARGET).bin" "$(TARGET).fpga" "$(TARGET).elf" "$(TARGET).lst" "$(TARGET).map" "$(TARGET).htm" "$(TARGET).rom"

$(OBJ_DIRS):
	-@1>nul 2>nul mkdir $(subst /,\,$@)

$(OUT_LST): $(OUT_ELF)
	$(L)llvm-objdump.exe --mcpu=$(CPU) --disassemble --line-numbers --source --disassemble-zeroes -j .reset -j .interrupt -j .rolling -j .jtable -j .text "$<" > "$@"

$(OUT_ELF): $(OBJS) $(LD_SCRIPT) | $(OBJ_DIRS)
	@echo Linking...
	$(L)$(LD) $(LDFLAGS) -Wl,--Map="$(@:.elf=.map)" -o "$@" $(OBJS) $(LIB_FILES)

$(OUT_ASM): $(OUT_ELF).0.5.precodegen.bc
	$(L)$(CC) -mcpu=$(CPU) -ffunction-sections -fdata-sections -Os -S -o "$@" "$<"

$(OUT_ELF).0.5.precodegen.bc: $(OBJS) | $(OBJ_DIRS)
	$(L)$(LD) $(LDFLAGS) -Wl,--save-temps -o "$(OUT_ELF)" $(OBJS) $(LIB_FILES)

$(OBJS_BC): OBJ/%.o: %.c | $(OBJ_DIRS)
	@echo Building file ... $<
	$(L)$(CC) -c $(CFLAGS) -MD -MF $(@:.o=.d) -o $@ $<

$(OBJS_LS): OBJ/%.o: %.s | $(OBJ_DIRS)
	$(L)$(AS) -c $(ASFLAGS) -o $@ $<

$(OBJS_BS): OBJ/%.o: %.S | $(OBJ_DIRS)
	@echo Building file ... $<
	$(L)$(AS) -c $(ASFLAGS) -o $@ $<

-include $(DEPS)
