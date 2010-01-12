/**
 * $Id$
 * Copyright (C) 2008 - 2009 Nils Asmussen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef CMPEXPR_H_
#define CMPEXPR_H_

#include <esc/common.h>
#include "node.h"

#define CMP_OP_EQ	0
#define CMP_OP_NEQ	1
#define CMP_OP_LT	2
#define CMP_OP_GT	3
#define CMP_OP_LEQ	4
#define CMP_OP_GEQ	5

typedef struct {
	u8 operation;
	sASTNode *operand1;
	sASTNode *operand2;
} sCmpExpr;

/**
 * Creates an compare-node with: <operand1> <operation> <operand2>
 *
 * @param operand1 the first operand
 * @param operation the operation
 * @param operand2 the second operand
 * @return the created node
 */
sASTNode *ast_createCmpExpr(sASTNode *operand1,u8 operation,sASTNode *operand2);

/**
 * Prints this expression
 *
 * @param s the expression
 * @param layer the layer
 */
void ast_printCmpExpr(sCmpExpr *s,u32 layer);

/**
 * Destroys the given compare-expression (should be called from ast_destroy() only!)
 *
 * @param n the expression
 */
void ast_destroyCmpExpr(sCmpExpr *n);

#endif /* CMPEXPR_H_ */
