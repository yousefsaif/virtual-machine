#include <signal.h>   // SIGINT
#include <stdint.h>  // uint16_t
#include <stdio.h>  // FILE
#include <stdlib.h>
#include <string.h>
/* unix */
#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/types.h>

/* ----------------------------------------------------------------------------------------------------------------------------------------------------------------- */

/* Registers --- register stores single value on cpu*/
enum {
  R_R0 = 0, /*R0 to R7 general purpose registers*/
  R_R1,     /* setting r0=0, makes r1=1, r2=2 and so on */
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

/* memory mapped registers */
enum {
  MR_KBSR = 0xFE00, /* keyboard status  register*/
  MR_KBDR = 0xFE02  /* keyboard data    register*/
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

/* Trap Codes */
enum {
  TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
  TRAP_OUT = 0x21,   /* output a character */
  TRAP_PUTS = 0x22,  /* output a word string */
  TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
  TRAP_PUTSP = 0x24, /* output a byte string */
  TRAP_HALT = 0x25   /* halt the program */
};

/* Memory - 65536 locations */
uint16_t memory[UINT16_MAX];

/* Register Storage - Store registers in an array */
uint16_t reg[R_COUNT];



/* -----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
/* Helper Functions */

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

//switch from little-endian to big-endian (as LC3 is big-endian, and (most)modern computers are little-endian)
uint16_t swap16(uint16_t x) { 
  return (x << 8) | (x >> 8); 
}

void read_image_file(FILE *file) {
  /* the origin tells us where in memory to place the image */
  uint16_t origin;
  fread(&origin, sizeof(origin), 1, file);
  origin = swap16(origin);

  /* we know the maximum file size so we only need one fread */
  uint16_t max_read = UINT16_MAX - origin;
  uint16_t *p = memory + origin;
  size_t read = fread(p, sizeof(uint16_t), max_read, file);

  /* swap to little endian */
  while (read-- > 0) {
    *p = swap16(*p);
    ++p;
  }
}

int read_image(const char *image_path) {
  FILE *file = fopen(image_path, "rb");
  if (!file) {
    return 0;
  };
  read_image_file(file);
  fclose(file);
  return 1;
}

uint16_t check_key() {
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

/* Memory Access */
void mem_write(uint16_t address, uint16_t val) {
   memory[address] = val; 
}

uint16_t mem_read(uint16_t address) {
  if (address == MR_KBSR) {
    if (check_key()) {
      memory[MR_KBSR] = (1 << 15);
      memory[MR_KBDR] = getchar();
    } else {
      memory[MR_KBSR] = 0;
    }
  }
  return memory[address];
}

/* Input Buffering */
struct termios original_tio;

void disable_input_buffering() {
  tcgetattr(STDIN_FILENO, &original_tio);
  struct termios new_tio = original_tio;
  new_tio.c_lflag &= ~ICANON & ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering() {
  tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

/* Handle Interrupt  */
void handle_interrupt(int signal) {
  restore_input_buffering();
  printf("\n");
  exit(-2);
}



/* -----------------------------------------------------------------------------------------------------------------------------------------------------------------*/

int main(int argc, const char *argv[]) {

  /* Load Arguments */
  if (argc < 2) { 
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

  /* Setup */
  signal(SIGINT, handle_interrupt);
  disable_input_buffering();

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
      case OP_ADD: {
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
      } 
      break;

      case OP_AND: { 
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
      } 
      break;

      case OP_NOT: {
        /* destination register (DR) */
        uint16_t r0 = (instr >> 9) & 0x7;
        /* source register (SR) */
        uint16_t r1 = (instr >> 6) & 0x7;

        reg[r0] = ~reg[r1]; 
        update_flags(r0);
      } 
      break;

      case OP_BR: {
        uint16_t cond = (instr >> 9) & 0x7;
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

        if(cond & reg[R_COND]){
          reg[R_PC] += pc_offset;
        }
      } 
      break;

      case OP_JMP: { 
        /* Jump && Return from Subroutine*/
        /* Handles RET when base_r is 7(111) */
        uint16_t base_r = (instr >> 6) & 0x7;
        reg[R_PC] = reg[base_r];
      } 
      break;

      case OP_JSR: {
        /* Jump to Subroutine */
        reg[R_R7] = reg[R_PC];
        uint16_t flag = (instr >> 11) & 0x1;
        if(flag){
          uint16_t pc_offset = sign_extend(instr & 0x7FF, 11);
          reg[R_PC] += pc_offset; /* JSR */
        }
        else{
          uint16_t base_r = (instr >> 6) & 0x7;
          reg[R_PC] = reg[base_r]; /* JSRR */
        }
        break;
      } 
      break;

      case OP_LD: {
        /*Load  */
        uint16_t r0 = (instr >> 9) & 0x7; /* destination register (DR) */
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
        reg[r0] = mem_read(reg[R_PC] + pc_offset);
        update_flags(r0);
      } 
      break;

      case OP_LDI: {
        /* destination register (DR) */
        uint16_t r0 = (instr >> 9) & 0x7;
        /* PCoffset 9*/
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
        /* add pc_offset to the current PC, look at that memory location to get the final address */
        reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
        update_flags(r0);
      } 
      break;

      case OP_LDR: {
        /* Load Base + offset */
        uint16_t r0 = (instr >> 9) & 0x7; /* destination register (DR) */
        uint16_t base_r = (instr >> 6) & 0x7;
        uint16_t offset = sign_extend(instr & 0x3F, 6);

        reg[r0] = mem_read(reg[base_r] + offset);
        update_flags(r0);
      } 
      break;

      case OP_LEA: {
        /* Load Effective Address */
        uint16_t r0 = (instr >> 9) & 0x7; /* destination register (DR) */
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

        reg[r0] = reg[R_PC] + pc_offset;
        update_flags(r0);
      } 
      break;

      case OP_ST: { //Store
        uint16_t r0 = (instr >> 9) & 0x7; /* Source register (SR) */
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
        mem_write(reg[R_PC] + pc_offset, reg[r0]);
      } 
      break;

      case OP_STI: { //Store Indirect
        uint16_t r0 = (instr >> 9) & 0x7; /* Source register (SR) */
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

        mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
      } 
      break;

      case OP_STR: { //Store Base+offset
        uint16_t r0 = (instr >> 9) & 0x7; /* Source register (SR) */
        uint16_t base_r = (instr >> 6) & 0x7;
        uint16_t offset = sign_extend(instr & 0x3F, 6);

        mem_write(reg[base_r] + offset, reg[r0]);
      } 
      break;

      case OP_TRAP: {
        switch (instr & 0xFF) {
          case TRAP_GETC: 
            reg[R_R0] = (uint16_t) getchar();
            break;
          case TRAP_OUT: {
            putc((char)reg[R_R0], stdout);
            fflush(stdout);
          } 
          break;

          case TRAP_PUTS: {
            /* one char per word */
            uint16_t *c = memory + reg[R_R0];
            while (*c) {
              putc((char)*c, stdout);
              ++c;
            }
            fflush(stdout);
          } 
          break;

          case TRAP_IN: {
            printf("Input a character\n");
            char c = getchar();
            putc(c, stdout);
            reg[R_R0] = (uint16_t) c;
          } 
          break;

          case TRAP_PUTSP: {
            /* two chars per word */
            uint16_t *c = memory + reg[R_R0];
            while (*c) {
              putc((char)*c && 0XFF, stdout);
              if(*c >> 8){
                putc(*c >> 8, stdout);
              }
              ++c;
            }
            fflush(stdout);
          } 
          break;
          
          case TRAP_HALT:
            printf("Halting\n");
            running = 0;
            break;
        } break;
      } break; //Trap end

      case OP_RES:
      case OP_RTI:
      default:
        // BAD OPCODE
        abort();
        break;

    } //switch end 
  } //while end 
  
  /* Shutdown */
  restore_input_buffering();
}