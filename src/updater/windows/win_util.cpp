#include "win_util.hpp"

#include "battlespades/updater/http_range.hpp"
#include "battlespades/updater/zip_extract.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <winhttp.h>

#include <fstream>
#include <memory>

namespace battlespades::updater::win {
namespace {

struct InternetHandleDeleter {
    void operator()(HINTERNET handle) const noexcept {
        if (handle != nullptr) WinHttpCloseHandle(handle);
    }
};
using InternetHandle = std::unique_ptr<void, InternetHandleDeleter>;

struct HandleDeleter {
    void operator()(HANDLE handle) const noexcept {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
};
using Handle = std::unique_ptr<void, HandleDeleter>;

[[nodiscard]] std::string last_error_text(const char* what) {
    const auto code = GetLastError();
    wchar_t* buffer = nullptr;
    HMODULE module = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    if (code >= 12000U && code <= 12200U) {
        module = GetModuleHandleW(L"winhttp.dll");
        flags |= FORMAT_MESSAGE_FROM_HMODULE;
    }
    const auto length = FormatMessageW(flags, module, code, 0U, reinterpret_cast<wchar_t*>(&buffer), 0U, nullptr);
    std::string message = std::string{what} + " failed (" + std::to_string(code) + ")";
    if (length != 0U && buffer != nullptr) {
        auto text = narrow(std::wstring_view{buffer, length});
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
        message += ": " + text;
    }
    if (buffer != nullptr) LocalFree(buffer);
    return message;
}

struct Request {
    InternetHandle session;
    InternetHandle connection;
    InternetHandle request;
    unsigned status{};
    std::uint64_t content_length{};
    std::optional<HttpContentRange> content_range;
};

[[nodiscard]] bool open_request(const std::string& url, const HttpOptions& options, Request& out,
                                std::string& error) {
    const auto wide_url = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{};
    wchar_t path[4096]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    parts.dwExtraInfoLength = 1U;   // request the query string too
    wchar_t extra[2048]{};
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(wide_url.c_str(), 0U, 0U, &parts)) {
        error = last_error_text("parsing the URL");
        return false;
    }
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    const auto agent = widen(options.user_agent);
    out.session.reset(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0U));
    if (!out.session) {
        // WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY needs Windows 8.1+.
        out.session.reset(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0U));
    }
    if (!out.session) {
        error = last_error_text("WinHttpOpen");
        return false;
    }
    const int timeout = static_cast<int>(options.timeout_ms);
    WinHttpSetTimeouts(out.session.get(), timeout, timeout, timeout, timeout);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    if (!WinHttpSetOption(out.session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(out.session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }

    out.connection.reset(WinHttpConnect(out.session.get(), host, parts.nPort, 0U));
    if (!out.connection) {
        error = last_error_text("WinHttpConnect");
        return false;
    }
    std::wstring object = path;
    object += extra;
    out.request.reset(WinHttpOpenRequest(out.connection.get(), L"GET", object.c_str(), nullptr,
                                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         secure ? WINHTTP_FLAG_SECURE : 0U));
    if (!out.request) {
        error = last_error_text("WinHttpOpenRequest");
        return false;
    }
    std::wstring headers;
    for (const auto& header : options.headers) headers += widen(header) + L"\r\n";
    if (!WinHttpSendRequest(out.request.get(), headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            headers.empty() ? 0U : static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0U, 0U, 0U) ||
        !WinHttpReceiveResponse(out.request.get(), nullptr)) {
        error = last_error_text("the HTTP request");
        return false;
    }
    DWORD status{};
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(out.request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    out.status = status;
    wchar_t length[32]{};
    DWORD length_size = sizeof(length);
    if (WinHttpQueryHeaders(out.request.get(), WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            length, &length_size, WINHTTP_NO_HEADER_INDEX)) {
        out.content_length = std::wcstoull(length, nullptr, 10);
    }
    wchar_t range[128]{};
    DWORD range_size = sizeof(range);
    if (WinHttpQueryHeaders(out.request.get(), WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX,
                            range, &range_size, WINHTTP_NO_HEADER_INDEX)) {
        out.content_range = parse_http_content_range(narrow(range));
    }
    return true;
}

template <typename Sink>
bool read_body(Request& request, Sink&& sink, std::string& error) {
    std::vector<char> buffer(1U << 16U);
    for (;;) {
        DWORD read{};
        if (!WinHttpReadData(request.request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            error = last_error_text("reading the HTTP response");
            return false;
        }
        if (read == 0U) return true;
        if (!sink(buffer.data(), static_cast<std::size_t>(read))) return false;
    }
}

} // namespace

std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
    return wide;
}

std::string narrow(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                           nullptr, nullptr);
    std::string text(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), text.data(), length, nullptr,
                        nullptr);
    return text;
}

HttpResponse http_get(const std::string& url, const HttpOptions& options, std::size_t max_body) {
    HttpResponse response;
    Request request;
    if (!open_request(url, options, request, response.error)) return response;
    response.status = request.status;
    const bool ok = read_body(
        request,
        [&](const char* data, std::size_t size) {
            if (response.body.size() + size > max_body) {
                response.error = "response is larger than " + std::to_string(max_body) + " bytes";
                return false;
            }
            response.body.append(data, size);
            return true;
        },
        response.error);
    if (!ok) response.status = 0U;
    return response;
}

HttpResponse http_download(const std::string& url, const std::filesystem::path& file,
                           const HttpOptions& options, const ProgressCallback& progress,
                           std::uint64_t expected_size) {
    HttpResponse response;
    std::error_code code;
    // Resume an interrupted download: ask for the remaining bytes. A server
    // that ignores ranges answers 200 and the file restarts from zero; the
    // caller verifies size and SHA-256 of the whole file either way.
    std::uint64_t existing = std::filesystem::is_regular_file(file, code) ? std::filesystem::file_size(file, code) : 0U;
    if (code) existing = 0U;
    if (expected_size != 0U && existing > expected_size) existing = 0U;
    for (int attempt = 0; attempt < 2; ++attempt) {
        HttpOptions request_options = options;
        if (existing > 0U) request_options.headers.push_back("Range: bytes=" + std::to_string(existing) + "-");
        Request request;
        response = HttpResponse{};
        if (!open_request(url, request_options, request, response.error)) return response;
        response.status = request.status;
        if (request.status == 416U && existing > 0U) {
            // Range not satisfiable (stale or already complete partial): start over.
            std::filesystem::remove(file, code);
            existing = 0U;
            continue;
        }
        const bool resumed = request.status == 206U && existing > 0U;
        if (request.status != 200U && !resumed) return response;
        if (resumed && (!request.content_range.has_value() || request.content_range->first != existing ||
                        (expected_size != 0U && request.content_range->total != expected_size))) {
            response.status = 0U;
            response.error = "the server sent an invalid Content-Range for this download";
            return response;
        }
        if (!resumed) existing = 0U;
        if (expected_size != 0U && request.content_length > expected_size - existing) {
            response.status = 0U;
            response.error = "the server sent more than the expected package size";
            return response;
        }
        std::ofstream output{file, std::ios::binary | (resumed ? std::ios::app : std::ios::trunc)};
        if (!output) {
            response.error = "cannot create " + narrow(file.wstring());
            response.status = 0U;
            return response;
        }
        const std::uint64_t total = expected_size != 0U ? expected_size
                                       : resumed ? request.content_range->total : request.content_length;
        std::uint64_t received = existing;
        const bool ok = read_body(
            request,
            [&](const char* data, std::size_t size) {
                if ((expected_size != 0U && size > expected_size - received) ||
                    (resumed && (received > request.content_range->last + 1U ||
                                 size > request.content_range->last + 1U - received))) {
                    response.error = "the server sent more than the expected package size";
                    return false;
                }
                output.write(data, static_cast<std::streamsize>(size));
                if (!output) {
                    response.error = "cannot write " + narrow(file.wstring()) + " (disk full?)";
                    return false;
                }
                received += size;
                if (progress && !progress(received, total)) {
                    response.error = "cancelled";
                    return false;
                }
                return true;
            },
            response.error);
        output.close();
        if (!ok || !output) {
            if (response.error.empty()) response.error = "download interrupted";
            response.status = 0U;
            return response;   // the partial file is kept for the next attempt
        }
        if (resumed && received != request.content_range->last + 1U) {
            response.status = 0U;
            response.error = "the server sent an incomplete byte range";
            return response;
        }
        response.status = 200U;
        return response;
    }
    return response;
}

std::optional<std::filesystem::path> registry_steam_root() {
    const auto read = [](HKEY root, const wchar_t* key, const wchar_t* value, DWORD flags)
        -> std::optional<std::filesystem::path> {
        wchar_t buffer[MAX_PATH * 2]{};
        DWORD size = sizeof(buffer);
        if (RegGetValueW(root, key, value, RRF_RT_REG_SZ | flags, nullptr, buffer, &size) != ERROR_SUCCESS) {
            return std::nullopt;
        }
        std::filesystem::path path{buffer};
        std::error_code code;
        if (path.empty() || !std::filesystem::is_directory(path, code)) return std::nullopt;
        return path.make_preferred();
    };
    if (auto path = read(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", 0U); path.has_value()) {
        return path;
    }
    return read(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", RRF_SUBKEY_WOW6432KEY);
}

bool is_process_running(std::wstring_view image_name) {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0U)};
    if (snapshot.get() == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot.get(), &entry); more; more = Process32NextW(snapshot.get(), &entry)) {
        if (CompareStringOrdinal(entry.szExeFile, -1, image_name.data(), static_cast<int>(image_name.size()),
                                 TRUE) == CSTR_EQUAL) {
            return true;
        }
    }
    return false;
}

std::optional<unsigned long> run_hidden(const std::wstring& application, const std::wstring& command_line,
                                        unsigned long timeout_ms) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    std::wstring mutable_line = command_line;
    if (!CreateProcessW(application.c_str(), mutable_line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) {
        return std::nullopt;
    }
    Handle thread{process.hThread};
    Handle child{process.hProcess};
    if (WaitForSingleObject(child.get(), timeout_ms) != WAIT_OBJECT_0) {
        TerminateProcess(child.get(), 1U);
        return std::nullopt;
    }
    DWORD code{};
    GetExitCodeProcess(child.get(), &code);
    return code;
}

bool extract_zip(const std::filesystem::path& archive, const std::filesystem::path& destination,
                 std::string& error) {
    // In-process: Windows' tar.exe opens archives through the ANSI code page
    // and fails on paths such as C:\Users\<Cyrillic name>\...
    return extract_zip_archive(archive, destination, error);
}

std::filesystem::path current_executable() {
    std::wstring buffer(32768U, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path{buffer};
}

std::filesystem::path local_app_data() {
    PWSTR path = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0U, nullptr, &path))) result = path;
    CoTaskMemFree(path);
    return result;
}

} // namespace battlespades::updater::win
