#pragma once

#include "BasicBlock.h"

class CEeBasicBlock : public CBasicBlock
{
public:
	using CBasicBlock::CBasicBlock;

	void SetFpRoundingMode(Jitter::CJitter::ROUNDINGMODE);
	void SetIsIdleLoopBlock();
#ifdef __EMSCRIPTEN__
	uint32 GetCompileKeyExtra() const override
	{
		return static_cast<uint32>(m_fpRoundingMode) | (m_isIdleLoopBlock ? 0x100 : 0);
	}
#endif

protected:
	void CompileProlog(CMipsJitter*) override;
	void CompileEpilog(CMipsJitter*, bool) override;

private:
	bool IsCodeIdleLoopBlock() const;

	static constexpr auto DEFAULT_FP_ROUNDING_MODE = Jitter::CJitter::ROUND_TRUNCATE;
	Jitter::CJitter::ROUNDINGMODE m_fpRoundingMode = DEFAULT_FP_ROUNDING_MODE;

	bool m_isIdleLoopBlock = false;
};
