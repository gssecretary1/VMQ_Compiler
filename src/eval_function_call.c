#include "eval.h"

/*
    This eval function does not actually generate the function call statement itself (i.e., "c <#> <#>").
    This is due to the fact that the 1st operand of the function call statement is used to determine
    where the return value of the function is placed - this is dependent on the context of the function call.
*/
void evalFuncCall(struct AST_node *a, struct var_list_node *param)
{
	if (!a || a->nodetype == 0 || param == NULL)
		return;

	char op_code;
	char *addr_mode = NULL;
	struct func_list_node *func = NULL, *target_func = NULL;
	struct var *v = NULL;
	struct intlit *i_lit = NULL;
	struct fltlit *f_lit = NULL;
	struct VMQ_temp_node *result = &CURRENT_FUNC->VMQ_data.math_result;
	struct AST_node *arg;
	unsigned int param_type = (param) ? param->pv->var_type : 0;
	unsigned int orig_size, temp_addr = CURRENT_FUNC->VMQ_data.tempvar_cur_addr;

	if (a->r && a->nodetype == FUNC_CALL)
		arg = a->r;
	else
		arg = a;

	// Parameter(s) are any type of expression:
	// ASSIGNOPs, INCOPs, ADDOPs, MULOPs, VAR/ARR_ACCESSes, INT/FLT_LITERALS, or other FUNC_CALLs.
	switch (arg->nodetype)
	{
	// Highest level - expression list case.
	case EXPRS:
		orig_size = CURRENT_FUNC->VMQ_data.tempvar_cur_size;

		target_func = ((struct func_node *)a->l)->val;
		param = target_func->param_list_tail;

		evalFuncCall(arg->r, param);

		if (arg->r->nodetype == FUNC_CALL)
		{
			// Code for handling nested function calls (i.e., function calls used as arguments).
			// The nested function call should already have been evaluated, so we'll just handle the
			// code for generating the appropriate VMQ statements to handle the function call itself.

			func = ((struct func_node *)arg->r->l)->val;

			if (func->return_type == INT)
				temp_addr = getNewTempVar(INT);
			else
				temp_addr = getNewTempVar(FLOAT);

			// Call function, store return value in temporary variable
			sprintf(VMQ_line, "c #/-%d %d", temp_addr, func->VMQ_data.quad_start_line);
			appendToVMQList(VMQ_line);

			// Pop the called functions parameters off of the stack.
			sprintf(VMQ_line, "^ %d", func->param_count * VMQ_ADDR_SIZE);
			appendToVMQList(VMQ_line);

			// Push the result of the function call onto the stack.
			sprintf(VMQ_line, "p #/-%d", temp_addr);
			appendToVMQList(VMQ_line);
		}

		evalFuncCall(arg->l, param->prev);

		if (arg->l->nodetype == FUNC_CALL)
		{
			// Code for handling nested function calls (i.e., function calls used as arguments).
			// The nested function call should already have been evaluated, so we'll just handle the
			// code for generating the appropriate VMQ statements to handle the function call itself.

			func = ((struct func_node *)arg->l->l)->val;

			if (func->return_type == INT)
				temp_addr = getNewTempVar(INT);
			else
				temp_addr = getNewTempVar(FLOAT);
			 
			// Call function, store return value in temporary variable
			sprintf(VMQ_line, "c #/-%d %d", temp_addr, func->VMQ_data.quad_start_line);
			appendToVMQList(VMQ_line);

			// Pop the called functions parameters off of the stack.
			sprintf(VMQ_line, "^ %d", func->param_count * VMQ_ADDR_SIZE);
			appendToVMQList(VMQ_line);

			// Push the result of the function call onto the stack.
			sprintf(VMQ_line, "p #/-%d", temp_addr);
			appendToVMQList(VMQ_line);
		}

		while (CURRENT_FUNC->VMQ_data.tempvar_cur_size != orig_size)
			freeTempVar();

		break;

	// Lower level - terminal evaluation cases and push statements.

	// Push the address of the l_val/variable onto the stack.
	case ASSIGNOP:
	case ADD_ASSIGN:
	case SUB_ASSIGN:
		if (arg->nodetype == ASSIGNOP)
			evalAssignOp(arg);
		else
			evalIncOp(arg);

		v = ((struct var_node *)arg->l)->val;

	case VAR_ACCESS:
	case ARR_ACCESS:
		if (arg->nodetype == VAR_ACCESS)
		{
			v = ((struct var_node *)arg)->val;

			if (v->isGlobal)
				addr_mode = "#";
			else if (v->isParam)
				addr_mode = "/";
			else
				addr_mode = "#/-";
		}
		else if (arg->nodetype == ARR_ACCESS)
		{
			evalArrAccess(arg);

			result->VMQ_loc = getNewTempVar(ADDR);

			v = ((struct var_node *)arg->l)->val;

			addr_mode = "/-";
		}
		 
		if (v->var_type == param_type)
		{
			if (arg->nodetype == ARR_ACCESS)
				sprintf(VMQ_line, "p %s%d", addr_mode, result->VMQ_loc);
			else
				sprintf(VMQ_line, "p %s%d", addr_mode, v->VMQ_loc);

			appendToVMQList(VMQ_line);
		}
		else // need to cast the value to the correct type before pushing.
		{
			unsigned int src_addr;
			if (arg->nodetype == ARR_ACCESS)
			{
				src_addr = result->VMQ_loc;
				freeTempVar();
				addr_mode = "@/-";
			}
			else // arg->nodetype == VAR_ACCESS
			{
				src_addr = v->VMQ_loc;
				addr_mode = "/-";
			}

			if (param_type == INT)
			{
				op_code = 'f';
				temp_addr = getNewTempVar(INT);
			}
			else // param_type == FLOAT
			{
				op_code = 'F';
				temp_addr = getNewTempVar(FLOAT);
			}

			// Generate the cast statement
			sprintf(VMQ_line, "%c %s%d /-%d", op_code, addr_mode, src_addr, temp_addr);
			appendToVMQList(VMQ_line);

			// Generate the push statement
			sprintf(VMQ_line, "p #/-%d", temp_addr);
			appendToVMQList(VMQ_line);
		}

		break;

	// Push the result of the math op onto the stack.
	case ADD:
	case SUB:
	case MUL:
	case DIV:
	case MOD:
		evalMath(arg);

		if (result->type == param_type)
		{
			sprintf(VMQ_line, "p #/-%d", result->VMQ_loc);
			appendToVMQList(VMQ_line);
		}
		else
		{
			op_code = (param_type == INT) ? 'f' : 'F';
			temp_addr = (op_code == 'f') ? getNewTempVar(INT) : getNewTempVar(FLOAT);

			sprintf(VMQ_line, "%c /-%d /-%d", op_code, result->VMQ_loc, temp_addr);
			appendToVMQList(VMQ_line);
			sprintf(VMQ_line, "p #/-%d", temp_addr);
			appendToVMQList(VMQ_line);

			freeTempVar();
		}

		break;

	// Push the int/float literal onto the stack.
	case INT_LITERAL:
	case FLT_LITERAL:		 
		if (arg->nodetype == INT_LITERAL)
		{
			i_lit = ((struct int_node *)arg)->val;

			if (param_type == INT)
			{
				sprintf(VMQ_line, "p #%d", i_lit->VMQ_loc);
				appendToVMQList(VMQ_line);
			}
			else
			{
				temp_addr = getNewTempVar(FLOAT);
				sprintf(VMQ_line, "F %d /-%d", i_lit->VMQ_loc, temp_addr);
				appendToVMQList(VMQ_line);
				sprintf(VMQ_line, "p #/-%d", temp_addr);
				appendToVMQList(VMQ_line);
				freeTempVar();
			}
		}
		else
		{
			f_lit = ((struct flt_node *)arg)->val;

			if (param_type == FLOAT)
			{
				sprintf(VMQ_line, "p #%d", f_lit->VMQ_loc);
				appendToVMQList(VMQ_line);
			}
			else
			{
				temp_addr = getNewTempVar(INT);
				sprintf(VMQ_line, "f %d /-%d", f_lit->VMQ_loc, temp_addr);
				appendToVMQList(VMQ_line);
				sprintf(VMQ_line, "p #/-%d", temp_addr);
				appendToVMQList(VMQ_line);
				freeTempVar();
			}
		}

		break;
	}
}
