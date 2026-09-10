//---------------------------------------------------------------------------
// 内部: host 非依存プラットフォーム shim の実装
//---------------------------------------------------------------------------
#include "em_platform.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>

namespace elements_modal {

static em_clock g_clock = nullptr;

void em_set_clock(em_clock clock)
{
	g_clock = clock;
}

std::uint64_t em_now_ms()
{
	if (g_clock) return g_clock();
	using namespace std::chrono;
	return static_cast<std::uint64_t>(
		duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

static em_log_sink g_log_sink = nullptr;

void em_set_log_sink(em_log_sink sink)
{
	g_log_sink = sink;
}

static bool g_nav_log = false;

void em_set_nav_log(bool enable)
{
	g_nav_log = enable;
}

bool em_nav_log()
{
	return g_nav_log;
}

void em_navlogf(const char* fmt, ...)
{
	if (!g_nav_log) return;
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (g_log_sink) {
		char line[1100];
		std::snprintf(line, sizeof(line), "nav: %s", buf);
		g_log_sink(line);
		return;
	}
	std::fprintf(stderr, "elements_modal: nav: %s\n", buf);
}

void em_logf(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (g_log_sink) {
		char buf[1024];
		std::vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		g_log_sink(buf);
		return;
	}
	std::fputs("elements_modal: ", stderr);
	std::vfprintf(stderr, fmt, ap);
	va_end(ap);
	std::fputc('\n', stderr);
}

} // namespace elements_modal
