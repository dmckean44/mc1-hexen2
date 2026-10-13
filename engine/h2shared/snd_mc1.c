/* snd_mc1.c -- sound output for MC1: the core has no audio device yet, so the mixer writes
   into a memory ring buffer whose play position follows the cycle counter (for measuring
   what mixing costs). Build with -DMC1_SOUND=ON. */

#include "quakedef.h"
#include "snd_sys.h"

static unsigned char	dma_buffer[16384];

static qboolean S_MC1_Init (dma_t *dma)
{
	memset (dma, 0, sizeof(*dma));
	shm = dma;
	shm->samplebits = 16;
	shm->speed = 11025;
	shm->channels = 2;
	shm->samples = sizeof(dma_buffer) / (shm->samplebits / 8);
	shm->samplepos = 0;
	shm->submission_chunk = 1;
	shm->buffer = dma_buffer;
	return true;
}

static int S_MC1_GetDMAPos (void)
{
	const int frames = (int)(Sys_DoubleTime () * shm->speed);

	shm->samplepos = (frames * shm->channels) & (shm->samples - 1);
	return shm->samplepos;
}

static void S_MC1_void (void)
{
}

snd_driver_t snddrv_mc1 =
{
	S_MC1_Init,
	S_MC1_void,
	S_MC1_GetDMAPos,
	S_MC1_void,
	S_MC1_void,
	S_MC1_void,
	S_MC1_void,
	"MC1",
	SNDDRV_ID_NULL,
	false,
	NULL
};
