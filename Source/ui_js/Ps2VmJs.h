#pragma once

#include "PS2VM.h"

class CPs2VmJs : public CPS2VM
{
public:
	void CreateVM() override;

	void BootElf(std::string);
	void BootDiscImage(std::string);

	//Non-blocking: the main thread must stay free to run GS calls.
	void PauseAsyncJs();
	void ResumeAsyncJs();
	bool IsPaused() const;

	//EE clock as a fraction of the real PS2 (speed hack: fewer EE cycles per frame).
	//Kept across boots, since Reset() sets the scale back to 1:1.
	void SetEeClockScale(uint32 numerator, uint32 denominator);

private:
	void ApplyEeClockScale();

	uint32 m_eeScaleNumerator = 1;
	uint32 m_eeScaleDenominator = 1;
};
