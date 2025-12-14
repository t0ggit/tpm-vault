#include "utils.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <array>
#include <memory>
#include <climits>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

namespace tpm_vault {

void secure_erase(void* ptr, size_t size) {
    if (ptr && size > 0) {
        // Используем volatile чтобы компилятор не оптимизировал
        volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
        while (size--) {
            *p++ = 0;
        }
        // Барьер памяти
        __asm__ __volatile__("" ::: "memory");
    }
}

void secure_erase(std::vector<uint8_t>& data) {
    secure_erase(data.data(), data.size());
    data.clear();
}

// SecureBuffer implementation
SecureBuffer::SecureBuffer(size_t size) : data_(size, 0) {}

SecureBuffer::~SecureBuffer() {
    secure_erase(data_.data(), data_.size());
}

SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept 
    : data_(std::move(other.data_)) {
}

SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept {
    if (this != &other) {
        secure_erase(data_.data(), data_.size());
        data_ = std::move(other.data_);
    }
    return *this;
}

size_t parse_size(const std::string& size_str) {
    if (size_str.empty()) {
        throw VaultError("Empty size string");
    }
    
    size_t multiplier = 1;
    std::string num_part = size_str;
    
    char suffix = size_str.back();
    if (suffix == 'M' || suffix == 'm') {
        multiplier = 1024ULL * 1024ULL;
        num_part = size_str.substr(0, size_str.length() - 1);
    } else if (suffix == 'G' || suffix == 'g') {
        multiplier = 1024ULL * 1024ULL * 1024ULL;
        num_part = size_str.substr(0, size_str.length() - 1);
    } else if (suffix == 'K' || suffix == 'k') {
        multiplier = 1024ULL;
        num_part = size_str.substr(0, size_str.length() - 1);
    }
    
    try {
        size_t value = std::stoull(num_part);
        return value * multiplier;
    } catch (const std::exception&) {
        throw VaultError("Invalid size format: " + size_str);
    }
}

std::string format_size(size_t bytes) {
    std::ostringstream oss;
    if (bytes >= 1024ULL * 1024ULL * 1024ULL && bytes % (1024ULL * 1024ULL * 1024ULL) == 0) {
        oss << (bytes / (1024ULL * 1024ULL * 1024ULL)) << "G";
    } else if (bytes >= 1024ULL * 1024ULL && bytes % (1024ULL * 1024ULL) == 0) {
        oss << (bytes / (1024ULL * 1024ULL)) << "M";
    } else if (bytes >= 1024ULL && bytes % 1024ULL == 0) {
        oss << (bytes / 1024ULL) << "K";
    } else {
        oss << bytes;
    }
    return oss.str();
}

std::vector<uint8_t> generate_random_bytes(size_t size) {
    std::vector<uint8_t> result(size);
    
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        throw VaultError("Failed to open /dev/urandom");
    }
    
    size_t total_read = 0;
    while (total_read < size) {
        ssize_t bytes_read = ::read(fd, result.data() + total_read, size - total_read);
        if (bytes_read < 0) {
            ::close(fd);
            throw VaultError("Failed to read from /dev/urandom");
        }
        total_read += bytes_read;
    }
    
    ::close(fd);
    return result;
}

bool is_root() {
    return geteuid() == 0;
}

bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool directory_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void ensure_directory(const std::string& path) {
    if (!directory_exists(path)) {
        if (mkdir(path.c_str(), 0755) != 0) {
            throw VaultError("Failed to create directory: " + path);
        }
    }
}

int execute_command(const std::string& cmd, const std::vector<uint8_t>* stdin_data) {
    int pipe_fd[2] = {-1, -1};
    
    if (stdin_data) {
        if (pipe(pipe_fd) < 0) {
            throw VaultError("Failed to create pipe");
        }
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        if (stdin_data) {
            ::close(pipe_fd[0]);
            ::close(pipe_fd[1]);
        }
        throw VaultError("Failed to fork");
    }
    
    if (pid == 0) {
        // Child process
        if (stdin_data) {
            ::close(pipe_fd[1]); // Close write end
            dup2(pipe_fd[0], STDIN_FILENO);
            ::close(pipe_fd[0]);
        }

        // Redirect stdout/stderr to /dev/null for clean output
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }

        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }
    
    // Parent process
    if (stdin_data) {
        ::close(pipe_fd[0]); // Close read end
        
        size_t written = 0;
        while (written < stdin_data->size()) {
            ssize_t n = write(pipe_fd[1], stdin_data->data() + written, 
                             stdin_data->size() - written);
            if (n < 0) {
                ::close(pipe_fd[1]);
                throw VaultError("Failed to write to pipe");
            }
            written += n;
        }
        ::close(pipe_fd[1]);
    }
    
    int status;
    waitpid(pid, &status, 0);
    
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

std::string execute_command_output(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        throw VaultError("Failed to execute command: " + cmd);
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    
    int status = pclose(pipe);
    if (WEXITSTATUS(status) != 0) {
        throw VaultError("Command failed: " + cmd);
    }
    
    // Trim trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    
    return result;
}

std::string get_current_directory() {
    char buffer[PATH_MAX];
    if (getcwd(buffer, sizeof(buffer)) == nullptr) {
        throw VaultError("Failed to get current directory");
    }
    return std::string(buffer);
}

} // namespace tpm_vault
