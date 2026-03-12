/*
 * vm.c — Tiny bytecode interpreter for dynamic handlers
 * Stack-based VM. Stack depth: 32. No malloc.
 */
#include "vm.h"
#include "pool.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define STACK_DEPTH 32
#define STR_MAX     256

/* Stack value types */
typedef enum { VAL_INT = 0, VAL_STR = 1 } ValType;

typedef struct {
    ValType type;
    union {
        int32_t  i;
        char     s[STR_MAX];
    } v;
} StackVal;

/* ------------------------------------------------------------------ */
/* Internal stack helpers                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    StackVal vals[STACK_DEPTH];
    int      top; /* index of next free slot */
} VMStack;

static void stack_init(VMStack *st) { st->top = 0; }

static int stack_push_int(VMStack *st, int32_t i) {
    if (st->top >= STACK_DEPTH) return -1;
    st->vals[st->top].type  = VAL_INT;
    st->vals[st->top].v.i   = i;
    st->top++;
    return 0;
}

static int stack_push_str(VMStack *st, const char *s) {
    if (st->top >= STACK_DEPTH) return -1;
    st->vals[st->top].type = VAL_STR;
    strncpy(st->vals[st->top].v.s, s, STR_MAX - 1);
    st->vals[st->top].v.s[STR_MAX - 1] = '\0';
    st->top++;
    return 0;
}

static StackVal *stack_pop(VMStack *st) {
    if (st->top <= 0) return NULL;
    return &st->vals[--st->top];
}

static StackVal *stack_peek(VMStack *st) {
    if (st->top <= 0) return NULL;
    return &st->vals[st->top - 1];
}

/* ------------------------------------------------------------------ */
/* Query string helper                                                 */
/* ------------------------------------------------------------------ */

/* Extract param 'key' from query string "a=1&b=2" into out (max STR_MAX). */
static int get_query_param(const char *query, const char *key, char *out) {
    if (!query || !key) { out[0] = '\0'; return 0; }
    size_t klen = strlen(key);
    const char *p = query;
    while (*p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            int i = 0;
            while (*p && *p != '&' && i < STR_MAX - 1) {
                out[i++] = *p++;
            }
            out[i] = '\0';
            return 1;
        }
        /* skip to next '&' */
        while (*p && *p != '&') p++;
        if (*p == '&') p++;
    }
    out[0] = '\0';
    return 0;
}

/* ------------------------------------------------------------------ */
/* Bytecode layout                                                     */
/* ------------------------------------------------------------------ */
/*
 * Each instruction: [1 byte opcode] [optional operands]
 *
 * OP_PUSH_STR  : [u16 offset] [u16 len] followed by inline string
 * OP_PUSH_INT  : [i32 value]
 * OP_LOAD_REQ  : [u8 field_id]   0=method 1=path 2=query 3=body
 * OP_STORE_RES : [u8 field_id]   0=status 1=body 2=content_type 3=redirect
 * OP_JMP       : [i16 offset]    relative to current PC after reading operand
 * OP_JMP_IF    : [i16 offset]
 * OP_GETPARAM  : [u8 namelen][name bytes]
 * OP_RESPOND   : [u16 status] — pops body from stack
 * Others       : no operands
 */

#define READ_U8(bc, pc)  ((bc)[(pc)++])
#define READ_U16(bc, pc) (((uint16_t)(bc)[(pc)] | ((uint16_t)(bc)[(pc)+1] << 8))); (pc) += 2
#define READ_I16(bc, pc) ((int16_t)READ_U16(bc, pc))
#define READ_I32(bc, pc) (((int32_t)(bc)[(pc)] \
                         | ((int32_t)(bc)[(pc)+1] <<  8) \
                         | ((int32_t)(bc)[(pc)+2] << 16) \
                         | ((int32_t)(bc)[(pc)+3] << 24))); (pc) += 4

/* ------------------------------------------------------------------ */
/* Main interpreter                                                    */
/* ------------------------------------------------------------------ */

VMResult vm_run(const uint8_t *bytecode, uint32_t blen,
                VMRequest *req, VMResponse *res) {
    VMStack st;
    stack_init(&st);

    memset(res, 0, sizeof(*res));
    res->status = 200;
    strncpy(res->content_type, "text/html; charset=utf-8", 64);

    uint32_t pc = 0;

    while (pc < blen) {
        uint8_t op = READ_U8(bytecode, pc);

        switch ((Opcode)op) {

        case OP_HALT:
            return VM_OK;

        case OP_PUSH_INT: {
            int32_t val;
            if (pc + 4 > blen) return VM_ERR_OOB;
            val = (int32_t)bytecode[pc]
                | ((int32_t)bytecode[pc+1] <<  8)
                | ((int32_t)bytecode[pc+2] << 16)
                | ((int32_t)bytecode[pc+3] << 24);
            pc += 4;
            stack_push_int(&st, val);
            break;
        }

        case OP_PUSH_STR: {
            /* u16 length, then raw bytes */
            if (pc + 2 > blen) return VM_ERR_OOB;
            uint16_t slen = (uint16_t)bytecode[pc] | ((uint16_t)bytecode[pc+1] << 8);
            pc += 2;
            if (pc + slen > blen) return VM_ERR_OOB;
            char tmp[STR_MAX];
            uint16_t copy = slen < STR_MAX - 1 ? slen : STR_MAX - 1;
            memcpy(tmp, bytecode + pc, copy);
            tmp[copy] = '\0';
            pc += slen;
            stack_push_str(&st, tmp);
            break;
        }

        case OP_LOAD_REQ: {
            if (pc + 1 > blen) return VM_ERR_OOB;
            uint8_t field = READ_U8(bytecode, pc);
            const char *val = NULL;
            switch (field) {
                case 0: val = req->method ? req->method : ""; break;
                case 1: val = req->path   ? req->path   : ""; break;
                case 2: val = req->query  ? req->query  : ""; break;
                case 3: val = req->body   ? req->body   : ""; break;
                default: val = ""; break;
            }
            stack_push_str(&st, val);
            break;
        }

        case OP_STORE_RES: {
            if (pc + 1 > blen) return VM_ERR_OOB;
            uint8_t field = READ_U8(bytecode, pc);
            StackVal *top = stack_pop(&st);
            if (!top) break;
            switch (field) {
                case 0: /* status */
                    res->status = (uint16_t)(top->type == VAL_INT
                                             ? top->v.i : 200);
                    break;
                case 1: /* body */
                    if (top->type == VAL_STR) {
                        strncpy(res->body, top->v.s, sizeof(res->body) - 1);
                        res->body_len = (uint32_t)strlen(res->body);
                    }
                    break;
                case 2: /* content_type */
                    if (top->type == VAL_STR)
                        strncpy(res->content_type, top->v.s,
                                sizeof(res->content_type) - 1);
                    break;
                case 3: /* redirect */
                    if (top->type == VAL_STR)
                        strncpy(res->redirect_to, top->v.s,
                                sizeof(res->redirect_to) - 1);
                    break;
            }
            break;
        }

        case OP_ADD: {
            StackVal *b_val = stack_pop(&st);
            StackVal *a_val = stack_pop(&st);
            if (!a_val || !b_val) return VM_ERR_STACK;
            if (a_val->type == VAL_INT && b_val->type == VAL_INT) {
                stack_push_int(&st, a_val->v.i + b_val->v.i);
            } else {
                /* string concatenation */
                char tmp[STR_MAX];
                const char *sa = (a_val->type == VAL_STR) ? a_val->v.s : "";
                const char *sb = (b_val->type == VAL_STR) ? b_val->v.s : "";
                snprintf(tmp, STR_MAX, "%s%s", sa, sb);
                stack_push_str(&st, tmp);
            }
            break;
        }

        case OP_EQ: {
            StackVal *b_val = stack_pop(&st);
            StackVal *a_val = stack_pop(&st);
            if (!a_val || !b_val) return VM_ERR_STACK;
            int eq = 0;
            if (a_val->type == VAL_INT && b_val->type == VAL_INT)
                eq = (a_val->v.i == b_val->v.i);
            else if (a_val->type == VAL_STR && b_val->type == VAL_STR)
                eq = (strcmp(a_val->v.s, b_val->v.s) == 0);
            stack_push_int(&st, eq);
            break;
        }

        case OP_JMP: {
            if (pc + 2 > blen) return VM_ERR_OOB;
            int16_t offset = (int16_t)((uint16_t)bytecode[pc]
                                      | ((uint16_t)bytecode[pc+1] << 8));
            pc += 2;
            pc = (uint32_t)((int32_t)pc + offset);
            break;
        }

        case OP_JMP_IF: {
            if (pc + 2 > blen) return VM_ERR_OOB;
            int16_t offset = (int16_t)((uint16_t)bytecode[pc]
                                      | ((uint16_t)bytecode[pc+1] << 8));
            pc += 2;
            StackVal *cond = stack_pop(&st);
            int truth = cond && ((cond->type == VAL_INT && cond->v.i != 0)
                              || (cond->type == VAL_STR && cond->v.s[0] != '\0'));
            if (truth) {
                pc = (uint32_t)((int32_t)pc + offset);
            }
            break;
        }

        case OP_SEND:
            return VM_OK;

        case OP_REDIRECT: {
            StackVal *top = stack_pop(&st);
            if (top && top->type == VAL_STR) {
                strncpy(res->redirect_to, top->v.s, sizeof(res->redirect_to) - 1);
                res->status = 302;
            }
            return VM_OK;
        }

        case OP_GETPARAM: {
            if (pc + 1 > blen) return VM_ERR_OOB;
            uint8_t namelen = READ_U8(bytecode, pc);
            if (pc + namelen > blen) return VM_ERR_OOB;
            char key[64];
            uint8_t copy = namelen < 63 ? namelen : 63;
            memcpy(key, bytecode + pc, copy);
            key[copy] = '\0';
            pc += namelen;
            char out[STR_MAX];
            get_query_param(req->query, key, out);
            stack_push_str(&st, out);
            break;
        }

        case OP_RESPOND: {
            if (pc + 2 > blen) return VM_ERR_OOB;
            res->status = (uint16_t)bytecode[pc] | ((uint16_t)bytecode[pc+1] << 8);
            pc += 2;
            StackVal *top = stack_pop(&st);
            if (top && top->type == VAL_STR) {
                strncpy(res->body, top->v.s, sizeof(res->body) - 1);
                res->body_len = (uint32_t)strlen(res->body);
            }
            return VM_OK;
        }

        default:
            fprintf(stderr, "webzero: unknown opcode 0x%02X at pc=%u\n", op, pc - 1);
            return VM_ERR_HALT;
        }
    }

    return VM_OK;
}
