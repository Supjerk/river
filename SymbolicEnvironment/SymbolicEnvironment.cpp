#include "SymbolicEnvironment.h"

#include "LargeStack.h"

namespace sym {
	inline bool SymbolicEnvironment::SetExecutor(SymbolicExecutor *e) {
		exec = e;
		return true;
	}

	void sym::ScopedSymbolicEnvironment::_SetReferenceCounting(AddRefFunc addRef, DecRefFunc decRef) {
	}

	bool ScopedSymbolicEnvironment::_SetCurrentInstruction(RiverInstruction *instruction, void *opBuffer) {
		return true;
	}

	ScopedSymbolicEnvironment::ScopedSymbolicEnvironment() {
		subEnv = nullptr;
	}

	inline bool ScopedSymbolicEnvironment::SetExecutor(SymbolicExecutor *e) {
		exec = e;
		return subEnv->SetExecutor(e);
	}

	void ScopedSymbolicEnvironment::PushState(stk::LargeStack &stack) {
		subEnv->PushState(stack);
		_PushState(stack);
	}

	void ScopedSymbolicEnvironment::PopState(stk::LargeStack &stack) {
		_PopState(stack);
		subEnv->PopState(stack);
	}

	void ScopedSymbolicEnvironment::SetSymbolicVariable(const char * name, rev::ADDR_TYPE addr, rev::DWORD size) {
		subEnv->SetSymbolicVariable(name, addr, size);
	}

	bool ScopedSymbolicEnvironment::SetSubEnvironment(SymbolicEnvironment *env) {
		subEnv = env;
		return true;
	}

	void ScopedSymbolicEnvironment::SetReferenceCounting(AddRefFunc addRef, DecRefFunc decRef) {
		_SetReferenceCounting(addRef, decRef);
		subEnv->SetReferenceCounting(addRef, decRef);
	}

	bool ScopedSymbolicEnvironment::SetCurrentInstruction(RiverInstruction *instruction, void *opBuffer) {
		if (!subEnv->SetCurrentInstruction(instruction, opBuffer)) {
			return false;
		}

		return _SetCurrentInstruction(instruction, opBuffer);
	}

	bool ScopedSymbolicEnvironment::GetOperand(rev::BYTE opIdx, rev::BOOL &isTracked, rev::DWORD &concreteValue, void *&symbolicValue) {
		return subEnv->GetOperand(opIdx, isTracked, concreteValue, symbolicValue);
	}

	bool ScopedSymbolicEnvironment::GetFlgValue(rev::BYTE flg, rev::BOOL &isTracked, rev::BYTE &concreteValue, void *&symbolicValue) {
		return subEnv->GetFlgValue(flg, isTracked, concreteValue, symbolicValue);
	}

	bool ScopedSymbolicEnvironment::SetOperand(rev::BYTE opIdx, void *symbolicValue, bool doRefCount) {
		return subEnv->SetOperand(opIdx, symbolicValue, doRefCount);
	}

	bool ScopedSymbolicEnvironment::UnsetOperand(rev::BYTE opIdx, bool doRefCount) {
		return subEnv->UnsetOperand(opIdx, doRefCount);
	}

	void ScopedSymbolicEnvironment::SetFlgValue(rev::BYTE flg, void *symbolicValue, bool doRefCount) {
		return subEnv->SetFlgValue(flg, symbolicValue, doRefCount);
	}

	void ScopedSymbolicEnvironment::UnsetFlgValue(rev::BYTE flg, bool doRefCount) {
		return subEnv->UnsetFlgValue(flg, doRefCount);
	}
};