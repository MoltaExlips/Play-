#include "GSH_OpenGLJs.h"
#include <emscripten/proxying.h>

//GS calls run on the browser main thread, which owns the WebGL context, so GL calls are made
//directly instead of each being proxied from a GS thread to the main thread.
CGSH_OpenGLJs::CGSH_OpenGLJs(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context)
    : CGSH_OpenGL(true, true)
    , m_context(context)
{
}

CGSH_OpenGL::FactoryFunction CGSH_OpenGLJs::GetFactoryFunction(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context)
{
	return [context]() { return new CGSH_OpenGLJs(context); };
}

void CGSH_OpenGLJs::InitializeImpl()
{
	emscripten_webgl_make_context_current(m_context);
	CGSH_OpenGL::InitializeImpl();
}

void CGSH_OpenGLJs::ReleaseImpl()
{
	CGSH_OpenGL::ReleaseImpl();
}

void CGSH_OpenGLJs::PresentBackbuffer()
{
}

void CGSH_OpenGLJs::NotifyCallPosted()
{
	//One scheduled pump drains everything queued before it runs.
	if(m_pumpScheduled.exchange(true)) return;
	//Proxied work also runs while the main thread is blocked waiting on a lock, which prevents
	//deadlocks when the main thread waits on the VM thread while it waits on the GS.
	emscripten_proxy_async(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(), &CGSH_OpenGLJs::PumpCalls, this);
}

void CGSH_OpenGLJs::PumpCalls(void* context)
{
	auto gs = static_cast<CGSH_OpenGLJs*>(context);
	//Clear before draining so calls posted while draining schedule another pump.
	gs->m_pumpScheduled = false;
	gs->ProcessPendingCalls();
}
