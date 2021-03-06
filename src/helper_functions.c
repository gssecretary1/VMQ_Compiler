#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "helper_functions.h"
#include "parser.tab.h"

void init()
{
    INT_LIST_HEAD = INT_LIST_TAIL = NULL;
    FLT_LIST_HEAD = FLT_LIST_TAIL = NULL;
    STR_LIST_HEAD = STR_LIST_TAIL = NULL;
    GLOBAL_VAR_LIST_HEAD = GLOBAL_VAR_LIST_TAIL = NULL;
    VMQ_MEM_LIST_HEAD = VMQ_MEM_LIST_TAIL = NULL;

    LOGIC_STACK_HEAD = NULL;

    FUNC_LIST_HEAD = CURRENT_FUNC = NULL;
	
    pushScope(&SCOPE_STACK_HEAD);

    GLOBAL_SCOPE = SCOPE_STACK_HEAD;

    DEBUG = FLEX_DEBUG = BISON_DEBUG = 0;
}

void configureGlobalMemorySpace()
{
    /*
*   VMQ Global Memory Layout
*
*   000 0	    ; Literal integer of value 0, stored at addr 000, represents FALSE
*   002 1	    ; Literal integer of value 1, stored at addr 002, represents TRUE
*   004 2	    ; Literal integer of value 2, stored at addr 004, represents size of INT
*   006 4	    ; Literal integer of value 4, stored at addr 006, represents size of FLOAT
*   008 <flt_val>   ; Literal float values will be stored after initial TRUE and FALSE
*   012 <flt_var>   ; Space for any float variables comes after literal floats
*   016 <int_val>   ; Literal integer values will be stored after float values
*   018 <int_var>   ; Space for any integer variables comes after literal integers
*   020 <str_val>   ; Literal string values will be stored after integer values
*   .	.   .	.
*   .	.   .	.
*   .	.   .	.
*
*   Per VMQ Specifications...
*
*   Literal float values take up 32-bits (4 bytes), and must be aligned on a memory addr that is evenly divisible by 4.
*   Literal integer values take up 16-bits (2 bytes), and must be aligned on a memory addr that is evenly divisible by 2.
*   Literal strings values are null-terminated, and have no restrictions on memory addr alignment.
*   This particular layout optimizes VMQ global memory allocation, so that no memory padding (i.e., wasting memory) is needed
    */

    if(DEBUG) { printf("CONFIGURING GLOBAL MEMORY SPACE..."); fflush(stdout); }

    unsigned int mem_addr = 0;

    // Literal floats come next.
    struct flt_list_node* pfln = FLT_LIST_HEAD;
    while(pfln)
    {
	pfln->pfl->VMQ_loc = mem_addr;
	appendToVMQMemList(FLT_LITERAL, pfln);
	mem_addr += VMQ_FLT_SIZE;
	pfln = pfln->next;
    }

    // Scan global var list for float vars, setting their VMQ_loc values.
   /* 
    *	Note that variables are merely allocated space in global memory and will not
    *	appear as literals do in the initial data list at the beginning of each VMQ file
    */
    struct var_list_node* pvln = GLOBAL_VAR_LIST_HEAD;
    while(pvln)
    {
	if(pvln->pv->var_type == FLOAT)
	{
	    pvln->pv->VMQ_loc = mem_addr;
	    mem_addr += VMQ_FLT_SIZE * pvln->pv->size;
	}
	pvln = pvln->next;
    }

    // Literal integers come next.
    struct int_list_node* piln = INT_LIST_HEAD;
    while(piln)
    {
	piln->pil->VMQ_loc = mem_addr;
	appendToVMQMemList(INT_LITERAL, piln);
	mem_addr += VMQ_INT_SIZE;
	piln = piln->next;
    }

    // Rescan global var list for int vars; set their VMQ_loc values.
    pvln = GLOBAL_VAR_LIST_HEAD;
    while(pvln)
    {
	if(pvln->pv->var_type == INT)
	{
	    pvln->pv->VMQ_loc = mem_addr;
	    mem_addr += VMQ_INT_SIZE * pvln->pv->size;
	}

	pvln = pvln->next;
    }

    // Literal strings come next
    struct str_list_node* psln = STR_LIST_HEAD;
    while(psln)
    {
	psln->psl->VMQ_loc = mem_addr;
	appendToVMQMemList(STR_LITERAL, psln);
	if(strcmp(psln->psl->val, "\\n") == 0)
	    mem_addr += 2;
	else
	    mem_addr += strlen(psln->psl->val) - 1;
	psln = psln->next;
    }

    if(DEBUG) { printf("DONE!\n"); fflush(stdout); } 
}

void configureLocalMemorySpaces()
{
    if(DEBUG) { printf("CONFIGURING LOCAL MEMORY SPACES..."); fflush(stdout); }

    CURRENT_FUNC = FUNC_LIST_HEAD;

    while(CURRENT_FUNC)
    {
	// If a function has no params and no local variables, skip to next function.
	if(!CURRENT_FUNC->param_list_head && !CURRENT_FUNC->var_list_head)
	{
	    CURRENT_FUNC->VMQ_data.tempvar_start = CURRENT_FUNC->VMQ_data.tempvar_cur_addr = 4;
	    CURRENT_FUNC->VMQ_data.tempvar_cur_size = 0;
	    CURRENT_FUNC->VMQ_data.tempvar_max_size = 0;

	    CURRENT_FUNC = CURRENT_FUNC->next;
	    continue;
	}

	struct var_list_node* list_ptr = CURRENT_FUNC->param_list_head;
	struct var_list_node* prev_node = NULL;
	struct var* v = NULL;
	
	// Start with the parameters, if any.
	if(list_ptr)
	{
	    unsigned int param_addr = 6;
	    while(list_ptr)
	    {
		v = list_ptr->pv;
		v->VMQ_loc = param_addr;
		param_addr += 2;
		list_ptr = list_ptr->next;
	    }
	}

	// Next do the local variables, if any.
	list_ptr = CURRENT_FUNC->var_list_head;
	
	if(!list_ptr)
	{ 
	    CURRENT_FUNC->VMQ_data.tempvar_start = CURRENT_FUNC->VMQ_data.tempvar_cur_addr = 4;
	    CURRENT_FUNC->VMQ_data.tempvar_cur_size = 0;
	    CURRENT_FUNC->VMQ_data.tempvar_max_size = 0;
	    
	    CURRENT_FUNC = CURRENT_FUNC->next;
	    continue; 
	}

	prev_node = NULL;
	v = NULL;
	unsigned int* var_total_size = &(CURRENT_FUNC->var_total_size);

	// This ensures the first two bytes of local memory space are reserved, since
	// not doing so leads to problems with casting from local VMQ address space /-2.
	*var_total_size = 2;

	// Traverse list, find first INT var or array of odd size - set VMQ_loc to 2;
	while(list_ptr)
	{
	    v = list_ptr->pv;
	    if(v->var_type == INT && v->size % 2)
	    {
		prev_node = list_ptr;
		v->VMQ_loc = VMQ_INT_SIZE * v->size + 2;
		*var_total_size += VMQ_INT_SIZE * list_ptr->pv->size;
		break;
	    }

	    list_ptr = list_ptr->next;
	}

	if(!list_ptr)
	{
	    // Function contains no INT vars or INT arrays of odd size.
	    // Memory cannot be ideally packed - just traverse the list
	    // and set VMQ_locs simply

	    // Do the first variable so we can use prev_node in the loop
	    list_ptr = CURRENT_FUNC->var_list_head;

	    if(list_ptr->pv->var_type == INT)
		list_ptr->pv->VMQ_loc = 4 + (VMQ_INT_SIZE * (list_ptr->pv->size - 1));
	    else
		list_ptr->pv->VMQ_loc = 4 + (VMQ_FLT_SIZE * (list_ptr->pv->size - 1));
	    if(list_ptr->pv->var_type == INT)
		*var_total_size += VMQ_INT_SIZE * list_ptr->pv->size;
	    else
		*var_total_size += VMQ_FLT_SIZE * list_ptr->pv->size;

	    prev_node = list_ptr;
	    list_ptr = list_ptr->next;

	    while(list_ptr)
	    {
		v = prev_node->pv;
		if(v->var_type == INT)
		    list_ptr->pv->VMQ_loc = v->VMQ_loc + list_ptr->pv->size * VMQ_INT_SIZE;
		else
		    list_ptr->pv->VMQ_loc = v->VMQ_loc + list_ptr->pv->size * VMQ_FLT_SIZE;
		
		printf("For VAR %s(%s), VMQ_loc set to %d\n", list_ptr->pv->var_name, nodeTypeToString(list_ptr->pv->var_type), list_ptr->pv->VMQ_loc);
		if(list_ptr->pv->var_type == INT)
		    *var_total_size += VMQ_INT_SIZE * list_ptr->pv->size;
		else
		    *var_total_size += VMQ_FLT_SIZE * list_ptr->pv->size;

		prev_node = list_ptr;
		list_ptr = list_ptr->next;
	    }
	    
	}
	else
	{
	    struct var_list_node* skip_node = prev_node;
	    // Scan for floats, set their VMQ_locs first.
	    list_ptr = CURRENT_FUNC->var_list_head;

	    while(list_ptr)
	    {
		if(list_ptr == skip_node) { list_ptr = list_ptr->next; continue; }
		v = prev_node->pv;
		
		if(list_ptr->pv->var_type == FLOAT)
		{
		    if(v->var_type == INT)		    // ensures first float is properly aligned
			list_ptr->pv->VMQ_loc = v->VMQ_loc + ((v->VMQ_loc + 2) % VMQ_FLT_SIZE) + list_ptr->pv->size * VMQ_INT_SIZE;
		    else
			list_ptr->pv->VMQ_loc = v->VMQ_loc + list_ptr->pv->size * VMQ_FLT_SIZE;

		    *var_total_size += VMQ_FLT_SIZE * list_ptr->pv->size;
		    prev_node = list_ptr;
		}

		list_ptr = list_ptr->next;
	    }

	    // Now scan for ints and set their VMQ_locs
	    list_ptr = CURRENT_FUNC->var_list_head;
	    while(list_ptr)
	    {
		if(list_ptr == skip_node) { list_ptr = list_ptr->next; continue; }
		v = prev_node->pv;
		
		if(list_ptr->pv->var_type == INT)
		{
		    if(v->var_type == INT)
			list_ptr->pv->VMQ_loc = v->VMQ_loc + list_ptr->pv->size * VMQ_INT_SIZE;
		    else
			list_ptr->pv->VMQ_loc = v->VMQ_loc + list_ptr->pv->size * VMQ_FLT_SIZE;

		    *var_total_size += VMQ_INT_SIZE * list_ptr->pv->size;
		    prev_node = list_ptr;
		}

		list_ptr = list_ptr->next;
	    }   
	}

	if(DEBUG) { printf("\n\tCURRENT_FUNC (\"%s\") size of local vars == %d\n", CURRENT_FUNC->func_name, CURRENT_FUNC->var_total_size); fflush(stdout); }


	// Add padding for the temporary variables to ensure that casting can work for the first temporary variable.
	unsigned int temp_start;
	if(prev_node->pv->var_type == INT)
	    temp_start = CURRENT_FUNC->var_total_size + VMQ_INT_SIZE + VMQ_ADDR_SIZE;
	else
	    temp_start = CURRENT_FUNC->var_total_size + VMQ_FLT_SIZE + VMQ_ADDR_SIZE;

	CURRENT_FUNC->VMQ_data.tempvar_start = CURRENT_FUNC->VMQ_data.tempvar_cur_addr = temp_start;
	CURRENT_FUNC->VMQ_data.tempvar_cur_size = CURRENT_FUNC->VMQ_data.tempvar_max_size = 0;

	CURRENT_FUNC = CURRENT_FUNC->next;
    }

    if(DEBUG) { printf("DONE!\n"); fflush(stdout); }
}

void setDebugFlags(int argc, char*** argv)
{
    for(int i = 1; i < argc; ++i)
    {
	if(strcmp((*argv)[i], "-d") == 0)
	{
	    DEBUG = 1;
	    printf("DEBUG MODE ENABLED\n");
	    fflush(stdout);
	}
	else if(strcmp((*argv)[i], "-ld") == 0)
	{
	    FLEX_DEBUG = 1;
	    printf("LEXER DEBUG MODE ENABLED\n");
	    fflush(stdout);
	}
	else if(strcmp((*argv)[i], "-bd") == 0)
	{
	    BISON_DEBUG = 1;
	    printf("PARSER DEBUG MODE ENABLED\n");
	    fflush(stdout);
	}
    }
}

void dumpGlobalDataLists()
{
    printf("==========================\n");
    printf("Dumping LITERAL FLOAT list\n");
    printf("==========================\n\n");
    fflush(stdout);

    struct flt_list_node* pfln = FLT_LIST_HEAD;

    if(!pfln)
	printf("EMPTY\n");
    else
	while(pfln)
	{
	    printf("%03d\t%-15s\t(0x%llX)\n", pfln->pfl->VMQ_loc, pfln->pfl->val, (unsigned long long)pfln);
	    fflush(stdout);
	    pfln = pfln->next;
	}
    
    printf("==========================\n");
    printf("Dumping LITERAL INT list\n");
    printf("==========================\n\n");
    fflush(stdout);

    struct int_list_node* piln = INT_LIST_HEAD;

    if(!piln)
	printf("EMPTY\n");
    else
	while(piln)
	{
	    printf("%03d\t%-15s\t(0x%llX)\n", piln->pil->VMQ_loc, piln->pil->val, (unsigned long long)piln);
	    fflush(stdout);
	    piln = piln->next;
	}

    printf("==========================\n");
    printf("Dumping LITERAL STR list\n");
    printf("==========================\n\n");
    fflush(stdout);
    
    struct str_list_node* psln = STR_LIST_HEAD;
    
    if(!psln)
	printf("EMPTY\n");
    else
	while(psln)
	{
	    if(strcmp(psln->psl->val, "\\n") == 0)
		printf("%03d\t%-30s\t(0x%llX)\n", psln->psl->VMQ_loc, "\"\\n\"", (unsigned long long)psln);
	    else
		printf("%03d\t%-30s\t(0x%llX)\n", psln->psl->VMQ_loc, psln->psl->val, (unsigned long long)psln);

	    fflush(stdout);
	    psln = psln->next;
	}

    printf("==========================\n");
    printf("Dumping GLOBAL VAR list\n");
    printf("==========================\n\n");
    fflush(stdout);

    struct var_list_node* pvln = GLOBAL_VAR_LIST_HEAD;

    if(!pvln)
	printf("EMPTY\n");
    else
	while(pvln)
	{
	    printf("%03d\t", pvln->pv->VMQ_loc);			    fflush(stdout);
	    printf("%-15s\t", pvln->pv->var_name);			    fflush(stdout);
	    printf("%-5s\t", nodeTypeToString(pvln->pv->var_type));	    fflush(stdout);
	    printf("%-3d\t", pvln->pv->size);				    fflush(stdout);
	    printf("(0x%llX)\n", (unsigned long long)pvln);		    fflush(stdout);
	    pvln = pvln->next;
	}
    
    printf("==========================\n");
    printf("Dumping FUNC LIST\n");
    printf("==========================\n\n");
    fflush(stdout);

    struct func_list_node* temp = CURRENT_FUNC;
    CURRENT_FUNC = FUNC_LIST_HEAD;

    if(!CURRENT_FUNC)
	printf("EMPTY\n");
    else
	while(CURRENT_FUNC)
	{
	    printf("%-15s\t(0x%llX)\n", CURRENT_FUNC->func_name, (unsigned long long)CURRENT_FUNC);
	    printf("Quad Start: %d || Quad End: %d\n", CURRENT_FUNC->VMQ_data.quad_start_line, CURRENT_FUNC->VMQ_data.quad_end_line);
	    fflush(stdout);
	    
	    struct var_list_node* paramPtr = CURRENT_FUNC->param_list_head, *varPtr = CURRENT_FUNC->var_list_head;
	    
	    printf("\t--> Dumping %s\'s parameters:\n", CURRENT_FUNC->func_name); fflush(stdout);
	    if(!paramPtr){ printf("\tPARAM LIST EMPTY!\n"); fflush(stdout); }
	    while(paramPtr)
	    {
		struct var* v = paramPtr->pv;
		printf("\t%-15s\t", v->var_name);				fflush(stdout);
		printf("%s(%d)\t", nodeTypeToString(v->var_type), v->size);	fflush(stdout);
		printf("@/%d\t", v->VMQ_loc);					fflush(stdout);
		printf("(0x%llX)\n", (unsigned long long)paramPtr);		fflush(stdout);
		    
		paramPtr = paramPtr->next;
	    }

	    printf("\t--> Dumping %s\'s vars:\n", CURRENT_FUNC->func_name); fflush(stdout);
	    if(!varPtr){ printf("\tVAR LIST EMPTY!\n"); fflush(stdout); }
	    while(varPtr)
	    {
		struct var* v = varPtr->pv;
		printf("\t%-15s\t", v->var_name);				fflush(stdout);
		printf("%s(%d)\t", nodeTypeToString(v->var_type), v->size);	fflush(stdout);
		if(v->isGlobal)
		    printf("%03d\t", v->VMQ_loc);
		else
		    printf("/-%d\t", v->VMQ_loc);
		fflush(stdout);
		printf("(0x%llX)\n", (unsigned long long)varPtr);		fflush(stdout);
		varPtr = varPtr->next;
	    }

	    CURRENT_FUNC = CURRENT_FUNC->next;
	}
    // Restore value.
    CURRENT_FUNC = temp;

    printf("==========================\n");
    printf("Dumping VMQ MEMORY LIST\n");
    printf("==========================\n\n");
    fflush(stdout);

    struct VMQ_mem_node* pvmn = VMQ_MEM_LIST_HEAD;

    if(!pvmn)
	printf("EMPTY\n");
    else
	while(pvmn)
	{
	    switch(pvmn->nodetype)
	    {
		struct intlit* pil;
		struct fltlit* pfl;
		struct strlit* psl;
		case INT_LITERAL:   pil = ((struct int_list_node*)pvmn->node)->pil;
				    printf("%03d\t%-25s(0x%llX)\n", pil->VMQ_loc, pil->val, (unsigned long long)pvmn->node);
				    break;

		case FLT_LITERAL:   pfl = ((struct flt_list_node*)pvmn->node)->pfl;
				    printf("%03d\t%-25s(0x%llX)\n", pfl->VMQ_loc, pfl->val, (unsigned long long)pvmn->node);
				    break;

		case STR_LITERAL:   psl = ((struct str_list_node*)pvmn->node)->psl;
				    if(strcmp(psl->val, "\\n") == 0)
					printf("%03d\t\"\\n\"\t\t(0x%llX)\n", psl->VMQ_loc, (unsigned long long)pvmn->node);
				    else
					printf("%03d\t%-25s(0x%llX)\n", psl->VMQ_loc, psl->val, (unsigned long long)pvmn->node);
				    break;
	    }
	    
	    fflush(stdout);
	    pvmn = pvmn->next;
	}

    printf("==========================\n\n");
    fflush(stdout);
}

char* nodeTypeToString(int nodetype)
{
    char* str = NULL;
    switch(nodetype)
    {
	/* "Non-terminal" cases */
	case PROG:	    str = "PROG"; break;
	case FUNC_DEF:	    str = "FUNC_DEF"; break;
	case FUNC_DEFS:	    str = "FUNC_DEFS"; break;
	case ID_LIST:	    str = "ID_LIST"; break;
	case VAR_DEFS:	    str = "VAR_DEFS"; break;
	case FUNC_HEAD:	    str = "FUNC_HEAD"; break;
	case PARAM_LIST:    str = "PARAM_LIST"; break;
	case PARAMS:	    str = "PARAM"; break;
	case BLOCK:	    str = "BLOCK"; break;
	case STMTS:	    str = "STMTS"; break;
	case STMT:	    str = "STMT"; break;
	case INPUT:	    str = "INPUT"; break;
	case OUTPUT:	    str = "OUTPUT"; break;
	case EXPRS:	    str = "EXPRS"; break;
	case ADD_ASSIGN:    str = "ADD_ASSIGN"; break;
	case SUB_ASSIGN:    str = "SUB_ASSIGN"; break;
	case ADD:	    str = "ADD"; break;
	case SUB:	    str = "SUB"; break;
	case MUL:	    str = "MUL"; break;
	case DIV:	    str = "DIV"; break;
	case MOD:	    str = "MOD"; break;
	case FUNC_CALL:	    str = "FUNC_CALL"; break;
	case LT:	    str = "LT"; break;
	case GT:	    str = "GT"; break;
	case LTE:	    str = "LTE"; break;
	case GTE:	    str = "GTE"; break;
	case EQ:	    str = "EQ"; break;
	case NEQ:	    str = "NEQ"; break;
	case AND:	    str = "AND"; break;
	case OR:	    str = "OR"; break;
	/* "Terminal"/keyword tokens */
	case 0:		    str = "NULL(0)"; break;
	case ASSIGNOP:	    str = "ASSIGNOP"; break;
	case ADDOP:	    str = "ADDOP"; break;
	case MULOP:	    str = "MULOP"; break;
	case INCOP:	    str = "INCOP"; break;
	case INT_LITERAL:   str = "INT_LITERAL"; break;
	case FLT_LITERAL:   str = "FLT_LITERAL"; break;
	case STR_LITERAL:   str = "STR_LITERAL"; break;
	case STREAMIN:	    str = "STREAMIN"; break;
	case STREAMOUT:	    str = "STREAMOUT"; break;
	case ID:	    str = "ID"; break;
	case CIN:	    str = "CIN"; break;
	case COUT:	    str = "COUT"; break;
	case ELSE:	    str = "ELSE"; break;
	case ENDL:	    str = "ENDL"; break;
	case FLOAT:	    str = "FLOAT"; break;
	case IF:	    str = "IF"; break;
	case INT:	    str = "INT"; break;
	case RETURN:	    str = "RETURN"; break;
	case WHILE:	    str = "WHILE"; break;
	case VAR_DEC:	    str = "VAR_DEC"; break;
	case ARR_DEC:	    str = "ARR_DEC"; break;
	case VAR_ACCESS:    str = "VAR_ACCESS"; break;
	case ARR_ACCESS:    str = "ARR_ACCESS"; break;
	case ADDR:	    str = "ADDR"; break;
	default:	    str = "!!UNKNOWN!!";
    }
    
    if(strcmp(str, "!!UNKNOWN!!") == 0)
	printf("(%d)", nodetype);

    return str;
}

void dumpTempVarStack(char op)
{
    struct VMQ_func_data* VMQ = &CURRENT_FUNC->VMQ_data;
    struct VMQ_temp_node* stack_ptr = VMQ->tempvar_stack_head;

    unsigned int arr_size = 64;
    char separator[arr_size];
    for(unsigned int i = 0; i < arr_size - 1; i++)
	separator[i] = '=';

    separator[arr_size - 1] = '\0';

    printf("\nDumping Stack - ");
    if(op == 'n')
	printf("Post-New\n");
    else
	printf("Post-Free\n");

    printf("%s\n", separator);
    while(stack_ptr)
    {
	printf("/-%d(%s) -> ", stack_ptr->VMQ_loc, nodeTypeToString(stack_ptr->type));
	stack_ptr = stack_ptr->next;
    }
    printf("NULL\n");

    printf("\tcur_addr == %d || cur_size == %d\n", VMQ->tempvar_cur_addr, VMQ->tempvar_cur_size);
    
    printf("%s\n\n", separator);

    fflush(stdout);
}
