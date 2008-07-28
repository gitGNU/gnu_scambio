#include "main.h"

/* The reader listens for commands.
 * On a put response, it removes the temporary filename stored in the action
 * (the actual meta file will be synchronized independantly from the server).
 * On a rem response, it removes the temporary filename (same remark as above).
 * On a sub response, it moves the subscription from subscribing to subscribed.
 * On an unsub response, it deletes the unsubcribing subscription.
 * On a patch for addition, it creates the meta file (under the digest name)
 * and updates the version number. The meta may already been there if the update of
 * the version number previously failed.
 * On a patch for deletion, it removes the meta file and updates the version number.
 * Again, the meta may already have been removed if the update of the version number
 * previously failed.
 */
