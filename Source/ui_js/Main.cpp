#include <cstdio>
#include <algorithm>
#include <exception>
#include <emscripten/bind.h>
#include <emscripten/heap.h>
#include <future>
#include <AL/al.h>
#include "Ps2VmJs.h"
#include "GSH_OpenGLJs.h"
#include "sound/SH_OpenAL/SH_OpenALProxy.h"
#include "input/PH_GenericInput.h"
#include "InputProviderEmscripten.h"
#include "ui_shared/StatsManager.h"
#include "BasicBlock.h"
#include "DefaultAppConfig.h"

CPs2VmJs* g_virtualMachine = nullptr;
CGSHandler::NewFrameEvent::Connection g_gsNewFrameConnection;
CPS2VM::NewFrameEvent::Connection g_vmNewFrameConnection;
EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_context = 0;
std::shared_ptr<CInputProviderEmscripten> g_inputProvider;
CSH_OpenAL* g_soundHandler = nullptr;

//An exception nothing catches (e.g. on the VM thread) otherwise ends in a bare
//"RuntimeError: unreachable" with no hint of what went wrong.
static void ReportUncaughtException()
{
	try
	{
		if(auto exception = std::current_exception()) std::rethrow_exception(exception);
		fprintf(stderr, "Play! stopped: std::terminate called without an exception.\n");
	}
	catch(const std::exception& exception)
	{
		fprintf(stderr, "Play! stopped: uncaught exception: %s\n", exception.what());
	}
	catch(...)
	{
		fprintf(stderr, "Play! stopped: uncaught non-standard exception.\n");
	}
	fprintf(stderr, "Memory in use when it stopped: %u MB.\n", static_cast<unsigned>(emscripten_get_heap_size() >> 20));
	abort();
}

int main(int argc, const char** argv)
{
	std::set_terminate(&ReportUncaughtException);
	printf("Play! - Version %s\r\n", PLAY_VERSION);
	return 0;
}

EM_BOOL keyboardCallback(int eventType, const EmscriptenKeyboardEvent* keyEvent, void* userData)
{
	if(keyEvent->repeat)
	{
		return true;
	}
	switch(eventType)
	{
	case EMSCRIPTEN_EVENT_KEYDOWN:
		g_inputProvider->OnKeyDown(keyEvent->code);
		break;
	case EMSCRIPTEN_EVENT_KEYUP:
		g_inputProvider->OnKeyUp(keyEvent->code);
		break;
	}
	return true;
}

extern "C" void initVm()
{
	EmscriptenWebGLContextAttributes attr;
	emscripten_webgl_init_context_attributes(&attr);
	attr.majorVersion = 2;
	attr.minorVersion = 0;
	attr.alpha = false;
	g_context = emscripten_webgl_create_context("#outputCanvas", &attr);
	assert(g_context >= 0);

	g_virtualMachine = new CPs2VmJs();
	g_virtualMachine->Initialize();
	g_virtualMachine->CreateGSHandler(CGSH_OpenGLJs::GetFactoryFunction(g_context));

	{
		//Size here needs to match the size of the canvas in HTML file.

		CGSHandler::PRESENTATION_PARAMS presentationParams;
		presentationParams.mode = CGSHandler::PRESENTATION_MODE_FIT;
		presentationParams.windowWidth = 640;
		presentationParams.windowHeight = 480;

		g_virtualMachine->m_ee->m_gs->SetPresentationParams(presentationParams);
	}

	{
		g_virtualMachine->CreatePadHandler(CPH_GenericInput::GetFactoryFunction());
		auto padHandler = static_cast<CPH_GenericInput*>(g_virtualMachine->GetPadHandler());
		auto& bindingManager = padHandler->GetBindingManager();

		g_inputProvider = std::make_shared<CInputProviderEmscripten>();
		bindingManager.RegisterInputProvider(g_inputProvider);

		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::START, CInputProviderEmscripten::MakeBindingTarget("Enter"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::SELECT, CInputProviderEmscripten::MakeBindingTarget("Backspace"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::DPAD_LEFT, CInputProviderEmscripten::MakeBindingTarget("ArrowLeft"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::DPAD_RIGHT, CInputProviderEmscripten::MakeBindingTarget("ArrowRight"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::DPAD_UP, CInputProviderEmscripten::MakeBindingTarget("ArrowUp"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::DPAD_DOWN, CInputProviderEmscripten::MakeBindingTarget("ArrowDown"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::SQUARE, CInputProviderEmscripten::MakeBindingTarget("KeyA"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::CROSS, CInputProviderEmscripten::MakeBindingTarget("KeyZ"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::TRIANGLE, CInputProviderEmscripten::MakeBindingTarget("KeyS"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::CIRCLE, CInputProviderEmscripten::MakeBindingTarget("KeyX"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::L1, CInputProviderEmscripten::MakeBindingTarget("Digit1"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::L2, CInputProviderEmscripten::MakeBindingTarget("Digit2"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::L3, CInputProviderEmscripten::MakeBindingTarget("Digit3"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::R1, CInputProviderEmscripten::MakeBindingTarget("Digit8"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::R2, CInputProviderEmscripten::MakeBindingTarget("Digit9"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::R3, CInputProviderEmscripten::MakeBindingTarget("Digit0"));

		bindingManager.SetSimulatedAxisBinding(0, PS2::CControllerInfo::ANALOG_LEFT_X,
		                                       CInputProviderEmscripten::MakeBindingTarget("KeyF"),
		                                       CInputProviderEmscripten::MakeBindingTarget("KeyH"));
		bindingManager.SetSimulatedAxisBinding(0, PS2::CControllerInfo::ANALOG_LEFT_Y,
		                                       CInputProviderEmscripten::MakeBindingTarget("KeyT"),
		                                       CInputProviderEmscripten::MakeBindingTarget("KeyG"));

		bindingManager.SetSimulatedAxisBinding(0, PS2::CControllerInfo::ANALOG_RIGHT_X,
		                                       CInputProviderEmscripten::MakeBindingTarget("KeyJ"),
		                                       CInputProviderEmscripten::MakeBindingTarget("KeyL"));
		bindingManager.SetSimulatedAxisBinding(0, PS2::CControllerInfo::ANALOG_RIGHT_Y,
		                                       CInputProviderEmscripten::MakeBindingTarget("KeyI"),
		                                       CInputProviderEmscripten::MakeBindingTarget("KeyK"));
	}

	{
		g_soundHandler = new CSH_OpenAL();
		g_virtualMachine->CreateSoundHandler(CSH_OpenALProxy::GetFactoryFunction(g_soundHandler));
	}

	g_gsNewFrameConnection = g_virtualMachine->GetGSHandler()->OnNewFrame.Connect(std::bind(&CStatsManager::OnGsNewFrame, &CStatsManager::GetInstance(), std::placeholders::_1));
	g_vmNewFrameConnection = g_virtualMachine->OnNewFrame.Connect(std::bind(&CStatsManager::OnNewFrame, &CStatsManager::GetInstance(), g_virtualMachine));

	EMSCRIPTEN_RESULT result = EMSCRIPTEN_RESULT_SUCCESS;

	result = emscripten_set_keydown_callback("#outputCanvas", nullptr, false, &keyboardCallback);
	assert(result == EMSCRIPTEN_RESULT_SUCCESS);

	result = emscripten_set_keyup_callback("#outputCanvas", nullptr, false, &keyboardCallback);
	assert(result == EMSCRIPTEN_RESULT_SUCCESS);
}

void bootElf(std::string path)
{
	g_virtualMachine->BootElf(path);
}

void bootDiscImage(std::string path)
{
	g_virtualMachine->BootDiscImage(path);
}

int getFrames()
{
	return CStatsManager::GetInstance().GetFrames();
}

//Stats accumulated since the last clearStats(): draw calls, EE/IOP usage and, in builds
//configured with -DPROFILE=ON, time spent per subsystem.
std::string getStats()
{
	auto& stats = CStatsManager::GetInstance();
	auto cpu = stats.GetCpuUtilisationInfo();
	std::string result;
	result += "Draw calls: " + std::to_string(stats.GetDrawCalls()) + "\n";
	result += "EE usage:  " + std::to_string(static_cast<int>(CStatsManager::ComputeCpuUsageRatio(cpu.eeIdleTicks, cpu.eeTotalTicks))) + "%\n";
	result += "IOP usage: " + std::to_string(static_cast<int>(CStatsManager::ComputeCpuUsageRatio(cpu.iopIdleTicks, cpu.iopTotalTicks))) + "%\n";
	result += "Memory:    " + std::to_string(emscripten_get_heap_size() >> 20) + " MB of " + std::to_string(emscripten_get_heap_max() >> 20) + " MB\n";
	{
		auto compile = GetBlockCompileStats(true);
		char line[200];
		snprintf(line, sizeof(line), "JIT compiles: EE %u (%.1f ms) | IOP %u (%.1f ms) | VU %u (%.1f ms), VU cache hits %u\n",
		         compile.count[0], compile.milliseconds[0], compile.count[1], compile.milliseconds[1],
		         compile.count[2], compile.milliseconds[2], compile.vuCacheHits);
		result += line;
	}
#ifdef PROFILE
	result += "\n      Zone  Share  Avg/frame    Min      Max\n";
	result += stats.GetProfilingInfo();
#endif
	return result;
}

void pauseVm()
{
	g_virtualMachine->PauseAsyncJs();
}

void resumeVm()
{
	g_virtualMachine->ResumeAsyncJs();
}

bool isPaused()
{
	return g_virtualMachine->IsPaused();
}

void setEeClockScale(int numerator, int denominator)
{
	if(numerator <= 0 || denominator <= 0) return;
	g_virtualMachine->SetEeClockScale(numerator, denominator);
}

//Save states run on the VM thread; the page polls getStateResult() instead of blocking the main
//thread (which runs GS calls the save needs).
static std::future<bool> g_stateOperation;

void saveState(std::string path)
{
	g_stateOperation = g_virtualMachine->SaveState(path);
}

void loadState(std::string path)
{
	g_stateOperation = g_virtualMachine->LoadState(path);
}

//-1: in progress, 0: failed, 1: succeeded, 2: nothing pending.
int getStateResult()
{
	if(!g_stateOperation.valid()) return 2;
	if(g_stateOperation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return -1;
	return g_stateOperation.get() ? 1 : 0;
}

void setVolume(float volume)
{
	alListenerf(AL_GAIN, std::clamp(volume, 0.f, 1.f));
}

void clearStats()
{
	CStatsManager::GetInstance().ClearStats();
}

EMSCRIPTEN_BINDINGS(Play)
{
	using namespace emscripten;

	function("bootElf", &bootElf);
	function("bootDiscImage", &bootDiscImage);
	function("getFrames", &getFrames);
	function("clearStats", &clearStats);
	function("getStats", &getStats);
	function("pauseVm", &pauseVm);
	function("resumeVm", &resumeVm);
	function("isPaused", &isPaused);
	function("setEeClockScale", &setEeClockScale);
	function("saveState", &saveState);
	function("loadState", &loadState);
	function("getStateResult", &getStateResult);
	function("setVolume", &setVolume);
}
