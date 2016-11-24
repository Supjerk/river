#include "Debugger.h"

#include <sys/wait.h>
#include <sys/user.h>
#include <string.h>

namespace dbg {

#if defined(__i386)
#define REGISTER_IP EIP
#define TRAP_LEN    1
#define TRAP_INST   0xCC
#define TRAP_MASK   0xFFFFFF00
#endif

	const int long_size = sizeof(long);
	void Debugger::GetData(long addr, unsigned char *str, int len)
	{   unsigned char *laddr;
		int i, j;
		union u {
			long val;
			char chars[long_size];
		}data;
		i = 0;
		j = len / long_size;
		laddr = str;
		while(i < j) {
			data.val = ptrace(PTRACE_PEEKDATA, Tracee,
					addr + i * 4, nullptr);
			memcpy(laddr, data.chars, long_size);
			++i;
			laddr += long_size;
		}
		j = len % long_size;
		if(j != 0) {
			data.val = ptrace(PTRACE_PEEKDATA, Tracee,
					addr + i * 4, nullptr);
			memcpy(laddr, data.chars, j);
		}
		str[len] = '\0';
	}

	void Debugger::PutData(long addr, unsigned char *str, int len)
	{   unsigned char *laddr;
		int i, j;
		union u {
			long val;
			char chars[long_size];
		}data;
		i = 0;
		j = len / long_size;
		laddr = str;
		while(i < j) {
			memcpy(data.chars, laddr, long_size);
			ptrace(PTRACE_POKEDATA, Tracee,
					addr + i * 4, data.val);
			++i;
			laddr += long_size;
		}
		j = len % long_size;
		if(j != 0) {
			memcpy(data.chars, laddr, j);
			ptrace(PTRACE_POKEDATA, Tracee,
					addr + i * 4, data.val);
		}
	}

	void Debugger::SetEip(unsigned long address) {
		struct user_regs_struct regs;
		ptrace(PTRACE_GETREGS, Tracee, 0, &regs);
		regs.eip = address;
		ptrace (PTRACE_SETREGS, Tracee, 0, &regs);
	}

	void Debugger::PrintEip() {
		struct user_regs_struct regs;
		ptrace(PTRACE_GETREGS, Tracee, 0, &regs);
		printf("[Debugger] Tracee process %d stopped at eip %lx\n", Tracee, regs.eip);
	}

	Debugger::Debugger() {
		Tracee = -1;
	}

	void Debugger::Attach(pid_t pid) {
		if (Tracee != -1) {
			printf("[Debugger] Already tracing %d\n", Tracee);
			return;
		}

		Tracee = pid;
		int status;

		//ptrace(PTRACE_ATTACH, pid);
		waitpid(pid, &status, 0);
		ptrace(PTRACE_SETOPTIONS, Tracee, 0, PTRACE_O_TRACEEXIT);

		struct user_regs_struct regs;
		ptrace(PTRACE_GETREGS, pid, 0, &regs);
		printf("[Debugger] Attached to pid %d, ip %08lx\n", pid, regs.eip);
	}

	unsigned long Debugger::InsertBreakpoint(unsigned long address) {
		printf("[Debugger] Inserting breakpoint at address %lx\n", address);

		long orig = ptrace(PTRACE_PEEKTEXT, Tracee, address);
		ptrace(PTRACE_POKETEXT, Tracee, address, (orig & TRAP_MASK) | TRAP_INST);
		BreakpointCode.insert(std::make_pair(address, orig));
		return address;
	}

	void Debugger::DeleteBreakpoint(unsigned long address) {
		printf("[Debugger] Deleting breakpoint at address %lx\n", address);
		if (BreakpointCode.find(address) == BreakpointCode.end()) {
			printf("[Debugger] Breakpoint not found at address %lx\n", address);
			return;
		}
		ptrace(PTRACE_POKETEXT, Tracee, address, BreakpointCode[address]);
		SetEip(address);
	}

	int Debugger::Run(enum __ptrace_request request) {
		int status, last_sig = 0, event;

		while (1) {
			ptrace(request, Tracee, 0, last_sig);
			waitpid(Tracee, &status, 0);

			if (WIFEXITED(status))
				return 0;

			if (WIFSTOPPED(status)) {
				last_sig = WSTOPSIG(status);
				if (last_sig == SIGTRAP) {
					event = (status >> 16) & 0xffff;
					return (event == PTRACE_EVENT_EXIT) ? 0 : 1;
				}
			}
		}
	}

} //namespace dbg
