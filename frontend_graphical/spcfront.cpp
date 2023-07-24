#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
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

#include "drive.h"
#include "base64.h"
void PANIC(uint32_t error) { fprintf(stderr, "PANIC(%d)\n", error); abort(); } // heh
#include "base64.c" // eheheh
#include "adler32.h"
#include "adler32.c" // ;-)

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

struct controller_status {
	uint64_t timestamp_us;
	uint32_t status;
};

struct com_file {
	int in_use;
	int fd;
	int sequence;
	size_t bytes_written;
	size_t bytes_total;
	struct adler32 adler;
};

struct com {
	char** status_descriptor_arr;
	struct cond ready_cond;

	int fd;
	char* tty_path;
	char* recv_line_arr;
	pthread_mutex_t queue_mutex;
	char** queue_arr;

	pthread_rwlock_t rwlock;
	char** controller_log;
	struct controller_status* controller_status_arr;
	uint64_t controller_timestamp_us;

	struct com_file file;
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

static int is_payload(char* s, const char* cppp)
{
	size_t ncppp = strlen(cppp);
	return starts_with(s, cppp) && (s[ncppp] == ' ' || s[ncppp] == 0);
}

static char* duplicate_string(char* s) // strdup() is deprecated?
{
	const size_t sz = strlen(s)+1;
	void* p = malloc(sz);
	memcpy(p, s, sz);
	return (char*)p;
}

static void end_com_file(void)
{
	struct com_file* cf = &com.file;
	if (!cf->in_use) return;
	close(cf->fd);
	memset(cf, 0, sizeof *cf);
}

__attribute__((format(printf, 1, 2)))
static void com_printf(const char* fmt, ...)
{
	char buf[1<<16];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	char* msg = (char*)malloc(n+1);
	memcpy(msg, buf, n+1);
	arrput(com.controller_log, msg);
}

static void bad_msg(char* msg)
{
	com_printf("WARNING: garbage message from controller: [%s]", msg);
}

static void com__handle_msg(char* msg)
{
	struct com_file* comfile = &com.file;
	if (starts_with(msg, CPPP_LOG)) {
		printf("(CTRL) %s\n", msg);
		msg = duplicate_string(msg);
		pthread_rwlock_wrlock(&com.rwlock);
		arrput(com.controller_log, msg);
		pthread_rwlock_unlock(&com.rwlock);
	} else if (is_payload(msg, CPPP_STATUS_DESCRIPTORS)) {
		assert((com.status_descriptor_arr == NULL) && "seen twice? that's probably not thread-safe...");
		char* p = msg + strlen(CPPP_STATUS_DESCRIPTORS);
		for (;;) {
			char c = *p;
			if (c == 0) break;
			assert(c == ' ');
			p++;
			char* p0 = p;
			for (;;) {
				char c2 = *(p++);
				if (c2 == ' ' || c2 == 0) {
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
	} else if (is_payload(msg, CPPP_STATUS)) {
		char* p = msg + strlen(CPPP_STATUS);
		uint64_t timestamp_us = 0;
		uint32_t status = 0;
		if (sscanf(p, " %lu %u", &timestamp_us, &status) == 2) {
			struct controller_status s;
			s.timestamp_us = timestamp_us;
			s.status = status;
			pthread_rwlock_wrlock(&com.rwlock);
			arrput(com.controller_status_arr, s);
			if (timestamp_us > com.controller_timestamp_us) {
				com.controller_timestamp_us = timestamp_us;
			}
			pthread_rwlock_unlock(&com.rwlock);
		} else {
			bad_msg(msg);
		}
	} else if (is_payload(msg, CPPP_STATUS_TIME)) {
		char* p = msg + strlen(CPPP_STATUS_TIME);
		uint64_t timestamp_us;
		if (sscanf(p, " %lu", &timestamp_us) == 1) {
			pthread_rwlock_wrlock(&com.rwlock);
			if (timestamp_us > com.controller_timestamp_us) com.controller_timestamp_us = timestamp_us;
			pthread_rwlock_unlock(&com.rwlock);
		} else {
			bad_msg(msg);
		}
	} else if (is_payload(msg, CPPP_DATA_HEADER)) {
		char* p = msg + strlen(CPPP_DATA_HEADER);
		int n_bytes = -1;
		char filename[1<<10];
		if (sscanf(p, " %d %s", &n_bytes, filename) == 2) {
			assert(!comfile->in_use);
			char other_filename[1<<11];
			char* path = filename;
			for (;;) {
				int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
				if (fd == -1) {
					if (errno == EEXIST) {
						snprintf(
							other_filename, sizeof other_filename,
							"%s-resolv%d", filename, rand());
						path = other_filename;
						continue;
					} else {
						fprintf(stderr, "%s: %s\n", path, strerror(errno));
						exit(EXIT_FAILURE);
					}
				}
				memset(comfile, 0, sizeof *comfile);
				comfile->in_use = 1;
				comfile->fd = fd;
				comfile->bytes_total = n_bytes;
				adler32_init(&comfile->adler);
				com_printf("D/L %d bytes [%s]...", n_bytes, path);
				break;
			}
		} else {
			bad_msg(msg);
		}
	} else if (is_payload(msg, CPPP_DATA_LINE)) {
		if (!comfile->in_use) {
			com_printf("ERROR: out of sequence (not-in-use) data line [%s]", msg);
		} else {
			char* p = msg + strlen(CPPP_DATA_LINE);
			int sequence = -1;
			char b64[1<<10];
			if (sscanf(p, " %d %s", &sequence, b64) == 2) {
				if (sequence != comfile->sequence) {
					com_printf("ERROR: out of sequence (expected %d, got %d) data line [%s]", comfile->sequence, sequence, msg);
					end_com_file();
					return;
				} else {
					comfile->sequence++;
					uint8_t buffer[1<<10];
					uint8_t* eb = base64_decode_line(buffer, b64);
					if (eb == NULL) {
						com_printf("ERROR: could not decode data line [%s]", msg);
						end_com_file();
						return;
					} else {
						const size_t n_recv = eb - buffer;
						adler32_push(&comfile->adler, buffer, n_recv);
						comfile->bytes_written += n_recv;
						size_t remaining = n_recv;
						uint8_t* tp = buffer;
						while (remaining > 0) {
							ssize_t nw = write(comfile->fd, tp, remaining);
							if (nw == -1) {
								if (errno == EINTR) {
									continue;
								} else {
									assert(!"write error");
								}
							}
							tp += nw;
							remaining -= nw;
						}
					}
				}
			} else {
				bad_msg(msg);
				end_com_file();
			}
		}
	} else if (is_payload(msg, CPPP_DATA_FOOTER)) {
		char* p = msg + strlen(CPPP_DATA_FOOTER);
		if (!comfile->in_use) {
			com_printf("WARNING: out of sequence (not-in-use) footer [%s]", msg);
		} else {
			int sequence = -1;
			uint32_t pico_checksum = 0;
			if (sscanf(p, " %d %u", &sequence, &pico_checksum) == 2) {
				const uint32_t our_checksum = adler32_sum(&comfile->adler);
				if (comfile->bytes_written != comfile->bytes_total) {
					com_printf("ERROR: expected %zd bytes; only received %zd", comfile->bytes_total, comfile->bytes_written);
					end_com_file();
					return;
				}
				if (our_checksum != pico_checksum) {
					com_printf("ERROR: bad checksum; pico says %u; our calc says %u", pico_checksum, our_checksum);
					end_com_file();
					return;
				}
				if (sequence != comfile->sequence) {
					com_printf("ERROR: out of sequence (expected %d, got %d) footer [%s]", comfile->sequence, sequence, msg);
					end_com_file();
					return;
				}
				close(comfile->fd);
				comfile->in_use = 0;
			} else {
				bad_msg(msg);
				end_com_file();
			}
		}
	} else {
		bad_msg(msg);
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
	com_enqueue("%s 1", CMDSTR_subscribe_to_status);

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

	assert(pthread_rwlock_init(&com.rwlock, NULL) == 0);

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

static ImU32 get_status_color(const char* nm, int st)
{
	//INDEX SECTOR FAULT SEEK_ERROR ON_CYLINDER UNIT_READY ADDRESS_MARK UNIT_SELECTED SEEK_END
	double r,g,b;
	if (strcmp(nm, "FAULT") == 0 || strcmp(nm, "SEEK_ERROR") == 0) {
		r = 0.8;
		g = 0.2;
		b = 0;
	} else if (strcmp(nm, "ON_CYLINDER") == 0 || strcmp(nm, "UNIT_READY") == 0 || strcmp(nm, "UNIT_SELECTED") == 0 || strcmp(nm, "SEEK_END") == 0) {
		r = 0.2;
		g = 0.5;
		b = 0;
	} else {
		r = 0.5;
		g = 0.6;
		b = 1.0;
	}

	double m = 1.0;
	if (st == 0) {
		m = 0.2;
	} else if (st == 1) {
		m = 1.0;
	} else {
		assert(!"UNREACHABLE");
	}
	return ImGui::GetColorU32(ImVec4(r*m, g*m, b*m, 1.0));
}

static void push_danger_style(void)
{
	const float r = 0.8;
	const float g = 0.0;
	const float b = 0.2;
	const float a0 = 0.2;
	const float a1 = 0.5;
	ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(r,g,b,1));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r+a0,g+a0,b+a0,1));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(r+a1,g+a1,b+a1,1));
}

static void pop_danger_style(void)
{
	ImGui::PopStyleColor(3);
}

int main(int argc, char** argv)
{
	if (argc != 2 && argc != 3) {
		fprintf(stderr, "Usage: %s </path/to/tty/for/smd-pico-controller> [font size px]\n", argv[0]);
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

	float status_scope_scale = 7.5;

	bool previous_debug_led = false,     debug_led = false;
	int previous_debug_control_pins = 0, debug_control_pins = 0;
	int raw_tag_selected_index = 3;
	int raw_tag1_cylinder = 0;
	int raw_tag2_head = 0;
	int raw_tag3_flags = 0;
	int basic_selected_index = 0;
	int basic_cylinder = 0;
	bool basic_cylinder_allow_overflow = false;
	int basic_head = 0;
	bool basic_index_sync = true;
	bool basic_skip_checks = false;
	int batch_cylinder0 = 0;
	int batch_cylinder1 = 822;
	int batch_head_set = 31;
	int common_32bit_word_count = ((DRIVE_BYTES_PER_TRACK/4)*31)/10;
	int common_servo_offset = 0;
	int common_data_strobe_delay = 0;
	bool poll_gpio = false;
	uint32_t last_poll_gpio = 0;

	int font_size = 18;
	if (argc == 3) {
		font_size = atoi(argv[2]);
		if (font_size < 1) {
			fprintf(stderr, "invalid font size [%s]\n", argv[2]);
			exit(EXIT_FAILURE);
		}
	}
	ImFont* font = io.Fonts->AddFontFromFileTTF("Inconsolata-Medium.ttf", font_size);
	io.Fonts->Build();

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

		pthread_rwlock_rdlock(&com.rwlock);

		{ // controller status window
			ImGui::Begin("Controller Status");
			const uint64_t now_us = com.controller_timestamp_us;
			ImGui::Text("Uptime: %.1fs", (double)now_us * 1e-6);

			ImGui::SliderFloat("scale", &status_scope_scale, 1.0f, 60.0f, "%.1f seconds");

			const int n_rows = arrlen(com.status_descriptor_arr);
			const int n_columns = 2;
			if (ImGui::BeginTable("table", n_columns)) {
				ImGui::TableSetupColumn("0", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("1", ImGuiTableColumnFlags_WidthFixed);
				const struct controller_status* cs = com.controller_status_arr;
				const int ncs = arrlen(cs);
				unsigned mask = 1;
				for (int row = 0; row < n_rows; row++, mask <<= 1) {
					const char* s = com.status_descriptor_arr[row];
					ImU32 st0col = get_status_color(s, 0);
					ImU32 st1col = get_status_color(s, 1);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);

					if (ncs > 0) {
						ImVec2 area_p0 = ImGui::GetCursorScreenPos();
						ImVec2 area_sz = ImGui::GetContentRegionAvail();
						ImDrawList* draw_list = ImGui::GetWindowDrawList();
						float x_right = area_p0.x+area_sz.x;
						for (int cursor = ncs-1; cursor >= 0 && draw_list->_VtxCurrentIdx < ((1<<16)-64); cursor--) {
							const struct controller_status* st = &cs[cursor];
							const double ts = st->timestamp_us;
							const double ts1 = now_us;
							const double ts0 = ts1 - status_scope_scale * 1e6;
							const double q = (ts-ts0) / (ts1-ts0);
							const float x_left = area_p0.x + area_sz.x*q;
							if (st->status & mask) {
								draw_list->AddRectFilled(
									ImVec2(x_left, area_p0.y),
									ImVec2(x_right, area_p0.y+font_size),
									st1col);
							}
							x_right = x_left;
							if (x_right < area_p0.x) break;
						}
					}

					ImGui::TableSetColumnIndex(1);
					const int is_on = (ncs > 0) && (cs[ncs-1].status & mask);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, is_on ? st1col : st0col);
					ImGui::Text("%s", s);
				}
				ImGui::EndTable();
			}

			ImGui::End();
		}

		{ // controller log window
			ImGui::Begin("Controller Log");

			ImGuiListClipper clipper;
			const int n = arrlen(com.controller_log);
			clipper.Begin(n);
			while (clipper.Step()) {
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
					char* entry = com.controller_log[i];
					ImGui::TextUnformatted(entry);
				}
			}
			clipper.End();
			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
				ImGui::SetScrollHereY(1.0f);
			}
			ImGui::End();
		}

		{ // controller control
			ImGui::Begin("Controller Control");

			if (ImGui::CollapsingHeader("Batch Read")) {
				if (ImGui::InputInt("First cylinder", &batch_cylinder0)) {
					if (batch_cylinder0 < 0) batch_cylinder0 = 0;
					if (batch_cylinder0 >= DRIVE_CYLINDER_COUNT) batch_cylinder0 = DRIVE_CYLINDER_COUNT-1;
					if (batch_cylinder0 > batch_cylinder1) batch_cylinder1 = batch_cylinder0;
				}
				if (ImGui::InputInt("Last cylinder (inclusive)", &batch_cylinder1)) {
					if (batch_cylinder1 < 0) batch_cylinder1 = 0;
					if (batch_cylinder1 >= DRIVE_CYLINDER_COUNT) batch_cylinder1 = DRIVE_CYLINDER_COUNT-1;
					if (batch_cylinder1 < batch_cylinder0) batch_cylinder0 = batch_cylinder1;
				}
				ImGui::CheckboxFlags("Head 0", &batch_head_set, 1<<0);
				ImGui::SameLine();
				ImGui::CheckboxFlags("Head 1", &batch_head_set, 1<<1);
				ImGui::SameLine();
				ImGui::CheckboxFlags("Head 2", &batch_head_set, 1<<2);
				ImGui::SameLine();
				ImGui::CheckboxFlags("Head 3", &batch_head_set, 1<<3);
				ImGui::SameLine();
				ImGui::CheckboxFlags("Head 4", &batch_head_set, 1<<4);
				ImGui::InputInt("32bit Word Count", &common_32bit_word_count);
				if (common_32bit_word_count < 0) common_32bit_word_count = 0;
				if (ImGui::Button("Execute!")) {
					com_enqueue("op_read_batch %d %d %d %d %d %d",
						batch_cylinder0,
						batch_cylinder1,
						batch_head_set,
						common_32bit_word_count,
						common_servo_offset,
						common_data_strobe_delay);
				}
			}

			if (ImGui::CollapsingHeader("Basic Operation")) {
				const char* items[] = {
					"Select Unit 0",
					"Select Cylinder",
					"Select Head",
					"Read Enable",
					"Read Data",
				};
				ImGui::Combo("##selected_basic", &basic_selected_index, items, IM_ARRAYSIZE(items));
				switch (basic_selected_index) {
				case 0: {
					// nothing
				} break;
				case 1: {
					ImGui::InputInt("Cylinder##bo", &basic_cylinder);
					ImGui::Checkbox("Allow overflow (full 10-bit range)", &basic_cylinder_allow_overflow);
					if (basic_cylinder < 0) basic_cylinder = 0;
					const int max_cylinder = (basic_cylinder_allow_overflow ? (1<<10) : DRIVE_CYLINDER_COUNT) - 1;
					if (basic_cylinder > max_cylinder) basic_cylinder = max_cylinder;
				} break;
				case 2: {
					ImGui::InputInt("Head##bo", &basic_head);
					if (basic_head < 0) basic_head = 0;
					if (basic_head >= DRIVE_HEAD_COUNT) basic_head = DRIVE_HEAD_COUNT-1;
				} break;
				case 3: {
					// nothing
				} break;
				case 4: {
					ImGui::InputInt("32bit Word Count", &common_32bit_word_count);
					if (common_32bit_word_count < 0) common_32bit_word_count = 0;
					ImGui::Checkbox("Index Sync", &basic_index_sync);
					ImGui::SetItemTooltip("Waits until INDEX signal from drive before reading");
					ImGui::Checkbox("Skip Checks", &basic_skip_checks);
					ImGui::SetItemTooltip("Skip error/readyness checking; read regardless of status");
				} break;
				}

				if (ImGui::Button("Execute!")) {
					switch (basic_selected_index) {
					case 0: {
						com_enqueue("op_select_unit0");
					} break;
					case 1: {
						com_enqueue("op_select_cylinder %d", basic_cylinder);
					} break;
					case 2: {
						com_enqueue("op_select_head %d", basic_head);
					} break;
					case 3: {
						com_enqueue("op_read_enable %d %d", common_servo_offset, common_data_strobe_delay);
					} break;
					case 4: {
						com_enqueue("op_read_data %d %d %d",
							common_32bit_word_count,
							basic_index_sync?1:0,
							basic_skip_checks?1:0);
					} break;
					}
				}
			}

			if (ImGui::CollapsingHeader("Raw Tag")) {
				const char* items[] = {
					// same order as `enum tag`
					"TAG_UNIT_SELECT",
					"TAG1 (Cylinder)",
					"TAG2 (Head)",
					"TAG3 (Control)",
				};
				ImGui::Combo("##selected_raw_tag", &raw_tag_selected_index, items, IM_ARRAYSIZE(items));
				const int MAX_ADDR = (1<<10)-1;
				switch (raw_tag_selected_index) {
				case TAG_UNIT_SELECT: {
					ImGui::Text("(always selects unit 0)");
				} break;
				case TAG1: {
					ImGui::InputInt("Cylinder##tag", &raw_tag1_cylinder);
					if (raw_tag1_cylinder < 0) raw_tag1_cylinder = 0;
					if (raw_tag1_cylinder > MAX_ADDR) raw_tag1_cylinder = MAX_ADDR;
				} break;
				case TAG2: {
					ImGui::InputInt("Head##tag", &raw_tag2_head);
					if (raw_tag2_head < 0) raw_tag2_head = 0;
					if (raw_tag2_head > MAX_ADDR) raw_tag2_head = MAX_ADDR;
				} break;
				case TAG3: {
					#define BIT(NAME,DESC) \
						ImGui::CheckboxFlags(#NAME, &raw_tag3_flags, TAG3BIT_ ## NAME); \
						ImGui::SetItemTooltip("%s", DESC);
					EMIT_TAG3_BITS
					#undef CONTROL
				} break;
				}

				if (ImGui::Button("Execute!")) {
					com_enqueue("op_raw_tag %d %d",
						raw_tag_selected_index,
						raw_tag_selected_index == TAG_UNIT_SELECT ? 0 :
						raw_tag_selected_index == TAG1 ? raw_tag1_cylinder :
						raw_tag_selected_index == TAG2 ? raw_tag2_head :
						raw_tag_selected_index == TAG3 ? raw_tag3_flags :
						0);
				}
			}

			if (ImGui::CollapsingHeader("Control Pins Manual ON/OFF")) {
				{
					int mask = 1;
					#define CONTROL(NAME,SUPPORTED) \
						if (SUPPORTED) { \
							ImGui::CheckboxFlags(#NAME, &debug_control_pins, mask); \
						} \
						mask <<= 1;
					EMIT_CONTROLS
					#undef CONTROL
				}
				ImGui::Checkbox("*LED", &debug_led);
				if (ImGui::Button("Set all")) {
					int mask = 1;
					#define CONTROL(NAME,SUPPORTED) \
						if (SUPPORTED) debug_control_pins |= mask; \
						mask <<= 1;
					EMIT_CONTROLS
					#undef CONTROL
					debug_led = 1;
				}
				if (ImGui::Button("Clear all")) {
					debug_control_pins = 0;
					debug_led = 0;
				}
			}

			if (ImGui::CollapsingHeader("Misc Debugging")) {
				if (ImGui::Button("Execute Blink Test Job (Succeed)")) {
					com_enqueue("op_blink_test 0");
				}
				ImGui::SameLine();
				if (ImGui::Button("(Fail)")) {
					com_enqueue("op_blink_test 1");
				}
				ImGui::Checkbox("Poll all GPIO (see log output)", &poll_gpio);
				if (ImGui::Button("Execute Data Download Test (1000b)")) {
					com_enqueue("xfer_test 1000");
				}
				ImGui::SameLine();
				if (ImGui::Button("(10000b)")) {
					com_enqueue("xfer_test 10000");
				}
			}

			if (ImGui::CollapsingHeader("Fire Extinguishers")) {
				{
					push_danger_style();
					if (ImGui::Button("TERMINATE OPERATION")) {
						com_enqueue("terminate_op");
					}
					ImGui::SetItemTooltip("Terminates current drive operation on Pico and sets all control pins to zero");
					pop_danger_style();
				}

				if (ImGui::Button("RTZ")) {
					com_enqueue("op_rtz");
				}
				ImGui::SetItemTooltip("Return to cylinder zero, clear fault");

				if (ImGui::Button("Clear FAULT")) {
					com_enqueue("op_raw_tag 3 %d", TAG3BIT_FAULT_CLEAR);
				}

				if (ImGui::Button("Clear Control (TAG3 with all zeroes)")) {
					com_enqueue("op_raw_tag 3 0");
				}
			}

			if (ImGui::CollapsingHeader("TAG3/READ_GATE modifiers")) {
				ImGui::TextWrapped("Normal read operations begin by strobing TAG3 with READ_GATE=1; the following translates into modifier bits (see \"Raw Tag\") during the strobe:");
				ImGui::SeparatorText("Servo Offset");
				ImGui::InputInt("##servo_offset", &common_servo_offset);
				ImGui::TextWrapped("\"Offsets the actuator from the nominal on cylinder position toward/away from the spindle.\". +1 is towards (SERVO_OFFSET_POSITIVE), -1 is away (SERVO_OFFSET_NEGATIVE)");
				if (common_servo_offset < -1) common_servo_offset = -1;
				if (common_servo_offset > 1) common_servo_offset = 1;

				ImGui::SeparatorText("Data Strobe Delay");
				ImGui::InputInt("##data_strobe_delay", &common_data_strobe_delay);
				ImGui::TextWrapped("\"Enables the PLO data separator to strobe the data at a time earlier/later than optimum.\". -1 is earlier (DATA_STROBE_EARLY), 1 is later (DATA_STROBE_LATE)");
				if (common_data_strobe_delay < -1) common_data_strobe_delay = -1;
				if (common_data_strobe_delay > 1) common_data_strobe_delay = 1;
			}

			ImGui::End();
		}

		if (debug_control_pins != previous_debug_control_pins) {
			com_enqueue("set_ctrl %d", debug_control_pins);
			previous_debug_control_pins = debug_control_pins;
		}

		if (debug_led != previous_debug_led) {
			com_enqueue("led %d", debug_led?1:0);
			previous_debug_led = debug_led;
		}

		if (poll_gpio) {
			uint32_t t = SDL_GetTicks();
			if (t > (last_poll_gpio + 100)) {
				com_enqueue("poll_gpio");
				last_poll_gpio = t;
			}
		}

		pthread_rwlock_unlock(&com.rwlock);

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
