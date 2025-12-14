#!/bin/bash
# test-in-qemu.sh — скрипт для тестирования tpm-vault в QEMU с swtpm
#
# Использование:
#   ./scripts/test-in-qemu.sh setup    # Подготовка окружения
#   ./scripts/test-in-qemu.sh start    # Запуск swtpm и QEMU
#   ./scripts/test-in-qemu.sh stop     # Остановка
#   ./scripts/test-in-qemu.sh clean    # Очистка

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
QEMU_DIR="$PROJECT_DIR/qemu-test"

DISK_SIZE="20G"
MEMORY="4G"
CPUS="4"
SSH_PORT="2222"

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_dependencies() {
    local missing=()
    
    for cmd in qemu-system-x86_64 qemu-img swtpm; do
        if ! command -v $cmd &> /dev/null; then
            missing+=($cmd)
        fi
    done
    
    if [ ${#missing[@]} -ne 0 ]; then
        log_error "Missing dependencies: ${missing[*]}"
        echo "Install with: sudo apt install qemu-system-x86 qemu-utils swtpm"
        exit 1
    fi
    
    # Проверка OVMF
    if [ ! -f /usr/share/OVMF/OVMF_CODE_4M.fd ]; then
        log_error "OVMF not found. Install with: sudo apt install ovmf"
        exit 1
    fi
}

cmd_setup() {
    log_info "Setting up QEMU test environment in $QEMU_DIR"
    
    check_dependencies
    
    mkdir -p "$QEMU_DIR/tpmstate"
    
    # Создание диска если не существует
    if [ ! -f "$QEMU_DIR/disk.qcow2" ]; then
        log_info "Creating disk image ($DISK_SIZE)..."
        qemu-img create -f qcow2 "$QEMU_DIR/disk.qcow2" $DISK_SIZE
    else
        log_info "Disk image already exists"
    fi
    
    # Копирование OVMF_VARS
    if [ ! -f "$QEMU_DIR/OVMF_VARS.fd" ]; then
        log_info "Copying OVMF_VARS..."
        cp /usr/share/OVMF/OVMF_VARS_4M.fd "$QEMU_DIR/OVMF_VARS.fd"
        chmod +w "$QEMU_DIR/OVMF_VARS.fd"
    fi
    
    log_info "Setup complete!"
    echo ""
    echo "Next steps:"
    echo "  1. Download Ubuntu Server ISO:"
    echo "     wget https://releases.ubuntu.com/22.04/ubuntu-22.04.4-live-server-amd64.iso -O $QEMU_DIR/ubuntu.iso"
    echo ""
    echo "  2. Start VM for installation:"
    echo "     $0 start --cdrom"
    echo ""
    echo "  3. After installation, start normally:"
    echo "     $0 start"
}

cmd_start_swtpm() {
    if pgrep -f "swtpm.*$QEMU_DIR/swtpm-sock" > /dev/null; then
        log_info "swtpm already running"
        return 0
    fi
    
    log_info "Starting swtpm..."
    
    swtpm socket \
        --tpmstate dir="$QEMU_DIR/tpmstate" \
        --ctrl type=unixio,path="$QEMU_DIR/swtpm-sock",mode=0666 \
        --tpm2 \
        --log level=5 \
        --daemon
    
    sleep 1
    
    if [ -S "$QEMU_DIR/swtpm-sock" ]; then
        log_info "swtpm started successfully"
    else
        log_error "Failed to start swtpm"
        exit 1
    fi
}

cmd_start() {
    local use_cdrom=false
    
    for arg in "$@"; do
        case $arg in
            --cdrom)
                use_cdrom=true
                ;;
        esac
    done
    
    if [ ! -d "$QEMU_DIR" ]; then
        log_error "Test environment not set up. Run: $0 setup"
        exit 1
    fi
    
    cmd_start_swtpm
    
    local qemu_args=(
        -enable-kvm
        -cpu host
        -smp $CPUS
        -m $MEMORY
        -machine q35,smm=on
        -global driver=cfi.pflash01,property=secure,value=on
        -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd
        -drive if=pflash,format=raw,file="$QEMU_DIR/OVMF_VARS.fd"
        -drive file="$QEMU_DIR/disk.qcow2",format=qcow2,if=virtio,cache=writeback
        -chardev socket,id=chrtpm,path="$QEMU_DIR/swtpm-sock"
        -tpmdev emulator,id=tpm0,chardev=chrtpm
        -device tpm-crb,tpmdev=tpm0
        -nic user,model=virtio-net-pci,hostfwd=tcp::${SSH_PORT}-:22
        -device virtio-balloon
        -rtc base=utc,clock=host
        -serial mon:stdio
    )
    
    if $use_cdrom; then
        if [ ! -f "$QEMU_DIR/ubuntu.iso" ]; then
            log_error "ISO not found: $QEMU_DIR/ubuntu.iso"
            echo "Download with: wget https://releases.ubuntu.com/22.04/ubuntu-22.04.4-live-server-amd64.iso -O $QEMU_DIR/ubuntu.iso"
            exit 1
        fi
        qemu_args+=(-cdrom "$QEMU_DIR/ubuntu.iso" -boot menu=on)
        log_info "Starting QEMU with CDROM for installation..."
    else
        log_info "Starting QEMU..."
    fi
    
    echo ""
    echo "VM will be accessible via SSH: ssh -p $SSH_PORT user@localhost"
    echo "Press Ctrl+A, X to exit QEMU"
    echo ""
    
    qemu-system-x86_64 "${qemu_args[@]}"
}

cmd_stop() {
    log_info "Stopping swtpm..."
    pkill -f "swtpm.*$QEMU_DIR/swtpm-sock" || true
    rm -f "$QEMU_DIR/swtpm-sock"
    log_info "Stopped"
}

cmd_clean() {
    log_warn "This will delete all test data!"
    read -p "Are you sure? [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        cmd_stop
        rm -rf "$QEMU_DIR"
        log_info "Cleaned up"
    else
        log_info "Cancelled"
    fi
}

cmd_ssh() {
    ssh -p $SSH_PORT -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null user@localhost
}

cmd_copy() {
    if [ ! -f "$PROJECT_DIR/build/tpm-vault" ]; then
        log_error "tpm-vault not built. Run: cd build && cmake .. && make"
        exit 1
    fi
    
    log_info "Copying tpm-vault to VM..."
    scp -P $SSH_PORT -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        "$PROJECT_DIR/build/tpm-vault" user@localhost:~/
    log_info "Done! Run in VM: sudo ./tpm-vault"
}

cmd_help() {
    echo "Usage: $0 <command> [options]"
    echo ""
    echo "Commands:"
    echo "  setup       Create QEMU test environment"
    echo "  start       Start swtpm and QEMU VM"
    echo "    --cdrom   Boot from Ubuntu ISO for installation"
    echo "  stop        Stop swtpm daemon"
    echo "  clean       Remove all test data"
    echo "  ssh         Connect to VM via SSH"
    echo "  copy        Copy built tpm-vault to VM"
    echo "  help        Show this help"
    echo ""
    echo "Typical workflow:"
    echo "  1. $0 setup"
    echo "  2. Download Ubuntu ISO to qemu-test/ubuntu.iso"
    echo "  3. $0 start --cdrom   # Install Ubuntu"
    echo "  4. $0 start           # Boot installed system"
    echo "  5. $0 ssh             # Connect and install deps"
    echo "  6. $0 copy            # Copy tpm-vault binary"
}

case "${1:-help}" in
    setup)
        cmd_setup
        ;;
    start)
        shift
        cmd_start "$@"
        ;;
    stop)
        cmd_stop
        ;;
    clean)
        cmd_clean
        ;;
    ssh)
        cmd_ssh
        ;;
    copy)
        cmd_copy
        ;;
    help|--help|-h)
        cmd_help
        ;;
    *)
        log_error "Unknown command: $1"
        cmd_help
        exit 1
        ;;
esac
