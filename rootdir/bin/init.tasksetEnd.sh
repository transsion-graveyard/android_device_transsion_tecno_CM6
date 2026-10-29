#!/vendor/bin/sh

#add kswapd0 bind 0~5 core by yixin.zhu 20251021 start
## This is a shell script for executing taskset commands at early-init step
## Do NOT add other commands in this file otherwise you may violate its SELinux policy

taskset -ap ff `pidof -x kswapd0`
#add kswapd0 bind 0~5 core by yixin.zhu 20251021 end
