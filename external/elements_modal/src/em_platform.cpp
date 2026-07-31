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

void em_logf(const char* fmt, ...)
{
	std::fputs("elements_modal: ", stderr);
	va_list ap;
	va_start(ap, fmt);
	std::vfprintf(stderr, fmt, ap);
	va_end(ap);
	std::fputc('\n', stderr);
}

} // namespace elements_modal
