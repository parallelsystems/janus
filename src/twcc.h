/*! \file    twcc.h
 * \brief    Transport-wide CC receive-side helpers
 * \details  Helpers used by the ICE stack to generate transport-wide
 * congestion control (TWCC) feedback in a way that is robust to packets
 * arriving out of order or late:
 *
 * - janus_twcc_unwrapper extends the 16-bit transport-wide sequence numbers
 *   to 64 bits using the signed distance from the highest number seen so
 *   far, so a reordered packet around a wrap can't skew the extended space;
 * - janus_twcc_staging keeps a contiguous window of received/missing
 *   sequence numbers, and only releases a missing one (as "not received")
 *   once it's been missing for a configurable grace period. Everything
 *   before the first unexpired gap is released immediately, so in-order
 *   traffic is never delayed.
 *
 * These helpers only depend on GLib, and are not thread-safe: the caller
 * is responsible for serializing access.
 *
 * \ingroup protocols
 * \ref protocols
 */

#ifndef JANUS_TWCC_H
#define JANUS_TWCC_H

#include <glib.h>

/*! \brief Maximum distance (in sequence numbers) from the start of the
 * staging window a packet can have: anything further away is considered
 * a discontinuity, and causes the window to be flushed and restarted */
#define JANUS_TWCC_MAX_STAGED	0x4000

/*! \brief Transport-wide sequence number unwrapper */
typedef struct janus_twcc_unwrapper {
	/*! \brief Whether we've seen any sequence number yet */
	gboolean init;
	/*! \brief Highest extended sequence number seen so far */
	gint64 highest;
} janus_twcc_unwrapper;

/*! \brief Extend a 16-bit transport-wide sequence number to 64 bits
 * @note Packets older than the highest one seen (i.e., out of order) are
 * mapped to their correct position in the same cycle, or the previous one,
 * and never move the reference forward
 * @param[in] u The unwrapper instance
 * @param[in] seq The 16-bit sequence number in the packet
 * @returns The extended sequence number */
gint64 janus_twcc_unwrap(janus_twcc_unwrapper *u, guint16 seq);

/*! \brief Staging window for TWCC feedback */
typedef struct janus_twcc_staging {
	/*! \brief Whether start is valid (i.e., whether we've ever staged anything) */
	gboolean started;
	/*! \brief Extended sequence number of the first slot in arrivals */
	gint64 start;
	/*! \brief Arrival times (gint64, monotonic microseconds) indexed by
	 * (seq - start), with 0 meaning "not received (yet)" */
	GArray *arrivals;
} janus_twcc_staging;

/*! \brief Result of janus_twcc_staging_add */
typedef enum janus_twcc_add_result {
	/*! \brief The packet was staged */
	JANUS_TWCC_ADDED = 0,
	/*! \brief The packet is older than the window (already reported): ignored */
	JANUS_TWCC_LATE,
	/*! \brief The packet was already staged: ignored */
	JANUS_TWCC_DUPLICATE,
	/*! \brief The packet is too far ahead of the window: the caller should
	 * release everything staged, clear the window, and then add it again */
	JANUS_TWCC_TOO_FAR,
} janus_twcc_add_result;

/*! \brief Free the resources of a staging window, and reset it */
void janus_twcc_staging_clear(janus_twcc_staging *s);

/*! \brief Stage a received packet
 * @param[in] s The staging window
 * @param[in] seq The extended transport-wide sequence number
 * @param[in] arrival The arrival time (monotonic, microseconds, must be > 0)
 * @returns A janus_twcc_add_result value */
janus_twcc_add_result janus_twcc_staging_add(janus_twcc_staging *s, gint64 seq, gint64 arrival);

/*! \brief Number of staged slots */
guint janus_twcc_staging_length(janus_twcc_staging *s);

/*! \brief Compute how many slots, from the start of the window, can be
 * released as feedback now
 * \details All received packets before the first missing one can always be
 * released. A missing packet becomes visible when a later sequence number
 * arrives: it's released (as "not received") only once it's been visible
 * for at least \c grace microseconds
 * @param[in] s The staging window
 * @param[in] now The current monotonic time, in microseconds
 * @param[in] grace How long a gap is held back waiting for reordered/late
 * packets, in microseconds (0 releases everything immediately)
 * @returns The number of slots that can be released */
guint janus_twcc_staging_releasable(janus_twcc_staging *s, gint64 now, gint64 grace);

/*! \brief Get the content of a staged slot
 * @param[in] s The staging window
 * @param[in] index The slot index (0 is the start of the window)
 * @param[out] seq The extended sequence number of the slot
 * @returns The arrival time, or 0 if the packet was not received */
gint64 janus_twcc_staging_get(janus_twcc_staging *s, guint index, gint64 *seq);

/*! \brief Drop the first \c count slots from the window (after they've been
 * released as feedback); anything older than the new start will be
 * considered late from now on */
void janus_twcc_staging_consume(janus_twcc_staging *s, guint count);

#endif
