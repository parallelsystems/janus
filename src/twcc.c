/*! \file    twcc.c
 * \brief    Transport-wide CC receive-side helpers
 * \details  See twcc.h for details.
 *
 * \ingroup protocols
 * \ref protocols
 */

#include "twcc.h"

gint64 janus_twcc_unwrap(janus_twcc_unwrapper *u, guint16 seq) {
	if(!u->init) {
		u->init = TRUE;
		u->highest = seq;
		return seq;
	}
	/* Signed distance from the highest sequence number seen so far:
	 * anything within +/- 32767 is mapped to the closest candidate */
	gint16 diff = (gint16)(guint16)(seq - (guint16)(u->highest & 0xFFFF));
	gint64 ext = u->highest + diff;
	if(ext > u->highest)
		u->highest = ext;
	return ext;
}

void janus_twcc_staging_clear(janus_twcc_staging *s) {
	if(s->arrivals != NULL)
		g_array_free(s->arrivals, TRUE);
	s->arrivals = NULL;
	s->started = FALSE;
	s->start = 0;
}

janus_twcc_add_result janus_twcc_staging_add(janus_twcc_staging *s, gint64 seq, gint64 arrival) {
	if(s->arrivals == NULL)
		s->arrivals = g_array_new(FALSE, TRUE, sizeof(gint64));
	if(!s->started) {
		s->started = TRUE;
		s->start = seq;
	}
	if(seq < s->start)
		return JANUS_TWCC_LATE;
	if(seq - s->start >= JANUS_TWCC_MAX_STAGED) {
		if(s->arrivals->len > 0)
			return JANUS_TWCC_TOO_FAR;
		/* Nothing pending: just restart the window from here */
		s->start = seq;
	}
	guint index = (guint)(seq - s->start);
	if(index >= s->arrivals->len) {
		/* Grow the window: the new slots are zeroed, i.e., not received */
		g_array_set_size(s->arrivals, index + 1);
	}
	gint64 *slot = &g_array_index(s->arrivals, gint64, index);
	if(*slot != 0)
		return JANUS_TWCC_DUPLICATE;
	*slot = arrival > 0 ? arrival : 1;
	return JANUS_TWCC_ADDED;
}

guint janus_twcc_staging_length(janus_twcc_staging *s) {
	return s->arrivals ? s->arrivals->len : 0;
}

guint janus_twcc_staging_releasable(janus_twcc_staging *s, gint64 now, gint64 grace) {
	guint len = janus_twcc_staging_length(s);
	if(len == 0)
		return 0;
	gint64 *a = (gint64 *)s->arrivals->data;
	/* Walk backwards, keeping track of the earliest arrival among the slots
	 * after the current one: that's when a gap in the current slot became
	 * visible. The first gap still within the grace period (from the start)
	 * is where we have to stop */
	gint64 earliest_after = G_MAXINT64;
	guint release = len;
	guint i = len;
	while(i-- > 0) {
		if(a[i] == 0) {
			if(earliest_after == G_MAXINT64 || now - earliest_after < grace)
				release = i;
		} else if(a[i] < earliest_after) {
			earliest_after = a[i];
		}
	}
	return release;
}

gint64 janus_twcc_staging_get(janus_twcc_staging *s, guint index, gint64 *seq) {
	if(seq)
		*seq = s->start + index;
	if(index >= janus_twcc_staging_length(s))
		return 0;
	return g_array_index(s->arrivals, gint64, index);
}

void janus_twcc_staging_consume(janus_twcc_staging *s, guint count) {
	guint len = janus_twcc_staging_length(s);
	if(count == 0 || len == 0)
		return;
	if(count > len)
		count = len;
	g_array_remove_range(s->arrivals, 0, count);
	s->start += count;
}
