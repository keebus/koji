/*
* koji language. 2015 Canio Massimo Tristano
* Read LICENSE.txt for information about this software copyright and license.
*/


#include "koji.h"
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#ifdef _MSC_VER
#define inline __forceinline
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmultichar"
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wmissing-braces"
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmultichar"
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wmissing-braces"
#endif

//-------------------------------------------------------------------------------------------------
#pragma region Support

/* global defines */
#define KJ_TRUE 1
#define KJ_FALSE 0
#define KJ_NULL 0

/* convenience typedefs */
typedef unsigned int uint;
typedef unsigned char ubyte;

/* Dynamic Array */

/* Generates the declaration for a dynamic array of type @type */
#define array_type(type) struct { uint capacity; uint size; type* data; }

/* Generic definition of a dynamic array that points to void* */
typedef array_type(void) void_array;

/**
  * Initializes an array instance to a valid empty array.
	*/
static inline void array_init(void* array_)
{
	void_array* array = array_;
	array->capacity = 0;
	array->size = 0;
	array->data = KJ_NULL;
}

/*
* Makes sure that specified karray at @array_ of elements of size @element_size has enough capacity
* to store @new_capacity elements.
*/
static koji_bool array_reserve(void *array_, uint element_size, uint new_capacity)
{
	void_array *array = array_;

	/* Not enough capacity? */
	if (new_capacity > array->capacity)
	{
		/* Calculate and set new capacity as the smallest large enough multiple of 10 */
		array->capacity = (1 + ((new_capacity - 1) / 10)) * 10;

		/* Allocate new buffer and copy old values over */
		void *new_data = malloc(array->capacity * element_size);
		memcpy(new_data, array->data, array->size * element_size);

		/* free old buffer and set the new buffer to the array */
		free(array->data);
		array->data = new_data;

		return 1;
	}
	else
	{
		return 0;
	}
}

/**
* Resizes specified dynamic array @array_ containing elements of size @element_size to a new
* number of elements @new_size. If the array has enough capacity to contain @new_size elements
* then the function simply returns KJ_FALSE. If the array instead is too small, the function
* allocates a new buffer large enough to contain @new_size, copies the old buffer to the new one,
* destroys the old buffer and returns KJ_TRUE.
*/
static inline koji_bool array_resize(void *array_, uint element_size, uint new_size)
{
	void_array *array = array_;
	koji_bool result = array_reserve(array_, element_size, new_size);
	array->size = new_size;
	return result;
}

/**
* Destroys the buffer contained in the array_t pointed by @array_.
*/
static inline void array_destroy(void *array_)
{
	void_array *array = array_;
	free(array->data);
	array->capacity = 0;
	array->size = 0;
	array->data = NULL;
}

/**
* (internal) Resizes @array_ to contain its current size plus @num_elements.
*  @returns a pointer to the first of new elements.
*/
static inline void* _array_push(void *array_, uint element_size, uint num_elements)
{
	void_array *array = array_;
	uint prev_size = array->size;
	array_resize(array, element_size, array->size + num_elements);
	return ((char*)array->data) + prev_size * element_size;
}

/**
* Expands @array contain @n new elements of specified @type and returns a pointer to the first
* new element.
*/
#define array_push(array, type, n) ((type*)_array_push(array, sizeof(type), n))

/**
* Pushes @value of type @type to the back of @array.
*/
#define array_push_value(array, type, value) *(type*)_array_push(array, sizeof(type), 1) = value

#pragma endregion

//-------------------------------------------------------------------------------------------------
#pragma region Diagnostics

/**
* Describes a specific location as in line and column in a specific source file.
*/
typedef struct kj_source_location
{
	const char *filename;
	uint line;
	uint column;
}
kj_source_location;

/**
* Type of the function set by the user that will handle any compilation or runtime error.
*/
typedef void(*kj_error_report_fn)(void *user_data, kj_source_location, const char *message);

static void default_error_report_fn(void* user_data, kj_source_location sl, const char *message)
{
	(void)user_data;
	(void)sl;
	printf(message);
}

/**
* Groups info about the error handler used during compilation and execution.
*/
typedef struct kj_error_handler
{
	void *user_data;
	kj_error_report_fn reporter;
	jmp_buf jmpbuf;
}
kj_error_handler;

/**
* Reports an issue at source location @sl with printf-like @format and arguments @args using
* specified handler @e.
*/
static void reportv(kj_error_handler *e, kj_source_location sl, const char *format, va_list args)
{
	static const char *header_format = "at '%s' (%d:%d): ";
	uint header_length = snprintf(NULL, 0, header_format, sl.filename, sl.line, sl.column);
	uint body_length = vsnprintf(NULL, 0, format, args);
	char *message = alloca(header_length + body_length + 1);
	snprintf(message, header_length + 1, header_format, sl.filename, sl.line, sl.column);
	vsnprintf(message + header_length, header_length + body_length + 1, format, args);
	e->reporter(e->user_data, sl, message);
}

/**
* Reports an issue at source location @sl with printf-like @format and variadic arguments using
* specified handler @e.
*/
static void report(kj_error_handler *e, kj_source_location sl, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	reportv(e, sl, format, args);
	va_end(args);
}

/**
* Reports the specified error message and jumps to the error handler code.
*/
static void compile_error(kj_error_handler *e, kj_source_location sl, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	reportv(e, sl, format, args);
	va_end(args);
	longjmp(e->jmpbuf, 0);
}

#pragma endregion

//-------------------------------------------------------------------------------------------------
#pragma region Lexer

/**
* Value rapresenting an enumerated token value (e.g. kw_while) or a valid sequence of characters
* (e.g. '>=').
*/
typedef int kj_token;

enum
{
	/* tokens */
	tok_eos = -127,
	tok_integer,
	tok_real,
	tok_string,
	tok_identifier,

	/* keywords */
	kw_def,
	kw_do,
	kw_else,
	kw_false,
	kw_for,
	kw_if,
	kw_in,
	kw_return,
	kw_true,
	kw_var,
	kw_while,
};

/**
* A lexer scans a stream using a provided stream_reader matching tokens recognized by the
* language such as the "if" keyword, or a string, or an identifier. The parser then verifies that
* the sequence of tokens in an input source file scanned by the lexer is valid and generates the
* appropriate output bytecode.
* This lexer is implemented as a basic state machine that implements the language tokens regular
* expression.
*/
typedef struct kj_lexer
{
	kj_error_handler *error_handler;
	void *stream_data;
	koji_stream_reader_fn stream;
	kj_source_location source_location;
	char curr_char;
	kj_token lookahead;
	koji_bool newline;
	array_type(char) token_string;
	union
	{
		koji_integer token_int;
		koji_real    token_real;
	};
}
kj_lexer;

static char _lex_skip(kj_lexer *l)
{
	if (l->curr_char == '\n')
	{
		++l->source_location.line;
		l->source_location.column = 0;
	}
	++l->source_location.column;
	l->curr_char = l->stream(l->stream_data);
	return l->curr_char;
}

static inline char _lex_push(kj_lexer *l)
{
	array_reserve(&l->token_string, sizeof(char), l->token_string.size + 1);
	l->token_string.data[l->token_string.size - 1] = l->curr_char;
	l->token_string.data[l->token_string.size++] = '\0';
	return _lex_skip(l);
}

static inline void _lex_clear_token_string(kj_lexer *l)
{
	l->token_string.size = 1;
	l->token_string.data[0] = '\0';
}

static inline int _lex_accept(kj_lexer *l, const char *str)
{
	while (l->curr_char == *str)
	{
		_lex_push(l);
		++str;
	}
	return *str == 0;
}

static inline int _lex_accept_char(kj_lexer *l, char ch)
{
	if (l->curr_char == ch)
	{
		_lex_push(l);
		return KJ_TRUE;
	}
	return KJ_FALSE;
}

/**
* @returns whether the next char is a valid name char.
*/
static inline koji_bool _lex_is_identifier_char(char ch, koji_bool first_char)
{
	return (ch >= 'A' && ch <= 'Z') ||
		(ch >= 'a' && ch <= 'z') ||
		(ch == '_') ||
		(!first_char && ch >= '0' && ch <= '9');
}

/**
* Scans the input for an name. \p first_char specifies whether this is the first name
* character read. It sets l->lookahead to the tok_identifier if some name was read and returns it.
*/
static inline kj_token _lex_scan_identifier(kj_lexer *l, koji_bool first_char)
{
	while (_lex_is_identifier_char(l->curr_char, first_char))
	{
		_lex_push(l);
		l->lookahead = tok_identifier;
		first_char = KJ_FALSE;
	}
	return l->lookahead;
}

/**
* Converts specified token @tok into its equivalent string and writs the reslt in @buffer of size
* @buffer_size.
*/
static const char *lex_token_to_string(kj_token tok, char *buffer, uint buffer_size)
{
	switch (tok)
	{
		case tok_eos: return "end-of-stream";
		case tok_integer: return "integer";
		case tok_real: return "float";
		case tok_string: return "string";
		case tok_identifier: return "identifier";
		case kw_def: return "def";
		case kw_do: return "do";
		case kw_else: return "else";
		case kw_false: return "false";
		case kw_for: return "for";
		case kw_if: return "if";
		case kw_in: return "in";
		case kw_return: return "return";
		case kw_true: return "true";
		case kw_var: return "var";
		case kw_while: return "while";
		default:
			snprintf(buffer, buffer_size, "'%s'", (const char*)&tok);
			return buffer;
	}
}

/*
* @returns a readable string for current lookahead (e.g. it returns "end-of-stream" for tok_eos)
*/
static const char *lex_lookahead_to_string(kj_lexer *l)
{
	if (l->lookahead == tok_eos)
		return "end-of-stream";
	else
		return l->token_string.data;
}

/**
* Scans the next token in the source stream and returns its type.
*/
static kj_token lex(kj_lexer *l)
{
	l->lookahead = tok_eos;
	_lex_clear_token_string(l);

	for (;;)
	{
		switch (l->curr_char)
		{
			case EOF:
				return tok_eos;

			case '\n':
				l->newline = KJ_TRUE;

			case ' ': case '\r': case '\t':
				_lex_skip(l);
				break;

			case ',': case ';': case ':': case '(': case ')': case '[': case ']': case '{': case '}':
				l->lookahead = l->curr_char;
				_lex_push(l);
				return l->lookahead;

			case '.': case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			{
				koji_bool decimal = l->curr_char == '.';
				l->lookahead = decimal ? tok_real : tok_integer;

				if (!decimal)
				{
					// First sequence of numbers before optional dot.
					while (l->curr_char >= '0' && l->curr_char <= '9')
						_lex_push(l);

					if (l->curr_char == '.')
					{
						_lex_push(l);
						decimal = KJ_TRUE;
					}
				}

				if (decimal)
				{
					l->lookahead = tok_real;

					// Scan decimal part
					while (l->curr_char >= '0' && l->curr_char <= '9')
						_lex_push(l);
				}
				else if (l->curr_char == 'e')
				{
					decimal = KJ_TRUE;
					_lex_push(l);
					while (l->curr_char >= '0' && l->curr_char <= '9')
						_lex_push(l);
				}

				if (decimal)
				{
					l->token_real = (koji_real)atof(l->token_string.data);
				}
				else
				{
					char *dummy;
					l->token_int = (koji_integer)strtoll(l->token_string.data, &dummy, 10);
				}

				return l->lookahead;
			}

			case '!':
				_lex_push(l);
				return l->lookahead = (_lex_accept_char(l, '=') ? '!=' : '!');

			case '&':
				_lex_push(l);
				return l->lookahead = (_lex_accept_char(l, '&') ? '&&' : '&');

			case '|':
				_lex_push(l);
				return l->lookahead = (_lex_accept_char(l, '|') ? '||' : '|');

			case '=':
				_lex_push(l);
				return l->lookahead = (_lex_accept_char(l, '=') ? '==' : '=');

			case '<':
				_lex_push(l);
				return l->lookahead = (_lex_accept_char(l, '=') ? '<=' : '<');

			case '>':
				_lex_push(l);
				return l->lookahead = (_lex_accept_char(l, '=') ? '>=' : '>');

			case '+':
				_lex_push(l);
				return l->lookahead = (_lex_accept_char(l, '=') ? '+=' : '+');

			case '-':
				_lex_push(l);
				return l->lookahead = (_lex_accept_char(l, '=') ? '-=' : '-');

			case '*':
				_lex_push(l);
				return l->lookahead = (_lex_accept_char(l, '=') ? '*=' : '*');

			case '/':
				_lex_push(l);
				if (_lex_accept_char(l, '='))
					return l->lookahead = '/=';
				else if (l->curr_char == '/') /* line-comment */
				{
					_lex_skip(l);
					_lex_clear_token_string(l);
					while (l->curr_char != '\n' && l->curr_char != -1)
						_lex_skip(l);
					break;
				}
				// todo add block comment /* */
				return l->lookahead = '/';

				/* keywords */
			case 'd':
				_lex_push(l);
				l->lookahead = tok_identifier;
				switch (l->curr_char)
				{
					case 'e':
						_lex_push(l);
						if (_lex_accept(l, "f")) l->lookahead = kw_def;
						break;
					case 'o': _lex_push(l); l->lookahead = kw_do; break;
				}
				return l->lookahead = _lex_scan_identifier(l, KJ_FALSE);

			case 'e':
				_lex_push(l);
				l->lookahead = tok_identifier;
				if (_lex_accept(l, "lse")) l->lookahead = kw_else;
				return l->lookahead = _lex_scan_identifier(l, KJ_FALSE);

			case 'f':
				_lex_push(l);
				l->lookahead = tok_identifier;
				switch (l->curr_char)
				{
					case 'a': _lex_push(l); if (_lex_accept(l, "lse")) l->lookahead = kw_false; break;
					case 'o': _lex_push(l); if (_lex_accept(l, "r"))   l->lookahead = kw_for; break;
				}
				return l->lookahead = _lex_scan_identifier(l, KJ_FALSE);

			case 'i':
				_lex_push(l);
				l->lookahead = tok_identifier;
				switch (l->curr_char)
				{
					case 'f': _lex_push(l); l->lookahead = kw_if; break;
					case 'n': _lex_push(l); l->lookahead = kw_in; break;
				}
				return l->lookahead = _lex_scan_identifier(l, KJ_FALSE);

			case 'r':
				_lex_push(l);
				l->lookahead = tok_identifier;
				if (_lex_accept(l, "eturn")) l->lookahead = kw_return;
				return l->lookahead = _lex_scan_identifier(l, KJ_FALSE);

			case 't':
				_lex_push(l);
				l->lookahead = tok_identifier;
				if (_lex_accept(l, "rue")) l->lookahead = kw_true;
				return l->lookahead = _lex_scan_identifier(l, KJ_FALSE);

			case 'v':
				_lex_push(l);
				l->lookahead = tok_identifier;
				if (_lex_accept(l, "ar")) l->lookahead = kw_var;
				return l->lookahead = _lex_scan_identifier(l, KJ_FALSE);

			case 'w':
				_lex_push(l);
				l->lookahead = tok_identifier;
				if (_lex_accept(l, "hile")) l->lookahead = kw_while;
				return l->lookahead = _lex_scan_identifier(l, KJ_FALSE);

			default:
				_lex_scan_identifier(l, KJ_TRUE);
				if (l->lookahead != tok_identifier)
					compile_error(l->error_handler, l->source_location, "unexpected character '%c' found.", l->curr_char);
				return l->lookahead;
		}
	}
}

/**
* Initializes an unitialized lexer instance @l to use specified error handler @e. Lexer will scan
* source using specified stream @stream_func and @stream_data, using @filename as the source origin
* descriptor.
*/
static void lex_init(kj_lexer *l, kj_error_handler *e, const char *filename, koji_stream_reader_fn stream_func,
	void* stream_data)
{
	l->error_handler = e;
	l->stream = stream_func;
	l->stream_data = stream_data;
	array_init(&l->token_string);
	array_push_value(&l->token_string, char, '\0');
	l->source_location.filename = filename;
	l->source_location.line = 1;
	l->source_location.column = 0;
	l->newline = 0;

	_lex_skip(l);
	lex(l);
}

/**
* Deinitializes an initialized lexer instance destroying its resources.
*/
static void lex_close(kj_lexer *l)
{
	array_destroy(&l->token_string);
}

#pragma endregion

//---------------------------------------------------------------------------------------------------------------------
#pragma region Bytecode

/** Enumeration that lists all Virtual Machine opcodes */
typedef enum kj_opcode
{
	KJ_OP_LOADNIL, /* loadnil A, Bx;    ; R(A), ..., R(Bx) = nil */
	KJ_OP_LOADB,   /* loadbool A, B, C  ; R(A) = bool(B) then jump by C */
	KJ_OP_MOV,     /* mov A, Bx         ; R(A) = R(Bx) */
	KJ_OP_NEG,     /* neg A, Bx         ; R(A) = not R(Bx) */
	KJ_OP_UNM,     /* unm A, Bx         ; R(A) = -R(Bx) */
	KJ_OP_ADD,     /* add A, B, C       ; R(A) = R(B) + R(C) */
	KJ_OP_SUB,     /* sub A, B, C       ; R(A) = R(B) - R(C) */
	KJ_OP_MUL,     /* mul A, B, C       ; R(A) = R(B) * R(C) */
	KJ_OP_DIV,     /* div A, B, C       ; R(A) = R(B) / R(C) */
	KJ_OP_MOD,     /* mod A, B, C       ; R(A) = R(B) % R(C) */
	KJ_OP_POW,     /* pow A, B, C       ; R(A) = pow(R(B), R(C)) */
	KJ_OP_TESTSET, /* testset A, B, C   ; if R(B) == (bool)C then R(A) = R(B) else jump 1 */
	KJ_OP_CLOSURE, /* closure A, Bx     ; R(A) = closure for prototype Bx */
	KJ_OP_CALL,    /* call A, B, C      ; call closure R(A) with C arguments starting at R(B) */

								// operations that do not write into a target register
	KJ_OP_TEST,    /* test A, Bx        ; if (bool)R(A) != (bool)B then jump 1 */
	KJ_OP_JUMP,    /* jump Bx           ; jump by Bx instructions */
	KJ_OP_EQ,      /* eq A, B, C        ; if (R(A) == R(B)) == (bool)C then nothing else jump 1 */
	KJ_OP_LT,      /* lt A, B, C        ; if (R(A) < R(B)) == (bool)C then nothing else jump 1 */
	KJ_OP_LTE,     /* lte A, B, C       ; if (R(A) <= R(B)) == (bool)C then nothing else jump 1 */
	KJ_OP_SCALL,   /* scall A, Bx       ; call static function at Bx with arguments starting from R(A) */
	KJ_OP_RET,     /* ret A, B          ; return values R(A), ..., R(B)*/
} kj_opcode;

static const char *KJ_OP_STRINGS[] = {
	"loadnil", "loadb", "mov", "neg", "unm", "add", "sub", "mul", "div", "mod", "pow", "testset", "closure", "call", "test",
	"jump", "eq", "lt", "lte",  "scall", "ret"
};

/* Instructions */

/** Type of a single instruction, always a 32bit long */
typedef uint32_t kj_instruction;

/** Maximum value a register can hold (positive or negative) */
static const uint MAX_REGISTER_VALUE = 255;

/** Maximum value Bx can hold (positive or negative) */
static const int MAX_BX_INTEGER = 131071;

/** Returns whether opcode @op involves writing into register A. */
static inline koji_bool opcode_has_target(kj_opcode op) { return op <= KJ_OP_CALL; }

/** Encodes an instruction with arguments A and Bx. */
static inline kj_instruction encode_ABx(kj_opcode op, int A, int Bx)
{
	assert(A >= 0);
	return (Bx << 14) | (A & 0xff) << 6 | op;
}

/** Encodes and returns an instruction with arguments A, B and C. */
static inline kj_instruction encode_ABC(kj_opcode op, int A, int B, int C)
{
	assert(A >= 0);
	return (C << 23) | (B & 0x1ff) << 14 | (A & 0xff) << 6 | op;
}

/** Decodes an instruction opcode. */
static inline kj_opcode decode_op(kj_instruction i) { return i & 0x3f; }

/** Decodes an instruction argument A. */
static inline int decode_A(kj_instruction i) { return (i >> 6) & 0xff; }

/** Decodes an instructions argument B. */
static inline int decode_B(kj_instruction i) { return ((int)i << 9) >> 23; }

/** Decodes an instruction argument C. */
static inline int decode_C(kj_instruction i) { return (int)i >> 23; }

/** Decodes an instruction argument Bx. */
static inline int decode_Bx(kj_instruction i) { return (int)i >> 14; }

/** Sets instruction argument A. */
static inline void replace_A(kj_instruction *i, int A) { assert(A >= 0); *i = (*i & 0xFFFFC03F) | (A << 6); }

/** Sets instruction argument Bx. */
static inline void replace_Bx(kj_instruction *i, int Bx) { *i = (*i & 0x3FFF) | (Bx << 14); }

/** Set instruction argument C */
static inline void replace_C(kj_instruction *i, int C) { *i = (*i & 0x7FFFFF) | (C << 23); }

/**
* A value is the primary script data type. It is dynamically typed as it carries its type information at
* runtime.
*/
typedef struct kj_value kj_value;


/**
* TODO
*/
typedef struct kj_closure
{
	koji_prototype* proto;
}
kj_closure;

/**
* Enumeration of all supported value types.
*/
typedef enum
{
	KJ_VALUE_NIL,
	KJ_VALUE_BOOL,
	KJ_VALUE_INT,
	KJ_VALUE_REAL,
	KJ_VALUE_CLOSURE,
} kj_value_type;

static const char *KJ_VALUE_TYPE_STRING[] = { "nil", "bool", "int", "real", "closure" };

/* Definition */
struct kj_value
{
	kj_value_type type;
	union
	{
		koji_bool    boolean;
		koji_integer integer;
		koji_real    real;
		kj_closure  closure;
	};
};

/** Dynamic array of instructions. */
typedef array_type(kj_instruction) instructions;

/* Definition */
struct koji_prototype
{
	uint references;
	ubyte nargs;
	ubyte ntemporaries;
	array_type(kj_value) constants;
	instructions instructions;
	array_type(koji_prototype*) prototypes;
};

/**
* (internal) Destroys prototype @p resources and frees its memory.
*/
static void prototype_delete(koji_prototype* p)
{
	assert(p->references == 0);
	array_destroy(&p->constants);
	array_destroy(&p->instructions);
	// delete all child prototypes that reach reference to zero.
	for (uint i = 0; i < p->prototypes.size; ++i)
		koji_prototype_release(p->prototypes.data[i]);
	array_destroy(&p->prototypes);
	free(p);
}

/**
* Prints prototype information and instructions for compilation debugging.
*/
static void prototype_dump(koji_prototype* p, int level, int index)
{
	if (level == 0)
		printf("Main prototype\n");
	else
		printf("\nChild prototype %d-%d:\n", level, index);

	printf("#const %d, #args %d, #temps %d, #instr %d, #proto %d\n",
		p->constants.size, p->nargs, p->ntemporaries, p->instructions.size, p->prototypes.size);
	for (uint i = 0; i < p->instructions.size; ++i)
	{
		printf("[%d] ", i);

		kj_instruction instr = p->instructions.data[i];

		kj_opcode op = decode_op(instr);
		const char* opstr = KJ_OP_STRINGS[op];

		int A = decode_A(instr), B = decode_B(instr), C = decode_C(instr), Bx = decode_Bx(instr);

		switch (op)
		{
			case KJ_OP_MOV: case KJ_OP_NEG: case KJ_OP_LOADNIL: case KJ_OP_RET:
				printf("%s\t%d, %d\t", opstr, A, Bx);
				goto print_constant;

			case KJ_OP_ADD: case KJ_OP_SUB: case KJ_OP_MUL: case KJ_OP_DIV: case KJ_OP_MOD: case KJ_OP_POW: case KJ_OP_CALL:
				printf("%s\t%d, %d, %d", opstr, A, B, C);
				Bx = C;

			print_constant:
				if (Bx < 0)
				{
					printf("\t; ");
					kj_value k = p->constants.data[-Bx - 1];
					switch (k.type)
					{
						case KJ_VALUE_INT: printf("%lld", (long long int)k.integer); break;
						case KJ_VALUE_REAL: printf("%.3f", k.real); break;
						default: assert(0);
					}
				}
				break;

			case KJ_OP_TEST:
				printf("%s\t%d, %s", opstr, A, Bx ? "true" : "false");
				break;

			case KJ_OP_LOADB:
				printf("%s\t%d, %s, %d\t; to [%d]", opstr, A, B ? "true" : "false", C, i + C + 1);
				break;

			case KJ_OP_TESTSET: case KJ_OP_EQ: case KJ_OP_LT: case KJ_OP_LTE:
				printf("%s\t%d, %d, %s", opstr, A, B, C ? "true" : "false");
				break;

			case KJ_OP_JUMP:
				printf("%s\t%d\t\t; to [%d]", opstr, Bx, i + Bx + 1);
				break;

			case KJ_OP_CLOSURE:
			case KJ_OP_SCALL:
				printf("%s\t%d, %d", opstr, A, Bx);
				break;

			default:
				assert(KJ_FALSE);
				break;
		}

		printf("\n");
	}

	for (uint i = 0; i < p->prototypes.size; ++i)
		prototype_dump(p->prototypes.data[i], level + 1, i);
}

/* kj_value functions */

static inline void value_destroy(kj_value* v)
{
	switch (v->type)
	{
		case KJ_VALUE_CLOSURE:
			/* check whether this closure was the last reference to the prototype module, if so destroy the module */
			koji_prototype_release(v->closure.proto);
			break;

		default: break;
	}
}

static inline void value_set_nil(kj_value* v)
{
	value_destroy(v);
	v->type = KJ_VALUE_NIL;
}

static inline void value_set_boolean(kj_value* v, koji_bool boolean)
{
	value_destroy(v);
	v->type = KJ_VALUE_BOOL;
	v->boolean = boolean;
}

static inline void value_set_integer(kj_value* v, koji_integer integer)
{
	value_destroy(v);
	v->type = KJ_VALUE_INT;
	v->integer = integer;
}

static inline void value_set_real(kj_value* v, koji_real real)
{
	value_destroy(v);
	v->type = KJ_VALUE_REAL;
	v->real = real;
}

static inline void value_make_closure(kj_value* v, koji_prototype* proto)
{
	value_destroy(v);
	++proto->references;
	v->type = KJ_VALUE_CLOSURE;
	v->closure = (kj_closure) { proto };
}

static inline void value_set(kj_value* dest, const kj_value* src)
{
	if (dest == src) return;
	value_destroy(dest);
	*dest = *src;
	// bump up object reference if needed
	switch (dest->type)
	{
		case KJ_VALUE_CLOSURE:
			++dest->closure.proto->references;
			break;

		default: break;
	}
}

/**
* A static function is a C function registered by the user *before* compiling scripts. These
* should be low level library functions that might are called often (e.g. sqrt).
*/
typedef struct kj_static_function
{
	uint name_string_offset;
	koji_user_function function;
	int nargs;
} kj_static_function;

/**
* Collection of static functions.
*/
typedef struct kj_static_functions
{
	array_type(char) name_buffer;
	array_type(kj_static_function) functions;
} kj_static_functions;

/**
* Searches for a static function with name @name and @nargs number of arguments in @fns and returns
* its index in fns->functions if found, -1 otherwise.
*/
static int static_functions_fetch(kj_static_functions const* fns, const char* name, int nargs)
{
	int best_candidate = -1;
	for (uint i = 0; i < fns->functions.size; ++i)
	{
		kj_static_function* fn = fns->functions.data + i;
		if (strcmp(name, fns->name_buffer.data + fn->name_string_offset) == 0)
		{
			best_candidate = i;
			if (nargs < 0 || nargs == fn->nargs) return i;
		}
	}
	return best_candidate;
}

#pragma endregion

//---------------------------------------------------------------------------------------------------------------------
#pragma region Compiler

/**
* A local variable is simply a named and reserved stack register offset.
*/
typedef struct
{
	uint identifier_offset;
	int location;
} kc_local;


/**
* A label is a dynamic array of the indices of the instructions that branch to it.
*/
typedef array_type(uint) kc_label;

/**
* Holds the state of a compilation execution.
*/
typedef struct kj_compiler
{
	kj_static_functions const* static_functions;
	kj_lexer *lex;
	koji_prototype * proto;
	array_type(char) identifiers;
	array_type(kc_local) locals;
	int  temporaries;
	kc_label true_label;
	kc_label false_label;
} kj_compiler;

/* Auxiliary parsing functions */

/**
* Formats and reports a syntax error (unexpected <token>).
*/
static void kc_syntax_error(kj_compiler *c, kj_source_location sourceloc)
{
	compile_error(c->lex->error_handler, sourceloc, "unexpected %s.", lex_lookahead_to_string(c->lex));
}

/**
* Scans next token if lookahead is @tok. Returns whether a new token was scanned.
*/
static inline koji_bool kc_accept(kj_compiler *c, kj_token tok)
{
	if (c->lex->lookahead == tok)
	{
		lex(c->lex);
		return KJ_TRUE;
	}
	return KJ_FALSE;
}

/**
* Reports a compilation error if lookhead differs from @tok.
*/
static inline void kc_check(kj_compiler *c, kj_token tok)
{
	if (c->lex->lookahead != tok)
	{
		char token_string_buffer[64];
		compile_error(c->lex->error_handler, c->lex->source_location, "missing %s before '%s'.",
			lex_token_to_string(tok, token_string_buffer, 64), lex_lookahead_to_string(c->lex));
	}
}

/**
* Checks that lookahead is @tok then scans next token.
*/
static inline void kc_expect(kj_compiler *c, kj_token tok)
{
	kc_check(c, tok);
	lex(c->lex);
}

/**
* Returns an "end of statement" token is found (newline, ';', '}' or end-of-stream) and "eats" it.
*/
static inline koji_bool kc_accept_end_of_stmt(kj_compiler *c)
{
	if (kc_accept(c, ';')) return KJ_TRUE;
	if (c->lex->lookahead == '}' || c->lex->lookahead == tok_eos) return KJ_TRUE;
	if (c->lex->newline) { c->lex->newline = KJ_FALSE; return KJ_TRUE; }
	return KJ_FALSE;
}

/**
* Expects an end of statement.
*/
static inline void kc_expect_end_of_stmt(kj_compiler *c)
{
	if (!kc_accept_end_of_stmt(c)) kc_syntax_error(c, c->lex->source_location);
}

/* Constants management */

/**
* Searches a constant in value @k and adds it if not found, then it returns its index.
*/
static inline int _kc_fetch_constant(kj_compiler *c, kj_value k)
{
	for (uint i = 0; i < c->proto->constants.size; ++i)
	{
		if (memcmp(c->proto->constants.data + i, &k, sizeof(kj_value)) == 0)
			return i;
	}

	/* constant not found, add it */
	int index = c->proto->constants.size;
	array_push_value(&c->proto->constants, kj_value, k);

	return index;
}

static inline int kc_fetch_constant_int(kj_compiler *c, koji_integer k)
{
	return _kc_fetch_constant(c, (kj_value) { KJ_VALUE_INT, .integer = k });
}

static inline int kc_fetch_constant_real(kj_compiler *c, koji_real k)
{
	return _kc_fetch_constant(c, (kj_value) { KJ_VALUE_REAL, .real = k });
}

/* instruction emission */

/**
* Pushes instruction @i to current prototype instructions.
*/
static inline void kc_emit(kj_compiler *c, kj_instruction i)
{
	if (opcode_has_target(decode_op(i)))
	{
		ubyte ntemporaries = (ubyte)decode_A(i) + 1;
		c->proto->ntemporaries = c->proto->ntemporaries < ntemporaries ? ntemporaries : c->proto->ntemporaries;
	}
	array_push_value(&c->proto->instructions, kj_instruction, i);
}

/**
* Computes and returns the offset specified from instruction index to the next instruction
* that will be emitted in current prototype.
*/
static inline int kc_offset_to_next_instruction(kj_compiler *c, int from_instruction_index)
{
	return c->proto->instructions.size - from_instruction_index - 1;
}

/* Jump instruction related */

/**
* Writes the offset to jump instructions contained in @label starting from @begin to target
* instruction index target_index.
*/
static void kc_bind_label_to(kj_compiler *c, kc_label* label, uint begin, int target_index)
{
	for (uint i = begin, size = label->size; i < size; ++i)
	{
		uint jump_instr_index = label->data[i];
		replace_Bx(c->proto->instructions.data + jump_instr_index, target_index - jump_instr_index - 1);
	}
	label->size = begin;
}

/**
* Binds jump instructions in @label starting from @begin to the next instruction that will be
* emitted to current prototype.
*/
static inline void kc_bind_label_here(kj_compiler *c, kc_label* label, uint begin)
{
	kc_bind_label_to(c, label, begin, c->proto->instructions.size);
}

/* local variables */

/**
* Adds an identifier string to the compiler buffer and returns the string offset within it.
*/
static uint kc_push_identifier(kj_compiler *c, const char* identifier, uint identifier_size)
{
	char* my_identifier = array_push(&c->identifiers, char, identifier_size + 1);
	memcpy(my_identifier, identifier, identifier_size + 1);
	return my_identifier - c->identifiers.data;
}

/**
* Searches for a local named @identifier from current prototype up to the main one and returns
* a pointer to it if found, null otherwise.
*/
static kc_local* kc_fetch_local(kj_compiler *c, const char* identifier)
{
	for (int i = c->locals.size - 1; i >= 0; --i)
	{
		const char* var_identifier = c->identifiers.data + c->locals.data[i].identifier_offset;
		if (strcmp(var_identifier, identifier) == 0)
			return c->locals.data + i;
	}

	return NULL;
}

/**
* Defines a new local variable in current prototype with an identifier starting at
* identifier_offset preiously pushed through _push_identifier().
*/
static void kc_define_local(kj_compiler *c, uint identifier_offset)
{
	kc_local* var = array_push(&c->locals, kc_local, 1);
	var->identifier_offset = identifier_offset;
	// set the variable location to the current unused free register
	var->location = c->temporaries++;
}

/* Expressions */

/**
* A state structure used to contain information about expression parsing and compilation
* such as the desired target register, whether the expression should be negated and the
* indices of new jump instructions in compiler state global true/false labels.
*/
typedef struct kc_expr_state
{
	int target_register;
	uint true_jumps_begin;
	uint false_jumps_begin;
	koji_bool negated;
} kc_expr_state;

/**
* Enumeration of the types an expression can take.
*/
typedef enum
{
	KC_EXPR_TYPE_NIL,
	KC_EXPR_TYPE_BOOL,
	KC_EXPR_TYPE_INT,
	KC_EXPR_TYPE_REAL,
	KC_EXPR_TYPE_REGISTER,
	KC_EXPR_TYPE_EQ,
	KC_EXPR_TYPE_LT,
	KC_EXPR_TYPE_LTE,
} kc_expr_type;

static const char* KC_EXPR_TYPE_TYPE_TO_STRING[] =
{ "nil", "bool", "int", "real", "register", "bool", "bool", "bool" };

/**
* An expr is the result of a subexpression during compilation. For optimal bytecode generation
* expression are actually compiled to registers lazily, at the last moment possible so that
* simple optimizations such as operations on constants can be performed.
*/
typedef struct kc_expr
{
	kc_expr_type type;
	union
	{
		koji_integer integer;
		koji_real real;
		int location;
		struct
		{
			int lhs;
			int rhs;
		};
	};
	koji_bool positive;
} kc_expr;

static const kc_expr KC_EXPR_NIL = { .type = KC_EXPR_TYPE_NIL, .positive = KJ_TRUE };

/**
* Makes and returns a boolean expr of specified @value.
*/
static inline kc_expr kc_expr_boolean(koji_bool value)
{
	return (kc_expr) { KC_EXPR_TYPE_BOOL, .integer = value, .positive = KJ_TRUE };
}

/**
* Makes and returns a integer expr of specified @value.
*/
static inline kc_expr kc_expr_integer(koji_integer value)
{
	return (kc_expr) { KC_EXPR_TYPE_INT, .integer = value, .positive = KJ_TRUE };
}

/**
* Makes and returns a real expr of specified @value.
*/
static inline kc_expr kc_expr_real(koji_real value)
{
	return (kc_expr) { KC_EXPR_TYPE_REAL, .real = value, .positive = KJ_TRUE };
}

/**
* Makes and returns a register expr of specified @location.
*/
static inline kc_expr kc_expr_register(int location)
{
	return (kc_expr) { KC_EXPR_TYPE_REGISTER, .location = location, .positive = KJ_TRUE };
}

/**
* Makes and returns a comparison expr of specified @type between @lhs_location and @rhs_location
* against test value @test_value.
*/
static inline kc_expr kc_expr_comparison(kc_expr_type type, koji_bool test_value, int lhs_location,
	int rhs_location)
{
	return (kc_expr) { type, .lhs = lhs_location, .rhs = rhs_location, .positive = test_value };
}

/**
* Returns whether expression @type is a constant (bool, int or real).
*/
static inline koji_bool kc_expr_is_constant(kc_expr_type type)
{
	return type >= KC_EXPR_TYPE_BOOL && type <= KC_EXPR_TYPE_REAL;
}

/**
* Returns whether an expression of specified @expr can be *statically* converted to a bool.
*/
static inline koji_bool kc_expr_is_bool_convertible(kc_expr_type type) { return type <= KC_EXPR_TYPE_REAL; }

/**
* Returns whether an expression of specified @type is a comparison.
*/
static inline koji_bool kc_expr_is_comparison(kc_expr_type type) { return type >= KC_EXPR_TYPE_EQ; }

/**
* Converts @expr to a boolean. kc_expr_is_bool_convertible(expr) must return true.
*/
static inline koji_bool kc_expr_to_bool(kc_expr expr)
{
	switch (expr.type)
	{
		case KC_EXPR_TYPE_NIL:
			return 0;

		case KC_EXPR_TYPE_BOOL:
		case KC_EXPR_TYPE_INT:
			return expr.integer != 0;

		case KC_EXPR_TYPE_REAL:
			return expr.real != 0;

		default: assert(0); return 0;
	}
}

/**
* Converts @expr to a real. Value is casted if expr is a constant, otherwise it returns zero.
*/
static inline koji_real kc_expr_to_real(kc_expr expr)
{
	switch (expr.type)
	{
		case KC_EXPR_TYPE_BOOL:
		case KC_EXPR_TYPE_INT:
			return (koji_real)expr.integer;

		case KC_EXPR_TYPE_REAL:
			return expr.real;

		default:
			return 0;
	}
}

/**
* Compiles expression @e to a register and returns the expression (of type KC_EXPR_TYPE_REGISTER)
* containing the value that was contained in @e.
* If @e needs to be moved to some register (e.g. it is a boolean value), @target_hint is used.
*/
static inline kc_expr kc_expr_to_any_register(kj_compiler *c, kc_expr e, int target_hint)
{
	uint constant_index;
	int location;

	switch (e.type)
	{
		case KC_EXPR_TYPE_NIL:
			kc_emit(c, encode_ABx(KJ_OP_LOADNIL, target_hint, target_hint));
			return kc_expr_register(target_hint);

		case KC_EXPR_TYPE_BOOL:
			kc_emit(c, encode_ABC(KJ_OP_LOADB, target_hint, (koji_bool)e.integer, 0));
			return kc_expr_register(target_hint);

		case KC_EXPR_TYPE_INT:
			constant_index = kc_fetch_constant_int(c, e.integer);
			goto make_constant;

		case KC_EXPR_TYPE_REAL: constant_index = kc_fetch_constant_real(c, e.real);
			goto make_constant;

		make_constant:
			location = -(int)constant_index - 1;
			if (constant_index <= MAX_REGISTER_VALUE)
			{
				// constant is small enough to be used as direct index
				return kc_expr_register(location);
			}
			else
			{
				// constant too large, load it into a temporary register
				kc_emit(c, encode_ABx(KJ_OP_MOV, target_hint, location));
				return kc_expr_register(target_hint);
			}

		case KC_EXPR_TYPE_REGISTER:
			if (e.positive)
				return e;
			kc_emit(c, encode_ABx(KJ_OP_NEG, target_hint, e.location));
			return kc_expr_register(target_hint);

		case KC_EXPR_TYPE_EQ: case KC_EXPR_TYPE_LT: case KC_EXPR_TYPE_LTE:
			kc_emit(c, encode_ABC(KJ_OP_EQ + e.type - KC_EXPR_TYPE_EQ, e.lhs, e.positive, e.rhs));
			kc_emit(c, encode_ABx(KJ_OP_JUMP, 0, 1));
			kc_emit(c, encode_ABC(KJ_OP_LOADB, target_hint, KJ_FALSE, 1));
			kc_emit(c, encode_ABC(KJ_OP_LOADB, target_hint, KJ_TRUE, 0));
			return kc_expr_register(target_hint);

		default: assert(0); return KC_EXPR_NIL;
	}
}

/**
* Emits the appropriate instructions so that expression e value is written to register @target.
*/
static void kc_move_expr_to_register(kj_compiler *c, kc_expr e, int target)
{
	e = kc_expr_to_any_register(c, e, target);
	assert(e.positive);
	if (e.location != target)
	{
		instructions *instructions = &c->proto->instructions;
		kj_instruction *last_instruction;
		if (instructions->size > 0

			/* last operation has target register in A*/
			&& opcode_has_target(decode_op(*(last_instruction = instructions->data + instructions->size - 1)))

			/* A register is only a temporary */
			&& decode_A(*last_instruction) >= c->temporaries)
		{
			replace_A(last_instruction, target);
			return;
		}

		kc_emit(c, encode_ABx(KJ_OP_MOV, target, e.location));
	}
}

/**
* Enumeration of binary operators.
*/
typedef enum
{
	KC_BINOP_INVALID,
	KC_BINOP_LOGICAL_AND,
	KC_BINOP_LOGICAL_OR,
	KC_BINOP_EQ,
	KC_BINOP_NEQ,
	KC_BINOP_LT,
	KC_BINOP_LTE,
	KC_BINOP_GT,
	KC_BINOP_GTE,
	KC_BINOP_ADD,
	KC_BINOP_SUB,
	KC_BINOP_MUL,
	KC_BINOP_DIV,
	KC_BINOP_MOD,
} kc_binop;

static const char* KC_BINOP_TO_STR[] = { "<invalid>", "&&", "||", "==", "!=", "<", "<=", ">", ">=",
"+", "-", "*", "/", "%" };

/**
* Converts token @tok to the corresponding binary operator.
*/
static inline kc_binop kc_token_to_binop(kj_token tok)
{
	switch (tok)
	{
		case '&&': return KC_BINOP_LOGICAL_AND;
		case '||': return KC_BINOP_LOGICAL_OR;
		case '==': return KC_BINOP_EQ;
		case '!=': return KC_BINOP_NEQ;
		case '<':  return KC_BINOP_LT;
		case '<=': return KC_BINOP_LTE;
		case '>':  return KC_BINOP_GT;
		case '>=': return KC_BINOP_GTE;
		case '+':  return KC_BINOP_ADD;
		case '-':  return KC_BINOP_SUB;
		case '*':  return KC_BINOP_MUL;
		case '/':  return KC_BINOP_DIV;
		case '%':  return KC_BINOP_MOD;
		default:   return KC_BINOP_INVALID;
	}
}

/**
* If expression @e is a register of location equal to current free register, it bumps up the free
* register counter. It returns the old temporary register regardless whether if the current
* temporary register was bumped up.
* After using the new temporary you must restore the temporary register c->temporary to the value
* returned by this function.
*/
static int kc_use_temporary(kj_compiler *c, kc_expr const* e)
{
	int old_temporaries = c->temporaries;
	if (e->type == KC_EXPR_TYPE_REGISTER && e->location == c->temporaries)
	{
		++c->temporaries;
		return old_temporaries;
	}
	return old_temporaries;
}

/**
* Compiles the logical negation of expression @e. It returns the negated result.
*/
static kc_expr kc_negate(kc_expr e)
{
	switch (e.type)
	{
		case KC_EXPR_TYPE_NIL:
			return kc_expr_boolean(KJ_TRUE);

		case KC_EXPR_TYPE_INT:
		case KC_EXPR_TYPE_REAL:
			return kc_expr_boolean(!kc_expr_to_bool(e));

		default:
			e.positive = !e.positive;
			return e;
	}
}

/**
* Compiles the unary minus of expression @e and returns the result.
*/
static kc_expr kc_unary_minus(kj_compiler *c, const kc_expr_state* es, kj_source_location sourceloc, kc_expr e)
{
	switch (e.type)
	{
		case KC_EXPR_TYPE_INT:
			return kc_expr_integer(-e.integer);

		case KC_EXPR_TYPE_REAL:
			return kc_expr_real(-e.real);

		case KC_EXPR_TYPE_REGISTER:
			kc_emit(c, encode_ABx(KJ_OP_NEG, es->target_register, e.location));
			return kc_expr_register(es->target_register);

		default:
			compile_error(c->lex->error_handler, sourceloc,
				"cannot apply operator unary minus to a value of type %s.", KC_EXPR_TYPE_TYPE_TO_STRING[e.type]);
	}
	return KC_EXPR_NIL;
}

/* forward decls */
static kc_expr kc_parse_expression(kj_compiler *c, const kc_expr_state* es);
static kc_expr kc_parse_expression_to_any_register(kj_compiler *c, int target);
static inline void kc_parse_block(kj_compiler *c);

/**
* Parses and compiles a closure starting from arguments declaration (therefore excluded "def" and
* eventual identifier). It returns the register expr with the location of the compiled closure.
*/
static kc_expr kc_parse_closure(kj_compiler *c, const kc_expr_state* es)
{
	ubyte num_args = 0;
	int proto_index = c->proto->prototypes.size;

	uint oldnlocals = c->locals.size;
	koji_prototype* oldproto = c->proto;
	uint oldtemporaries = c->temporaries;
	c->temporaries = 0;

	c->proto = malloc(sizeof(koji_prototype));
	*c->proto = (koji_prototype) { 0 };
	c->proto->references = 1;
	array_push_value(&oldproto->prototypes, koji_prototype*, c->proto);

	kc_expect(c, '(');
	if (c->lex->lookahead != ')')
	{
		do
		{
			kc_check(c, tok_identifier);
			uint id_offset = kc_push_identifier(c, c->lex->token_string.data, c->lex->token_string.size);
			lex(c->lex);
			kc_define_local(c, id_offset);
			++num_args;
		} while (kc_accept(c, ','));
	}
	kc_expect(c, ')');

	if (kc_accept(c, '=>'))
	{
		kc_move_expr_to_register(c, kc_parse_expression_to_any_register(c, c->temporaries), c->temporaries);
	}
	else
	{
		kc_parse_block(c);
	}

	kc_emit(c, encode_ABx(KJ_OP_RET, 0, 0));

	c->temporaries = oldtemporaries;
	c->locals.size = oldnlocals;
	c->proto->nargs = num_args;
	c->proto->ntemporaries -= c->proto->nargs;
	c->proto = oldproto;

	kc_emit(c, encode_ABx(KJ_OP_CLOSURE, c->temporaries, proto_index));
	return kc_expr_register(es->target_register);
}

/**
* Parses and compiles a function call arguments "(arg1, arg2, ..)" and returns the number of
* arguments.
* Before calling this function save the current temporary as arguments will be compiled to
* temporaries staring from current. It's responsibility of the caller to restore the temporary
* register.
*/
static int kc_parse_function_call_args(kj_compiler *c)
{
	int nargs = 0;
	kj_source_location sourceloc = c->lex->source_location;

	if (kc_accept(c, '('))
	{
		if (c->lex->lookahead != ')')
		{
			do
			{
				// parse argument expression into current free register
				kc_expr arg = kc_parse_expression_to_any_register(c, c->temporaries);
				kc_move_expr_to_register(c, arg, c->temporaries++);
				++nargs;
			} while (kc_accept(c, ','));
		}

		kc_expect(c, ')');
		return nargs;
	}

	// todo {}
	kc_syntax_error(c, sourceloc);
	return nargs;
}

/**
* Parses and returns a primary expression such as a constant or a function call.
*/
static kc_expr kc_parse_primary_expression(kj_compiler *c, const kc_expr_state* es)
{
	kc_expr expr = { KC_EXPR_TYPE_NIL };
	kj_source_location sourceloc = c->lex->source_location;

	switch (c->lex->lookahead)
	{
		/* literals */
		case kw_true: lex(c->lex); expr = kc_expr_boolean(KJ_TRUE); break;
		case kw_false: lex(c->lex); expr = kc_expr_boolean(KJ_FALSE); break;
		case tok_integer: expr = kc_expr_integer(c->lex->token_int); lex(c->lex); break;
		case tok_real: expr = kc_expr_real(c->lex->token_real); lex(c->lex); break;

		case '(': /* subexpression */
			lex(c->lex);
			expr = kc_parse_expression(c, es);
			kc_expect(c, ')');
			break;

		case '!': /* not */
		{
			koji_bool negated = KJ_TRUE;
			lex(c->lex);
			while (kc_accept(c, '!')) negated = !negated;
			kc_expr_state my_es = *es;
			my_es.negated = my_es.negated ^ negated;
			expr = kc_parse_primary_expression(c, &my_es);
			if (negated) expr = kc_negate(expr);
			break;
		}

		case '-': /* unary minus */
		{
			koji_bool minus = KJ_TRUE;
			lex(c->lex);
			while (kc_accept(c, '-')) minus = !minus;
			expr = kc_parse_primary_expression(c, es);
			if (minus) expr = kc_unary_minus(c, es, sourceloc, expr);
			break;
		}

		case tok_identifier: /* variable */
		{
			/* we need to scan the token after the identifier so copy it to a temporary on the stack */
			char* id = alloca(c->lex->token_string.size + 1);
			memcpy(id, c->lex->token_string.data, c->lex->token_string.size + 1);
			lex(c->lex);

			/* identifier refers to a local variable? */
			kc_local* var = kc_fetch_local(c, id);
			if (var)
			{
				expr = kc_expr_register(var->location);
				break;
			}

			/* identifier refers to a static function? */
			int nargs = -1;
			if (c->lex->lookahead == '(')
			{
				int first_arg_reg = c->temporaries;
				nargs = kc_parse_function_call_args(c);

				uint fn_index = static_functions_fetch(c->static_functions, id, nargs);
				if (fn_index != -1)
				{
					const kj_static_function* fn = c->static_functions->functions.data + fn_index;
					if (fn->nargs != nargs)
					{
						compile_error(c->lex->error_handler, sourceloc, "static function '%s' does not take %d argument/s but %d.",
							id, nargs, fn->nargs);
						break;
					}

					// call is to a static host function, emit the appropriate instruction and reset the number of used registers
					kc_emit(c, encode_ABx(KJ_OP_SCALL, first_arg_reg, fn_index));
					c->temporaries = first_arg_reg;

					// all args will be popped and return values put into the first arg register.
					expr = kc_expr_register(first_arg_reg);
					break;
				}
			}

			(void)nargs;
			compile_error(c->lex->error_handler, sourceloc, "undeclared local variable '%s'.", id);
			break;
		}

		case kw_def: /* closure */
			lex(c->lex);
			expr = kc_parse_closure(c, es);
			break;

		default: kc_syntax_error(c, sourceloc);
	}

	// parse function calls
	while (c->lex->lookahead == '(')
	{
		int first_arg_reg = c->temporaries;
		int nargs = kc_parse_function_call_args(c);
		if (expr.type != KC_EXPR_TYPE_REGISTER)
		{
			compile_error(c->lex->error_handler, sourceloc, "cannot call value of type %s.", KC_EXPR_TYPE_TYPE_TO_STRING[expr.type]);
			return KC_EXPR_NIL;
		}
		kc_emit(c, encode_ABC(KJ_OP_CALL, first_arg_reg, expr.location, nargs));
		expr = kc_expr_register(first_arg_reg);
		c->temporaries = first_arg_reg;
	}

	return expr;
}

/**
* Compiles the lhs of a logical expression if @op is such and lhs is a register or comparison.
* Only called by _parse_binary_expr_rhs() before parsing its rhs.
* In a nutshell, the purpose of this function is to patch th current early out branches to the true
* or false label depending on @op, the truth value (positivity) of lhs and whether the whole expression
* should be negated (as stated by @es).
* What this function does is: compiles the comparison if lhs is one, or emit the test/testset if
* is a register if say op is an OR then it patches all existing branches to false in the compiled
* expression represented by lhs to this point so that the future rhs will "try again" the OR. The
* opposite holds for AND (jumps to true are patched to evaluate the future rhs because evaluating
* only the lhs to true is not enough in an AND).
*/
static void kc_compile_logical_operation(kj_compiler *c, const kc_expr_state* es, kc_binop op, kc_expr lhs)
{
	if ((lhs.type != KC_EXPR_TYPE_REGISTER && !kc_expr_is_comparison(lhs.type))
		|| (op != KC_BINOP_LOGICAL_AND && op != KC_BINOP_LOGICAL_OR))
		return;

	instructions* instructions = &c->proto->instructions;

	koji_bool test_value = (op == KC_BINOP_LOGICAL_OR) ^ es->negated;

	/* compile condition */
	switch (lhs.type)
	{
		case KC_EXPR_TYPE_REGISTER:
			if (!lhs.positive == es->negated)
			{
				kc_emit(c, encode_ABC(KJ_OP_TESTSET, es->target_register, lhs.location, test_value));
			}
			else
			{
				assert(lhs.location >= 0); // is non-constant
				kc_emit(c, encode_ABC(KJ_OP_TEST, lhs.location, !test_value, 0));
			}
			break;

		case KC_EXPR_TYPE_EQ: case KC_EXPR_TYPE_LT: case KC_EXPR_TYPE_LTE:
			kc_emit(c, encode_ABC(KJ_OP_EQ + lhs.type - KC_EXPR_TYPE_EQ, lhs.lhs, lhs.rhs, (lhs.positive ^ es->negated) ^ !test_value));
			break;

		default: assert(KJ_FALSE);
	}

	// push jump instruction index to the appropriate label.
	uint* offset = array_push(test_value ? &c->true_label : &c->false_label, uint, 1);
	*offset = instructions->size;
	kc_emit(c, KJ_OP_JUMP);

	kc_label *pjump_vector = &c->true_label;
	size_t begin = es->true_jumps_begin;

	if (test_value) // or
	{
		pjump_vector = &c->false_label;
		begin = es->false_jumps_begin;
	}

	uint num_jumps;
	while ((num_jumps = pjump_vector->size) > begin)
	{
		uint index = pjump_vector->data[pjump_vector->size - 1];

		// if instruction before the current jump instruction is a TESTSET, turn it to a simple TEST
		// instruction as its "set" is wasted since we need to test more locations before we can
		// finally set the target (example "c = a && b", if a is true, don't c just yet, we need to
		// test b first)
		if (index > 0 && decode_op(instructions->data[index - 1]) == KJ_OP_TESTSET)
		{
			kj_instruction instr = instructions->data[index - 1];

			int tested_reg = decode_B(instr);
			koji_bool flag = (koji_bool)decode_C(instr);

			instructions->data[index - 1] = encode_ABx(KJ_OP_TEST, tested_reg, flag);
		}

		replace_Bx(instructions->data + index, kc_offset_to_next_instruction(c, index));
		array_resize(pjump_vector, sizeof(uint), num_jumps - 1);
	}
}

/**
* Helper function for parse_binary_expression_rhs() that actually compiles the binary operation
* between @lhs and @rhs. This function also checks whether the operation can be optimized to a
* constant if possible before falling back to emitting the actual instructions.
*/
static kc_expr _compile_binary_expression(kj_compiler *c, const kc_expr_state* es, const kj_source_location sourceloc,
	kc_binop binop, kc_expr lhs, kc_expr rhs)
{

#define MAKEBINOP(binop, opchar)\
	case binop:\
		if (lhs.type == KC_EXPR_TYPE_NIL || rhs.type == KC_EXPR_TYPE_NIL) goto error;\
		if (kc_expr_is_constant(lhs.type) && kc_expr_is_constant(rhs.type))\
		{\
			if (lhs.type == KC_EXPR_TYPE_BOOL || rhs.type == KC_EXPR_TYPE_BOOL) goto error;\
			if (lhs.type == KC_EXPR_TYPE_REAL || rhs.type == KC_EXPR_TYPE_REAL)\
				lhs = kc_expr_real(kc_expr_to_real(lhs) opchar kc_expr_to_real(rhs));\
			else\
				lhs.integer = lhs.integer opchar rhs.integer;\
			return lhs;\
		}\
		break;

	// make a binary operator between our lhs and the rhs
	//lhs = compile_binary_operation(c, es, sourceloc, binop, lhs, rhs);
	switch (binop)
	{
		MAKEBINOP(KC_BINOP_ADD, +)
			MAKEBINOP(KC_BINOP_SUB, -)
			MAKEBINOP(KC_BINOP_MUL, *)
			MAKEBINOP(KC_BINOP_DIV, / )

		case KC_BINOP_MOD:
			if (lhs.type == KC_EXPR_TYPE_NIL || rhs.type == KC_EXPR_TYPE_NIL) goto error;
			if (kc_expr_is_constant(lhs.type) && kc_expr_is_constant(rhs.type))
			{
				if (lhs.type == KC_EXPR_TYPE_BOOL || rhs.type == KC_EXPR_TYPE_BOOL) goto error;
				if (lhs.type == KC_EXPR_TYPE_REAL || rhs.type == KC_EXPR_TYPE_REAL) goto error;
				lhs.integer %= rhs.integer;
				return lhs;
			}
			break;

			/* lhs is a register and we assume that Compiler has called "prepare_logical_operator_lhs" before calling this hence
			the testset instruction has already been emitted. */
		case KC_BINOP_LOGICAL_AND:
			lhs = (kc_expr_is_bool_convertible(lhs.type) && !kc_expr_to_bool(lhs)) ? kc_expr_boolean(KJ_FALSE) : rhs;
			return lhs;

		case KC_BINOP_LOGICAL_OR:
			lhs = (kc_expr_is_bool_convertible(lhs.type) && kc_expr_to_bool(lhs)) ? kc_expr_boolean(KJ_TRUE) : rhs;
			return lhs;

		case KC_BINOP_EQ:
		case KC_BINOP_NEQ:
		{
			koji_bool invert = binop == KC_BINOP_NEQ;
			if (lhs.type == KC_EXPR_TYPE_NIL || rhs.type == KC_EXPR_TYPE_NIL)
			{
				lhs = kc_expr_boolean(((lhs.type == KC_EXPR_TYPE_NIL) == (rhs.type == KC_EXPR_TYPE_NIL)) ^ invert);
				return lhs;
			}

			if (kc_expr_is_constant(lhs.type) && kc_expr_is_constant(rhs.type))
			{
				if ((lhs.type == KC_EXPR_TYPE_BOOL) != (rhs.type == KC_EXPR_TYPE_BOOL))
					goto error;
				if (lhs.type == KC_EXPR_TYPE_BOOL)
					lhs = kc_expr_boolean((lhs.integer == rhs.integer) ^ invert);
				if (lhs.type == KC_EXPR_TYPE_REAL || rhs.type == KC_EXPR_TYPE_REAL)
					lhs = kc_expr_boolean((kc_expr_to_real(lhs) == kc_expr_to_real(rhs)) ^ invert);
				lhs = kc_expr_boolean((lhs.integer == rhs.integer) ^ invert);
				return lhs;
			}
			break;
		}

		case KC_BINOP_LT:
		case KC_BINOP_GTE:
		{
			koji_bool invert = binop == KC_BINOP_GTE;
			if (lhs.type == KC_EXPR_TYPE_NIL)
			{
				lhs = kc_expr_boolean((rhs.type == KC_EXPR_TYPE_NIL) == invert);
				return lhs;
			}

			if (rhs.type == KC_EXPR_TYPE_NIL)
			{
				lhs = kc_expr_boolean((lhs.type == KC_EXPR_TYPE_NIL) != invert);
				return lhs;
			}

			if (kc_expr_is_constant(lhs.type) && kc_expr_is_constant(rhs.type))
			{
				if ((lhs.type == KC_EXPR_TYPE_BOOL) != (rhs.type == KC_EXPR_TYPE_BOOL))
					goto error;
				if (lhs.type == KC_EXPR_TYPE_BOOL)
					lhs = kc_expr_boolean((lhs.integer < rhs.integer) ^ invert);
				if (lhs.type == KC_EXPR_TYPE_REAL || rhs.type == KC_EXPR_TYPE_REAL)
					lhs = kc_expr_boolean((kc_expr_to_real(lhs) < kc_expr_to_real(rhs)) ^ invert);
				lhs = kc_expr_boolean((lhs.integer < rhs.integer) ^ invert);
				return lhs;
			}
			break;
		}

		case KC_BINOP_LTE:
		case KC_BINOP_GT:
		{
			koji_bool invert = binop == KC_BINOP_GT;
			if (lhs.type == KC_EXPR_TYPE_NIL)
			{
				lhs = kc_expr_boolean((rhs.type == KC_EXPR_TYPE_NIL) == invert);
				return lhs;
			}

			if (rhs.type == KC_EXPR_TYPE_NIL)
			{
				lhs = kc_expr_boolean((lhs.type == KC_EXPR_TYPE_NIL) != invert);
				return lhs;
			}

			if (kc_expr_is_constant(lhs.type) && kc_expr_is_constant(rhs.type))
			{
				if ((lhs.type == KC_EXPR_TYPE_BOOL) != (rhs.type == KC_EXPR_TYPE_BOOL))
					goto error;
				if (lhs.type == KC_EXPR_TYPE_BOOL)
					lhs = kc_expr_boolean((lhs.integer <= rhs.integer) ^ invert);
				if (lhs.type == KC_EXPR_TYPE_REAL || rhs.type == KC_EXPR_TYPE_REAL)
					lhs = kc_expr_boolean((kc_expr_to_real(lhs) <= kc_expr_to_real(rhs)) ^ invert);
				lhs = kc_expr_boolean((lhs.integer <= rhs.integer) ^ invert);
				return lhs;
			}
			break;
		}

		default: break;
	}

	// if we get here, lhs or rhs is a register, the binary operation instruction must be omitted.
	kc_expr lhs_reg = kc_expr_to_any_register(c, lhs, c->temporaries);

	// if lhs is using the current free register (example, it's constant that has been moved to the free register because its
	// index is too large), create a new state copy using the next register
	int old_temps = kc_use_temporary(c, &lhs_reg);

	kc_expr rhs_reg = kc_expr_to_any_register(c, rhs, c->temporaries);

	c->temporaries = old_temps;

	static const kc_expr_type COMPARISON_BINOP_TOKC_EXPR_TYPE_TYPE[] = {
		KC_EXPR_TYPE_EQ, KC_EXPR_TYPE_EQ, KC_EXPR_TYPE_LT, KC_EXPR_TYPE_LTE, KC_EXPR_TYPE_LTE, KC_EXPR_TYPE_LT
	};

	static const koji_bool BINOP_COMPARISON_TEST_VALUE[] = { KJ_TRUE, KJ_FALSE, KJ_TRUE, KJ_TRUE, KJ_FALSE, KJ_FALSE };

	if (binop >= KC_BINOP_EQ && binop <= KC_BINOP_GTE)
	{
		kc_expr_type etype = COMPARISON_BINOP_TOKC_EXPR_TYPE_TYPE[binop - KC_BINOP_EQ];
		koji_bool positive = BINOP_COMPARISON_TEST_VALUE[binop - KC_BINOP_EQ];
		lhs = kc_expr_comparison(etype, positive, lhs_reg.location, rhs_reg.location);
	}
	else
	{
		kc_emit(c, encode_ABC(binop - KC_BINOP_ADD + KJ_OP_ADD, es->target_register, lhs_reg.location, rhs_reg.location));
		lhs = kc_expr_register(es->target_register);
	}

	return lhs;

	// the binary operation between lhs and rhs is invalid
error:
	compile_error(c->lex->error_handler, sourceloc, "cannot make binary operation '%s' between values"
		" of type '%s' and '%s'.", KC_BINOP_TO_STR[binop], KC_EXPR_TYPE_TYPE_TO_STRING[lhs.type], KC_EXPR_TYPE_TYPE_TO_STRING[rhs.type]);
	return (kc_expr) { KC_EXPR_TYPE_NIL };

#undef MAKEBINOP
}

/**
* Parses and compiles the potential right hand side of a binary expression if binary operators
* are found.
*/
static kc_expr parse_binary_expression_rhs(kj_compiler *c, const kc_expr_state* es, kc_expr lhs, int precedence)
{
	static const int precedences[] = { -1, 1, 0, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 5 };

	for (;;)
	{
		// what's the lookahead operator?
		kc_binop binop = kc_token_to_binop(c->lex->lookahead);

		// and what is it's precedence?
		int tok_precedence = precedences[binop];

		// if the next operator precedence is lower than expression precedence (or next token is not an
		// operator) then we're done.
		if (tok_precedence < precedence) return lhs;

		// remember operator source location as the expression location
		kj_source_location sourceloc = c->lex->source_location;

		// todo explain this
		kc_compile_logical_operation(c, es, binop, lhs);

		// todo: compiel logical operation
		lex(c->lex);

		// if lhs is using the current free register, create a new state copy using the next register
		int old_temporary = kc_use_temporary(c, &lhs);

		kc_expr_state es_rhs = *es;
		es_rhs.target_register = c->temporaries;

		// compile the right-hand-side of the binary expression
		kc_expr rhs = kc_parse_primary_expression(c, &es_rhs);

		// look at the new operator precedence
		int next_binop_precedence = precedences[kc_token_to_binop(c->lex->lookahead)];

		// if next operator precedence is higher than current expression, then call recursively this function to
		// give higher priority to the next binary operation (pass our rhs as their lhs)
		if (next_binop_precedence > tok_precedence)
		{
			es_rhs = *es;
			es_rhs.true_jumps_begin = c->true_label.size;
			es_rhs.false_jumps_begin = c->false_label.size;
			rhs = parse_binary_expression_rhs(c, &es_rhs, rhs, tok_precedence + 1);
		}

		// subexpression has been evaluated, restore the free register to the one before compiling rhs.
		c->temporaries = old_temporary;

		/* compile the binary operation */
		lhs = _compile_binary_expression(c, es, sourceloc, binop, lhs, rhs);
	}
}

/**
* Parses a full expression and returns it. Note that the returned expr is not guaranteed to be in
* a register.
*/
static kc_expr kc_parse_expression(kj_compiler *c, const kc_expr_state* es)
{
	kc_expr_state my_es = *es;
	my_es.true_jumps_begin = c->true_label.size;
	my_es.false_jumps_begin = c->false_label.size;

	kj_source_location sourceloc = c->lex->source_location;
	koji_bool peek_identifier = c->lex->lookahead == tok_identifier;

	kc_expr lhs = kc_parse_primary_expression(c, &my_es);

	/* parse chain of assignments if any */
	if (kc_accept(c, '='))
	{
		if (!peek_identifier) goto error;

		switch (lhs.type)
		{
			case KC_EXPR_TYPE_REGISTER:
				if (lhs.location < 0 || !lhs.positive) goto error; // more checks for lvalue
				kc_move_expr_to_register(c, kc_parse_expression_to_any_register(c, c->temporaries), lhs.location);
				return lhs;

			default: goto error;
		}
	}

	return parse_binary_expression_rhs(c, &my_es, lhs, 0);

error:
	compile_error(c->lex->error_handler, sourceloc, "lhs of assignment is not an lvalue.");
	return KC_EXPR_NIL;
}

/**
* Parses and compiles an expression, then it makes sure that the final result is written to some
* register; it could be @target_hint or something else.
*/
static kc_expr kc_parse_expression_to_any_register(kj_compiler *c, int target_hint)
{
	instructions* instructions = &c->proto->instructions;

	// remember the number of instructions in the branches to true/false so that we can restore it when we return.
	uint true_label_size = c->true_label.size;
	uint false_label_size = c->false_label.size;
	int old_temps = c->temporaries;

	kc_expr_state es = { 0 };
	es.true_jumps_begin = true_label_size;
	es.false_jumps_begin = false_label_size;
	es.target_register = target_hint;

	kc_expr expr = kc_parse_expression(c, &es);

	uint rhs_move_jump_index = 0;
	koji_bool set_value_to_false = KJ_FALSE;
	koji_bool set_value_to_true = KJ_FALSE;
	koji_bool value_is_condition = kc_expr_is_comparison(expr.type);

	if (value_is_condition)
	{
		kc_emit(c, encode_ABC(KJ_OP_EQ + expr.type - KC_EXPR_TYPE_EQ, expr.lhs, expr.rhs, expr.positive));
		array_push_value(&c->true_label, uint, instructions->size);
		kc_emit(c, encode_ABx(KJ_OP_JUMP, 0, 0));
		set_value_to_false = KJ_TRUE;
	}
	else
	{
		expr = kc_expr_to_any_register(c, expr, target_hint);

		if (c->true_label.size <= true_label_size && c->false_label.size <= false_label_size)
			goto done;

		kc_move_expr_to_register(c, expr, target_hint);
		rhs_move_jump_index = instructions->size;
		kc_emit(c, encode_ABx(KJ_OP_JUMP, 0, 0));
	}

	// iterate over instructions that branch to false and if any is not a testset instruction, it means that we need to
	// emit a loadbool instruction to set the result to false, so for now just remember this by flagging set_value_to_false
	// to true
	for (uint i = false_label_size, size = c->false_label.size; i < size; ++i)
	{
		uint index = c->false_label.data[i];
		if (index > 0 && decode_op(instructions->data[index - 1]) != KJ_OP_TESTSET)
		{
			set_value_to_false = KJ_TRUE;
			replace_Bx(instructions->data + index, kc_offset_to_next_instruction(c, index));
		}
	}

	// if we need to set the result to false, emit the loadbool instruction (to false) now and remember its index so that
	// we can eventually patch it later
	size_t load_false_instruction_index = 0;
	if (set_value_to_false)
	{
		load_false_instruction_index = instructions->size;
		kc_emit(c, encode_ABC(KJ_OP_LOADB, target_hint, KJ_FALSE, 0));
	}

	// analogous to the false case, iterate over the list of instructions branching to true, flag set_value_to_true if
	// instruction is not a testset, as we'll need to emit a loadbool to true instruction in such case.
	// also patch all jumps to this point as the next instruction emitted could be the loadbool to true.
	for (uint i = true_label_size, size = c->true_label.size; i < size; ++i)
	{
		uint index = c->true_label.data[i];
		if (index > 0 && decode_op(instructions->data[index - 1]) != KJ_OP_TESTSET)
		{
			set_value_to_true = KJ_TRUE;
			replace_Bx(instructions->data + index, kc_offset_to_next_instruction(c, index));
		}
	}

	// emit the loadbool instruction to *true* if we need to
	if (set_value_to_true)
	{
		kc_emit(c, encode_ABC(KJ_OP_LOADB, target_hint, KJ_TRUE, 0));
	}

	// if we emitted a loadbool to *false* instruction before, we'll need to patch the jump offset to the current position
	// (after the eventual loadbool to *true* has been emitted)
	if (set_value_to_false)
	{
		replace_C(instructions->data + load_false_instruction_index, kc_offset_to_next_instruction(c, load_false_instruction_index));
	}

	// if the final subexpression was a register, check if we have added any loadb instruction. If so, set the right jump
	// offset to this location, otherwise pop the last instruction which is the "jump" after the "mov" or "neg" to skip
	// the loadb instructions.
	if (!value_is_condition)
	{
		if (!set_value_to_true && !set_value_to_false)
		{
			array_resize(instructions, sizeof(kj_instruction), instructions->size - 1);
		}
		else
		{
			replace_Bx(instructions->data + rhs_move_jump_index, kc_offset_to_next_instruction(c, rhs_move_jump_index));
		}
	}

	// finally set the jump offset of all remaining TESTSET instructions generated by the expression to true...
	for (uint i = true_label_size, size = c->true_label.size; i < size; ++i)
	{
		uint index = c->true_label.data[i];
		if (index > 0 && decode_op(instructions->data[index - 1]) == KJ_OP_TESTSET)
		{
			replace_Bx(instructions->data + index, kc_offset_to_next_instruction(c, index));
		}
	}

	// ...and to false to the next instruction.
	for (uint i = false_label_size, size = c->false_label.size; i < size; ++i)
	{
		uint index = c->false_label.data[i];
		if (index > 0 && decode_op(instructions->data[index - 1]) == KJ_OP_TESTSET)
		{
			replace_Bx(instructions->data + index, kc_offset_to_next_instruction(c, index));
		}
	}

	// restore the number of branches to what we found at the beginning.
	c->true_label.size = true_label_size;
	c->false_label.size = false_label_size;

	// final value is the requested target register.
	expr = kc_expr_register(target_hint);

done:
	c->temporaries = old_temps;
	return expr;
}

/**
* Parses and compiles an expression only focusing on emitting appropriate branching instructions
* to the true or false branch. The expression parsed & compiled is tested against @truth_value, i.e.
* if the expression is @truth_value (true or false) then it branches to *true*, otherwise to false.
*/
static void _parse_condition(kj_compiler *c, koji_bool test_value)
{
	instructions* instructions = &c->proto->instructions;
	int old_temps = c->temporaries;

	kc_expr_state es;
	es.true_jumps_begin = c->true_label.size;
	es.false_jumps_begin = c->false_label.size;
	es.negated = !test_value;
	es.target_register = c->temporaries;

	kc_expr expr = kc_parse_expression(c, &es);

	koji_bool value_is_condition = kc_expr_is_comparison(expr.type);

	if (value_is_condition)
	{
		kc_emit(c, encode_ABC(KJ_OP_EQ + expr.type - KC_EXPR_TYPE_EQ, expr.lhs, expr.rhs, expr.positive ^ !test_value));
	}
	else
	{
		expr = kc_expr_to_any_register(c, expr, c->temporaries);
		kc_emit(c, encode_ABx(KJ_OP_TEST, expr.location, test_value));
	}

	uint jump_to_true_index = instructions->size;
	array_push_value(&c->true_label, uint, jump_to_true_index);
	kc_emit(c, encode_ABx(KJ_OP_JUMP, 0, 0));

	c->temporaries = old_temps;
}

static void kc_parse_block_stmts(kj_compiler *c);

/**
* Parses and compiles an entire "{ stmts... }" block.
*/
static inline void kc_parse_block(kj_compiler *c)
{
	kc_expect(c, '{');
	kc_parse_block_stmts(c);
	kc_expect(c, '}');
}

/**
* Parses and compiles an if statement.
*/
static void kc_parse_if_stmt(kj_compiler *c)
{
	instructions* instructions = &c->proto->instructions;
	kc_expect(c, kw_if);

	uint true_label_begin = c->true_label.size;
	uint false_label_begin = c->false_label.size;

	// parse the condition to branch to 'true' if it's false.
	kc_expect(c, '(');
	_parse_condition(c, KJ_FALSE);
	kc_expect(c, ')');

	// bind the true branch (contained in the false label) and parse the true branch block.
	kc_bind_label_here(c, &c->false_label, false_label_begin);
	kc_parse_block(c);

	// check if there's a else block ahead
	if (kc_accept(c, kw_else))
	{
		// emit the jump from the true branch that will skip the elese block
		uint exit_jump_index = instructions->size;
		kc_emit(c, KJ_OP_JUMP);

		// bind the label to "else branch" contained in the true label (remember, we're compiling the condition to false,
		// so labels are s swapped).
		kc_bind_label_here(c, &c->true_label, true_label_begin);

		if (c->lex->lookahead == kw_if)
			kc_parse_if_stmt(c);
		else
			kc_parse_block(c);

		// patch the previous jump expression
		replace_Bx(instructions->data + exit_jump_index, kc_offset_to_next_instruction(c, exit_jump_index));
	}
	else
	{
		// just bind the exit branch
		kc_bind_label_here(c, &c->true_label, true_label_begin);
	}
}

/**
* Parses and compiles a while statement.
*/
static void kc_parse_while_stmt(kj_compiler *c)
{
	kc_expect(c, kw_while);

	uint true_label_begin = c->true_label.size;
	uint false_label_begin = c->false_label.size;
	uint first_condition_instruction = c->proto->instructions.size;

	// parse and compile the condition
	kc_expect(c, '(');
	_parse_condition(c, KJ_FALSE); // if condition is KJ_FALSE jump (to KJ_TRUE)
	kc_expect(c, ')');

	// compile the loop body
	kc_bind_label_here(c, &c->false_label, false_label_begin);

	// compile the loop body
	kc_parse_block(c);

	// jump back to the beginning of the loop, the first condition instruction.
	kc_emit(c, encode_ABx(KJ_OP_JUMP, 0, first_condition_instruction - c->proto->instructions.size - 1));

	// just bind the exit branch
	kc_bind_label_here(c, &c->true_label, true_label_begin);
}

/**
* Parses and compiles a do-while statement.
*/
static void kc_parse_do_while_stmt(kj_compiler *c)
{
	kc_expect(c, kw_do);

	uint true_label_begin = c->true_label.size;
	uint false_label_begin = c->false_label.size;
	uint first_body_instruction = c->proto->instructions.size;

	// compile the loop body
	kc_parse_block(c);

	// just bind the exit branch
	kc_bind_label_here(c, &c->true_label, true_label_begin);

	// parse and compile the condition
	kc_expect(c, kw_while);
	kc_expect(c, '(');
	_parse_condition(c, KJ_TRUE); // if condition is KJ_TRUE jump (to KJ_TRUE)
	kc_expect(c, ')');

	// bind the branches to true to the first body instruction
	kc_bind_label_to(c, &c->true_label, true_label_begin, first_body_instruction);

	// bind the exit label
	kc_bind_label_here(c, &c->false_label, false_label_begin);
}

/**
* Parses and compiles a single statement of any kind.
*/
static void kc_parse_statement(kj_compiler *c)
{
	switch (c->lex->lookahead)
	{
		case '{':
			lex(c->lex);
			kc_parse_block_stmts(c);
			kc_expect(c, '}');
			break;

		case kw_var:
			// eat var
			lex(c->lex);

			// push the identifier
			kc_check(c, tok_identifier);
			uint identifier_offset = kc_push_identifier(c, c->lex->token_string.data, c->lex->token_string.size);
			lex(c->lex);

			// parse initialization expression, if any
			if (kc_accept(c, '='))
			{
				kc_expr expr = kc_parse_expression_to_any_register(c, c->temporaries);
				kc_move_expr_to_register(c, expr, c->temporaries);
			}
			else
			{
				kc_emit(c, encode_ABx(KJ_OP_LOADNIL, c->temporaries, c->temporaries));
			}

			/* declare the variable */
			kc_define_local(c, identifier_offset);

			kc_expect_end_of_stmt(c);
			break;

		case kw_if:
			kc_parse_if_stmt(c);
			break;

		case kw_while:
			kc_parse_while_stmt(c);
			break;

		case kw_do:
			kc_parse_do_while_stmt(c);
			break;

		case kw_return:
		{
			lex(c->lex);
			int oldur = c->temporaries;
			do
			{
				kc_expr e = kc_parse_expression_to_any_register(c, c->temporaries);
				kc_move_expr_to_register(c, e, c->temporaries++);
			} while (kc_accept(c, ','));
			kc_emit(c, encode_ABx(KJ_OP_RET, oldur, c->temporaries));
			c->temporaries = oldur;
			kc_expect_end_of_stmt(c);
			break;
		}

		default: /* evaluate expression */
		{
			kc_parse_expression_to_any_register(c, c->temporaries);
			kc_expect_end_of_stmt(c);
		}
	}
}

/**
  * Parses and compiles the content of a block, i.e. the instructions between '{' and '}'.
	*/
static void kc_parse_block_stmts(kj_compiler *c)
{
	uint num_variables = c->locals.size;

	while (c->lex->lookahead != '}' && c->lex->lookahead != tok_eos)
		kc_parse_statement(c);

	c->locals.size = num_variables;
}

/**
  * Parses and compiles and entire module, i.e. a source file.
	*/
static void kc_parse_module(kj_compiler *c)
{
	while (c->lex->lookahead != tok_eos)
	{
		kc_parse_statement(c);
	}

	kc_emit(c, encode_ABx(KJ_OP_RET, 0, 0));

	prototype_dump(c->proto, 0, 0);
}

/**
  * It compiles a stream containing a source string to a koji module, reporting any error to specified
  * @error_handler (todo). This function is called by the koji_state in its load* functions.
	*/
static koji_prototype* compile(const char* source_name, koji_stream_reader_fn stream_func, void* stream_data,
	const kj_static_functions* local_variable)
{
	/* todo: make this part of the state */
	kj_error_handler e;
	e.reporter = default_error_report_fn;

	/* create a new module that will contain all instructions, constants, identifiers, and that all prototypes will refer to */
	koji_prototype* mainproto = malloc(sizeof(koji_prototype));
	*mainproto = (koji_prototype) { 0 };

	/* redirect the error handler jump buffer here so that we can cleanup the state. */
	if (setjmp(e.jmpbuf)) goto error;

	/* create and initialize the lexer */
	kj_lexer lexer;
	lex_init(&lexer, &e, source_name, stream_func, stream_data);

	/* setup the compilation state */
	kj_compiler state = { 0 };
	state.static_functions = local_variable;
	state.lex = &lexer;
	state.proto = mainproto;

	kc_parse_module(&state);

	goto cleanup;

error:
	//printf("%s\n", e.buffer);
	prototype_delete(mainproto);
	mainproto = NULL;

cleanup:
	array_destroy(&state.locals);
	array_destroy(&state.false_label);
	array_destroy(&state.true_label);
	array_destroy(&state.identifiers);
	lex_close(&lexer);
	return mainproto;
}

#pragma endregion

//---------------------------------------------------------------------------------------------------------------------
#pragma region State

static char ks_string_stream_reader(void* data)
{
	const char** stream = data;
	if (**stream)
		return *((*stream)++);
	return -1; // eof
}

static char ks_file_stream_reader(void* data)
{
	return (char)fgetc(data);
}

// A frame contains all the necessary information to run a script function (closure).
typedef struct ks_frame
{
	// The function prototype this frame is executing.
	koji_prototype* proto;

	// The program counter (current instruction index).
	uint pc;

	// Value stack base, i.e. the index of the first value in the stack for this frame.
	int stackbase;

} ks_frame;

struct koji_state
{
	jmp_buf errorjumpbuf;
	kj_static_functions static_host_functions;
	array_type(ks_frame) framestack;
	array_type(kj_value) valuestack;
	uint sp;
};

static void ks_error(koji_state* s, const char* format, ...)
{
	(void)s; // fixme
	va_list args;
	va_start(args, format);

	vprintf(format, args);
	printf("\n");
	fflush(stdout);

	va_end(args);
}

/* koji state functions (ks) */

/* operations */
static inline void ks_op_neg(kj_value* dest, const kj_value* src)
{
	switch (src->type)
	{
		case KJ_VALUE_NIL: value_set_boolean(dest, KJ_TRUE);
		case KJ_VALUE_BOOL: value_set_boolean(dest, !src->boolean);
		case KJ_VALUE_INT: value_set_boolean(dest, !src->integer);
		case KJ_VALUE_REAL: value_set_boolean(dest, !src->real);
		case KJ_VALUE_CLOSURE: value_set_boolean(dest, KJ_TRUE);
	}
}

static inline void ks_op_unm(koji_state* s, kj_value* dest, const kj_value* src)
{
	switch (src->type)
	{
		case KJ_VALUE_INT: value_set_integer(dest, -src->integer); break;
		case KJ_VALUE_REAL: value_set_real(dest, -src->real); break;

		case KJ_VALUE_NIL:
		case KJ_VALUE_BOOL:
		case KJ_VALUE_CLOSURE:
			ks_error(s, "cannot apply unary minus operation to a %s value.", KJ_VALUE_TYPE_STRING[src->type]);
	}
}

#define KS_BINOP(op)\
{\
	switch (lhs->type)\
	{\
			case KJ_VALUE_NIL: case KJ_VALUE_CLOSURE: goto error;\
			case KJ_VALUE_INT:\
				switch (rhs->type)\
				{\
					case KJ_VALUE_NIL: case KJ_VALUE_BOOL: case KJ_VALUE_CLOSURE: goto error;\
					case KJ_VALUE_INT: value_set_integer(dest, lhs->integer op rhs->integer); return;\
					case KJ_VALUE_REAL: value_set_real(dest, lhs->integer op rhs->real); return;\
				}\
			case KJ_VALUE_REAL:\
				switch (rhs->type)\
				{\
					case KJ_VALUE_NIL: case KJ_VALUE_BOOL: case KJ_VALUE_CLOSURE: goto error;\
					case KJ_VALUE_INT: value_set_real(dest, lhs->real op rhs->integer); return;\
					case KJ_VALUE_REAL: value_set_real(dest, lhs->real op rhs->real); return;\
				}\
			default: goto error;\
	}\
	error:\
	ks_error(s, "cannot apply binary operator '" #op "' between values of type %s and %s.",\
		KJ_VALUE_TYPE_STRING[lhs->type], KJ_VALUE_TYPE_STRING[rhs->type]);\
}

static inline void ks_binop_add(koji_state* s, kj_value* dest, const kj_value* lhs, const kj_value* rhs) { KS_BINOP(+) }
static inline void ks_binop_sub(koji_state* s, kj_value* dest, const kj_value* lhs, const kj_value* rhs) { KS_BINOP(-) }
static inline void ks_binop_mul(koji_state* s, kj_value* dest, const kj_value* lhs, const kj_value* rhs) { KS_BINOP(*) }
static inline void ks_binop_div(koji_state* s, kj_value* dest, const kj_value* lhs, const kj_value* rhs) { KS_BINOP(/ ) }

#undef KS_BINOP

static inline void ks_binop_mod(koji_state* s, kj_value* dest, const kj_value* lhs, const kj_value* rhs)
{
	switch (lhs->type)
	{
		case KJ_VALUE_INT:
			switch (lhs->type)
			{
				case KJ_VALUE_INT: value_set_integer(dest, lhs->integer + rhs->integer); return;
				default: goto error;
			}

		default: goto error;
	}

error:
	ks_error(s, "cannot apply binary operator '%' between values of type %s and %s.",
		KJ_VALUE_TYPE_STRING[lhs->type], KJ_VALUE_TYPE_STRING[rhs->type]);
}

static inline koji_bool value_to_bool(const kj_value* v)
{
	switch (v->type)
	{
		case KJ_VALUE_NIL: return KJ_FALSE;
		case KJ_VALUE_BOOL: return v->boolean;
		case KJ_VALUE_INT: return v->integer != 0;
		case KJ_VALUE_REAL: return v->real != 0;
		case KJ_VALUE_CLOSURE: return KJ_TRUE;
		default: assert(!"implement me");
	}
	return 0;
}

static inline koji_integer value_to_int(const kj_value* v)
{
	switch (v->type)
	{
		case KJ_VALUE_BOOL: return (koji_integer)v->boolean;
		case KJ_VALUE_INT: return v->integer;
		case KJ_VALUE_REAL: return (koji_integer)v->real;
		default: assert(!"implement me");
	}
	return 0;
}

static inline koji_real value_to_real(const kj_value* v)
{
	switch (v->type)
	{
		case KJ_VALUE_BOOL: return (koji_real)v->boolean;
		case KJ_VALUE_INT: return (koji_real)v->integer;
		case KJ_VALUE_REAL: return v->real;
		default: assert(!"implement me");
	}
	return 0;
}

static inline koji_bool ks_comp_eq(const kj_value* lhs, const kj_value* rhs)
{
	switch (lhs->type)
	{
		case KJ_VALUE_NIL: return rhs->type == KJ_VALUE_NIL;
		case KJ_VALUE_BOOL: return rhs->type == KJ_VALUE_BOOL && lhs->boolean == rhs->boolean;
		case KJ_VALUE_INT: return rhs->type == KJ_VALUE_REAL ? lhs->integer == rhs->real : lhs->integer == value_to_int(rhs);
		case KJ_VALUE_REAL: return lhs->real == value_to_real(rhs);
		default: assert(!"implement me");
	}
	return 0;
}

static inline koji_bool ks_comp_lt(const kj_value* lhs, const kj_value* rhs)
{
	switch (lhs->type)
	{
		case KJ_VALUE_NIL: return rhs->type != KJ_VALUE_NIL;
		case KJ_VALUE_BOOL: return lhs->boolean < value_to_int(rhs);
		case KJ_VALUE_INT: return lhs->integer < value_to_int(rhs);
		case KJ_VALUE_REAL: return lhs->real < value_to_real(rhs);
		default: assert(!"implement me");
	}
	return 0;
}

static inline koji_bool ks_comp_lte(const kj_value* lhs, const kj_value* rhs)
{
	switch (lhs->type)
	{
		case KJ_VALUE_NIL: return KJ_TRUE;
		case KJ_VALUE_BOOL: return lhs->boolean <= value_to_int(rhs);
		case KJ_VALUE_INT: return lhs->integer <= value_to_int(rhs);
		case KJ_VALUE_REAL: return lhs->real <= value_to_real(rhs);
		default: assert(!"implement me");
	}
	return 0;
}


static inline kj_value* ks_push(koji_state* s)
{
	array_reserve(&s->valuestack, sizeof(kj_value), s->sp + 1);
	return s->valuestack.data + s->sp++;
}

static inline kj_value* ks_top(koji_state* s, int offset)
{
	uint index = s->sp + offset;
	assert(index < (uint)s->sp && "offset out of stack bounds.");
	return s->valuestack.data + index;
}

static inline kj_value ks_pop(koji_state* s)
{
	kj_value* value = ks_top(s, -1);
	kj_value retvalue = *value;
	value->type = KJ_VALUE_NIL;
	--s->sp;
	return retvalue;
}

static inline kj_value* ks_get_register(koji_state* s, ks_frame* currframe, int location)
{
	return s->valuestack.data + currframe->stackbase + location;
}

static inline const kj_value* ks_get(koji_state* s, ks_frame* currframe, int location)
{
	return location >= 0 ? ks_get_register(s, currframe, location) :
		currframe->proto->constants.data - location - 1;
}

/* API functions */

koji_state* koji_create(void)
{
	koji_state* s = malloc(sizeof(koji_state));
	*s = (koji_state) { 0 };
	return s;
}

void koji_destroy(koji_state* s)
{
	/* destroy all value on the stack */
	for (uint i = 0; i < s->valuestack.size; ++i)
		value_set_nil(s->valuestack.data + i);

	array_destroy(&s->static_host_functions.functions);
	array_destroy(&s->static_host_functions.name_buffer);
	array_destroy(&s->framestack);
	array_destroy(&s->valuestack);

	free(s);
}

void koji_static_function(koji_state* s, const char* name, koji_user_function fn, int nargs)
{
	int sfunindex = static_functions_fetch(&s->static_host_functions, name, nargs);

	if (sfunindex > 0)
	{
		s->static_host_functions.functions.data[sfunindex].function = fn;
	}
	else
	{
		sfunindex = static_functions_fetch(&s->static_host_functions, name, -1);

		uint name_string_offset;
		if (sfunindex > 0)
		{
			name_string_offset = s->static_host_functions.functions.data[sfunindex].name_string_offset;
		}
		else
		{
			uint length = strlen(name);
			char* destname = array_push(&s->static_host_functions.name_buffer, char, length + 1);
			memcpy(destname, name, length + 1);
			name_string_offset = destname - s->static_host_functions.name_buffer.data;
		}

		kj_static_function* sf = array_push(&s->static_host_functions.functions, kj_static_function, 1);
		sf->name_string_offset = name_string_offset;
		sf->function = fn;
		sf->nargs = nargs;
	}
}

void koji_prototype_release(koji_prototype* p)
{
	if (--p->references == 0)
		prototype_delete(p);
}

int koji_load(koji_state* s, const char* source_name, koji_stream_reader_fn stream_fn, void* stream_data)
{
	koji_prototype* mainproto = compile(source_name, stream_fn, stream_data, &s->static_host_functions);

	if (!mainproto) return KOJI_RESULT_FAIL;

	/* create a closure to main prototype and push it to the stack */
	koji_push_nil(s);
	value_make_closure(ks_top(s, -1), mainproto);

	return KOJI_RESULT_OK;
}

int koji_load_string(koji_state* s, const char* source)
{
	return koji_load(s, "<user-string>", ks_string_stream_reader, (void*)&source);
}

int koji_load_file(koji_state* s, const char* filename)
{
	FILE* file = fopen(filename, "r");
	if (!file)
	{
		ks_error(s, "(fixme) cannot open file '%s'.", filename);
		return KOJI_RESULT_INVALID_FILE;
	}
	int r = koji_load(s, filename, ks_file_stream_reader, file);
	fclose(file);
	return r;
}

int koji_run(koji_state* s)
{
	if (s->sp == 0)
	{
		ks_error(s, "cannot run function on stack, stack is empty.");
		return KOJI_RESULT_FAIL;
	}

	kj_value top = ks_pop(s);

	if (top.type != KJ_VALUE_CLOSURE)
	{
		ks_error(s, "cannot run function on stack, top value is not closure.");
		return KOJI_RESULT_FAIL;
	}

	// push stack frame for closure
	ks_frame* frame = array_push(&s->framestack, ks_frame, 1);
	frame->proto = top.closure.proto;
	frame->pc = 0;
	frame->stackbase = s->sp;
	array_push(&s->valuestack, kj_value, frame->proto->ntemporaries);

	return koji_continue(s);
}

int koji_continue(koji_state* c)
{
newframe:
	assert(c->framestack.size > 0);
	ks_frame* f = c->framestack.data + c->framestack.size - 1;
	const kj_instruction* instructions = f->proto->instructions.data;

	for (;;)
	{
		kj_instruction ins = instructions[f->pc++];

#define KJXREGA ks_get_register(c, f, decode_A(ins))
#define KJXARG(X) ks_get(c, f, decode_##X(ins))

		switch (decode_op(ins))
		{
			case KJ_OP_LOADNIL:
				for (uint r = decode_A(ins), to = r + decode_Bx(ins); r < to; ++r)
					value_set_nil(ks_get_register(c, f, r));
				break;

			case KJ_OP_LOADB:
				value_set_boolean(KJXREGA, (koji_bool)decode_B(ins));
				f->pc += decode_C(ins);
				break;

			case KJ_OP_MOV:
				value_set(KJXREGA, KJXARG(Bx));
				break;

			case KJ_OP_NEG:
				ks_op_neg(KJXREGA, KJXARG(Bx));
				break;

			case KJ_OP_UNM:
				ks_op_unm(c, KJXREGA, KJXARG(Bx));
				break;

			case KJ_OP_ADD:
				ks_binop_add(c, KJXREGA, KJXARG(B), KJXARG(C));
				break;

			case KJ_OP_SUB:
				ks_binop_sub(c, KJXREGA, KJXARG(B), KJXARG(C));
				break;

			case KJ_OP_MUL:
				ks_binop_mul(c, KJXREGA, KJXARG(B), KJXARG(C));
				break;

			case KJ_OP_DIV:
				ks_binop_div(c, KJXREGA, KJXARG(B), KJXARG(C));
				break;

			case KJ_OP_MOD:
				ks_binop_mod(c, KJXREGA, KJXARG(B), KJXARG(C));
				break;

			case KJ_OP_POW:
				assert(0);
				break;

			case KJ_OP_TEST:
			{
				int newpc = f->pc + 1;
				if (value_to_bool(KJXREGA) == decode_Bx(ins))
					newpc += decode_Bx(instructions[f->pc]);
				f->pc = newpc;
				break;
			}

			case KJ_OP_TESTSET:
			{
				int newpc = f->pc + 1;
				const kj_value* arg = KJXARG(B);
				if (value_to_bool(arg) == decode_C(ins))
				{
					value_set(KJXREGA, arg);
					newpc += decode_Bx(instructions[f->pc]);
				}
				f->pc = newpc;
				break;
			}

			case KJ_OP_JUMP:
				f->pc += decode_Bx(ins);
				break;

			case KJ_OP_EQ:
			{
				int newpc = f->pc + 1;
				if (ks_comp_eq(KJXREGA, KJXARG(B)) == decode_C(ins))
				{
					newpc += decode_Bx(instructions[f->pc]);
				}
				f->pc = newpc;
				break;
			}

			case KJ_OP_LT:
			{
				int newpc = f->pc + 1;
				if (ks_comp_lt(KJXREGA, KJXARG(B)) == decode_C(ins))
				{
					newpc += decode_Bx(instructions[f->pc]);
				}
				f->pc = newpc;
				break;
			}

			case KJ_OP_LTE:
			{
				int newpc = f->pc + 1;
				if (ks_comp_lte(KJXREGA, KJXARG(B)) == decode_C(ins))
				{
					newpc += decode_Bx(instructions[f->pc]);
				}
				f->pc = newpc;
				break;
			}

			case KJ_OP_CLOSURE:
			{
				assert((uint)decode_Bx(ins) < f->proto->prototypes.size);
				value_make_closure(KJXREGA, f->proto->prototypes.data[decode_Bx(ins)]);
				break;
			}

			case KJ_OP_CALL:
			{
				const kj_value* value = KJXARG(B);
				if (value->type != KJ_VALUE_CLOSURE)
				{
					ks_error(c, "cannot call value of type %s.", KJ_VALUE_TYPE_STRING[value->type]);
					return KOJI_RESULT_FAIL; // temp
				}
				koji_prototype* proto = value->closure.proto;
				uint ncallargs = decode_C(ins);
				if (proto->nargs != ncallargs)
				{
					ks_error(c, "closure at (TODO) takes %d number of arguments (%d provided).", proto->nargs, ncallargs);
					return KOJI_RESULT_FAIL; // temp
				}
				++proto->references;
				ks_frame* frame = array_push(&c->framestack, ks_frame, 1);
				frame->proto = proto;
				frame->pc = 0;
				frame->stackbase = f->stackbase + decode_A(ins);
				array_push(&c->valuestack, kj_value, proto->ntemporaries);
				goto newframe;
			}

			case KJ_OP_SCALL:
			{
				const kj_static_function* sfun = c->static_host_functions.functions.data + decode_Bx(ins);
				uint sp = c->sp;
				c->sp = f->stackbase + decode_A(ins) + sfun->nargs;
				int nretvalues = sfun->function(c, sfun->nargs);
				if (nretvalues == 0) value_set_nil(ks_get_register(c, f, c->sp));
				c->sp = sp;
				break;
			}

			case KJ_OP_RET:
			{
				int i = 0;
				for (int reg = decode_A(ins), to_reg = decode_Bx(ins); reg < to_reg; ++reg, ++i)
					value_set(ks_get_register(c, f, i), ks_get(c, f, reg));
				for (int end = f->proto->nargs + f->proto->ntemporaries; i < end; ++i)
					value_set_nil(ks_get_register(c, f, i));
				koji_prototype_release(f->proto);
				--c->framestack.size;
				if (c->framestack.size == 0) return KOJI_RESULT_OK;
				goto newframe;
			}

			default:
				assert(0 && "unsupported op code");
				break;
		}

#undef KJREGA
#undef KJXARG
	}
}

void koji_push_nil(koji_state* s)
{
	ks_push(s)->type = KJ_VALUE_NIL;
}

void koji_push_bool(koji_state* s, koji_bool b)
{
	kj_value* value = ks_push(s);
	value->type = KJ_VALUE_BOOL;
	value->boolean = b;
}

void koji_push_int(koji_state* s, koji_integer i)
{
	kj_value* value = ks_push(s);
	value->type = KJ_VALUE_INT;
	value->integer = i;
}

void koji_push_real(koji_state* s, koji_real f)
{
	kj_value* value = ks_push(s);
	value->type = KJ_VALUE_REAL;
	value->real = f;
}

koji_bool koji_is_nil(koji_state* s, int offset)
{
	return ks_top(s, offset)->type == KJ_VALUE_NIL;
}

koji_bool koji_is_bool(koji_state* s, int offset)
{
	return ks_top(s, offset)->type == KJ_VALUE_BOOL;
}

koji_bool koji_is_int(koji_state* s, int offset)
{
	return ks_top(s, offset)->type == KJ_VALUE_INT;
}

koji_bool koji_is_real(koji_state* s, int offset)
{
	return ks_top(s, offset)->type == KJ_VALUE_REAL;
}

koji_integer koji_to_int(koji_state* s, int offset)
{
	return value_to_int(ks_top(s, offset));
}

koji_real koji_to_real(koji_state* s, int offset)
{
	return value_to_real(ks_top(s, offset));
}

void koji_pop(koji_state* s, int count)
{
	for (int i = 0; i < count; ++i)
	{
		kj_value val = ks_pop(s);
		value_destroy(&val);
	}
}

#pragma endregion

#ifdef __clang__ 
#pragma clang diagnostic pop
#endif

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
