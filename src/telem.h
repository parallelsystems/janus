#ifndef JANUS_TELEM_H
#define JANUS_TELEM_H

#include "debug.h"

/* Prefix for telemetry log filtering */
#define TELEM_LOG_PREFIX  "TELEM"
#define TELEM_PLUGIN_PACKAGE_NAME "janus.plugin.telem_logger"

/* Special logger macro that directly sends specifically-formatted lines to
	Janus' logging system. These telemetered logs are always logged, regardless
	of the configured runtime log level of the Janus core.
 */
#define JANUS_TELEMETER_LOG(format, ...) \
do { \
	char telem_prefix[16] = ""; \
	snprintf(telem_prefix, sizeof(telem_prefix), "%s", TELEM_LOG_PREFIX); \
	JANUS_PRINT("%s" format, \
		telem_prefix, \
		##__VA_ARGS__); \
} while (0)

#endif