#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* unix */
#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/types.h>

/* Memory - 65536 locations */
uint16_t memory[UINT16_MAX];

/* Registers --- register stores single value on cpu*/
enum {
  R_R0 = 0, /*R0 to R7 general purpose registers*/
  R_R1,     /* https://www.tutorialspoint.com/enum-in-c, setting r0=0, makes r1=1, r2=2 and so on */
  R_R2,
  R_R3,
  R_R4,
  R_R5,
  R_R6,
  R_R7,
  R_PC,   /* program counter */
  R_COND, /* Condition flag, tells us info about previous calculation */
  R_COUNT // think this is the enums name? or size?
};

/* Store registers in an array */
uint16_t reg[R_COUNT];

/* Instruction set */
/* Each has optcode(kind of task to perform), and set of parameters */
/* RISCs(Reduced instruction set computer) Vs CISCs(Complex instruction set computer)
    Intel x86 architecture --- CISC
    ARM and LC-3(what we're using) --- RISC
    both have pros and cons (CISC easieer to write assembly for, but more complictated to engineer and manufacture etc..)
    so these two keep going in and out off style over histroy
*/
enum {
  OP_BR = 0, /* branch */
  OP_ADD,    /* add  */
  OP_LD,     /* load */
  OP_ST,     /* store */
  OP_JSR,    /* jump register */
  OP_AND,    /* bitwise and */
  OP_LDR,    /* load register */
  OP_STR,    /* store register */
  OP_RTI,    /* unused */
  OP_NOT,    /* bitwise not */
  OP_LDI,    /* load indirect */
  OP_STI,    /* store indirect */
  OP_JMP,    /* jump */
  OP_RES,    /* reserved (unused) */
  OP_LEA,    /* load effective address */
  OP_TRAP    /* execute trap */
};


/* Conditional Flags */
/* Provide infomation on most recently executed calculation, allows programs 
to check logcial condtions(e.g. if statements)  
LC-3 uses only three conditional flags(that indicate sign of prev calc), but other architectures have more
*/
enum {
  FL_POS = 1 << 0, /* P */ /* Left bit shifting */
  FL_ZRO = 1 << 1, /* Z */
  FL_NEG = 1 << 2, /* N */
};

//update condtion flag
void update_flags(uint16_t r) {
  if (reg[r] == 0) {
    reg[R_COND] = FL_ZRO;
  } 
  else if (reg[r] >> 15){ /* a 1 in the left-most bit indicates negative */
    reg[R_COND] = FL_NEG;
  } 
  else {
    reg[R_COND] = FL_POS;
  }
}

//2's complements, deals with negative numbers
uint16_t sign_extend(uint16_t x, int bit_count) {
  if ((x >> (bit_count - 1)) & 1) {
    x |= (0xFFFF << bit_count);
  }
  return x;
}









int main(int argc, const char *argv[]) {

  /* Load Arguments */
  if (argc < 2) { //{Load Arguments, 5}
    /* show usage string */
    printf("lc3 [image-file1] ...\n");
    exit(2);
  }

  for (int j = 1; j < argc; ++j) {
    if (!read_image(argv[j])) {
      printf("failed to load image: %s\n", argv[j]);
      exit(1);
    }
  }

  //{ Setup, 12 }

  /* set the PC to starting position */
  /* 0x3000 is the default */
  enum { PC_START = 0x3000 };
  reg[R_PC] = PC_START;

  int running = 1;
  while (running) {
    /* FETCH */
    uint16_t instr = mem_read(reg[R_PC]++);
    uint16_t op = instr >> 12;

    switch (op) {
      case OP_ADD: { // ADD, 6
        /* destination register (DR) */
        uint16_t r0 = (instr >> 9) & 0x7;
        /* first operand (SR1) */
        uint16_t r1 = (instr >> 6) & 0x7;
        /* whether we are in immediate mode */
        uint16_t imm_flag = (instr >> 5) & 0x1;

        if (imm_flag) {
          uint16_t imm5 = sign_extend(instr & 0x1F, 5);
          reg[r0] = reg[r1] + imm5;
        } 
        else {
          uint16_t r2 = instr & 0x7;
          reg[r0] = reg[r1] + reg[r2];
        }

        update_flags(r0);
      } break;

      case OP_AND: { // AND, 7
        /* destination register (DR) */
        uint16_t r0 = (instr >> 9) & 0x7;
        /* first operand (SR1) */
        uint16_t r1 = (instr >> 6) & 0x7;
        /* whether we are in immediate mode */
        uint16_t imm_flag = (instr >> 5) & 0x1;
        
        if(imm_flag){
          uint16_t imm5 = sign_extend(instr & 0x1F, 5);
          reg[r0] = reg[r1] & imm5;
        }
        else{
          uint16_t r2 = instr & 0x7;
          reg[r0] = reg[r1] & reg[r2];
        }
        update_flags(r0);
      } break;

      case OP_NOT: { // NOT, 7
        /* destination register (DR) */
        uint16_t r0 = (instr >> 9) & 0x7;
        /* source register (SR) */
        uint16_t r1 = (instr >> 6) & 0x7;

        reg[r0] = ~reg[r1]; 
        update_flags(r0);
      } break;

      case OP_BR: { // BR, 7
        uint16_t cond = (instr >> 9) & 0x7;
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

        if(cond && reg[R_COND]){
          reg[R_PC] += pc_offset;
        }
      } break;

      case OP_JMP: { // JMP, 7 
        /* Jump && Return from Subroutine*/
        /* Handles RET when base_r is 7(111) */
        uint16_t base_r = (instr >> 6) & 0x7;
        reg[R_PC] = reg[base_r];
      } break;

      case OP_JSR: { // JSR, 7
        /* Jump to Subroutine */
        reg[R_R7] = reg[R_PC];
        uint16_t flag = (instr >> 11) & 0x1;
        if(flag){
          uint16_t pc_offset = sign_extend(instr & 0x7FF, 10);
          reg[R_PC] += pc_offset; /* JSR */
        }
        else{
          uint16_t base_r = (instr >> 6) & 0x7;
          reg[R_PC] = reg[base_r]; /* JSRR */
        }
        break;
      } break;

      case OP_LD: { // LD, 7
        /*Load  */
        uint16_t r0 = (instr >> 9) & 0x7; /* destination register (DR) */
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
        reg[r0] = mem_read(reg[R_PC] + pc_offset);
        update_flags(r0);
      } break;

      case OP_LDI: { // LDI, 6
        /* destination register (DR) */
        uint16_t r0 = (instr >> 9) & 0x7;
        /* PCoffset 9*/
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
        /* add pc_offset to the current PC, look at that memory location to get the final address */
        reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
        update_flags(r0);
      } break;

      case OP_LDR: { // LDR, 7
        /* Load Base + offset */
        uint16_t r0 = (instr >> 9) & 0x7; /* destination register (DR) */
        uint16_t base_r = (instr >> 6) & 0x7;
        uint16_t offset = sign_extend(instr && 0x3F, 6);

        reg[r0] = mem_read(reg[base_r] + offset);
        update_flags(r0);
      } break;

      case OP_LEA: { // LEA, 7
      } break;

      case OP_ST: { // ST, 7
      } break;

      case OP_STI: { // STI, 7
      } break;

      case OP_STR: { // STR, 7
      } break;

      case OP_TRAP: { // TRAP, 8
      } break;

      case OP_RES:
      case OP_RTI:
      default: {
        // BAD OPCODE, 7
        abort();
      } break;

    }
  }
  // { Shutdown, 12 }
}