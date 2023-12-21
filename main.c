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
#include <semaphore.h>
#include "fft/fft.h"

// Various magic numbers
#define NUM_SAMPLES 2048
#define Y_SCALE 8
#define SLEEP_TIME_MICROS 10000

typedef struct Terminal {
	int rows;
	int cols;
	char** buf;
} Terminal;

Terminal TerminalCreate();
void TerminalDestroy(Terminal t);
void TerminalClear(Terminal t);
void TerminalWrite(Terminal t, int x, int y, char c);
void TerminalPrint(Terminal t);
void StreamProcessor(void* data, unsigned int frameCount);

static int numChannels = 2;
static sem_t sampleBufferMutex = { 0 };
static float sampleBuffer[NUM_SAMPLES] = { 0 };
static int sampleIndex = 0;

int main(int argc, char** argv) {
	if (argc < 2) {
		TraceLog(LOG_INFO, "No file name provided.\n");
		return 0;
	}

	Terminal t = TerminalCreate();

	sem_init(&sampleBufferMutex, 0, 1);

	// Setup audio playback
	InitAudioDevice();
	if (!IsAudioDeviceReady()) {
		return 1;
	}
	Music audio = LoadMusicStream(argv[1]);
	audio.looping = false;
	if (audio.stream.buffer == NULL) {
		return 1;
	}
	numChannels = audio.stream.channels;
	AttachAudioStreamProcessor(audio.stream, StreamProcessor);
	PlayMusicStream(audio);

	// Various buffers we will need later
	float sig_td[NUM_SAMPLES] = { 0 }; // Time domain signal copied from audio
	float complex sig_fd[NUM_SAMPLES] = { 0 }; // Frequency domain signal
	float sig_abs[NUM_SAMPLES] = { 0 }; // Magnitude of frequency domain signal
	float sig_old[NUM_SAMPLES] = { 0 }; // Previous magnitude values

	// Exit when user presses enter or end of audio stream is reached
	char key[1] = { 0 };
	fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
	while (read(STDIN_FILENO, key, 1) <= 0 && IsMusicStreamPlaying(audio)) {
		// Update various buffer 
		UpdateMusicStream(audio);
		sem_wait(&sampleBufferMutex);
		memcpy(sig_td, sampleBuffer, sizeof(float) * NUM_SAMPLES);
		sem_post(&sampleBufferMutex);

		// Compute freq values
		fft(sig_td, sig_fd, NUM_SAMPLES);
		// Calculate magnitude at each frequency
		// Apply low pass filter to magnitude of each frequency
		for (int i = 0; i < NUM_SAMPLES; i++) {
			sig_abs[i] = cabsf(sig_fd[i]);
			sig_abs[i] = 0.1f * sig_abs[i] + 0.9f * sig_old[i];
			sig_old[i] = sig_abs[i];
		}

		// Display freq information
		TerminalClear(t);
		for (int i = 0; i < t.cols; i++) {
			// Select frequency bin nearest to each column
			int idx = round(NUM_SAMPLES * (float) i / (float) t.cols);
			float mag = fmax(sig_abs[idx], 1.f);
			float logmag = logf(mag);
			int max = t.rows - (logmag * Y_SCALE);
			for (int y = t.rows - 1; y > max; y--) {
				// Rotate freq response so 0Hz is in the center
				int x = (i+t.cols/2)%t.cols;
				// Select a random letter, number or symbol
				char c = rand()%(122-33)+33;
				TerminalWrite(t, x, y, c);
			}
		}
		TerminalPrint(t);

		usleep(SLEEP_TIME_MICROS);
	}

	StopMusicStream(audio);
	UnloadMusicStream(audio);
	CloseAudioDevice();
	sem_destroy(&sampleBufferMutex);
	TerminalDestroy(t);
	return 0;
}

Terminal TerminalCreate() {
	Terminal t;
	struct winsize ws = { 0 };
	ioctl(0, TIOCGWINSZ, &ws);
	t.cols = ws.ws_col;
	t.rows = ws.ws_row;
	t.buf = malloc(sizeof(char*) * t.rows);
	for (int i = 0; i < t.rows; i++) {
		t.buf[i] = malloc(t.cols+1);
	}
	TerminalClear(t);
	return t;
}

void TerminalDestroy(Terminal t) {
	for (int i = 0; i < t.rows; i++) {
		free(t.buf[i]);
	}
	free(t.buf);
}

void TerminalClear(Terminal t) {
	for (int i = 0; i < t.rows; i++) {
		memset(t.buf[i], ' ', t.cols);
		t.buf[i][t.cols] = 0;
	}
}

void TerminalWrite(Terminal t, int x, int y, char c) {
	if (x < 0 || x >= t.cols || y < 0 || y >= t.rows);
	else t.buf[y][x] = c;
}

void TerminalPrint(Terminal t) {
	// Clear screen and move cursor to top left
	printf("\e[1;1H\e[2J");
	for (int i = 0; i < t.rows; i++) {
		fputs(t.buf[i], stdout);
		// Don't insert newline after last line
		if (i < t.rows-1) {
			fputc('\n', stdout);
		}
	}
	// Move cursor to bottom left
	fputc('\r', stdout);
	fflush(stdout);
}

void StreamProcessor(void* data, unsigned int frameCount) {
	sem_wait(&sampleBufferMutex);
	float* sourceBuffer = (float*) data;
	for (int i = 0; i < frameCount; i++) {
		sampleBuffer[sampleIndex] = sourceBuffer[i*numChannels];
		sampleIndex = (sampleIndex + 1) % NUM_SAMPLES;
	}
	sem_post(&sampleBufferMutex);
}

