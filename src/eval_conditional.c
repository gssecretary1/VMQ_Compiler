#include "eval.h"
#include "conditional_helper_functions.h"

void evalCond(struct cond_list* list)
{
    struct cond_list_node *cond_ptr = list->head;
    unsigned int *cur_line_num = &(CURRENT_FUNC->VMQ_data.quad_end_line);

    while (cond_ptr)
    {
        struct logic_node *ptr = cond_ptr->val;

        unsigned int orig_temp_size = CURRENT_FUNC->VMQ_data.tempvar_cur_size;
        struct relop_node *l_relop, *r_relop;
        struct AST_node *lhs = NULL, *rhs = NULL;
        char relop_code;
        char *lhs_addr_mode = NULL, *rhs_addr_mode = NULL;
        unsigned int lhs_addr, lhs_type, rhs_addr, rhs_type;

        if (ptr->l && isRelOp(ptr->l->nodetype))
            l_relop = ((struct relop_node *)ptr->l);
        else
            l_relop = NULL;

        if (ptr->r && isRelOp(ptr->r->nodetype))
            r_relop = ((struct relop_node *)ptr->r);
        else
            r_relop = NULL;

        if (!l_relop && !r_relop)
            yyerror("evalCond() - A logic_node with no relop_node children ended up in the conditional list!");

        // Case:  No logical operations in boolean expression - just a simple RELOP.
        if (ptr->nodetype == 0)
        {
            lhs = l_relop->l;
            rhs = l_relop->r;
            l_relop->line_start = *cur_line_num + 1;

            // Evaluate the LHS of the relational operation.
            switch(lhs->nodetype)
            {
                case ADD:
            case SUB:
            case MUL:
            case DIV:
            case MOD:
                evalMath(lhs);
                lhs_addr = CURRENT_FUNC->VMQ_data.math_result.VMQ_loc;
                lhs_type = CURRENT_FUNC->VMQ_data.math_result.type;
                lhs_addr_mode = "/-";
                break;

            case VAR_ACCESS:
                lhs_addr = ((struct var_node *)lhs)->val->VMQ_loc;
                lhs_type = ((struct var_node *)lhs)->val->var_type;

                if (((struct var_node*)lhs)->val->isGlobal)
                    lhs_addr_mode = "";
                else
                    lhs_addr_mode = (((struct var_node*)lhs)->val->isParam) ? "@/" : "/-";
                break;

            case FUNC_CALL:
                evalFuncCall(lhs, ((struct func_node *)lhs)->val->param_list_tail);
                lhs_type = ((struct func_node *)lhs)->val->return_type;
                lhs_addr = (lhs_type == INT) ? getNewTempVar(INT) : getNewTempVar(FLOAT);
                lhs_addr_mode = "/-";
                sprintf(VMQ_line, "c /-%d %d", lhs_addr, ((struct func_node *)lhs)->val->VMQ_data.quad_start_line);
                appendToVMQList(VMQ_line);
                sprintf(VMQ_line, "^ %d", ((struct func_node *)lhs)->val->param_count * VMQ_ADDR_SIZE);
                appendToVMQList(VMQ_line);
                break;

            case INT_LITERAL:
            case FLT_LITERAL:
                lhs_type = (lhs->nodetype == INT_LITERAL) ? INT : FLOAT;
                lhs_addr = (lhs_type == INT) ? (((struct int_node *)lhs)->val->VMQ_loc) : (((struct flt_node *)lhs)->val->VMQ_loc);
                lhs_addr_mode = "";
                break;

            case ARR_ACCESS:
                evalArrAccess(lhs);
                lhs_type = ((struct var_node *)lhs->l)->val->var_type;
                lhs_addr = getNewTempVar(ADDR);
                lhs_addr_mode = "@/-";
                break;
            }

            // Evaluate the RHS of the relational operation.
            switch (rhs->nodetype)
            {
            case ADD:
            case SUB:
            case MUL:
            case DIV:
            case MOD:
                evalMath(rhs);
                rhs_addr = CURRENT_FUNC->VMQ_data.math_result.VMQ_loc;
                rhs_type = CURRENT_FUNC->VMQ_data.math_result.type;
                rhs_addr_mode = "/-";
                break;

            case VAR_ACCESS:
                rhs_addr = ((struct var_node *)rhs)->val->VMQ_loc;
                rhs_type = ((struct var_node *)rhs)->val->var_type;
                if (((struct var_node*)rhs)->val->isGlobal)
                    rhs_addr_mode = "";
                else
                    rhs_addr_mode = (((struct var_node*)rhs)->val->isParam) ? "@/" : "/-";
                break;

            case FUNC_CALL:
                evalFuncCall(rhs, ((struct func_node *)rhs)->val->param_list_tail);
                rhs_type = ((struct func_node *)rhs)->val->return_type;
                rhs_addr = (rhs_type == INT) ? getNewTempVar(INT) : getNewTempVar(FLOAT);
                rhs_addr_mode = "/-";
                sprintf(VMQ_line, "c /-%d %d", rhs_addr, ((struct func_node *)rhs)->val->VMQ_data.quad_start_line);
                appendToVMQList(VMQ_line);
                sprintf(VMQ_line, "^ %d", ((struct func_node *)rhs)->val->param_count * VMQ_ADDR_SIZE);
                appendToVMQList(VMQ_line);
                break;

            case INT_LITERAL:
            case FLT_LITERAL:
                rhs_type = (rhs->nodetype == INT_LITERAL) ? INT : FLOAT;
                rhs_addr = (rhs_type == INT) ? (((struct int_node *)rhs)->val->VMQ_loc) : (((struct flt_node *)rhs)->val->VMQ_loc);
                rhs_addr_mode = "";
                break;

            case ARR_ACCESS:
                evalArrAccess(rhs);
                rhs_type = ((struct var_node *)rhs->l)->val->var_type;
                rhs_addr = getNewTempVar(ADDR);
                rhs_addr_mode = "@/-";
                break;
            }

            // If the data type of the LHS doesn't match the RHS, we need to perform a cast prior to executing the conditional code.
            if (lhs_type != rhs_type)
            {

                // Types don't match, so we need to perform a cast before comparing values.
                unsigned int temp_addr = getNewTempVar(FLOAT);
                if (lhs_type == INT)
                {
                    sprintf(VMQ_line, "F %s%d /-%d", lhs_addr_mode, lhs_addr, temp_addr);
                    appendToVMQList(VMQ_line);
                    lhs_addr_mode = "/-";
                    lhs_addr = temp_addr;
                }
                else // rhs_type == INT
                {
                    sprintf(VMQ_line, "F %s%d /-%d", rhs_addr_mode, rhs_addr, temp_addr);
                    appendToVMQList(VMQ_line);
                    rhs_addr_mode = "/-";
                    rhs_addr = temp_addr;
                }

                if (l_relop->nodetype == LT || l_relop->nodetype == GTE)
                    relop_code = 'L';
                else if (l_relop->nodetype == GT || l_relop->nodetype == LTE)
                    relop_code = 'G';
                else
                    relop_code = 'E';

                sprintf(VMQ_line, "%c %s%d %s%d", relop_code, lhs_addr_mode, lhs_addr, rhs_addr_mode, rhs_addr);
                appendToVMQList(VMQ_line);
            }
            else // lhs_type == rhs_type : No cast required.
            {
                if (l_relop->nodetype == LT || l_relop->nodetype == GTE)
                    relop_code = 'L';
                else if (l_relop->nodetype == GT || l_relop->nodetype == LTE)
                    relop_code = 'G';
                else
                    relop_code = 'E';

                // Use the lowercase op code for INTs
                if (lhs_type == INT)
                    relop_code += 32;

                sprintf(VMQ_line, "%c %s%d %s%d", relop_code, lhs_addr_mode, lhs_addr, rhs_addr_mode, rhs_addr);
                appendToVMQList(VMQ_line);
            }

            l_relop->cond_jump_stmt = &CURRENT_FUNC->VMQ_data.stmt_list_tail->VMQ_line;

            // If using a supported relational operator (<, >, or ==), then the jump target
            // for that statement will be for the true codeblock.  Since this is the codeblock
            // immediately following the conditional statement, we need to insert another jump
            // statement between the conditional and true codeblock for when the conditional
            // fails and we want to execute the false codeblock.
            // Again: this only applies to relational operators "<", ">", and "==".
            if(isSupportedRelop(l_relop->nodetype))
            {
                appendToVMQList("");
                l_relop->uncond_jump_stmt = &CURRENT_FUNC->VMQ_data.stmt_list_tail->VMQ_line;
            }

        }
        else // Boolean expression contains one or more logical operations (|| &&)
        {
            // Evaluate the LHS of the logical operation
            if (l_relop)
            {
                lhs = l_relop->l;
                rhs = l_relop->r;
                l_relop->line_start = *cur_line_num + 1;

                // Evaluate the LHS of the relational operator.
                switch (lhs->nodetype)
                {
                case ADD:
                case SUB:
                case MUL:
                case DIV:
                case MOD:
                    evalMath(lhs);
                    lhs_addr = CURRENT_FUNC->VMQ_data.math_result.VMQ_loc;
                    lhs_type = CURRENT_FUNC->VMQ_data.math_result.type;
                    lhs_addr_mode = "/-";
                    break;

                case VAR_ACCESS:
                    lhs_addr = ((struct var_node *)lhs)->val->VMQ_loc;
                    lhs_type = ((struct var_node *)lhs)->val->var_type;
                    if (((struct var_node*)lhs)->val->isGlobal)
                        lhs_addr_mode = "";
                    else
                        lhs_addr_mode = (((struct var_node*)lhs)->val->isParam) ? "@/" : "/-";
                    break;

                case FUNC_CALL:
                    evalFuncCall(lhs, ((struct func_node *)lhs)->val->param_list_tail);
                    lhs_type = ((struct func_node *)lhs)->val->return_type;
                    lhs_addr = (lhs_type == INT) ? getNewTempVar(INT) : getNewTempVar(FLOAT);
                    lhs_addr_mode = "/-";
                    sprintf(VMQ_line, "c /-%d %d", lhs_addr, ((struct func_node *)lhs)->val->VMQ_data.quad_start_line);
                    appendToVMQList(VMQ_line);
                    sprintf(VMQ_line, "^ %d", ((struct func_node *)lhs)->val->param_count * VMQ_ADDR_SIZE);
                    appendToVMQList(VMQ_line);
                    break;

                case INT_LITERAL:
                case FLT_LITERAL:
                    lhs_type = (lhs->nodetype == INT_LITERAL) ? INT : FLOAT;
                    lhs_addr = (lhs_type == INT) ? (((struct int_node *)lhs)->val->VMQ_loc) : (((struct flt_node *)lhs)->val->VMQ_loc);
                    lhs_addr_mode = "";
                    break;

                case ARR_ACCESS:
                    evalArrAccess(lhs);
                    lhs_type = ((struct var_node *)lhs->l)->val->var_type;
                    lhs_addr = getNewTempVar(ADDR);
                    lhs_addr_mode = "@/-";
                    break;
                }

                // Evaluate the RHS of the relational operator.
                switch (rhs->nodetype)
                {
                case ADD:
                case SUB:
                case MUL:
                case DIV:
                case MOD:
                    evalMath(rhs);
                    rhs_addr = CURRENT_FUNC->VMQ_data.math_result.VMQ_loc;
                    rhs_type = CURRENT_FUNC->VMQ_data.math_result.type;
                    rhs_addr_mode = "/-";
                    break;

                case VAR_ACCESS:
                    rhs_addr = ((struct var_node *)rhs)->val->VMQ_loc;
                    rhs_type = ((struct var_node *)rhs)->val->var_type;
                    if (((struct var_node*)rhs)->val->isGlobal)
                        rhs_addr_mode = "";
                    else
                        rhs_addr_mode = (((struct var_node*)rhs)->val->isParam) ? "@/" : "/-";
                    break;

                case FUNC_CALL:
                    evalFuncCall(rhs, ((struct func_node *)rhs)->val->param_list_tail);
                    rhs_type = ((struct func_node *)rhs)->val->return_type;
                    rhs_addr = (rhs_type == INT) ? getNewTempVar(INT) : getNewTempVar(FLOAT);
                    rhs_addr_mode = "/-";
                    sprintf(VMQ_line, "c /-%d %d", rhs_addr, ((struct func_node *)rhs)->val->VMQ_data.quad_start_line);
                    appendToVMQList(VMQ_line);
                    sprintf(VMQ_line, "^ %d", ((struct func_node *)rhs)->val->param_count * VMQ_ADDR_SIZE);
                    appendToVMQList(VMQ_line);
                    break;

                case INT_LITERAL:
                case FLT_LITERAL:
                    rhs_type = (rhs->nodetype == INT_LITERAL) ? INT : FLOAT;
                    rhs_addr = (rhs_type == INT) ? (((struct int_node *)rhs)->val->VMQ_loc) : (((struct flt_node *)rhs)->val->VMQ_loc);
                    rhs_addr_mode = "";
                    break;

                case ARR_ACCESS:
                    evalArrAccess(rhs);
                    rhs_type = ((struct var_node *)rhs->l)->val->var_type;
                    rhs_addr = getNewTempVar(ADDR);
                    rhs_addr_mode = "@/-";
                    break;
                }

                // Check if casting is required.
                if (lhs_type != rhs_type)
                {
                    // Types don't match, so we need to perform a cast before comparing values.
                    unsigned int temp_addr = getNewTempVar(FLOAT);
                    if (lhs_type == INT)
                    {
                        sprintf(VMQ_line, "F %s%d /-%d", lhs_addr_mode, lhs_addr, temp_addr);
                        appendToVMQList(VMQ_line);
                        lhs_addr_mode = "/-";
                        lhs_addr = temp_addr;
                    }
                    else // rhs_type == INT
                    {
                        sprintf(VMQ_line, "F %s%d /-%d", rhs_addr_mode, rhs_addr, temp_addr);
                        appendToVMQList(VMQ_line);
                        rhs_addr_mode = "/-";
                        rhs_addr = temp_addr;
                    }

                    if (l_relop->nodetype == LT || l_relop->nodetype == GTE)
                        relop_code = 'L';
                    else if (l_relop->nodetype == GT || l_relop->nodetype == LTE)
                        relop_code = 'G';
                    else
                        relop_code = 'E';

                    sprintf(VMQ_line, "%c %s%d %s%d", relop_code, lhs_addr_mode, lhs_addr, rhs_addr_mode, rhs_addr);
                    appendToVMQList(VMQ_line);
                }
                else // lhs_type == rhs_type : No casting required.
                {
                    if (l_relop->nodetype == LT || l_relop->nodetype == GTE)
                        relop_code = 'L';
                    else if (l_relop->nodetype == GT || l_relop->nodetype == LTE)
                        relop_code = 'G';
                    else
                        relop_code = 'E';

                    // Use the lowercase op code for INTs
                    if (lhs_type == INT)
                        relop_code += 32;

                    sprintf(VMQ_line, "%c %s%d %s%d", relop_code, lhs_addr_mode, lhs_addr, rhs_addr_mode, rhs_addr);
                    appendToVMQList(VMQ_line);
                }

                l_relop->cond_jump_stmt = &CURRENT_FUNC->VMQ_data.stmt_list_tail->VMQ_line;

                // True codeblock is first
                // If the LHS of an AND evaluates to true, then we want to immediately begin evaluating the RHS of the
                // AND - if it's false then we jump to the short circuit target.  If using a supported relop, we need an
                // additional jump statement.  For OR, evaluating to true causes a short-circuit while false causes the need
                // the evaluate the RHS.  Unsupported relational operators for OR require an additional jump.
                if ((ptr->nodetype == AND && isSupportedRelop(l_relop->nodetype)) || (ptr->nodetype == OR && isUnsupportedRelop(l_relop->nodetype)))
                {
                    // Dummy unconditional jump statement
                    appendToVMQList("");
                    l_relop->uncond_jump_stmt = &CURRENT_FUNC->VMQ_data.stmt_list_tail->VMQ_line;
                }
            }

            // Evaluate the RHS of the logical operation (if available: "RHS" may be some other logical operation).
            if (r_relop)
            {
                lhs = r_relop->l;
                rhs = r_relop->r;
                r_relop->line_start = *cur_line_num + 1;

                // Evaluate LHS of relational operator.
                switch (lhs->nodetype)
                {
                case ADD:
                case SUB:
                case MUL:
                case DIV:
                case MOD:
                    evalMath(lhs);
                    lhs_addr = CURRENT_FUNC->VMQ_data.math_result.VMQ_loc;
                    lhs_type = CURRENT_FUNC->VMQ_data.math_result.type;
                    lhs_addr_mode = "/-";
                    break;

                case VAR_ACCESS:
                    lhs_addr = ((struct var_node *)lhs)->val->VMQ_loc;
                    lhs_type = ((struct var_node *)lhs)->val->var_type;
                    if (((struct var_node*)lhs)->val->isGlobal)
                        lhs_addr_mode = "";
                    else
                        lhs_addr_mode = (((struct var_node*)lhs)->val->isParam) ? "@/" : "/-";
                    break;

                case FUNC_CALL:
                    evalFuncCall(lhs, ((struct func_node *)lhs)->val->param_list_tail);
                    lhs_type = ((struct func_node *)lhs)->val->return_type;
                    lhs_addr = (lhs_type == INT) ? getNewTempVar(INT) : getNewTempVar(FLOAT);
                    lhs_addr_mode = "/-";
                    sprintf(VMQ_line, "c /-%d %d", lhs_addr, ((struct func_node *)lhs)->val->VMQ_data.quad_start_line);
                    appendToVMQList(VMQ_line);
                    sprintf(VMQ_line, "^ %d", ((struct func_node *)lhs)->val->param_count * VMQ_ADDR_SIZE);
                    appendToVMQList(VMQ_line);
                    break;

                case INT_LITERAL:
                case FLT_LITERAL:
                    lhs_type = (lhs->nodetype == INT_LITERAL) ? INT : FLOAT;
                    lhs_addr = (lhs_type == INT) ? (((struct int_node *)lhs)->val->VMQ_loc) : (((struct flt_node *)lhs)->val->VMQ_loc);
                    lhs_addr_mode = "";
                    break;

                case ARR_ACCESS:
                    evalArrAccess(lhs);
                    lhs_type = ((struct var_node *)lhs->l)->val->var_type;
                    lhs_addr = getNewTempVar(ADDR);
                    lhs_addr_mode = "@/-";
                    break;
                }

                // Evaluate RHS of relational operator.
                switch (rhs->nodetype)
                {
                case ADD:
                case SUB:
                case MUL:
                case DIV:
                case MOD:
                    evalMath(rhs);
                    rhs_addr = CURRENT_FUNC->VMQ_data.math_result.VMQ_loc;
                    rhs_type = CURRENT_FUNC->VMQ_data.math_result.type;
                    rhs_addr_mode = "/-";
                    break;

                case VAR_ACCESS:
                    rhs_addr = ((struct var_node *)rhs)->val->VMQ_loc;
                    rhs_type = ((struct var_node *)rhs)->val->var_type;
                    if (((struct var_node*)rhs)->val->isGlobal)
                        rhs_addr_mode = "";
                    else
                        rhs_addr_mode = (((struct var_node*)rhs)->val->isParam) ? "@/" : "/-";
                    break;

                case FUNC_CALL:
                    evalFuncCall(rhs, ((struct func_node *)rhs)->val->param_list_tail);
                    rhs_type = ((struct func_node *)rhs)->val->return_type;
                    rhs_addr = (rhs_type == INT) ? getNewTempVar(INT) : getNewTempVar(FLOAT);
                    rhs_addr_mode = "/-";
                    sprintf(VMQ_line, "c /-%d %d", rhs_addr, ((struct func_node *)rhs)->val->VMQ_data.quad_start_line);
                    appendToVMQList(VMQ_line);
                    sprintf(VMQ_line, "^ %d", ((struct func_node *)rhs)->val->param_count * VMQ_ADDR_SIZE);
                    appendToVMQList(VMQ_line);
                    break;

                case INT_LITERAL:
                case FLT_LITERAL:
                    rhs_type = (rhs->nodetype == INT_LITERAL) ? INT : FLOAT;
                    rhs_addr = (rhs_type == INT) ? (((struct int_node *)rhs)->val->VMQ_loc) : (((struct flt_node *)rhs)->val->VMQ_loc);
                    rhs_addr_mode = "";
                    break;

                case ARR_ACCESS:
                    evalArrAccess(rhs);
                    rhs_type = ((struct var_node *)rhs->l)->val->var_type;
                    rhs_addr = getNewTempVar(ADDR);
                    rhs_addr_mode = "@/-";
                    break;
                }

                // Check to see if we need to cast.
                if (lhs_type != rhs_type)
                {
                    // Types don't match, so we need to perform a cast before comparing values.
                    unsigned int temp_addr = getNewTempVar(FLOAT);
                    if (lhs_type == INT)
                    {
                        sprintf(VMQ_line, "F %s%d /-%d", lhs_addr_mode, lhs_addr, temp_addr);
                        appendToVMQList(VMQ_line);
                        lhs_addr_mode = "/-";
                        lhs_addr = temp_addr;
                    }
                    else // rhs_type == INT
                    {
                        sprintf(VMQ_line, "F %s%d /-%d", rhs_addr_mode, rhs_addr, temp_addr);
                        appendToVMQList(VMQ_line);
                        rhs_addr_mode = "/-";
                        rhs_addr = temp_addr;
                    }

                    if (r_relop->nodetype == LT || r_relop->nodetype == GTE)
                        relop_code = 'L';
                    else if (r_relop->nodetype == GT || r_relop->nodetype == LTE)
                        relop_code = 'G';
                    else
                        relop_code = 'E';

                    sprintf(VMQ_line, "%c %s%d %s%d", relop_code, lhs_addr_mode, lhs_addr, rhs_addr_mode, rhs_addr);
                    appendToVMQList(VMQ_line);
                }
                else // lhs_type == rhs_type : No casting required.
                {
                    if (r_relop->nodetype == LT || r_relop->nodetype == GTE)
                        relop_code = 'L';
                    else if (r_relop->nodetype == GT || r_relop->nodetype == LTE)
                        relop_code = 'G';
                    else
                        relop_code = 'E';

                    // Use the lowercase op code for INTs
                    if (lhs_type == INT)
                        relop_code += 32;

                    sprintf(VMQ_line, "%c %s%d %s%d", relop_code, lhs_addr_mode, lhs_addr, rhs_addr_mode, rhs_addr);
                    appendToVMQList(VMQ_line);
                }

                r_relop->cond_jump_stmt = &CURRENT_FUNC->VMQ_data.stmt_list_tail->VMQ_line;

                // Unconditional jump statement is needed after a conditional statement on the RHS of
                // a logical operation if the RHS relational operator is a "<", ">", or "==".
                if(isSupportedRelop(r_relop->nodetype))
                {
                    // Dummy jump statement for the unconditional jump after the conditional statement.
                    appendToVMQList("");
                    r_relop->uncond_jump_stmt = &CURRENT_FUNC->VMQ_data.stmt_list_tail->VMQ_line;
                }
            }
        }

        // Free any temporary variables that were used during current conditional evaluation.
        while (orig_temp_size != CURRENT_FUNC->VMQ_data.tempvar_cur_size)
            freeTempVar();

        cond_ptr = cond_ptr->next;
        
    }
}
