include toolchain.mk

.PHONY: check-toolchain
check-toolchain:
	bash scripts/check-toolchain.sh "$(CC)" "$(AS)" "$(LD)" "$(ARCH)" "$(CFLAGS)" "$(ASFLAGS)" "$(LDFLAGS)"
