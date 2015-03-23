#include "river.h"
#include "riverinternl.h"

#include "mm.h"

extern DWORD dwSysHandler; // = 0; // &SysHandler
extern DWORD dwSysEndHandler; // = 0; // &SysEndHandler
extern DWORD dwBranchHandler; // = 0; // &BranchHandler

#define X86_LOCK_PREFIX				0xF0
#define X86_REPNZ_PREFIX			0xF2
#define X86_REPZ_PREFIX				0xF3
#define X86_REP_PREFIX				0xF3

extern const AssemblingOpcodeFunc RiverOpcodeTable00[];
extern const AssemblingOpcodeFunc RiverOpcodeTable0F[];
extern const AssemblingOperandsFunc RiverOperandsTable00[];
extern const AssemblingOperandsFunc RiverOperandsTable0F[];

#define FLAG_SKIP_METAOP			0x01
#define FLAG_GENERATE_RIVER			0x02
#define FLAG_GENERATE_RIVER_xSP		0x04

void SwitchToRiver(RiverRuntime *rt, BYTE **px86) {
	static const unsigned char code[] = { 0x87, 0x25, 0x00, 0x00, 0x00, 0x00 };			// 0x00 - xchg esp, large ds:<dwVirtualStack>}

	memcpy(*px86, code, sizeof(code));
	*(unsigned int *)(&((*px86)[0x02])) = (unsigned int)&rt->execBuff;

	(*px86) += sizeof(code);
}

void SwitchToRiverEsp(RiverRuntime *rt, BYTE **px86) {
	static const unsigned char code[] = { 0x87, 0x05, 0x00, 0x00, 0x00, 0x00 };			// 0x00 - xchg eax, large ds:<dwVirtualStack>}

	memcpy(*px86, code, sizeof(code));
	*(unsigned int *)(&((*px86)[0x02])) = (unsigned int)&rt->execBuff;

	(*px86) += sizeof(code);
}

void EndRiverConversion(RiverRuntime *rt, BYTE **px86, DWORD *pFlags) {
	if (*pFlags & FLAG_GENERATE_RIVER) {
		if (*pFlags & FLAG_GENERATE_RIVER_xSP) {
			SwitchToRiverEsp(rt, px86);
			*pFlags &= ~FLAG_GENERATE_RIVER_xSP;
		}

		SwitchToRiver(rt, px86);
		*pFlags &= ~FLAG_GENERATE_RIVER;
	}
}

void FixRiverEspOp(BYTE opType, RiverOperand *op) {
	switch (opType & 0xFC) {
	case RIVER_OPTYPE_IMM :
	case RIVER_OPTYPE_NONE:
		break;
	case RIVER_OPTYPE_REG :
		if (op->asRegister.name == RIVER_REG_xSP) {
			op->asRegister.versioned = RIVER_REG_xAX;
		}
		break;
	case RIVER_OPTYPE_MEM :
		break;
	default :
		__asm int 3;
	}
}

RiverInstruction *FixRiverEspInstruction(RiverInstruction *rIn, RiverInstruction *rTmp, RiverAddress *aTmp) {
	if (rIn->modifiers & RIVER_MODIFIER_ORIG_xSP) {
		memcpy(rTmp, rIn, sizeof(*rTmp));
		if (rIn->opTypes[0] != RIVER_OPTYPE_NONE) {
			if (rIn->opTypes[0] & RIVER_OPTYPE_MEM) {
				rTmp->operands[0].asAddress = aTmp;
				memcpy(aTmp, rIn->operands[0].asAddress, sizeof(*aTmp));
			}
			FixRiverEspOp(rTmp->opTypes[0], &rTmp->operands[0]);
		}

		if (rIn->opTypes[1] != RIVER_OPTYPE_NONE) {
			if (rIn->opTypes[1] & RIVER_OPTYPE_MEM) {
				rTmp->operands[1].asAddress = aTmp;
				memcpy(aTmp, rIn->operands[1].asAddress, sizeof(*aTmp));
			}
			FixRiverEspOp(rTmp->opTypes[1], &rTmp->operands[1]);
		}
		return rTmp;
	} else {
		return rIn;
	}
}

void ConvertRiverInstruction(RiverCodeGen *cg, RiverRuntime *rt, struct RiverInstruction *ri, BYTE **px86, DWORD *pFlags) {
	// skip ignored instructions
	if (ri->modifiers & RIVER_MODIFIER_IGNORE) {
		return;
	}

	// when generating fwcode skip meta instructions
	if (ri->modifiers & RIVER_MODIFIER_METAOP) {
		if (*pFlags & FLAG_SKIP_METAOP) {
			return;
		}
	}

	// ensure state transitions between river and x86
	if (ri->modifiers & RIVER_MODIFIER_RIVEROP) {
		if (0 == (*pFlags & FLAG_GENERATE_RIVER)) {
			SwitchToRiver(rt, px86);
			*pFlags |= FLAG_GENERATE_RIVER;
		}

		// ensure state transitions between riveresp and river
		if (ri->modifiers & RIVER_MODIFIER_ORIG_xSP) {
			if (0 == (*pFlags & FLAG_GENERATE_RIVER_xSP)) {
				SwitchToRiverEsp(rt, px86);
				*pFlags |= FLAG_GENERATE_RIVER_xSP;
			}
		}
		else {
			if (*pFlags & FLAG_GENERATE_RIVER_xSP) {
				SwitchToRiverEsp(rt, px86);
				*pFlags &= ~FLAG_GENERATE_RIVER_xSP;
			}
		}
	} else {
		if (*pFlags & FLAG_GENERATE_RIVER) {
			if (*pFlags & FLAG_GENERATE_RIVER_xSP) {
				SwitchToRiverEsp(rt, px86);
				*pFlags &= ~FLAG_GENERATE_RIVER_xSP;
			}

			SwitchToRiver(rt, px86);
			*pFlags &= ~FLAG_GENERATE_RIVER;
		}
	}

	if (ri->modifiers & RIVER_MODIFIER_LOCK) {
		**px86 = X86_LOCK_PREFIX;
		(*px86)++;
	}

	if (ri->modifiers & RIVER_MODIFIER_REP) {
		**px86 = X86_REP_PREFIX;
		(*px86)++;
	}

	if (ri->modifiers & RIVER_MODIFIER_REPZ) {
		**px86 = X86_REPZ_PREFIX;
		(*px86)++;
	}

	if (ri->modifiers & RIVER_MODIFIER_REPNZ) {
		**px86 = X86_REPNZ_PREFIX;
		(*px86)++;
	}

	RiverInstruction rInstr;
	RiverAddress32 rAddr;

	RiverInstruction *rOut = FixRiverEspInstruction(ri, &rInstr, &rAddr);

	const AssemblingOpcodeFunc *translateRiverTable = RiverOpcodeTable00;
	const AssemblingOperandsFunc *translateRiverOperands = RiverOperandsTable00;

	if (rOut->modifiers & RIVER_MODIFIER_EXT) {
		**px86 = 0x0F;
		(*px86)++;

		translateRiverTable = RiverOpcodeTable0F;
		translateRiverOperands = RiverOperandsTable0F;
	}

	translateRiverTable[rOut->opCode](cg, rt, rOut, px86, pFlags);
	translateRiverOperands[rOut->opCode](cg, rOut, px86);

}

/* classic opcode encoders */

static void TranslateUnknownInstr(RiverCodeGen *cg, RiverRuntime *rt, RiverInstruction *ri, BYTE **px86, DWORD *pFlags) {
	__asm int 3;
	(*px86)++;
}

// handles +r opcodes such as 0x50+r
// base template argument corresponds to op eax
static void TranslatePlusRegInstr(RiverCodeGen *cg, RiverRuntime *rt, RiverInstruction *ri, BYTE **px86, DWORD *pFlags) {
	unsigned char regName = ri->operands[0].asRegister.name;

	if ((ri->modifiers & RIVER_MODIFIER_ORIG_xSP) && (regName == RIVER_REG_xSP)) {
		regName = RIVER_REG_xAX;
	}

	**px86 = ri->opCode + (regName & 0x07);
	(*px86)++;
}

// translate single opcode instruction
static void TranslateDefaultInstr(RiverCodeGen *cg, RiverRuntime *rt, RiverInstruction *ri, BYTE **px86, DWORD *pFlags) {
	**px86 = ri->opCode;
	(*px86)++;
}

static const BYTE pBranchJMP[] = {
	0x87, 0x25, 0x00, 0x00, 0x00, 0x00,			// 0x00 - xchg esp, large ds:<dwVirtualStack>
	0x9C, 										// 0x06 - pushf
	0x60,										// 0x07 - pusha
	0x68, 0x46, 0x02, 0x00, 0x00,				// 0x08 - push 0x00000246 - NEW FLAGS
	0x9D,										// 0x0D - popf
	0x68, 0x00, 0x00, 0x00, 0x00,				// 0x0E - push <jump_addr>
	0x68, 0x00, 0x00, 0x00, 0x00,				// 0x13 - push <execution_environment>
	0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,			// 0x18 - call <dwBranchHandler>
	0x61,										// 0x1E - popa
	0x9D,										// 0x1F - popf
	0x87, 0x25, 0x00, 0x00, 0x00, 0x00,			// 0x20 - xchg esp, large ds:<dwVirtualStack>
	0xFF, 0x25, 0x00, 0x00, 0x00, 0x00			// 0x26 - jmp large dword ptr ds:<jumpbuff>	
};

static void TranslateRelJmpInstr(RiverCodeGen *cg, RiverRuntime *rt, RiverInstruction *ri, BYTE **px86, DWORD *pFlags) {
	int addrJump = (int)(ri->operands[1].asImm32);

	switch (ri->opTypes[0] & 0x03) {
		case RIVER_OPSIZE_8 :
			addrJump += (char)ri->operands[0].asImm8;
			break;
		case RIVER_OPSIZE_16:
			addrJump += (short)ri->operands[0].asImm16;
			break;
		case RIVER_OPSIZE_32:
			addrJump += (int)ri->operands[0].asImm32;
			break;
	}

	memcpy((*px86), pBranchJMP, sizeof(pBranchJMP));
	*(unsigned int *)(&((*px86)[0x02])) = (unsigned int)&rt->virtualStack;
	*(unsigned int *)(&((*px86)[0x0F])) = addrJump;
	*(unsigned int *)(&((*px86)[0x14])) = (unsigned int)rt;
	*(unsigned int *)(&((*px86)[0x1A])) = (unsigned int)&dwBranchHandler;
	*(unsigned int *)(&((*px86)[0x22])) = (unsigned int)&rt->virtualStack;
	*(unsigned int *)(&((*px86)[0x28])) = (unsigned int)&rt->jumpBuff;

	(*px86) += sizeof(pBranchJMP);
	*pFlags |= RIVER_FLAG_BRANCH;
}

const BYTE pBranchJCC[] = {
	0x87, 0x25, 0x00, 0x00, 0x00, 0x00,			// 0x00 - xchg esp, large ds:<dwVirtualStack>
	0x9C, 										// 0x06 - pushf
	0x60,										// 0x07 - pusha
	0x68, 0x46, 0x02, 0x00, 0x00,				// 0x08 - push 0x00000246 - NEW FLAGS
	0x9D,										// 0x0D - popf
	0x68, 0x00, 0x00, 0x00, 0x00,				// 0x0E - push <original_address>
	0x68, 0x00, 0x00, 0x00, 0x00,				// 0x13 - push <execution_environment>
	0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,			// 0x18 - call <branch_handler>
	0x61,										// 0x1E - popa
	0x9D,										// 0x1F - popf
	0x87, 0x25, 0x00, 0x00, 0x00, 0x00,			// 0x20 - xchg esp, large ds:<dwVirtualStack>
	0xFF, 0x25, 0x00, 0x00, 0x00, 0x00			// 0x26 - jmp large dword ptr ds:<jumpbuff>
};

static void TranslateRelJccInstr(RiverCodeGen *cg, RiverRuntime *rt, RiverInstruction *ri, BYTE **px86, DWORD *pFlags) {
	int addrFallThrough = (int)(ri->operands[1].asImm32);
	int addrJump = addrFallThrough;

	switch (ri->opTypes[0] & 0x03) {
		case RIVER_OPSIZE_8:
			addrJump += (char)ri->operands[0].asImm8;
			break;
		case RIVER_OPSIZE_16:
			addrJump += (short)ri->operands[0].asImm16;
			break;
		case RIVER_OPSIZE_32:
			addrJump += (int)ri->operands[0].asImm32;
			break;
	}

	**px86 = ri->opCode;
	(*px86)++; 
	**px86 = sizeof(pBranchJCC);
	(*px86)++;

	memcpy((*px86), pBranchJMP, sizeof(pBranchJCC));
	
	*(unsigned int *)(&((*px86)[0x02])) = (unsigned int)&rt->virtualStack;
	*(unsigned int *)(&((*px86)[0x0F])) = addrFallThrough;
	*(unsigned int *)(&((*px86)[0x14])) = (unsigned int)rt;
	*(unsigned int *)(&((*px86)[0x1A])) = (unsigned int)&dwBranchHandler;
	*(unsigned int *)(&((*px86)[0x22])) = (unsigned int)&rt->virtualStack;
	*(unsigned int *)(&((*px86)[0x28])) = (unsigned int)&rt->jumpBuff;
	(*px86) += sizeof(pBranchJMP);


	memcpy((*px86), pBranchJMP, sizeof(pBranchJCC));

	*(unsigned int *)(&((*px86)[0x02])) = (unsigned int)&rt->virtualStack;
	*(unsigned int *)(&((*px86)[0x0F])) = addrJump;
	*(unsigned int *)(&((*px86)[0x14])) = (unsigned int)rt;
	*(unsigned int *)(&((*px86)[0x1A])) = (unsigned int)&dwBranchHandler;
	*(unsigned int *)(&((*px86)[0x22])) = (unsigned int)&rt->virtualStack;
	*(unsigned int *)(&((*px86)[0x28])) = (unsigned int)&rt->jumpBuff;
	(*px86) += sizeof(pBranchJMP);

	*pFlags |= RIVER_FLAG_BRANCH;
}


//RetImm - copy the value
//Retn - 0
//RetFar - 4
const BYTE pBranchRet[] = {
	0xA3, 0x00, 0x00, 0x00, 0x00,				// 0x00 - mov [<dwEaxSave>], eax
	0x58,										// 0x05 - pop eax
	0x87, 0x25, 0x00, 0x00, 0x00, 0x00,			// 0x06 - xchg esp, large ds:<dwVirtualStack>
	0x9C,			 							// 0x0C - pushf
	0x60,										// 0x0D - pusha
	0x68, 0x46, 0x02, 0x00, 0x00,				// 0x0E - push 0x00000246 - NEW FLAGS
	0x9D,										// 0x13 - popf
	0x50,										// 0x14 - push eax
	0x68, 0x00, 0x00, 0x00, 0x00,				// 0x15 - push <execution_environment>
	0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,			// 0x1A - call <branch_handler>

	0x61,										// 0x20 - popa
	0x9D,										// 0x21 - popf
	0x87, 0x25, 0x00, 0x00, 0x00, 0x00,			// 0x22 - xchg esp, large ds:<dwVirtualStack>
	0xA1, 0x00, 0x00, 0x00, 0x00,				// 0x28 - mov eax, large ds:<dwEaxSave>
	0x8D, 0xA4, 0x24, 0x00, 0x00, 0x00, 0x00,   // 0x2D - lea esp, [esp + <pI>] // probably sub esp, xxx
	0xFF, 0x25, 0x00, 0x00, 0x00, 0x00			// 0x34 - jmp large dword ptr ds:<jumpbuff>
};

static void TranslateRetnInstr(RiverCodeGen *cg, RiverRuntime *rt, RiverInstruction *ri, BYTE **px86, DWORD *pFlags) {
//unsigned int __stdcall FuncCRetImm(struct _exec_env *pEnv, struct _cb_info *pCB, unsigned int *dwFlags, char *pI, char *pD, unsigned int *dwWritten) {
	unsigned short stackSpace = 0;
	memcpy((*px86), pBranchRet, sizeof(pBranchRet));

	*(unsigned int *)(&((*px86)[0x01])) = (unsigned int)&rt->returnRegister;
	*(unsigned int *)(&((*px86)[0x08])) = (unsigned int)&rt->virtualStack;
	*(unsigned int *)(&((*px86)[0x16])) = (unsigned int)rt;
	*(unsigned int *)(&((*px86)[0x1C])) = (unsigned int)&dwBranchHandler;
	*(unsigned int *)(&((*px86)[0x24])) = (unsigned int)&rt->virtualStack;
	*(unsigned int *)(&((*px86)[0x29])) = (unsigned int)&rt->returnRegister;
	*(unsigned int *)(&((*px86)[0x30])) = stackSpace;
	*(unsigned int *)(&((*px86)[0x36])) = (unsigned int)&rt->jumpBuff;

	(*px86) += sizeof(pBranchRet);
	*pFlags |= RIVER_FLAG_BRANCH;
}

static void Translate0xFF(RiverCodeGen *cg, RiverRuntime *rt, RiverInstruction *ri, BYTE **px86, DWORD *pFlags) {
	switch (ri->subOpCode) {
	case 6:
		TranslateDefaultInstr(cg, rt, ri, px86, pFlags);
		break;
	default:
		__asm int 3;
	}
}

static void TranslateRiverAddSubInstr(RiverCodeGen *cg, RiverRuntime *rt, RiverInstruction *ri, BYTE **px86, DWORD *pFlags) {
	**px86 = 0x8d; // add and sub are converted to lea
	(*px86)++;
}


template <AssemblingOpcodeFunc fRiver, AssemblingOpcodeFunc fNormal> static void TranslateMultiplexedInstr(RiverCodeGen *cg, RiverRuntime *rt, RiverInstruction *ri, BYTE **px86, DWORD *pFlags) {
	if (ri->modifiers & RIVER_MODIFIER_RIVEROP) {
		fRiver(cg, rt, ri, px86, pFlags);
	}
	else {
		fNormal(cg, rt, ri, px86, pFlags);
	}
}

/* operands encoders */

static void TranslateImmOp(RiverCodeGen *cg, unsigned int opIdx, RiverInstruction *ri, BYTE **px86, BYTE immSize) {
	switch (immSize) {
		case RIVER_OPSIZE_8:
			*((BYTE *)(*px86)) = ri->operands[opIdx].asImm8;
			(*px86)++;
			break;
		case RIVER_OPSIZE_16:
			*((WORD *)(*px86)) = ri->operands[opIdx].asImm16;
			(*px86) += 2;
			break;
		case RIVER_OPSIZE_32:
			*((DWORD *)(*px86)) = ri->operands[opIdx].asImm32;
			(*px86) += 4;
			break;
	}
}

static void TranslateModRMOp(RiverCodeGen *cg, unsigned int opIdx, RiverInstruction *ri, BYTE **px86, BYTE extra) {
	ri->operands[opIdx].asAddress->EncodeTox86(*px86, extra, ri->modifiers);
}

static void TranslateUnknownOp(RiverCodeGen *cg, RiverInstruction *ri, BYTE **px86) {
	__asm int 3;
}

static void TranslateNoOp(RiverCodeGen *cg, RiverInstruction *ri, BYTE **px86) {
}

template <int extra> static void TranslateModRMOp(RiverCodeGen *cg, RiverInstruction *ri, BYTE **px86) {
	TranslateModRMOp(cg, 0, ri, px86, extra);
}

static void TranslateRegModRMOp(RiverCodeGen *cg, RiverInstruction *ri, BYTE **px86) {
	TranslateModRMOp(cg, 1, ri, px86, ri->operands[0].asRegister.name);
}

static void TranslateModRMRegOp(RiverCodeGen *cg, RiverInstruction *ri, BYTE **px86) {
	TranslateModRMOp(cg, 0, ri, px86, ri->operands[1].asRegister.name);
}

static void TranslateSubOpModRMImm8Op(RiverCodeGen *cg, RiverInstruction *ri, BYTE **px86) {
	TranslateModRMOp(cg, 0, ri, px86, ri->subOpCode);
	TranslateImmOp(cg, 1, ri, px86, RIVER_OPSIZE_8);
}

static void Translate0xFFOp(RiverCodeGen *cg, RiverInstruction *ri, BYTE **px86) {
	switch (ri->subOpCode) {
	case 6:
		TranslateModRMOp(cg, 0, ri, px86, ri->subOpCode);
		break;
	default:
		__asm int 3;
	}
}

static void TranslateRiverAddSubOp(RiverCodeGen *cg, RiverInstruction *ri, BYTE **px86) {
	RiverInstruction rTmp;
	RiverAddress32 addr;

	addr.type = RIVER_ADDR_DIRTY | RIVER_ADDR_BASE | RIVER_ADDR_DISP8;
	addr.base.versioned = ri->operands[0].asRegister.versioned;
	
	addr.disp.d8 = ri->operands[1].asImm8;

	if (ri->subOpCode == 5) {
		addr.disp.d8 = ~addr.disp.d8 + 1;
	}

	addr.modRM |= ri->operands[0].asRegister.name << 3;

	rTmp.opTypes[0] = ri->opTypes[0];
	rTmp.operands[0].asRegister.versioned = ri->operands[0].asRegister.versioned;

	rTmp.opTypes[1] = RIVER_OPTYPE_MEM; // no size specified;
	rTmp.operands[1].asAddress = &addr;
	

	TranslateRegModRMOp(cg, &rTmp, px86);
}

template <TranslateOperandsFunc fRiver, TranslateOperandsFunc fNormal> static void TranslateMultiplexedOp(RiverCodeGen *cg, RiverInstruction *ri, BYTE **px86) {
	if (ri->modifiers & RIVER_MODIFIER_RIVEROP) {
		fRiver(cg, ri, px86);
	} else {
		fNormal(cg, ri, px86);
	}
}

const AssemblingOpcodeFunc RiverOpcodeTable00[] = {
	/*0x00*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x04*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x08*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x0C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0x10*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x14*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x18*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x1C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0x20*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x24*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x28*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x2C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0x30*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateDefaultInstr,
	/*0x34*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x38*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateDefaultInstr,
	/*0x3C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0x40*/ TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr,
	/*0x44*/ TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr,
	/*0x48*/ TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr,
	/*0x4C*/ TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr,

	/*0x50*/ TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr,
	/*0x54*/ TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr,
	/*0x58*/ TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr,
	/*0x5C*/ TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr,

	/*0x60*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x64*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x68*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x6C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0x70*/ TranslateRelJccInstr, TranslateRelJccInstr, TranslateRelJccInstr, TranslateRelJccInstr,
	/*0x74*/ TranslateRelJccInstr, TranslateRelJccInstr, TranslateRelJccInstr, TranslateRelJccInstr,
	/*0x78*/ TranslateRelJccInstr, TranslateRelJccInstr, TranslateRelJccInstr, TranslateRelJccInstr,
	/*0x7C*/ TranslateRelJccInstr, TranslateRelJccInstr, TranslateRelJccInstr, TranslateRelJccInstr,

	/*0x80*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateMultiplexedInstr<TranslateRiverAddSubInstr, TranslateDefaultInstr>,
	/*0x84*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x88*/ TranslateUnknownInstr, TranslateDefaultInstr, TranslateUnknownInstr, TranslateDefaultInstr,
	/*0x8C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateDefaultInstr,

	/*0x90*/ TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr,
	/*0x94*/ TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr,
	/*0x98*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x9C*/ TranslateDefaultInstr, TranslateDefaultInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0xA0*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xA4*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xA8*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xAC*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0xB0*/ TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr,
	/*0xB4*/ TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr,
	/*0xB8*/ TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr,
	/*0xBC*/ TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr, TranslatePlusRegInstr,

	/*0xC0*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateRetnInstr,
	/*0xC4*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xC8*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xCC*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0xD0*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xD4*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xD8*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xDC*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0xE0*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xE4*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xE8*/ TranslateUnknownInstr, TranslateRelJmpInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xEC*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0xF0*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xF4*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xF8*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xFC*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, Translate0xFF
};


const AssemblingOpcodeFunc RiverOpcodeTable0F[] = {
	/*0x00*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x04*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x08*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x0C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0x10*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x14*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x18*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x1C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0x20*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x24*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x28*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x2C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0x30*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x34*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x38*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x3C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0x40*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x44*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x48*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x4C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0x50*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x54*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x58*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x5C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0x60*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x64*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x68*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x6C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0x70*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x74*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x78*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x7C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0x80*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x84*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x88*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x8C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0x90*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x94*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x98*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0x9C*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0xA0*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xA4*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xA8*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xAC*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0xB0*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xB4*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xB8*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xBC*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0xC0*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xC4*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xC8*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xCC*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0xD0*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xD4*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xD8*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xDC*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0xE0*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xE4*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xE8*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xEC*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,

	/*0xF0*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xF4*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xF8*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr,
	/*0xFC*/ TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr, TranslateUnknownInstr
};

const AssemblingOperandsFunc RiverOperandsTable00[] = {
	/*0x00*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x04*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x08*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x0C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0x10*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x14*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x18*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x1C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0x20*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x24*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x28*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x2C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0x30*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateRegModRMOp,
	/*0x34*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x38*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateRegModRMOp,
	/*0x3C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0x40*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,
	/*0x44*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,
	/*0x48*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,
	/*0x4C*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,

	/*0x50*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,
	/*0x54*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,
	/*0x58*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,
	/*0x5C*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,

	/*0x60*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x64*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x68*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x6C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0x70*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,
	/*0x74*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,
	/*0x78*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,
	/*0x7C*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,

	/*0x80*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateMultiplexedOp<TranslateRiverAddSubOp, TranslateSubOpModRMImm8Op>,
	/*0x84*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x88*/ TranslateUnknownOp, TranslateModRMRegOp, TranslateUnknownOp, TranslateRegModRMOp,
	/*0x8C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateModRMOp<0>,

	/*0x90*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,
	/*0x94*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,
	/*0x98*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x9C*/ TranslateNoOp, TranslateNoOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0xA0*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xA4*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xA8*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xAC*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0xB0*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,
	/*0xB4*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,
	/*0xB8*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,
	/*0xBC*/ TranslateNoOp, TranslateNoOp, TranslateNoOp, TranslateNoOp,

	/*0xC0*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateNoOp,
	/*0xC4*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xC8*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xCC*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0xD0*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xD4*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xD8*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xDC*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0xE0*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xE4*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xE8*/ TranslateUnknownOp, TranslateNoOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xEC*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0xF0*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xF4*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xF8*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xFC*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, Translate0xFFOp
};

const AssemblingOperandsFunc RiverOperandsTable0F[] = {
	/*0x00*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x04*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x08*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x0C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0x10*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x14*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x18*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x1C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0x20*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x24*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x28*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x2C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0x30*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x34*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x38*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x3C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0x40*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x44*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x48*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x4C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0x50*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x54*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x58*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x5C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0x60*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x64*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x68*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x6C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0x70*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x74*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x78*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x7C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0x80*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x84*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x88*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x8C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0x90*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x94*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x98*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0x9C*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0xA0*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xA4*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xA8*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xAC*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0xB0*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xB4*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xB8*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xBC*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0xC0*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xC4*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xC8*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xCC*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0xD0*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xD4*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xD8*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xDC*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0xE0*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xE4*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xE8*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xEC*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,

	/*0xF0*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xF4*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xF8*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp,
	/*0xFC*/ TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp, TranslateUnknownOp
};