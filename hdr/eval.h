#ifndef EVAL_H_
#define EVAL_H_

#include "AST.h"
#include "helper_functions.h"
#include "parser.tab.h"
#include "error_handling.h"

// Fixed-size string space, used when generating VMQ statements.
char VMQ_line[32];

/* Recursively evaluates nodes in the AST Tree generated by Yacc parsing the src file */
extern void eval(struct AST_node *a);

// Specfic case functions for eval to help break up the work load, so we don't have a 2000+ line function
extern void evalReturn(struct AST_node *a);
extern void evalControl(struct AST_node *a);
extern void evalCond(struct cond_list* list);
extern void evalInput(struct AST_node *a);
extern void evalOutput(struct AST_node *a);
extern void evalAssignOp(struct AST_node *a);
extern void evalIncOp(struct AST_node *a);
extern void evalArrAccess(struct AST_node *a);
extern void evalFuncCall(struct AST_node *a, struct var_list_node *param);
extern void evalMath(struct AST_node *a);

// Helper function that "allocates" new temporary variables for storing intermediate values when evaluating expressions.
static inline unsigned int getNewTempVar(unsigned int type)
{
    unsigned int *cur_addr = &CURRENT_FUNC->VMQ_data.tempvar_cur_addr;
    unsigned int *cur_size = &CURRENT_FUNC->VMQ_data.tempvar_cur_size;
    unsigned int *max_size = &CURRENT_FUNC->VMQ_data.tempvar_max_size;

    if (type == INT)
    { // VMQ integers are 16 bits (2 bytes)
        *cur_size += VMQ_INT_SIZE;

        // If cur_size was zero before the above op, don't change cur_addr (it is already set to the correct address)
        if (*cur_size - VMQ_INT_SIZE)
            *cur_addr += VMQ_INT_SIZE;
    }
    else if (type == ADDR)
    { // VMQ addresses are 16 bits (2 bytes)
        *cur_size += VMQ_ADDR_SIZE;

        // If cur_size was zero before the above op, don't change cur_addr (it is already set to the correct address)
        if (*cur_size - VMQ_ADDR_SIZE)
            *cur_addr += VMQ_ADDR_SIZE;
    }
    else if (type == FLOAT)
    { // VMQ floats are 32 bits (4 bytes)

        if (*cur_size)
        {
            struct VMQ_temp_node *last_temp = CURRENT_FUNC->VMQ_data.tempvar_stack_head;

            if (last_temp && (last_temp->type == INT || last_temp->type == ADDR))
            {
                *cur_size += (*cur_addr % VMQ_FLT_SIZE) ? VMQ_FLT_SIZE : VMQ_FLT_SIZE + VMQ_ADDR_SIZE;
                *cur_addr += (*cur_addr % VMQ_FLT_SIZE) ? VMQ_ADDR_SIZE : VMQ_FLT_SIZE;
            }
            else // !last_temp || last_temp->type == FLOAT
            {
                *cur_size += (*cur_addr % VMQ_FLT_SIZE) ? VMQ_ADDR_SIZE + VMQ_FLT_SIZE : VMQ_FLT_SIZE;
                *cur_addr += (*cur_addr % VMQ_FLT_SIZE) ? (*cur_addr % VMQ_FLT_SIZE) : VMQ_FLT_SIZE;
            }
        }
        else // No temporary variables are current in use
        {
            *cur_size += (*cur_addr % VMQ_FLT_SIZE) ? VMQ_FLT_SIZE + VMQ_ADDR_SIZE : VMQ_FLT_SIZE;
            *cur_addr += (*cur_addr % VMQ_FLT_SIZE) ? (*cur_addr % VMQ_FLT_SIZE) : 0;
        }
    }

    /*
===================================================================================================
	Special Case - Temporary Floating Point Variables and Memory Address Alignment
	-  Floating point values/variables must be stored at memory addresses which  -
	-		   are divisible by 4, per VMQ specifications		     -
===================================================================================================
Case 1:  Adding float tempvar, next 2-byte slot is aligned (i.e., *cur_addr % VMQ_FLT_SIZE != 0)

	      v ---> destination memory addr
    [INT]   [	]   [	]   [	]   ...
     -2	     -4	     -6	     -8
      ^			Required adjustments for adding float tempvar:
      |			*cur_size += VMQ_FLT_SIZE		(i.e., += 4)
      |			*cur_addr += (*cur_addr % VMQ_FLT_SIZE) (i.e., += 2)
   cur_addr
====================================================================================================
Case 2:  Adding float tempvar, next 2-byte slot is NOT aligned (i.e., *cur_addr % VMQ_FLT_SIZE == 0)

			      v ---> destination memory addr
    [INT]   [INT]   [	]   [	]   ...
     -2	     -4	     -6	     -8
	      ^		Required adjustments for adding float tempvar:
	      |		*cur_size += VMQ_ADDR_SIZE + VMQ_FLT_SIZE   (i.e., += 6)
	      |		*cur_addr += VMQ_FLT_SIZE		    (i.e., += 4)
	   cur_addr
====================================================================================================
Case 3:  Adding float tempvar, no temporary variables are in use - starting addr is aligned

      v ---> destination memory addr
    [	]   [	]   [	]   ...
     -4	     -6	     -8	
      ^			Required adjustments for adding float tempvar:
      |			*cur_size += VMQ_FLT_SIZE		(i.e., += 4)
      |			*cur_addr += 0
   cur_addr
====================================================================================================
Case 4:  Adding float tempvar, no temporary variables are in use - starting addr NOT aligned

	      v ---> destination memory addr
    [	]   [	]   [	]   [	]   ...
     -2	     -4	     -6	     -8
      ^			Required adjustments for adding float tempvar:
      |			*cur_size += VMQ_FLT_SIZE + VMQ_ADDR_SIZE   (i.e., += 6)
      |			*cur_addr += (*cur_addr % VMQ_FLT_SIZE)	    (i.e., += 2)
   cur_addr
====================================================================================================
*/

    // If we have used up a record number of bytes, set max_size so we can
    // track required amount of local memory needed for the current function.
    if (*cur_size > *max_size)
        *max_size = *cur_size;

    // Push the newly "allocated" temporary variable onto the temporary variable stack.
    pushTempVar(type);

    if (DEBUG)
        dumpTempVarStack('n');

    return *cur_addr;
}

// Helper function that is used to free the most recently "allocated" temporary variable.
static inline void freeTempVar()
{
    unsigned int *cur_addr = &CURRENT_FUNC->VMQ_data.tempvar_cur_addr;
    unsigned int *cur_size = &CURRENT_FUNC->VMQ_data.tempvar_cur_size;
    unsigned int new_addr = CURRENT_FUNC->VMQ_data.tempvar_start;

    struct VMQ_temp_node **stack_ptr = &CURRENT_FUNC->VMQ_data.tempvar_stack_head;
    unsigned int old_type = 0, new_type = 0;

    if (!(*stack_ptr))
        yyerror("ERROR - freeTempVar():  Deallocation attempted on non-existant temporary variable");
    else
    {
        old_type = (*stack_ptr)->type;

        popTempVar();

        if (stack_ptr && *stack_ptr)
            new_type = (*stack_ptr)->type;
    }

    if (stack_ptr && *stack_ptr)
        new_addr = (*stack_ptr)->VMQ_loc;

    *cur_addr = new_addr;
    if (old_type == INT)
        *cur_size -= VMQ_INT_SIZE;
    else if (old_type == ADDR)
        *cur_size -= VMQ_ADDR_SIZE;
    else
    {
        if (((new_type == INT || new_type == ADDR) && (new_addr % VMQ_FLT_SIZE == 0)) || ((new_type == 0) && (new_addr % VMQ_FLT_SIZE == 2)))
            *cur_size -= VMQ_ADDR_SIZE;

        *cur_size -= VMQ_FLT_SIZE;
    }

    if (DEBUG)
        dumpTempVarStack('f');
}

// Helper function for evalMath(), used to determine whether a LHS or RHS operand is a terminal case or requires evaluation.
static inline int isMathLeaf(unsigned int type)
{
    if (type == INT_LITERAL || type == FLT_LITERAL || type == VAR_ACCESS || type == ARR_ACCESS)
        return 1;
    else
        return 0;
}

#endif
