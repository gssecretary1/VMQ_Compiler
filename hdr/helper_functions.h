#ifndef HELPER_FUNCTIONS_H_
#define HELPER_FUNCTIONS_H_

#include "AST.h"
#include "data_lists.h"
#include "scope.h"

//	================
//	Global Variables
//	================

enum { VMQ_INT_SIZE = 2, VMQ_FLT_SIZE = 4 };

/* Lists for tracking global VMQ memory-space elements */
INT_LIST int_list_head;
FLT_LIST flt_list_head;
STR_LIST str_list_head;
VAR_LIST global_var_list_head;

/* List for tracking functions as they are encountered in src file */
FUNC_LIST func_list_head;

/* Tracks current function (effectively tail of func_list_head) */
struct func_list_node* current_func;

/* Stack for tracking scope (top of stack == current scope) */
SCOPE scope_stack_head;

struct AST_node* AST_root;

//	=========
//	Functions
//	=========

/* Initializes global variables (see above) */
void init();


#endif