#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdatomic.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "logger.h"
#include "../telem.h"
#include "../config.h"

/* Plugin information */
#define JANUS_THREADED_LOGGER_VERSION		    1
#define JANUS_THREADED_LOGGER_VERSION_STRING	"0.0.1"
#define JANUS_THREADED_LOGGER_DESCRIPTION	    "This plugin pushes streams of telemetry data to a Parallel Systems Telemetry client over UDP."
#define JANUS_THREADED_LOGGER_NAME		        "JANUS Telemetry streaming plugin"
#define JANUS_THREADED_LOGGER_AUTHOR		    "Parallel Systems"
#define JANUS_THREADED_LOGGER_PACKAGE		    TELEM_PLUGIN_PACKAGE_NAME

/* Socket configuration */
#define LOCALHOST "127.0.0.1"
/* Port on which the Telemetry messages are streamed out of */
#define LOG_UDP_PORT 9090

/* Log entry structure */
typedef struct log_entry {
    /// @brief Monotonically increasing counter
    uint64_t seqnum;
    /// @brief Monotonically increasing timestamp when this message was printed
    uint64_t timestamp;
    /// @brief The log message to telemeter
    char *message;
} log_entry_t;


/* Plugin state */
static volatile gint initialized = 0, stopping = 0;
/* Worker thread */
static GThread *logger_thread = NULL;

/* Telemetry log queue */
static GAsyncQueue *log_queue = NULL;

/* IO Sockets */
static int udp_socket = -1;
static struct sockaddr_in udp_addr;

/* The sequence number seed counter */
static atomic_uint_fast64_t seqnum = 0;

/* JANUS Plugin methods */
static int janus_threadedlogger_init(const char *server_name, const char *config_path);
static void janus_threadedlogger_destroy(void);
static void janus_threadedlogger_incoming_logline(int64_t timestamp, const char *line);
int janus_threadedlogger_get_api_compatibility(void);
int janus_threadedlogger_get_version(void);
const char *janus_threadedlogger_get_version_string(void);
const char *janus_threadedlogger_get_description(void);
const char *janus_threadedlogger_get_name(void);
const char *janus_threadedlogger_get_author(void);
const char *janus_threadedlogger_get_package(void);

/* Forward declarations of specific plugin functions */
static void *worker_thread_func(void *arg);
static void free_log_entry(log_entry_t *entry);

/* Plugin descriptor */
static janus_logger janus_threadedlogger_logger = {
    .init = janus_threadedlogger_init,
    .destroy = janus_threadedlogger_destroy,
    .get_api_compatibility = janus_threadedlogger_get_api_compatibility,
    .get_version = janus_threadedlogger_get_version,
    .get_version_string = janus_threadedlogger_get_version_string,
    .get_description = janus_threadedlogger_get_description,
    .get_name = janus_threadedlogger_get_name,
    .get_author = janus_threadedlogger_get_author,
    .get_package = janus_threadedlogger_get_package,
    .incoming_logline = janus_threadedlogger_incoming_logline,
};

/* Plugin creator */
janus_logger *create(void) {
    JANUS_LOG(LOG_INFO, "%s created!\n", JANUS_THREADED_LOGGER_NAME);
    return &janus_threadedlogger_logger;
}

/* Initialize the plugin */
static int janus_threadedlogger_init(const char *server_name, const char *config_path) {
    if(g_atomic_int_get(&initialized) || g_atomic_int_get(&stopping)) {
        JANUS_LOG(LOG_ERR, "Telemetry logger already initialized\n");
        return -1;
    }
    JANUS_LOG(LOG_INFO, "Initializing telemetry logger plugin\n");

    char udp_address[64];
    snprintf(udp_address, 64, "%s", LOCALHOST);
    uint16_t udp_port = LOG_UDP_PORT;

    if(config_path == NULL) {
		/* Invalid arguments */
		return -1;
	}

	/* Read configuration file */
	gboolean enabled = FALSE;
	char filename[255];
	g_snprintf(filename, 255, "%s/%s.jcfg", config_path, JANUS_THREADED_LOGGER_PACKAGE);
	JANUS_LOG(LOG_VERB, "Checking configuration file: %s\n", filename);

	janus_config *config = janus_config_parse(filename);
	if(config == NULL) {
		JANUS_LOG(LOG_WARN, "Couldn't find .jcfg configuration file (%s), trying .cfg\n", JANUS_THREADED_LOGGER_PACKAGE);
		g_snprintf(filename, 255, "%s/%s.cfg", config_path, JANUS_THREADED_LOGGER_PACKAGE);
		JANUS_LOG(LOG_VERB, "Checking configuration file: %s\n", filename);
		config = janus_config_parse(filename);
	}

	if(config != NULL) {
		/* Handle configuration */
		janus_config_print(config);
		janus_config_category *config_general = janus_config_get_create(config, NULL, janus_config_type_category, "general");
        if (config_general) {
            janus_config_item *item = janus_config_get(config, config_general, janus_config_type_item, "udp_address");
            if(!item || !item->value) {
                JANUS_LOG(LOG_WARN, "No UDP Address in telem config - using default [%s]\n", udp_address);
            } else {
                snprintf(udp_address, 64, "%s", item->value);
                JANUS_LOG(LOG_INFO, "Telemetry configured to UDP Address [%s]\n", udp_address);
            }

            item = janus_config_get(config, config_general, janus_config_type_item, "udp_port");
            if(!item || !item->value) {
                JANUS_LOG(LOG_WARN, "No UDP Port in telem config - using default [%d]\n", udp_port);
            } else {
                udp_port = atoi(item->value);
                JANUS_LOG(LOG_INFO, "Telemetry configured to UDP Port [%d]\n", udp_port);
            }
        } else {
            JANUS_LOG(LOG_WARN, "Unable to configure telemetry logger from file [%s] - missing 'general' blob\n", filename);
        }
	}
	janus_config_destroy(config);
	config = NULL;

    JANUS_LOG(LOG_INFO, "\tTelemetry Logger will stream data to %s:%d\n", udp_address, udp_port);

    /* Initialize async queue for log entries */
    log_queue = g_async_queue_new_full((GDestroyNotify)free_log_entry);

	g_atomic_int_set(&initialized, 1);
    g_atomic_int_set(&stopping, 0);

    GError *error = NULL;
    // /* Set up log output UDP socket */
    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if(udp_socket < 0) {
		JANUS_LOG(LOG_ERR, "Failed to create UDP socket: %s\n", strerror(errno));
		return -1;
	}

    memset(&udp_addr, 0, sizeof(udp_addr));
	udp_addr.sin_family = AF_INET;
	udp_addr.sin_port = htons(udp_port);
	if(inet_aton(udp_address, &udp_addr.sin_addr) == 0) {
		JANUS_LOG(LOG_ERR, "Invalid UDP host address: %s\n", udp_address);
		close(udp_socket);
		udp_socket = -1;
		return -1;
	}

    if(connect(udp_socket, (struct sockaddr *)&udp_addr, sizeof(udp_addr)) < 0)
    {
        JANUS_LOG(LOG_ERR, "Unable to connect to UDP socket: %s:%d\n", udp_address, udp_port);
        close(udp_socket);
		udp_socket = -1;
        return -1;
    }

    /* Start worker threads */
	logger_thread = g_thread_try_new("io", worker_thread_func, NULL, &error);
	if(error != NULL) {
		JANUS_LOG(LOG_ERR, "Got error %d (%s) trying to launch the telemetry logger thread...\n",
			error->code, error->message ? error->message : "?");
		g_error_free(error);
        janus_threadedlogger_destroy();
		return -1;
	}

    JANUS_LOG(LOG_INFO, "Telemetry logger plugin initialized (filter: '%s')\n", TELEM_LOG_PREFIX);
    return 0;
}

/* Destroy the plugin */
static void janus_threadedlogger_destroy(void) {
	if(!g_atomic_int_get(&initialized))
		return;
	g_atomic_int_set(&stopping, 1);

    JANUS_LOG(LOG_WARN, "Destroying telemetry logger plugin\n");
    
    /* Wait for threads to finish */
    if(logger_thread != NULL) {
		g_thread_join(logger_thread);
		logger_thread = NULL;
	}

    /* Close socket */
    if (udp_socket >= 0) {
        close(udp_socket);
        udp_socket = -1;
    }
    /* Cleanup the queue */
    if (log_queue != NULL) {
        g_async_queue_unref(log_queue);
        log_queue = NULL;
    }

	g_atomic_int_set(&initialized, 0);
	g_atomic_int_set(&stopping, 0);
	JANUS_LOG(LOG_INFO, "%s destroyed!\n", JANUS_THREADED_LOGGER_NAME);
}

int janus_threadedlogger_get_api_compatibility(void) {
	/* Important! This is what your plugin MUST always return: don't lie here or bad things will happen */
	return JANUS_LOGGER_API_VERSION;
}

int janus_threadedlogger_get_version(void) {
	return JANUS_THREADED_LOGGER_VERSION;
}

const char *janus_threadedlogger_get_version_string(void) {
	return JANUS_THREADED_LOGGER_VERSION_STRING;
}

const char *janus_threadedlogger_get_description(void) {
	return JANUS_THREADED_LOGGER_DESCRIPTION;
}

const char *janus_threadedlogger_get_name(void) {
	return JANUS_THREADED_LOGGER_NAME;
}

const char *janus_threadedlogger_get_author(void) {
	return JANUS_THREADED_LOGGER_AUTHOR;
}

const char *janus_threadedlogger_get_package(void) {
	return JANUS_THREADED_LOGGER_PACKAGE;
}

/* Main handle incoming log lines, called from the main JANUS thread */
static void janus_threadedlogger_incoming_logline(int64_t timestamp, const char *line) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized) || line == NULL) {
		/* Janus is closing or the plugin is */
		return;
	}

    /* Apply basic filter - only process messages starting with the filter prefix */
    if(strncmp(line, TELEM_LOG_PREFIX, strlen(TELEM_LOG_PREFIX)) != 0) {
        /* Message doesn't match filter, skip it */
        return;
    }

    log_entry_t *entry = g_malloc(sizeof(log_entry_t));
    if(!entry)
        return;
    
    entry->message = g_strdup(line);
    if(!entry->message) {
        g_free(entry);
        return;
    }
    entry->seqnum = atomic_fetch_add(&seqnum, 1);
    entry->timestamp = timestamp;

    // Enqueue it and move on!
    g_async_queue_push(log_queue, entry);
}


// Helper macro to pack a payload; allocates heap memory via glib that must be freed
#define PACK_TELEMETRY_MSG(seqnum, timestamp, msg) \
	g_strdup_printf("{\"seqnum\":%lu,\"time\":%lu,\"msg\":{%s}", \
        (uint64_t)seqnum, (uint64_t)timestamp, (char*)msg)

/* Worker thread function - reads queued log messages and writes them on the UDP socket */
static void *worker_thread_func(void *arg) {
    JANUS_LOG(LOG_WARN, "Worker thread started\n");
    
    while(g_atomic_int_get(&initialized) && !g_atomic_int_get(&stopping)) {        
        // This blocks this thread until there's data to be read - takes the head of the queue.
        // entry will be automatically freed by queue's destroy notify.
        log_entry_t *entry = g_async_queue_pop(log_queue);
        size_t prefix_len = strlen(TELEM_LOG_PREFIX) + 1;
        if (entry && (strlen(entry->message) > prefix_len)) {
            // Index past the prefix
            gchar *msg = entry->message + prefix_len;
            gchar *telemetered_msg = PACK_TELEMETRY_MSG(entry->seqnum, entry->timestamp, msg);
            if (telemetered_msg) {
                ssize_t sent = sendto(udp_socket, (char*)telemetered_msg, strlen((char*)telemetered_msg), 0, NULL, sizeof(udp_addr));
                if(sent < 0) {
                    JANUS_LOG(LOG_WARN, "Failed to send UDP log message %s -> %s\n", telemetered_msg, strerror(errno));
                }
                g_free(telemetered_msg);
            } else {
                JANUS_LOG(LOG_WARN, "Failed to allocate telemetry message\n");
            }
        }
    }

    JANUS_LOG(LOG_INFO, "Worker thread stopped\n");
    return NULL;
}
/* Free log entry - async destructor function for each queue entry when it's popped off */
static void free_log_entry(log_entry_t *entry) {
    if(entry) {
        if (entry->message)
            g_free(entry->message);
        g_free(entry);
    }
}