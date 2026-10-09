#pragma once

#include "Types.h"

class CMipsExecutor
{
public:
	virtual ~CMipsExecutor() = default;
	virtual void Reset() = 0;
	virtual int Execute(int) = 0;
	virtual void ClearActiveBlocksInRange(uint32 start, uint32 end, bool executing) = 0;
	//Compiled code of the block starting at address, or nullptr if it isn't compiled yet.
	virtual void* FindBlockCodeForLink(uint32 address) const
	{
		return nullptr;
	}

#ifdef DEBUGGER_INCLUDED
	virtual bool MustBreak() const = 0;
	virtual void DisableBreakpointsOnce() = 0;
	virtual bool FilterBreakpoint() = 0;
#endif
};
