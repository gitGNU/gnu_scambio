#include "scambio.h"
#include "hide.h"

int hide_begin(void)
{
	return 0;
}
void hide_end(void)
{
}

int hide_cfg_get(struct hide_cfg **cfg, char const *path)
{
	(void)path;
	*cfg = NULL;
	return 0;
}

void hide_cfg_release(struct hide_cfg *cfg)
{
	(void)cfg;
}


bool show_this_dir(struct hide_cfg *cfg, char const *name)
{
	(void)cfg;
	(void)name;
	return true;
}
