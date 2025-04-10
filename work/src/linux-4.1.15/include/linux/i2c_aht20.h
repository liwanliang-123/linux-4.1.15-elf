#ifndef I2C_AHT20_H
#define I2C_AHT20_H



#define AHT20_STATUS_CALI_SHIFT 3       // bit[3] CAL Enable
#define AHT20_STATUS_CALI_MASK  (0x1<<AHT20_STATUS_CALI_SHIFT)
#define AHT20_STATUS_CALI(status) ((status & AHT20_STATUS_CALI_MASK) >> AHT20_STATUS_CALI_SHIFT)
                                        // bit[2:0] Reserved
#define AHT20_STATUS_BUSY_SHIFT 7       // bit[7] Busy indication
#define AHT20_STATUS_BUSY_MASK  (0x1<<AHT20_STATUS_BUSY_SHIFT)
#define AHT20_STATUS_BUSY(status) ((status & AHT20_STATUS_BUSY_MASK) >> AHT20_STATUS_BUSY_SHIFT)

#define AHT20_CMD_STATUS        0x71
#define AHT20_CMD_RESET         0xBA

#define AHT20_CMD_TRIGGER       0xAC
#define AHT20_CMD_TRIGGER_ARG0  0x33
#define AHT20_CMD_TRIGGER_ARG1  0x00

#define AHT20_CMD_CALIBRATION       0xBE
#define AHT20_CMD_CALIBRATION_ARG0  0x08
#define AHT20_CMD_CALIBRATION_ARG1  0x00

#define AHT20_STARTUP_TIME     20 //ms
#define AHT20_CALIBRATION_TIME 40 //ms
#define AHT20_MEASURE_TIME     75 //ms

#define AHT20_MAX_RETRY 5
#define AHT20_RESOLUTION		(1<<20)

#endif
