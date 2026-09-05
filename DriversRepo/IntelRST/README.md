# Intel RST / VMD 驱动仓库（CustomPE 注入用）

每个文件夹含一个 WHQL 签署的 INF 驱动包，构建脚本会递归扫描 D:\Project\DeskUp\DriversRepo 自动注入。

| 文件夹 | INF | 驱动版本(INF 日期) | 覆盖硬件 | 来源 |
| --- | --- | --- | --- | --- |
| RST_Legacy_AHCI_17.11.3.1010 | iaAHCIC.inf (Class=HDC) | 17.11.3.1010 (2022-11-25) | CC_0106 AHCI/RST Premium：6th(9D03)、300系(A282/9DD3/A352/A353)、400系(34D3/02D3/06D2/06D3/A382) | Microsoft Update Catalog: Intel Corporation - HDC - 17.11.3.1010 |
| RST_Legacy_RAID_17.11.3.1010 | iaStorAC.inf (Class=SCSIAdapter) | 17.11.3.1010 (2022-11-25) | CC_0104 RAID：2822/282A/9D07/A286/9DD7/A356/A357/A35E/34D7/02D7/06D6/06D7/06DE/A386 + CC_0108 NVMe | Microsoft Update Catalog: Intel Corporation - SCSIAdapter - 17.11.3.1010 |
| RST_Legacy_RAID_11_18.37.6.1010 | iaStorAC.inf (Class=SCSIAdapter) | 18.37.6.1010 (2022-09-19) | CC_0104 RAID 500系：43D6(RST Premium)/43DE(RST Optane) + 300/400系 + CC_0108 NVMe | Intel SetupRST.exe 18.7.6.1010.3（WHQL 2022-10-27；目录未托管 18.x iaStorAC，此为唯一非目录渠道包） |
| RST_VMD_11_18.7.6.1010 | iaStorVD.inf (Class=SCSIAdapter) | 18.7.6.1010 (2022-09-19) | VMD 9A0B(Tiger/Rocket Lake, 11th) + 09AB managed | Microsoft Update Catalog: Intel Corporation - SCSIAdapter - 18.7.6.1010 |
| RST_VMD_12-15_20.2.6.1025 | iaStorVD.inf (Class=SCSIAdapter) | 20.2.6.1025 (2025-04-11) | VMD 467F(Alder Lake 12th)/A77F(Raptor Lake 13/14th)/7D0B(Meteor Lake & Arrow Lake H-U)/AD0B(Arrow Lake S)/09AB | Microsoft Update Catalog: Intel Corporation - SCSIAdapter - 20.2.6.1025 |

## 代际覆盖速查
- 8-9 代（300 系，桌面+移动）：RAID 用 RST_Legacy_RAID_17.11.3.1010；AHCI/RST-Premium 用 RST_Legacy_AHCI_17.11.3.1010。
- 10 代（400 系）：同上两包；Ice Lake 平台 VMD(9A0B) 由 RST_VMD_11_18.7.6.1010 覆盖。
- 11 代（Rocket/Tiger Lake）：VMD 9A0B 由 RST_VMD_11_18.7.6.1010；非 VMD 的 500 系 SATA RAID(43D6/43DE) 由 RST_Legacy_RAID_11_18.37.6.1010。
- 12-14 代及 Core Ultra 100/200：RST_VMD_12-15_20.2.6.1025（467F/A77F/7D0B/AD0B）。

## 说明
- 所有 .cat 均为 Microsoft Windows Hardware Compatibility Publisher WHQL 有效签名（本目录部署时已逐包核验）。
- MOD01D01DD00Z6002H（Dell 渠道 20.2.1.1016 VMD）已移入 D:\Backup\DriversRepo_Archive 归档，避免与 20.2.6 重复注入。
