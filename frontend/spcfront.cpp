#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <pthread.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl2.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

int tty_fd = -1;

struct com {
	char* recv_line_arr;
	pthread_mutex_t queue_mutex;
	char** queue_arr;
} com;

static void com_recv_char(char ch)
{
	if (ch == '\r' || ch == '\n') {
		if (arrlen(com.recv_line_arr) > 0) {
			arrput(com.recv_line_arr, 0);
			printf("got [%s] from tty\n", com.recv_line_arr);
			arrsetlen(com.recv_line_arr, 0);
		}
	} else {
		arrput(com.recv_line_arr, ch);
	}
}

__attribute__((format(printf, 1, 2)))
static void com_enqueue(const char* fmt, ...)
{
	char buf[1<<16];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	char* s = (char*)malloc(n+1);
	memcpy(s, buf, n+1);
	pthread_mutex_lock(&com.queue_mutex);
	arrput(com.queue_arr, s);
	pthread_mutex_unlock(&com.queue_mutex);
}

// caller should free return value when done with it
static char* com_shift(void)
{
	char* c = NULL;
	pthread_mutex_lock(&com.queue_mutex);
	if (arrlen(com.queue_arr) > 0) {
		c = com.queue_arr[0];
		arrdel(com.queue_arr, 0);
	}
	pthread_mutex_unlock(&com.queue_mutex);
	return c;
}

static int com_has_pending_writes(void)
{
	pthread_mutex_lock(&com.queue_mutex);
	int p = arrlen(com.queue_arr) > 0;
	pthread_mutex_unlock(&com.queue_mutex);
	return p;
}

void* io_thread_start(void* arg)
{
	assert(tty_fd >= 0);

	struct timeval timeout = {0};

	for (;;) {
		fd_set rfds, wfds;

		FD_ZERO(&rfds);
		FD_SET(tty_fd, &rfds);

		FD_ZERO(&wfds);
		if (com_has_pending_writes()) {
			FD_SET(tty_fd, &wfds);
		}

		memset(&timeout, 0, sizeof timeout);
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000000/30;
		int r = select(tty_fd+1, &rfds, &wfds, NULL, &timeout);
		if (r == -1) {
			fprintf(stderr, "select(): %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		} else if (r == 0) {
			continue;
		}

		assert(r > 0);

		if (FD_ISSET(tty_fd, &rfds)) {
			char buf[1<<16];
			int n = read(tty_fd, buf, sizeof buf);
			if (n == -1) {
				fprintf(stderr, "tty: %s\n", strerror(errno));
				exit(EXIT_FAILURE);
			}
			for (int i = 0; i < n; i++) com_recv_char(buf[i]);
		}

		if (FD_ISSET(tty_fd, &wfds)) {
			char* c = com_shift();
			assert((c != NULL) && "expected to shift command");
			size_t n = strlen(c);
			assert(write(tty_fd, c, n) != -1);
			free(c);
			assert(write(tty_fd, "\r\n", 2) != -1);
		}
	}
	return NULL;
}

__attribute__ ((noreturn))
static void SDL2FATAL(void)
{
	fprintf(stderr, "SDL2: %s\n", SDL_GetError());
	exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s </path/to/tty/for/smd-pico-controller>\n", argv[0]);
		fprintf(stderr, "Try `/dev/ttyACM0`, or run `dmesg` or `ls -ltr /dev/` to see/guess what tty is assigned to the device\n");
		exit(EXIT_FAILURE);
	}

	const char* tty_path = argv[1];

	{
		tty_fd = open(tty_path, O_RDWR | O_NOCTTY);
		if (tty_fd == -1) {
			fprintf(stderr, "%s: %s\n", tty_path, strerror(errno));
			exit(EXIT_FAILURE);
		}

		if (!isatty(tty_fd)) {
			fprintf(stderr, "%s: not a tty\n", tty_path);
			assert(close(tty_fd) == 0);
			exit(EXIT_FAILURE);
		}

		if (flock(tty_fd, LOCK_EX | LOCK_NB) == -1) {
			if (errno == EWOULDBLOCK) {
				fprintf(stderr, "%s: already locked by another process\n", tty_path);
			} else {
				fprintf(stderr, "%s: %s\n", tty_path, strerror(errno));
			}
			exit(EXIT_FAILURE);
		}

		{
			struct termios t;
			tcgetattr(tty_fd, &t);
			t.c_lflag &= ~(ICANON | ECHO);
			tcsetattr(tty_fd, TCSANOW, &t);
		}
	}

	// TODO do handshake before starting thread?
	pthread_mutex_init(&com.queue_mutex, NULL);
	pthread_t io_thread;
	assert(pthread_create(&io_thread, NULL, io_thread_start, NULL) == 0);

	if (SDL_Init(SDL_INIT_VIDEO) != 0) SDL2FATAL();

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE,     8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,   8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,    8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   0);

	SDL_Window* window = SDL_CreateWindow(
		"spcfront",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		1920, 1080,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

	SDL_GLContext glctx = SDL_GL_CreateContext(window);

	SDL_GL_MakeCurrent(window, glctx);
	SDL_GL_SetSwapInterval(1);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	ImGui::StyleColorsDark();

	ImGui_ImplSDL2_InitForOpenGL(window, glctx);
	ImGui_ImplOpenGL2_Init();

	int XXXLED = 0;
	int exiting = 0;
	while (!exiting) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if ((ev.type == SDL_QUIT) || (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE)) {
				exiting = 1;
			} else {
				ImGui_ImplSDL2_ProcessEvent(&ev);
			}
		}

		ImGui_ImplOpenGL2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		ImGui::Begin("Hello, world!");
		ImGui::Text("This is some useful text.");
		if (ImGui::Button("Toggle LED")) {
			XXXLED = !XXXLED;
			com_enqueue("led %d", XXXLED);
		}
		ImGui::End();

		ImGui::Render();
		glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
		glClearColor(0, 0, 0.2, 0);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(window);
	}

	// TODO signal thread to quit
	//assert(pthread_join(io_thread, NULL) == 0);

	SDL_GL_DeleteContext(glctx);
	SDL_DestroyWindow(window);

	if (flock(tty_fd, LOCK_UN) == -1) {
		fprintf(stderr, "%s: %s\n", tty_path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	assert(close(tty_fd) == 0); // XXX hangs... is it because the other end has to "ACK" the close? maybe?

	return EXIT_SUCCESS;
}
