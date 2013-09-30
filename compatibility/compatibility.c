#include <glib.h>

#if 1
gpointer g_malloc_n       (gsize	 n_blocks,
			   gsize	 n_block_bytes) /*G_GNUC_MALLOC G_GNUC_ALLOC_SIZE2(1,2)*/
{
    return g_malloc(n_blocks * n_block_bytes);
}

gpointer g_malloc0_n      (gsize	 n_blocks,
			   gsize	 n_block_bytes) /*G_GNUC_MALLOC G_GNUC_ALLOC_SIZE2(1,2)*/
{
    return g_malloc0(n_blocks * n_block_bytes);
}

gpointer g_realloc_n      (gpointer	 mem,
			   gsize	 n_blocks,
			   gsize	 n_block_bytes) /*G_GNUC_WARN_UNUSED_RESULT*/
{
    return g_realloc(mem, n_blocks * n_block_bytes);
}

#endif
