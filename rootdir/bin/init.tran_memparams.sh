#!/system/bin/sh

MEM_TOTAL_KB=$(grep -m1 "MemTotal:" /proc/meminfo | tr -s ' ' | cut -d' ' -f2)
if [ "$MEM_TOTAL_KB" -lt 4194304 ]; then
    # <4G
    Swappiness=150
    WatermarkScale=28
elif [ "$MEM_TOTAL_KB" -lt 6291456 ]; then
    # 6G
    Swappiness=100
    WatermarkScale=25
elif [ "$MEM_TOTAL_KB" -lt 8388608 ]; then
    # 8G
    Swappiness=100
    WatermarkScale=25
elif [ "$MEM_TOTAL_KB" -lt 12582912 ]; then
    # 12G
    Swappiness=100
    WatermarkScale=25
else
    # >12G
    Swappiness=100
    WatermarkScale=25
fi

echo $Swappiness > /proc/sys/vm/swappiness
echo $WatermarkScale > /proc/sys/vm/watermark_scale_factor

