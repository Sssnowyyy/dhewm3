// Wii U: totals of some expensive operations, to break down where level loads
// spend their time (printed at the end of idSessionLocal::ExecuteMapChange())

#ifndef __WIIU_LOADPROFILE_H__
#define __WIIU_LOADPROFILE_H__

struct wiiuLoadProfile_t {
	double	driveReadMs;		// raw reads from the SD card / USB drive (in the .pk4 code)
	int		driveReads;
	double	driveReadBytes;
	int		pk4Opens;			// files opened in .pk4s
	int		skippedLookups;		// looking for a file in base/ skipped as its directory doesn't exist
	double	pk4ReadMs;			// reads from .pk4 files incl. decompression
	double	soundMs;			// loading sound samples
	double	soundDecodeMs;		// decoding .ogg sounds to PCM at load time (part of soundMs)
	double	modelMs;			// loading models
	double	imageMs;			// loading images
	int		decodedSounds;
};

extern wiiuLoadProfile_t wiiuLoadProfile;

// see wiiu_main.cpp
bool	Wiiu_FileMayExist( const char *path );

// memory use, from malloc()'s point of view (the GPU's memory comes from there too)
int		Wiiu_MemoryUsedMB( void );
void	Wiiu_PrintMemory( const char *when );

// adds the time until it goes out of scope to a total
class idWiiULoadTimer {
public:
	idWiiULoadTimer( double &total ) : total( total ), start( Sys_MillisecondsPrecise() ) {}
	~idWiiULoadTimer() { total += Sys_MillisecondsPrecise() - start; }
private:
	double &	total;
	double		start;
};

#endif
