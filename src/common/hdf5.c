#include "common/hdf5.h"

gboolean
ufo_hdf5_can_open (const gchar *filename)
{
    gchar *delimiter;

    delimiter = g_strrstr (filename, ":");

    if (delimiter == NULL)
        return FALSE;

    /* delimiter is not preceeded by three characters */
    if (delimiter < filename + 3)
        return FALSE;

    if (!g_str_has_prefix (delimiter - 3, ".h5"))
        return FALSE;

    /* no dataset after delimiter */
    if ((delimiter[1] == '\0') || (delimiter[2] == '\0'))
        return FALSE;

    return TRUE;
}
