/* include/linux/i2c/bh1726_driver.h - ROHM bh1726 Linux kernel driver
 *
 * Copyright (C) 2012-2018
 * Written by Aaron Liu <aaron-liu@rohm.com.cn>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __BH1726_DRIVER_H__
#define __BH1726_DRIVER_H__

/*************** Definitions ******************/
/* GENERAL */
#define BH1726_DRV_NAME "bh1726"
#define DRIVER_VERSION    "1.0"
#define QCOM_SENSORS       (0)

/*-----------------------DEBUG------------------------------*/
#define BH1726_DGB_SWITCH         // debug switch
#define BH1726_TAG             "[ALS/PS]BH1726"

#ifdef BH1726_DGB_SWITCH
#define BH1726_DEBUG   1
#else
#define BH1726_DEBUG   0
#endif

#define  BH1726_ERR(f, a...)        do {printk(KERN_ERR BH1726_TAG "ERROR (%s(), %d):"   f, __func__,  __LINE__, ## a);} while (0)
#define  BH1726_WARNING(f, a...)    do {printk(KERN_WARNING BH1726_TAG "(%s(), %d):"         f, __func__,  __LINE__, ## a);} while (0)
#define  BH1726_INFO(f, a...)       do {printk(KERN_INFO BH1726_TAG "INFO (%s(), %d):"    f, __func__,  __LINE__, ## a);} while (0)


#if BH1726_DEBUG
#define  BH1726_FUN()               do {printk(KERN_INFO BH1726_TAG "(%s(), %d)\n",          __func__,  __LINE__);} while (0)
#define  BH1726_DBG(f, a...)        do {printk(KERN_DEBUG BH1726_TAG "DEBUG (%s(), %d):"   f, __func__,  __LINE__, ## a);} while (0)
#else
#define  BH1726_FUN()   do {} while (0)
#define  BH1726_DBG(f, a...)   do {} while (0)
#endif
/*-----------------------------------------------------*/


/************ define register for IC ************/
/* BH1726 REGSTER */
#define REG_INTERRUPT_RST      (0xE1)
#define REG_SOFT_RST           (0xE4)
#define REG_CONTROL            (0x80)
#define REG_TIMING             (0x81)
#define REG_INTERRUPT          (0x82)
#define REG_THRED_LOW          (0x83)
#define REG_THRED_HIGH         (0x85)
#define REG_GAIN               (0x87)
#define REG_PART_ID            (0x92)
#define REG_DATA0              (0x94)
#define REG_DATA1              (0x96)
#define REG_WAIT               (0x98)


#define PART_ID_VALUE          (0x72)


/************ define parameter for register ************/
#define ADC_EN_ON          (1 << 1)
#define ADC_EN_OFF         (0 << 1)
#define POWER_ON           (1 << 0)
#define POWER_OFF          (0 << 0)

#define BH1726_ENABLE      (ADC_EN_ON  | POWER_ON)
#define BH1726_DISABLE     (ADC_EN_OFF | POWER_OFF)

#define CLR_ADC_POWER      (0xFC)
#define CLR_GAIN0          (0xF3)
#define CLR_GAIN1          (0xFC)


#define ALS_VALID_HIGH        (1 << 4)

//////////////////
#define DATA0_VALUE_40000       (40000)
#define DATA0_VALUE_10000       (10000)
#define DATA0_VALUE_1400        (1400)
#define DATA0_VALUE_700         (700)
#define DATA0_VALUE_400         (400)
#define DATA0_VALUE_120         (120)
#define DATA0_VALUE_40          (40)



/* Gain (0x87) */
#define DATA0_GAIN_X1      (0x00 << 2)
#define DATA1_GAIN_X1      (0x00)

#define DATA0_GAIN_X2      (0x01 << 2)
#define DATA1_GAIN_X2      (0x01)

#define DATA0_GAIN_X64     (0x02 << 2)
#define DATA1_GAIN_X64     (0x02)

#define DATA0_GAIN_X128    (0x03 << 2)
#define DATA1_GAIN_X128    (0x03)


#define JUDGE_FIXED_COEF   (1000)
#define MAX_OUTRANGE       (65535)  
#define TIME_COEF          (102600) // 102.6ms * 1000, unit is us
#define TINT_TYP           (28) // 2.8 * 10
#define TINT_DIV           (10) //
#define INTEGRAL_COEF      (964)
#define TIME_BASE_CALC     (256L) //
#define USE_TIME_EXECUTE   (714)
#define MEASUREMENTS_WIDTH (10)  // rate down, unit is %
#define MAX_MEASURE_VAL    (760000000UL)
#define IC_INTERNAL_TIME   ((TINT_TYP * USE_TIME_EXECUTE) / TINT_DIV)


/* Time(0x81) */
/* measure time = (2.8*964*(256-X)+2.8*714)/1000, X is the value of 0x81 register  */
/* reg_value  = 256-time*1000/2.8/964    */
#define MEASURE_25MS       (25)
#define MEASURE_50MS       (50)
#define MEASURE_100MS      (100)
#define MEASURE_135MS      (135)
#define MEASURE_300MS      (300)
#define MEASURE_400MS      (400)
#define MEASURE_700MS      (691)
#define MEASURE_MAX_TIME   (MEASURE_700MS)

/* POWER SUPPLY VOLTAGE RANGE */
#define BH1726_VDD_MIN_UV  (2000000)
#define BH1726_VDD_MAX_UV  (3300000)
#define BH1726_VIO_MIN_UV  (1750000)
#define BH1726_VIO_MAX_UV  (1950000)

#define ALS_SET_MIN_DELAY_TIME  (100)



/*************** Structs ******************/
typedef struct {
    struct i2c_client *client;
    struct regulator *vdd;
    struct regulator *vio;
    struct mutex update_lock;

    struct delayed_work als_dwork; /* for ALS polling */

    struct input_dev *input_dev_als;

#if QCOM_SENSORS
    struct sensors_classdev als_cdev;
#endif

    unsigned int enable;
    
    unsigned int als_time;


    /* control flag from HAL */
    unsigned int enable_als_sensor;
    unsigned int als_suspend_flag;


    /* ALS parameters */
    unsigned int als_data;      /* to store ALS data */
    unsigned int als_level;
    unsigned int gain;
    unsigned int als_poll_delay;

    unsigned int dev_id;
}BH1726_ALS_DATA;

/* structure to read data value from sensor */
typedef struct {
    unsigned int data0;
    unsigned int data1;
    } READ_DATA_ARG;

typedef struct  {
    unsigned long long data0;
    unsigned long long data1;
    unsigned char gain_data0;
    unsigned char gain_data1;
    unsigned long  als_time; 
    unsigned int als_data0;
    unsigned int als_data1;
    }CALC_DATA;

#endif
