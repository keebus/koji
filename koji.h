/*
 * koji language - 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This is public domain software, read UNLICENSE.txt for more information.
 */

#pragma once
#ifndef KOJI_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Types */

/**
  * The type of integer koji compiler and VM will use.
  */
typedef long long koji_integer;

/**
  * The type of floating poing number koji compiler and VM will use.
  */
typedef double koji_real;

/**
  * The boolean type koji uses (leave it to unsigned char).
  */
typedef unsigned char koji_bool;

/**
  * Represents the result of some koji operation.
  */
typedef int koji_result;

/**
  * A context is used to compile scripts and execute them. Each context is independent from the
  * others.
  */
typedef struct koji_state koji_state;

/**
* A prototype is a script function descriptor. It contains function instructions, constants and properties.
* Prototypes can contain other nested prototypes in a tree structure. Prototypes are reference counted and
* closures hold a reference to their prototype, the VM holds a reference to prototypes in the frame stack and
* parent prototypes hold a reference of their children.
*/
typedef struct koji_prototype koji_prototype;

/**
  * Enumeration of all supported value types.
  */
enum koji_type
{
	KOJI_TYPE_NIL,
	KOJI_TYPE_BOOL,
	KOJI_TYPE_INT,
	KOJI_TYPE_REAL,
	KOJI_TYPE_STRING,
	KOJI_TYPE_TABLE,
	KOJI_TYPE_CLOSURE
};

typedef enum koji_type koji_type;

/**
* Function that takes some user data and returns the next char in the stream every time it's
* called or EOF if finished.
*/
typedef char(*koji_stream_reader_fn)(void*);

/**
  * Type of host functions called from script.
  */
typedef int(*koji_user_function) (koji_state*, int nargs);

/* Function results */

/**
  * Returned when a function performs its operations successfully.
  */
#define KOJI_RESULT_OK 0

/**
  * Returned when some error occurred in a function.
  */
#define KOJI_RESULT_FAIL -1

/**
  * Returned if a function tried to open an invalid file (e.g. inexistent).
  */
#define KOJI_RESULT_INVALID_FILE -2

/**
  * Returned when an API function expects a value of a given type but actual type does not match.  
  * Example: koji_run() expects top value to be a closure in order to be executed.
  */
#define KOJI_RESULT_INVALID_VALUE -3

/* Functions */

/**
  * Creates and returns a new koji context.
  * This function only fails if system is out of memory and returns NULL.
  */
extern koji_state* koji_create(void);

/**
  * Destroys a previously created context.
  */
extern void koji_destroy(koji_state*);

/**
  * TODO add documentation.
  */
extern void koji_static_function(koji_state*, const char* name, koji_user_function fn, int nargs);

/**
  * Compiles a source from a generic stream and returns compilation result.
  * If compilation was succesful, the script main prototype function is pushed as a closure onto the
  * stack. Call koji_run() to execute the just compiled script.
  */
extern koji_result koji_load(koji_state*, const char* source_name, koji_stream_reader_fn stream_fn, void* stream_data);

/**
  * Compiles specified string @source code and returns compilation result.
  * If compilation was succesful, the script main prototype function is pushed as a closure onto the
  * stack. Call koji_run() to execute the just compiled script.
  */
extern koji_result koji_load_string(koji_state*, const char* source);

/**
  * Compiles specified source file at @filename and returns compilation result.
  * If compilation was succesful, the script main prototype function is pushed as a closure onto
  * the stack. Call koji_run() to execute the just compiled script.
  */
extern koji_result koji_load_file(koji_state*, const char* filename);

/**
  * Pops the stack top and executes it right away if it's a closure, otherwise it returns
  * KOJI_NOT_CLOSURE result.
  */
extern koji_result koji_run(koji_state*);

/**
  * Resumes execution.
  */
extern koji_result koji_continue(koji_state*);

/**
  * TODO add documentation.
  */
extern void koji_push_nil(koji_state*);

/**
  * TODO add documentation.
  */
extern void koji_push_bool(koji_state*, koji_bool value);

/**
  * TODO add documentation.
  */
extern void koji_push_int(koji_state*, koji_integer value);

/**
  * TODO add documentation.
  */
extern void koji_push_real(koji_state*, koji_real value);

/**
* TODO add documentation.
*/
extern void koji_pop(koji_state*, int count);

/**
  * TODO add documentation.
  */
extern koji_type koji_get_value_type(koji_state*, int offset);

/**
  * TODO add documentation.
  */
extern koji_integer koji_to_int(koji_state*, int offset);

/**
  * TODO add documentation.
  */
extern koji_real koji_to_real(koji_state*, int offset);

/**
  * TODO add documentation.
  */
extern const char* koji_get_string(koji_state*, int offset);


#ifdef __cplusplus
} // extern "C"
#endif

#endif // KOJI_H_
