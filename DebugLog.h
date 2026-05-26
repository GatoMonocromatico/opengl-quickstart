#pragma once

// -----------------------------------------------------------------------------
// DebugLog.h — optional development logging controlled by the DEBUG macro
// -----------------------------------------------------------------------------
// CMake defines DEBUG for single-configuration Debug builds (see CMakeLists.txt).
// Without DEBUG, DBG / MDBG / DBG_N / DBG_F6 compile out completely (zero runtime cost).
//
// Scaling: logging inside tight loops (per frame, per vertex) can become I/O bound and
// distort timing; keep those sites sparse or sample if traces explode in volume.
//
// Tradeoff: lightweight macros + stderr avoid pulling a full logging framework, but you
// do not get log levels, remote sinks, or structured fields without extending this layer.

#include <cstring>
#include <iomanip>
#include <iostream>
#include <utility>

namespace flux::detail {

// Step: reduce __FILE__ to its basename so log lines stay short across different build paths.
inline const char* dbgBasename(const char* path)
{
	const char* slash = std::strrchr(path, '/');
	const char* bslash = std::strrchr(path, '\\');
	const char* base = path;
	if (slash && slash + 1 > base)
	{
		base = slash + 1;
	}
	if (bslash && bslash + 1 > base)
	{
		base = bslash + 1;
	}
	return base;
}

// Step: write the required "[filename] " prefix (filename only, no path).
inline std::ostream& dbgFilePrefix(std::ostream& os, const char* file)
{
	return os << '[' << dbgBasename(file) << "] ";
}

// Step: stream one float with fixed six decimals without leaving the caller's fmt flags changed.
struct Fixed6
{
	float value;
};
inline std::ostream& operator<<(std::ostream& os, Fixed6 wrapped)
{
	const std::ios::fmtflags saved = os.flags();
	os << std::fixed << std::setprecision(6) << wrapped.value;
	os.flags(saved);
	return os;
}

// Step: pair a string label with a value for MDBG lines (prints name=value).
template<typename T>
struct DbgNamed
{
	const char* name;
	const T& ref;
};

template<typename T>
std::ostream& operator<<(std::ostream& os, const DbgNamed<T>& item)
{
	return os << item.name << '=' << item.ref;
}

template<typename T>
constexpr DbgNamed<T> dbgNamed(const char* name, const T& ref)
{
	return DbgNamed<T>{ name, ref };
}

// Step: emit one full stderr line — file tag once, then each fragment separated by " | ".
template<typename... Args>
void mdbgEmitLine(const char* file, Args&&... args)
{
	dbgFilePrefix(std::cerr, file);
	int slot = 0;
	((std::cerr << ((++slot > 1) ? " | " : "") << std::forward<Args>(args)), ...);
	std::cerr << std::endl;
}

} // namespace flux::detail

#ifdef DEBUG

#define FLUX_DBG_PP_CAT(a, b) a##b
#define FLUX_DBG_PP_ARG3(_1, _2, N, ...) N
#define FLUX_DBG_PP_NARG(...) FLUX_DBG_PP_ARG3(__VA_ARGS__, 2, 1)
#define FLUX_DBG_JOIN_IMPL(a, b) FLUX_DBG_PP_CAT(a, b)
#define FLUX_DBG_DISPATCH(n) FLUX_DBG_JOIN_IMPL(FLUX_DBG_INTERNAL_, n)
#define FLUX_DBG_INTERNAL_1(x) FLUX_DBG_ONE(x)
#define FLUX_DBG_INTERNAL_2(label, x) FLUX_DBG_TWO(label, x)

// Step: one-argument form streams only the value (no label).
#define FLUX_DBG_ONE(x) \
	do \
	{ \
		::flux::detail::dbgFilePrefix(std::cerr, __FILE__) << (x) << std::endl; \
	} while (0)

// Step: two-argument form streams label=value (label is usually a string literal).
#define FLUX_DBG_TWO(label, x) \
	do \
	{ \
		::flux::detail::dbgFilePrefix(std::cerr, __FILE__) << (label) << '=' << (x) << std::endl; \
	} while (0)

// Step: dispatch DBG(...) to one- or two-argument streaming (use MDBG for more fields).
#define DBG(...) FLUX_DBG_DISPATCH(FLUX_DBG_PP_NARG(__VA_ARGS__))(__VA_ARGS__)

// Step: attach a name to a value inside MDBG(...) (expands to a streamable DbgNamed<T>).
#define DBG_N(name, val) ::flux::detail::dbgNamed((name), (val))

// Step: concatenate multiple fragments on a single stderr line after one file tag.
#define MDBG(...) ::flux::detail::mdbgEmitLine(__FILE__, __VA_ARGS__)

// Step: numeric helper for stable float layout without mutating external iostream state.
#define DBG_F6(x) ::flux::detail::Fixed6{ static_cast<float>(x) }

// Step: runtime-gated variants — same signatures as DBG/MDBG but only emit when cond is true.
#define DBG_IF(cond, ...) \
	do { if (cond) { DBG(__VA_ARGS__); } } while (0)

#define MDBG_IF(cond, ...) \
	do { if (cond) { MDBG(__VA_ARGS__); } } while (0)

#else

#define DBG(...) ((void)0)
#define MDBG(...) ((void)0)
#define DBG_N(name, val) ((void)0)
#define DBG_F6(x) ((void)0)
#define DBG_IF(cond, ...) ((void)0)
#define MDBG_IF(cond, ...) ((void)0)

#endif
