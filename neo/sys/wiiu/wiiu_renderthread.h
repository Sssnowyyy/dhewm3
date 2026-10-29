// The Wii U runs the renderer's back end on its own thread and CPU core, while
// the game and the renderer's front end work on the next frame on another core.
// See gx2_glimp.cpp.

#ifndef __WIIU_RENDERTHREAD_H__
#define __WIIU_RENDERTHREAD_H__

// runs the back end commands on the render thread; async returns right away,
// otherwise it waits until they're done
void	WiiU_ExecuteBackEnd( const void *cmds, bool async );

// waits until the render thread is done with what it's doing, and moves the
// calling thread to the core that does the GPU work (done before anything that
// sends commands to the GPU or frees what the back end may still use)
void	WiiU_SyncRenderThread( void );
void	WiiU_SyncRenderThreadFor( const char *reason );	// same, reason is shown in the perf log

// replaces WHBProcIsRunning() for the engine thread's main loop
bool	WiiU_ProcIsRunning( void );

#endif
