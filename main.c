// 
// TUI Music Player / Spectrum Analyzer
//
// Copyright (c) 2023, Jonathan Tainer. Subject to the BSD 2-Clause License.
//

#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <raylib.h>
#include <math.h>
#include "fft/fft.h"

static const int sampleDepth = 32;
static int numChannels = 2;
static int numSamples = 64;
static float* sampleBuffer;
static int sampleIndex = 0;
static char frameBuffer[32][64+1] = { 0 };

void StreamProcessor(void* data, unsigned int frameCount) {
	float* sourceBuffer = (float*) data;
	for (int i = 0; i < frameCount; i++) {
		sampleBuffer[sampleIndex] = sourceBuffer[i*numChannels];
		sampleIndex = (sampleIndex + 1) % numSamples;
	}
}

void DrawChar(int x, int y, char c) {
	frameBuffer[y][x] = c;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		TraceLog(LOG_INFO, "No file name provided.\n");
		return 0;
	}

	struct winsize ws = { 0 };
	ioctl(0, TIOCGWINSZ, &ws);
//	numSamples = ws.ws_col;
	sampleBuffer = (float*)malloc(sizeof(float) * numSamples);

	InitAudioDevice();
	if (!IsAudioDeviceReady()) {
		return 1;
	}

	Music audio = LoadMusicStream(argv[1]);
	if (audio.stream.buffer == NULL) {
		return 1;
	}
//	numChannels = audio.stream.channels;

	AttachAudioStreamProcessor(audio.stream, StreamProcessor);
	PlayMusicStream(audio);

	// Main loop
	char key[1] = { 0 };
	fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
	while (read(STDIN_FILENO, key, 1) <= 0) {
		UpdateMusicStream(audio);
		
		// Clear frame buffer
		for (int i = 0; i < sampleDepth; i++) {
			memset(frameBuffer[i], ' ', numSamples);
			frameBuffer[i][numSamples] = 0;
		}

		// Plot points in frame buffer
		float sig_td[numSamples];
		float complex sig_fd[numSamples];
		memcpy(sig_td, sampleBuffer, sizeof(float) * numSamples);
		fft(sig_td, sig_fd, numSamples);
		for (int x = 0; x < numSamples; x++) {
			int y = (int) cabs(sig_fd[x]);
			if (y < 0) y = 0;
			if (y > sampleDepth-1) y = sampleDepth-1;
			DrawChar(x,y,'*');	
		}

		for (int i = 0; i < sampleDepth; i++) {
			DrawChar(numSamples,i,0);
		}
		
		// Print frame buffer
		printf("\e[1;1H\e[2J");
		for (int i = 0; i < sampleDepth; i++) {
			printf("%s\n", frameBuffer[i]);
		}

		usleep(10000);
	}

	StopMusicStream(audio);
	UnloadMusicStream(audio);
	CloseAudioDevice();
	free(sampleBuffer);
	return 0;
}
