#!/bin/bash

arg1=$1
arg2=$2

select_app_type()
{
    select app_type in "linux" "x86_64";
    do
        break
    done
}

build_common()
{
    select_app_type

    if [ "${app_type}" == "linux" ]; then
        make CROSS_COMPILE=arm-buildroot-linux-uclibcgnueabihf-
    fi

    if [ "${app_type}" == "x86_64" ]; then
        make CROSS_COMPILE=
    fi
}

case ${arg1} in
    c)      make clean;;
    *)      build_common;;
esac
