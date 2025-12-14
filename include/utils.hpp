#ifndef TPM_VAULT_UTILS_HPP
#define TPM_VAULT_UTILS_HPP

#include <string>
#include <cstdint>
#include <vector>
#include <stdexcept>

namespace tpm_vault {

/**
 * @brief Исключение для ошибок tpm-vault
 */
class VaultError : public std::runtime_error {
public:
    explicit VaultError(const std::string& msg) : std::runtime_error(msg) {}
};

/**
 * @brief Безопасно затирает память
 * @param ptr Указатель на память
 * @param size Размер области памяти
 */
void secure_erase(void* ptr, size_t size);

/**
 * @brief Безопасно затирает вектор байт
 * @param data Вектор для затирания
 */
void secure_erase(std::vector<uint8_t>& data);

/**
 * @brief RAII-обёртка для автоматического затирания памяти
 */
class SecureBuffer {
public:
    explicit SecureBuffer(size_t size);
    ~SecureBuffer();
    
    // Запрещаем копирование
    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;
    
    // Разрешаем перемещение
    SecureBuffer(SecureBuffer&& other) noexcept;
    SecureBuffer& operator=(SecureBuffer&& other) noexcept;
    
    uint8_t* data() { return data_.data(); }
    const uint8_t* data() const { return data_.data(); }
    size_t size() const { return data_.size(); }
    
    // Для удобства работы с cryptsetup
    std::vector<uint8_t>& vector() { return data_; }

private:
    std::vector<uint8_t> data_;
};

/**
 * @brief Парсит размер с суффиксом (M, G)
 * @param size_str Строка с размером (например, "100M", "1G")
 * @return Размер в байтах
 * @throws VaultError при некорректном формате
 */
size_t parse_size(const std::string& size_str);

/**
 * @brief Форматирует размер в человекочитаемый вид
 * @param bytes Размер в байтах
 * @return Строка вида "100M" или "1G"
 */
std::string format_size(size_t bytes);

/**
 * @brief Генерирует криптографически стойкие случайные байты
 * @param size Количество байт
 * @return Вектор случайных байт
 * @throws VaultError при ошибке генерации
 */
std::vector<uint8_t> generate_random_bytes(size_t size);

/**
 * @brief Проверяет, запущено ли приложение с правами root
 * @return true если root
 */
bool is_root();

/**
 * @brief Проверяет существование файла
 * @param path Путь к файлу
 * @return true если файл существует
 */
bool file_exists(const std::string& path);

/**
 * @brief Проверяет существование директории
 * @param path Путь к директории
 * @return true если директория существует
 */
bool directory_exists(const std::string& path);

/**
 * @brief Создаёт директорию если не существует
 * @param path Путь к директории
 * @throws VaultError при ошибке создания
 */
void ensure_directory(const std::string& path);

/**
 * @brief Выполняет внешнюю команду
 * @param cmd Команда для выполнения
 * @param stdin_data Данные для передачи в stdin (опционально)
 * @return Код возврата команды
 */
int execute_command(const std::string& cmd, const std::vector<uint8_t>* stdin_data = nullptr);

/**
 * @brief Выполняет команду и возвращает stdout
 * @param cmd Команда для выполнения
 * @return Вывод команды
 * @throws VaultError при ошибке выполнения
 */
std::string execute_command_output(const std::string& cmd);

/**
 * @brief Получает текущую рабочую директорию
 * @return Путь к текущей директории
 */
std::string get_current_directory();

} // namespace tpm_vault

#endif // TPM_VAULT_UTILS_HPP
