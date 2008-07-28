#include "cnx.h"
/* Connecter try to establish the connection, and keep trying once in a while
 * untill success, then spawn reader and writer until one of them return, when
 * it kills the remaining one, close the connection, and restart.
 */
void *connecter_thread(void *arg)
{
}

