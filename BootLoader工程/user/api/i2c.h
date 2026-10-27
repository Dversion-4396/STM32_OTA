#ifndef _I2C_H
#define _I2C_H
#include "common.h"
#include "delay.h"
#define I2C_SCL_PIN_HIGH GPIOB->ODR |= (0X01 << 6);
#define I2C_SCL_PIN_LOW GPIOB->ODR &= ~(0X01 << 6);
#define I2C_SDA_PIN_HIGH GPIOB->ODR |= (0X01 << 7);
#define I2C_SDA_PIN_LOW GPIOB->ODR &= ~(0X01 << 7);
#define I2C_DELAY_TIME (2)
typedef enum
{
    I2C_ACK,
    I2C_NACK
} I2C_ACK_t;
/// @brief ��i2cģ����г�ʼ������
///         PB6��PB7 Ϊ��©���ģʽ����2mhz
void I2C_GPIO_Init(void);
/// @brief �˺�������ʱ���߸ߵ͵�ƽ�仯
/// @param flag 1Ϊ�ߵ�ƽ 0����͵�ƽ
void SCl_Set(u8 flag);
/// @brief �˺�������ʱ���߸ߵ͵�ƽ�仯
/// @param flag 1Ϊ�ߵ�ƽ 0����͵�ƽ
void SDA_Set(u8 flag);
/// @brief I2c��ʱ���ƺ���
void I2C_Delay(void);
/// @brief ����SDAΪ��©���ģʽ
void SDA_Output_Mode(void);
/// @brief ����SDAΪ��������ģʽ
void SDA_Input_Mode(void);
/// @brief ����I2c��ʼ�źſ���
///        1. scl ��sda�������ȶ��ĸߵ�ƽ״̬
/////      2. ��scl���ڸߵ�ƽ״̬ʱ ���sda�Ӹߵ�ƽ��Ϊ�͵�ƽ�Ĺ���
///        3. scl ��ʼ��������ڱ仯
void I2C_Start(void);
/// @brief ����i2cֹͣ�ź�
///         1. scl ��sda�������ȶ��ĸߵ�ƽ״̬
///         2. ��scl���ڸߵ�ƽ״̬ʱ ���sda�ӵ͵�ƽ��Ϊ�ߵ�ƽ�Ĺ���
///         3. ���������߽����ȶ��ߵ�ƽ״̬
void I2C_Stop(void);
/**
 * @brief ������ I2C������������Ӧ����������
 *      1. ��֤��ǰ SDA ��������ģʽ
 *      2. ��Ҫ����������ϣ����� SCL ���������Ե�ƽ�仯��
 *
 * @return ����ֵ��Ӧ I2C_ACK(0) Ӧ��ɹ���I2C_NACK(1) Ӧ��ʧ��
 */
I2C_ACK_t I2C_ReadBit(void);
/**
 * @brief ������ I2C������ 1 ���ֽ����ݣ���Ҫ���������ݰ���
 *      1. 1 �ֽ���������
 *      2. ��ǰ�������֮�󣬴ӻ���Ӧ���ź�
 *      ���� I2C ���ݷ���Ҫ��
 *          1. �������� 1��SCL �����ȶ��ߵ�ƽ״̬��ͬʱ SDA �����ȶ��ߵ�ƽ
 *          2. �������� 0��SCL �����ȶ��ߵ�ƽ״̬��ͬʱ SDA �����ȶ��͵�ƽ
 *      ��ע�⡿���е� SDA ��ƽ�仯�������� SCL �͵�ƽ��������ɡ�
 *
 * @param data ���ڷ��͵� 1 �ֽ���������
 * @return �ӻ�Ӧ����Ϣ
 */
u8 I2C_SendByte(u8 data);

u8 I2C_ReadByte(I2C_ACK_t ack);
#endif