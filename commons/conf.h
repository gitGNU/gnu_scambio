/* A mean to access to a persistant, immutable configuration.
 * The static configuration is a set of key=>value, where a value may be
 * a string or a integer.
 * This is recommended to use commands instead of static configuration file
 * for configuration purpose, so that configuration can be change in real time
 * while the system is running.
 */
#ifndef CONF_H_080617
#define CONF_H_080617

int conf_set_default_str(char const *name, char const *value);
int conf_set_default_int(char const *name, long long value);

// abort if not found !
// set default values first !
char const *conf_get_str(char const *name);
long long conf_get_int(char const *name);

#endif
