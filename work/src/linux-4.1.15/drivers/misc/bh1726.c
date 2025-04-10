/* Copyright (C) 2012-2018
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
/*
 *  This is Linux kernel modules for ambient light sensor
 *  Revision History
 *  2018-7-15:    Ver. 1.0    New release 
 */
#include <linux/debugfs.h>
//#include <linux/wakelock.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/of_gpio.h>
#include <asm/div64.h>
#include <linux/math64.h>

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#endif

#include "linux/bh1726.h"

#if QCOM_SENSORS
#include <linux/sensors.h>
#endif


/*************** Global Data ******************/
////////////////////////////////////////////////////////////////

#define     COEFFICIENT_SIZE               (4)
#define     GAIN_FACTOR                    (1) //(128), this value is related with gain when make coefficient   
const int  judge_coefficient[COEFFICIENT_SIZE] = {157, 261, 1121, 4910};
const int  data0_coefficient[COEFFICIENT_SIZE] = {159, 403, 256,  221}; 
const int  data1_coefficient[COEFFICIENT_SIZE] = {941, -615,-84,  -45};

/* gain table */
#define GAIN_FACTOR_SIZE  (16)
static const struct GAIN_TABLE {
    unsigned char data0;
    unsigned char data1;
} gain_table[GAIN_FACTOR_SIZE] = {
    {  1,   1},   /*  0 */
    {  1,   2},   /*  1 */
    {  1,  64},   /*  2 */
    {  1, 128},   /*  3 */
    {  2,   1},   /*  4 */
    {  2,   2},   /*  5 */
    {  2,  64},   /*  6 */
    {  2, 128},   /*  7 */
    { 64,   1},   /*  8 */
    { 64,   2},   /*  9 */
    { 64,  64},   /* 10 */
    { 64, 128},   /* 11 */
    {128,   1},   /* 12 */
    {128,   2},   /* 13 */
    {128,  64},   /* 14 */
    {128, 128}    /* 15 */
};


#if QCOM_SENSORS

/* static struct bh1726_chip *pdev_data;*/
static struct sensors_classdev sensors_light_cdev = {
    .name = "lightsensor",
    .vendor = "rohm",
    .version = 1,
    .handle = SENSORS_LIGHT_HANDLE,
    .type = SENSOR_TYPE_LIGHT,
    .max_range = "43000",
    .resolution = "0.0125",
    .sensor_power = "0.20",
    .min_delay = 1000, /* in microseconds */
    .fifo_reserved_event_count = 0,
    .fifo_max_event_count = 0,
    .enabled = 0,
    .delay_msec = 100,
    .sensors_enable = NULL,
    .sensors_poll_delay = NULL,
    .flags = 3,
    .max_latency = 0,
};

#endif

/******************************************************************************
 * NAME       : bh1726_driver_reset
 * FUNCTION   : reset BH1726 register
 * REMARKS    :
 *****************************************************************************/
static int bh1726_driver_reset(struct i2c_client *client)
{
    int result = 0;        

    if (NULL == client)
    {
        BH1726_ERR(" Parameter error \n");
        return EINVAL;
    }

    /* set interrupt and soft reset */
    result  = i2c_smbus_write_byte(client, REG_INTERRUPT_RST);    
    result |= i2c_smbus_write_byte(client, REG_SOFT_RST);

    return (result);
}

static unsigned char bh1726_calculate_itime_register(unsigned int time_value)
{
    unsigned char time_reg ;

    if( time_value < MEASURE_MAX_TIME)
        time_reg = TIME_BASE_CALC - time_value * JUDGE_FIXED_COEF * TINT_DIV / ( TINT_TYP * INTEGRAL_COEF)  ;
    else 
        time_reg = TIME_BASE_CALC - MEASURE_100MS * JUDGE_FIXED_COEF * TINT_DIV / ( TINT_TYP * INTEGRAL_COEF);  //set 100ms as default

    return (time_reg);
}

/******************************************************************************
 * NAME       : bh1726_driver_set_measure_time
 * FUNCTION   : set als measure time
 * REMARKS    :
 *****************************************************************************/
static int bh1726_driver_set_measure_time(BH1726_ALS_DATA *als, unsigned int time_value)
{
    int result = 0;
    unsigned char time_reg = 0;

    if(!als){
        BH1726_ERR(" Parameter error \n");
        return EINVAL;
    }

    time_reg  = bh1726_calculate_itime_register(time_value);
    result = i2c_smbus_write_byte_data(als->client, REG_TIMING, time_reg);
    if(result == 0){
        als->als_time = time_value; //save time
    }

    BH1726_WARNING(" time_value=%d, time_reg=%d, als->als_time=%d \n", time_value, time_reg, als->als_time );

    return result;

}


/**
 * @Brief: bh1726_register_dump print register value for debug
 *
 * @Param: reg_address regsiter address
 *
 * @return: return register's value
 */
static int bh1726_register_dump(BH1726_ALS_DATA * data, int addr)
{
    int  read_data = 0;

    if (!data || !data->client){
        printk(" Parameter error \n");
        return -EINVAL;;
    }

    if( addr >  REG_WAIT || addr < REG_CONTROL){
        printk( "reg =0x%x  is out of range!!!", addr);
        return -EINVAL;
    }

    /*read */
    read_data = i2c_smbus_read_byte_data(data->client, addr);

    if (read_data < 0) {
        printk( "bh1726_driver_general_read : transfer error \n");
        return -EINVAL;

    }

    printk( "reg(0x%x) = 0x%x \n", addr, read_data);

    return read_data;
}


static unsigned long bh1726_calculate_itime_us(unsigned short time_reg)
{
    unsigned long time_value;

    time_value = (TINT_TYP * INTEGRAL_COEF * (TIME_BASE_CALC - time_reg)) / TINT_DIV;

    return (time_value);
}


/**
 * @Brief: bh1726_calculate_light Calculate lux base on rgbc
 *
 * @Param: data RGBC value from sensor
 *
 * @Returns: lux value or failed.
 */
static int bh1726_calculate_light(READ_DATA_ARG data, unsigned char gain, unsigned short time_reg)
{    

    int index = 0;
    long long lx, lx_tmp;

    long  calculated_judge; 

    CALC_DATA  calculated_data;

    /* set the value of measured als data */
    calculated_data.als_time   = bh1726_calculate_itime_us(time_reg);
    calculated_data.gain_data0 = gain_table[gain&0x0F].data0;
    calculated_data.gain_data1 = gain_table[gain&0x0F].data1;
    calculated_data.als_data0  = data.data0;
    calculated_data.als_data1  = data.data1;

    if(!calculated_data.gain_data0 || !calculated_data.gain_data1){
        BH1726_WARNING("error. gain is 0\n");
        return 0;
    }
        

    if(data.data0 == 0xFFFF){
        BH1726_WARNING("Data0 is 0xFFFF, return max lux 65535.");
        return MAX_OUTRANGE;
    }

    calculated_judge = calculated_data.als_data1 * JUDGE_FIXED_COEF;

    do {
        if ( calculated_judge  < (unsigned long)(judge_coefficient[index] * calculated_data.als_data0)) {
            break;
        } else {
            index++;
        }
    } while (index < COEFFICIENT_SIZE);


    if(index < COEFFICIENT_SIZE){
        //=============================================================================================
        // lx = (coeff_d0 * data0 + coeff_d1 * data1) * (102.6 / 134.96) * (10000 / 10000) * (1 / 1000)
        //    = (coeff_d0 * data0 + coeff_d1 * data1) * 1000 * (1026 / 13496) * (1 / 100000)
        //    = (coeff_d0 * data0 + coeff_d1 * data1) * 1000 * (674800 * 1000 / 513)
        //    = (coeff_d0 * data0 + coeff_d1 * data1) * 1000 / 1315399
        //=============================================================================================
        lx_tmp = ((GAIN_FACTOR * data0_coefficient[index] * (int)calculated_data.als_data0) / (int)calculated_data.gain_data0)
               + ((GAIN_FACTOR * data1_coefficient[index] * (int)calculated_data.als_data1) / (int)calculated_data.gain_data1); // coeff_d0 and coeff_d1 are already x1000
        
        if(lx_tmp < 0){
            lx_tmp = 0;
        }

       // lx = lx_tmp * TIME_COEF / (long long)calculated_data.als_time / JUDGE_FIXED_COEF;
	lx_tmp = div_u64(lx_tmp*TIME_COEF,calculated_data.als_time);
	lx = div_u64(lx_tmp,JUDGE_FIXED_COEF);
    }
    else{
        lx = 0;
    }


   /* BH1726_WARNING("lux:%lld, index=%d, data0=%d, data1=%d, gain0=%d, gain1=%d, als_time:%ld(us)\n",
            lx, index,
            data.data0, data.data1,
            calculated_data.gain_data0, calculated_data.gain_data1,
            calculated_data.als_time);*/

    return (int)(lx);
}


static unsigned int bh1726_als_data_to_level(unsigned int als_data)
{
    return als_data;
}




void bh1726_auto_change_measure_time(BH1726_ALS_DATA * als, READ_DATA_ARG data)
{

    unsigned int time = 0, data0 = 0;

    if (!als)
    {
        BH1726_ERR(" Parameter error \n");
        return ;
    }

    time = als->als_time;
    data0 = data.data0;


    if(data0 >= DATA0_VALUE_40000 && time == MEASURE_100MS){
        //set to 25ms
        bh1726_driver_set_measure_time(als, MEASURE_25MS);
    }
    else if((data0 < DATA0_VALUE_400 && time == MEASURE_25MS)
            || (data0 >= 1400 && time == MEASURE_400MS)){
        //set to 100ms
        bh1726_driver_set_measure_time(als, MEASURE_100MS);
    }else if ((data0 < DATA0_VALUE_120 && time == MEASURE_100MS)
            ||(data0 >= DATA0_VALUE_700 && time == MEASURE_700MS )){
        //set to 400ms
        bh1726_driver_set_measure_time(als, MEASURE_400MS);
    }else if (data0 < DATA0_VALUE_40 && time == MEASURE_400MS ){
        //set to 700ms
        bh1726_driver_set_measure_time(als, MEASURE_700MS);

    }

}



/* ALS polling routine */
static void bh1726_als_polling_work_handler(struct work_struct *work)
{
    BH1726_ALS_DATA * als = container_of(work,
            BH1726_ALS_DATA, als_dwork.work);
    struct i2c_client *client = als->client;
    int tmp = 0;

    //get valid from REG_CONTROL(0x80)
    tmp = i2c_smbus_read_byte_data(client, REG_CONTROL);
    if (tmp < 0)
    {
        BH1726_ERR("Read data from IC error.\n");
        return ;
    }

    //BH1726_WARNING("Data valid REG_MODECONTROL2(0x%x) = 0x%x\n", REG_MODECONTROL2, result);
    if ((tmp & ALS_VALID_HIGH) == 0)//not valid
    {
        BH1726_WARNING("Data Not valid. But it does not matter, please ignore it.\n");
    }
    else
    {
        READ_DATA_ARG  data = {0};
        unsigned char       gain;
        unsigned short      time_reg;

        //read data0
        tmp = i2c_smbus_read_word_data(client, REG_DATA0);
        if (tmp < 0){
            BH1726_DBG("%s: i2c read data0 fail.\n", __func__);
            return ;
        }
        data.data0 = (unsigned int)tmp;

        //read data1
        tmp = i2c_smbus_read_word_data(client, REG_DATA1);
        if (tmp < 0){
            BH1726_DBG("%s: i2c read data1 fail.\n", __func__);
            return ;
        }
        data.data1 = (unsigned int)tmp;


        //read gain
        tmp = i2c_smbus_read_byte_data(client, REG_GAIN);
        if (tmp < 0){
            BH1726_DBG("%s: i2c read gain fail.\n", __func__);
            return ;
        }
        gain = (unsigned char)tmp;

        //read time_reg
        tmp = i2c_smbus_read_byte_data(client, REG_TIMING);
        if (tmp < 0){
            BH1726_DBG("%s: i2c read time fail.\n", __func__);
            return ;
        }
        time_reg = (unsigned short)tmp;

        als->als_data = bh1726_calculate_light(data, gain, time_reg);

        //measure time change auto according to data0
        bh1726_auto_change_measure_time(als, data);

        if (als->als_data == 0){
            als->als_data++;
        }
        
        als->als_level = bh1726_als_data_to_level(als->als_data);

    }
    
    input_report_abs(als->input_dev_als,  ABS_MISC, als->als_level);
    input_sync(als->input_dev_als);

    schedule_delayed_work(&als->als_dwork, msecs_to_jiffies(als->als_poll_delay));

}




static int bh1726_enable_als_sensor(struct i2c_client *client, int val)
{
    BH1726_ALS_DATA *als = i2c_get_clientdata(client);

    int result = 0;
    unsigned char power_set;
    unsigned char write_data;

    if (!als || ( val != POWER_ON && val != POWER_OFF))
    {
        BH1726_ERR(" Parameter error \n");
        return EINVAL;
    }

    BH1726_WARNING(" val=%d\n", val);

    mutex_lock(&als->update_lock);

    /* read  control1 register */
    result = i2c_smbus_read_byte_data(als->client, REG_CONTROL);
    if (result < 0) {
        mutex_unlock(&als->update_lock);  
        /* i2c communication error */
        return (result);
    }

    if (val == POWER_ON) {
        power_set = BH1726_ENABLE;
        if (als->enable_als_sensor == POWER_OFF) {
            als->enable_als_sensor = POWER_ON;
            
            //mask adc_en and power 
            write_data  = ((unsigned char)(result & CLR_ADC_POWER)) | power_set;
            result = i2c_smbus_write_byte_data(als->client, REG_CONTROL, write_data);
            if (result < 0) {
                // i2c communication error 
                BH1726_ERR(" I2C write error \n");
                mutex_unlock(&als->update_lock);  
                return (result);
            }
        }
        cancel_delayed_work(&als->als_dwork);
        schedule_delayed_work(&als->als_dwork, msecs_to_jiffies(als->als_poll_delay));
        
    } else {
        power_set = BH1726_DISABLE;
        if (als->enable_als_sensor == POWER_ON) {
            als->enable_als_sensor = POWER_OFF;

            //mask adc_en and power 
            write_data  = ((unsigned char)(result & CLR_ADC_POWER)) | power_set;
            result = i2c_smbus_write_byte_data(als->client, REG_CONTROL, write_data);
            if (result < 0) {
                // i2c communication error 
                BH1726_ERR(" I2C write error \n");
                mutex_unlock(&als->update_lock);  
                return (result);
            }
        }
        cancel_delayed_work(&als->als_dwork);
    }

    mutex_unlock(&als->update_lock);

    return result;
}

static ssize_t bh1726_show_enable_als_sensor(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    BH1726_ALS_DATA *als = i2c_get_clientdata(client);

    return snprintf(buf, PAGE_SIZE, "%d\n", als->enable_als_sensor);
}

static ssize_t bh1726_store_enable_als_sensor(struct device *dev,
        struct device_attribute *attr,
        const char *buf, size_t count)
{
    struct i2c_client *client = to_i2c_client(dev);
    BH1726_ALS_DATA *als = i2c_get_clientdata(client);
    /*unsigned long val = simple_strtoul(buf, NULL, 10);*/
    unsigned long val;
    int result = 0;
    unsigned char power_set;
    unsigned char write_data;

    result = kstrtoul(buf, 10, &val);
    if (result)
        return result;

    if ((val != POWER_ON) && (val != POWER_OFF)) {
        BH1726_DBG("%s: enable als sensor=%ld\n", __func__, val);
        return count;
    }

    bh1726_enable_als_sensor(client, val);

    return count;
}

#if QCOM_SENSORS

static int bh1726_set_als_poll_delay(struct i2c_client *client,
        unsigned int val)
{
    BH1726_ALS_DATA *als = i2c_get_clientdata(client);

    if (val < ALS_SET_MIN_DELAY_TIME * 1000)
        val = ALS_SET_MIN_DELAY_TIME * 1000;

    mutex_lock(&als->update_lock);

    als->als_poll_delay = val / 1000;    /* convert us => ms */

    if (als->enable_als_sensor == 1) {
        cancel_delayed_work(&als->als_dwork);
        schedule_delayed_work(&als->als_dwork,
                msecs_to_jiffies(als->als_poll_delay));
    }
    mutex_unlock(&als->update_lock);

    return 0;
}



static int bh1726_als_set_enable(struct sensors_classdev *sensors_cdev,
        unsigned int enable)
{
    BH1726_ALS_DATA *als = container_of(sensors_cdev,
            BH1726_ALS_DATA, als_cdev);

    if ((enable != POWER_ON) && (enable != POWER_OFF)) {
        pr_err("%s: invalid value(%d)\n", __func__, enable);
        return -EINVAL;
    }

    return bh1726_enable_als_sensor(als->client, enable);
}

static int bh1726_als_poll_delay(struct sensors_classdev *sensors_cdev,
        unsigned int delay_msec)
{
    BH1726_ALS_DATA *als = container_of(sensors_cdev,
            BH1726_ALS_DATA, als_cdev);
    bh1726_set_als_poll_delay(als->client, delay_msec);
    return 0;
}
#endif

/******************************************************************************/

static ssize_t bh1726_show_als_poll_delay(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    BH1726_ALS_DATA *als = i2c_get_clientdata(client);

    return snprintf(buf, PAGE_SIZE, "%d\n", als->als_poll_delay*1000);
}

static ssize_t bh1726_store_als_poll_delay(struct device *dev,
        struct device_attribute *attr,
        const char *buf, size_t count)
{
    struct i2c_client *client = to_i2c_client(dev);
    BH1726_ALS_DATA *als = i2c_get_clientdata(client);
    /*unsigned long val = simple_strtoul(buf, NULL, 10);*/
    unsigned long val;
    int error;

    error = kstrtoul(buf, 10, &val);
    if (error)
        return error;

    if (val < ALS_SET_MIN_DELAY_TIME * 1000)
        val = ALS_SET_MIN_DELAY_TIME * 1000;

    mutex_lock(&als->update_lock);

    als->als_poll_delay = val / 1000;

    if (als->enable_als_sensor == POWER_ON) {
        cancel_delayed_work(&als->als_dwork);
        schedule_delayed_work(&als->als_dwork, msecs_to_jiffies(als->als_poll_delay));
    }

    mutex_unlock(&als->update_lock);

    return count;
}

static ssize_t bh1726_show_als_data(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    int data0;
    int data1;
    data0 = i2c_smbus_read_word_data(client, REG_DATA0);
    data1 = i2c_smbus_read_word_data(client, REG_DATA1);

    return snprintf(buf, PAGE_SIZE, "%d %d\n", data0, data1);
}

static ssize_t bh1726_show_type(struct device *dev,
        struct device_attribute *attr,
        char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    BH1726_ALS_DATA *data = i2c_get_clientdata(client);

    return snprintf(buf, PAGE_SIZE, "%d\n", data->dev_id);
}


static ssize_t bh1726_show_allreg(struct device *dev, struct device_attribute *attr, char *buf)
{

    struct i2c_client *client = to_i2c_client(dev);
    BH1726_ALS_DATA *data = i2c_get_clientdata(client);

    ssize_t len = 0;

    if(!client || !data){
        len += snprintf(buf+len, PAGE_SIZE-len, "obj is null!!\n");
        return len;
    }

    len += snprintf(buf+len, PAGE_SIZE-len,
            "judge_coefficient[4]=[%d,%d,%d,%d]\n",
            judge_coefficient[0],judge_coefficient[1],judge_coefficient[2],judge_coefficient[3]);

    len += snprintf(buf+len, PAGE_SIZE-len,
            "data0_coefficient[4]=[%d,%d,%d,%d]\n",
            data0_coefficient[0],data0_coefficient[1],data0_coefficient[2],data0_coefficient[3]);

    len += snprintf(buf+len, PAGE_SIZE-len,
            "data1_coefficient[4]=[%d,%d,%d,%d]\n",
            data1_coefficient[0],data1_coefficient[1],data1_coefficient[2],data1_coefficient[3]);


    len += snprintf(buf+len, PAGE_SIZE-len,
            "You can read/write a register just like the follow:\n        read:  echo \"r 0x40     \" > reg\n        write: echo \"w 0x40 0xFF\" > reg\n        para:  echo \"para       \" > reg\n        (Use dmesg to see kernel log)\n\n");

    len += snprintf(buf+len, PAGE_SIZE-len, " 0x%02x 0x%02x\n", REG_CONTROL           , bh1726_register_dump( data,  REG_CONTROL         ));
    len += snprintf(buf+len, PAGE_SIZE-len, " 0x%02x 0x%02x\n", REG_TIMING            , bh1726_register_dump( data,  REG_TIMING          ));
    len += snprintf(buf+len, PAGE_SIZE-len, " 0x%02x 0x%02x\n", REG_INTERRUPT         , bh1726_register_dump( data,  REG_INTERRUPT       ));
    len += snprintf(buf+len, PAGE_SIZE-len, " 0x%02x 0x%02x\n", REG_THRED_LOW         , bh1726_register_dump( data,  REG_THRED_LOW       ));
    len += snprintf(buf+len, PAGE_SIZE-len, " 0x%02x 0x%02x\n", REG_THRED_LOW+1       , bh1726_register_dump( data,  REG_THRED_LOW+1     ));
    len += snprintf(buf+len, PAGE_SIZE-len, " 0x%02x 0x%02x\n", REG_THRED_HIGH        , bh1726_register_dump( data,  REG_THRED_HIGH      ));
    len += snprintf(buf+len, PAGE_SIZE-len, " 0x%02x 0x%02x\n", REG_THRED_HIGH+1      , bh1726_register_dump( data,  REG_THRED_HIGH+1    ));
    len += snprintf(buf+len, PAGE_SIZE-len, " 0x%02x 0x%02x\n", REG_GAIN              , bh1726_register_dump( data,  REG_GAIN            ));
    len += snprintf(buf+len, PAGE_SIZE-len, " 0x%02x 0x%02x\n", REG_PART_ID           , bh1726_register_dump( data,  REG_PART_ID         ));
    len += snprintf(buf+len, PAGE_SIZE-len, " 0x%02x 0x%02x\n", REG_DATA0             , bh1726_register_dump( data,  REG_DATA0           ));
    len += snprintf(buf+len, PAGE_SIZE-len, " 0x%02x 0x%02x\n", REG_DATA0+1           , bh1726_register_dump( data,  REG_DATA0+1         ));
    len += snprintf(buf+len, PAGE_SIZE-len, " 0x%02x 0x%02x\n", REG_DATA1             , bh1726_register_dump( data,  REG_DATA1           ));
    len += snprintf(buf+len, PAGE_SIZE-len, " 0x%02x 0x%02x\n", REG_DATA1+1           , bh1726_register_dump( data,  REG_DATA1+1         ));
    len += snprintf(buf+len, PAGE_SIZE-len, " 0x%02x 0x%02x\n", REG_WAIT              , bh1726_register_dump( data,  REG_WAIT            ));

    return len;
}


static ssize_t bh1726_store_reg(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct i2c_client *client = to_i2c_client(dev);
    BH1726_ALS_DATA *data = i2c_get_clientdata(client);

#define MAX_LENGTH (3)

    int reg , i2c_data;
    int i = 0;
    int ret = 0;

    char * str_dest[MAX_LENGTH] = {0};
    char str_src[128];

    char delims[] = " ";
    char *str_result = NULL;
    char *cur_str = str_src;

    if(!data){
        printk("obj is null !!!\n");
        return 0;
    }

    memcpy(str_src, buf, count);
    printk("Your input buf is: %s\n", str_src );

    //spilt buf by space(" "), and seperated string are saved in str_src[]
    while(( str_result = strsep( &cur_str, delims ))) {
        if( i < MAX_LENGTH){  //max length should be 3
            str_dest[i++] = str_result;
        }
        else{
            //printk("break\n");
            break;
        }
    }

    if (!strncmp(str_dest[0], "r", 1)){
        reg = simple_strtol(str_dest[1], NULL, 16);

        //check reg valid
        if(((reg&0xFF) > REG_WAIT) || ((reg&0xFF) < REG_CONTROL)){
            printk("reg=0x%x is out of range !!!\n", reg );
            return -1;
        }

        //read i2c data
        bh1726_register_dump(data, reg&0xFF);
    }else if (!strncmp(str_dest[0], "w",  1)) {
        reg      = simple_strtol(str_dest[1], NULL, 16);
        i2c_data = simple_strtol(str_dest[2], NULL, 16);

        //check reg valid
        if(((reg&0xFF) > REG_WAIT) || ((reg&0xFF) < REG_CONTROL)){
            printk("reg=0x%x is out of range !!!\n", reg );
            return -1;
        }

        //write i2c data
        ret = i2c_smbus_write_byte_data(data->client, reg&0xFF, i2c_data&0xFF);
        if (ret < 0) {
            printk( " I2C read error !!!  \n" );
            return -1;
        }
        printk("writing...reg=0x%x, i2c_data=0x%x success\n", reg, i2c_data);
    }else if(!strncmp(str_dest[0], "para",  4)){  //print parameter
        int i;
        for(i=0;i < COEFFICIENT_SIZE; i++){
            printk ("data0_coefficient[%d] = %5d, ", i, data0_coefficient[i]);
            printk ("data1_coefficient[%d] = %5d, ", i, data1_coefficient[i]);
            printk ("judge_coefficient[%d] = %5d, ", i, judge_coefficient[i]);
            printk("\n");
        }
    } else{
        printk("Please input right format: \"r 0x40\", \"w 0x40 0xFF\"\n");
    }

    printk( "bh1726_store_reg count=%d\n", (int)count);

    return count;
}


static DEVICE_ATTR(als_poll_delay, 0660, bh1726_show_als_poll_delay, bh1726_store_als_poll_delay);
static DEVICE_ATTR(enable_als_sensor, 0660, bh1726_show_enable_als_sensor, bh1726_store_enable_als_sensor);
static DEVICE_ATTR(als_data, S_IRUGO, bh1726_show_als_data, NULL);
static DEVICE_ATTR(type, S_IRUGO, bh1726_show_type, NULL);
static DEVICE_ATTR(reg, 0660, bh1726_show_allreg, bh1726_store_reg);

static struct attribute *bh1726_attributes[] = {
    &dev_attr_enable_als_sensor.attr,
    &dev_attr_als_poll_delay.attr,
    &dev_attr_als_data.attr,
    &dev_attr_type.attr,
    &dev_attr_reg.attr,
    NULL
};

static const struct attribute_group bh1726_attr_group = {
    .attrs = bh1726_attributes,
};


/******************************************************************************
 * NAME       : bh1726_driver_set_data0_gain
 * FUNCTION   : set gain0
 * REMARKS    :
 *****************************************************************************/
static int bh1726_driver_set_data0_gain(BH1726_ALS_DATA *als, unsigned char gain)
{
    int result;

    if (NULL == als)
    {
        BH1726_ERR(" Parameter error \n");
        return EINVAL;
    }

    BH1726_FUN();

    /* read  gain register */
    result = i2c_smbus_read_byte_data(als->client, REG_GAIN);

    result = (result & CLR_GAIN0) | gain;

    BH1726_DBG("result = %d", result);

    result = i2c_smbus_write_byte_data(als->client, REG_GAIN, result);

    return (result);
}



/******************************************************************************
 * NAME       : bh1726_driver_set_data1_gain
 * FUNCTION   : set gain1
 * REMARKS    :
 *****************************************************************************/
static int bh1726_driver_set_data1_gain(BH1726_ALS_DATA *als, unsigned char gain)
{
    int result;

    if (!als)
    {
        BH1726_ERR(" Parameter error \n");
        return EINVAL;
    }

    BH1726_FUN();

    /* read  gain register */
    result = i2c_smbus_read_byte_data(als->client, REG_GAIN);

    result = (result & CLR_GAIN1) | gain;

    BH1726_DBG("result = %d", result);

    result = i2c_smbus_write_byte_data(als->client, REG_GAIN, result);

    return (result);
}



/*************** Initialze Functions ******************/
static int bh1726_init_client(BH1726_ALS_DATA *als)
{
    int result = 0;

    /* execute software reset */
    result = bh1726_driver_reset(als->client);
    if (result != 0) {
        return (result);
    }

    result = bh1726_driver_set_measure_time(als, MEASURE_100MS);
    if (result == 0) {
        bh1726_driver_set_data0_gain(als, DATA0_GAIN_X128 );  //set data0 gain
        bh1726_driver_set_data1_gain(als, DATA1_GAIN_X128 );  //set data1 gain
    }
    else{
        BH1726_ERR(" I2c write failed! \n");
    }

    return (result);
}

static int bh1726_power_on(BH1726_ALS_DATA *data, bool on)
{
    int rc;

    if (!on)
        goto power_off;

    rc = regulator_enable(data->vdd);
    if (rc) {
        dev_err(&data->client->dev,
                "Regulator vdd enable failed rc=%d\n", rc);
        return rc;
    }

    rc = regulator_enable(data->vio);
    if (rc) {
        dev_err(&data->client->dev,
                "Regulator vio enable failed rc=%d\n", rc);
        regulator_disable(data->vdd);
    }

    return rc;

power_off:
    rc = regulator_disable(data->vdd);
    if (rc) {
        dev_err(&data->client->dev,
                "Regulator vdd disable failed rc=%d\n", rc);
        return rc;
    }

    rc = regulator_disable(data->vio);
    if (rc) {
        dev_err(&data->client->dev,
                "Regulator vio disable failed rc=%d\n", rc);
    }

    return rc;
}
static int bh1726_power_init(BH1726_ALS_DATA *data, bool on)
{
    int rc;

    if (!on)
        goto pwr_deinit;

    data->vdd = regulator_get(&data->client->dev, "vdd");
    if (IS_ERR(data->vdd)) {
        rc = PTR_ERR(data->vdd);
        dev_err(&data->client->dev,
                "Regulator get failed vdd rc=%d\n", rc);
        return rc;
    }

    if (regulator_count_voltages(data->vdd) > 0) {
        rc = regulator_set_voltage(data->vdd, BH1726_VDD_MIN_UV,
                BH1726_VDD_MAX_UV);
        if (rc) {
            dev_err(&data->client->dev,
                    "Regulator set_vtg failed vdd rc=%d\n", rc);
            goto reg_vdd_put;
        }
    }

    data->vio = regulator_get(&data->client->dev, "vio");
    if (IS_ERR(data->vio)) {
        rc = PTR_ERR(data->vio);
        dev_err(&data->client->dev,
                "Regulator get failed vio rc=%d\n", rc);
        goto reg_vdd_set_vtg;
    }

    if (regulator_count_voltages(data->vio) > 0) {
        rc = regulator_set_voltage(data->vio, BH1726_VIO_MIN_UV,
                BH1726_VIO_MAX_UV);
        if (rc) {
            dev_err(&data->client->dev,
                    "Regulator set_vtg failed vio rc=%d\n", rc);
            goto reg_vio_put;
        }
    }

    return 0;

reg_vio_put:
    regulator_put(data->vio);
reg_vdd_set_vtg:
    if (regulator_count_voltages(data->vdd) > 0)
        regulator_set_voltage(data->vdd, 0, BH1726_VDD_MAX_UV);
reg_vdd_put:
    regulator_put(data->vdd);
    return rc;

pwr_deinit:
    if (regulator_count_voltages(data->vdd) > 0)
        regulator_set_voltage(data->vdd, 0, BH1726_VDD_MAX_UV);

    regulator_put(data->vdd);

    if (regulator_count_voltages(data->vio) > 0)
        regulator_set_voltage(data->vio, 0, BH1726_VIO_MAX_UV);

    regulator_put(data->vio);
    return 0;
}


static int bh1726_probe(struct i2c_client *client,
        const struct i2c_device_id *id)
{
#define ROHM_ALS_MAX (65535)

    struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
    BH1726_ALS_DATA * als;

    int err = 0;
    int dev_id;
    BH1726_INFO("%s probe started.\n", __func__);

    if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE)) {
        err = -EIO;
        goto exit;
    }

    als = kzalloc(sizeof(BH1726_ALS_DATA), GFP_KERNEL);
    if (!als) {
        err = -ENOMEM;
        goto exit;
    }
    als->client = client;
    i2c_set_clientdata(client, als);

   /* err = bh1726_power_init(als, true);
    if (err)
        dev_err(&client->dev, "power init failed");

    err = bh1726_power_on(als, true);
    if (err)
        dev_err(&client->dev, "power on failed");
*/
   // bh1726_enable_als_sensor(client, POWER_ON);
    BH1726_DBG("enable = %x\n", als->enable);
BH1726_INFO("enable = %x\n", als->enable);
    dev_id = i2c_smbus_read_byte_data(client, REG_PART_ID);
    if (dev_id != PART_ID_VALUE) {
        kfree(als);
        return -EPERM;
    }
    als->dev_id = dev_id;
    BH1726_DBG("%s: id(0x%x), this is bh1726!\n", __func__, dev_id);

    mutex_init(&als->update_lock);

    INIT_DELAYED_WORK(&als->als_dwork, bh1726_als_polling_work_handler);

    /* Initialize the bh1726 chip */
    err = bh1726_init_client(als);
    if (err)
        goto exit_kfree;

    als->als_poll_delay = ALS_SET_MIN_DELAY_TIME;

    /* Register to Input Device */
    als->input_dev_als = input_allocate_device();
    if (!als->input_dev_als) {
        err = -ENOMEM;
        BH1726_DBG("%s: Failed to allocate input device als\n", __func__);
        goto exit_kfree;
    }

    input_set_drvdata(als->input_dev_als, als);
    set_bit(EV_ABS, als->input_dev_als->evbit);
    input_set_abs_params(als->input_dev_als,
            ABS_MISC, 0, ROHM_ALS_MAX, 0, 0);

    als->input_dev_als->name = "lightsensor";
    als->input_dev_als->id.bustype = BUS_I2C;
    als->input_dev_als->dev.parent = &als->client->dev;

    err = input_register_device(als->input_dev_als);
    if (err) {
        err = -ENOMEM;
        BH1726_DBG("%s:register input device als fail: %s\n", __func__,
                als->input_dev_als->name);
        goto exit_free_dev_als;
    }


    /* Register sysfs hooks */
    err = sysfs_create_group(&client->dev.kobj, &bh1726_attr_group);
    if (err) {
        BH1726_DBG("%s sysfs_create_groupX\n", __func__);
        goto exit_unregister_dev_als;
    }

    BH1726_DBG("%s support ver. %s enabled\n", __func__, DRIVER_VERSION);

#if QCOM_SENSORS

    /* Register to sensors class for qcom  */
    als->als_cdev = sensors_light_cdev;
    als->als_cdev.sensors_enable = bh1726_als_set_enable;
    als->als_cdev.sensors_poll_delay = bh1726_als_poll_delay;

    err = sensors_classdev_register(&client->dev, &als->als_cdev);
    if (err) {
        pr_err("%s: Unable to register to sensors class: %d\n",
                __func__, err);
        goto exit_remove_sysfs_group;
    }
#endif

    BH1726_INFO("bh1726 probe success!");
    
    return 0;
    
#if QCOM_SENSORS
exit_create_class_sysfs:
    sensors_classdev_unregister(&als->als_cdev);
exit_remove_sysfs_group:
#endif

    sysfs_remove_group(&client->dev.kobj, &bh1726_attr_group);
exit_unregister_dev_als:
    input_unregister_device(als->input_dev_als);
exit_free_dev_als:
    input_free_device(als->input_dev_als);
exit_kfree:
    kfree(als);
exit:
    return err;

#undef ROHM_ALS_MAX
}

static int bh1726_remove(struct i2c_client *client)
{
    BH1726_ALS_DATA *als = i2c_get_clientdata(client);

#if QCOM_SENSORS
    sensors_classdev_unregister(&als->als_cdev);
#endif

    cancel_delayed_work(&als->als_dwork);

    input_unregister_device(als->input_dev_als);

    input_free_device(als->input_dev_als);

    sysfs_remove_group(&client->dev.kobj, &bh1726_attr_group);

    /* Power down the device */
    bh1726_enable_als_sensor(client, POWER_OFF);

    kfree(als);

    return 0;
}

static int bh1726_suspend(struct i2c_client *client, pm_message_t mesg)
{
    BH1726_ALS_DATA * als = i2c_get_clientdata(client);
    int ret = 0;

    BH1726_INFO("%s\n", __func__);


    if(als->enable_als_sensor == POWER_ON){
        als->als_suspend_flag = 1;
    }
    
    ret =  bh1726_enable_als_sensor(client, POWER_OFF);
    
    return ret;
}

static int bh1726_resume(struct i2c_client *client)
{
    BH1726_ALS_DATA * als = i2c_get_clientdata(client);

    int ret = 0;
    
    BH1726_INFO("%s\n", __func__);
    
    if(als->als_suspend_flag == 1){
        als->als_suspend_flag = 0;
    }
    
    ret = bh1726_enable_als_sensor(client, POWER_ON);

    return ret; 
}


MODULE_DEVICE_TABLE(i2c, bh1726_id);

static const struct i2c_device_id bh1726_id[] = {
    { "bh1726", 0 },
    { }
};

#ifdef CONFIG_OF
static struct of_device_id bh1726_match_table[] = {
    { .compatible = "elf,bh1726",},
    { },
};
#else
#define bh1726_match_table NULL
#endif

static struct i2c_driver bh1726_driver = {
    .driver = {
        .name    = BH1726_DRV_NAME,
        .owner    = THIS_MODULE,
        .of_match_table = bh1726_match_table,
    },
//    .suspend = bh1726_suspend,
 //   .resume    = bh1726_resume,
    .probe    = bh1726_probe,
    .remove    = bh1726_remove,
    .id_table = bh1726_id,
};

static int __init bh1726_init(void)
{
    return i2c_add_driver(&bh1726_driver);
}

static void __exit bh1726_exit(void)
{
    i2c_del_driver(&bh1726_driver);
}

MODULE_AUTHOR("Aaron_liu@rohm");
MODULE_DESCRIPTION("bh1726 ambient light sensor driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

module_init(bh1726_init);
module_exit(bh1726_exit);
