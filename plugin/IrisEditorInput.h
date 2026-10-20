#pragma once

#include <cmath>
#include <cwchar>
#include <cwctype>
#include <string>

namespace iris::vst3::editor {

// Human entry accepts explicit units but never partially consumes a malformed
// value. Parameter and paired-crossover bounds are applied by the controller UI.
inline bool parseNumber(const std::wstring& text, const char* units, double& result) {
    const auto first=text.find_first_not_of(L" \t\r\n\f\v");
    if (first==std::wstring::npos) return false;
    const auto trimmed=text.substr(first);
    wchar_t* end = nullptr;
    double value = std::wcstod(trimmed.c_str(), &end);
    if (end == trimmed.c_str() || !std::isfinite(value)) return false;
    std::wstring suffix;
    for (const wchar_t* c=end; *c; ++c)
        if (!std::iswspace(*c)) suffix.push_back(static_cast<wchar_t>(std::towlower(*c)));
    const std::string unit = units ? units : "";
    if (unit == "Hz") {
        if (suffix == L"k" || suffix == L"khz") value *= 1000.0;
        else if (!suffix.empty() && suffix != L"hz") return false;
    } else if (unit == "dB") {
        if (!suffix.empty() && suffix != L"db") return false;
    } else if (unit == "%") {
        if (!suffix.empty() && suffix != L"%") return false;
    } else if (unit == "ms") {
        if (!suffix.empty() && suffix != L"ms") return false;
    } else if (unit == "st") {
        if (!suffix.empty() && suffix != L"st" && suffix != L"semitones") return false;
    } else if (!suffix.empty()) return false;
    if (!std::isfinite(value)) return false;
    result = value;
    return true;
}

} // namespace iris::vst3::editor
