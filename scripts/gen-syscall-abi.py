"""Gera constantes NASM a partir do mesmo cabecalho usado por C/userspace."""
from pathlib import Path
import re
import sys

source, destination = map(Path, sys.argv[1:])
constants = re.findall(r"^#define (USER_\w+) (\d+)\s*$", source.read_text(), re.M)
calls = [int(value) for name, value in constants if name.startswith("USER_SYSCALL_") and name != "USER_SYSCALL_COUNT"]
if len(constants) != len({name for name, _ in constants}) or sorted(calls) != list(range(len(calls))):
    raise SystemExit("ABI invalida: constantes duplicadas ou numeros de syscall nao contiguos")
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text("; Gerado de syscall_abi.h; nao editar.\n" + "".join(f"%define {name} {value}\n" for name, value in constants))
