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

### Ubuntu/Debian

```bash
# Установка зависимостей
sudo apt update
sudo apt install -y \
    build-essential cmake pkg-config \
    libtss2-dev \
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
├── CMakeLists.txt
├── README.md
├── HANDBOOK.md          # Подробная документация по разработке
├── include/
│   ├── tpm_vault.hpp    # Основной класс
│   ├── tpm_manager.hpp  # Работа с TPM через FAPI
│   ├── luks_manager.hpp # Работа с LUKS
│   ├── loop_manager.hpp # Работа с loop-устройствами
│   └── utils.hpp        # Утилиты
├── src/
│   ├── main.cpp
│   ├── tpm_vault.cpp
│   ├── tpm_manager.cpp
│   ├── luks_manager.cpp
│   ├── loop_manager.cpp
│   └── utils.cpp
└── scripts/
    └── test-in-qemu.sh  # Скрипт для тестирования в QEMU
```

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
