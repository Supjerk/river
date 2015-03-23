#include <intrin.h>

#include "common.h"
#include "callgates.h"
#include "cb.h"
#include "mm.h"
#include "BaseTsd.h"

void DbgPrint(const char *fmt, ...);
int Translate(struct _exec_env *pEnv, struct _cb_info *pCB, DWORD dwTranslationFlags);

void Stopper(struct _exec_env *pEnv, BYTE *s) {
	RiverBasicBlock *pStop;

	DbgPrint("STOPPER: %p.\n", s);
	pStop = pEnv->blockCache.NewBlock((UINT_PTR)s);
	pEnv->codeGen.Translate(pStop, &pEnv->runtimeContext, 0x80000000); // this will fix pStop->CRC and pStop->Size
}


DWORD __declspec(noinline) call_cdecl_0(struct _exec_env *env, _fn_cdecl_0 f) {
	RiverBasicBlock *pBlock;
	DWORD ret;

	Stopper (env, (BYTE *)_ReturnAddress());

	pBlock = env->blockCache.NewBlock((UINT_PTR)f);
	//pBlock->address = (DWORD) f;
	env->codeGen.Translate(pBlock, &env->runtimeContext, 0);
	env->bForward = 1;
	pBlock->MarkForward();
	//AddBlock(env, pBlock);
	ret = ((_fn_cdecl_0)(pBlock->pFwCode))(); //JUMP in TVM

	return ret;
}

DWORD __declspec(noinline) call_cdecl_1(struct _exec_env *env, _fn_cdecl_1 f, void *p1) {
	DWORD ret;
	RiverBasicBlock *pBlock;

	Stopper (env, (BYTE *)_ReturnAddress());

	pBlock = env->blockCache.NewBlock((UINT_PTR)f);
	pBlock->address = (DWORD) f;
	env->codeGen.Translate(pBlock, &env->runtimeContext, 0);
	env->bForward = 1;
	pBlock->MarkForward();
	//AddBlock(env, pBlock);

	ret = ((_fn_cdecl_1)(pBlock->pFwCode))(p1);

	return ret;
}

DWORD __declspec(noinline) call_cdecl_2(struct _exec_env *env, _fn_cdecl_2 f, void *p1, void *p2) {
	DWORD ret;
	RiverBasicBlock *pBlock;

	Stopper(env, (BYTE *)_ReturnAddress());

	pBlock = env->blockCache.NewBlock((UINT_PTR)f);
	pBlock->address = (DWORD) f;
	env->codeGen.Translate(pBlock, &env->runtimeContext, 0);
	env->bForward = 1;
	pBlock->MarkForward();
	//AddBlock(env, pBlock);

	ret = ((_fn_cdecl_2)(pBlock->pFwCode))(p1, p2);

	return ret;
}

DWORD __declspec(noinline) call_cdecl_3(struct _exec_env *env, _fn_cdecl_3 f, void *p1, void *p2, void *p3) {
	DWORD ret;
	RiverBasicBlock *pBlock;

	Stopper(env, (BYTE *)_ReturnAddress());

	pBlock = env->blockCache.NewBlock((UINT_PTR)f);
	pBlock->address = (DWORD) f;
	env->codeGen.Translate(pBlock, &env->runtimeContext, 0);
	env->bForward = 1;
	pBlock->MarkForward();
	//AddBlock(env, pBlock);

	ret = ((_fn_cdecl_3)(pBlock->pFwCode))(p1, p2, p3);

	return ret;
}

DWORD __declspec(noinline) call_cdecl_4(struct _exec_env *env, _fn_cdecl_4 f, void *p1, void *p2, void *p3, void *p4) {
	DWORD ret;
	RiverBasicBlock *pBlock;

	Stopper(env, (BYTE *)_ReturnAddress());

	pBlock = env->blockCache.NewBlock((UINT_PTR)f);
	pBlock->address = (DWORD) f;
	env->codeGen.Translate(pBlock, &env->runtimeContext, 0);
	env->bForward = 1;
	pBlock->MarkForward();
	//AddBlock(env, pBlock);

	ret = ((_fn_cdecl_4)(pBlock->pFwCode))(p1, p2, p3, p4);
	return ret;
}

DWORD __declspec(noinline) call_stdcall_0(struct _exec_env *env, _fn_stdcall_0 f) {
	RiverBasicBlock *pBlock;
	DWORD ret;

	Stopper(env, (BYTE *)_ReturnAddress());

	pBlock = env->blockCache.NewBlock((UINT_PTR)f);
	pBlock->address = (DWORD) f;
	env->codeGen.Translate(pBlock, &env->runtimeContext, 0);
	env->bForward = 1;
	pBlock->MarkForward();
	//AddBlock(env, pBlock);

	ret = ((_fn_stdcall_0)(pBlock->pFwCode))(); //JUMP in TVM
	return ret;
}

DWORD __declspec(noinline) call_stdcall_1(struct _exec_env *env, _fn_stdcall_1 f, void *p1) {
	DWORD ret;
	RiverBasicBlock *pBlock;

	Stopper(env, (BYTE *)_ReturnAddress());

	pBlock = env->blockCache.NewBlock((UINT_PTR)f);
	pBlock->address = (DWORD) f;
	env->codeGen.Translate(pBlock, &env->runtimeContext, 0);
	env->bForward = 1;
	pBlock->MarkForward();
	//AddBlock(env, pBlock);

	ret = ((_fn_stdcall_1)(pBlock->pFwCode))(p1);
	return ret;
}

DWORD __declspec(noinline) call_stdcall_2(struct _exec_env *env, _fn_stdcall_2 f, void *p1, void *p2) {
	DWORD ret;
	RiverBasicBlock *pBlock;

	Stopper(env, (BYTE *)_ReturnAddress());

	pBlock = env->blockCache.NewBlock((UINT_PTR)f);
	pBlock->address = (DWORD) f;
	env->codeGen.Translate(pBlock, &env->runtimeContext, 0);
	env->bForward = 1;
	pBlock->MarkForward();
	//AddBlock(env, pBlock);

	ret = ((_fn_stdcall_2)(pBlock->pFwCode))(p1, p2);
	return ret;
}

DWORD __declspec(noinline) call_stdcall_3(struct _exec_env *env, _fn_stdcall_3 f, void *p1, void *p2, void *p3) {
	DWORD ret;
	RiverBasicBlock *pBlock;

	Stopper(env, (BYTE *)_ReturnAddress());

	pBlock = env->blockCache.NewBlock((UINT_PTR)f);
	pBlock->address = (DWORD) f;
	env->codeGen.Translate(pBlock, &env->runtimeContext, 0);
	env->bForward = 1;
	pBlock->MarkForward();
	//AddBlock(env, pBlock);

	ret = ((_fn_stdcall_3)(pBlock->pFwCode))(p1, p2, p3);
	return ret;
}

DWORD __declspec(noinline) call_stdcall_4(struct _exec_env *env, _fn_stdcall_4 f, void *p1, void *p2, void *p3, void *p4) {
	DWORD ret;
	RiverBasicBlock *pBlock;

	Stopper(env, (BYTE *)_ReturnAddress());

	pBlock = env->blockCache.NewBlock((UINT_PTR)f);
	pBlock->address = (DWORD) f;
	env->codeGen.Translate(pBlock, &env->runtimeContext, 0);
	env->bForward = 1;
	pBlock->MarkForward();
	//AddBlock(env, pBlock);

	ret = ((_fn_stdcall_4)(pBlock->pFwCode))(p1, p2, p3, p4);
	return ret;
}