ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_MACH_XIAOMI_F4) += tucana-sdmmagpie-overlay.dtbo
tucana-sdmmagpie-overlay.dtbo-base := tucana-sdmmagpie.dtb

dtbo-$(CONFIG_MACH_XIAOMI_F7B) += \
    violet-sm6150-overlay.dtbo
violet-sm6150-overlay.dtbo-base := violet-sm6150.dtb

dtbo-$(CONFIG_MACH_XIAOMI_F10) += \
    davinci-sdmmagpie-overlay.dtbo \
    davinci_m0a-sdmmagpie-overlay.dtbo \
    davinci_m0b-sdmmagpie-overlay.dtbo

davinci-sdmmagpie-overlay.dtbo-base := davinci-sdmmagpie.dtb
davinci_m0a-sdmmagpie-overlay.dtbo-base := davinci_m0a-sdmmagpie.dtb
davinci_m0b-sdmmagpie-overlay.dtbo-base := davinci_m0b-sdmmagpie.dtb

dtbo-$(CONFIG_MACH_XIAOMI_G7B) += phoenix-sdmmagpie-overlay.dtbo
phoenix-sdmmagpie-overlay.dtbo-base := phoenix-sdmmagpie.dtb
endif

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)

$(obj)/%.dtbo:$(src)/../../../../arm64/boot/dts/xiaomi/%.dts FORCE
	$(call if_changed_dep,dtc)
	$(call if_changed,dtbo_verify)

dtbs: $(addprefix $(obj)/,$(dtb-y)) $(addprefix $(obj)/,$(dtbo-y))
clean-files	:= *.dtb *.dtbo
