# SPDX-License-Identifier: GPL-2.0
# UFSHCD makefile
obj-$(CONFIG_SCSI_UFS_DWC_TC_PCI) += tc-dwc-g210-pci.o ufshcd-dwc.o tc-dwc-g210.o
obj-$(CONFIG_SCSI_UFS_DWC_TC_PLATFORM) += tc-dwc-g210-pltfrm.o ufshcd-dwc.o tc-dwc-g210.o
obj-$(CONFIG_SCSI_UFS_QCOM) += ufs-qcom.o
obj-$(CONFIG_SCSI_UFSHCD) += ufshcd-core.o
ufshcd-core-y := ufshcd.o ufs-sysfs.o
obj-$(CONFIG_SCSI_UFSHCD_PCI) += ufshcd-pci.o
obj-$(CONFIG_SCSI_UFSHCD_PLATFORM) += ufshcd-pltfrm.o
obj-$(CONFIG_SCSI_UFS_TEST) += ufs_test.o
obj-$(CONFIG_DEBUG_FS) += ufs-debugfs.o ufs-qcom-debugfs.o
ufshcd-core-$(CONFIG_SCSI_UFS_CRYPTO) += ufshcd-crypto.o
ufshcd-core-$(CONFIG_SCSI_UFS_CRYPTO_QTI) += ufshcd-crypto-qti.o

ifeq ($(CONFIG_UFSFEATURE30), y)
	obj-$(CONFIG_UFSFEATURE) += ufs30/ufsfeature.o
	obj-$(CONFIG_UFSHPB) += ufs30/ufshpb.o
	obj-$(CONFIG_UFSTW) += ufs30/ufstw.o
else
	ifeq ($(CONFIG_UFSFEATURE31), y)
		obj-$(CONFIG_UFSFEATURE) += ufs31/ufsfeature.o
		obj-$(CONFIG_UFSHPB) += ufs31/ufshpb.o
		obj-$(CONFIG_UFSTW) += ufs31/ufstw.o
	endif
endif
