#include "ufo-priv.h"

guint
ceil_power_of_two (guint x)
{
    guint res = 1;

    while (res < x)
        res *= 2;

    return res;
}
