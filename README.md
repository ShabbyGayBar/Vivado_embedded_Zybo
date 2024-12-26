# 嵌入式系统开发 HW2
### SJTU MST-4311 智能芯片与系统设计

<p align='right'>@ShabbyGayBar</p>
<p align='right'>2024/12/26</p>

## 实验内容

设计基于Zybo的简单嵌入式系统，使用：
- 双通道GPIO
- 中断程序

实现：
- 可调周期 LED 跑马灯
- 按键中断控制 UART 输出

## 工程文件

`/`: 工程根目录
- `/README.md`: 本文档
- `/LICENSE`: 许可证
- `/report/`: 实验报告目录
  - `/main.pdf`: 实验报告 PDF
  - `/main.tex`: 实验报告 TeX 源码
  - `/setup.tex`: 实验报告 TeX 环境配置源码
  - `/figures/`: 实验报告图片目录
- `/hw2/`: Vivado 工程目录
  - `/hw2.xpr`: Vivado 工程文件
  - `/hw2.srcs/`: Vivado 工程源码目录
  - `/hw2.srcs/constrs_1/`: 约束文件目录
  - `/hw2.srcs/sources_1/`: VHDL 源码文件目录
  - `/hw2.sdk/hw2/src/`: SDK 工程源码目录
    - `/hw2.c`: SDK 工程C文件源码
- `/hw2/hw2.runs/impl_1/system_wrapper.bit`: Bitstream 文件