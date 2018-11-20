#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "targetver.h"
#include <windows.h>
#include "resource.h"

#include <stdint.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_GIF
#include "stb_image.h"

#define MAX_LOADSTRING 100

#ifdef _DEBUG
#define ENABLE_LOGGING
#endif

// Global Variables:
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow, int clickable, HWND& hwnd);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

#ifdef ENABLE_LOGGING
namespace logging
{
	void log(const char* format, va_list arg_list)
	{
		const size_t buff_size = 256;
		char buff[buff_size];
		vsprintf_s(buff, buff_size, format, arg_list);

		OutputDebugStringA(buff);
	}

	void log(const char* format, ...)
	{
		va_list arglist;
		va_start(arglist, format);
		log(format, arglist);
		va_end(arglist);
	}
}
#endif

class timer_t
{
public:
	timer_t()
	{
		init();
		start();
	}

	void start()
	{
		LARGE_INTEGER counter;
		QueryPerformanceCounter(&counter);
		m_counter = counter.QuadPart;
	}

	int64_t elapsed()
	{
		LARGE_INTEGER counter;
		QueryPerformanceCounter(&counter);
		return (int64_t)counter.QuadPart - m_counter;
	}

	int64_t restart()
	{
		LARGE_INTEGER counter;
		QueryPerformanceCounter(&counter);
		int64_t new_counter = (int64_t)counter.QuadPart;
		int64_t elapsed = new_counter - m_counter;
		m_counter = new_counter;
		return elapsed;
	}

	int64_t frequency() { return m_freq; }

	double elapsed_ms()
	{
		return ((double)elapsed() / frequency()) * 1000.0;
	}

private:
	void init()
	{
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		m_freq = f.QuadPart;
	}
private:
	int64_t m_freq;
	int64_t m_counter;
};

struct update_window_params_t
{
	int32_t window_x, window_y;
	int32_t window_width, window_height;
	uint8_t alpha;
};

void update_window(HWND hwnd, HDC hdc_frame, const update_window_params_t &params)
{
	GdiFlush();

	HDC hdc_screen = GetDC(NULL);

	POINT ptWindow = { params.window_x, params.window_y };
	SIZE szWindowSize = { params.window_width, params.window_height };

	BLENDFUNCTION blend = { 0 };
	blend.BlendOp = AC_SRC_OVER;
	blend.SourceConstantAlpha = (BYTE)params.alpha;
	blend.AlphaFormat = AC_SRC_ALPHA;

	POINT ptSrc = { 0, 0 };

	BOOL res = UpdateLayeredWindow(hwnd, hdc_screen, &ptWindow, &szWindowSize, hdc_frame, &ptSrc, 0, &blend, ULW_ALPHA);
#ifdef ENABLE_LOGGING
	if (res == 0)
	{
		DWORD err = GetLastError();
		logging::log("UpdateLayeredWindow failed with error: %d\n", err);
	}
#endif
	ReleaseDC(NULL, hdc_screen);
}

uint32_t rgba_2_bgra(uint32_t value)
{
	return ((value & 0xFF00FF00)) | ((value & 0x00FF0000) >> 16) | ((value & 0x000000FF) << 16);
}

void rgba_2_bgra(uint32_t* data, size_t size)
{
	for (size_t i = 0; i < size; ++i, ++data)
	{
		*data = rgba_2_bgra(*data);
	}
}

struct gif_t
{
	uint8_t *data;
	int *frames_delay_ms;
	int width;
	int height;
	int frames_count;
	int comp;
};

void init_gif(gif_t &gif)
{
	gif.data = nullptr;
	gif.frames_delay_ms = nullptr;
	gif.width = 0;
	gif.height = 0;
	gif.frames_count = 0;
	gif.comp = 0;
}

int load_gif(const char* filename, gif_t &gif)
{
	init_gif(gif);

	FILE* gif_file = nullptr;
	fopen_s(&gif_file, filename, "rb");
	if (gif_file == nullptr)
	{
		return -1;
	}

	fseek(gif_file, 0l, SEEK_END);
	size_t gif_file_size = ftell(gif_file);
	rewind(gif_file);

	uint8_t* gif_buffer = new uint8_t[gif_file_size];
	fread(gif_buffer, 1, gif_file_size, gif_file);
	fclose(gif_file);

	gif.data = stbi_load_gif_from_memory(gif_buffer, (int)gif_file_size, &gif.frames_delay_ms, &gif.width, &gif.height, &gif.frames_count, &gif.comp, 0);
	if (gif.data != nullptr)
	{
		rgba_2_bgra((uint32_t*)gif.data, gif.width * gif.height * gif.frames_count);
	}

	delete gif_buffer;

	return gif.data != nullptr ? 0 : -1;
}

void free_gif(gif_t &gif)
{
	STBI_FREE(gif.frames_delay_ms);
	STBI_FREE(gif.data);
	init_gif(gif);
}

int create_dib_section(HDC &hdc, const gif_t &gif, HBITMAP &hbmp, uint8_t *&data)
{
	BITMAPINFO frame_bmi = { 0 };
	frame_bmi.bmiHeader.biSize = sizeof(frame_bmi.bmiHeader);
	frame_bmi.bmiHeader.biWidth = gif.width;
	frame_bmi.bmiHeader.biHeight = -gif.height;
	frame_bmi.bmiHeader.biPlanes = 1;
	frame_bmi.bmiHeader.biBitCount = 32;
	frame_bmi.bmiHeader.biCompression = BI_RGB;

	hbmp = CreateDIBSection(hdc, &frame_bmi, DIB_RGB_COLORS, (void**)&data, NULL, NULL);

	return data != nullptr ? 0 : -1;
}

const unsigned int MAX_FILENAME_SIZE = 256;
struct config_t
{
	char gif_filename[MAX_FILENAME_SIZE];
	int32_t monitor_idx;
	int32_t window_pos_x;
	int32_t window_pos_y;
	int32_t clickable;
	float alpha;
};

void init_config(config_t &cfg)
{
	cfg.gif_filename[0] = '\0';
	cfg.monitor_idx = 0;
	cfg.window_pos_x = 0;
	cfg.window_pos_y = 0;
	cfg.clickable = 0;
	cfg.alpha = 0.0f;
}

int load_config(const char* filename, config_t &cfg)
{
	init_config(cfg);

	FILE* cfg_file = nullptr;
	fopen_s(&cfg_file, filename, "r");
	if (cfg_file == nullptr)
	{
		return -1;
	}

	int nloaded = fscanf_s(cfg_file, "gif_file = %[^;]; alpha = %f; monitor = %d; window_pos_x = %d; window_pos_y = %d; clickable = %d", cfg.gif_filename, MAX_FILENAME_SIZE, &cfg.alpha, &cfg.monitor_idx, &cfg.window_pos_x, &cfg.window_pos_y, &cfg.clickable);
	fclose(cfg_file);

	return nloaded == 6 ? 0 : -1;
}

struct monitor_enum_proc_data_t
{
	int32_t idx_current;
	int32_t idx_wanted;

	int32_t pos_x;
	int32_t pos_y;
};

BOOL CALLBACK monitor_enum_proc(HMONITOR mon, HDC hdc, LPRECT prc, LPARAM pdata)
{
	monitor_enum_proc_data_t *enum_data = (monitor_enum_proc_data_t*)pdata;
	if (enum_data == nullptr)
	{
		return FALSE;
	}

	if (enum_data->idx_current != enum_data->idx_wanted)
	{
		enum_data->idx_current += 1;
		return TRUE;
	}

	enum_data->pos_x = prc->left;
	enum_data->pos_y = prc->top;

	return FALSE;
}

int get_monitor_pos(const int32_t monitor_idx, int32_t &monitor_pos_x, int32_t &monitor_pos_y)
{
	monitor_enum_proc_data_t enum_data;
	enum_data.idx_current = 0;
	enum_data.idx_wanted = monitor_idx;

	EnumDisplayMonitors(NULL, NULL, monitor_enum_proc, (LPARAM)&enum_data);

	if (enum_data.idx_current == enum_data.idx_wanted)
	{
		monitor_pos_x = enum_data.pos_x;
		monitor_pos_y = enum_data.pos_y;
	}

	return enum_data.idx_current == enum_data.idx_wanted ? 0 : -1;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// Initialize global strings
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_TRANSPARENTGIFOVERLAY, szWindowClass, MAX_LOADSTRING);

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_TRANSPARENTGIFOVERLAY));

	const char* cfg_filename = "settings.cfg";
	config_t cfg;
	if (load_config(cfg_filename, cfg) != 0)
	{
#ifdef ENABLE_LOGGING
		logging::log("Failed to load config.\n");
#endif
		return -1;
	}

	gif_t gif;
	if (load_gif(cfg.gif_filename, gif) != 0)
	{
#ifdef ENABLE_LOGGING
		logging::log("Failed to load gif.\n");
#endif
		return -1;
	}

	MyRegisterClass(hInstance);
	HWND hwnd_main_window;
	if (!InitInstance(hInstance, nCmdShow, cfg.clickable, hwnd_main_window))
	{
#ifdef ENABLE_LOGGING
		logging::log("Failed to create window.\n");
#endif
		return -1;
	}

	int32_t monitor_pos_x = 0, monitor_pos_y = 0;
	get_monitor_pos(cfg.monitor_idx, monitor_pos_x, monitor_pos_y);
	update_window_params_t update_params = { cfg.window_pos_x + monitor_pos_x, cfg.window_pos_y + monitor_pos_y, gif.width, gif.height, (uint8_t)floor(cfg.alpha * 255.0f + 0.5f)};

	HDC hdc_window = GetDC(hwnd_main_window);
	HDC hdc_cur_frame = CreateCompatibleDC(hdc_window);
	ReleaseDC(hwnd_main_window, hdc_window);

	HBITMAP hbmp_cur_frame;
	uint8_t* data_cur_frame = nullptr;
	if (create_dib_section(hdc_cur_frame, gif, hbmp_cur_frame, data_cur_frame) != 0)
	{
#ifdef ENABLE_LOGGING
		logging::log("Failed to create dib section.\n");
#endif
		PostQuitMessage(-1);
	}

	HGDIOBJ hbmpOld = SelectObject(hdc_cur_frame, hbmp_cur_frame);

	timer_t timer;

	int cur_frame_idx = 0;
	int64_t next_frame_time = 0;
	int frame_slice_bytes = gif.width * gif.height * gif.comp;

	MSG msg;
	bool done = false;
	for(;;)
	{
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT)
			{
				done = true;
				break;
			}

			if (msg.message == WM_KEYDOWN)
			{
				if (msg.wParam == VK_ESCAPE)
				{
					done = true;
				}
			}
		}

		if (done == true)
			break;

		int64_t idt = timer.elapsed();
		if (idt > next_frame_time)
		{ 
			timer.start();
			next_frame_time = ((timer.frequency() * (int64_t)gif.frames_delay_ms[cur_frame_idx]) / 1000) - (idt - next_frame_time);
			if (next_frame_time < 0)
			{
				next_frame_time = 0;
			}

			uint8_t* cur_frame_data = gif.data + cur_frame_idx * frame_slice_bytes;

			memcpy(data_cur_frame, cur_frame_data, frame_slice_bytes);

			update_window(hwnd_main_window, hdc_cur_frame, update_params);

			++cur_frame_idx;
			if (cur_frame_idx >= gif.frames_count)
			{
				cur_frame_idx = 0;
			}
		}

		Sleep(1);
	}

	SelectObject(hdc_cur_frame, hbmpOld);
	DeleteObject(hbmp_cur_frame);
	DeleteDC(hdc_cur_frame);

	free_gif(gif);

	return 0;
}


ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TRANSPARENTGIFOVERLAY));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow, int clickable, HWND& hwnd)
{
	DWORD flags_ex = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
	if (clickable == 0)
	{
		flags_ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
	}

	DWORD flags = WS_POPUP;
	hwnd = CreateWindowExW(flags_ex, szWindowClass, szTitle, flags,
		0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);

	if (!hwnd)
	{
		return FALSE;
	}

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}