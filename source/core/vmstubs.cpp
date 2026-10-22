#include "name.h"
#include "dobject.h"

bool ShouldAllowGameSpecificVirtual(FName name, unsigned index, PType* arg, PType* varg)
{
	return false;
}

// The ZScript debug adapter (common/scripting/dap) depends on GZDoom's actor and state
// system, which Raze does not have, so it is not compiled. These are its VM entry points.
#include "common/scripting/dap/GameEventEmit.h"

namespace DebugServer
{
namespace RuntimeEvents
{
	void EmitInstructionExecutionEvent(VMFrameStack *stack, VMReturn *ret, int numret, const VMOP *pc) {}
	void EmitLogEvent(int level, const char *message) {}
	void EmitExceptionEvent(EVMAbortException reason, const std::string &message, const std::string &stackTrace) {}
	bool IsDebugServerRunning() { return false; }
}
}
