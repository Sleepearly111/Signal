# 常用器件与应用边界

| 器件 | 项目中的角色 | 接线和测量关注点 | 原厂资料 |
| --- | --- | --- | --- |
| STM32F407 | 分析、控制、DAC 与采集 | ADC 限幅与偏置、定时器触发、DMA、GPIO 复用 | [ST](https://www.st.com/en/microcontrollers-microprocessors/stm32f407-417.html) |
| AD9248 | 2026G 高速并行 ADC | 模块 INA/INB、真实前端增益、ADC 编码、采集时钟与数字 IO | [ADI](https://www.analog.com/en/products/ad9248.html) |
| AD8132 | AD9248 模块单端转差分输入驱动 | 共模、反馈网络、端接和输出摆幅按实物电路 | [ADI](https://www.analog.com/en/products/ad8132.html) |
| AD9280 | 高速采样桥通信基线 | 板级时钟、位序和 ADC 采样连续性 | [ADI](https://www.analog.com/en/products/ad9280.html) |
| AD9833 | 2025G/2023H DDS 激励或参考 | 时钟源决定频率字，核对模块参考时钟、FSYNC 和输出缓冲 | [ADI](https://www.analog.com/en/products/ad9833.html) |
| AD603 | 2025G 增益调节、2023D 模拟前端 | 实际增益随模块配置和控制电压变化，各样机分别标定 | [ADI](https://www.analog.com/en/products/ad603.html) |
| AD637 | 2025G 交流真有效值转直流 | 输入幅度、带宽和稳定时间相关，根据负载和峰值因数复测 | [ADI](https://www.analog.com/en/products/ad637.html) |
| ADS8688 | 扫频测幅和 AD630 直流读数 | 量程、SPI、自动/手动状态、通道丝印和实际有效采样率 | [TI](https://www.ti.com/product/ADS8688) |
| AD630 | 2023H 鉴相与 2025G 拍频辅助 | 差分输入、去直流、低通、鉴相中心和相位工作点 | [ADI](https://www.analog.com/en/products/ad630.html) |
| TLV3501 | 2023D 比较器和频率捕获 | 输入共模范围、参考阈值、滞回、输出电平和定时器输入关系 | [TI](https://www.ti.com/product/TLV3501) |
| LTC1068 | 时钟可调低通试验 | 后缀与模块决定时钟比，核对板载偏置、IO 电平和时钟输入 | [ADI](https://www.analog.com/en/products/ltc1068.html) |
| THS4001 | 2026G 历史双路增益候选 | 增益带宽、输出负载和输入/输出同步标定 | [TI](https://www.ti.com/product/THS4001) |
| AD8421 | 2026G 历史增益候选 | 饱和、增益电阻、带宽和输入共模边界 | [ADI](https://www.analog.com/en/products/ad8421.html) |

模块板的输入阻抗、电源、增益和引脚由该板电路决定。芯片手册负责核对器件极限和接口定义，调试前还应对照模块原理图或现场测量。
