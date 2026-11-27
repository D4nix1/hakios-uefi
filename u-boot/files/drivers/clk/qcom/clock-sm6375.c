// SPDX-License-Identifier: BSD-3-Clause
/*
 * Minimal Qualcomm SM6375 (Blair) GCC clock/reset support for U-Boot.
 *
 * Register offsets and clock IDs are taken from the SM6375 GCC binding and
 * Linux gcc-sm6375 driver. This deliberately does not reprogram PLLs: the
 * first experimental handoff relies on the clocks established by Motorola's
 * boot firmware. Only peripheral RCG rates and branch gates used by QUPv3
 * UART, USB3 and UFS are described here.
 */
#include <clk-uclass.h>
#include <dm.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <dt-bindings/clock/qcom,sm6375-gcc.h>
#include "clock-qcom.h"

/* SM6375 parent-map selectors differ from SM6350. */
#undef CFG_CLK_SRC_GPLL0_EVEN
#define CFG_CLK_SRC_GPLL0_EVEN (2 << 8)
#undef CFG_CLK_SRC_GPLL0_ODD
#define CFG_CLK_SRC_GPLL0_ODD (4 << 8)
#define CFG_CLK_SRC_GPLL0_MAIN (1 << 8)
#define CFG_CLK_SRC_GPLL6_EVEN (4 << 8)

#define QUPV3_WRAP1_S3_CMD_RCGR          0x533ac
#define USB30_PRIM_MASTER_CMD_RCGR       0x1a01c
#define USB30_PRIM_MOCK_UTMI_CMD_RCGR    0x1a034
#define USB3_PRIM_PHY_AUX_CMD_RCGR       0x1a060
#define UFS_PHY_AXI_CMD_RCGR             0x45020
#define UFS_PHY_ICE_CORE_CMD_RCGR        0x45048
#define UFS_PHY_PHY_AUX_CMD_RCGR         0x4507c
#define UFS_PHY_UNIPRO_CORE_CMD_RCGR     0x45060

static const struct freq_tbl ftbl_qupv3_wrap1_s3[] = {
	F(7372800, CFG_CLK_SRC_GPLL0_EVEN, 1, 384, 15625),
	F(14745600, CFG_CLK_SRC_GPLL0_EVEN, 1, 768, 15625),
	F(19200000, CFG_CLK_SRC_CXO, 1, 0, 0),
	F(29491200, CFG_CLK_SRC_GPLL0_EVEN, 1, 1536, 15625),
	F(32000000, CFG_CLK_SRC_GPLL0_EVEN, 1, 8, 75),
	F(48000000, CFG_CLK_SRC_GPLL0_EVEN, 1, 4, 25),
	F(64000000, CFG_CLK_SRC_GPLL0_EVEN, 1, 16, 75),
	F(75000000, CFG_CLK_SRC_GPLL0_EVEN, 4, 0, 0),
	F(80000000, CFG_CLK_SRC_GPLL0_EVEN, 1, 4, 15),
	F(96000000, CFG_CLK_SRC_GPLL0_EVEN, 1, 8, 25),
	F(100000000, CFG_CLK_SRC_GPLL0_EVEN, 3, 0, 0),
	F(102400000, CFG_CLK_SRC_GPLL0_EVEN, 1, 128, 375),
	F(112000000, CFG_CLK_SRC_GPLL0_EVEN, 1, 28, 75),
	F(117964800, CFG_CLK_SRC_GPLL0_EVEN, 1, 6144, 15625),
	F(120000000, CFG_CLK_SRC_GPLL0_EVEN, 2.5, 0, 0),
	F(128000000, CFG_CLK_SRC_GPLL6_EVEN, 3, 0, 0),
	{}
};

static const struct freq_tbl ftbl_usb30_prim_master[] = {
	F(66666667, CFG_CLK_SRC_GPLL0_EVEN, 4.5, 0, 0),
	F(133333333, CFG_CLK_SRC_GPLL0_MAIN, 4.5, 0, 0),
	F(200000000, CFG_CLK_SRC_GPLL0_ODD, 1, 0, 0),
	F(240000000, CFG_CLK_SRC_GPLL0_MAIN, 2.5, 0, 0),
	{}
};

static const struct freq_tbl ftbl_usb30_prim_mock_utmi[] = {
	F(19200000, CFG_CLK_SRC_CXO, 1, 0, 0),
	{}
};

static const struct freq_tbl ftbl_usb3_prim_phy_aux[] = {
	F(19200000, CFG_CLK_SRC_CXO, 1, 0, 0),
	{}
};

static const struct freq_tbl ftbl_ufs_phy_axi[] = {
	F(25000000, CFG_CLK_SRC_GPLL0_EVEN, 12, 0, 0),
	F(50000000, CFG_CLK_SRC_GPLL0_ODD, 4, 0, 0),
	F(100000000, CFG_CLK_SRC_GPLL0_ODD, 2, 0, 0),
	F(200000000, CFG_CLK_SRC_GPLL0_ODD, 1, 0, 0),
	F(240000000, CFG_CLK_SRC_GPLL0_MAIN, 2.5, 0, 0),
	{}
};

static const struct freq_tbl ftbl_ufs_phy_ice_core[] = {
	F(37500000, CFG_CLK_SRC_GPLL0_EVEN, 8, 0, 0),
	F(75000000, CFG_CLK_SRC_GPLL0_EVEN, 4, 0, 0),
	F(150000000, CFG_CLK_SRC_GPLL0_EVEN, 2, 0, 0),
	F(300000000, CFG_CLK_SRC_GPLL0_EVEN, 1, 0, 0),
	{}
};

static const struct freq_tbl ftbl_ufs_phy_phy_aux[] = {
	F(9600000, CFG_CLK_SRC_CXO, 2, 0, 0),
	F(19200000, CFG_CLK_SRC_CXO, 1, 0, 0),
	{}
};

static const struct freq_tbl ftbl_ufs_phy_unipro_core[] = {
	F(37500000, CFG_CLK_SRC_GPLL0_EVEN, 8, 0, 0),
	F(75000000, CFG_CLK_SRC_GPLL0_EVEN, 4, 0, 0),
	F(150000000, CFG_CLK_SRC_GPLL0_EVEN, 2, 0, 0),
	{}
};

static const struct gate_clk sm6375_clks[] = {
	GATE_CLK(GCC_CFG_NOC_USB3_PRIM_AXI_CLK, 0x1a084, BIT(0)),
	GATE_CLK(GCC_QUPV3_WRAP1_CORE_2X_CLK, 0x7900c, BIT(20)),
	GATE_CLK(GCC_QUPV3_WRAP1_CORE_CLK, 0x7900c, BIT(19)),
	GATE_CLK(GCC_QUPV3_WRAP1_S3_CLK, 0x7900c, BIT(24)),
	GATE_CLK(GCC_QUPV3_WRAP_1_M_AHB_CLK, 0x7900c, BIT(17)),
	GATE_CLK(GCC_QUPV3_WRAP_1_S_AHB_CLK, 0x7900c, BIT(18)),
	GATE_CLK(GCC_SYS_NOC_UFS_PHY_AXI_CLK, 0x45098, BIT(0)),
	GATE_CLK(GCC_SYS_NOC_USB3_PRIM_AXI_CLK, 0x1a080, BIT(0)),
	GATE_CLK(GCC_UFS_MEM_CLKREF_CLK, 0x8c000, BIT(0)),
	GATE_CLK(GCC_UFS_PHY_AHB_CLK, 0x45014, BIT(0)),
	GATE_CLK(GCC_UFS_PHY_AXI_CLK, 0x45010, BIT(0)),
	GATE_CLK(GCC_UFS_PHY_ICE_CORE_CLK, 0x45044, BIT(0)),
	GATE_CLK(GCC_UFS_PHY_PHY_AUX_CLK, 0x45078, BIT(0)),
	GATE_CLK(GCC_UFS_PHY_RX_SYMBOL_0_CLK, 0x4501c, BIT(0)),
	GATE_CLK(GCC_UFS_PHY_TX_SYMBOL_0_CLK, 0x45018, BIT(0)),
	GATE_CLK(GCC_UFS_PHY_UNIPRO_CORE_CLK, 0x45040, BIT(0)),
	GATE_CLK(GCC_USB30_PRIM_MASTER_CLK, 0x1a010, BIT(0)),
	GATE_CLK(GCC_USB30_PRIM_MOCK_UTMI_CLK, 0x1a018, BIT(0)),
	GATE_CLK(GCC_USB30_PRIM_SLEEP_CLK, 0x1a014, BIT(0)),
	GATE_CLK(GCC_USB3_PRIM_CLKREF_CLK, 0x8c010, BIT(0)),
	GATE_CLK(GCC_USB3_PRIM_PHY_COM_AUX_CLK, 0x1a054, BIT(0)),
	GATE_CLK(GCC_USB3_PRIM_PHY_PIPE_CLK, 0x1a058, BIT(0)),
};

static const struct qcom_reset_map sm6375_gcc_resets[] = {
	[GCC_MMSS_BCR] = { 0x17000 },
	[GCC_USB30_PRIM_BCR] = { 0x1a000 },
	[GCC_USB3_PHY_PRIM_SP0_BCR] = { 0x1b000 },
	[GCC_USB3_DP_PHY_PRIM_BCR] = { 0x1b020 },
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x1c000 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0x1c004 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x1d000 },
	[GCC_SDCC2_BCR] = { 0x1e000 },
	[GCC_QUPV3_WRAPPER_0_BCR] = { 0x1f000 },
	[GCC_PDM_BCR] = { 0x20000 },
	[GCC_GPU_BCR] = { 0x36000 },
	[GCC_SDCC1_BCR] = { 0x38000 },
	[GCC_UFS_PHY_BCR] = { 0x45000 },
	[GCC_CAMSS_TFE_BCR] = { 0x52000 },
	[GCC_QUPV3_WRAPPER_1_BCR] = { 0x53000 },
	[GCC_CAMSS_OPE_BCR] = { 0x55000 },
	[GCC_CAMSS_TOP_BCR] = { 0x58000 },
	[GCC_VENUS_BCR] = { 0x58078 },
	[GCC_VCODEC0_BCR] = { 0x58094 },
	[GCC_VIDEO_INTERFACE_BCR] = { 0x6e000 },
};

static const struct qcom_power_map sm6375_gdscs[] = {
	[USB30_PRIM_GDSC] = { 0x1a004 },
	[UFS_PHY_GDSC] = { 0x45004 },
};

static bool sm6375_is_rcg_id(ulong id)
{
	switch (id) {
	case GCC_QUPV3_WRAP1_S3_CLK_SRC:
	case GCC_USB30_PRIM_MASTER_CLK_SRC:
	case GCC_USB30_PRIM_MOCK_UTMI_CLK_SRC:
	case GCC_USB3_PRIM_PHY_AUX_CLK_SRC:
	case GCC_UFS_PHY_AXI_CLK_SRC:
	case GCC_UFS_PHY_ICE_CORE_CLK_SRC:
	case GCC_UFS_PHY_PHY_AUX_CLK_SRC:
	case GCC_UFS_PHY_UNIPRO_CORE_CLK_SRC:
		return true;
	default:
		return false;
	}
}

static int sm6375_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	/* RCG source clocks have no separate branch gate. */
	if (sm6375_is_rcg_id(clk->id))
		return 0;

	return qcom_gate_clk_en(priv, clk->id);
}

static ulong sm6375_set_rate(struct clk *clk, ulong rate)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);
	const struct freq_tbl *freq;

	switch (clk->id) {
	case GCC_QUPV3_WRAP1_S3_CLK:
	case GCC_QUPV3_WRAP1_S3_CLK_SRC:
		freq = qcom_find_freq(ftbl_qupv3_wrap1_s3, rate);
		if (!freq)
			return 0;
		clk_rcg_set_rate_mnd(priv->base, QUPV3_WRAP1_S3_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n,
				     freq->src, 16);
		return freq->freq;
	case GCC_USB30_PRIM_MASTER_CLK:
	case GCC_USB30_PRIM_MASTER_CLK_SRC:
		freq = qcom_find_freq(ftbl_usb30_prim_master, rate);
		if (!freq)
			return 0;
		clk_rcg_set_rate_mnd(priv->base, USB30_PRIM_MASTER_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n,
				     freq->src, 8);
		return freq->freq;
	case GCC_USB30_PRIM_MOCK_UTMI_CLK:
	case GCC_USB30_PRIM_MOCK_UTMI_CLK_SRC:
		freq = qcom_find_freq(ftbl_usb30_prim_mock_utmi, rate);
		if (!freq)
			return 0;
		clk_rcg_set_rate(priv->base, USB30_PRIM_MOCK_UTMI_CMD_RCGR,
				 freq->pre_div, freq->src);
		return freq->freq;
	case GCC_USB3_PRIM_PHY_COM_AUX_CLK:
	case GCC_USB3_PRIM_PHY_AUX_CLK_SRC:
		freq = qcom_find_freq(ftbl_usb3_prim_phy_aux, rate);
		if (!freq)
			return 0;
		clk_rcg_set_rate(priv->base, USB3_PRIM_PHY_AUX_CMD_RCGR,
				 freq->pre_div, freq->src);
		return freq->freq;
	case GCC_UFS_PHY_AXI_CLK:
	case GCC_UFS_PHY_AXI_CLK_SRC:
		freq = qcom_find_freq(ftbl_ufs_phy_axi, rate);
		if (!freq)
			return 0;
		clk_rcg_set_rate_mnd(priv->base, UFS_PHY_AXI_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n,
				     freq->src, 8);
		return freq->freq;
	case GCC_UFS_PHY_ICE_CORE_CLK:
	case GCC_UFS_PHY_ICE_CORE_CLK_SRC:
		freq = qcom_find_freq(ftbl_ufs_phy_ice_core, rate);
		if (!freq)
			return 0;
		clk_rcg_set_rate(priv->base, UFS_PHY_ICE_CORE_CMD_RCGR,
				 freq->pre_div, freq->src);
		return freq->freq;
	case GCC_UFS_PHY_PHY_AUX_CLK:
	case GCC_UFS_PHY_PHY_AUX_CLK_SRC:
		freq = qcom_find_freq(ftbl_ufs_phy_phy_aux, rate);
		if (!freq)
			return 0;
		clk_rcg_set_rate(priv->base, UFS_PHY_PHY_AUX_CMD_RCGR,
				 freq->pre_div, freq->src);
		return freq->freq;
	case GCC_UFS_PHY_UNIPRO_CORE_CLK:
	case GCC_UFS_PHY_UNIPRO_CORE_CLK_SRC:
		freq = qcom_find_freq(ftbl_ufs_phy_unipro_core, rate);
		if (!freq)
			return 0;
		clk_rcg_set_rate(priv->base, UFS_PHY_UNIPRO_CORE_CMD_RCGR,
				 freq->pre_div, freq->src);
		return freq->freq;
	default:
		return 0;
	}
}

static struct msm_clk_data sm6375_gcc_data = {
	.resets = sm6375_gcc_resets,
	.num_resets = ARRAY_SIZE(sm6375_gcc_resets),
	.clks = sm6375_clks,
	.num_clks = ARRAY_SIZE(sm6375_clks),
	.power_domains = sm6375_gdscs,
	.num_power_domains = ARRAY_SIZE(sm6375_gdscs),
	.enable = sm6375_enable,
	.set_rate = sm6375_set_rate,
};

static const struct udevice_id gcc_sm6375_of_match[] = {
	{
		.compatible = "qcom,sm6375-gcc",
		.data = (ulong)&sm6375_gcc_data,
	},
	{
		.compatible = "qcom,blair-gcc",
		.data = (ulong)&sm6375_gcc_data,
	},
	{}
};

U_BOOT_DRIVER(gcc_sm6375) = {
	.name = "gcc_sm6375",
	.id = UCLASS_NOP,
	.of_match = gcc_sm6375_of_match,
	.bind = qcom_cc_bind,
	.flags = DM_FLAG_PRE_RELOC,
};
