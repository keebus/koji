/*
 * koji language - 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This is public domain software, read UNLICENSE for more information.
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
#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>

#ifndef _MSC_VER
#include <alloca.h>
#endif

#ifdef _MSC_VER 
#ifndef _DEBUG
#define inline __forceinline
#else
#define inline
#endif
#endif

#if defined(_MSC_VER) && _MSC_VER <= 1800
#define snprintf _snprintf_c
#endif

#ifdef _MSC_VER
#pragma warning(disable:4996)
#pragma 
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

#if defined(_WIN64) || defined(__amd64__)
#define KOJI_64
#else
#define KOJI_32
#endif

//-------------------------------------------------------------------------------------------------
#pragma region Support

/* global defines */
#define KJ_NULL 0

/* convenience typedefs */
typedef unsigned int uint;
typedef unsigned long long uint64;
typedef unsigned char ubyte;

/* Dynamic Array */

/* Generates the declaration for a dynamic array of type @type */
#define array_type(type) struct { uint capacity; uint size; type* data; }

/* Generic definition of a dynamic array that points to void* */
typedef array_type(void) void_array;

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
		assert(new_data != KJ_NULL);
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
* then the function simply returns false. If the array instead is too small, the function
* allocates a new buffer large enough to contain @new_size, copies the old buffer to the new one,
* destroys the old buffer and returns true.
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
	printf("%s\n", message);
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
* specified handler @e.ko
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
* Reports the specified error message and jumps to the error handler code.
*/
static void compiler_error(kj_error_handler *e, kj_source_location sl, const char *format, ...)
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
	kw_globals,
	kw_for,
	kw_if,
	kw_in,
	kw_return,
	kw_this,
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
	char* token_string;
	uint token_string_length;
	uint token_string_capacity;
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
	if (l->token_string_length + 2 > l->token_string_capacity)
	{
		char* temp = malloc(l->token_string_capacity * 2);
		memcpy(temp, l->token_string, l->token_string_length + 1);
		free(l->token_string);
		l->token_string = temp;
	}

	l->token_string[l->token_string_length++] = l->curr_char;
	l->token_string[l->token_string_length] = '\0';

	return _lex_skip(l);
}

static inline void _lex_clear_token_string(kj_lexer *l)
{
	l->token_string_length = 0;
	l->token_string[0] = '\0';
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
		return true;
	}
	return false;
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
		first_char = false;
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
	case kw_globals: return "globals";
	case kw_for: return "for";
	case kw_if: return "if";
	case kw_in: return "in";
	case kw_return: return "return";
	case kw_this: return "this";
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
		return l->token_string;
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
		koji_bool decimal = false;
		switch (l->curr_char)
		{
		case EOF:
			return tok_eos;

		case '\n':
			l->newline = true;

		case ' ': case '\r': case '\t':
			_lex_skip(l);
			break;

		case ',': case ';': case ':': case '(': case ')': case '[': case ']': case '{': case '}':
			l->lookahead = l->curr_char;
			_lex_push(l);
			return l->lookahead;

			/* strings */
		case '"':
		case '\'':
		{
			char delimiter = l->curr_char;
			_lex_skip(l);
			while (l->curr_char != EOF && l->curr_char != delimiter)
			{
				_lex_push(l);
			}
			if (l->curr_char != delimiter)
			{
				compiler_error(l->error_handler, l->source_location, "end-of-stream while scanning string.");
				return l->lookahead = tok_eos;
			}
			_lex_skip(l);
			return l->lookahead = tok_string;
		}

			{
		case '.':
			decimal = true;
			_lex_push(l);
			if (l->curr_char < '0' || l->curr_char > '9') return l->lookahead = '.';

		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			l->lookahead = decimal ? tok_real : tok_integer;

			if (!decimal)
			{
				// First sequence of numbers before optional dot.
				while (l->curr_char >= '0' && l->curr_char <= '9')
					_lex_push(l);

				if (l->curr_char == '.')
				{
					_lex_push(l);
					decimal = true;
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
				decimal = true;
				_lex_push(l);
				while (l->curr_char >= '0' && l->curr_char <= '9')
					_lex_push(l);
			}

			if (decimal)
			{
				l->token_real = (koji_real)atof(l->token_string);
			}
			else
			{
				char *dummy;
				l->token_int = (koji_integer)strtoll(l->token_string, &dummy, 10);
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
			return l->lookahead = _lex_scan_identifier(l, false);

		case 'e':
			_lex_push(l);
			l->lookahead = tok_identifier;
			if (_lex_accept(l, "lse")) l->lookahead = kw_else;
			return l->lookahead = _lex_scan_identifier(l, false);

		case 'f':
			_lex_push(l);
			l->lookahead = tok_identifier;
			switch (l->curr_char)
			{
			case 'a': _lex_push(l); if (_lex_accept(l, "lse")) l->lookahead = kw_false; break;
			case 'o': _lex_push(l); if (_lex_accept(l, "r"))   l->lookahead = kw_for; break;
			}
			return l->lookahead = _lex_scan_identifier(l, false);

		case 'g':
			_lex_push(l);
			l->lookahead = tok_identifier;
			if (_lex_accept(l, "lobals")) l->lookahead = kw_globals;
			return l->lookahead = _lex_scan_identifier(l, false);

		case 'i':
			_lex_push(l);
			l->lookahead = tok_identifier;
			switch (l->curr_char)
			{
			case 'f': _lex_push(l); l->lookahead = kw_if; break;
			case 'n': _lex_push(l); l->lookahead = kw_in; break;
			}
			return l->lookahead = _lex_scan_identifier(l, false);

		case 'r':
			_lex_push(l);
			l->lookahead = tok_identifier;
			if (_lex_accept(l, "eturn")) l->lookahead = kw_return;
			return l->lookahead = _lex_scan_identifier(l, false);

		case 't':
			_lex_push(l);
			l->lookahead = tok_identifier;
			switch (l->curr_char)
			{
				case 'h': _lex_push(l); if (_lex_accept(l, "is")) l->lookahead = kw_this; break;
				case 'r': _lex_push(l); if (_lex_accept(l, "ue")) l->lookahead = kw_true; break;
			}
			return l->lookahead = _lex_scan_identifier(l, false);

		case 'v':
			_lex_push(l);
			l->lookahead = tok_identifier;
			if (_lex_accept(l, "ar")) l->lookahead = kw_var;
			return l->lookahead = _lex_scan_identifier(l, false);

		case 'w':
			_lex_push(l);
			l->lookahead = tok_identifier;
			if (_lex_accept(l, "hile")) l->lookahead = kw_while;
			return l->lookahead = _lex_scan_identifier(l, false);

		default:
			_lex_scan_identifier(l, true);
			if (l->lookahead != tok_identifier)
				compiler_error(l->error_handler, l->source_location, "unexpected character '%c' found.", l->curr_char);
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
	l->token_string_capacity = 16;
	l->token_string = malloc(l->token_string_capacity);
	l->token_string_length = 0;
	l->source_location.filename = filename;
	l->source_location.line = 1;
	l->source_location.column = 0;
	l->newline = 0;
	l->curr_char = 0;

	_lex_skip(l);
	lex(l);
}

/**
* Deinitializes an initialized lexer instance destroying its resources.
*/
static void lex_close(kj_lexer *l)
{
	free(l->token_string);
}

#pragma endregion

//---------------------------------------------------------------------------------------------------------------------
#pragma region Bytecode

/** Enumeration that lists all Virtual Machine opcodes */
typedef enum kj_opcode
{
	/* operations that write into R(A) */
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
	KJ_OP_GLOBALS, /* globals A         ; get the global table into register A */
	KJ_OP_NEWTABLE, /* newtable A       ; creates a new table in R(A) */
	KJ_OP_GET,      /* get A, B, C ; R(A) = R(B)[R(C)] */
	KJ_OP_THIS,     /* this A           ; R(A) = this */

	/* operations that do not write into R(A) */
	KJ_OP_TEST,    /* test A, Bx        ; if (bool)R(A) != (bool)B then jump 1 */
	KJ_OP_JUMP,    /* jump Bx           ; jump by Bx instructions */
	KJ_OP_EQ,      /* eq A, B, C        ; if (R(A) == R(B)) == (bool)C then nothing else jump 1 */
	KJ_OP_LT,      /* lt A, B, C        ; if (R(A) < R(B)) == (bool)C then nothing else jump 1 */
	KJ_OP_LTE,     /* lte A, B, C       ; if (R(A) <= R(B)) == (bool)C then nothing else jump 1 */
	KJ_OP_SCALL,   /* scall A, B, C     ; call static function at K[B) with C arguments starting from R(A) */
	KJ_OP_RET,     /* ret A, B          ; return values R(A), ..., R(B)*/
	KJ_OP_SET,     /* set A, B, C       ; R(A)[R(B)] = R(C) */
	KJ_OP_CALL,    /* call A, B, C      ; call closure R(B) with C arguments starting at R(A) */
	KJ_OP_MCALL,   /* mcall A, B, C     ; call object R(A - 1) method with name R(B) with C arguments from R(A) on */
} kj_opcode;

static const char *KJ_OP_STRINGS[] = {
	"loadnil", "loadb", "mov", "neg", "unm", "add", "sub", "mul", "div", "mod", "pow", "testset", "closure",
	"globals", "newtable", "get", "this", "test", "jump", "eq", "lt", "lte", "scall", "ret", "set", "call", "mcall"
};

/* Instructions */

/** Type of a single instruction, always a 32bit long */
typedef uint32_t kj_instruction;

/** Maximum value a register can hold (positive or negative) */
static const uint MAX_REGISTER_VALUE = 255;

/** Maximum value Bx can hold (positive or negative) */
static const int MAX_BX_INTEGER = 131071;

/** Returns whether opcode @op involves writing into register A. */
static inline koji_bool opcode_has_target(kj_opcode op) { return op <= KJ_OP_THIS; }

/** Encodes an instruction with arguments A and Bx. */
static inline kj_instruction encode_ABx(kj_opcode op, int A, int Bx)
{
	assert(A >= 0);
	assert(abs(Bx) <= MAX_BX_INTEGER);
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
* A prototype is a script function descriptor. It contains function instructions, constants and properties.
* Prototypes can contain other nested prototypes in a tree structure. Prototypes are reference counted and
* closures hold a reference to their prototype, the VM holds a reference to prototypes in the frame stack and
* parent prototypes hold a reference of their children.
*/
typedef struct kj_prototype kj_prototype;

/**
* TODO
*/
typedef struct kj_closure
{
	kj_prototype* proto;
}
kj_closure;

static const char *KJ_VALUE_TYPE_STRING[] = { "nil", "bool", "int", "real", "string", "table", "closure" };

typedef struct kj_string
{
	uint references;
	uint length;
	char* data;
} kj_string;

typedef struct kj_table_entry kj_table_entry;

typedef struct kj_table
{
	uint capacity;
	uint size;
	struct kj_table_entry* entries;
} kj_table;

#define KJ_TABLE_DEFAULT_CAPACITY 10

#ifdef KOJI_64
typedef uint64_t hash_t;
#else
typedef uint32_t hash_t;
#endif

typedef struct kj_value_table
{
	uint references;
	kj_table table;
	struct kj_value_table* metatable;
} kj_value_table;

/* Definition */
struct kj_value
{
	koji_type type;
	union
	{
		koji_bool       boolean;
		koji_integer    integer;
		koji_real       real;
		kj_string*      string;
		kj_value_table* table;
		void*           object;
		kj_closure      closure; // fixme
	};
};

struct kj_table_entry
{
	kj_value key;
	kj_value value;
};

/* helper macro to access a string object in a string value */

/** Dynamic array of instructions. */
typedef array_type(kj_instruction) instructions;

/* Definition */
struct kj_prototype
{
	uint references;
	ubyte nargs;
	ubyte ntemporaries;
	array_type(kj_value) constants;
	instructions instructions;
	array_type(kj_prototype*) prototypes;
};

/**
* (internal) Destroys prototype @p resources and frees its memory.
*/
static void prototype_delete(kj_prototype* p);

/**
* Frees a reference to prototype @p and deletes it if zero.
*/

static inline void prototype_release(kj_prototype* p)
{
	if (--p->references == 0)
		prototype_delete(p);
}

/* kj_value functions */

static void table_init(kj_table* table, size_t capacity);
static void table_destruct(kj_table* table);

static inline void _value_table_destroy(kj_value_table* vt)
{
	if (vt->references-- == 1)
	{
		if (vt->metatable) _value_table_destroy(vt->metatable);
		table_destruct(&vt->table);
		free(vt);
	}
}

static inline void value_destroy(kj_value* v)
{
	switch (v->type)
	{
	case KOJI_TYPE_NIL:
	case KOJI_TYPE_BOOL:
	case KOJI_TYPE_INT:
	case KOJI_TYPE_REAL:
		break;

	case KOJI_TYPE_STRING:
	{
		uint* references = (uint*)v->object;
		if ((*references)-- == 1)
			free(v->object);
		break;
	}

	case KOJI_TYPE_TABLE:
		_value_table_destroy(v->table);
		break;

	case KOJI_TYPE_CLOSURE:
		/* check whether this closure was the last reference to the prototype module, if so destroy the module */
		prototype_release(v->closure.proto);
		break;

	default: break;
	}
}

static inline void value_set_nil(kj_value* v)
{
	value_destroy(v);
	v->type = KOJI_TYPE_NIL;
}

static inline void value_set_boolean(kj_value* v, koji_bool boolean)
{
	value_destroy(v);
	v->type = KOJI_TYPE_BOOL;
	v->boolean = boolean;
}

static inline void value_set_integer(kj_value* v, koji_integer integer)
{
	value_destroy(v);
	v->type = KOJI_TYPE_INT;
	v->integer = integer;
}

static inline void value_set_real(kj_value* v, koji_real real)
{
	value_destroy(v);
	v->type = KOJI_TYPE_REAL;
	v->real = real;
}

static void value_new_string(kj_value* v, uint length)
{
	value_destroy(v);
	v->type = KOJI_TYPE_STRING;
	v->string = malloc(sizeof(kj_string) + length + 1);
	v->string->references = 1;
	v->string->length = 0;
	v->string->data = (char*)v->string + sizeof(kj_string);
}

static inline void value_set_table(kj_value* v, kj_value_table* table)
{
	value_destroy(v);
	v->table = table;
	++v->table->references;
}

static inline void value_new_table(kj_value* v)
{
	value_destroy(v);
	v->type = KOJI_TYPE_TABLE;
	v->table = malloc(sizeof(kj_value_table));
	v->table->references = 1;
	v->table->metatable = KJ_NULL;
	table_init(&v->table->table, KJ_TABLE_DEFAULT_CAPACITY);
}

static inline void value_new_closure(kj_value* v, kj_prototype* proto)
{
	value_destroy(v);
	++proto->references;
	v->type = KOJI_TYPE_CLOSURE;
	v->closure = (kj_closure) { proto };
}

static inline void value_set(kj_value* dest, const kj_value* src)
{
	if (dest == src) return;

	value_destroy(dest);
	*dest = *src;

	/* fixme */
	switch (dest->type)
	{
		case KOJI_TYPE_STRING:
		case KOJI_TYPE_TABLE:
			++*(uint*)(dest->object); /* bump up the reference */
			break;

		case KOJI_TYPE_CLOSURE: /* fixme */
			++dest->closure.proto->references;
			break;

		default: break;
	}
}

/* conversions */
static inline koji_bool value_to_bool(const kj_value* v)
{
	switch (v->type)
	{
		case KOJI_TYPE_NIL: return false;
		case KOJI_TYPE_BOOL: return v->boolean;
		case KOJI_TYPE_INT: return v->integer != 0;
		case KOJI_TYPE_REAL: return v->real != 0;
		case KOJI_TYPE_CLOSURE: return true;
		default: assert(!"implement me");
	}
	return 0;
}

static inline koji_integer value_to_int(const kj_value* v)
{
	switch (v->type)
	{
	case KOJI_TYPE_BOOL: return (koji_integer)v->boolean;
	case KOJI_TYPE_INT: return v->integer;
	case KOJI_TYPE_REAL: return (koji_integer)v->real;
	default: assert(!"implement me");
	}
	return 0;
}

static inline koji_real value_to_real(const kj_value* v)
{
	switch (v->type)
	{
	case KOJI_TYPE_BOOL: return (koji_real)v->boolean;
	case KOJI_TYPE_INT: return (koji_real)v->integer;
	case KOJI_TYPE_REAL: return v->real;
	default: assert(!"implement me");
	}
	return 0;
}

/* table functions */
static inline hash_t rehash(hash_t k)
{
#ifdef KOJI_64
	k ^= k >> 33;
	k *= 0xff51afd7ed558ccdLLU;
	k ^= k >> 33;
	k *= 0xc4ceb9fe1a85ec53LLU;
	k ^= k >> 33;
#else
	/* Thomas Wang's */
	k = (k ^ 61) ^ (k >> 16);
	k = k + (k << 3);
	k = k ^ (k >> 4);
	k = k * 0x27d4eb2d;
	k = k ^ (k >> 15);
#endif
	return k;
}

static hash_t value_hash(kj_value value)
{
	hash_t h;
	switch (value.type)
	{
		case KOJI_TYPE_NIL: h = 0; break;
		case KOJI_TYPE_STRING:
			h = 5381;
			for (char* ch = value.string->data; *ch; ++ch)
				h = ((h << 5) + h) + *ch; /* hash * 33 + c */
			return h;
		default: h = (hash_t)value.object; break;
	}
	hash_t k = value.type == KOJI_TYPE_NIL ? 0 : (hash_t)value.object;
	k ^= 1ULL << value.type;
	return rehash(k);
}

static koji_bool value_equals(kj_value const* lhs, kj_value const* rhs)
{
	if (lhs == rhs) return true;
	if (lhs->type != rhs->type) return false;
	if (lhs->type == KOJI_TYPE_NIL) return true;
	if (lhs->type == KOJI_TYPE_STRING)
		return lhs->string->length == rhs->string->length && !memcmp(lhs->string->data, rhs->string->data, lhs->string->length);
	return lhs->object == rhs->object;
}

static void table_init(kj_table* table, size_t capacity)
{
	table->size = 0;
	table->capacity = capacity;
	table->entries = malloc(sizeof(kj_table_entry) * capacity);
	memset(table->entries, 0, sizeof(kj_table_entry) * table->capacity);
}

static void table_destruct(kj_table* table)
{
	/* destroy key-value pairs */
	kj_table_entry* entries = table->entries;

	for (uint i = 0; i < table->capacity; ++i)
	{
		value_destroy(&entries[i].key);
		value_destroy(&entries[i].value);
	}

	/* free the table memory */
	free(table->entries);
}

static kj_table_entry* _table_find_entry(kj_table* table, kj_value const* key)
{
	kj_table_entry* entries = table->entries;
	hash_t hash = value_hash(*key);
	uint64 index = hash % table->capacity;

	while (entries[index].key.type != KOJI_TYPE_NIL && !value_equals(&entries[index].key, key))
	{
		index = (index + 1) % table->capacity;
	}

	return &entries[index];
}

static koji_bool _table_reserve(kj_table* table, size_t size)
{
	if (size == 0 || size <= table->capacity * 8 / 10) return false;

	size_t old_capacity = table->capacity;
	size_t new_capacity;

	/* compute new capacity */
	if (old_capacity == 0)
	{
		size_t double_size = size * 2;
		new_capacity = 10u > double_size ? 10u : double_size;
	}
	else
	{
		new_capacity = table->capacity;
		while (size > new_capacity * 8 / 10) new_capacity *= 2;
	}

	/* create the new type with the new capacity */
	kj_table_entry* old_entries = table->entries;
	table->capacity = new_capacity;
	table->entries = malloc(sizeof(kj_table_entry) * new_capacity);

	/* rehash */
	for (size_t i = 0; i < old_capacity; ++i)
	{
		if (old_entries[i].key.type != KOJI_TYPE_NIL)
		{
			kj_table_entry* entry = _table_find_entry(table, &old_entries[i].key);
			assert(entry->key.type == KOJI_TYPE_NIL);
			entry->key = old_entries[i].key;
			entry->value = old_entries[i].value;
		}
	}

	/* free old table without calling value destructors */
	free(old_entries);

	return true;
}

static koji_bool table_set(kj_table* table, kj_value const* key, kj_value const* value)
{
	assert(key->type != KOJI_TYPE_NIL && "Nil values cannot be used as table keys.");

	/* if capacity is zero (new hash-table) reserve at least the space for one element */
	_table_reserve(table, table->size + 1);

	/* find the right entry for given value in the table*/
	kj_table_entry* entry = _table_find_entry(table, key);

	if (entry->key.type != KOJI_TYPE_NIL)
	{
		/* value already in the table, update the entry with the new value (which could be different if
		* user specified a custom == operator) */
		value_set(&entry->value, value);

		/* no entry added */
		return false;
	}

	/* value's not in the table, can't use the move assignment, need to create the value */
	value_set(&entry->key, key);
	value_set(&entry->value, value);

	/* a new item has been added to the hash table */
	++table->size;

	return true;
}

static kj_value* table_get(kj_table* table, kj_value const* key)
{
	assert(key->type != KOJI_TYPE_NIL && "Nil values cannot be used as table keys.");

	/* find the right entry for given value in the table*/
	kj_table_entry* entry = _table_find_entry(table, key);

	/* return null if the key is nil, i.e. key not found */
	return &entry->value;
}

/**
* (internal) Destroys prototype @p resources and frees its memory.
*/
static void prototype_delete(kj_prototype* p)
{
	assert(p->references == 0);
	array_destroy(&p->instructions);

	/* delete all child prototypes that reach reference to zero */
	for (uint i = 0; i < p->prototypes.size; ++i)
		prototype_release(p->prototypes.data[i]);
	array_destroy(&p->prototypes);

	/* destroy constant values */
	for (uint i = 0; i < p->constants.size; ++i)
		value_destroy(p->constants.data + i);
	array_destroy(&p->constants);

	free(p);
}

/**
* Prints prototype information and instructions for compilation debugging.
*/
static void prototype_dump(kj_prototype* p, int level, int index)
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
		case KJ_OP_NEWTABLE:
			printf("%s\t%d\t", opstr, A);
			break;

		case KJ_OP_MOV: case KJ_OP_NEG: case KJ_OP_LOADNIL: case KJ_OP_RET:
			printf("%s\t\t%d, %d\t", opstr, A, Bx);
			goto print_constant;

		case KJ_OP_ADD: case KJ_OP_SUB: case KJ_OP_MUL: case KJ_OP_DIV: case KJ_OP_MOD: case KJ_OP_POW: case KJ_OP_CALL:
		case KJ_OP_SCALL:
			printf("%s\t\t%d, %d, %d", opstr, A, B, C);
			Bx = C;
			goto print_constant;

		case KJ_OP_SET: case KJ_OP_GET: case KJ_OP_MCALL:
			printf("%s\t%d, %d, %d", opstr, A, B, C);
			Bx = C;

		print_constant:
			if (Bx < 0)
			{
				printf("\t; ");
				kj_value k = p->constants.data[-Bx - 1];
				switch (k.type)
				{
				case KOJI_TYPE_INT: printf("%lld", (long long int)k.integer); break;
				case KOJI_TYPE_REAL: printf("%.3f", k.real); break;
				case KOJI_TYPE_STRING: printf("\"%s\"", k.string->data); break;
				default: assert(0);
				}
			}
			break;

		case KJ_OP_GLOBALS: case KJ_OP_THIS:
			printf("%s\t%d", opstr, A);
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
			printf("%s\t%d, %d", opstr, A, Bx);
			break;

		default:
			assert(false);
			break;
		}

		printf("\n");
	}

	for (uint i = 0; i < p->prototypes.size; ++i)
		prototype_dump(p->prototypes.data[i], level + 1, i);
}

/**
* A static function is a C function registered by the user *before* compiling scripts. These
* should be low level library functions that might are called often (e.g. sqrt).
*/
typedef struct kj_static_function
{
	uint name_string_offset;
	koji_user_function function;
	unsigned short min_num_args;
	unsigned short max_num_args;
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
* Searches for a static function with name @name in @fns and returns its index in
* fns->functions if found, -1 otherwise.
*/
static int static_functions_fetch(kj_static_functions const* fns, const char* name)
{
	for (uint i = 0; i < fns->functions.size; ++i)
	{
		kj_static_function* fn = fns->functions.data + i;
		if (strcmp(name, fns->name_buffer.data + fn->name_string_offset) == 0)
			return i;
	}
	return -1;
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


#pragma region Allocator

typedef struct kj_allocation_page
{
	struct kj_allocation_page* next;
	uint size;
	char* cursor;
} kj_allocation_page;

#define KJ_ALLOCATION_PAGE_MIN_SIZE 1024

static inline char* _align(char* ptr, uint alignment)
{
	const uint alignment_minus_one = alignment - 1;
	return (char*)(((uintptr_t)ptr + alignment_minus_one) & ~alignment_minus_one);
}

static inline char* _allocator_page_buffer(kj_allocation_page* page)
{
	return (char*)page + sizeof(kj_allocation_page);
}

static kj_allocation_page* allocator_page_create(uint size)
{
	size = size < KJ_ALLOCATION_PAGE_MIN_SIZE ? KJ_ALLOCATION_PAGE_MIN_SIZE : size;
	kj_allocation_page* page = malloc(sizeof(kj_allocation_page) + size);
	page->size = size;
	page->next = KJ_NULL;
	page->cursor = _allocator_page_buffer(page);
	return page;
}

static void allocator_destroy(kj_allocation_page* head)
{
	while (head)
	{
		kj_allocation_page* temp = head->next;
		free(head);
		head = temp;
	}
}

static void allocator_reset(kj_allocation_page** head)
{
	allocator_destroy((*head)->next);
	(*head)->next = KJ_NULL;
	(*head)->cursor = _allocator_page_buffer(*head);
}

static void* allocator_alloc(kj_allocation_page** head, uint size, uint alignment)
{
	char* ptr = KJ_NULL;
	if (*head && (ptr = _align((*head)->cursor, alignment)) >= _allocator_page_buffer(*head) + (*head)->size)
	{
		kj_allocation_page* temp = allocator_page_create(size);
		temp->next = *head;
		*head = temp;
		ptr = _align(temp->cursor, alignment);
	}
	(*head)->cursor = ptr + size;
	return ptr;
}

#pragma endregion

/**
* Holds the state of a compilation execution.
*/
typedef struct kj_compiler
{
	kj_allocation_page* allocator;
	kj_static_functions const* static_functions;
	kj_lexer* lex;
	kj_prototype* proto;
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
static void kc_syntax_error(kj_compiler* c, kj_source_location sourceloc)
{
	compiler_error(c->lex->error_handler, sourceloc, "unexpected %s.", lex_lookahead_to_string(c->lex));
}

/**
* Scans next token if lookahead is @tok. Returns whether a new token was scanned.
*/
static inline koji_bool kc_accept(kj_compiler* c, kj_token tok)
{
	if (c->lex->lookahead == tok)
	{
		lex(c->lex);
		return true;
	}
	return false;
}

/**
* Reports a compilation error if lookhead differs from @tok.
*/
static inline void kc_check(kj_compiler* c, kj_token tok)
{
	if (c->lex->lookahead != tok)
	{
		char token_string_buffer[64];
		compiler_error(c->lex->error_handler, c->lex->source_location, "missing %s before '%s'.",
			lex_token_to_string(tok, token_string_buffer, 64), lex_lookahead_to_string(c->lex));
	}
}

/**
* Checks that lookahead is @tok then scans next token.
*/
static inline void kc_expect(kj_compiler* c, kj_token tok)
{
	kc_check(c, tok);
	lex(c->lex);
}

/**
* Returns an "end of statement" token is found (newline, ';', '}' or end-of-stream) and "eats" it.
*/
static inline koji_bool kc_accept_end_of_stmt(kj_compiler* c)
{
	if (kc_accept(c, ';')) return true;
	if (c->lex->lookahead == '}' || c->lex->lookahead == tok_eos) return true;
	if (c->lex->newline) { c->lex->newline = false; return true; }
	return false;
}

/**
* Expects an end of statement.
*/
static inline void kc_expect_end_of_stmt(kj_compiler* c)
{
	if (!kc_accept_end_of_stmt(c)) kc_syntax_error(c, c->lex->source_location);
}

/* Constants management */

/**
* Searches a constant in value @k and adds it if not found, then it returns its index.
*/
static inline int _kc_fetch_primitive_constant(kj_compiler* c, kj_value k)
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

/**
* Fetches or defines if not found an int constant @k and returns its index.
*/
static inline int kc_fetch_constant_int(kj_compiler* c, koji_integer k)
{
	return _kc_fetch_primitive_constant(c, (kj_value) { KOJI_TYPE_INT, .integer = k });
}

/**
* Fetches or defines if not found a real constant @k and returns its index.
*/
static inline int kc_fetch_constant_real(kj_compiler* c, koji_real k)
{
	return _kc_fetch_primitive_constant(c, (kj_value) { KOJI_TYPE_REAL, .real = k });

}

/**
* Fetches or defines if not found a string constant @k and returns its index.
*/
static inline int kc_fetch_constant_string(kj_compiler* c, const char* k)
{
	for (uint i = 0; i < c->proto->constants.size; ++i)
	{
		kj_value* constant = c->proto->constants.data + i;
		if (constant->type == KOJI_TYPE_STRING && strcmp(k, ((kj_string*)constant->object)->data) == 0)
			return i;
	}

	/* constant not found, add it */
	int index = c->proto->constants.size;
	kj_value* constant = array_push(&c->proto->constants, kj_value, 1);
	constant->type = KOJI_TYPE_STRING;

	/* create the string object */
	uint str_length = strlen(k);
	kj_string* string = malloc(sizeof(kj_string) + str_length + 1);
	constant->object = string;

	/* setup the string object so that it always holds a reference (it is never destroyed)
	* and the actual string buffer is right after the string object in memory (same allocation) */
	string->references = 1;
	string->length = str_length;
	string->data = (char*)string + sizeof(kj_string);
	memcpy(string->data, k, str_length + 1);

	return index;
}

/* instruction emission */

static inline ubyte _kj_max_ub(ubyte a, ubyte b)
{
	return a > b ? a : b;
}

/**
* Pushes instruction @i to current prototype instructions.
*/
static inline void kc_emit(kj_compiler* c, kj_instruction i)
{
	kj_opcode const op = decode_op(i);
	if (opcode_has_target(op))
	{
		c->proto->ntemporaries = _kj_max_ub(c->proto->ntemporaries, (ubyte)decode_A(i) + 1);
	}
	array_push_value(&c->proto->instructions, kj_instruction, i);
}

/**
* Computes and returns the offset specified from instruction index to the next instruction
* that will be emitted in current prototype.
*/
static inline int kc_offset_to_next_instruction(kj_compiler* c, int from_instruction_index)
{
	return c->proto->instructions.size - from_instruction_index - 1;
}

/* Jump instruction related */

/**
* Writes the offset to jump instructions contained in @label starting from @begin to target
* instruction index target_index.
*/
static void kc_bind_label_to(kj_compiler* c, kc_label* label, uint begin, int target_index)
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
static inline void kc_bind_label_here(kj_compiler* c, kc_label* label, uint begin)
{
	kc_bind_label_to(c, label, begin, c->proto->instructions.size);
}

/* local variables */

/**
* Adds an identifier string to the compiler buffer and returns the string offset within it.
*/
static uint kc_push_identifier(kj_compiler* c, const char* identifier, uint identifier_size)
{
	char* my_identifier = array_push(&c->identifiers, char, identifier_size + 1);
	memcpy(my_identifier, identifier, identifier_size + 1);
	return my_identifier - c->identifiers.data;
}

/**
* Searches for a local named @identifier from current prototype up to the main one and returns
* a pointer to it if found, null otherwise.
*/
static kc_local* kc_fetch_local(kj_compiler* c, const char* identifier)
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
static void kc_define_local(kj_compiler* c, uint identifier_offset)
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
	KC_EXPR_TYPE_STRING,
	KC_EXPR_TYPE_REGISTER,
	KC_EXPR_TYPE_UPVALUE,
	KC_EXPR_TYPE_ACCESSOR,
	KC_EXPR_TYPE_EQ,
	KC_EXPR_TYPE_LT,
	KC_EXPR_TYPE_LTE,
} kc_expr_type;

static const char* KC_EXPR_TYPE_TYPE_TO_STRING[] = { "nil", "bool", "int", "real", "string", "register",
	"upvalue", "bool", "bool", "bool" };

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
		uint upvalue;
		struct
		{
			uint length;
			char* data;
		} string;
		struct
		{
			int lhs;
			int rhs;
		};
	};
	koji_bool positive;
} kc_expr;

static const kc_expr KC_EXPR_NIL = { .type = KC_EXPR_TYPE_NIL, .positive = true };

/**
* Makes and returns a boolean expr of specified @value.
*/
static inline kc_expr kc_expr_boolean(koji_bool value)
{
	return (kc_expr) { KC_EXPR_TYPE_BOOL, .integer = value, .positive = true };
}

/**
* Makes and returns a integer expr of specified @value.
*/
static inline kc_expr kc_expr_integer(koji_integer value)
{
	return (kc_expr) { KC_EXPR_TYPE_INT, .integer = value, .positive = true };
}

/**
* Makes and returns a real expr of specified @value.
*/
static inline kc_expr kc_expr_real(koji_real value)
{
	return (kc_expr) { KC_EXPR_TYPE_REAL, .real = value, .positive = true };
}

/**
* Makes and returns a string expr of specified @string.
*/
static inline kc_expr kc_expr_string(kj_compiler* c, uint length)
{
	return (kc_expr) {
		KC_EXPR_TYPE_STRING,
			.string.length = length,
			.string.data = allocator_alloc(&c->allocator, length + 1, 1),
			.positive = true
	};
}

/**
* Makes and returns a register expr of specified @location.
*/
static inline kc_expr kc_expr_register(int location)
{
	return (kc_expr) { KC_EXPR_TYPE_REGISTER, .location = location, .positive = true };
}

/**
* Makes and returns a register expr of specified @location.
*/
static inline kc_expr kc_expr_upvalue(uint index)
{
	return (kc_expr) { KC_EXPR_TYPE_UPVALUE, .upvalue = index, .positive = true };
}

/**
* Makes and returns an accessor expression as in "table[key]" for @location.
*/
static inline kc_expr kc_expr_accessor(int object_location, int accessor_name_location)
{
	return (kc_expr) {
		KC_EXPR_TYPE_ACCESSOR,
			.lhs = object_location,
			.rhs = accessor_name_location,
			.positive = true
	};
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
	return type >= KC_EXPR_TYPE_BOOL && type <= KC_EXPR_TYPE_STRING;
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
static inline kc_expr kc_expr_to_any_register(kj_compiler* c, kc_expr e, int target_hint)
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

	case KC_EXPR_TYPE_STRING: constant_index = kc_fetch_constant_string(c, e.string.data);
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
		if (e.positive) return e;
		kc_emit(c, encode_ABx(KJ_OP_NEG, target_hint, e.location));
		return kc_expr_register(target_hint);

	case KC_EXPR_TYPE_ACCESSOR:
		kc_emit(c, encode_ABC(KJ_OP_GET, target_hint, e.lhs, e.rhs));
		if (!e.positive) kc_emit(c, encode_ABx(KJ_OP_NEG, target_hint, target_hint));
		return kc_expr_register(target_hint);

	case KC_EXPR_TYPE_EQ: case KC_EXPR_TYPE_LT: case KC_EXPR_TYPE_LTE:
		kc_emit(c, encode_ABC(KJ_OP_EQ + e.type - KC_EXPR_TYPE_EQ, e.lhs, e.positive, e.rhs));
		kc_emit(c, encode_ABx(KJ_OP_JUMP, 0, 1));
		kc_emit(c, encode_ABC(KJ_OP_LOADB, target_hint, false, 1));
		kc_emit(c, encode_ABC(KJ_OP_LOADB, target_hint, true, 0));
		return kc_expr_register(target_hint);

	default: assert(0); return KC_EXPR_NIL;
	}
}

/**
* Emits the appropriate instructions so that expression e value is written to register @target.
*/
static void kc_move_expr_to_register(kj_compiler* c, kc_expr e, int target)
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
static int kc_use_temporary(kj_compiler* c, kc_expr const* e)
{
	int old_temporaries = c->temporaries;
	if ((e->type == KC_EXPR_TYPE_REGISTER || e->type == KC_EXPR_TYPE_ACCESSOR) && e->location == c->temporaries)
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
		return kc_expr_boolean(true);

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
static kc_expr kc_unary_minus(kj_compiler* c, const kc_expr_state* es, kj_source_location sourceloc, kc_expr e)
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
		compiler_error(c->lex->error_handler, sourceloc,
			"cannot apply operator unary minus to a value of type %s.", KC_EXPR_TYPE_TYPE_TO_STRING[e.type]);
	}
	return KC_EXPR_NIL;
}

/* forward decls */
static kc_expr kc_parse_expression(kj_compiler* c, const kc_expr_state* es);
static kc_expr kc_parse_expression_to_any_register(kj_compiler* c, int target);
static inline void kc_parse_block(kj_compiler* c);

/**
* Parses and compiles a closure starting from arguments declaration (therefore excluded "def" and
* eventual identifier). It returns the register expr with the location of the compiled closure.
*/
static kc_expr kc_parse_closure(kj_compiler* c, const kc_expr_state* es)
{
	ubyte num_args = 0;
	int proto_index = c->proto->prototypes.size;

	uint oldnlocals = c->locals.size;
	kj_prototype* oldproto = c->proto;
	uint oldtemporaries = c->temporaries;
	c->temporaries = 0;

	c->proto = malloc(sizeof(kj_prototype));
	*c->proto = (kj_prototype) { 0 };
	c->proto->references = 1;
	array_push_value(&oldproto->prototypes, kj_prototype*, c->proto);

	if (kc_accept(c, '('))
	{
		if (c->lex->lookahead != ')')
		{
			do
			{
				kc_check(c, tok_identifier);
				uint id_offset = kc_push_identifier(c, c->lex->token_string, c->lex->token_string_length);
				lex(c->lex);
				kc_define_local(c, id_offset);
				++num_args;
			} while (kc_accept(c, ','));
		}
		kc_expect(c, ')');
	}

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
* Parses and compiles a function call arguments "(arg1, arg2, ..)" and returns the number of arguments. Before calling
* this function save the current temporary as arguments will be compiled to temporaries staring from current. It's
* responsibility of the caller to restore the temporary register.
*/
static int kc_parse_function_call_args(kj_compiler* c)
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
static kc_expr kc_parse_primary_expression(kj_compiler* c, const kc_expr_state* es)
{
	kc_expr expr = { KC_EXPR_TYPE_NIL };
	kj_source_location sourceloc = c->lex->source_location;

	switch (c->lex->lookahead)
	{
		/* global table */
	case kw_globals:
		lex(c->lex);
		kc_emit(c, encode_ABx(KJ_OP_GLOBALS, es->target_register, 0));
		expr = kc_expr_register(es->target_register);
		break;

	case kw_this:
		lex(c->lex);
		kc_emit(c, encode_ABx(KJ_OP_THIS, es->target_register, 0));
		expr = kc_expr_register(es->target_register);
		break;

		/* literals */
	case kw_true: lex(c->lex); expr = kc_expr_boolean(true); break;
	case kw_false: lex(c->lex); expr = kc_expr_boolean(false); break;
	case tok_integer: expr = kc_expr_integer(c->lex->token_int); lex(c->lex); break;
	case tok_real: expr = kc_expr_real(c->lex->token_real); lex(c->lex); break;
	case tok_string:
		expr = kc_expr_string(c, c->lex->token_string_length);
		memcpy(expr.string.data, c->lex->token_string, c->lex->token_string_length + 1);
		lex(c->lex);
		break;

	case '(': /* subexpression */
		lex(c->lex);
		expr = kc_parse_expression(c, es);
		kc_expect(c, ')');
		break;

	case '!': /* not */
	{
		koji_bool negated = true;
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
		koji_bool minus = true;
		lex(c->lex);
		while (kc_accept(c, '-')) minus = !minus;
		expr = kc_parse_primary_expression(c, es);
		if (minus) expr = kc_unary_minus(c, es, sourceloc, expr);
		break;
	}

	case tok_identifier: /* variable */
	{
		/* we need to scan the token after the identifier so copy it to a temporary on the stack */
		char* id = alloca(c->lex->token_string_length + 1);
		memcpy(id, c->lex->token_string, c->lex->token_string_length + 1);
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

			uint fn_index = static_functions_fetch(c->static_functions, id);
			if (fn_index != -1)
			{
				kj_static_function const* fn = c->static_functions->functions.data + fn_index;

				if (nargs < fn->min_num_args || nargs > fn->max_num_args)
				{
					compiler_error(c->lex->error_handler, sourceloc,
						"static function '%s' does not accept %d %s.",
						c->static_functions->name_buffer.data + fn->name_string_offset, nargs, nargs == 1 ? "argument" : "arguments");
				}

				/* convert the function index to a constant */
				kc_expr fn_index_expr = kc_expr_to_any_register(c, kc_expr_integer(fn_index), c->temporaries);

				// call is to a static host function, emit the appropriate instruction and reset the number of used registers
				kc_emit(c, encode_ABC(KJ_OP_SCALL, first_arg_reg, fn_index_expr.location, nargs));

				c->temporaries = first_arg_reg;

				// all args will be popped and return values put into the first arg register.
				expr = kc_expr_register(first_arg_reg);
				break;
			}
		}
		
		compiler_error(c->lex->error_handler, sourceloc, "undeclared local variable '%s'.", id);
	}

	case kw_def: /* closure */
		lex(c->lex);
		expr = kc_parse_closure(c, es);
		break;

	case '{': /* table */
	{
		lex(c->lex);

		expr = kc_expr_register(es->target_register);
		int temps = kc_use_temporary(c, &expr);
		kc_emit(c, encode_ABx(KJ_OP_NEWTABLE, expr.location, 0));

		if (c->lex->lookahead != '}')
		{
			koji_integer index = 0;
			koji_bool has_key = false;

			do
			{
				/* parse key */
				kc_expr key, value;

				if (c->lex->lookahead == tok_identifier)
				{
					key = kc_expr_string(c, c->lex->token_string_length);
					memcpy(key.string.data, c->lex->token_string, c->lex->token_string_length + 1);
					lex(c->lex);
					key = kc_expr_to_any_register(c, key, c->temporaries);
					kc_expect(c, ':');
					has_key = true;
				}
				else
				{
					kj_source_location sl = c->lex->source_location;
					bool square_bracket = kc_accept(c, '[');
					key = kc_parse_expression_to_any_register(c, c->temporaries);
					if (square_bracket) kc_expect(c, ']');

					if (kc_accept(c, ':'))
					{
						has_key = true;
					}
					else if (has_key)
					{
						compiler_error(c->lex->error_handler, sl, "cannot leave key undefined after table entry with explicit key.");
					}
				}

				/* key might be occupying last temporary */
				int temps2 = kc_use_temporary(c, &key);

				if (has_key)
				{
					/* parse value */
					value = kc_parse_expression_to_any_register(c, c->temporaries);
				}
				else
				{
					value = key;
					key = kc_expr_to_any_register(c, kc_expr_integer(index++), c->temporaries);
				}

				c->temporaries = temps2;
				kc_emit(c, encode_ABC(KJ_OP_SET, expr.location, key.location, value.location));

			} while (kc_accept(c, ','));
		}
		kc_expect(c, '}');
		c->temporaries = temps;
		break;
	}

	default: kc_syntax_error(c, sourceloc);
	}

	/* remember this is a dot expression e.g. ".to_string"; if what follows is a call, dot expressions calls
	* are compiled differently from square-bracket accessors (vm will access prototype table if function
	* is not found) */
	koji_bool dot_accessor = false;

	for (;;)
	{
		switch (c->lex->lookahead)
		{
		case '(':
		{
			/* this holds the first argument or the object the function is called onto */
			int args_location = c->temporaries;

			kj_opcode op;
			int closure_or_key_location;

			if (expr.type == KC_EXPR_TYPE_ACCESSOR)
			{
				/* if expr is a dot expression, use the mcall opcode that will access the prototype table if
				* function name is not found, otherwise expr is like "value["key"]()", which is compiled as
				* a simple table get followed by closure call */
				if (dot_accessor)
				{
					/* if expr is an accessor, this is a "method" call. Put the object in the first temporary */
					if (expr.lhs != args_location) kc_emit(c, encode_ABx(KJ_OP_MOV, args_location, expr.lhs));
					
					op = KJ_OP_MCALL;
					args_location = ++c->temporaries;
					closure_or_key_location = expr.rhs;					
				}
				else
				{
					kc_emit(c, encode_ABC(KJ_OP_GET, args_location, expr.lhs, expr.rhs));

					op = KJ_OP_CALL;
					closure_or_key_location = c->temporaries++;
					args_location = c->temporaries;
				}

			}
			else if (expr.type == KC_EXPR_TYPE_REGISTER)
			{
				op = KJ_OP_CALL;
				closure_or_key_location = expr.location;
			}
			else
			{
				compiler_error(c->lex->error_handler, sourceloc, "cannot call value of type %s.", KC_EXPR_TYPE_TYPE_TO_STRING[expr.type]);
				return KC_EXPR_NIL;
			}

			/* parse the function call arguments */
			int nargs = kc_parse_function_call_args(c);

			/* memory optimization: if call has no args, put the return value to the fetched closure temp
			* instead of one above (no need for the temporary after the call, saves up one register) */
			if (nargs == 0 && expr.type && !dot_accessor)
			{
				args_location = closure_or_key_location;
			}

			/* emit the appropriate call instruction */
			kc_emit(c, encode_ABC(op, args_location, closure_or_key_location, nargs));

			/* the result now lives in */
			expr = kc_expr_register(args_location);

			/* restore the temporaries count */
			c->temporaries = args_location;

			break;
		}

		case '.': // object.identifier
		{
			lex(c->lex);

			/* compile the expr to a register */
			kc_check(c, tok_identifier);
			expr = kc_expr_to_any_register(c, expr, es->target_register);
			int temps = kc_use_temporary(c, &expr);

			/* now parse the key identifier and compile it to a register */
			kc_expr key = kc_expr_string(c, c->lex->token_string_length);
			memcpy(key.string.data, c->lex->token_string, c->lex->token_string_length + 1);
			key = kc_expr_to_any_register(c, key, es->target_register);

			c->temporaries = temps;

			/* expr now is the accessor "expr.key" */
			expr = kc_expr_accessor(expr.location, key.location);

			lex(c->lex); // identifier

			dot_accessor = true;

			continue; // skips the "dot_accessor = false" statement after the switch
		}

		case '[': // table[expr]
		{
			lex(c->lex);
			kc_expr key = kc_parse_expression_to_any_register(c, es->target_register);
			kc_expect(c, ']');
			expr = kc_expr_accessor(expr.location, key.location);
			break;
		}

		default:
			return expr;
		}

		dot_accessor = false;
	}
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
static void kc_compile_logical_operation(kj_compiler* c, const kc_expr_state* es, kc_binop op, kc_expr lhs)
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

	default: assert(false);
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
static kc_expr _compile_binary_expression(kj_compiler* c, const kc_expr_state* es, const kj_source_location sourceloc,
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
	case KC_BINOP_ADD:
		if (lhs.type == KC_EXPR_TYPE_NIL || rhs.type == KC_EXPR_TYPE_NIL) goto error;
		if (kc_expr_is_constant(lhs.type) && kc_expr_is_constant(rhs.type))
		{
			if (lhs.type == KC_EXPR_TYPE_BOOL || rhs.type == KC_EXPR_TYPE_BOOL)
				goto error;
			if ((lhs.type == KC_EXPR_TYPE_STRING) != (rhs.type == KC_EXPR_TYPE_STRING))
				goto error;
			if (lhs.type == KC_EXPR_TYPE_REAL || rhs.type == KC_EXPR_TYPE_REAL)
				lhs = kc_expr_real(kc_expr_to_real(lhs) + kc_expr_to_real(rhs));
			else if (lhs.type == KC_EXPR_TYPE_STRING && rhs.type == KC_EXPR_TYPE_STRING)
			{
				kc_expr temp = kc_expr_string(c, lhs.string.length + rhs.string.length);
				memcpy(temp.string.data, lhs.string.data, lhs.string.length);
				memcpy(temp.string.data + lhs.string.length, rhs.string.data, rhs.string.length + 1);
				lhs = temp;
			}
			else
				lhs.integer = lhs.integer + rhs.integer;
			return lhs;
		}
		break;

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
		lhs = (kc_expr_is_bool_convertible(lhs.type) && !kc_expr_to_bool(lhs)) ? kc_expr_boolean(false) : rhs;
		return lhs;

	case KC_BINOP_LOGICAL_OR:
		lhs = (kc_expr_is_bool_convertible(lhs.type) && kc_expr_to_bool(lhs)) ? kc_expr_boolean(true) : rhs;
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

	static const koji_bool BINOP_COMPARISON_TEST_VALUE[] = { true, false, true, true, false, false };

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
	compiler_error(c->lex->error_handler, sourceloc, "cannot make binary operation '%s' between values"
		" of type '%s' and '%s'.", KC_BINOP_TO_STR[binop], KC_EXPR_TYPE_TYPE_TO_STRING[lhs.type], KC_EXPR_TYPE_TYPE_TO_STRING[rhs.type]);
	return (kc_expr) { KC_EXPR_TYPE_NIL };

#undef MAKEBINOP
}

/**
* Parses and compiles the potential right hand side of a binary expression if binary operators
* are found.
*/
static kc_expr kc_parse_binary_expression_rhs(kj_compiler* c, const kc_expr_state* es, kc_expr lhs, int precedence)
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
			rhs = kc_parse_binary_expression_rhs(c, &es_rhs, rhs, tok_precedence + 1);
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
static kc_expr kc_parse_expression(kj_compiler* c, const kc_expr_state* es)
{
	kc_expr_state my_es = *es;
	my_es.true_jumps_begin = c->true_label.size;
	my_es.false_jumps_begin = c->false_label.size;

	kj_source_location sourceloc = c->lex->source_location;
	kc_expr lhs = kc_parse_primary_expression(c, &my_es);

	/* parse chain of assignments if any */
	if (kc_accept(c, '='))
	{
		if (!lhs.positive) goto error;

		switch (lhs.type)
		{
			case KC_EXPR_TYPE_REGISTER:
				if (lhs.location < 0 || lhs.location >= (int)c->locals.size) goto error; // more checks for lvalue
				kc_move_expr_to_register(c, kc_parse_expression_to_any_register(c, c->temporaries), lhs.location);
				return lhs;

			case KC_EXPR_TYPE_ACCESSOR:
			{
				int temps = kc_use_temporary(c, &lhs);
				kc_expr rhs = kc_parse_expression_to_any_register(c, c->temporaries);
				kc_emit(c, encode_ABC(KJ_OP_SET, lhs.lhs, lhs.rhs, rhs.location));
				c->temporaries = temps;
				return rhs;
			}

			default: goto error;
		}
	}

	return kc_parse_binary_expression_rhs(c, &my_es, lhs, 0);

error:
	compiler_error(c->lex->error_handler, sourceloc, "lhs of assignment is not an lvalue.");
	return KC_EXPR_NIL;
}

/**
* Parses and compiles an expression, then it makes sure that the final result is written to some
* register; it could be @target_hint or something else.
*/
static kc_expr kc_parse_expression_to_any_register(kj_compiler* c, int target_hint)
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
	koji_bool set_value_to_false = false;
	koji_bool set_value_to_true = false;
	koji_bool value_is_condition = kc_expr_is_comparison(expr.type);

	if (value_is_condition)
	{
		kc_emit(c, encode_ABC(KJ_OP_EQ + expr.type - KC_EXPR_TYPE_EQ, expr.lhs, expr.rhs, expr.positive));
		array_push_value(&c->true_label, uint, instructions->size);
		kc_emit(c, encode_ABx(KJ_OP_JUMP, 0, 0));
		set_value_to_false = true;
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
			set_value_to_false = true;
			replace_Bx(instructions->data + index, kc_offset_to_next_instruction(c, index));
		}
	}

	// if we need to set the result to false, emit the loadbool instruction (to false) now and remember its index so that
	// we can eventually patch it later
	size_t load_false_instruction_index = 0;
	if (set_value_to_false)
	{
		load_false_instruction_index = instructions->size;
		kc_emit(c, encode_ABC(KJ_OP_LOADB, target_hint, false, 0));
	}

	// analogous to the false case, iterate over the list of instructions branching to true, flag set_value_to_true if
	// instruction is not a testset, as we'll need to emit a loadbool to true instruction in such case.
	// also patch all jumps to this point as the next instruction emitted could be the loadbool to true.
	for (uint i = true_label_size, size = c->true_label.size; i < size; ++i)
	{
		uint index = c->true_label.data[i];
		if (index > 0 && decode_op(instructions->data[index - 1]) != KJ_OP_TESTSET)
		{
			set_value_to_true = true;
			replace_Bx(instructions->data + index, kc_offset_to_next_instruction(c, index));
		}
	}

	// emit the loadbool instruction to *true* if we need to
	if (set_value_to_true)
	{
		kc_emit(c, encode_ABC(KJ_OP_LOADB, target_hint, true, 0));
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
static void _kc_parse_condition(kj_compiler* c, koji_bool test_value)
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

static void kc_parse_block_stmts(kj_compiler* c);

/**
* Parses and compiles an entire "{ stmts... }" block.
*/
static inline void kc_parse_block(kj_compiler* c)
{
	kc_expect(c, '{');
	kc_parse_block_stmts(c);
	kc_expect(c, '}');
}

/**
* Parses and compiles an if statement.
*/
static void kc_parse_if_stmt(kj_compiler* c)
{
	instructions* instructions = &c->proto->instructions;
	kc_expect(c, kw_if);

	uint true_label_begin = c->true_label.size;
	uint false_label_begin = c->false_label.size;

	// parse the condition to branch to 'true' if it's false.
	kc_expect(c, '(');
	_kc_parse_condition(c, false);
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
static void kc_parse_while_stmt(kj_compiler* c)
{
	kc_expect(c, kw_while);

	uint true_label_begin = c->true_label.size;
	uint false_label_begin = c->false_label.size;
	uint first_condition_instruction = c->proto->instructions.size;

	// parse and compile the condition
	kc_expect(c, '(');
	_kc_parse_condition(c, false); // if condition is false jump (to true)
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
static void kc_parse_do_while_stmt(kj_compiler* c)
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
	_kc_parse_condition(c, true); // if condition is true jump (to true)
	kc_expect(c, ')');

	// bind the branches to true to the first body instruction
	kc_bind_label_to(c, &c->true_label, true_label_begin, first_body_instruction);

	// bind the exit label
	kc_bind_label_here(c, &c->false_label, false_label_begin);
}

/**
* Parses and compiles a single statement of any kind.
*/
static void kc_parse_statement(kj_compiler* c)
{
	/* reset the temporaries allocator every statement */
	allocator_reset(&c->allocator);

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
		uint identifier_offset = kc_push_identifier(c, c->lex->token_string, c->lex->token_string_length);
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
static void kc_parse_block_stmts(kj_compiler* c)
{
	uint num_variables = c->locals.size;

	while (c->lex->lookahead != '}' && c->lex->lookahead != tok_eos)
		kc_parse_statement(c);

	c->locals.size = num_variables;
}

/**
* Parses and compiles and entire module, i.e. a source file.
*/
static void kc_parse_module(kj_compiler* c)
{
	while (c->lex->lookahead != tok_eos)
	{
		kc_parse_statement(c);
	}

	kc_emit(c, encode_ABx(KJ_OP_RET, 0, 0));

	prototype_dump(c->proto, 0, 0); //(fixme)
}

/**
* It compiles a stream containing a source string to a koji module, reporting any error to specified
* @error_handler (todo). This function is called by the koji_state in its load* functions.
*/
static kj_prototype* compile(const char* source_name, koji_stream_reader_fn stream_func, void* stream_data,
	const kj_static_functions* local_variable)
{
	/* todo: make this part of the state */
	kj_error_handler e;
	e.reporter = default_error_report_fn;

	/* create a new module that will contain all instructions, constants, identifiers, and that all prototypes will refer to */
	kj_prototype* mainproto = malloc(sizeof(kj_prototype));
	*mainproto = (kj_prototype) { 0 };

	/* redirect the error handler jump buffer here so that we can cleanup the state. */
	if (setjmp(e.jmpbuf)) goto error;

	/* create and initialize the lexer */
	kj_lexer lexer;
	lex_init(&lexer, &e, source_name, stream_func, stream_data);

	/* setup the compilation state */
	kj_compiler state = { 0 };
	state.allocator = allocator_page_create(0);
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
	allocator_destroy(state.allocator);
	array_destroy(&state.locals);
	array_destroy(&state.false_label);
	array_destroy(&state.true_label);
	array_destroy(&state.identifiers);
	lex_close(&lexer);
	return mainproto;
}

#pragma endregion

#pragma region Table



#pragma endregion

//---------------------------------------------------------------------------------------------------------------------
#pragma region State

/* Contains all the necessary information to run a script function (closure) */
typedef struct ks_frame
{
	/* the function prototype this frame is executing */
	kj_prototype* proto;

	/* the program counter (current instruction index) */
	uint pc;

	/* value stack base, i.e. the index of the first value in the stack for this frame */
	int stackbase;

	/* the this object for this frame */
	kj_value this;

} ks_frame;

struct koji_state
{
	koji_bool valid;
	jmp_buf error_jump_buf;
	kj_static_functions static_host_functions;
	array_type(ks_frame) framestack;
	array_type(kj_value) valuestack;
	kj_value_table globals;
	uint sp;
};

/* koji state functions (ks) */

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

static void ks_error(koji_state* s, const char* format, ...)
{
	(void)s; // fixme
	va_list args;
	va_start(args, format);

	printf("koji runtime error: ");
	vprintf(format, args);
	printf("\n");
	fflush(stdout);

	va_end(args);

	longjmp(s->error_jump_buf, 1);
}

/* operations */
static inline void ks_op_neg(kj_value* dest, const kj_value* src)
{
	switch (src->type)
	{
		case KOJI_TYPE_NIL: value_set_boolean(dest, true);
		case KOJI_TYPE_BOOL: value_set_boolean(dest, !src->boolean);
		case KOJI_TYPE_INT: value_set_boolean(dest, !src->integer);
		case KOJI_TYPE_REAL: value_set_boolean(dest, !src->real);
		case KOJI_TYPE_STRING: value_set_boolean(dest, src->string->length == 0);
		case KOJI_TYPE_TABLE:
		case KOJI_TYPE_CLOSURE: value_set_boolean(dest, true);
	}
}

static inline void ks_op_unm(koji_state* s, kj_value* dest, const kj_value* src)
{
	switch (src->type)
	{
		case KOJI_TYPE_INT: value_set_integer(dest, -src->integer); break;
		case KOJI_TYPE_REAL: value_set_real(dest, -src->real); break;

		default:
			ks_error(s, "cannot apply unary minus operation to a %s value.", KJ_VALUE_TYPE_STRING[src->type]);
	}
}

#define KS_BINOP(op)\
{\
	switch (lhs->type)\
	{\
			case KOJI_TYPE_NIL: case KOJI_TYPE_CLOSURE: goto error;\
			case KOJI_TYPE_INT:\
				switch (rhs->type)\
				{\
					case KOJI_TYPE_NIL: case KOJI_TYPE_BOOL: case KOJI_TYPE_CLOSURE: goto error;\
					case KOJI_TYPE_INT: value_set_integer(dest, lhs->integer op rhs->integer); return;\
					case KOJI_TYPE_REAL: value_set_real(dest, lhs->integer op rhs->real); return;\
					default: goto error;\
				}\
			case KOJI_TYPE_REAL:\
				switch (rhs->type)\
				{\
					case KOJI_TYPE_NIL: case KOJI_TYPE_BOOL: case KOJI_TYPE_CLOSURE: goto error;\
					case KOJI_TYPE_INT: value_set_real(dest, lhs->real op rhs->integer); return;\
					case KOJI_TYPE_REAL: value_set_real(dest, lhs->real op rhs->real); return;\
					default: goto error;\
				}\
			default: goto error;\
	}\
	error:\
	ks_error(s, "cannot apply binary operator '" #op "' between values of type %s and %s.",\
		KJ_VALUE_TYPE_STRING[lhs->type], KJ_VALUE_TYPE_STRING[rhs->type]);\
	return;\
}

static inline void ks_binop_add(koji_state* s, kj_value* dest, const kj_value* lhs, const kj_value* rhs)
{
	if (lhs->type == KOJI_TYPE_STRING)
	{
		if (rhs->type != KOJI_TYPE_STRING) goto error;

		/* create a new string */
		kj_value temp = { KOJI_TYPE_NIL };
		value_new_string(&temp, lhs->string->length + rhs->string->length);
		memcpy(temp.string->data, lhs->string->data, lhs->string->length); /* copy lhs */
		memcpy(temp.string->data + lhs->string->length, rhs->string->data, rhs->string->length + 1); /* copy rhs */

		dest->string->length = lhs->string->length + rhs->string->length;

		value_destroy(dest);
		*dest = temp;
		return;
	}

	KS_BINOP(+)
}

static inline void ks_binop_sub(koji_state* s, kj_value* dest, const kj_value* lhs, const kj_value* rhs)
{
	KS_BINOP(-)
}

static inline void ks_binop_mul(koji_state* s, kj_value* dest, const kj_value* lhs, const kj_value* rhs)
{
	if (lhs->type == KOJI_TYPE_STRING)
	{
		if (rhs->type != KOJI_TYPE_INT && rhs->type != KOJI_TYPE_REAL) goto error;

		koji_integer repetitions = value_to_int(rhs);
		if (repetitions < 0)
			ks_error(s, "rhs value of string by integer multiplication must be non negative, it is %lli.", repetitions);

		uint dest_length = (uint)(lhs->string->length * repetitions);

		kj_value temp = { KOJI_TYPE_NIL };
		value_new_string(&temp, dest_length);

		/* copy lhs into dest 'rhs' times */
		for (uint i = 0; i < repetitions; ++i)
			memcpy(temp.string->data + lhs->string->length * i, lhs->string->data, lhs->string->length);

		temp.string->length = dest_length;
		temp.string->data[dest_length] = '\0';

		value_destroy(dest);
		*dest = temp;

		return;
	}

	KS_BINOP(*)
}

static inline void ks_binop_div(koji_state* s, kj_value* dest, const kj_value* lhs, const kj_value* rhs)
{
	KS_BINOP(/ )
}

#undef KS_BINOP

static inline void ks_binop_mod(koji_state* s, kj_value* dest, const kj_value* lhs, const kj_value* rhs)
{
	switch (lhs->type)
	{
	case KOJI_TYPE_INT:
		switch (lhs->type)
		{
		case KOJI_TYPE_INT: value_set_integer(dest, lhs->integer + rhs->integer); return;
		default: goto error;
		}

	default: goto error;
	}

error:
	ks_error(s, "cannot apply binary operator '%' between values of type %s and %s.",
		KJ_VALUE_TYPE_STRING[lhs->type], KJ_VALUE_TYPE_STRING[rhs->type]);
}

static inline koji_bool ks_comp_eq(const kj_value* lhs, const kj_value* rhs)
{
	switch (lhs->type)
	{
		case KOJI_TYPE_NIL: return rhs->type == KOJI_TYPE_NIL;
		case KOJI_TYPE_BOOL: return rhs->type == KOJI_TYPE_BOOL && lhs->boolean == rhs->boolean;
		case KOJI_TYPE_INT: return rhs->type == KOJI_TYPE_REAL ? lhs->integer == rhs->real : lhs->integer == value_to_int(rhs);
		case KOJI_TYPE_REAL: return lhs->real == value_to_real(rhs);
		default: assert(!"implement me");
	}
	return 0;
}

static inline koji_bool ks_comp_lt(const kj_value* lhs, const kj_value* rhs)
{
	switch (lhs->type)
	{
		case KOJI_TYPE_NIL: return rhs->type != KOJI_TYPE_NIL;
		case KOJI_TYPE_BOOL: return lhs->boolean < value_to_int(rhs);
		case KOJI_TYPE_INT: return lhs->integer < value_to_int(rhs);
		case KOJI_TYPE_REAL: return lhs->real < value_to_real(rhs);
		default: assert(!"implement me");
	}
	return 0;
}

static inline koji_bool ks_comp_lte(const kj_value* lhs, const kj_value* rhs)
{
	switch (lhs->type)
	{
		case KOJI_TYPE_NIL: return true;
		case KOJI_TYPE_BOOL: return lhs->boolean <= value_to_int(rhs);
		case KOJI_TYPE_INT: return lhs->integer <= value_to_int(rhs);
		case KOJI_TYPE_REAL: return lhs->real <= value_to_real(rhs);
		default: assert(!"implement me");
	}
	return 0;
}

/**
* This function implements the "target = object[key]" syntax in the language.
* The semantics depend on the type of target, but the idea is that the sub-element with specified
* @key in @object will be put in in @target
*/
static inline void ks_object_get_element(koji_state* s, kj_value* target, kj_value const* object, kj_value const* key)
{
	switch (object->type)
	{
		case KOJI_TYPE_TABLE:
			value_set(target, table_get(&object->table->table, key));
			break;

		case KOJI_TYPE_STRING:
			value_new_string(target, 1);
			target->string->data[0] = object->string->data[value_to_int(key)];
			target->string->data[1] = '\0';
			break;

		default:
			ks_error(s, "invalid get operation performed on value of type %s.", KJ_VALUE_TYPE_STRING[object->type]);
	}
}

/**
* This function implements the "object[key] = value" syntax in the language.
* The semantics depend on the type of object, but the idea is that @value is set to the
* sub-value with specified @key in @object.
*/
static inline void ks_object_set_element(koji_state* s, kj_value* object, kj_value const* key, kj_value const* value)
{
	switch (object->type)
	{
		case KOJI_TYPE_TABLE:
			table_set(&object->table->table, key, value);
			break;

		default:
			ks_error(s, "invalid set operation performed on value of type %s.", KJ_VALUE_TYPE_STRING[object->type]);
	}
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
	value->type = KOJI_TYPE_NIL;
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

static inline void ks_push_frame(koji_state* s, kj_prototype* proto, uint stackbase, kj_value this)
{
	ks_frame* frame = array_push(&s->framestack, ks_frame, 1);
	frame->proto = proto;
	frame->pc = 0;
	frame->stackbase = stackbase;
	frame->this = this;
	array_push(&s->valuestack, kj_value, proto->ntemporaries);
	for (int i = proto->nargs, tot = proto->nargs + proto->ntemporaries; i < tot; ++i)
		s->valuestack.data[frame->stackbase + i].type = KOJI_TYPE_NIL;
}

static inline void ks_call_clojure(koji_state* s, ks_frame* curr_frame, kj_value const* closure, int ncallargs, int stackbaseoffset, kj_value const* this)
{
	if (closure->type != KOJI_TYPE_CLOSURE)
		ks_error(s, "cannot call value of type %s.", KJ_VALUE_TYPE_STRING[closure->type]);

	kj_prototype* proto = closure->closure.proto;

	if (proto->nargs != ncallargs)
		ks_error(s, "closure at (TODO) takes %d number of arguments (%d provided).", proto->nargs, ncallargs);

	++proto->references;

	/* push a new frame onto the stack with stack base at register A offset from current frame stack base */
	ks_push_frame(s, proto, curr_frame->stackbase + stackbaseoffset, *this);
}

#pragma region Standard functions

static int kjstd_set_metatable(koji_state* s, int nargs)
{
	kj_value* table = ks_top(s, -2);

	if (table->type != KOJI_TYPE_TABLE)
	{
		ks_error(s, "set_metatable() argument 1 must be of type table.");
	}

	kj_value* metatable = ks_top(s, -1);
	
	if (metatable->type != KOJI_TYPE_TABLE)
	{
		ks_error(s, "set_metatable() argument 2 must be of type table.");
	}

	table->table->metatable = metatable->table;
	++metatable->table->references;

	koji_pop(s, nargs);
	return 0;
}

static int kjstd_get_metatable(koji_state* s, int nargs)
{
	(void)nargs;
	kj_value* table = ks_top(s, -1);

	if (table->type != KOJI_TYPE_TABLE)
	{
		ks_error(s, "get_metatable() argument 1 must be of type table.");
	}

	if (table->table->metatable)
		value_set_table(ks_top(s, -1), table->table->metatable);
	else
		value_set_nil(ks_top(s, -1));

	return 1;
}

static int kjstd_print(koji_state* s, int nargs)
{
	for (int i = -nargs; i < 0; ++i)
	{
		if (i != -nargs) printf(" ");
		switch (koji_value_type(s, i))
		{
			case KOJI_TYPE_NIL: printf("nil"); break;
			case KOJI_TYPE_BOOL: printf(koji_to_int(s, i) ? "true" : "false"); break;
			case KOJI_TYPE_INT: printf("%lli", koji_to_int(s, i)); break;
			case KOJI_TYPE_REAL: printf("%f", koji_to_real(s, i)); break;
			case KOJI_TYPE_STRING: printf("%s", koji_get_string(s, i)); break;
			case KOJI_TYPE_TABLE: printf("table"); break;
			case KOJI_TYPE_CLOSURE: printf("closure"); break;
			default: printf("unknown\n");
		}
	}
	printf("\n");
	koji_pop(s, nargs);
	return 0;
}

static int kjstd_len(koji_state* s, int nargs)
{
	(void)nargs;
	kj_value* top = ks_top(s, -1);
	uint length;
	switch (top->type)
	{
		case KOJI_TYPE_STRING: length = top->string->length; break;
		case KOJI_TYPE_TABLE:  length = top->table->table.size;
		default: ks_error(s, "len argument must be of type string or table."); length = 0;
	}
	value_set_integer(top, length);
	return 1;
}

static void ks_register_standard_functions(koji_state* s)
{
	koji_static_function(s, "set_metatable", kjstd_set_metatable, 2, 2);
	koji_static_function(s, "get_metatable", kjstd_get_metatable, 2, 2);
	koji_static_function(s, "print", kjstd_print, 0, USHRT_MAX);
	koji_static_function(s, "len", kjstd_len, 1, 1);
}

#pragma endregion

/* API functions */

koji_state* koji_create(void)
{
	koji_state* s = malloc(sizeof(koji_state));
	*s = (koji_state) { 0 };
	s->valid = true;
	table_init(&s->globals.table, KJ_TABLE_DEFAULT_CAPACITY);
	s->globals.references = 1;
	ks_register_standard_functions(s);
	return s;
}

void koji_destroy(koji_state* s)
{
	/* destroy all value on the stack */
	for (uint i = 0; i < s->valuestack.size; ++i)
		value_set_nil(s->valuestack.data + i);

	/* release prototype references */
	for (uint i = 0; i < s->framestack.size; ++i)
		prototype_release(s->framestack.data[i].proto);

	array_destroy(&s->static_host_functions.functions);
	array_destroy(&s->static_host_functions.name_buffer);
	array_destroy(&s->valuestack);
	array_destroy(&s->framestack);
	assert(s->globals.references == 1);
	table_destruct(&s->globals.table);

	free(s);
}

koji_result koji_static_function(
	koji_state* s, const char* name, koji_user_function fn, unsigned short min_num_args, unsigned short max_num_args)
{
	int sfunindex = static_functions_fetch(&s->static_host_functions, name);

	if (sfunindex > 0)
	{
		/* function already in defined */
		return KOJI_RESULT_INVALID_VALUE;
	}
	else
	{
		/* push the name */
		uint length = strlen(name);
		char* destname = array_push(&s->static_host_functions.name_buffer, char, length + 1);
		memcpy(destname, name, length + 1);

		kj_static_function* sf = array_push(&s->static_host_functions.functions, kj_static_function, 1);
		sf->name_string_offset = destname - s->static_host_functions.name_buffer.data;
		sf->function = fn;
		sf->min_num_args = min_num_args;
		sf->max_num_args = max_num_args;

		return KOJI_RESULT_OK;
	}
}

int koji_load(koji_state* s, const char* source_name, koji_stream_reader_fn stream_fn, void* stream_data)
{
	kj_prototype* mainproto = compile(source_name, stream_fn, stream_data, &s->static_host_functions);

	if (!mainproto) return KOJI_RESULT_FAIL;

	/* create a closure to main prototype and push it to the stack */
	koji_push_nil(s);
	value_new_closure(ks_top(s, -1), mainproto);

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

	if (top.type != KOJI_TYPE_CLOSURE)
	{
		ks_error(s, "cannot run function on stack, top value is not closure.");
		return KOJI_RESULT_FAIL;
	}

	// push stack frame for closure
	kj_value nil_value = { .type = KOJI_TYPE_NIL };
	ks_push_frame(s, top.closure.proto, s->sp, nil_value);

	return koji_continue(s);
}

int koji_continue(koji_state* s)
{
	if (setjmp(s->error_jump_buf))
	{
		s->valid = false;
		return KOJI_RESULT_FAIL;
	}

	if (!s->valid) ks_error(s, "cannot resume state, not valid.");
	if (s->framestack.size == 0) ks_error(s, "cannot resume state, not currently executing any function.");

newframe:
	assert(s->framestack.size > 0);
	ks_frame* curr_frame = s->framestack.data + s->framestack.size - 1;
	const kj_instruction* instructions = curr_frame->proto->instructions.data;

	for (;;)
	{
		kj_instruction ins = instructions[curr_frame->pc++];

#define KSREGA ks_get_register(s, curr_frame, decode_A(ins))
#define KSARG(X) ks_get(s, curr_frame, decode_##X(ins))

		switch (decode_op(ins))
		{
		case KJ_OP_LOADNIL:
			for (uint r = decode_A(ins), to = r + decode_Bx(ins); r < to; ++r)
				value_set_nil(ks_get_register(s, curr_frame, r));
			break;

		case KJ_OP_LOADB:
			value_set_boolean(KSREGA, (koji_bool)decode_B(ins));
			curr_frame->pc += decode_C(ins);
			break;

		case KJ_OP_MOV:
			value_set(KSREGA, KSARG(Bx));
			break;

		case KJ_OP_NEG:
			ks_op_neg(KSREGA, KSARG(Bx));
			break;

		case KJ_OP_UNM:
			ks_op_unm(s, KSREGA, KSARG(Bx));
			break;

		case KJ_OP_ADD:
			ks_binop_add(s, KSREGA, KSARG(B), KSARG(C));
			break;

		case KJ_OP_SUB:
			ks_binop_sub(s, KSREGA, KSARG(B), KSARG(C));
			break;

		case KJ_OP_MUL:
			ks_binop_mul(s, KSREGA, KSARG(B), KSARG(C));
			break;

		case KJ_OP_DIV:
			ks_binop_div(s, KSREGA, KSARG(B), KSARG(C));
			break;

		case KJ_OP_MOD:
			ks_binop_mod(s, KSREGA, KSARG(B), KSARG(C));
			break;

		case KJ_OP_POW:
			assert(0);
			break;

		case KJ_OP_GLOBALS:
		{
			kj_value* regA = KSREGA;
			regA->type = KOJI_TYPE_TABLE;
			regA->table = &s->globals;
			++s->globals.references;
			break;
		}

		case KJ_OP_TEST:
		{
			int newpc = curr_frame->pc + 1;
			if (value_to_bool(KSREGA) == decode_Bx(ins))
				newpc += decode_Bx(instructions[curr_frame->pc]);
			curr_frame->pc = newpc;
			break;
		}

		case KJ_OP_TESTSET:
		{
			int newpc = curr_frame->pc + 1;
			const kj_value* arg = KSARG(B);
			if (value_to_bool(arg) == decode_C(ins))
			{
				value_set(KSREGA, arg);
				newpc += decode_Bx(instructions[curr_frame->pc]);
			}
			curr_frame->pc = newpc;
			break;
		}

		case KJ_OP_JUMP:
			curr_frame->pc += decode_Bx(ins);
			break;

		case KJ_OP_EQ:
		{
			int newpc = curr_frame->pc + 1;
			if (ks_comp_eq(KSREGA, KSARG(B)) == decode_C(ins))
			{
				newpc += decode_Bx(instructions[curr_frame->pc]);
			}
			curr_frame->pc = newpc;
			break;
		}

		case KJ_OP_LT:
		{
			int newpc = curr_frame->pc + 1;
			if (ks_comp_lt(KSREGA, KSARG(B)) == decode_C(ins))
			{
				newpc += decode_Bx(instructions[curr_frame->pc]);
			}
			curr_frame->pc = newpc;
			break;
		}

		case KJ_OP_LTE:
		{
			int newpc = curr_frame->pc + 1;
			if (ks_comp_lte(KSREGA, KSARG(B)) == decode_C(ins))
			{
				newpc += decode_Bx(instructions[curr_frame->pc]);
			}
			curr_frame->pc = newpc;
			break;
		}

		case KJ_OP_THIS:
			value_set(KSREGA, &curr_frame->this);
			break;

		case KJ_OP_CLOSURE:
		{
			assert((uint)decode_Bx(ins) < curr_frame->proto->prototypes.size);
			value_new_closure(KSREGA, curr_frame->proto->prototypes.data[decode_Bx(ins)]);
			break;
		}

		case KJ_OP_CALL:
		{
			const kj_value* value = KSARG(B);
			ks_call_clojure(s, curr_frame, value, decode_C(ins), decode_A(ins), value);
			goto newframe;
		}

		case KJ_OP_SCALL:
		{
			kj_value const* fn_index = KSARG(B);
			assert(fn_index->type == KOJI_TYPE_INT && fn_index->integer < s->static_host_functions.functions.size);
			const kj_static_function* sfun = s->static_host_functions.functions.data + fn_index->integer;
			uint sp = s->sp;
			s->sp = curr_frame->stackbase + decode_A(ins) + decode_C(ins);
			int nretvalues = sfun->function(s, decode_C(ins));
			if (nretvalues == 0) value_set_nil(ks_get_register(s, curr_frame, s->sp));
			s->sp = sp;
			break;
		}

		case KJ_OP_MCALL:
		{
			const int regA = decode_A(ins);
			
			kj_value* object = ks_get_register(s, curr_frame, regA - 1);
			if (object->type != KOJI_TYPE_TABLE)
				ks_error(s, "cannot call method on object of type %s", KJ_VALUE_TYPE_STRING[object->type]);

			const kj_value* key = KSARG(B);
			kj_value* closure = table_get(&object->table->table, key);

			/* entry not found in table, try in its metatable */
			if (closure->type == KOJI_TYPE_NIL && object->table->metatable)
				closure = table_get(&object->table->metatable->table, key);

			ks_call_clojure(s, curr_frame, closure, decode_C(ins), decode_A(ins), object);

			goto newframe;
		}

		case KJ_OP_RET:
		{
			int i = 0;
			for (int reg = decode_A(ins), to_reg = decode_Bx(ins); reg < to_reg; ++reg, ++i)
				value_set(ks_get_register(s, curr_frame, i), ks_get(s, curr_frame, reg));
			for (int end = curr_frame->proto->nargs + curr_frame->proto->ntemporaries; i < end; ++i)
				value_set_nil(ks_get_register(s, curr_frame, i));
			prototype_release(curr_frame->proto);
			--s->framestack.size;
			if (s->framestack.size == 0) return KOJI_RESULT_OK;
			goto newframe;
		}

		case KJ_OP_NEWTABLE:
			value_new_table(KSREGA);
			break;

		case KJ_OP_GET:
			ks_object_get_element(s, KSREGA, KSARG(B), KSARG(C));
			break;

		case KJ_OP_SET:
			ks_object_set_element(s, KSREGA, KSARG(B), KSARG(C));
			break;

		default:
			assert(0 && "unsupported op code");
			break;
		}

#undef KSREGA
#undef KSARG
	}
}

void koji_push_nil(koji_state* s)
{
	ks_push(s)->type = KOJI_TYPE_NIL;
}

void koji_push_bool(koji_state* s, koji_bool b)
{
	kj_value* value = ks_push(s);
	value->type = KOJI_TYPE_BOOL;
	value->boolean = b;
}

void koji_push_int(koji_state* s, koji_integer i)
{
	kj_value* value = ks_push(s);
	value->type = KOJI_TYPE_INT;
	value->integer = i;
}

void koji_push_real(koji_state* s, koji_real f)
{
	kj_value* value = ks_push(s);
	value->type = KOJI_TYPE_REAL;
	value->real = f;
}

void koji_pop(koji_state* s, int count)
{
	for (int i = 0; i < count; ++i)
	{
		kj_value val = ks_pop(s);
		value_destroy(&val);
	}
}

koji_type koji_value_type(koji_state* s, int offset)
{
	return ks_top(s, offset)->type;
}

koji_bool koji_is_real(koji_state* s, int offset)
{
	return ks_top(s, offset)->type == KOJI_TYPE_REAL;
}

koji_integer koji_to_int(koji_state* s, int offset)
{
	return value_to_int(ks_top(s, offset));
}

koji_real koji_to_real(koji_state* s, int offset)
{
	return value_to_real(ks_top(s, offset));
}

const char* koji_get_string(koji_state* s, int offset)
{
	kj_value* value = ks_top(s, offset);
	assert(value->type == KOJI_TYPE_STRING);
	return value->string->data;
}

#pragma endregion

#ifdef __clang__ 
#pragma clang diagnostic pop
#endif

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
