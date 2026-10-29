// Wii U: totals of some expensive operations, to break down where level loads
// spend their time (printed at the end of idSessionLocal::ExecuteMapChange())

#ifndef __WIIU_LOADPROFILE_H__
#define __WIIU_LOADPROFILE_H__

#ifdef __cplusplus

struct wiiuLoadProfile_t {
	double	driveReadMs;		// raw reads from the SD card / USB drive (in the .pk4 code)
	double	pk4ReadMs;			// reads from .pk4 files incl. decompression
	double	soundMs;			// loading sound samples
	double	soundDecodeMs;		// decoding .ogg sounds to PCM at load time (part of soundMs)
	double	modelMs;			// loading models
	double	imageMs;			// loading images
	int		decodedSounds;
};

extern wiiuLoadProfile_t wiiuLoadProfile;

// adds the time until it goes out of scope to a total
class idWiiULoadTimer {
public:
	idWiiULoadTimer( double &total ) : total( total ), start( Sys_MillisecondsPrecise() ) {}
	~idWiiULoadTimer() { total += Sys_MillisecondsPrecise() - start; }
private:
	double &	total;
	double		start;
};

extern "C" {
#endif

// for the (C) .pk4 code
double	Wiiu_Milliseconds( void );
void	Wiiu_AddDriveReadTime( double ms );

#ifdef __cplusplus
}
#endif

#endif
