#include "app_shell.h"
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/rtc.h>
#include <time.h>

__attribute__((weak)) void shell_cleanup_before_reboot()
{

}

static int shell_reboot(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 1) {
        shell_error(sh, "invalid arguments");
        return -EINVAL;
    }

    shell_cleanup_before_reboot();
    shell_info(sh, "reboot!");
    sys_reboot(SYS_REBOOT_COLD);
    return 0;
}

static int shell_date_set(char *date, char *time)
{
    int ret = 0;
    int max_day = 0;
    int month_day[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    struct tm tm = {
        0,
    };
    struct rtc_time rtc_time = {
        0,
    };
    const struct device *rtc_dev = DEVICE_DT_GET(DT_NODELABEL(rtc));

    ret = sscanf(date, "%04d-%02d-%02d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday);
    if (ret != 3) {
        return -EINVAL;
    }

    ret = sscanf(time, "%02d:%02d:%02d", &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    if (ret != 3) {
        return -EINVAL;
    }

    if (tm.tm_year < 2026 || tm.tm_year > 9999) {
        return -ERANGE;
    }

    if (tm.tm_mon <= 0 || tm.tm_mon > 12) {
        return -ERANGE;
    }

    tm.tm_mon -= 1;    
    max_day = month_day[tm.tm_mon];

    if (tm.tm_mon == 1 && tm.tm_year % 4 == 0 && 
        ((tm.tm_year % 100 != 0) || (tm.tm_year % 400 == 0))) {
        max_day = 29;
    }

    if (tm.tm_mday <= 0 || tm.tm_mday > max_day) {
        return -ERANGE;
    }

    if (tm.tm_hour < 0 || tm.tm_hour > 23) {
        return -ERANGE;
    }

    if (tm.tm_min < 0 || tm.tm_min > 59) {
        return -ERANGE;
    }

    if (tm.tm_sec < 0 || tm.tm_sec > 59) {
        return -ERANGE;
    }

    tm.tm_year -= 1900;
    rtc_time.tm_sec = tm.tm_sec;
    rtc_time.tm_min = tm.tm_min;
    rtc_time.tm_hour = tm.tm_hour;
    rtc_time.tm_mday = tm.tm_mday;
    rtc_time.tm_mon = tm.tm_mon;
    rtc_time.tm_year = tm.tm_year;
    return rtc_set_time(rtc_dev, &rtc_time);
}

static int shell_date_get(const struct shell *sh)
{
    struct rtc_time rtc_time = {
        0,
    };
    const struct device *rtc_dev = DEVICE_DT_GET(DT_NODELABEL(rtc));
    int ret = 0;
    ret = rtc_get_time(rtc_dev, &rtc_time);
    if (ret < 0) {
        return ret;
    }
    shell_info(sh, "info : %04d-%02d-%02d %02d:%02d:%02d",
               rtc_time.tm_year + 1900,
               rtc_time.tm_mon + 1,
               rtc_time.tm_mday,
               rtc_time.tm_hour,
               rtc_time.tm_min,
               rtc_time.tm_sec);
    return 0;
}

static int shell_date(const struct shell *sh, size_t argc, char **argv)
{
    int ret = 0;
    if (strncmp("set", argv[0], strlen("set")) == 0) {
        if (argc != 3) {
            shell_error(sh, "invalid arguments count");
            return -EINVAL;
        }
        ret = shell_date_set(argv[1], argv[2]);
        if (ret < 0) {
            shell_error(sh, "date set - %s", strerror(-ret));
        }
        return ret;
    } else if (strncmp("get", argv[0], strlen("get")) == 0) {
        ret = shell_date_get(sh);
        if (ret < 0) {
            shell_error(sh, "date get - %s", strerror(-ret));
        }
        return ret;
    } else {
        return -EINVAL;
    }
}

SHELL_CMD_REGISTER(
    reboot,
    NULL,
    "reboot system (cold reboot)",
    shell_reboot
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_date,
    SHELL_CMD_ARG(
        set,
        NULL,
        "set yyyy-mm-dd hh:mm:ss",
        shell_date,
        3,
        0
    ),

    SHELL_CMD_ARG(
        get,
        NULL,
        "print date",
        shell_date,
        1,
        0
    ),
    
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(
    date,
    &sub_date,
    "date",
    NULL
);