//---------------------------------------------------------------------------
// 内部: host 非依存プラットフォーム shim の実装
//---------------------------------------------------------------------------
#include "em_platform.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>

namespace elements_modal {

std::uint64_t em_now_ms()
{
	using namespace std::chrono;
	return static_cast<std::uint64_t>(
		duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

static em_log_sink g_log_sink = nullptr;

void em_set_log_sink(em_log_sink sink)
{
	g_log_sink = sink;
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
