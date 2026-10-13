#pragma once

#include "Types.h"

class CMipsExecutor
{
public:
	virtual ~CMipsExecutor() = default;
	virtual void Reset() = 0;
	virtual int Execute(int) = 0;
	virtual void ClearActiveBlocksInRange(uint32 start, uint32 end, bool executing) = 0;
	//Browser build: compiled code is per thread; recompile code kept across resets (the empty
	//block) on the calling thread before running this executor there.
	virtual void RecompileForThisThread()
	{
	}

#ifdef DEBUGGER_INCLUDED
	virtual bool MustBreak() const = 0;
	virtual void DisableBreakpointsOnce() = 0;
	virtual bool FilterBreakpoint() = 0;
#endif
};
