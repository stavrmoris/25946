#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
    time_t now; // текущий момент времени в сек с 1970
    struct tm *california_time; // структура с полями год, месяц, день, часы, минуты и признак летнего времени
    int zone_index; // просто подпись индекса зоны

    /* Устанавливаем часовой пояс только для текущего процесса */
    if (setenv("TZ", "America/Los_Angeles", 1) == -1) {
        perror("setenv(TZ)");
        return EXIT_FAILURE;
    }
    tzset();

    now = time(NULL);
    if (now == (time_t)-1) {
        perror("time");
        return EXIT_FAILURE;
    }

    california_time = localtime(&now);
    if (california_time == NULL) {
        perror("localtime");
        return EXIT_FAILURE;
    }

    /* Положительное значение tm_isdst означает летнее время, ноль означает стандартное время */
    zone_index = california_time->tm_isdst > 0 ? 1 : 0;

    printf("%02d/%02d/%04d %02d:%02d %s\n",
           california_time->tm_mon + 1,
           california_time->tm_mday,
           california_time->tm_year + 1900,
           california_time->tm_hour,
           california_time->tm_min,
           tzname[zone_index]);

    return EXIT_SUCCESS;
}
