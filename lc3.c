#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
/* unix */
#include <unistd.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>


enum
{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC, /* program counter */
    R_COND,
    R_COUNT
};

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
enum {
    FL_POS = 1 << 0,
    FL_ZRO = 1 << 1,
    FL_NEG = 1 << 2,
};
enum {
    TRAP_GETC = 0x20,
    TRAP_OUT = 0x21,
    TRAP_PUTS = 0x22,
    TRAP_IN = 0x23,
    TRAP_PUTSP = 0x24,
    TRAP_HALT = 0x25,
    
};
enum
{
    MR_KBSR = 0xFE00, /* keyboard status */
    MR_KBDR = 0xFE02  /* keyboard data */
};
uint16_t memory[UINT16_MAX];
uint16_t reg[R_COUNT];
uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 1) {
        x |= (0xFFFF << bit_count);
    }
    return x;
}

uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}
void update_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15) /* a 1 in the left-most bit indicates negative */
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}

void read_image_file(FILE* file) {
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);
    uint16_t maxread = UINT16_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), maxread, file);
    while (read-- > 0) {
        *p = swap16(*p);
        p++;
    }
}
int read_image(const char* image_path)
{
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0; };
    read_image_file(file);
    fclose(file);
    return 1;
}
uint16_t check_key()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}
void mem_write(uint16_t address, uint16_t value) {
    memory[address] = value;
}
uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else
        {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}
struct termios original_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}
void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}
int main(int argc, const char* argv[]) {
    if (argc < 2) {
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }
    for (int i = 0; i < argc; i++) {
        if (!read_image(argv[i])) {
            printf("failed to load image: %s\n", argv[i]);
            exit(1);
        }
    }
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    enum { PC_START = 0x3000, };
    reg[R_PC] = PC_START;
    int running = 1;
    while (running) {
        uint16_t instr = mem_read(reg[R_PC]);
        uint16_t op = instr >> 12;

        switch (op) {
            case OP_ADD:
                {uint16_t DR = (instr >> 9) & 7;
                uint16_t SR1 = (instr >> 6) & 7;                
                uint16_t addtype = (instr >> 5) & 1;
                if (addtype) {
                    uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                    reg[DR] = reg[SR1] + imm5;
                }                
                else {
                    uint16_t SR2 = instr & 7;
                    reg[DR] = reg[SR1] + reg[SR2];
                }
                update_flags(DR);}
                break;
            case OP_AND: 
                {
                    uint16_t DR = (instr >> 9) & 7;
                    uint16_t SR1 = (instr >> 6) & 7;                
                    uint16_t addtype = (instr >> 5) & 1;
                    if (addtype) {
                        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                        reg[DR] = reg[SR1] & imm5;
                    }                
                    else {
                        uint16_t SR2 = instr & 7;
                        reg[DR] = reg[SR1] & reg[SR2];
                    }
                    update_flags(DR);
                }
                break;    
            case OP_NOT:
                {uint16_t DR = (instr >> 9) & 7;
                uint16_t SR1 = (instr >> 6) & 7;
                reg[DR] = ~reg[SR1];}
                break;
            case OP_BR:
                {uint16_t cond_flag = (instr >> 9) & 0x7;
                uint16_t offset = sign_extend(instr & 0x1FF, 9);
                if (reg[cond_flag] & cond_flag) {
                    reg[R_PC] = reg[R_PC] + offset;
                }}
                break;
            case OP_JMP:
                {uint16_t BR = (instr >> 6) & 7;
                reg[R_PC] = reg[BR];}
                break;
            case OP_JSR:
                {uint16_t jtype = (instr >> 11) & 1;
                if (jtype) {
                    uint16_t offset = sign_extend(instr & 0x7FF, 11);
                    reg[R_PC] = reg[R_PC] + offset;
                }
                else {
                    uint16_t SR1 = (instr >> 6) & 7;
                    reg[R_PC] = reg[SR1];
                }}
                break;
            case OP_LD:
                {uint16_t DR = (instr >> 9) & 7;
                uint16_t offset = sign_extend(instr & 0x1FF, 9);
                reg[DR] = mem_read(reg[R_PC] + offset);}
                break;
            case OP_LDI:
                {uint16_t DR = (instr >> 9) & 7;
                uint16_t offset = sign_extend(instr & 0x1FF, 9);
                reg[DR] = mem_read(mem_read(reg[R_PC] + offset));
                update_flags(DR);}
                break;
            case OP_LDR:
                {uint16_t DR = (instr >> 9) & 7;
                uint16_t SR1 = (instr >> 6) & 7;
                uint16_t offset = sign_extend(instr & 0x3F, 6);
                reg[DR] = mem_read(reg[SR1] + offset);}
                break;
            case OP_LEA:
                {uint16_t DR = (instr >> 9) & 7;
                uint16_t offset = sign_extend(instr & 0x1FF, 9);
                reg[DR] = reg[R_PC] + offset;
                update_flags(DR);}
                break;
            case OP_ST:
                {uint16_t DR = (instr >> 9) & 7;
                uint16_t offset = sign_extend(instr & 0x1FF, 9);
                mem_write(reg[R_PC] + offset, reg[DR]);}
                break;
            case OP_STI:
                {uint16_t DR = (instr >> 9) & 7;
                uint16_t offset = sign_extend(instr & 0x1FF, 9);
                mem_write(mem_read(reg[R_PC] + offset), reg[DR]);}
                break;
            case OP_STR:
                {uint16_t DR = (instr >> 9) & 7;
                uint16_t SR = (instr >> 6) & 7;
                uint16_t offset = sign_extend(instr & 0x3F, 6);
                mem_write(reg[SR] + offset, reg[DR]);}
                break;
            case OP_TRAP:
                switch(instr & 0xFF) {
                    case TRAP_GETC:
                        {reg[R_R0] = (uint16_t) getchar();}
                        break;
                    case TRAP_OUT:
                        {putc((char)reg[R_R0], stdout);}
                        break;
                    case TRAP_PUTS:
                        {uint16_t *c = memory + reg[R_R0];
                        while (*c) {
                            putc((char)*c, stdout);
                            c++;
                        }
                        fflush(stdout);}
                        break;
                    case TRAP_IN:
                        {printf("Output a character: ");
                        reg[R_R0] = (uint16_t) getchar();
                        putc((char)reg[R_R0], stdout);}
                        break;
                    case TRAP_PUTSP:
                        {uint16_t *c = memory + reg[R_R0];
                        while (*c) {
                            char a = *c & 0xFF;
                            char b = *c >> 8;
                            putc(a, stdout);
                            putc(b, stdout);
                            c++;
                        }
                        fflush(stdout);}
                        break;
                    case TRAP_HALT:
                        {puts("HALT");
                        fflush(stdout);
                        running = 0;}
                        break;
                }
                break;
            case OP_RES:
            case OP_RTI:
            default:
                abort();
                break;
        }
    }
    restore_input_buffering();
}