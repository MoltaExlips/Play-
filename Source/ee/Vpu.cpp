#include "Vpu.h"
#include "make_unique.h"
#include "string_format.h"
#include "Log.h"
#include "../states/RegisterStateFile.h"
#include "../Ps2Const.h"
#include "../FrameDump.h"
#include "Vif.h"
#include "Vif1.h"
#include "GIF.h"

#define LOG_NAME ("ee_vpu")

#define STATE_PATH_REGS_FORMAT ("vpu/vpu_%d.xml")

#define STATE_REGS_VUSTATE ("vuState")
#define STATE_REGS_FBRST ("fbrst")

CVpu::CVpu(unsigned int number, const VPUINIT& vpuInit, CGIF& gif, CINTC& intc, uint8* ram, uint8* spr)
    : m_number(number)
    , m_vif((number == 0) ? std::make_unique<CVif>(0, *this, intc, ram, spr) : std::make_unique<CVif1>(1, *this, gif, intc, ram, spr))
    , m_microMem(vpuInit.microMem)
    , m_microMemSize((number == 0) ? PS2::MICROMEM0SIZE : PS2::MICROMEM1SIZE)
    , m_vuMem(vpuInit.vuMem)
    , m_vuMemSize((number == 0) ? PS2::VUMEM0SIZE : PS2::VUMEM1SIZE)
    , m_ctx(vpuInit.context)
    , m_gif(gif)
    , m_vuProfilerZone(CProfiler::GetInstance().RegisterZone("VU"))
#ifdef DEBUGGER_INCLUDED
    , m_microMemMiniState(new uint8[(number == 0) ? PS2::MICROMEM0SIZE : PS2::MICROMEM1SIZE])
    , m_vuMemMiniState(new uint8[(number == 0) ? PS2::VUMEM0SIZE : PS2::VUMEM1SIZE])
    , m_topMiniState(0)
    , m_itopMiniState(0)
#endif
{
}

std::atomic<bool> CVpu::s_asyncRequested = false;
void (*CVpu::s_asyncThreadInit)() = nullptr;

CVpu::~CVpu()
{
	StopAsyncThread();
#ifdef DEBUGGER_INCLUDED
	delete[] m_microMemMiniState;
	delete[] m_vuMemMiniState;
#endif
}

void CVpu::Execute(int32 quota)
{
	if(m_async)
	{
		PollAsync();
		return;
	}

	if(m_vuState != VU_STATE_RUNNING) return;

#ifdef PROFILE
	CProfilerZone profilerZone(m_vuProfilerZone);
#endif

	m_ctx->m_executor->Execute(quota);
	switch(m_ctx->m_State.nHasException)
	{
	case MIPS_EXCEPTION_VU_EBIT:
		//E bit encountered
		m_vuState = VU_STATE_READY;
		VuStateChanged(m_vuState);
		break;
	case MIPS_EXCEPTION_VU_TBIT:
	case MIPS_EXCEPTION_VU_DBIT:
		//T/D bit encountered
		{
			bool mustBreak = false;
			mustBreak |= (m_ctx->m_State.nHasException == MIPS_EXCEPTION_VU_TBIT) && (m_fbrst & FBRST_TE);
			mustBreak |= (m_ctx->m_State.nHasException == MIPS_EXCEPTION_VU_DBIT) && (m_fbrst & FBRST_DE);
			if(mustBreak)
			{
				m_vuState = VU_STATE_STOPPED;
				VuStateChanged(m_vuState);
				VuInterruptTriggered();
			}
			else
			{
				m_ctx->m_State.nHasException = 0;
			}
		}
		break;
	default:
		break;
	}
}

#ifdef DEBUGGER_INCLUDED

void CVpu::SaveMiniState()
{
	memcpy(m_microMemMiniState, m_microMem, (m_number == 0) ? PS2::MICROMEM0SIZE : PS2::MICROMEM1SIZE);
	memcpy(m_vuMemMiniState, m_vuMem, (m_number == 0) ? PS2::VUMEM0SIZE : PS2::VUMEM1SIZE);
	memcpy(&m_vuMiniState, &m_ctx->m_State, sizeof(MIPSSTATE));
	m_topMiniState = (m_number == 0) ? 0 : m_vif->GetTOP();
	m_itopMiniState = m_vif->GetITOP();
}

const MIPSSTATE& CVpu::GetVuMiniState() const
{
	return m_vuMiniState;
}

uint8* CVpu::GetVuMemoryMiniState() const
{
	return m_vuMemMiniState;
}

uint8* CVpu::GetMicroMemoryMiniState() const
{
	return m_microMemMiniState;
}

uint32 CVpu::GetVuTopMiniState() const
{
	return m_topMiniState;
}

uint32 CVpu::GetVuItopMiniState() const
{
	return m_itopMiniState;
}

#endif

void CVpu::Reset()
{
	//Compiled blocks belong to the thread that compiled them: reset them there, then pick the
	//mode for this boot.
	if(m_async)
	{
		m_asyncStopRequest = true;
		WaitForAsyncIdle();
		m_asyncStopRequest = false;
		RunOnVuThread([this]() { m_ctx->m_executor->Reset(); });
	}
	else
	{
		m_ctx->m_executor->Reset();
	}
	{
		std::lock_guard lock(m_xgKickMutex);
		m_xgKicks.clear();
	}
	m_vuState = VU_STATE_READY;
	m_async = (m_number == 1) && s_asyncRequested;
	if(m_async)
	{
		StartAsyncThread();
	}
	if(m_async != m_blocksOnWorker)
	{
		//Code kept across resets (the executor's empty block) moves to the thread that runs VU1.
		if(m_async)
		{
			RunOnVuThread([this]() { m_ctx->m_executor->RecompileForThisThread(); });
		}
		else
		{
			m_ctx->m_executor->RecompileForThisThread();
		}
		m_blocksOnWorker = m_async;
	}
	m_vif->Reset();
}

void CVpu::SaveState(Framework::CZipArchiveWriter& archive)
{
	{
		auto path = string_format(STATE_PATH_REGS_FORMAT, m_number);
		auto registerFile = std::make_unique<CRegisterStateFile>(path.c_str());
		registerFile->SetRegister32(STATE_REGS_VUSTATE, m_vuState);
		registerFile->SetRegister32(STATE_REGS_FBRST, m_fbrst);
		archive.InsertFile(std::move(registerFile));
	}

	m_vif->SaveState(archive);
}

void CVpu::LoadState(Framework::CZipArchiveReader& archive)
{
	{
		auto path = string_format(STATE_PATH_REGS_FORMAT, m_number);
		CRegisterStateFile registerFile(*archive.BeginReadFile(path.c_str()));
		m_vuState = static_cast<VU_STATE>(registerFile.GetRegister32(STATE_REGS_VUSTATE));
		m_fbrst = registerFile.GetRegister32(STATE_REGS_FBRST);
	}

	m_vif->LoadState(archive);
}

CMIPS& CVpu::GetContext() const
{
	return *m_ctx;
}

uint8* CVpu::GetMicroMemory() const
{
	return m_microMem;
}

uint32 CVpu::GetMicroMemorySize() const
{
	return m_microMemSize;
}

uint8* CVpu::GetVuMemory() const
{
	return m_vuMem;
}

uint32 CVpu::GetVuMemorySize() const
{
	return m_vuMemSize;
}

CVif& CVpu::GetVif()
{
	return *m_vif.get();
}

void CVpu::SetFbrst(uint32 fbrst)
{
	//Only keep DE and TE bits
	m_fbrst = (fbrst & (FBRST_DE | FBRST_TE));
}

void CVpu::ExecuteMicroProgram(uint32 nAddress)
{
	CLog::GetInstance().Print(LOG_NAME, "Starting microprogram execution at 0x%08X.\r\n", nAddress);

	m_ctx->m_State.nPC = nAddress;
	m_ctx->m_State.pipeTime = 0;
	m_ctx->m_State.pipeFmacWrite[0] = {};
	m_ctx->m_State.pipeFmacWrite[1] = {};
	m_ctx->m_State.pipeFmacWrite[2] = {};
	m_ctx->m_State.savedNextBlockIntRegIdx = 0;
	m_ctx->m_State.nHasException = 0;

#ifdef DEBUGGER_INCLUDED
	SaveMiniState();
#endif

	assert(m_vuState != VU_STATE_RUNNING);
	m_vuState = VU_STATE_RUNNING;
	VuStateChanged(m_vuState);
	if(m_async)
	{
		m_asyncBusy.store(true, std::memory_order_release);
		m_asyncMailBox.SendCall([this]() { RunAsyncMicroProgram(); });
		return;
	}
	for(unsigned int i = 0; i < 100; i++)
	{
		Execute(5000);
		if(m_vuState != VU_STATE_RUNNING) break;
	}
}

void CVpu::InvalidateMicroProgram()
{
	InvalidateMicroProgram(0, (m_number == 0) ? PS2::MICROMEM0SIZE : PS2::MICROMEM1SIZE);
}

void CVpu::InvalidateMicroProgram(uint32 start, uint32 end)
{
	if(m_async)
	{
		RunOnVuThread([this, start, end]() { m_ctx->m_executor->ClearActiveBlocksInRange(start, end, false); });
		return;
	}
	m_ctx->m_executor->ClearActiveBlocksInRange(start, end, false);
}

void CVpu::ProcessXgKick(uint32 address)
{
	if(m_async && (std::this_thread::get_id() == m_asyncThreadId))
	{
		QueueXgKick(address);
		return;
	}

	address &= 0x3FF;
	address *= 0x10;

	CGsPacketMetadata metadata;
	metadata.pathIndex = 1;
#ifdef DEBUGGER_INCLUDED
	metadata.vuMemPacketAddress = address;
	metadata.vpu1Top = GetVuTopMiniState();
	metadata.vpu1Itop = GetVuItopMiniState();
	memcpy(&metadata.vu1State, &GetVuMiniState(), sizeof(MIPSSTATE));
	memcpy(metadata.vuMem1, GetVuMemoryMiniState(), PS2::VUMEM1SIZE);
	memcpy(metadata.microMem1, GetMicroMemoryMiniState(), PS2::MICROMEM1SIZE);
#endif

	address += m_gif.ProcessSinglePacket(GetVuMemory(), PS2::VUMEM1SIZE, address, PS2::VUMEM1SIZE, metadata);
	if((address == PS2::VUMEM1SIZE) && (m_gif.GetActivePath() == 1))
	{
		address = 0;
		address += m_gif.ProcessSinglePacket(GetVuMemory(), PS2::VUMEM1SIZE, address, PS2::VUMEM1SIZE, metadata);
	}
	assert(m_gif.GetActivePath() == 0);

#ifdef DEBUGGER_INCLUDED
	SaveMiniState();
#endif
}

//Multi-threaded VU1 ------------------------------------------------------------

void CVpu::StartAsyncThread()
{
	if(m_asyncThread.joinable()) return;
	m_asyncEnd = false;
	m_asyncThread = std::thread([this]() {
		if(s_asyncThreadInit) s_asyncThreadInit();
		while(!m_asyncEnd)
		{
			m_asyncMailBox.WaitForCall();
			while(m_asyncMailBox.IsPending())
			{
				m_asyncMailBox.ReceiveCall();
			}
		}
	});
	m_asyncThreadId = m_asyncThread.get_id();
}

void CVpu::StopAsyncThread()
{
	if(!m_asyncThread.joinable()) return;
	m_asyncStopRequest = true;
	m_asyncMailBox.SendCall([this]() { m_asyncEnd = true; });
	m_asyncThread.join();
	m_asyncStopRequest = false;
}

void CVpu::RunOnVuThread(const std::function<void()>& function)
{
	if(!m_asyncThread.joinable() || (std::this_thread::get_id() == m_asyncThreadId))
	{
		function();
		return;
	}
	m_asyncMailBox.SendCall(function, true);
}

//Worker thread: runs the microprogram started by ExecuteMicroProgram until it ends.
void CVpu::RunAsyncMicroProgram()
{
	uint32 result = 0;
	while(!m_asyncStopRequest)
	{
		m_ctx->m_executor->Execute(5000);
		uint32 exception = m_ctx->m_State.nHasException;
		if(exception == MIPS_EXCEPTION_VU_EBIT)
		{
			result = exception;
			break;
		}
		if((exception == MIPS_EXCEPTION_VU_TBIT) || (exception == MIPS_EXCEPTION_VU_DBIT))
		{
			bool mustBreak = false;
			mustBreak |= (exception == MIPS_EXCEPTION_VU_TBIT) && (m_fbrst & FBRST_TE);
			mustBreak |= (exception == MIPS_EXCEPTION_VU_DBIT) && (m_fbrst & FBRST_DE);
			if(mustBreak)
			{
				result = exception;
				break;
			}
			m_ctx->m_State.nHasException = 0;
		}
		//Long programs: let the EE thread's requests (e.g. invalidations) through.
		while(m_asyncMailBox.IsPending())
		{
			m_asyncMailBox.ReceiveCall();
		}
	}
	m_asyncResult = result;
	{
		std::lock_guard lock(m_asyncIdleMutex);
		m_asyncBusy.store(false, std::memory_order_release);
	}
	m_asyncIdleCondition.notify_all();
}

//EE thread: hand queued XGKICKs to the GIF, and finish the program once the worker is done.
void CVpu::PollAsync()
{
	DrainXgKicks();
	if((m_vuState != VU_STATE_RUNNING) || m_asyncBusy.load(std::memory_order_acquire)) return;
	//Stay running until every XGKICK reached the GIF, so VIF1 and PATH3 keep waiting for them.
	if(!DrainXgKicks()) return;
	switch(m_asyncResult)
	{
	case MIPS_EXCEPTION_VU_TBIT:
	case MIPS_EXCEPTION_VU_DBIT:
		m_vuState = VU_STATE_STOPPED;
		VuStateChanged(m_vuState);
		VuInterruptTriggered();
		break;
	default:
		m_vuState = VU_STATE_READY;
		VuStateChanged(m_vuState);
		break;
	}
}

void CVpu::WaitForAsyncIdle()
{
	if(!m_async) return;
	{
		std::unique_lock lock(m_asyncIdleMutex);
		m_asyncIdleCondition.wait(lock, [this]() { return !m_asyncBusy.load(std::memory_order_acquire); });
	}
	PollAsync();
}

//Worker thread: copy the GS packet the microprogram kicks (it may overwrite the memory right after).
void CVpu::QueueXgKick(uint32 address)
{
	address = (address & 0x3FF) * 0x10;
	const uint32 memSize = PS2::VUMEM1SIZE;

	//Walk GIF tags until EOP to find the packet size.
	uint32 size = 0;
	while(size < memSize)
	{
		uint64 tag = 0;
		memcpy(&tag, m_vuMem + ((address + size) & (memSize - 1)), sizeof(tag));
		uint32 nloop = static_cast<uint32>(tag & 0x7FFF);
		bool eop = ((tag >> 15) & 1) != 0;
		uint32 flg = static_cast<uint32>((tag >> 58) & 3);
		uint32 nreg = static_cast<uint32>((tag >> 60) & 0xF);
		if(nreg == 0) nreg = 16;
		uint32 dataQwords = 0;
		switch(flg)
		{
		case 0: //PACKED
			dataQwords = nloop * nreg;
			break;
		case 1: //REGLIST
			dataQwords = ((nloop * nreg) + 1) / 2;
			break;
		default: //IMAGE
			dataQwords = nloop;
			break;
		}
		size += 0x10 + (dataQwords * 0x10);
		if(eop) break;
	}
	size = std::min(size, memSize);

	std::vector<uint8> packet(size);
	uint32 firstPart = std::min(size, memSize - address);
	memcpy(packet.data(), m_vuMem + address, firstPart);
	memcpy(packet.data() + firstPart, m_vuMem, size - firstPart);

	std::lock_guard lock(m_xgKickMutex);
	m_xgKicks.push_back(std::move(packet));
}

//Sends queued XGKICK packets to the GIF. Returns false if some must wait: while another path is in the
//middle of a packet (e.g. a PATH3 image transfer split across DMA chunks), the GIF would take PATH1
//data as that packet's continuation.
bool CVpu::DrainXgKicks()
{
	for(;;)
	{
		std::vector<uint8> packet;
		{
			std::lock_guard lock(m_xgKickMutex);
			if(m_xgKicks.empty()) return true;
			auto activePath = m_gif.GetActivePath();
			if((activePath != 0) && (activePath != 1)) return false;
			packet = std::move(m_xgKicks.front());
			m_xgKicks.pop_front();
		}
		CGsPacketMetadata metadata;
		metadata.pathIndex = 1;
		uint32 size = static_cast<uint32>(packet.size());
		uint32 offset = 0;
		while(offset < size)
		{
			uint32 processed = m_gif.ProcessSinglePacket(packet.data(), size, offset, size, metadata);
			if(processed == 0) break;
			offset += processed;
		}
	}
}
