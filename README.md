# tpm-vault

Консольный инструмент для работы с зашифрованными виртуальными дисками (LUKS2-образами), где мастер-ключ шифрования хранится в TPM2 и защищён политикой PCR.

## Возможности

- Создание зашифрованных образов дисков произвольного размера
- Автоматическое шифрование с использованием LUKS2 (AES-256)
- Хранение ключа шифрования в TPM2 с привязкой к состоянию системы (PCR 0, 7)
- Автоматическое монтирование/размонтирование

## Безопасность

- **Мастер-ключ (512 бит)** генерируется криптографически стойким ГПСЧ (`/dev/urandom`)
- **Ключ никогда не сохраняется на диск** — передаётся в cryptsetup через stdin
- **Память с ключом затирается** после использования (`explicit_bzero`)
- **Привязка к PCR 0,7** — unseal возможен только при неизменных значениях:
  - PCR 0 — измерения firmware (UEFI)
  - PCR 7 — состояние Secure Boot

## Требования

### Runtime зависимости

| Пакет | Назначение |
|-------|------------|
| `libtss2-fapi1` | Работа с TPM2 через Feature API |
| `cryptsetup` | Управление LUKS-контейнерами |
| `util-linux` | losetup, mount, umount |
| `e2fsprogs` | mkfs.ext4 |

### Build зависимости

| Пакет | Назначение |
|-------|------------|
| `libtss2-dev` | Заголовки tpm2-tss |
| `cmake` (≥3.16) | Система сборки |
| `pkg-config` | Поиск библиотек |
| `g++` | Компилятор C++ (требуется C++17) |

## Установка

Предварительно установить `tss2-fapi` свежей версии, через `apt` пока ставится только старая с багами:

```bash
sudo apt-get install -y autoconf automake libtool pkg-config gcc \
  libssl-dev libjson-c-dev libcurl4-openssl-dev uuid-dev

cd /tmp
git clone --depth 1 --branch 4.1.0 https://github.com/tpm2-software/tpm2-tss.git
cd tpm2-tss

./bootstrap
./configure --prefix=/usr
make -j$(nproc)
sudo make install
```

### Ubuntu/Debian

```bash
# Установка зависимостей
sudo apt update
sudo apt install -y \
    build-essential cmake pkg-config \
    cryptsetup \
    e2fsprogs

# Сборка
mkdir build && cd build
cmake ..
make -j$(nproc)

# Установка (опционально)
sudo make install
```

### Fedora

```bash
# Установка зависимостей
sudo dnf install -y \
    cmake gcc-c++ \
    tpm2-tss-devel \
    cryptsetup \
    e2fsprogs

# Сборка
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Использование

Все команды требуют прав root (используйте `sudo`).

### Создание хранилища

```bash
# Создать хранилище "secrets" размером 100M (по умолчанию)
sudo ./tpm-vault create secrets

# Создать хранилище "backup" размером 1GB
sudo ./tpm-vault create backup 1G
```

### Открытие хранилища

```bash
sudo ./tpm-vault open secrets
# Хранилище будет смонтировано в ./secrets
```

### Закрытие хранилища

```bash
sudo ./tpm-vault close secrets
```

### Список открытых хранилищ

```bash
sudo ./tpm-vault list
```

### Удаление ключа из TPM

```bash
sudo ./tpm-vault wipe secrets
```

> **Внимание:** После `wipe` файл образа останется, но открыть его будет невозможно!

## Структура проекта

```
tpm-vault/
├── CMakeLists.txt           # Конфигурация сборки (CMake)
├── README.md                # Основная документация проекта
├── HANDBOOK.md              # Детальное руководство разработчика
│
├── include/                 # Заголовочные файлы (публичные интерфейсы)
│   ├── tpm_vault.hpp        # Главный координатор всех операций
│   ├── tpm_manager.hpp      # Интерфейс для работы с TPM2 FAPI
│   ├── luks_manager.hpp     # Менеджер LUKS-шифрования
│   ├── loop_manager.hpp     # Менеджер loop-устройств
│   └── utils.hpp            # Вспомогательные функции
│
├── src/                     # Исходный код (реализация)
│   ├── main.cpp             # Точка входа и CLI-парсинг
│   ├── tpm_vault.cpp        # Реализация TPMVault
│   ├── tpm_manager.cpp      # Seal/Unseal через TPM2-TSS
│   ├── luks_manager.cpp     # Вызовы cryptsetup
│   ├── loop_manager.cpp     # Вызовы losetup
│   └── utils.cpp            # Реализация утилит
│
└── scripts/
    └── test-in-qemu.sh      # Автоматическое тестирование с swtpm
```

### Описание модулей

#### Основные компоненты

| Модуль | Назначение | Ключевые функции |
|--------|------------|------------------|
| **tpm_vault** | Главный координатор, объединяющий все компоненты | `create()`, `open()`, `close()`, `list()`, `wipe()` |
| **tpm_manager** | Работа с TPM2 через Feature API (FAPI) | `seal()` — сохранение ключа в TPM<br>`unseal()` — извлечение ключа из TPM |
| **luks_manager** | Управление LUKS2-шифрованием | `format()` — создание зашифрованного раздела<br>`open()` — расшифровка раздела<br>`close()` — закрытие зашифрованного раздела |
| **loop_manager** | Работа с loop-устройствами (образы как блочные устройства) | `setup()` — подключение образа к /dev/loop*<br>`detach()` — отключение loop-устройства |
| **utils** | Вспомогательные функции безопасности и выполнения команд | `secure_erase()` — безопасное стирание памяти<br>`execute_command()` — запуск внешних команд<br>`check_root()` — проверка root-прав |

#### CLI (main.cpp)

Точка входа программы. Парсит команды:
- `create <name> [size]` → создание хранилища
- `open <name>` → открытие и монтирование
- `close <name>` → размонтирование и закрытие
- `list` → список активных хранилищ
- `wipe <name>` → удаление ключа из TPM

#### Взаимодействие компонентов

```
┌─────────────┐
│   main.cpp  │  Обработка команд CLI
└──────┬──────┘
       │
       v
┌─────────────┐
│  tpm_vault  │  Координация всех операций
└──────┬──────┘
       │
       ├──> tpm_manager ──> TPM2 FAPI ──> /dev/tpm0
       │                      (seal/unseal с PCR-политикой)
       │
       ├──> loop_manager ──> losetup ──> /dev/loop*
       │                      (монтирование образа)
       │
       ├──> luks_manager ──> cryptsetup ──> /dev/mapper/luks-*
       │                      (LUKS2-шифрование)
       │
       └──> utils ──> secure_erase(), execute_command()
                      (безопасность + системные вызовы)
```

#### Поток данных при создании хранилища

1. **main.cpp** получает команду `create secrets 100M`
2. **tpm_vault** генерирует мастер-ключ (512 бит из /dev/urandom)
3. **loop_manager** создаёт файл-образ 100M и подключает его как /dev/loop0
4. **luks_manager** форматирует /dev/loop0 с LUKS2, используя мастер-ключ
5. **tpm_manager** сохраняет мастер-ключ в TPM с политикой PCR 0,7
6. **utils::secure_erase()** стирает мастер-ключ из памяти
7. Хранилище готово к использованию

## Тестирование в QEMU с swtpm

Для тестирования без реального TPM используйте виртуальную машину с swtpm:

```bash
# 1. Запуск swtpm
swtpm socket \
    --tpmstate dir=$PWD/tpmstate \
    --ctrl type=unixio,path=$PWD/swtpm-sock \
    --tpm2 --daemon

# 2. Запуск QEMU с vTPM
qemu-system-x86_64 \
    -enable-kvm -cpu host -m 4G \
    -machine q35 \
    -chardev socket,id=chrtpm,path=$PWD/swtpm-sock \
    -tpmdev emulator,id=tpm0,chardev=chrtpm \
    -device tpm-crb,tpmdev=tpm0 \
    -drive file=disk.qcow2,format=qcow2 \
    ...
```

Подробнее см. [HANDBOOK.md](HANDBOOK.md).

## Обработка ошибок

| Ситуация | Сообщение |
|----------|-----------|
| Файл образа не найден | `Error: <n>.img not found in current directory` |
| PCR изменились | `Error: TPM unseal failed — PCR values have changed` |
| Нет прав root | `Error: This operation requires root privileges` |
| Хранилище уже открыто | `Error: <n> is already open` |
| Sealed object не найден | `Error: No TPM sealed object found for <n>` |

## Лицензия

MIT License

## Автор

Михаил — семестровый проект по курсу «Аппаратное обеспечение»
