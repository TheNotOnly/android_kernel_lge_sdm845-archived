#
# Makefile for USB Power Delivery drivers
#

obj-$(CONFIG_USB_PD_POLICY)	+= policy_engine.o
obj-$(CONFIG_QPNP_USB_PDPHY)	+= qpnp-pdphy.o

ccflags-$(CONFIG_LGE_USB_LGMD_LEGACY)  += -I$(srctree)/drivers/usb/misc/lgmd
