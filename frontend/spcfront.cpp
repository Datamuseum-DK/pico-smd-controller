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

#include "controller_protocol.h"
#define COMMAND(NAME,ARGFMT) const char* CMDSTR_##NAME = #NAME;
EMIT_COMMANDS
#undef COMMAND

struct cond {
	int value;
	pthread_cond_t pt_cond;
	pthread_mutex_t pt_mutex;
};

static void cond_init(struct cond* cond)
{
	memset(cond, 0, sizeof *cond);
	assert(pthread_cond_init(&cond->pt_cond, NULL) == 0);
	assert(pthread_mutex_init(&cond->pt_mutex, NULL) == 0);
}

static void cond_wait_nonzero(struct cond* cond)
{
	pthread_mutex_lock(&cond->pt_mutex);
	while (cond->value == 0) pthread_cond_wait(&cond->pt_cond, &cond->pt_mutex);
	pthread_mutex_unlock(&cond->pt_mutex);
}

static void cond_signal_value(struct cond* cond, int new_value)
{
	pthread_mutex_lock(&cond->pt_mutex);
	cond->value = new_value;
	pthread_cond_signal(&cond->pt_cond);
	pthread_mutex_unlock(&cond->pt_mutex);
}

static void cond_signal(struct cond* cond)
{
	cond_signal_value(cond, 1);
}

struct com {
	int fd;
	char* tty_path;
	char* recv_line_arr;
	pthread_mutex_t queue_mutex;
	char** queue_arr;
	char** ctrlog;
	char** status_descriptor_arr;
	struct cond ready_cond;
} com;

static int starts_with(char* s, const char* prefix)
{
	const int prefix_length = strlen(prefix);
	if (prefix_length > strlen(s)) return 0;
	for (int i = 0; i < prefix_length; i++) {
		if (s[i] != prefix[i]) return 0;
	}
	return 1;
}

static char* duplicate_string(char* s) // strdup() is deprecated?
{
	const size_t sz = strlen(s)+1;
	void* p = malloc(sz);
	memcpy(p, s, sz);
	return (char*)p;
}

static void com__handle_msg(char* msg)
{
	if (starts_with(msg, CTPP_LOG)) {
		printf("(CTRLOG) %s\n", msg);
		msg = duplicate_string(msg);
		arrput(com.ctrlog, msg);
	} else if (starts_with(msg, CTPP_STATUS_DESCRIPTORS)) {
		assert((com.status_descriptor_arr == NULL) && "seen twice? that's probably not thread-safe...");
		char* p = msg + strlen(CTPP_STATUS_DESCRIPTORS);
		for (;;) {
			char c = *p;
			if (c == 0) break;
			assert(c == ':');
			p++;
			char* p0 = p;
			for (;;) {
				char c2 = *(p++);
				if (c2 == ':' || c2 == 0) {
					p--;
					break;
				}
			}
			if (p > p0) {
				const size_t n = (p-p0)+1;
				char* ss = (char*)malloc(n);
				memcpy(ss, p0, n-1);
				ss[n-1] = 0;
				arrput(com.status_descriptor_arr, ss);
			}
			cond_signal(&com.ready_cond);
		}
	} else {
		printf("WARNING: garbage message from controller: [%s]\n", msg);
	}
}

static void com_recv_char(char ch)
{
	if (ch == '\r' || ch == '\n') {
		if (arrlen(com.recv_line_arr) > 0) {
			arrput(com.recv_line_arr, 0);
			com__handle_msg(com.recv_line_arr);
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
	assert(com.fd >= 0);

	struct timeval timeout = {0};

	com_enqueue("%s", CMDSTR_get_status_descriptors);

	for (;;) {
		fd_set rfds, wfds;

		FD_ZERO(&rfds);
		FD_SET(com.fd, &rfds);

		FD_ZERO(&wfds);
		if (com_has_pending_writes()) {
			FD_SET(com.fd, &wfds);
		}

		memset(&timeout, 0, sizeof timeout);
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000000/30;
		int r = select(com.fd+1, &rfds, &wfds, NULL, &timeout);
		if (r == -1) {
			fprintf(stderr, "select(): %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		} else if (r == 0) {
			continue;
		}

		assert(r > 0);

		if (FD_ISSET(com.fd, &rfds)) {
			char buf[1<<16];
			int n = read(com.fd, buf, sizeof buf);
			if (n == -1) {
				fprintf(stderr, "tty: %s\n", strerror(errno));
				exit(EXIT_FAILURE);
			}
			for (int i = 0; i < n; i++) com_recv_char(buf[i]);
		}

		if (FD_ISSET(com.fd, &wfds)) {
			char* c = com_shift();
			assert((c != NULL) && "expected to shift command");
			size_t n = strlen(c);
			assert(write(com.fd, c, n) != -1);
			free(c);
			assert(write(com.fd, "\r\n", 2) != -1);
		}
	}
	return NULL;
}

static void com_startup(char* tty_path)
{
	com.tty_path = tty_path;
	com.fd = open(com.tty_path, O_RDWR | O_NOCTTY);
	if (com.fd == -1) {
		fprintf(stderr, "%s: %s\n", com.tty_path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!isatty(com.fd)) {
		fprintf(stderr, "%s: not a tty\n", com.tty_path);
		assert(close(com.fd) == 0);
		exit(EXIT_FAILURE);
	}

	if (flock(com.fd, LOCK_EX | LOCK_NB) == -1) {
		if (errno == EWOULDBLOCK) {
			fprintf(stderr, "%s: already locked by another process\n", com.tty_path);
		} else {
			fprintf(stderr, "%s: %s\n", com.tty_path, strerror(errno));
		}
		exit(EXIT_FAILURE);
	}

	{
		struct termios t;
		tcgetattr(com.fd, &t);
		t.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(com.fd, TCSANOW, &t);
	}

	cond_init(&com.ready_cond);

	pthread_mutex_init(&com.queue_mutex, NULL);
	pthread_t io_thread;
	assert(pthread_create(&io_thread, NULL, io_thread_start, NULL) == 0);

	printf("COM: waiting for handshake...\n");
	cond_wait_nonzero(&com.ready_cond);
	printf("COM: ready!\n");
}

static void com_shutdown(void)
{
	if (flock(com.fd, LOCK_UN) == -1) {
		fprintf(stderr, "%s: %s\n", com.tty_path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	assert(close(com.fd) == 0); // XXX hangs... is it because the other end has to "ACK" the close? maybe?
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

	com_startup(argv[1]);

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
		if (ImGui::Button("STAT")) {
			com_enqueue("stat");
		}

		for (int i = 0; i < arrlen(com.status_descriptor_arr); i++) {
			const char* s = com.status_descriptor_arr[i];
			ImGui::BulletText("%s", s);
		}

		ImGui::End();

		ImGui::Render();
		glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(window);
	}

	// TODO signal thread to quit
	//assert(pthread_join(io_thread, NULL) == 0);

	SDL_GL_DeleteContext(glctx);
	SDL_DestroyWindow(window);

	com_shutdown();

	return EXIT_SUCCESS;
}
