#!/bin/bash

# 立即休眠并在 10 秒后唤醒（测试用）
rtcwake -m mem -s 10

# 指定唤醒时间（例如 08:00）
rtcwake -m mem -l -t $(date +%s -d "08:00")

#    -m mem: 休眠到内存（Suspend-to-RAM）
#    -s 10: 休眠 10 秒后唤醒
#    -l: 使用本地时间（而非 UTC）
#    -t: 指定时间戳

# 方法一
echo "### suspend enter! ###"
rtcwake -m mem -s 10
echo "### suspend exit! ###"

# 方法二
echo "### suspend enter! ###"
echo 0 > /sys/class/rtc/rtc0/wakealarm                             # 清除现有闹钟
echo $(date +%s -d "+10 seconds") > /sys/class/rtc/rtc0/wakealarm  # 设置 10 秒后唤醒
echo mem > /sys/power/state                                        # 下 suspend 命令
echo "### suspend exit! ###"
