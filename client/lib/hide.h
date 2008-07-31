#ifndef HIDE_H_080731
#define HIDE_H_080731

struct hide_cfg;

int hide_begin(void);
void hide_end(void);

int hide_cfg_get(struct hide_cfg **cfg, char const *path);
void hide_cfg_release(struct hide_cfg *cfg);

#include <stdbool.h>
bool show_this_dir(struct hide_cfg *cfg, char const *name);

#endif
