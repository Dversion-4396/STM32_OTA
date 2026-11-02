#ifndef _LED_H
#define _LED_H

#include "common.h"

/*
	LED 驱动 —— 战神V1 板载两颗 LED
	==================================================================
	硬件（docs\战神V1引脚表.txt 第 25、73 行）:
	  LED0 红  PB5  引脚135  接DS0 LED灯(红色)  低电平点亮
	  LED1 绿  PE5  引脚4    接DS1 LED灯(绿色)  低电平点亮

	★ 为什么低电平点亮:
	  两颗灯的阳极接 3.3V，阴极经限流电阻接 IO。
	  IO 输出低 = 有压差 = 电流流过 = 亮。
	  IO 输出高 = 两端都是 3.3V = 无压差 = 灭。
	  所以初始状态必须先把 IO 置高，否则上电瞬间会闪一下。

	★ 灯的语义（约定，方便不开串口也能判断状态）:
	  绿灯 1Hz 慢闪        = 程序在跑，一切正常
	  绿灯常亮            = MQTT 已连上
	  红灯 5Hz 快闪       = 有错误（4G 失败 / Flash 失败 / OTA 失败）
	  红灯常亮            = OTA 正在下载
	  两灯交替（心跳）     = 主循环活着（Led_Heartbeat 负责）
*/

/* ---- 灯编号 ---- */
#define LED_RED     0       /* PB5 */
#define LED_GREEN   1       /* PE5 */

/* ---- 基本接口 ---- */
void Led_Init(void);
void Led_On(u8 which);
void Led_Off(u8 which);
void Led_Toggle(u8 which);

/*
	主循环心跳：在 while(1) 里每轮调用一次，内部自己节流。
	绿灯 1Hz 闪（500ms 亮 / 500ms 灭），证明主循环没卡死。
	★ 如果这个灯不闪了，说明程序卡在某处了 —— 这是最直观的"死机指示"。
*/
void Led_Heartbeat(void);

/*
	错误指示：on=1 开（红灯 5Hz 快闪），on=0 关。
	用于 4G 激活失败 / Flash 初始化失败 / OTA 下载失败。
*/
void Led_Error(u8 on);

/*
	OTA 下载指示：on=1 时红灯常亮（不闪），on=0 恢复正常心跳。
	下载过程最坏要几分钟，常亮比闪烁更能看出"正在进行"。
*/
void Led_OtaBusy(u8 on);

#endif
