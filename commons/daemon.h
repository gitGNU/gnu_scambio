/* Daemonize a process, and handle some signals.
 * For instance, HUP to reopen log files (and call log_init())
 *
 * Also use conf for log configuration.
 */

int daemonize(void);

