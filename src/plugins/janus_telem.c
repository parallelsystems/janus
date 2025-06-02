#include "plugin.h"
#include "debug.h"
#include "apierror.h"
#include "config.h"
#include "mutex.h"
#include "rtp.h"
#include "rtcp.h"
#include "utils.h"
// #include "ice.h"

#include <jansson.h>
#include <time.h>
#include <sys/time.h>

/* Plugin information */
#define JANUS_RTP_CACHE_VERSION			1
#define JANUS_RTP_CACHE_VERSION_STRING	"0.0.1"
#define JANUS_RTP_CACHE_DESCRIPTION		"RTP packet cache plugin for offboard telemetry"
#define JANUS_RTP_CACHE_NAME			"JANUS RTP Cache plugin"
#define JANUS_RTP_CACHE_AUTHOR			"Janus"
#define JANUS_RTP_CACHE_PACKAGE			"janus.plugin.rtpcache"

/* Plugin methods */
janus_plugin *create(void);
int janus_rtpcache_init(janus_callbacks *callback, const char *config_path);
void janus_rtpcache_destroy(void);
int janus_rtpcache_get_api_compatibility(void);
int janus_rtpcache_get_version(void);
const char *janus_rtpcache_get_version_string(void);
const char *janus_rtpcache_get_description(void);
const char *janus_rtpcache_get_name(void);
const char *janus_rtpcache_get_author(void);
const char *janus_rtpcache_get_package(void);
void janus_rtpcache_create_session(janus_plugin_session *handle, int *error);
struct janus_plugin_result *janus_rtpcache_handle_message(janus_plugin_session *handle, char *transaction, json_t *message, json_t *jsep);
json_t *janus_rtpcache_handle_admin_message(json_t *message);
void janus_rtpcache_setup_media(janus_plugin_session *handle);
void janus_rtpcache_incoming_rtp(janus_plugin_session *handle, janus_plugin_rtp *packet, void *peer_connection);
void janus_rtpcache_incoming_rtcp(janus_plugin_session *handle, janus_plugin_rtcp *packet);
void janus_rtpcache_incoming_data(janus_plugin_session *handle, janus_plugin_data *packet);
void janus_rtpcache_slow_link(janus_plugin_session *handle, int uplink, int video);
void janus_rtpcache_hangup_media(janus_plugin_session *handle);
void janus_rtpcache_destroy_session(janus_plugin_session *handle, int *error);
json_t *janus_rtpcache_query_session(janus_plugin_session *handle);

/* Plugin setup */
static janus_plugin janus_rtpcache_plugin =
	JANUS_PLUGIN_INIT (
		.init = janus_rtpcache_init,
		.destroy = janus_rtpcache_destroy,

		.get_api_compatibility = janus_rtpcache_get_api_compatibility,
		.get_version = janus_rtpcache_get_version,
		.get_version_string = janus_rtpcache_get_version_string,
		.get_description = janus_rtpcache_get_description,
		.get_name = janus_rtpcache_get_name,
		.get_author = janus_rtpcache_get_author,
		.get_package = janus_rtpcache_get_package,

		.create_session = janus_rtpcache_create_session,
		.handle_message = janus_rtpcache_handle_message,
		.handle_admin_message = janus_rtpcache_handle_admin_message,
		.setup_media = janus_rtpcache_setup_media,
		.incoming_rtp = janus_rtpcache_incoming_rtp,
		.incoming_rtcp = janus_rtpcache_incoming_rtcp,
		.incoming_data = janus_rtpcache_incoming_data,
		.slow_link = janus_rtpcache_slow_link,
		.hangup_media = janus_rtpcache_hangup_media,
		.destroy_session = janus_rtpcache_destroy_session,
		.query_session = janus_rtpcache_query_session,
	);

/* Plugin creator */
janus_plugin *create(void) {
	JANUS_LOG(LOG_VERB, "%s created!\n", JANUS_RTP_CACHE_NAME);
	return &janus_rtpcache_plugin;
}

/* Useful stuff */
static volatile gint initialized = 0, stopping = 0;
static janus_callbacks *gateway = NULL;

/* RTP packet cache entry */
typedef struct janus_rtpcache_packet {
	uint16_t seq_num;
	int64_t arrival_time;
	struct janus_rtpcache_packet *next;
} janus_rtpcache_packet;

/* Session data */
typedef struct janus_rtpcache_session {
	janus_plugin_session *handle;
	janus_rtpcache_packet *packet_list_head;
	janus_rtpcache_packet *packet_list_tail;
	int packet_count;
	janus_mutex packet_mutex;
	volatile gint destroyed;
} janus_rtpcache_session;

static GHashTable *sessions;
static janus_mutex sessions_mutex = JANUS_MUTEX_INITIALIZER;

/* Constants */
#define MAX_CACHE_PACKETS 500
#define MAX_CACHE_TIME_US 5000000 /* 5 seconds in microseconds */


static janus_rtpcache_session *janus_rtpcache_lookup_session(janus_plugin_session *handle) {
	janus_rtpcache_session *session = NULL;
	if (g_hash_table_contains(sessions, handle)) {
		session = (janus_rtpcache_session *)handle->plugin_handle;
	}
	return session;
}

static void janus_rtpcache_cleanup_old_packets(janus_rtpcache_session *session) {
	if(!session || !session->packet_list_head)
		return;
	
	int64_t current_time = janus_get_monotonic_time();
	int64_t cutoff_time = current_time - MAX_CACHE_TIME_US;
	
	janus_rtpcache_packet *current = session->packet_list_head;
	janus_rtpcache_packet *prev = NULL;
	
	/* Remove packets older than 5 seconds */
	while(current && current->arrival_time < cutoff_time) {
		janus_rtpcache_packet *to_remove = current;
		current = current->next;
		
		if(prev == NULL) {
			session->packet_list_head = current;
		} else {
			prev->next = current;
		}
		
		if(to_remove == session->packet_list_tail) {
			session->packet_list_tail = prev;
		}
		
		g_free(to_remove);
		session->packet_count--;
	}
	
	/* Remove excess packets if we have more than MAX_CACHE_PACKETS */
	while(session->packet_count > MAX_CACHE_PACKETS && session->packet_list_head) {
		janus_rtpcache_packet *to_remove = session->packet_list_head;
		session->packet_list_head = session->packet_list_head->next;
		
		if(to_remove == session->packet_list_tail) {
			session->packet_list_tail = NULL;
		}
		
		g_free(to_remove);
		session->packet_count--;
	}
}

static void janus_rtpcache_add_packet(janus_rtpcache_session *session, uint16_t seq_num) {
	if(!session)
		return;
	
	janus_mutex_lock(&session->packet_mutex);
	
	/* Clean up old packets first */
	janus_rtpcache_cleanup_old_packets(session);
	
	/* Create new packet entry */
	janus_rtpcache_packet *new_packet = g_malloc0(sizeof(janus_rtpcache_packet));
	new_packet->seq_num = seq_num;
	new_packet->arrival_time = janus_get_monotonic_time();
	new_packet->next = NULL;
	
	/* Add to tail of list */
	if(session->packet_list_tail) {
		session->packet_list_tail->next = new_packet;
		session->packet_list_tail = new_packet;
	} else {
		session->packet_list_head = session->packet_list_tail = new_packet;
	}
	
	session->packet_count++;
	
	/* Clean up again if we exceeded limits */
	janus_rtpcache_cleanup_old_packets(session);
	
	janus_mutex_unlock(&session->packet_mutex);
}

static json_t *janus_rtpcache_get_batch_and_clear(janus_rtpcache_session *session) {
	if(!session)
		return NULL;
	
	janus_mutex_lock(&session->packet_mutex);
	
	json_t *batch = json_array();
	janus_rtpcache_packet *current = session->packet_list_head;
	
	/* Build JSON array with all cached packets */
	while(current) {
		json_t *packet_info = json_object();
		json_object_set_new(packet_info, "sequence_number", json_integer(current->seq_num));
		json_object_set_new(packet_info, "arrival_timestamp", json_integer(current->arrival_time));
		json_array_append_new(batch, packet_info);
		current = current->next;
	}
	
	/* Clear the cache */
	current = session->packet_list_head;
	while(current) {
		janus_rtpcache_packet *to_remove = current;
		current = current->next;
		g_free(to_remove);
	}
	
	session->packet_list_head = NULL;
	session->packet_list_tail = NULL;
	session->packet_count = 0;
	
	janus_mutex_unlock(&session->packet_mutex);
	
	return batch;
}

/* Plugin implementation */
int janus_rtpcache_init(janus_callbacks *callback, const char *config_path) {
	if(g_atomic_int_get(&stopping)) {
		/* Still stopping from before */
		return -1;
	}
	if(callback == NULL || config_path == NULL) {
		/* Invalid arguments */
		return -1;
	}

	/* Read configuration */
	char filename[255];
	g_snprintf(filename, 255, "%s/%s.jcfg", config_path, JANUS_RTP_CACHE_PACKAGE);
	JANUS_LOG(LOG_VERB, "Configuration file: %s\n", filename);
	
	sessions = g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify)g_free);
	janus_mutex_init(&sessions_mutex);
	
	gateway = callback;
	g_atomic_int_set(&initialized, 1);

	JANUS_LOG(LOG_INFO, "%s initialized!\n", JANUS_RTP_CACHE_NAME);
	return 0;
}

void janus_rtpcache_destroy(void) {
	if(!g_atomic_int_get(&initialized))
		return;
	g_atomic_int_set(&stopping, 1);

	/* Destroy sessions hash table */
	janus_mutex_lock(&sessions_mutex);
	g_hash_table_destroy(sessions);
	sessions = NULL;
	janus_mutex_unlock(&sessions_mutex);

	g_atomic_int_set(&initialized, 0);
	g_atomic_int_set(&stopping, 0);
	JANUS_LOG(LOG_INFO, "%s destroyed!\n", JANUS_RTP_CACHE_NAME);
}

int janus_rtpcache_get_api_compatibility(void) {
	return JANUS_PLUGIN_API_VERSION;
}

int janus_rtpcache_get_version(void) {
	return JANUS_RTP_CACHE_VERSION;
}

const char *janus_rtpcache_get_version_string(void) {
	return JANUS_RTP_CACHE_VERSION_STRING;
}

const char *janus_rtpcache_get_description(void) {
	return JANUS_RTP_CACHE_DESCRIPTION;
}

const char *janus_rtpcache_get_name(void) {
	return JANUS_RTP_CACHE_NAME;
}

const char *janus_rtpcache_get_author(void) {
	return JANUS_RTP_CACHE_AUTHOR;
}

const char *janus_rtpcache_get_package(void) {
	return JANUS_RTP_CACHE_PACKAGE;
}

void janus_rtpcache_create_session(janus_plugin_session *handle, int *error) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized)) {
		*error = -1;
		return;
	}
	
	janus_rtpcache_session *session = g_malloc0(sizeof(janus_rtpcache_session));
	session->handle = handle;
	session->packet_list_head = NULL;
	session->packet_list_tail = NULL;
	session->packet_count = 0;
	janus_mutex_init(&session->packet_mutex);
	g_atomic_int_set(&session->destroyed, 0);

    JANUS_LOG(LOG_INFO, ">>> Creating RTPCache Session: [%"SCNu64"]\n", 0);
        // session->handle->gateway_handle ? ((janus_ice_handle*)session->handle->gateway_handle)->handle_id : 0);
	
	handle->plugin_handle = session;
	
	janus_mutex_lock(&sessions_mutex);
	g_hash_table_insert(sessions, handle, session);
	janus_mutex_unlock(&sessions_mutex);

	return;
}

struct janus_plugin_result *janus_rtpcache_handle_message(janus_plugin_session *handle, char *transaction, json_t *message, json_t *jsep) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return janus_plugin_result_new(JANUS_PLUGIN_ERROR, g_strdup("Plugin not initialized"), NULL);

	janus_rtpcache_session *session = (janus_rtpcache_session *)handle->plugin_handle;
	if(!session || g_atomic_int_get(&session->destroyed))
		return janus_plugin_result_new(JANUS_PLUGIN_ERROR, g_strdup("Session destroyed"), NULL);

	/* Parse the message */
	if(message == NULL)
		return janus_plugin_result_new(JANUS_PLUGIN_ERROR, g_strdup("No message body"), NULL);

	json_t *request = json_object_get(message, "request");
	if(!request)
		return janus_plugin_result_new(JANUS_PLUGIN_ERROR, g_strdup("Missing request"), NULL);

	const char *request_text = json_string_value(request);
	if(!request_text)
		return janus_plugin_result_new(JANUS_PLUGIN_ERROR, g_strdup("Invalid request"), NULL);

	json_t *result = NULL;

	if(!strcasecmp(request_text, "get_batch")) {
		/* Get all cached packets and clear the cache */
		json_t *batch = janus_rtpcache_get_batch_and_clear(session);
		
		result = json_object();
		json_object_set_new(result, "rtpcache", json_string("get_batch"));
		json_object_set_new(result, "packets", batch);
		
		JANUS_LOG(LOG_VERB, "Returning batch of %zu packets\n", json_array_size(batch));
	} else {
		return janus_plugin_result_new(JANUS_PLUGIN_ERROR, g_strdup("Unknown request"), NULL);
	}

	return janus_plugin_result_new(JANUS_PLUGIN_OK, NULL, result);
}

json_t *janus_rtpcache_handle_admin_message(json_t *message) {
	/* We don't handle any admin messages for now */
	return NULL;
}

void janus_rtpcache_setup_media(janus_plugin_session *handle) {
	JANUS_LOG(LOG_INFO, "[%s-%p] WebRTC media is now available\n", JANUS_RTP_CACHE_PACKAGE, handle);
}

void janus_rtpcache_incoming_rtp(janus_plugin_session *handle, janus_plugin_rtp *packet, void *peer_connection) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return;
	
	janus_rtpcache_session *session = (janus_rtpcache_session *)handle->plugin_handle;
	if(!session || g_atomic_int_get(&session->destroyed))
		return;
	
	janus_rtp_header *rtp = (janus_rtp_header *)packet->buffer;
    /* Extract sequence number from RTP header */
	uint16_t seq_num = ntohs(rtp->seq_number);
	
	/* Add packet to cache */
	janus_rtpcache_add_packet(session, seq_num);
	
	/* Forward the packet */
	gateway->relay_rtp(handle, packet);
}

void janus_rtpcache_incoming_rtcp(janus_plugin_session *handle, janus_plugin_rtcp *packet) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return;
	
	/* Just forward RTCP packets */
	gateway->relay_rtcp(handle, packet);
}

void janus_rtpcache_incoming_data(janus_plugin_session *handle, janus_plugin_data *packet) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return;
	
	/* Just forward data packets */
	gateway->relay_data(handle, packet);
}

void janus_rtpcache_slow_link(janus_plugin_session *handle, int uplink, int video) {
	/* Nothing to do here */
}

void janus_rtpcache_hangup_media(janus_plugin_session *handle) {
	JANUS_LOG(LOG_INFO, "[%s-%p] No WebRTC media anymore\n", JANUS_RTP_CACHE_PACKAGE, handle);
}

void janus_rtpcache_destroy_session(janus_plugin_session *handle, int *error) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized)) {
		*error = -1;
		return;
	}
	
	janus_rtpcache_session *session = (janus_rtpcache_session *)handle->plugin_handle;
	if(!session) {
		*error = -2;
		return;
	}
	
	janus_mutex_lock(&sessions_mutex);
	g_hash_table_remove(sessions, handle);
	janus_mutex_unlock(&sessions_mutex);
	
	g_atomic_int_set(&session->destroyed, 1);
	
	/* Clean up packet cache */
	janus_mutex_lock(&session->packet_mutex);
	janus_rtpcache_packet *current = session->packet_list_head;
	while(current) {
		janus_rtpcache_packet *to_remove = current;
		current = current->next;
		g_free(to_remove);
	}
	janus_mutex_unlock(&session->packet_mutex);
	
	g_free(session);
	handle->plugin_handle = NULL;
	
	return;
}

json_t *janus_rtpcache_query_session(janus_plugin_session *handle) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return NULL;
	
	janus_rtpcache_session *session = (janus_rtpcache_session *)handle->plugin_handle;
	if(!session || g_atomic_int_get(&session->destroyed))
		return NULL;
	
	/* Return basic session info */
	json_t *info = json_object();
	json_object_set_new(info, "packet_count", json_integer(session->packet_count));
	return info;
}