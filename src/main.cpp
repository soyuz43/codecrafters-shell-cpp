#include <iostream>
#include <string>
#include <sstream>
#include <set>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <cctype>
#include <locale>
#include <algorithm>

namespace fs = std::filesystem;

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <cwchar>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#endif

// Safe trim functions
inline void trim(std::string& s) {
    auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) { s.clear(); return; }
    auto last = s.find_last_not_of(" \t");
    s.erase(last + 1);
    s.erase(0, first);
}

inline void trim(std::wstring& s) {
    auto first = s.find_first_not_of(L" \t");
    if (first == std::wstring::npos) { s.clear(); return; }
    auto last = s.find_last_not_of(L" \t");
    s.erase(last + 1);
    s.erase(0, first);
}

// Platform-specific environment access
#ifdef _WIN32
std::optional<std::wstring> get_wenv(const wchar_t* name) {
    const wchar_t* val = _wgetenv(name);
    if (!val || *val == L'\0') return std::nullopt;
    return std::wstring(val);
}

std::string wide_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) return "";
    
    std::vector<char> buffer(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, buffer.data(), size_needed, nullptr, nullptr);
    return std::string(buffer.data());
}

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n-1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
#else
std::optional<std::string> get_env(const char* name) {
    const char* val = std::getenv(name);
    if (!val || *val == '\0') return std::nullopt;
    return std::string(val);
}
#endif

std::vector<fs::path> get_path_directories() {
#ifdef _WIN32
    auto path_env = get_wenv(L"PATH");
    if (!path_env) return {};
    
    std::vector<fs::path> dirs;
    dirs.reserve(16); // Pre-allocate for common case
    std::wstringstream ss(*path_env);
    std::wstring dir;
    while (std::getline(ss, dir, L';')) {
        trim(dir);
        if (!dir.empty()) dirs.push_back(fs::path(dir));
    }
    return dirs;
#else
    auto path_env = get_env("PATH");
    if (!path_env) return {};
    
    std::vector<fs::path> dirs;
    dirs.reserve(16);
    std::stringstream ss(*path_env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        if (dir.empty()) {
            dirs.push_back(fs::path("."));
        } else {
            trim(dir);
            if (!dir.empty()) dirs.push_back(fs::path(dir));
        }
    }
    return dirs;
#endif
}

std::vector<std::string> get_executable_extensions() {
#ifdef _WIN32
    auto pathext_env = get_wenv(L"PATHEXT");
    if (pathext_env && !pathext_env->empty()) {
        std::vector<std::string> exts;
        std::unordered_set<std::string> seen;
        std::wstringstream ss(*pathext_env);
        std::wstring ext;
        while (std::getline(ss, ext, L';')) {
            trim(ext);
            if (ext.empty()) continue;
            if (ext[0] != L'.') ext = L'.' + ext;
            auto u8 = wide_to_utf8(ext);
            for (auto& c : u8) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (seen.insert(u8).second) exts.push_back(std::move(u8));
        }
        if (!exts.empty()) return exts;
    }
    return {".exe", ".bat", ".cmd", ".com"};
#else
    return {""};
#endif
}

class PathCache {
    std::unordered_map<std::string, std::optional<fs::path>> cache;
    std::string last_path_value;
    std::string last_pathext_value;
    std::vector<fs::path> path_directories;
    std::vector<std::string> executable_extensions;
    
    bool environment_changed() {
#ifdef _WIN32
        auto path_val = get_wenv(L"PATH");
        auto pathext_val = get_wenv(L"PATHEXT");
        bool changed = (!path_val || wide_to_utf8(*path_val) != last_path_value) || 
                      (!pathext_val || wide_to_utf8(*pathext_val) != last_pathext_value);
        if (changed) {
            last_path_value = path_val ? wide_to_utf8(*path_val) : "";
            last_pathext_value = pathext_val ? wide_to_utf8(*pathext_val) : "";
            path_directories = get_path_directories();
            executable_extensions = get_executable_extensions();
            cache.clear();
        }
        return changed;
#else
        auto path_val = get_env("PATH");
        bool changed = (!path_val || *path_val != last_path_value);
        if (changed) {
            last_path_value = path_val ? *path_val : "";
            path_directories = get_path_directories();
            executable_extensions = get_executable_extensions();
            cache.clear();
        }
        return changed;
#endif
    }
    
    fs::path resolve_path_internal(const std::string& cmd, bool direct_path) {
        if (direct_path) {
            fs::path candidate(cmd);
            try {
                if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
#ifdef _WIN32
                    return fs::weakly_canonical(candidate);
#else
                    if (access(candidate.string().c_str(), X_OK) == 0) {
                        return fs::weakly_canonical(candidate);
                    }
#endif
                }
            } catch (const fs::filesystem_error&) {
                // File disappeared - skip
            }
            return {};
        }
        
        auto try_one = [](const fs::path& p) -> fs::path {
            try {
                if (fs::exists(p) && fs::is_regular_file(p)) {
#ifdef _WIN32
                    return fs::weakly_canonical(p);
#else
                    if (access(p.string().c_str(), X_OK) == 0) {
                        return fs::weakly_canonical(p);
                    }
#endif
                }
            } catch (const fs::filesystem_error&) {}
            return {};
        };
        
#ifdef _WIN32
        bool has_ext = fs::path(cmd).has_extension();
        
        // Try current directory first on Windows
        try {
            if (has_ext) {
                if (auto p = try_one(fs::current_path() / cmd); !p.empty()) return p;
            }
            for (const auto& ext : executable_extensions) {
                if (auto p = try_one(fs::current_path() / (cmd + ext)); !p.empty()) return p;
            }
        } catch (const fs::filesystem_error&) {
            // Current path access failed, continue to PATH search
        }
        
        for (const auto& dir : path_directories) {
            if (has_ext) {
                if (auto p = try_one(dir / cmd); !p.empty()) return p;
            }
            for (const auto& ext : executable_extensions) {
                if (auto p = try_one(dir / (cmd + ext)); !p.empty()) return p;
            }
        }
#else
        for (const auto& dir : path_directories) {
            for (const auto& ext : executable_extensions) {
                if (auto p = try_one(dir / (cmd + ext)); !p.empty()) return p;
            }
        }
#endif
        
        return {};
    }
    
public:
    PathCache() {
        environment_changed();
    }
    
    fs::path find(const std::string& cmd) {
        if (cmd.empty()) return {};
        
        environment_changed();
        
#ifdef _WIN32
        std::string cache_key = cmd;
        std::transform(cache_key.begin(), cache_key.end(), cache_key.begin(),
                       [](unsigned char c){ return std::tolower(c); });
#else
        const std::string& cache_key = cmd;
#endif
        
        if (auto it = cache.find(cache_key); it != cache.end()) {
            return it->second.value_or(fs::path{});
        }
        
        bool direct_path = (cmd.find('/') != std::string::npos || 
                           cmd.find('\\') != std::string::npos ||
                           (cmd.size() >= 2 && cmd[1] == ':'));
        
        auto path = resolve_path_internal(cmd, direct_path);
        cache[cache_key] = path.empty() ? std::nullopt : std::make_optional(path);
        return path;
    }
};

std::vector<std::string> tokenize_command(const std::string& line) {
    std::vector<std::string> tokens;
    tokens.reserve(8);
    std::string token;
    bool in_double_quotes = false;
    bool in_single_quotes = false;
    bool escape_next = false;
    
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        
        if (escape_next) {
            token += c;
            escape_next = false;
            continue;
        }
        
#ifdef _WIN32
        // Windows: Only escape inside double quotes
        if (in_double_quotes && c == '\\' && i + 1 < line.size()) {
            if (line[i+1] == '"' || line[i+1] == '\\') {
                escape_next = true;
                continue;
            }
        }
#else
        // Unix: Handle single quotes
        if (c == '\'' && !in_double_quotes) {
            in_single_quotes = !in_single_quotes;
            continue;
        }
        
        // Unix: Handle line continuation
        if (!in_single_quotes && !in_double_quotes && c == '\\' && i+1 < line.size() && line[i+1] == '\n') {
            ++i; // skip the newline
            continue;
        }
        
        // Unix: Only escape specific chars inside double quotes, all chars outside
        if (c == '\\' && i + 1 < line.size()) {
            if (in_double_quotes) {
                if (line[i+1] == '"' || line[i+1] == '\\' || line[i+1] == '$' || line[i+1] == '`') {
                    escape_next = true;
                    continue;
                }
            } else {
                escape_next = true;
                continue;
            }
        }
#endif
        
        if (c == '"' && !in_single_quotes) {
            in_double_quotes = !in_double_quotes;
            continue;
        }
        
        if (std::isspace(static_cast<unsigned char>(c)) && !in_double_quotes && !in_single_quotes) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
            continue;
        }
        
        token += c;
    }
    
    // Check for unclosed quotes
    if (in_double_quotes || in_single_quotes) {
        std::cerr << "Error: unclosed quote\n";
        return {};  // Return empty tokens to indicate error
    }
    
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

#ifdef _WIN32
std::wstring quote_windows_arg(const std::wstring& arg) {
    if (arg.find_first_of(L" \t\"") == std::wstring::npos && 
        arg.find_first_of(L'\n') == std::wstring::npos) {
        return arg;
    }
    
    std::wstring quoted = L"\"";
    size_t backslash_count = 0;
    
    for (wchar_t wc : arg) {
        if (wc == L'\\') {
            backslash_count++;
        } else if (wc == L'"') {
            quoted.append(backslash_count * 2, L'\\');
            quoted += L"\\\"";
            backslash_count = 0;
        } else {
            quoted.append(backslash_count, L'\\');
            quoted += wc;
            backslash_count = 0;
        }
    }
    
    quoted.append(backslash_count * 2, L'\\');
    quoted += L'"';
    return quoted;
}
#endif

// Global for exit status tracking
int last_status = 0;

#ifdef _WIN32
void execute_command(const fs::path& program, const std::vector<std::string>& args) {
    // Build full command line including argv[0]
    std::wstring full = quote_windows_arg(program.wstring());
    for (size_t i = 1; i < args.size(); ++i) {
        full += L' ';
        full += quote_windows_arg(utf8_to_wide(args[i]));
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> mutable_cmd(full.begin(), full.end());
    mutable_cmd.push_back(L'\0');

    if (!CreateProcessW(
            nullptr,                    // let Windows parse argv[0] from command line
            mutable_cmd.data(),         // MUST be mutable
            nullptr, nullptr,
            FALSE,                      // don't inherit handles unless needed
            0,
            nullptr,
            nullptr,
            &si, &pi)) {
        DWORD err = GetLastError();
        LPWSTR msg = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                       nullptr, err, 0, (LPWSTR)&msg, 0, nullptr);
        std::string error_msg = msg ? wide_to_utf8(msg) : "Unknown error";
        std::cerr << "Execute failed (" << err << "): " << error_msg << std::endl;
        if (msg) LocalFree(msg);
        return;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}
#else
void execute_command(const fs::path& program, const std::vector<std::string>& args) {
    // Create copies and then create argv array pointing to those copies
    std::vector<std::string> arg_copies;
    arg_copies.reserve(args.size());
    for (const auto& arg : args) {
        arg_copies.push_back(arg);
    }
    std::vector<char*> argv;
    argv.reserve(arg_copies.size() + 1);
    for (auto& arg : arg_copies) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    
    pid_t pid = fork();
    if (pid == 0) {
        execv(program.string().c_str(), argv.data());
        perror("exec failed");
        _exit(127);
    } else if (pid > 0) {
        int status = 0;
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid");
        } else if (WIFSIGNALED(status)) {
            std::cerr << "terminated by signal " << WTERMSIG(status) << "\n";
            last_status = 128 + WTERMSIG(status);
        } else if (WIFEXITED(status)) {
            last_status = WEXITSTATUS(status);
        }
    } else {
        perror("fork failed");
    }
}
#endif

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    // Add "pwd" to the set of built-in commands
    const std::set<std::string> builtins = {"echo", "exit", "type", "pwd"};
    PathCache path_cache;

    while (true) {
        std::cout << "$ " << std::flush;

        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << std::endl;
            break;
        }

        if (line.find_first_not_of(" \t") == std::string::npos) continue;

        auto args = tokenize_command(line);
        if (args.empty()) continue; // Handle tokenizer error

        const auto& cmd = args[0];

        if (cmd == "exit") {
            int code = 0;
            if (args.size() > 1) {
                try {
                    code = std::stoi(args[1]);
                } catch (const std::invalid_argument&) {
                    std::cerr << "exit: invalid number\n";
                    continue;
                } catch (const std::out_of_range&) {
                    std::cerr << "exit: number out of range\n";
                    continue;
                }
            }
            return code;
        }

        // Handle echo command
        if (cmd == "echo") {
            for (size_t i = 1; i < args.size(); ++i) {
                if (i > 1) std::cout << ' ';
                std::cout << args[i];
            }
            std::cout << '\n';
            continue;
        }

        // Handle type command
        if (cmd == "type") {
            if (args.size() < 2) {
                std::cerr << "type: missing argument\n";
                continue;
            }

            const auto& target = args[1];
            if (builtins.count(target)) {
                std::cout << target << " is a shell builtin\n";
                continue;
            }

            auto path = path_cache.find(target);
            if (path.empty()) {
                std::cerr << target << ": not found\n";
            } else {
                std::cout << target << " is " << path.string() << '\n';
            }
            continue;
        }

// Handle pwd command
if (cmd == "pwd") {
    try {
        // Use .string() to get the path as a standard string
        std::cout << fs::current_path().string() << '\n';
    } catch (const fs::filesystem_error& ex) {
        // Handle potential errors (e.g., permissions, inaccessible path)
        std::cerr << "pwd: error accessing current directory: " << ex.what() << '\n';
    }
    continue; // Move to the next prompt after printing
}

        // Handle cd command (absolute, relative, and ~ paths)
        if (cmd == "cd") {
            if (args.size() != 2) {
                std::cerr << "cd: expected 1 argument, got " << (args.size() - 1) << '\n';
                continue;
            }

            std::string target_dir = args[1]; // Use a copy that we might modify

            // Check if the path starts with ~ or is exactly ~
            if (target_dir == "~" || (target_dir.size() >= 2 && target_dir[0] == '~' && target_dir[1] == '/')) {
                // Get the HOME environment variable
                const char* home_cstr = std::getenv("HOME");
                #ifdef _WIN32
                    // On Windows, HOME might not be set, try USERPROFILE or HOMEDRIVE + HOMEPATH
                    if (!home_cstr) {
                        home_cstr = std::getenv("USERPROFILE");
                    }
                    if (!home_cstr) {
                        const char* drive = std::getenv("HOMEDRIVE");
                        const char* path = std::getenv("HOMEPATH");
                        if (drive && path) {
                            std::string home_win = std::string(drive) + path;
                            home_cstr = home_win.c_str();
                        }
                    }
                #endif

                if (!home_cstr) {
                    std::cerr << "cd: HOME not set\n";
                    continue; // Stay in current directory if HOME is not available
                }

                std::string home_dir(home_cstr);

                // If target was just "~", use the home directory directly
                if (target_dir == "~") {
                    target_dir = home_dir;
                } else {
                    // If target was "~/" or "~/path", replace ~ with home_dir
                    // Remove the leading "~/" and append the rest to home_dir
                    std::string relative_part = target_dir.substr(2); // Remove "~/"
                    // Use fs::path to correctly combine paths (handles trailing / in home_dir correctly)
                    fs::path combined_path(home_dir);
                    combined_path /= relative_part; // or combined_path = home_dir / relative_part;
                    target_dir = combined_path.string();
                }
            }
            // If the path did not start with ~, target_dir remains unchanged


            // Now, target_dir contains the resolved path (absolute or relative after ~ expansion)
            // Continue with the same logic as before for absolute/relative path handling

            // Determine if the *resolved* path is now absolute
            fs::path target_path(target_dir);
            bool is_absolute = target_path.is_absolute();

            // If it's not absolute (it was a relative path like 'dirname' or './dirname'),
            // resolve it relative to the current working directory
            if (!is_absolute) {
                // Get the current working directory
                fs::path current_dir = fs::current_path();
                // Combine current directory with the relative path
                target_path = current_dir / target_path;
            }
            // If it was absolute (either originally like '/path' or resolved from '~' like '/home/user'),
            // target_path already holds the correct absolute path

            // Normalize the path to resolve '.' and '..' components
            try {
                target_path = fs::weakly_canonical(target_path);
            } catch (const fs::filesystem_error&) {
                std::cerr << "cd: error resolving path: " << args[1] << '\n';
                continue; // Stay in current directory
            }

            // Validate the final resolved path exists and is a directory
            if (!fs::exists(target_path)) {
                std::cerr << "cd: " << args[1] << ": No such file or directory\n";
                continue; // Stay in current directory
            }

            if (!fs::is_directory(target_path)) {
                std::cerr << "cd: " << args[1] << ": Not a directory\n";
                continue; // Stay in current directory
            }

            // Attempt to change the current directory to the resolved absolute path
            try {
                fs::current_path(target_path);
                // Success: directory changed, loop continues normally
            } catch (const fs::filesystem_error& ex) {
                // Handle potential errors during the change (e.g., permissions)
                std::cerr << "cd: error changing to " << args[1] << ": " << ex.what() << '\n';
                // Stay in current directory
            }
            continue; // Move to the next prompt after attempting cd
        }

        // External command handling (as before)
        auto path = path_cache.find(cmd);
        if (path.empty()) {
            std::cerr << cmd << ": command not found\n";
            continue;
        }

        execute_command(path, args);
    }

    return 0;
}