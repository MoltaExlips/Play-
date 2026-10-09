#include "Ps2VmJs.h"
#include "Jitter_CodeGen_Wasm.h"
#include "MemoryUtils.h"
#include "BasicBlock.h"
#include "PS2VM_Preferences.h"
#include "AppConfig.h"
#include <emscripten.h>

// CodeGen binds each helper by wrapping its JS export (Emscripten's lazy export stub) with
// convertJsFunctionToWasm, so every call from generated code goes wasm -> JS -> wasm. A C++
// function pointer is an index into the module's function table, so point the import table
// slot straight at the wasm function instead.
EM_JS_DEPS(PlayJsDirectImports, "$getWasmTableEntry");
EM_JS(void, BindImportDirect, (int importId, uintptr_t functionPtr), {
	Module.codeGenImportTable.set(importId, getWasmTableEntry(functionPtr));
});

static void RegisterFunction(uintptr_t functionPtr, const char* functionName, const char* functionSig)
{
	Jitter::CWasmFunctionRegistry::RegisterFunction(functionPtr, functionName, functionSig);
	BindImportDirect(Jitter::CWasmFunctionRegistry::FindFunction(functionPtr)->id, functionPtr);
}

extern "C" uint32 LWL_Proxy(uint32, uint32, CMIPS*);
extern "C" uint32 LWR_Proxy(uint32, uint32, CMIPS*);
extern "C" uint64 LDL_Proxy(uint32, uint64, CMIPS*);
extern "C" uint64 LDR_Proxy(uint32, uint64, CMIPS*);
extern "C" void SWL_Proxy(uint32, uint32, CMIPS*);
extern "C" void SWR_Proxy(uint32, uint32, CMIPS*);
extern "C" void SDL_Proxy(uint32, uint64, CMIPS*);
extern "C" void SDR_Proxy(uint32, uint64, CMIPS*);

void CPs2VmJs::CreateVM()
{
	printf("Initializing PS2VM...\r\n");

	RegisterFunction(reinterpret_cast<uintptr_t>(&EmptyBlockHandler), "_EmptyBlockHandler", "vi");
	RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_GetByteProxy), "_MemoryUtils_GetByteProxy", "iii");
	RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_GetHalfProxy), "_MemoryUtils_GetHalfProxy", "iii");
	RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_GetWordProxy), "_MemoryUtils_GetWordProxy", "iii");
	RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_GetDoubleProxy), "_MemoryUtils_GetDoubleProxy", "jii");

	RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_SetByteProxy), "_MemoryUtils_SetByteProxy", "viii");
	RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_SetHalfProxy), "_MemoryUtils_SetHalfProxy", "viii");
	RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_SetWordProxy), "_MemoryUtils_SetWordProxy", "viii");
	RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_SetDoubleProxy), "_MemoryUtils_SetDoubleProxy", "viji");

	RegisterFunction(reinterpret_cast<uintptr_t>(&LWL_Proxy), "_LWL_Proxy", "iiii");
	RegisterFunction(reinterpret_cast<uintptr_t>(&LWR_Proxy), "_LWR_Proxy", "iiii");

	RegisterFunction(reinterpret_cast<uintptr_t>(&LDL_Proxy), "_LDL_Proxy", "jiji");
	RegisterFunction(reinterpret_cast<uintptr_t>(&LDR_Proxy), "_LDR_Proxy", "jiji");

	RegisterFunction(reinterpret_cast<uintptr_t>(&SWL_Proxy), "_SWL_Proxy", "viii");
	RegisterFunction(reinterpret_cast<uintptr_t>(&SWR_Proxy), "_SWR_Proxy", "viii");

	RegisterFunction(reinterpret_cast<uintptr_t>(&SDL_Proxy), "_SDL_Proxy", "viji");
	RegisterFunction(reinterpret_cast<uintptr_t>(&SDR_Proxy), "_SDR_Proxy", "viji");

	CPS2VM::CreateVM();
}

void CPs2VmJs::BootElf(std::string path)
{
	m_mailBox.SendCall([this, path]() {
		printf("Loading '%s'...\r\n", path.c_str());
		try
		{
			Reset();
			m_ee->m_os->BootFromFile(path);
		}
		catch(const std::exception& ex)
		{
			printf("Failed to start: %s.\r\n", ex.what());
			return;
		}
		printf("Starting...\r\n");
		ResumeImpl();
	});
}

void CPs2VmJs::BootDiscImage(std::string path)
{
	m_mailBox.SendCall([this, path]() {
		printf("Loading '%s'...\r\n", path.c_str());
		try
		{
			CAppConfig::GetInstance().SetPreferencePath(PREF_PS2_CDROM0_PATH, path);
			Reset();
			m_ee->m_os->BootFromCDROM();
		}
		catch(const std::exception& ex)
		{
			printf("Failed to start: %s.\r\n", ex.what());
			return;
		}
		printf("Starting...\r\n");
		ResumeImpl();
	});
}