/*
 * Copyright (c) 2006-2020, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2020-11-27     Rick       the first version
 */
#include "led.h"
#include "water_work.h"
#include "debug.h"

static uint8_t valve_status = 0;
static uint8_t valve_valid = 1;
static uint8_t valve_check_tick = 0;

uint8_t internal_valve_open_result = 0;  // 0:normal,1:warning
uint8_t internal_valve_close_result = 0; // 0:normal,1:warning
uint8_t internal_valve_check_result = 0; // 0:normal,1:warning
uint8_t external_valve_open_result = 0;  // 0:normal,1:warning
uint8_t external_valve_close_result = 0; // 0:normal,1:warning
uint8_t external_valve_check_result = 0; // 0:normal,1:warning

uint32_t valve_tick = 0;
uint32_t valve_check_test_time = 0;

#define VALVE_STATUS_CLOSE 0
#define VALVE_STATUS_OPEN 1

struct valve_timer
{
    uint8_t status;
    uint32_t self_tick;
    uint32_t init_tick;
    uint8_t periodic;
    void (*timeout_func)(void *parameter);
};

struct valve_timer valve_open_timer = {0};
struct valve_timer valve_open_once_timer = {0};
struct valve_timer valve_close_timer = {0};
struct valve_timer valve_detect_timer = {0};
struct valve_timer valve_check_timer = {0};

extern enum Device_Status DeviceStatus;
extern WariningEvent InternalValveFailEvent;

void valve_turn_control(int dir)
{
    if (dir < 0)
    {
        valve_status = VALVE_STATUS_CLOSE;
        GPIO_WriteBit(GPIOB, GPIO_Pin_12, Bit_RESET);
    }
    else
    {
        valve_status = VALVE_STATUS_OPEN;
        GPIO_WriteBit(GPIOB, GPIO_Pin_12, Bit_SET);
    }
}

void valve_open(void)
{
    if (valve_valid == 0 || pd_chip_lock_voltage_get() < 3)
    {
        led_valve_fail();
        return;
    }

    DeviceStatus = ValveOpen;

    led_valve_on();
    beep_once();
    my_Valve_Controls_Open();
    valve_turn_control(1);

    valve_timer_stop(&valve_close_timer);
    valve_timer_stop(&valve_open_once_timer);
    valve_timer_start(&valve_open_timer);
    valve_timer_start(&valve_detect_timer);
}

void valve_close(void)
{
    if (valve_valid == 0)
    {
        led_valve_fail();
        valve_turn_control(-1);
        my_Valve_Controls_Close();
        return;
    }

    DeviceStatus = ValveClose;

    beep_key_down();
    led_valve_off();
    my_Valve_Controls_Close();
    valve_turn_control(-1);

    valve_timer_stop(&valve_open_timer);
    valve_timer_stop(&valve_open_once_timer);
    valve_timer_stop(&valve_detect_timer);
    valve_timer_start(&valve_close_timer);
}

uint8_t get_valve_status(void)
{
    return valve_status;
}

void valve_check(void)
{
    valve_check_tick = 0;
    valve_timer_stop(&valve_open_timer);
    valve_timer_stop(&valve_close_timer);
    valve_timer_stop(&valve_detect_timer);
    valve_timer_start(&valve_check_timer);
}

void valva_check_timer_callback(void *parameter)
{
    switch (valve_check_tick++)
    {
    case 0: // start turn
        if (valve_status == VALVE_STATUS_OPEN)
        {
            GPIO_WriteBit(GPIOB, GPIO_Pin_12, Bit_SET);
            my_Valve_Controls_Check();
        }
        else
        {
            valve_timer_stop(&valve_check_timer);
        }
        break;
    case 15: //如果15秒开到位就开始回转
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_4) == 0)
        {
            GPIO_WriteBit(GPIOB, GPIO_Pin_12, Bit_RESET);
        }
        else
        {
            valve_valid = 0;
            internal_valve_check_result = 1;
            valve_timer_stop(&valve_check_timer);
            warning_enable(InternalValveFailEvent);
        }
        break;
    case 20: //turn forward
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_8) == 0)//如果关到位被按下
        {
            valve_valid = 0;
            internal_valve_check_result = 1;
            valve_timer_stop(&valve_check_timer);
            warning_enable(InternalValveFailEvent);
        }
        GPIO_WriteBit(GPIOB, GPIO_Pin_12, Bit_SET);
        break;
    case 21: //check back
        if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_4) == 0)//如果开到位被按下，就报警（相当于没弹开）
        {
            valve_valid = 0;
            internal_valve_check_result = 1;
            valve_timer_stop(&valve_check_timer);
            warning_enable(InternalValveFailEvent);
        }
        break;
    case 28: // check forward
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_4) == 0)
        {
            valve_valid = 1;
            internal_valve_check_result = 0;
            valvefail_warning_disable();
        }
        else
        {
            valve_valid = 0;
            internal_valve_check_result = 1;
            valve_timer_stop(&valve_check_timer);
            warning_enable(InternalValveFailEvent);
        }
        break;
    case 30: // check external,stop all
        valve_timer_stop(&valve_check_timer);
        if (my_Valve_Get_Error() & 0x04 == 0)
        {
            external_valve_check_result = 0;
        }
        else
        {
            external_valve_check_result = 1;
        }
        break;
    default:
        break;
    }
}

void valve_open_timer_callback(void *parameter)
{
    uint8_t internal_valve_open_status = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_4);
    uint8_t external_valve_open_status = my_Valve_Get_Error() & 0x02;

    if (internal_valve_open_status == 0)
    {
        valve_valid = 1;
        internal_valve_open_result = 0;
        valvefail_warning_disable();
        printf("valve_open_timer_callback internal_valve_check success");
    }
    else
    {
        valve_valid = 0;
        internal_valve_open_result = 1;
        warning_enable(InternalValveFailEvent);
        printf("valve_open_timer_callback internal_valve_check failed");
    }

    if (external_valve_open_status == 0)
    {
        external_valve_open_result = 0;
        printf("valve_open_timer_callback external_valve_check success\r\n");
    }
    else
    {
        external_valve_open_result = 1;
        printf("valve_open_timer_callback external_valve_check failed");
    }
}

void valve_close_timer_callback(void *parameter)
{
    uint8_t internal_valve_close_status = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_8);
    uint8_t external_valve_close_status = my_Valve_Get_Error() & 0x01;
    if (internal_valve_close_status == 0)
    {
        valve_valid = 1;
        internal_valve_close_result = 0;
        valvefail_warning_disable();
        printf("valve_close_timer_callback internal_valve_check success");
    }
    else
    {
        valve_valid = 0;
        internal_valve_close_result = 1;
        warning_enable(InternalValveFailEvent);
        printf("valve_close_timer_callback internal_valve_check failed");
    }

    if (external_valve_close_status == 0)
    {
        external_valve_close_result = 0;
        printf("valve_close_timer_callback external_valve_check success\r\n");
    }
    else
    {
        external_valve_close_result = 1;
        //        gateway_warning_master_valve_check(4);
        printf("valve_close_timer_callback external_valve_check failed");
    }
}

void valve_detect_timer_callback(void *parameter)
{
    valve_check();
}

void valve_open_once_timer_callback(void *parameter)
{
    if(DeviceStatus == ValveClose || DeviceStatus == ValveOpen || DeviceStatus == MasterSensorLost)
    {
        valve_open();
    }
}

void valve_timer_check(struct valve_timer *timer)
{
    if (timer->status)
    {
        if (timer->self_tick++ >= timer->init_tick)
        {
            if (timer->periodic)
            {
                timer->self_tick = 0;
            }
            else
            {
                timer->status = 0;
            }
            timer->timeout_func(0);
        }
    }
}

void valve_timer_init(struct valve_timer *timer, void (*timeout_func)(void *parameter), uint32_t timeout, uint8_t periodic)
{
    timer->status = 0;
    timer->init_tick = timeout;
    timer->periodic = periodic;
    timer->timeout_func = timeout_func;
}

void valve_timer_start(struct valve_timer *timer)
{
    timer->self_tick = 0;
    timer->status = 1;
}

void valve_timer_stop(struct valve_timer *timer)
{
    timer->status = 0;
}

void valve_handle(void)
{
    if (valve_tick > 0)
    {
        valve_tick = 0;
        valve_timer_check(&valve_open_timer);
        valve_timer_check(&valve_open_once_timer);
        valve_timer_check(&valve_close_timer);
        valve_timer_check(&valve_detect_timer);
        valve_timer_check(&valve_check_timer);
    }
}

void valve_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8; // VALVE_ON
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4; // VALVE_OFF
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12; // RELAY_ON
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    GPIO_WriteBit(GPIOB, GPIO_Pin_12, Bit_RESET);

    valve_timer_init(&valve_open_timer, valve_open_timer_callback, 30000, 0);
    valve_timer_init(&valve_close_timer, valve_close_timer_callback, 30000, 0);
    valve_timer_init(&valve_detect_timer, valve_detect_timer_callback, 60 * 1000 * 5, 0);
    valve_timer_init(&valve_open_once_timer, valve_open_once_timer_callback, 2 * 1000, 0);
    valve_timer_init(&valve_check_timer, valva_check_timer_callback, 1000, 1);
    valve_timer_start(&valve_open_once_timer);
}

uint8_t get_valve_test_status()
{
    uint8_t data = 0;
    data |= valve_status << 1;
    data |= internal_valve_close_result << 2;
    data |= internal_valve_open_result << 3;
    data |= internal_valve_check_result << 4;
    data |= external_valve_close_result << 5;
    data |= external_valve_open_result << 6;
    data |= external_valve_check_result << 7;
    return data;
}