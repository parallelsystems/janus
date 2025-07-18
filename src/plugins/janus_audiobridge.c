/*! \file   janus_audiobridge.c
 * \author Lorenzo Miniero <lorenzo@meetecho.com>
 * \copyright GNU General Public License v3
 * \brief  Janus AudioBridge plugin
 * \details Check the \ref audiobridge for more details.
 *
 * \ingroup plugins
 * \ref plugins
 *
 * \page audiobridge AudioBridge plugin documentation
 * This is a plugin implementing an audio conference bridge for
 * Janus, specifically mixing Opus streams. This means that it replies
 * by providing in the SDP only support for Opus, and disabling video.
 * Opus encoding and decoding is implemented using libopus (http://opus.codec.org).
 * The plugin provides an API to allow peers to join and leave conference
 * rooms. Peers can then mute/unmute themselves by sending specific messages
 * to the plugin: any way a peer mutes/unmutes, an event is triggered
 * to the other participants, so that it can be rendered in the UI
 * accordingly.
 *
 * Rooms to make available are listed in the plugin configuration file.
 * A pre-filled configuration file is provided in \c conf/janus.plugin.audiobridge.jcfg
 * and includes a demo room for testing.
 *
 * To add more rooms or modify the existing one, you can use the following
 * syntax:
 *
 * \verbatim
room-<unique room ID>: {
	description = This is my awesome room
	is_private = true|false (private rooms don't appear when you do a 'list' request)
	secret = <optional password needed for manipulating (e.g. destroying) the room>
	pin = <optional password needed for joining the room>
	sampling_rate = <sampling rate> (e.g., 16000 for wideband mixing)
	spatial_audio = true|false (if true, the mix will be stereo to spatially place users, default=false)
	audiolevel_ext = true|false (whether the ssrc-audio-level RTP extension must be
		negotiated/used or not for new joins, default=true)
	audiolevel_event = true|false (whether to emit event to other users or not, default=false)
	audio_active_packets = 100 (number of packets with audio level, default=100, 2 seconds)
	audio_level_average = 25 (average value of audio level, 127=muted, 0='too loud', default=25)
	default_prebuffering = number of packets to buffer before decoding each participant (default=DEFAULT_PREBUFFERING)
	default_expectedloss = percent of packets we expect participants may miss, to help with FEC (default=0, max=20; automatically used for forwarders too)
	default_bitrate = default bitrate in bps to use for the all participants (default=0, which means libopus decides; automatically used for forwarders too)
	record = true|false (whether this room should be recorded, default=false)
	record_file = /path/to/recording.wav (where to save the recording)
	record_dir = /path/to/ (path to save the recording to, makes record_file a relative path if provided)
	mjrs = true|false (whether all participants in the room should be individually recorded to mjr files, default=false)
	mjrs_dir = "/path/to/" (path to save the mjr files to)
	allow_rtp_participants = true|false (whether participants should be allowed to join
		via plain RTP as well, rather than just WebRTC, default=false)
	groups = optional, non-hierarchical, array of groups to tag participants, for external forwarding purposes only

		[The following lines are only needed if you want the mixed audio
		to be automatically forwarded via plain RTP to an external component
		(e.g., an ffmpeg script, or a gstreamer pipeline) for processing.
		By default plain RTP is used, SRTP must be configured if needed]
	rtp_forward_id = numeric RTP forwarder ID for referencing it via API (optional: random ID used if missing)
	rtp_forward_host = host address to forward RTP packets of mixed audio to
	rtp_forward_host_family = ipv4|ipv6; by default, first family returned by DNS request
	rtp_forward_port = port to forward RTP packets of mixed audio to
	rtp_forward_ssrc = SSRC to use to use when streaming (optional: stream_id used if missing)
	rtp_forward_codec = opus (default), pcma (A-Law) or pcmu (mu-Law)
	rtp_forward_ptype = payload type to use when streaming (optional: only read for Opus, 100 used if missing)
	rtp_forward_group = group of participants to forward, if enabled in the room (optional: forwards full mix if missing)
	rtp_forward_srtp_suite = length of authentication tag, if SRTP is needed (32 or 80)
	rtp_forward_srtp_crypto = key to use as crypto, if SRTP is needed (base64 encoded key as in SDES)
	rtp_forward_always_on = true|false, whether silence should be forwarded when the room is empty (optional: false used if missing)
}
\endverbatim
 *
 * \section bridgeapi Audio Bridge API
 *
 * The Audio Bridge API supports several requests, some of which are
 * synchronous and some asynchronous. There are some situations, though,
 * (invalid JSON, invalid request) which will always result in a
 * synchronous error response even for asynchronous requests.
 *
 * \c create , \c edit , \c destroy , \c exists, \c allowed, \c kick, \c list,
 * \c mute , \c unmute , \c mute_room , \c unmute_room , \c listparticipants ,
 * \c resetdecoder , \c rtp_forward, \c stop_rtp_forward , \c list_forwarders ,
 * \c play_file , \c is_playing and \c stop_file are synchronous requests,
 * which means you'll get a response directly within the context of the
 * transaction. \c create allows you to create a new audio conference bridge
 * dynamically, as an alternative to using the configuration file; \c edit
 * allows you to dynamically edit some room properties (e.g., the PIN);
 * \c destroy removes an audio conference bridge and destroys it, kicking
 * all the users out as part of the process; \c exists allows you to
 * check whether a specific audio conference exists; \c allowed allows
 * you to edit who's allowed to join a room via ad-hoc tokens; \c list
 * lists all the available rooms, while \c listparticipants lists all
 * the participants of a specific room and their details; \c resetdecoder
 * marks the Opus decoder for the participant as invalid, and forces it
 * to be recreated (which might be needed if the audio for generated by
 * the participant becomes garbled); \c rtp_forward allows you to forward
 * the mix of an AudioBridge room via RTP to a separate component (e.g.,
 * for broadcasting it to a wider audience, or for processing/recording),
 * whereas \c stop_rtp_forward can remove an existing forwarder; a list
 * of configured forwarders for a room can be retrieved using the
 * \c list_forwarders request; finally, \c play_file allows you to
 * reproduce an audio .opus file in a mix (e.g., to play an announcement
 * or some background music), \c is_playing checks if a specific file is
 * still playing, while \c stop_file will stop such a playback instead.
 *
 * The \c join , \c configure , \c changeroom and \c leave requests
 * instead are all asynchronous, which means you'll get a notification
 * about their success or failure in an event. \c join allows you to
 * join a specific audio conference bridge; \c configure can be used
 * to modify some of the participation settings (e.g., mute/unmute);
 * \c changeroom can be used to leave the current room and move to a
 * different one without having to tear down the PeerConnection and
 * recreate it again (useful for sidebars and "waiting rooms"); finally,
 * \c leave allows you to leave an audio conference bridge for good.
 *
 * The AudioBridge plugin also allows you to forward the mix to an
 * external listener, e.g., a gstreamer/ffmpeg pipeline waiting to
 * process the mixer audio stream. You can add new RTP forwarders with
 * the \c rtp_forward request; a \c stop_rtp_forward request removes an
 * existing RTP forwarder; \c listforwarders lists all the current RTP
 * forwarders on a specific AudioBridge room instance. As an alternative,
 * you can configure a single static RTP forwarder in the plugin
 * configuration file. A finer grained control of what to forward
 * externally, in terms of participants mix, can be achieved using
 * groups.
 *
 * \c create can be used to create a new audio room, and has to be
 * formatted as follows:
 *
\verbatim
{
	"request" : "create",
	"room" : <unique numeric ID, optional, chosen by plugin if missing>,
	"permanent" : <true|false, whether the room should be saved in the config file, default=false>,
	"description" : "<pretty name of the room, optional>",
	"secret" : "<password required to edit/destroy the room, optional>",
	"pin" : "<password required to join the room, optional>",
	"is_private" : <true|false, whether the room should appear in a list request>,
	"allowed" : [ array of string tokens users can use to join this room, optional],
	"sampling_rate" : <sampling rate of the room, optional, 16000 by default>,
	"spatial_audio" : <true|false, whether the mix should spatially place users, default=false>,
	"audiolevel_ext" : <true|false, whether the ssrc-audio-level RTP extension must be negotiated for new joins, default=true>,
	"audiolevel_event" : <true|false (whether to emit event to other users or not)>,
	"audio_active_packets" : <number of packets with audio level (default=100, 2 seconds)>,
	"audio_level_average" : <average value of audio level (127=muted, 0='too loud', default=25)>,
	"default_prebuffering" : <number of packets to buffer before decoding each participant (default=DEFAULT_PREBUFFERING)>,
	"default_expectedloss" : <percent of packets we expect participants may miss, to help with FEC (default=0, max=20; automatically used for forwarders too)>,
	"default_bitrate" : <bitrate in bps to use for the all participants (default=0, which means libopus decides; automatically used for forwarders too)>,
	"record" : <true|false, whether to record the room or not, default=false>,
	"record_file" : "</path/to/the/recording.wav, optional>",
	"record_dir" : "</path/to/, optional; makes record_file a relative path, if provided>",
	"mjrs" : <true|false (whether all participants in the room should be individually recorded to mjr files, default=false)>,
	"mjrs_dir" : "</path/to/, optional>",
	"allow_rtp_participants" : <true|false, whether participants should be allowed to join via plain RTP as well, default=false>,
	"groups" : [ non-hierarchical array of string group names to use to gat participants, for external forwarding purposes only, optional]
}
\endverbatim
 *
 * A successful creation procedure will result in a \c created response:
 *
\verbatim
{
	"audiobridge" : "created",
	"room" : <unique numeric ID>,
	"permanent" : <true if saved to config file, false if not>
}
\endverbatim
 *
 * If you requested a permanent room but a \c false value is returned
 * instead, good chances are that there are permission problems.
 *
 * An error instead (and the same applies to all other requests, so this
 * won't be repeated) would provide both an error code and a more verbose
 * description of the cause of the issue:
 *
\verbatim
{
	"audiobridge" : "event",
	"error_code" : <numeric ID, check Macros below>,
	"error" : "<error description as a string>"
}
\endverbatim
 *
 * Notice that, in general, all users can create rooms. If you want to
 * limit this functionality, you can configure an admin \c admin_key in
 * the plugin settings. When configured, only "create" requests that
 * include the correct \c admin_key value in an "admin_key" property
 * will succeed, and will be rejected otherwise. Notice that you can
 * optionally extend this functionality to RTP forwarding as well, in
 * order to only allow trusted clients to use that feature.
 *
 * Once a room has been created, you can still edit some (but not all)
 * of its properties using the \c edit request. This allows you to modify
 * the room description, secret, pin and whether it's private or not: you
 * won't be able to modify other more static properties, like the room ID,
 * the sampling rate, the extensions-related stuff and so on. If you're
 * interested in changing the ACL, instead, check the \c allowed message.
 * An \c edit request has to be formatted as follows:
 *
\verbatim
{
	"request" : "edit",
	"room" : <unique numeric ID of the room to edit>,
	"secret" : "<room secret, mandatory if configured>",
	"new_description" : "<new pretty name of the room, optional>",
	"new_secret" : "<new password required to edit/destroy the room, optional>",
	"new_pin" : "<new PIN required to join the room, PIN will be removed if set to an empty string, optional>",
	"new_is_private" : <true|false, whether the room should appear in a list request>,
	"new_record_dir" : "<new path where new recording files should be saved>",
	"new_mjrs_dir" : "<new path where new MJR files should be saved>",
	"permanent" : <true|false, whether the room should be also removed from the config file, default=false>
}
\endverbatim
 *
 * A successful edit procedure will result in an \c edited response:
 *
\verbatim
{
	"audiobridge" : "edited",
	"room" : <unique numeric ID>
}
\endverbatim
 *
 * On the other hand, \c destroy can be used to destroy an existing audio
 * room, whether created dynamically or statically, and has to be
 * formatted as follows:
 *
\verbatim
{
	"request" : "destroy",
	"room" : <unique numeric ID of the room to destroy>,
	"secret" : "<room secret, mandatory if configured>",
	"permanent" : <true|false, whether the room should be also removed from the config file, default=false>
}
\endverbatim
 *
 * A successful destruction procedure will result in a \c destroyed response:
 *
\verbatim
{
	"audiobridge" : "destroyed",
	"room" : <unique numeric ID>
}
\endverbatim
 *
 * This will also result in a \c destroyed event being sent to all the
 * participants in the audio room, which will look like this:
 *
\verbatim
{
	"audiobridge" : "destroyed",
	"room" : <unique numeric ID of the destroyed room>
}
\endverbatim
 *
 * To enable or disable recording of mixed audio stream while the conference
 * is in progress, you can make use of the \c enable_recording request,
 * which has to be formatted as follows:
 *
\verbatim
{
	"request" : "enable_recording",
	"room" : <unique numeric ID of the room>,
	"secret" : "<room secret; mandatory if configured>"
	"record" : <true|false, whether this room should be automatically recorded or not>,
	"record_file" : "<file where audio recording should be saved (optional)>",
	"record_dir" : "<path where audio recording file should be saved (optional)>"
}
\endverbatim
 *
 * A room can also be recorded by saving the individual contributions of
 * participants to separate MJR files instead, in a format compatible with
 * the \ref recordings. While a recording for each participant can be
 * enabled or disabled separately, there also is a request to enable or
 * disable them in bulk, thus implementing a feature similar to \c enable_recording
 * but for MJR files, rather than for a \c .wav mix. This can be done using
 * the \c enable_mjrs request, which has to be formatted as follows:
 *
\verbatim
{
	"request" : "enable_mjrs",
	"room" : <unique numeric ID of the room>,
	"secret" : "<room secret; mandatory if configured>"
	"mjrs" : <true|false, whether all participants in the room should be individually recorded to mjr files or not>,
	"mjrs_dir" : "<path where all MJR files should be saved to (optional)>"
}
\endverbatim
 *
 *
 * You can check whether a room exists using the \c exists request,
 * which has to be formatted as follows:
 *
\verbatim
{
	"request" : "exists",
	"room" : <unique numeric ID of the room to check>
}
\endverbatim
 *
 * A successful request will result in a \c success response:
 *
\verbatim
{
	"audiobridge" : "success",
	"room" : <unique numeric ID>,
	"exists" : <true|false>
}
\endverbatim
 *
 * You can configure whether to check tokens or add/remove people who can join
 * a room using the \c allowed request, which has to be formatted as follows:
 *
\verbatim
{
	"request" : "allowed",
	"secret" : "<room secret, mandatory if configured>",
	"action" : "enable|disable|add|remove",
	"room" : <unique numeric ID of the room to update>,
	"allowed" : [
		// Array of strings (tokens users might pass in "join", only for add|remove)
	]
}
\endverbatim
 *
 * A successful request will result in a \c success response:
 *
\verbatim
{
	"audiobridge" : "success",
	"room" : <unique numeric ID>,
	"allowed" : [
		// Updated, complete, list of allowed tokens (only for enable|add|remove)
	]
}
\endverbatim
 *
 * If you're the administrator of a room (that is, you created it and have access
 * to the secret) you can kick participants using the \c kick request. Notice
 * that this only kicks the user out of the room, but does not prevent them from
 * re-joining: to ban them, you need to first remove them from the list of
 * authorized users (see \c allowed request) and then \c kick them. The \c kick
 * request has to be formatted as follows:
 *
\verbatim
{
	"request" : "kick",
	"secret" : "<room secret, mandatory if configured>",
	"room" : <unique numeric ID of the room>,
	"id" : <unique numeric ID of the participant to kick>
}
\endverbatim
 *
 * A successful request will result in a \c success response:
 *
\verbatim
{
	"audiobridge" : "success",
}
\endverbatim
 *
 * If you're the administrator of a room (that is, you created it and have access
 * to the secret) you can kick all participants using the \c kick_all request. Notice
 * that this only kicks all users out of the room, but does not prevent them from
 * re-joining: to ban them, you need to first remove them from the list of
 * authorized users (see \c allowed request) and then perform \c kick_all.
 * The \c kick_all request has to be formatted as follows:
 *
\verbatim
{
	"request" : "kick_all",
	"secret" : "<room secret, mandatory if configured>",
	"room" : <unique numeric ID of the room>
}
\endverbatim
 *
 * A successful request will result in a \c success response:
 *
\verbatim
{
	"audiobridge" : "success",
}
\endverbatim
 *
 * To get a list of the available rooms (excluded those configured or
 * created as private rooms) you can make use of the \c list request,
 * which has to be formatted as follows:
 *
\verbatim
{
	"request" : "list"
}
\endverbatim
 *
 * A successful request will produce a list of rooms in a \c success response:
 *
\verbatim
{
	"audiobridge" : "success",
	"rooms" : [		// Array of room objects
		{	// Room #1
			"room" : <unique numeric ID>,
			"description" : "<Name of the room>",
			"pin_required" : <true|false, whether a PIN is required to join this room>,
			"sampling_rate" : <sampling rate of the mixer>,
			"spatial_audio" : <true|false, whether the mix has spatial audio (stereo)>,
			"record" : <true|false, whether the room is being recorded>,
			"num_participants" : <count of the participants>
		},
		// Other rooms
	]
}
\endverbatim
 *
 * To get a list of the participants in a specific room, instead, you
 * can make use of the \c listparticipants request, which has to be
 * formatted as follows:
 *
\verbatim
{
	"request" : "listparticipants",
	"room" : <unique numeric ID of the room>
}
\endverbatim
 *
 * A successful request will produce a list of participants in a
 * \c participants response:
 *
\verbatim
{
	"audiobridge" : "participants",
	"room" : <unique numeric ID of the room>,
	"participants" : [		// Array of participant objects
		{	// Participant #1
			"id" : <unique numeric ID of the participant>,
			"display" : "<display name of the participant, if any; optional>",
			"setup" : <true|false, whether user successfully negotiate a WebRTC PeerConnection or not>,
			"muted" : <true|false, whether user is muted or not>,
			"talking" : <true|false, whether user is talking or not (only if audio levels are used)>,
			"spatial_position" : <in case spatial audio is used, the panning of this participant (0=left, 50=center, 100=right)>,
		},
		// Other participants
	]
}
\endverbatim
 *
 * To mark the Opus decoder context for the current participant as
 * invalid and force it to be recreated, use the \c resetdecoder request:
 *
\verbatim
{
	"request" : "resetdecoder"
}
\endverbatim
 *
 * A successful request will produce a \c success response:
 *
\verbatim
{
	"audiobridge" : "success"
}
\endverbatim
 *
 * You can add a new RTP forwarder for an existing room using the
 * \c rtp_forward request, which has to be formatted as follows:
 *
\verbatim
{
	"request" : "rtp_forward",
	"room" : <unique numeric ID of the room to add the forwarder to>,
	"group" : "<group to forward, if enabled in the room (forwards full mix if missing)>",
	"ssrc" : <SSRC to use to use when streaming (optional: stream_id used if missing)>,
	"codec" : "<opus (default), pcma (A-Law) or pcmu (mu-Law)>",
	"ptype" : <payload type to use when streaming (optional: 100 used if missing)>,
	"host" : "<host address to forward the RTP packets to>",
	"host_family" : "<ipv4|ipv6, if we need to resolve the host address to an IP; by default, whatever we get>",
	"port" : <port to forward the RTP packets to>,
	"srtp_suite" : <length of authentication tag (32 or 80); optional>,
	"srtp_crypto" : "<key to use as crypto (base64 encoded key as in SDES); optional>",
	"always_on" : <true|false, whether silence should be forwarded when the room is empty>
}
\endverbatim
 *
 * The concept of "groups" is particularly important, here, in case groups were
 * enabled when creating a room. By default, in fact, if a room has groups disabled,
 * then an RTP forwarder will simply relay the mix of all active participants;
 * sometimes, though, an external application may want to only receive the mix
 * of some of the participants, and not all of them. This is what groups are
 * for: if you tag participants with a specific group name, then creating a
 * new forwarder that explicitly references that group name will ensure that
 * only a mix of the participants tagged with that name will be forwarded.
 * As such, it's important to point out groups \b only impact forwarders,
 * and \c NOT participants or how they're mixed in main mix for the room itself.
 * Omitting a group name when creating a forwarder for a room where groups
 * are enabled will simply fall back to the default behaviour of forwarding
 * the full mix.
 *
 * Notice that, as explained above, in case you configured an \c admin_key
 * property and extended it to RTP forwarding as well, you'll need to provide
 * it in the request as well or it will be rejected as unauthorized. By
 * default no limitation is posed on \c rtp_forward .
 *
 * A successful request will result in a \c success response:
 *
\verbatim
{
	"audiobridge" : "success",
	"room" : <unique numeric ID, same as request>,
	"group" : "<group to forward, same as request if provided>",
	"stream_id" : <unique numeric ID assigned to the new RTP forwarder>,
	"host" : "<host this forwarder is streaming to, same as request if not resolved>",
	"port" : <audio port this forwarder is streaming to, same as request>
}
\endverbatim
 *
 * To stop a previously created RTP forwarder and stop it, you can use
 * the \c stop_rtp_forward request, which has to be formatted as follows:
 *
\verbatim
{
	"request" : "stop_rtp_forward",
	"room" : <unique numeric ID of the room to remove the forwarder from>,
	"stream_id" : <unique numeric ID of the RTP forwarder>
}
\endverbatim
 *
 * A successful request will result in a \c success response:
 *
\verbatim
{
	"audiobridge" : "success",
	"room" : <unique numeric ID, same as request>,
	"stream_id" : <unique numeric ID, same as request>
}
\endverbatim
 *
 * To get a list of the forwarders in a specific room, instead, you
 * can make use of the \c listforwarders request, which has to be
 * formatted as follows:
 *
\verbatim
{
	"request" : "listforwarders",
	"room" : <unique numeric ID of the room>
}
\endverbatim
 *
 * A successful request will produce a list of RTP forwarders in a
 * \c forwarders response:
 *
\verbatim
{
	"audiobridge" : "forwarders",
	"room" : <unique numeric ID of the room>,
	"rtp_forwarders" : [		// Array of RTP forwarder objects
		{	// RTP forwarder #1
			"stream_id" : <unique numeric ID of the forwarder>,
			"group" : "<group that is being forwarded, if available>",
			"ip" : "<IP this forwarder is streaming to>",
			"port" : <port this forwarder is streaming to>,
			"ssrc" : <SSRC this forwarder is using, if any>,
			"codec" : <codec this forwarder is using, if any>,
			"ptype" : <payload type this forwarder is using, if any>,
			"srtp" : <true|false, whether the RTP stream is encrypted>,
			"always_on" : <true|false, whether this forwarder works even when no participant is in or not>
		},
		// Other forwarders
	]
}
\endverbatim
 *
 * As anticipated, while the AudioBridge is mainly meant to allow real users
 * to interact with each other by mixing their contributions, you can also
 * start the playback of one or more pre-recorded audio files in a mix:
 * this is especially useful whenever you have, for instance, to play
 * an announcement of some sort, or when maybe you want to play some
 * background music (e.g., some music on hold when the room is empty).
 * You can start the playback of an .opus file in an existing room using
 * the \c play_file request, which has to be formatted as follows:
 *
\verbatim
{
	"request" : "play_file",
	"room" : <unique numeric ID of the room to play the file in>,
	"secret" : "<room password, if configured>",
	"group" : "<group to play in (for forwarding purposes only; optional, mandatory if enabled in the room)>",
	"file_id": "<unique string ID of the announcement; random if not provided>",
	"filename": "<path to the Opus file to play>",
	"loop": <true|false, depending on whether or not the file should be played in a loop forever>
}
\endverbatim
 *
 * Notice that, as explained above, in case you configured an \c admin_key
 * property and extended it to RTP forwarding as well, you'll need to provide
 * it in the request as well or it will be rejected as unauthorized. By
 * default \c play_file only requires the room secret, meaning only people
 * authorized to edit the room can start an audio playback.
 *
 * Also notice that the only supported files are .opus files: no other
 * audio format will be accepted. Besides, the file must be reachable
 * and available on the file system: network addresses (e.g., HTTP URL)
 * are NOT supported.
 *
 * A successful request will result in a \c success response:
 *
\verbatim
{
	"audiobridge" : "success",
	"room" : <unique numeric ID, same as request>,
	"file_id" : "<unique string ID of the announcement, same as request if provided or randomly generated otherwise>"
}
\endverbatim
 *
 * As soon as the playback actually starts (usually immediately after
 * the request has been sent), an event is sent to all participants so
 * that they're aware something is being played back in the room besides
 * themselves:
 *
\verbatim
{
	"audiobridge" : "announcement-started",
	"room" : <unique numeric ID, same as request>,
	"file_id" : "<unique string ID of the announcement>"
}
\endverbatim
 *
 * A similar event is also sent whenever the playback stops, whether it's
 * because the file ended and \c loop was \c FALSE (which will automatically
 * clear the resources) or because a \c stop_file request asked for the
 * playback to be interrupted:
 *
\verbatim
{
	"audiobridge" : "announcement-stopped",
	"room" : <unique numeric ID, same as request>,
	"file_id" : "<unique string ID of the announcement>"
}
\endverbatim
 *
 * You can check whether a specific playback is still going on in a room,
 * you can use the \c is_playing request, which has to be formatted as follows:
 *
\verbatim
{
	"request" : "is_playing",
	"room" : <unique numeric ID of the room where the playback is taking place>,
	"secret" : "<room password, if configured>",
	"file_id" : "<unique string ID of the announcement>"
}
\endverbatim
 *
 * A successful request will result in a \c success response:
 *
\verbatim
{
	"audiobridge" : "success",
	"room" : <unique numeric ID>,
	"file_id" : "<unique string ID of the announcement>",
	"playing" : <true|false>
}
\endverbatim
 *
 * As anticipated, when not looping a playback will automatically stop and
 * self-destruct when it reaches the end of the audio file. In case you
 * want to stop a playback sooner than that, or want to stop a looped
 * playback, you can use the \c stop_file request:
 *
\verbatim
{
	"request" : "stop_file",
	"room" : <unique numeric ID of the room where the playback is taking place>,
	"secret" : "<room password, if configured>",
	"file_id": "<unique string ID of the announcement>"
}
\endverbatim
 *
 * A successful request will result in a \c success response:
 *
\verbatim
{
	"audiobridge" : "success",
	"room" : <unique numeric ID, same as request>,
	"file_id" : "<unique string ID of the now interrupted announcement>"
}
\endverbatim
 *
 * That completes the list of synchronous requests you can send to the
 * AudioBridge plugin. As anticipated, though, there are also several
 * asynchronous requests you can send, specifically those related to
 * joining and updating one's presence as a participant in an audio room.
 *
 * The way you'd interact with the plugin is usually as follows:
 *
 * -# you use a \c join request to join an audio room, and wait for the
 * \c joined event; this event will also include a list of the other
 * participants, if any;
 * -# you send a \c configure request attached to an audio-only JSEP offer
 * to start configuring your participation in the room (e.g., join unmuted
 * or muted), and wait for the related \c event, which will be attached
 * to a JSEP answer by the plugin to complete the setup of the WebRTC
 * PeerConnection;
 * -# you send other \c configure requests (without any JSEP-related
 * attachment) to mute/unmute yourself during the audio conference;
 * -# you intercept events originated by the plugin (\c joined , \c leaving )
 * to notify you about users joining/leaving/muting/unmuting;
 * -# you eventually send a \c leave request to leave a room; if you leave the
 * PeerConnection instance intact, you can subsequently join a different
 * room without requiring a new negotiation (and so just use a \c join + JSEP-less \c configure to join).
 *
 * Notice that there's also a \c changeroom request available: you can use
 * this request to immediately leave the room you're in and join a different
 * one, without requiring you to do a \c leave + \c join + \c configure
 * round. Of course remember not to pass any JSEP-related payload when
 * doing a \c changeroom as the same pre-existing PeerConnection will be
 * re-used for the purpose.
 *
 * Notice that you can also ask the AudioBridge plugin to send you an offer,
 * when you join, rather than providing one yourself: this means that the
 * SDP offer/answer roles would be reversed, and so you'd have to provide
 * an answer yourself in this case. Remember that, in case renegotiations
 * or restarts take place, they MUST follow the same negotiation pattern
 * as the one that originated the connection: it's an error to send an
 * SDP offer to the plugin to update a PeerConnection, if the plugin sent
 * you an offer originally. It's adviced to let users generate the offer,
 * and let the plugin answer: this reverserd role is mostly here to
 * facilitate the setup of cascaded mixers, e.g., allow one AudioBridge
 * to connect to the other via WebRTC (which wouldn't be possible if
 * both expected an offer from the other). Refer to the \ref aboffer
 * section for more details.
 *
 * About the syntax of all the above mentioned requests, \c join has
 * to be formatted as follows:
 *
\verbatim
{
	"request" : "join",
	"room" : <numeric ID of the room to join>,
	"id" : <unique ID to assign to the participant; optional, assigned by the plugin if missing>,
	"group" : "<group to assign to this participant (for forwarding purposes only; optional, mandatory if enabled in the room)>",
	"pin" : "<password required to join the room, if any; optional>",
	"display" : "<display name to have in the room; optional>",
	"token" : "<invitation token, in case the room has an ACL; optional>",
	"muted" : <true|false, whether to start unmuted or muted>,
	"codec" : "<codec to use, among opus (default), pcma (A-Law) or pcmu (mu-Law)>",
	"prebuffer" : <number of packets to buffer before decoding this participant (default=room value, or DEFAULT_PREBUFFERING)>,
	"bitrate" : <bitrate to use for the Opus stream in bps; optional, default=0 (libopus decides)>,
	"quality" : <0-10, Opus-related complexity to use, the higher the value, the better the quality (but more CPU); optional, default is 4>,
	"expected_loss" : <0-20, a percentage of the expected loss (capped at 20%), only needed in case FEC is used; optional, default is 0 (FEC disabled even when negotiated) or the room default>,
	"volume" : <percent value, <100 reduces volume, >100 increases volume; optional, default is 100 (no volume change)>,
	"spatial_position" : <in case spatial audio is enabled for the room, panning of this participant (0=left, 50=center, 100=right)>,
	"secret" : "<room management password; optional, if provided the user is an admin and can't be globally muted with mute_room>",
	"audio_level_average" : "<if provided, overrides the room audio_level_average for this user; optional>",
	"audio_active_packets" : "<if provided, overrides the room audio_active_packets for this user; optional>",
	"record": <true|false, whether to record this user's contribution to a .mjr file (mixer not involved),
	"filename": "<basename of the file to record to, -audio.mjr will be added by the plugin; will be relative to mjrs_dir, if configured in the room>"
}
\endverbatim
 *
 * A successful request will produce a \c joined event:
 *
\verbatim
{
	"audiobridge" : "joined",
	"room" : <numeric ID of the room>,
	"id" : <unique ID assigned to the participant>,
	"display" : "<display name of the new participant>",
	"participants" : [
		// Array of existing participants in the room
	]
}
\endverbatim
 *
 * The other participants in the room will be notified about the new
 * participant by means of a different \c joined event, which will only
 * include the \c room and the new participant as the only object in
 * a \c participants array.
 *
 * Notice that, while the AudioBridge assumes participants will exchange
 * media via WebRTC, there's a less known feature that allows you to use
 * plain RTP to join an AudioBridge room instead. This functionality may
 * be helpful in case you want, e.g., SIP based endpoints to join an
 * AudioBridge room, by crafting SDPs for the SIP dialogs yourself using
 * the info exchanged with the plugin. In order to do that, you keep on
 * using the API to join as a participant as explained above, but instead
 * of negotiating a PeerConnection as you usually would, you add an \c rtp
 * object to the \c join request, which needs to be formatted as follows:
 *
\verbatim
{
	"request" : "join",
	[..]
	"rtp" : {
		"ip" : "<IP address you want media to be sent to>",
		"port" : <port you want media to be sent to>,
		"payload_type" : <payload type to use for RTP packets (optional; only needed in case Opus is used, automatic for G.711)>,
		"audiolevel_ext" : <ID of the audiolevel RTP extension, if used (optional)>,
		"fec" : <true|false, whether FEC should be enabled for the Opus stream (optional; only needed in case Opus is used)>
	}
}
\endverbatim
 *
 * In that case, the participant will be configured to use plain RTP to
 * exchange media with the room, and the \c joined event will include an
 * \c rtp object as well to complete the negotiation:
 *
\verbatim
{
	"audiobridge" : "joined",
	[..]
	"rtp" : {
		"ip" : "<IP address the AudioBridge will expect media to be sent to>",
		"port" : <port the AudioBridge will expect media to be sent to>,
		"payload_type" : <payload type to use for RTP packets (optional; only needed in case Opus is used, automatic for G.711)>
	}
}
\endverbatim
 *
 * Notice that, after a plain RTP session has been established, the
 * AudioBridge plugin will only start sending media via RTP after it
 * has received at least a valid RTP packet from the remote endpoint.
 *
 * At this point, whether the participant will be interacting via WebRTC
 * or plain RTP, the media-related settings of the participant can be
 * modified by means of a \c configure request. The \c configure request
 * has to be formatted as follows (notice that all parameters except
 * \c request are optional, depending on what you want to change):
 *
\verbatim
{
	"request" : "configure",
	"muted" : <true|false, whether to unmute or mute>,
	"display" : "<new display name to have in the room>",
	"prebuffer" : <new number of packets to buffer before decoding this participant (see "join" for more info)>,
	"bitrate" : <new bitrate to use for the Opus stream (see "join" for more info)>,
	"quality" : <new Opus-related complexity to use (see "join" for more info)>,
	"expected_loss" : <new value for the expected loss (see "join" for more info)>
	"volume" : <new volume percent value (see "join" for more info)>,
	"spatial_position" : <in case spatial audio is enabled for the room, new panning of this participant (0=left, 50=center, 100=right)>,
	"record": <true|false, whether to record this user's contribution to a .mjr file (mixer not involved),
	"filename": "<basename of the file to record to, -audio.mjr will be added by the plugin; will be relative to mjrs_dir, if configured in the room>",
	"group" : "<new group to assign to this participant, if enabled in the room (for forwarding purposes)>"
}
\endverbatim
 *
 * \c muted instructs the plugin to mute or unmute the participant;
 * \c quality changes the complexity of the Opus encoder for the
 * participant; \c record can be used to record this participant's contribution
 * to a Janus .mjr file, and \c filename to provide a basename for the path to
 * save the file to (notice that this is different from the recording of a whole
 * room: this feature only records the packets this user is sending, and is not
 * related to the mixer stuff). A successful request will result in a \c ok event:
 *
\verbatim
{
	"audiobridge" : "event",
	"room" : <numeric ID of the room>,
	"result" : "ok"
}
\endverbatim
 *
 * In case the \c muted property was modified, the other participants in
 * the room will be notified about this by means of a \c event notification,
 * which will only include the \c room and the updated participant as the
 * only object in a \c participants array.
 *
 * If you're the administrator of a room (that is, you created it and have access to the secret)
 * you can mute or unmute individual participants using the \c mute or \c unmute request
 *
 \verbatim
{
	"request" : "<mute|unmute, whether to mute or unmute>",
	"secret" : "<room secret, mandatory if configured>",
	"room" : <unique numeric ID of the room>,
	"id" : <unique numeric ID of the participant to mute|unmute>
}
\endverbatim
 *
 * A successful request will result in a success response:
 *
 \verbatim
{
	"audiobridge" : "success",
}
\endverbatim
 *
 * To mute/unmute the whole room, use \c mute_room and \c unmute_room instead.
 *
 \verbatim
{
	"request" : "<mute_room|unmute_room, whether to mute or unmute>",
	"secret" : "<room secret, mandatory if configured>",
	"room" : <unique numeric ID of the room>
}
\endverbatim
 *
 * A successful request will result in a success response:
 *
 \verbatim
{
	"audiobridge" : "success",
}
\endverbatim
 *
 * As anticipated, you can leave an audio room using the \c leave request,
 * which has to be formatted as follows:
 *
\verbatim
{
	"request" : "leave"
}
\endverbatim
 *
 * The leaving user will receive a \c left notification:
 *
\verbatim
{
	"audiobridge" : "left",
	"room" : <numeric ID of the room>,
	"id" : <numeric ID of the participant who left>
}
\endverbatim
 *
 * All the other participants will receive an \c event notification with the
 * ID of the participant who just left:
 *
\verbatim
{
	"audiobridge" : "event",
	"room" : <numeric ID of the room>,
	"leaving" : <numeric ID of the participant who left>
}
\endverbatim
 *
 * For what concerns the \c changeroom request, instead, it's pretty much
 * the same as a \c join request and as such has to be formatted as follows:
 *
\verbatim
{
	"request" : "changeroom",
	"room" : <numeric ID of the room to move to>,
	"id" : <unique ID to assign to the participant; optional, assigned by the plugin if missing>,
	"group" : "<group to assign to this participant (for forwarding purposes only; optional, mandatory if enabled in the new room)>",
	"display" : "<display name to have in the room; optional>",
	"token" : "<invitation token, in case the new room has an ACL; optional>",
	"muted" : <true|false, whether to start unmuted or muted>,
	"bitrate" : <bitrate to use for the Opus stream in bps; optional, default=0 (libopus decides)>,
	"quality" : <0-10, Opus-related complexity to use, higher is higher quality; optional, default is 4>,
	"expected_loss" : <0-20, a percentage of the expected loss (capped at 20%), only needed in case FEC is used; optional, default is 0 (FEC disabled even when negotiated) or the room default>
}
\endverbatim
 *
 * Such a request will trigger all the above-described leaving/joined
 * events to the other participants, as it is indeed wrapping a \c leave
 * followed by a \c join and as such the other participants in both rooms
 * need to be updated accordingly. The participant who switched room
 * instead will be sent a \c roomchanged event which is pretty similar
 * to what \c joined looks like:
 *
 * A successful request will produce a \c joined event:
 *
\verbatim
{
	"audiobridge" : "roomchanged",
	"room" : <numeric ID of the new room>,
	"id" : <unique ID assigned to the participant in the new room>,
	"display" : "<display name of the new participant>",
	"participants" : [
		// Array of existing participants in the new room
	]
}
\endverbatim
 *
 * As a last note, notice that the AudioBridge plugin does support
 * renegotiations, mostly for the purpose of facilitating ICE restarts:
 * in fact, there isn't much need for renegotiations outside of that
 * context, as PeerConnections here will typically always contain a single
 * m-line for audio, and so adding/removing streams makes no sense; besides,
 * muting and unmuting is available via APIs, meaning that updating the
 * media direction via SDP renegotiations would be overkill.
 *
 * To force a renegotiation, all you need to do is send the new JSEP
 * offer together with a \c configure request: this request doesn't need
 * to contain any directive at all, and can be empty. A JSEP answer will
 * be sent back along the result of the request, if successful.
 *
 * \subsection aboffer AudioBridge-generated offers
 *
 * As anticipated in the previous sections, by default the AudioBridge
 * plugin expects an SDP offer from users interested to join a room, and
 * generates an SDP answer to complete the WebRTC negotiation process:
 * this SDP offer can be provided either in a \c join request or a
 * \c configure one, depending on how the app is constructed.
 *
 * It's worth pointing out that the AudioBridge plugin also supports
 * reversed roles when it comes to negotiation: that is, a user can ask
 * the plugin to generate an SDP offer first, to which they'd provide
 * an SDP answer to. This slightly changes the way the negotiation works
 * within the context of the AudioBridge API, as some messages may have
 * to be used in a different way. More specifically, if a user wants the
 * plugin to generate an offer, they'll have to include a:
 *
\verbatim
	[..]
	"generate_offer" : true,
	[..]
}
\endverbatim
 *
 * property in the \c join or \c configure request used to setup the
 * PeerConnection. This means that the user will receive a JSEP SDP
 * offer as part of the related event: at this point, the user needs
 * to prepare to send a JSEP SDP answer and send it back to the plugin
 * to complete the negotiation. The user must use the \c configure
 * request to provide this SDP answer: no need to provide additional
 * attributes in the request, unless it's needed for application related
 * purposes (e.g., to start muted).
 *
 * Notice that this does have an impact on renegotiations, e.g., for
 * ICE restarts or changes in the media direction. As a policy, plugins
 * in Janus tend to enforce the same negotiation pattern used to setup
 * the PeerConnection initially for renegotiations too, as it reduces
 * the risk of issues like glare: this means that users will NOT be able
 * to send an SDP offer to the AudioBridge plugin to update an existing
 * PeerConnection, if that PeerConnection had previously been originated
 * by a plugin offer instead. The plugin will treat this as an error.
 *
 */

#include "plugin.h"
#ifdef __FreeBSD__
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include <jansson.h>
#include <opus/opus.h>
#ifdef HAVE_LIBOGG
#include <ogg/ogg.h>
#endif
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#include <poll.h>

#include "../debug.h"
#include "../apierror.h"
#include "../config.h"
#include "../mutex.h"
#include "../rtp.h"
#include "../rtpsrtp.h"
#include "../rtcp.h"
#include "../record.h"
#include "../sdp-utils.h"
#include "../utils.h"
#include "../ip-utils.h"


/* Plugin information */
#define JANUS_AUDIOBRIDGE_VERSION			12
#define JANUS_AUDIOBRIDGE_VERSION_STRING	"0.0.12"
#define JANUS_AUDIOBRIDGE_DESCRIPTION		"This is a plugin implementing an audio conference bridge for Janus, mixing Opus streams."
#define JANUS_AUDIOBRIDGE_NAME				"JANUS AudioBridge plugin"
#define JANUS_AUDIOBRIDGE_AUTHOR			"Meetecho s.r.l."
#define JANUS_AUDIOBRIDGE_PACKAGE			"janus.plugin.audiobridge"

#define MIN_SEQUENTIAL 						2
#define MAX_MISORDER						50

#define JANUS_AUDIOBRIDGE_MAX_GROUPS		5

/* Plugin methods */
janus_plugin *create(void);
int janus_audiobridge_init(janus_callbacks *callback, const char *config_path);
void janus_audiobridge_destroy(void);
int janus_audiobridge_get_api_compatibility(void);
int janus_audiobridge_get_version(void);
const char *janus_audiobridge_get_version_string(void);
const char *janus_audiobridge_get_description(void);
const char *janus_audiobridge_get_name(void);
const char *janus_audiobridge_get_author(void);
const char *janus_audiobridge_get_package(void);
void janus_audiobridge_create_session(janus_plugin_session *handle, int *error);
struct janus_plugin_result *janus_audiobridge_handle_message(janus_plugin_session *handle, char *transaction, json_t *message, json_t *jsep);
json_t *janus_audiobridge_handle_admin_message(json_t *message);
void janus_audiobridge_setup_media(janus_plugin_session *handle);
void janus_audiobridge_incoming_rtp(janus_plugin_session *handle, janus_plugin_rtp *packet);
void janus_audiobridge_incoming_rtcp(janus_plugin_session *handle, janus_plugin_rtcp *packet);
void janus_audiobridge_hangup_media(janus_plugin_session *handle);
void janus_audiobridge_destroy_session(janus_plugin_session *handle, int *error);
json_t *janus_audiobridge_query_session(janus_plugin_session *handle);

/* Plugin setup */
static janus_plugin janus_audiobridge_plugin =
	JANUS_PLUGIN_INIT (
		.init = janus_audiobridge_init,
		.destroy = janus_audiobridge_destroy,

		.get_api_compatibility = janus_audiobridge_get_api_compatibility,
		.get_version = janus_audiobridge_get_version,
		.get_version_string = janus_audiobridge_get_version_string,
		.get_description = janus_audiobridge_get_description,
		.get_name = janus_audiobridge_get_name,
		.get_author = janus_audiobridge_get_author,
		.get_package = janus_audiobridge_get_package,

		.create_session = janus_audiobridge_create_session,
		.handle_message = janus_audiobridge_handle_message,
		.handle_admin_message = janus_audiobridge_handle_admin_message,
		.setup_media = janus_audiobridge_setup_media,
		.incoming_rtp = janus_audiobridge_incoming_rtp,
		.incoming_rtcp = janus_audiobridge_incoming_rtcp,
		.hangup_media = janus_audiobridge_hangup_media,
		.destroy_session = janus_audiobridge_destroy_session,
		.query_session = janus_audiobridge_query_session,
	);

/* Plugin creator */
janus_plugin *create(void) {
	JANUS_LOG(LOG_VERB, "%s created!\n", JANUS_AUDIOBRIDGE_NAME);
	return &janus_audiobridge_plugin;
}

/* Parameter validation */
static struct janus_json_parameter request_parameters[] = {
	{"request", JSON_STRING, JANUS_JSON_PARAM_REQUIRED}
};
static struct janus_json_parameter adminkey_parameters[] = {
	{"admin_key", JSON_STRING, JANUS_JSON_PARAM_REQUIRED}
};
static struct janus_json_parameter room_parameters[] = {
	{"room", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE}
};
static struct janus_json_parameter roomopt_parameters[] = {
	{"room", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE}
};
static struct janus_json_parameter roomstr_parameters[] = {
	{"room", JSON_STRING, JANUS_JSON_PARAM_REQUIRED}
};
static struct janus_json_parameter roomstropt_parameters[] = {
	{"room", JSON_STRING, 0}
};
static struct janus_json_parameter id_parameters[] = {
	{"id", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE}
};
static struct janus_json_parameter idopt_parameters[] = {
	{"id", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE}
};
static struct janus_json_parameter idstr_parameters[] = {
	{"id", JSON_STRING, JANUS_JSON_PARAM_REQUIRED}
};
static struct janus_json_parameter idstropt_parameters[] = {
	{"id", JSON_STRING, 0}
};
static struct janus_json_parameter group_parameters[] = {
	{"group", JSON_STRING, JANUS_JSON_PARAM_REQUIRED}
};
static struct janus_json_parameter create_parameters[] = {
	{"description", JSON_STRING, 0},
	{"secret", JSON_STRING, 0},
	{"pin", JSON_STRING, 0},
	{"is_private", JANUS_JSON_BOOL, 0},
	{"allowed", JSON_ARRAY, 0},
	{"sampling_rate", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"sampling", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},	/* We keep this to be backwards compatible */
	{"spatial_audio", JANUS_JSON_BOOL, 0},
	{"record", JANUS_JSON_BOOL, 0},
	{"record_file", JSON_STRING, 0},
	{"record_dir", JSON_STRING, 0},
	{"mjrs", JANUS_JSON_BOOL, 0},
	{"mjrs_dir", JSON_STRING, 0},
	{"allow_rtp_participants", JANUS_JSON_BOOL, 0},
	{"permanent", JANUS_JSON_BOOL, 0},
	{"audiolevel_ext", JANUS_JSON_BOOL, 0},
	{"audiolevel_event", JANUS_JSON_BOOL, 0},
	{"audio_active_packets", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"audio_level_average", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"default_prebuffering", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"default_expectedloss", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"default_bitrate", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"groups", JSON_ARRAY, 0}
};
static struct janus_json_parameter edit_parameters[] = {
	{"secret", JSON_STRING, 0},
	{"new_description", JSON_STRING, 0},
	{"new_secret", JSON_STRING, 0},
	{"new_pin", JSON_STRING, 0},
	{"new_is_private", JANUS_JSON_BOOL, 0},
	{"new_record_dir", JSON_STRING, 0},
	{"new_mjrs_dir", JSON_STRING, 0},
	{"permanent", JANUS_JSON_BOOL, 0}
};
static struct janus_json_parameter destroy_parameters[] = {
	{"permanent", JANUS_JSON_BOOL, 0}
};
static struct janus_json_parameter allowed_parameters[] = {
	{"secret", JSON_STRING, 0},
	{"action", JSON_STRING, JANUS_JSON_PARAM_REQUIRED},
	{"allowed", JSON_ARRAY, 0}
};
static struct janus_json_parameter secret_parameters[] = {
	{"secret", JSON_STRING, 0}
};
static struct janus_json_parameter join_parameters[] = {
	{"display", JSON_STRING, 0},
	{"token", JSON_STRING, 0},
	{"group", JSON_STRING, 0},
	{"muted", JANUS_JSON_BOOL, 0},
	{"codec", JSON_STRING, 0},
	{"prebuffer", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"bitrate", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"quality", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"expected_loss", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"volume", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"spatial_position", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"audio_level_average", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"audio_active_packets", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"record", JANUS_JSON_BOOL, 0},
	{"filename", JSON_STRING, 0},
	{"generate_offer", JANUS_JSON_BOOL, 0},
	{"rtp", JSON_OBJECT, 0},
	{"secret", JSON_STRING, 0}
};
static struct janus_json_parameter record_parameters[] = {
	{"record", JANUS_JSON_BOOL, JANUS_JSON_PARAM_REQUIRED},
	{"record_file", JSON_STRING, 0},
	{"record_dir", JSON_STRING, 0}
};
static struct janus_json_parameter mjrs_parameters[] = {
	{"mjrs", JANUS_JSON_BOOL, JANUS_JSON_PARAM_REQUIRED},
	{"mjrs_dir", JSON_STRING, 0}
};
static struct janus_json_parameter rtp_parameters[] = {
	{"ip", JSON_STRING, 0},
	{"port", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"payload_type", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"audiolevel_ext", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"fec", JANUS_JSON_BOOL, 0},
};
static struct janus_json_parameter configure_parameters[] = {
	{"muted", JANUS_JSON_BOOL, 0},
	{"prebuffer", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"bitrate", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"quality", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"expected_loss", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"volume", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"group", JSON_STRING, 0},
	{"spatial_position", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"record", JANUS_JSON_BOOL, 0},
	{"filename", JSON_STRING, 0},
	{"display", JSON_STRING, 0},
	{"generate_offer", JANUS_JSON_BOOL, 0},
	{"update", JANUS_JSON_BOOL, 0}
};
static struct janus_json_parameter rtp_forward_parameters[] = {
	{"group", JSON_STRING, 0},
	{"ssrc", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"codec", JSON_STRING, 0},
	{"ptype", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"port", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE},
	{"host", JSON_STRING, JANUS_JSON_PARAM_REQUIRED},
	{"host_family", JSON_STRING, 0},
	{"srtp_suite", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
	{"srtp_crypto", JSON_STRING, 0},
	{"always_on", JANUS_JSON_BOOL, 0}
};
static struct janus_json_parameter stop_rtp_forward_parameters[] = {
	{"stream_id", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE}
};
static struct janus_json_parameter play_file_parameters[] = {
	{"filename", JSON_STRING, JANUS_JSON_PARAM_REQUIRED},
	{"file_id", JSON_STRING, 0},
	{"group", JSON_STRING, 0},
	{"loop", JANUS_JSON_BOOL, 0}
};
static struct janus_json_parameter checkstop_file_parameters[] = {
	{"file_id", JSON_STRING, JANUS_JSON_PARAM_REQUIRED}
};

/* Static configuration instance */
static janus_config *config = NULL;
static const char *config_folder = NULL;
static janus_mutex config_mutex = JANUS_MUTEX_INITIALIZER;

/* Useful stuff */
static volatile gint initialized = 0, stopping = 0;
static gboolean notify_events = TRUE;
static gboolean string_ids = FALSE;
static gboolean ipv6_disabled = FALSE;
static janus_callbacks *gateway = NULL;
static GThread *handler_thread;
static void *janus_audiobridge_handler(void *data);
static void janus_audiobridge_relay_rtp_packet(gpointer data, gpointer user_data);
static void *janus_audiobridge_mixer_thread(void *data);
static void *janus_audiobridge_participant_thread(void *data);
static void janus_audiobridge_hangup_media_internal(janus_plugin_session *handle);

/* Extension to add while recording (e.g., "tmp" --> ".wav.tmp") */
static char *rec_tempext = NULL;

/* RTP range, in case we need to support plain RTP participants */
static char *local_ip = NULL;
#define JANUS_AUDIOBRIDGE_DEFAULT_RTP_RANGE_MIN 10000
#define JANUS_AUDIOBRIDGE_DEFAULT_RTP_RANGE_MAX 60000
static uint16_t rtp_range_min = JANUS_AUDIOBRIDGE_DEFAULT_RTP_RANGE_MIN;
static uint16_t rtp_range_max = JANUS_AUDIOBRIDGE_DEFAULT_RTP_RANGE_MAX;
static uint16_t rtp_range_slider = JANUS_AUDIOBRIDGE_DEFAULT_RTP_RANGE_MIN;

/* Asynchronous API message to handle */
typedef struct janus_audiobridge_message {
	janus_plugin_session *handle;
	char *transaction;
	json_t *message;
	json_t *jsep;
} janus_audiobridge_message;
static GAsyncQueue *messages = NULL;
static janus_audiobridge_message exit_message;


/* Structs */
typedef struct janus_audiobridge_room {
	guint64 room_id;			/* Unique room ID (when using integers) */
	gchar *room_id_str;			/* Unique room ID (when using strings) */
	gchar *room_name;			/* Room description */
	gchar *room_secret;			/* Secret needed to manipulate (e.g., destroy) this room */
	gchar *room_pin;			/* Password needed to join this room, if any */
	uint32_t room_ssrc;			/* SSRC we'll use for packets generated by the mixer */
	gboolean is_private;		/* Whether this room is 'private' (as in hidden) or not */
	uint32_t sampling_rate;		/* Sampling rate of the mix (e.g., 16000 for wideband; can be 8, 12, 16, 24 or 48kHz) */
	gboolean spatial_audio;		/* Whether the mix will use spatial audio, using stereo */
	gboolean audiolevel_ext;	/* Whether the ssrc-audio-level extension must be negotiated or not for new joins */
	gboolean audiolevel_event;	/* Whether to emit event to other users about audiolevel */
	uint default_prebuffering;	/* Number of packets to buffer before decoding each participant */
	uint default_expectedloss;	/* Percent of packets we expect participants may miss, to help with FEC: can be overridden per-participant */
	int32_t default_bitrate;	/* Default bitrate to use for all Opus streams when encoding */
	int audio_active_packets;	/* Amount of packets with audio level for checkup */
	int audio_level_average;	/* Average audio level */
	volatile gint record;		/* Whether this room has to be recorded or not */
	gchar *record_file;			/* Path of the recording file (absolute or relative, depending on record_dir) */
	gchar *record_dir;			/* Folder to save the recording file to */
	gboolean mjrs;				/* Whether all participants in the room should be individually recorded to mjr files or not */
	gchar *mjrs_dir;			/* Folder to save the mjrs file to */
	FILE *recording;			/* File to record the room into */
	gint64 record_lastupdate;	/* Time when we last updated the wav header */
	volatile gint wav_header_added;	/* If wav header is added in recording file */
	gint64 rec_start_time;		/* Time when recording started for generating file name */
	gboolean allow_plainrtp;	/* Whether plain RTP participants are allowed*/
	gboolean destroy;			/* Value to flag the room for destruction */
	GHashTable *participants;	/* Map of participants */
	GHashTable *anncs;			/* Map of announcements */
	gboolean check_tokens;		/* Whether to check tokens when participants join (see below) */
	gboolean muted;				/* Whether the room is globally muted (except for admins and played files) */
	GHashTable *allowed;		/* Map of participants (as tokens) allowed to join */
	GThread *thread;			/* Mixer thread for this room */
	volatile gint destroyed;	/* Whether this room has been destroyed */
	janus_mutex mutex;			/* Mutex to lock this room instance */
	/* RTP forwarders for this room's mix */
	GHashTable *groups;			/* Forwarding groups supported in this room, indexed by name */
	GHashTable *groups_byid;	/* Forwarding groups supported in this room, indexed by numeric ID */
	GHashTable *rtp_forwarders;	/* RTP forwarders list (as a hashmap) */
	OpusEncoder *rtp_encoder;	/* Opus encoder instance to use for all RTP forwarders */
	janus_mutex rtp_mutex;		/* Mutex to lock the RTP forwarders list */
	int rtp_udp_sock;			/* UDP socket to use to forward RTP packets */
	janus_refcount ref;			/* Reference counter for this room */
} janus_audiobridge_room;
static GHashTable *rooms;
static janus_mutex rooms_mutex = JANUS_MUTEX_INITIALIZER;
static char *admin_key = NULL;
static gboolean lock_rtpfwd = FALSE;
static gboolean lock_playfile = FALSE;

typedef struct janus_audiobridge_session {
	janus_plugin_session *handle;
	gint64 sdp_sessid;
	gint64 sdp_version;
	gboolean plugin_offer;
	gpointer participant;
	volatile gint started;
	volatile gint hangingup;
	volatile gint destroyed;
	janus_refcount ref;
} janus_audiobridge_session;
static GHashTable *sessions;
static janus_mutex sessions_mutex = JANUS_MUTEX_INITIALIZER;

#ifdef HAVE_LIBOGG
/* Helper struct to handle the playout of Opus files */
typedef struct janus_audiobridge_file {
	char *id;
	char *filename;
	FILE *file;
	ogg_sync_state sync;
	ogg_stream_state stream;
	ogg_page page;
	ogg_packet pkt;
	char *oggbuf;
	gboolean started, loop;
	gint state, headers;
} janus_audiobridge_file;
/* Helper method to open an Opus file, and make sure it's valid */
static int janus_audiobridge_file_init(janus_audiobridge_file *ctx) {
	if(ctx == NULL || ctx->file == NULL)
		return -1;
	fseek(ctx->file, 0, SEEK_SET);
	ogg_stream_clear(&ctx->stream);
	ogg_sync_clear(&ctx->sync);
	if(ogg_sync_init(&ctx->sync) < 0) {
		JANUS_LOG(LOG_ERR, "[%s] Error re-initializing Ogg sync state...\n", ctx->id);
		return -1;
	}
	ctx->headers = 0;
	return 0;
}
/* Helper method to check if an Ogg page begins with an Ogg stream */
static gboolean janus_audiobridge_ogg_is_opus(ogg_page *page) {
	ogg_stream_state state;
	ogg_packet pkt;
	ogg_stream_init(&state, ogg_page_serialno(page));
	ogg_stream_pagein(&state, page);
	if(ogg_stream_packetout(&state, &pkt) == 1) {
		if(pkt.bytes >= 19 && !memcmp(pkt.packet, "OpusHead", 8)) {
			ogg_stream_clear(&state);
			return 1;
		}
	}
	ogg_stream_clear(&state);
	return FALSE;
}
/* Helper method to traverse the Opus file until we get a packet we can send */
static int janus_audiobridge_file_read(janus_audiobridge_file *ctx, OpusDecoder *decoder, opus_int16 *buffer, int length) {
	if(ctx == NULL || ctx->file == NULL || decoder == NULL || buffer == NULL)
		return -1;
	/* Check our current state in processing the Ogg file */
	int read = 0;
	if(ctx->state == 0) {
		/* Prepare a buffer, and read from the Ogg file... */
		ctx->oggbuf = ogg_sync_buffer(&ctx->sync, 8192);
		if(ctx->oggbuf == NULL) {
			JANUS_LOG(LOG_ERR, "[%s] ogg_sync_buffer failed...\n", ctx->id);
			return -2;
		}
		read = fread(ctx->oggbuf, 1, 8192, ctx->file);
		if(read == 0 && feof(ctx->file)) {
			/* Check if we should rewind, or be done */
			if(!ctx->loop) {
				/* We're done */
				return 0;
			}
			/* Rewind */
			JANUS_LOG(LOG_VERB, "[%s] Rewind! (%s)\n", ctx->id, ctx->filename);
			if(janus_audiobridge_file_init(ctx) < 0)
				return -3;
			return janus_audiobridge_file_read(ctx, decoder, buffer, length);
		}
		if(ogg_sync_wrote(&ctx->sync, read) < 0) {
			JANUS_LOG(LOG_ERR, "[%s] ogg_sync_wrote failed...\n", ctx->id);
			return -4;
		}
		/* Next state: sync pageout */
		ctx->state = 1;
	}
	if(ctx->state == 1) {
		/* Prepare an ogg_page out of the buffer */
		while((read = ogg_sync_pageout(&ctx->sync, &ctx->page)) == 1) {
			/* Let's look for an Opus stream, first of all */
			if(ctx->headers == 0) {
				if(janus_audiobridge_ogg_is_opus(&ctx->page)) {
					/* This is the start of an Opus stream */
					if(ogg_stream_init(&ctx->stream, ogg_page_serialno(&ctx->page)) < 0) {
						JANUS_LOG(LOG_ERR, "[%s] ogg_stream_init failed...\n", ctx->id);
						return -5;
					}
					ctx->headers++;
				} else if(!ogg_page_bos(&ctx->page)) {
					/* No Opus stream? */
					JANUS_LOG(LOG_ERR, "[%s] No Opus stream...\n", ctx->id);
					return -6;
				} else {
					/* Still waiting for an Opus stream */
					return janus_audiobridge_file_read(ctx, decoder, buffer, length);
				}
			}
			/* Submit the page for packetization */
			if(ogg_stream_pagein(&ctx->stream, &ctx->page) < 0) {
				JANUS_LOG(LOG_ERR, "[%s] ogg_stream_pagein failed...\n", ctx->id);
				return -7;
			}
			/* Time to start reading packets */
			ctx->state = 2;
			break;
		}
		if(read != 1) {
			/* Go back to reading from the file */
			ctx->state = 0;
			return janus_audiobridge_file_read(ctx, decoder, buffer, length);
		}
	}
	if(ctx->state == 2) {
		/* Read and process available packets */
		if(ogg_stream_packetout(&ctx->stream, &ctx->pkt) != 1) {
			/* Go back to reading pages */
			ctx->state = 1;
			return janus_audiobridge_file_read(ctx, decoder, buffer, length);
		} else {
			/* Skip header packets */
			if(ctx->headers == 1 && ctx->pkt.bytes >= 19 && !memcmp(ctx->pkt.packet, "OpusHead", 8)) {
				ctx->headers++;
				return janus_audiobridge_file_read(ctx, decoder, buffer, length);
			}
			if(ctx->headers == 2 && ctx->pkt.bytes >= 16 && !memcmp(ctx->pkt.packet, "OpusTags", 8)) {
				ctx->headers++;
				return janus_audiobridge_file_read(ctx, decoder, buffer, length);
			}
			/* Decode the audio */
			length = opus_decode(decoder, ctx->pkt.packet, ctx->pkt.bytes,
				(opus_int16 *)buffer, length, 0);
			return length;
		}
	}
	/* If we got here, continue with the iteration */
	return -9;
}
/* Helper method to cleanup an Opus context */
static void janus_audiobridge_file_free(janus_audiobridge_file *ctx) {
	if(ctx == NULL)
		return;
	g_free(ctx->id);
	g_free(ctx->filename);
	if(ctx->file)
		fclose(ctx->file);
	if(ctx->headers > 0)
		ogg_stream_clear(&ctx->stream);
	ogg_sync_clear(&ctx->sync);
	g_free(ctx);
}
#endif

/* In case we need to support plain RTP participants, this struct helps with that */
typedef struct janus_audiobridge_plainrtp_media {
	char *remote_audio_ip;
	int ready:1;
	int audio_rtp_fd;
	int local_audio_rtp_port, remote_audio_rtp_port;
	guint32 audio_ssrc, audio_ssrc_peer;
	int audio_pt;
	gboolean audio_send;
	janus_rtp_switching_context context;
	int pipefd[2];
	GThread *thread;
} janus_audiobridge_plainrtp_media;
static void janus_audiobridge_plainrtp_media_cleanup(janus_audiobridge_plainrtp_media *media);
static int janus_audiobridge_plainrtp_allocate_port(janus_audiobridge_plainrtp_media *media);
static void *janus_audiobridge_plainrtp_relay_thread(void *data);

/* AudioBridge participant */
typedef struct janus_audiobridge_participant {
	janus_audiobridge_session *session;
	janus_audiobridge_room *room;	/* Room */
	guint64 user_id;		/* Unique ID in the room */
	gchar *user_id_str;		/* Unique ID in the room (when using strings) */
	gchar *display;			/* Display name (opaque value, only meaningful to application) */
	gboolean admin;			/* If the participant is an admin (can't be globally muted) */
	gboolean prebuffering;	/* Whether this participant needs pre-buffering of a few packets (just joined) */
	uint prebuffer_count;	/* Number of packets to buffer before decoding this participant */
	volatile gint active;	/* Whether this participant can receive media at all */
	volatile gint encoding;	/* Whether this participant is currently encoding */
	volatile gint decoding;	/* Whether this participant is currently decoding */
	gboolean muted;			/* Whether this participant is muted */
	int volume_gain;		/* Gain to apply to the input audio (in percentage) */
	int32_t opus_bitrate;	/* Bitrate to use for the Opus stream */
	int opus_complexity;	/* Complexity to use in the encoder (by default, DEFAULT_COMPLEXITY) */
	gboolean stereo;		/* Whether stereo will be used for spatial audio */
	int spatial_position;	/* Panning of this participant in the mix */
	/* RTP stuff */
	GList *inbuf;			/* Incoming audio from this participant, as an ordered list of packets */
	GAsyncQueue *outbuf;	/* Mixed audio for this participant */
	gint64 last_drop;		/* When we last dropped a packet because the imcoming queue was full */
	janus_mutex qmutex;		/* Incoming queue mutex */
	int opus_pt;			/* Opus payload type */
	int extmap_id;			/* Audio level RTP extension id, if any */
	int dBov_level;			/* Value in dBov of the audio level (last value from extension) */
	int audio_active_packets;	/* Participant's number of audio packets to accumulate */
	int audio_dBov_sum;	    /* Participant's accumulated dBov value for audio level */
	int user_audio_active_packets; /* Participant's number of audio packets to evaluate */
	int user_audio_level_average;	 /* Participant's average level of dBov value */
	gboolean talking;		/* Whether this participant is currently talking (uses audio levels extension) */
	janus_rtp_switching_context context;	/* Needed in case the participant changes room */
	janus_audiocodec codec;	/* Codec this participant is using (most often Opus, but G.711 is supported too) */
	/* Plain RTP, in case this is not a WebRTC participant */
	gboolean plainrtp;			/* Whether this is a WebRTC participant, or a plain RTP one */
	janus_audiobridge_plainrtp_media plainrtp_media;
	janus_mutex pmutex;
	/* Opus stuff */
	OpusEncoder *encoder;		/* Opus encoder instance */
	OpusDecoder *decoder;		/* Opus decoder instance */
	gboolean fec;				/* Opus FEC status */
	int expected_loss;			/* Percentage of expected loss, to configure libopus FEC behaviour (default=0, no FEC even if negotiated) */
	uint16_t expected_seq;		/* Expected sequence number */
	uint16_t probation; 		/* Used to determine new ssrc validity */
	uint32_t last_timestamp;	/* Last in seq timestamp */
	gboolean reset;				/* Whether or not the Opus context must be reset, without re-joining the room */
	GThread *thread;			/* Encoding thread for this participant */
	gboolean mjr_active;		/* Whether this participant has to be recorded to an mjr file or not */
	gchar *mjr_base;			/* Base name for the mjr recording (e.g., /path/to/filename, will generate /path/to/filename-audio.mjr) */
	janus_recorder *arc;		/* The Janus recorder instance for this user's audio, if enabled */
#ifdef HAVE_LIBOGG
	janus_audiobridge_file *annc;	/* In case this is a fake participant, a playable file */
#endif
	uint group;					/* Forwarding group index, if enabled in the room */
	janus_mutex rec_mutex;		/* Mutex to protect the recorder from race conditions */
	volatile gint destroyed;	/* Whether this room has been destroyed */
	janus_refcount ref;			/* Reference counter for this participant */
} janus_audiobridge_participant;

typedef struct janus_audiobridge_rtp_relay_packet {
	janus_rtp_header *data;
	gint length;
	uint32_t ssrc;
	uint32_t timestamp;
	uint16_t seq_number;
	gboolean silence;
} janus_audiobridge_rtp_relay_packet;


static void janus_audiobridge_participant_destroy(janus_audiobridge_participant *participant) {
	if(!participant)
		return;
	if(!g_atomic_int_compare_and_exchange(&participant->destroyed, 0, 1))
		return;
	/* Decrease the counter */
	janus_refcount_decrease(&participant->ref);
}

static void janus_audiobridge_participant_unref(janus_audiobridge_participant *participant) {
	if(!participant)
		return;
	/* Just decrease the counter */
	janus_refcount_decrease(&participant->ref);
}

static void janus_audiobridge_participant_free(const janus_refcount *participant_ref) {
	janus_audiobridge_participant *participant = janus_refcount_containerof(participant_ref, janus_audiobridge_participant, ref);
	/* This participant can be destroyed, free all the resources */
	g_free(participant->user_id_str);
	g_free(participant->display);
	if(participant->encoder)
		opus_encoder_destroy(participant->encoder);
	if(participant->decoder)
		opus_decoder_destroy(participant->decoder);
	while(participant->inbuf) {
		GList *first = g_list_first(participant->inbuf);
		janus_audiobridge_rtp_relay_packet *pkt = (janus_audiobridge_rtp_relay_packet *)first->data;
		participant->inbuf = g_list_delete_link(participant->inbuf, first);
		if(pkt)
			g_free(pkt->data);
		g_free(pkt);
	}
	if(participant->outbuf != NULL) {
		while(g_async_queue_length(participant->outbuf) > 0) {
			janus_audiobridge_rtp_relay_packet *pkt = g_async_queue_pop(participant->outbuf);
			g_free(pkt->data);
			g_free(pkt);
		}
		g_async_queue_unref(participant->outbuf);
	}
	g_free(participant->mjr_base);
#ifdef HAVE_LIBOGG
	janus_audiobridge_file_free(participant->annc);
#endif
	janus_mutex_lock(&participant->pmutex);
	janus_audiobridge_plainrtp_media_cleanup(&participant->plainrtp_media);
	janus_mutex_unlock(&participant->pmutex);
	g_free(participant);
}

static void janus_audiobridge_session_destroy(janus_audiobridge_session *session) {
	if(session && g_atomic_int_compare_and_exchange(&session->destroyed, 0, 1))
		janus_refcount_decrease(&session->ref);
}

static void janus_audiobridge_session_free(const janus_refcount *session_ref) {
	janus_audiobridge_session *session = janus_refcount_containerof(session_ref, janus_audiobridge_session, ref);
	/* Destroy the participant instance, if any */
	if(session->participant)
		janus_audiobridge_participant_destroy(session->participant);
	/* Remove the reference to the core plugin session */
	janus_refcount_decrease(&session->handle->ref);
	/* This session can be destroyed, free all the resources */
	g_free(session);
}

static void janus_audiobridge_room_destroy(janus_audiobridge_room *audiobridge) {
	if(!audiobridge)
		return;
	if(!g_atomic_int_compare_and_exchange(&audiobridge->destroyed, 0, 1))
		return;
	/* Decrease the counter */
	janus_refcount_decrease(&audiobridge->ref);
}

static void janus_audiobridge_room_free(const janus_refcount *audiobridge_ref) {
	janus_audiobridge_room *audiobridge = janus_refcount_containerof(audiobridge_ref, janus_audiobridge_room, ref);
	/* This room can be destroyed, free all the resources */
	g_free(audiobridge->room_id_str);
	g_free(audiobridge->room_name);
	g_free(audiobridge->room_secret);
	g_free(audiobridge->room_pin);
	g_free(audiobridge->record_file);
	g_free(audiobridge->record_dir);
	g_hash_table_destroy(audiobridge->participants);
	g_hash_table_destroy(audiobridge->anncs);
	g_hash_table_destroy(audiobridge->allowed);
	if(audiobridge->rtp_udp_sock > 0)
		close(audiobridge->rtp_udp_sock);
	if(audiobridge->rtp_encoder)
		opus_encoder_destroy(audiobridge->rtp_encoder);
	g_hash_table_destroy(audiobridge->rtp_forwarders);
	if(audiobridge->groups)
		g_hash_table_destroy(audiobridge->groups);
	if(audiobridge->groups_byid)
		g_hash_table_destroy(audiobridge->groups_byid);
	g_free(audiobridge);
}

static void janus_audiobridge_message_free(janus_audiobridge_message *msg) {
	if(!msg || msg == &exit_message)
		return;

	if(msg->handle && msg->handle->plugin_handle) {
		janus_audiobridge_session *session = (janus_audiobridge_session *)msg->handle->plugin_handle;
		janus_refcount_decrease(&session->ref);
	}
	msg->handle = NULL;

	g_free(msg->transaction);
	msg->transaction = NULL;
	if(msg->message)
		json_decref(msg->message);
	msg->message = NULL;
	if(msg->jsep)
		json_decref(msg->jsep);
	msg->jsep = NULL;

	g_free(msg);
}

/* Start / stop recording */
static void janus_audiobridge_recorder_create(janus_audiobridge_participant *participant);
static void janus_audiobridge_recorder_close(janus_audiobridge_participant *participant);

/* RTP forwarder instance: address to send to, and current RTP header info */
typedef struct janus_audiobridge_rtp_forwarder {
	struct sockaddr_in serv_addr;
	struct sockaddr_in6 serv_addr6;
	uint32_t ssrc;
	janus_audiocodec codec;
	int payload_type;
	uint16_t seq_number;
	uint32_t timestamp;
	uint group;
	gboolean always_on;
	/* Only needed for SRTP forwarders */
	gboolean is_srtp;
	srtp_t srtp_ctx;
	srtp_policy_t srtp_policy;
	/* Reference */
	volatile gint destroyed;
	janus_refcount ref;
} janus_audiobridge_rtp_forwarder;
static void janus_audiobridge_rtp_forwarder_destroy(janus_audiobridge_rtp_forwarder *rf) {
	if(rf && g_atomic_int_compare_and_exchange(&rf->destroyed, 0, 1)) {
		janus_refcount_decrease(&rf->ref);
	}
}
static void janus_audiobridge_rtp_forwarder_free(const janus_refcount *f_ref) {
	janus_audiobridge_rtp_forwarder *rf = janus_refcount_containerof(f_ref, janus_audiobridge_rtp_forwarder, ref);
	if(rf->is_srtp) {
		srtp_dealloc(rf->srtp_ctx);
		g_free(rf->srtp_policy.key);
	}
	g_free(rf);
}
static guint32 janus_audiobridge_rtp_forwarder_add_helper(janus_audiobridge_room *room,
		uint group, const gchar *host, uint16_t port, uint32_t ssrc, int pt,
		janus_audiocodec codec, int srtp_suite, const char *srtp_crypto,
		gboolean always_on, guint32 stream_id) {
	if(room == NULL || host == NULL)
		return 0;
	janus_audiobridge_rtp_forwarder *rf = g_malloc0(sizeof(janus_audiobridge_rtp_forwarder));
	/* First of all, let's check if we need to setup an SRTP forwarder */
	if(srtp_suite > 0 && srtp_crypto != NULL) {
		/* Base64 decode the crypto string and set it as the SRTP context */
		gsize len = 0;
		guchar *decoded = g_base64_decode(srtp_crypto, &len);
		if(len < SRTP_MASTER_LENGTH) {
			JANUS_LOG(LOG_ERR, "Invalid SRTP crypto (%s)\n", srtp_crypto);
			g_free(decoded);
			g_free(rf);
			return 0;
		}
		/* Set SRTP policy */
		srtp_policy_t *policy = &rf->srtp_policy;
		srtp_crypto_policy_set_rtp_default(&(policy->rtp));
		if(srtp_suite == 32) {
			srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32(&(policy->rtp));
		} else if(srtp_suite == 80) {
			srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&(policy->rtp));
		}
		policy->ssrc.type = ssrc_any_outbound;
		policy->key = decoded;
		policy->next = NULL;
		/* Create SRTP context */
		srtp_err_status_t res = srtp_create(&rf->srtp_ctx, policy);
		if(res != srtp_err_status_ok) {
			/* Something went wrong... */
			JANUS_LOG(LOG_ERR, "Error creating forwarder SRTP session: %d (%s)\n", res, janus_srtp_error_str(res));
			g_free(decoded);
			policy->key = NULL;
			g_free(rf);
			return 0;
		}
		rf->is_srtp = TRUE;
	}
	/* Check if the host address is IPv4 or IPv6 */
	if(strstr(host, ":") != NULL) {
		rf->serv_addr6.sin6_family = AF_INET6;
		inet_pton(AF_INET6, host, &(rf->serv_addr6.sin6_addr));
		rf->serv_addr6.sin6_port = htons(port);
	} else {
		rf->serv_addr.sin_family = AF_INET;
		inet_pton(AF_INET, host, &(rf->serv_addr.sin_addr));
		rf->serv_addr.sin_port = htons(port);
	}
	/* Setup RTP info (we'll use the stream ID as SSRC) */
	rf->codec = codec;
	rf->ssrc = ssrc;
	rf->payload_type = pt;
	if(codec == JANUS_AUDIOCODEC_PCMA)
		rf->payload_type = 8;
	else if(codec == JANUS_AUDIOCODEC_PCMU)
		rf->payload_type = 0;
	rf->seq_number = 0;
	rf->timestamp = 0;
	rf->group = group;
	rf->always_on = always_on;

	janus_mutex_lock(&room->rtp_mutex);

	guint32 actual_stream_id;
	if(stream_id > 0) {
		actual_stream_id = stream_id;
	} else {
		actual_stream_id = janus_random_uint32();
	}

	while(g_hash_table_lookup(room->rtp_forwarders, GUINT_TO_POINTER(actual_stream_id)) != NULL) {
		actual_stream_id = janus_random_uint32();
	}
	janus_refcount_init(&rf->ref, janus_audiobridge_rtp_forwarder_free);
	g_hash_table_insert(room->rtp_forwarders, GUINT_TO_POINTER(actual_stream_id), rf);

	janus_mutex_unlock(&room->rtp_mutex);

	JANUS_LOG(LOG_VERB, "Added RTP forwarder to room %s: %s:%d (ID: %"SCNu32")\n",
		room->room_id_str, host, port, actual_stream_id);

	return actual_stream_id;
}


/* Helper to sort incoming RTP packets by sequence numbers */
static gint janus_audiobridge_rtp_sort(gconstpointer a, gconstpointer b) {
	janus_audiobridge_rtp_relay_packet *pkt1 = (janus_audiobridge_rtp_relay_packet *)a;
	janus_audiobridge_rtp_relay_packet *pkt2 = (janus_audiobridge_rtp_relay_packet *)b;
	if(pkt1->seq_number < 100 && pkt2->seq_number > 65000) {
		/* Sequence number was probably reset, pkt2 is older */
		return 1;
	} else if(pkt2->seq_number < 100 && pkt1->seq_number > 65000) {
		/* Sequence number was probably reset, pkt1 is older */
		return -1;
	}
	/* Simply compare timestamps */
	if(pkt1->seq_number < pkt2->seq_number)
		return -1;
	else if(pkt1->seq_number > pkt2->seq_number)
		return 1;
	return 0;
}

/* Helper struct to generate and parse WAVE headers */
typedef struct wav_header {
	char riff[4];
	uint32_t len;
	char wave[4];
	char fmt[4];
	uint32_t formatsize;
	uint16_t format;
	uint16_t channels;
	uint32_t samplerate;
	uint32_t avgbyterate;
	uint16_t samplebytes;
	uint16_t channelbits;
	char data[4];
	uint32_t blocksize;
} wav_header;


/* In case we need mu-Law/a-Law support, these tables help us transcode */
static uint8_t janus_audiobridge_g711_ulaw_enctable[256] = {
	0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
};
static int16_t janus_audiobridge_g711_ulaw_dectable[256] = {
	-32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956,
	-23932, -22908, -21884, -20860, -19836, -18812, -17788, -16764,
	-15996, -15484, -14972, -14460, -13948, -13436, -12924, -12412,
	-11900, -11388, -10876, -10364, -9852, -9340, -8828, -8316,
	-7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140,
	-5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
	-3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004,
	-2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
	-1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436,
	-1372, -1308, -1244, -1180, -1116, -1052, -988, -924,
	-876, -844, -812, -780, -748, -716, -684, -652,
	-620, -588, -556, -524, -492, -460, -428, -396,
	-372, -356, -340, -324, -308, -292, -276, -260,
	-244, -228, -212, -196, -180, -164, -148, -132,
	-120, -112, -104, -96, -88, -80, -72, -64,
	-56, -48, -40, -32, -24, -16, -8, 0,
	32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956,
	23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
	15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412,
	11900, 11388, 10876, 10364, 9852, 9340, 8828, 8316,
	7932, 7676, 7420, 7164, 6908, 6652, 6396, 6140,
	5884, 5628, 5372, 5116, 4860, 4604, 4348, 4092,
	3900, 3772, 3644, 3516, 3388, 3260, 3132, 3004,
	2876, 2748, 2620, 2492, 2364, 2236, 2108, 1980,
	1884, 1820, 1756, 1692, 1628, 1564, 1500, 1436,
	1372, 1308, 1244, 1180, 1116, 1052, 988, 924,
	876, 844, 812, 780, 748, 716, 684, 652,
	620, 588, 556, 524, 492, 460, 428, 396,
	372, 356, 340, 324, 308, 292, 276, 260,
	244, 228, 212, 196, 180, 164, 148, 132,
	120, 112, 104, 96, 88, 80, 72, 64,
	56, 48, 40, 32, 24, 16, 8, 0
};
static uint8_t janus_audiobridge_g711_ulaw_encode(int16_t sample) {
	uint8_t sign = (sample >> 8) & 0x80;
	if(sign)
		sample = -sample;
	if(sample > 32635)
		sample = 32635;
	sample = (int16_t)(sample + 0x84);
	uint8_t exponent = (int)janus_audiobridge_g711_ulaw_enctable[(sample>>7) & 0xFF];
	uint8_t mantissa = (sample >> (exponent+3)) & 0x0F;
	uint8_t encoded = ~ (sign | (exponent << 4) | mantissa);
	return encoded;
}
static uint8_t janus_audiobridge_g711_alaw_enctable[128] = {
	1, 1, 2, 2, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5,
	6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7
};
static int16_t janus_audiobridge_g711_alaw_dectable[256] = {
	-5504, -5248, -6016, -5760, -4480, -4224, -4992, -4736,
	-7552, -7296, -8064, -7808, -6528, -6272, -7040, -6784,
	-2752, -2624, -3008, -2880, -2240, -2112, -2496, -2368,
	-3776, -3648, -4032, -3904, -3264, -3136, -3520, -3392,
	-22016, -20992, -24064, -23040, -17920, -16896, -19968, -18944,
	-30208, -29184, -32256, -31232, -26112, -25088, -28160, -27136,
	-11008, -10496, -12032, -11520, -8960, -8448, -9984, -9472,
	-15104, -14592, -16128, -15616, -13056, -12544, -14080, -13568,
	-344, -328, -376, -360, -280, -264, -312, -296,
	-472, -456, -504, -488, -408, -392, -440, -424,
	-88, -72, -120, -104, -24, -8, -56, -40,
	-216, -200, -248, -232, -152, -136, -184, -168,
	-1376, -1312, -1504, -1440, -1120, -1056, -1248, -1184,
	-1888, -1824, -2016, -1952, -1632, -1568, -1760, -1696,
	-688, -656, -752, -720, -560, -528, -624, -592,
	-944, -912, -1008, -976, -816, -784, -880, -848,
	5504, 5248, 6016, 5760, 4480, 4224, 4992, 4736,
	7552, 7296, 8064, 7808, 6528, 6272, 7040, 6784,
	2752, 2624, 3008, 2880, 2240, 2112, 2496, 2368,
	3776, 3648, 4032, 3904, 3264, 3136, 3520, 3392,
	22016, 20992, 24064, 23040, 17920, 16896, 19968, 18944,
	30208, 29184, 32256, 31232, 26112, 25088, 28160, 27136,
	11008, 10496, 12032, 11520, 8960, 8448, 9984, 9472,
	15104, 14592, 16128, 15616, 13056, 12544, 14080, 13568,
	344, 328, 376, 360, 280, 264, 312, 296,
	472, 456, 504, 488, 408, 392, 440, 424,
	88, 72, 120, 104, 24, 8, 56, 40,
	216, 200, 248, 232, 152, 136, 184, 168,
	1376, 1312, 1504, 1440, 1120, 1056, 1248, 1184,
	1888, 1824, 2016, 1952, 1632, 1568, 1760, 1696,
	688, 656, 752, 720, 560, 528, 624, 592,
	944, 912, 1008, 976, 816, 784, 880, 848
};
static uint8_t janus_audiobridge_g711_alaw_encode(int16_t sample) {
	uint8_t sign = ((~sample) >> 8) & 0x80;
	uint8_t encoded = 0;
	if(!sign)
		sample = -sample;
	if(sample > 32635)
		sample = 32635;
	if(sample >= 256) {
		uint8_t exponent = janus_audiobridge_g711_alaw_enctable[(sample >> 8) & 0x7F];
		uint8_t mantissa = (sample >> (exponent + 3) ) & 0x0F;
		encoded = ((exponent << 4) | mantissa);
	} else {
		encoded = (uint8_t)(sample >> 4);
	}
	encoded ^= (sign ^ 0x55);
	return encoded;
}

/* Ugly helper code to quickly resample (in case we're using G.711 anywhere) */
static int janus_audiobridge_resample(int16_t *input, int input_num, int input_rate, int16_t *output, int output_rate) {
	if(input == NULL || output == NULL)
		return 0;
	if((input_rate != 8000 && input_rate != 16000 && input_rate != 24000 && input_rate != 48000) ||
			(output_rate != 8000 && output_rate != 16000 && output_rate != 24000 && output_rate != 48000)) {
		/* Invalid sampling rate */
		return 0;
	}
	if(input_rate != 8000 && output_rate != 8000) {
		/* We only use this for G.711, so one of the two MUST be 8000 */
		return 0;
	}
	if(input_rate == output_rate) {
		/* Easy enough */
		memcpy(output, input, input_num*sizeof(int16_t));
		return input_num;
	} else if(input_rate < output_rate) {
		/* Upsample */
		int up = output_rate/input_rate, i = 0;
		memset(output, 0, input_num*up);
		for(i=0; i<input_num; i++) {
			*(output + i*up) = *(input + i);
		}
		return input_num*up;
	} else {
		/* Downsample */
		int down = input_rate/output_rate, i = 0;
		for(i=0; i<input_num; i++) {
			*(output + i) = *(input + i*down);
		}
		return input_num/down;
	}
}


/* Mixer settings */
#define DEFAULT_PREBUFFERING	6
#define MAX_PREBUFFERING		50


/* Opus settings */
#define	OPUS_SAMPLES	960
#define	G711_SAMPLES	160
#define	BUFFER_SAMPLES	OPUS_SAMPLES*12
#define DEFAULT_COMPLEXITY	4


/* Error codes */
#define JANUS_AUDIOBRIDGE_ERROR_UNKNOWN_ERROR	499
#define JANUS_AUDIOBRIDGE_ERROR_NO_MESSAGE		480
#define JANUS_AUDIOBRIDGE_ERROR_INVALID_JSON	481
#define JANUS_AUDIOBRIDGE_ERROR_INVALID_REQUEST	482
#define JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT	483
#define JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT	484
#define JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM	485
#define JANUS_AUDIOBRIDGE_ERROR_ROOM_EXISTS		486
#define JANUS_AUDIOBRIDGE_ERROR_NOT_JOINED		487
#define JANUS_AUDIOBRIDGE_ERROR_LIBOPUS_ERROR	488
#define JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED	489
#define JANUS_AUDIOBRIDGE_ERROR_ID_EXISTS		490
#define JANUS_AUDIOBRIDGE_ERROR_ALREADY_JOINED	491
#define JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_USER	492
#define JANUS_AUDIOBRIDGE_ERROR_INVALID_SDP		493
#define JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_GROUP	494

static int janus_audiobridge_create_udp_socket_if_needed(janus_audiobridge_room *audiobridge) {
	if(audiobridge->rtp_udp_sock > 0) {
		return 0;
	}

	audiobridge->rtp_udp_sock = socket(!ipv6_disabled ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(audiobridge->rtp_udp_sock <= 0) {
		JANUS_LOG(LOG_ERR, "Could not open UDP socket for RTP forwarder (room %s), %d (%s)\n",
			audiobridge->room_id_str, errno, g_strerror(errno));
		return -1;
	}
	if(!ipv6_disabled) {
		int v6only = 0;
		if(setsockopt(audiobridge->rtp_udp_sock, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) != 0) {
			JANUS_LOG(LOG_ERR, "Could not configure UDP socket for RTP forwarder (room %s), %d (%s))\n",
				audiobridge->room_id_str, errno, g_strerror(errno));
			return -1;
		}
	}

	return 0;
}

static int janus_audiobridge_create_opus_encoder_if_needed(janus_audiobridge_room *audiobridge) {
	if(audiobridge->rtp_encoder != NULL) {
		return 0;
	}

	int error = 0;
	audiobridge->rtp_encoder = opus_encoder_create(audiobridge->sampling_rate,
		audiobridge->spatial_audio ? 2 : 1, OPUS_APPLICATION_VOIP, &error);
	if(error != OPUS_OK) {
		JANUS_LOG(LOG_ERR, "Error creating Opus encoder for RTP forwarder (room %s)\n", audiobridge->room_id_str);
		return -1;
	}

	if(audiobridge->sampling_rate == 8000) {
		opus_encoder_ctl(audiobridge->rtp_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_NARROWBAND));
	} else if(audiobridge->sampling_rate == 12000) {
		opus_encoder_ctl(audiobridge->rtp_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_MEDIUMBAND));
	} else if(audiobridge->sampling_rate == 16000) {
		opus_encoder_ctl(audiobridge->rtp_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
	} else if(audiobridge->sampling_rate == 24000) {
		opus_encoder_ctl(audiobridge->rtp_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_SUPERWIDEBAND));
	} else if(audiobridge->sampling_rate == 48000) {
		opus_encoder_ctl(audiobridge->rtp_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
	} else {
		JANUS_LOG(LOG_WARN, "Unsupported sampling rate %d, setting 16kHz\n", audiobridge->sampling_rate);
		opus_encoder_ctl(audiobridge->rtp_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
	}

	/* Check if we need FEC */
	if(audiobridge->default_expectedloss > 0) {
		opus_encoder_ctl(audiobridge->rtp_encoder, OPUS_SET_INBAND_FEC(TRUE));
		opus_encoder_ctl(audiobridge->rtp_encoder, OPUS_SET_PACKET_LOSS_PERC(audiobridge->default_expectedloss));
	}
	/* Also check if we need to enforce a bitrate */
	if(audiobridge->default_bitrate > 0)
		opus_encoder_ctl(audiobridge->rtp_encoder, OPUS_SET_BITRATE(audiobridge->default_bitrate));

	return 0;
}

static int janus_audiobridge_create_static_rtp_forwarder(janus_config_category *cat, janus_audiobridge_room *audiobridge) {
	guint32 forwarder_id = 0;
	janus_config_item *forwarder_id_item = janus_config_get(config, cat, janus_config_type_item, "rtp_forward_id");
	if(forwarder_id_item != NULL && forwarder_id_item->value != NULL &&
			janus_string_to_uint32(forwarder_id_item->value, &forwarder_id) < 0) {
		JANUS_LOG(LOG_ERR, "Invalid forwarder ID\n");
		return 0;
	}

	guint32 ssrc_value = 0;
	janus_config_item *ssrc = janus_config_get(config, cat, janus_config_type_item, "rtp_forward_ssrc");
	if(ssrc != NULL && ssrc->value != NULL && janus_string_to_uint32(ssrc->value, &ssrc_value) < 0) {
		JANUS_LOG(LOG_ERR, "Invalid SSRC (%s)\n", ssrc->value);
		return 0;
	}

	janus_audiocodec codec = JANUS_AUDIOCODEC_OPUS;
	janus_config_item *rfcodec = janus_config_get(config, cat, janus_config_type_item, "rtp_forward_codec");
	if(rfcodec != NULL && rfcodec->value != NULL) {
		codec = janus_audiocodec_from_name(rfcodec->value);
		if(codec != JANUS_AUDIOCODEC_OPUS && codec != JANUS_AUDIOCODEC_PCMA && codec != JANUS_AUDIOCODEC_PCMU) {
			JANUS_LOG(LOG_ERR, "Unsupported codec (%s)\n", rfcodec->value);
			return 0;
		}
	}

	int ptype = 100;
	janus_config_item *pt = janus_config_get(config, cat, janus_config_type_item, "rtp_forward_ptype");
	if(pt != NULL && pt->value != NULL) {
		ptype = atoi(pt->value);
		if(ptype < 0 || ptype > 127) {
			JANUS_LOG(LOG_ERR, "Invalid payload type (%s)\n", pt->value);
			return 0;
		}
	}

	/* If this room uses groups, check if a valid group name was provided */
	uint group = 0;
	if(audiobridge->groups != NULL) {
		janus_config_item *group_name = janus_config_get(config, cat, janus_config_type_item, "rtp_forward_group");
		if(group_name != NULL && group_name->value != NULL) {
			group = GPOINTER_TO_UINT(g_hash_table_lookup(audiobridge->groups, group_name->value));
			if(group == 0) {
				JANUS_LOG(LOG_ERR, "Invalid group name (%s)\n", group_name->value);
				return 0;
			}
		}
	}

	janus_config_item *port_item = janus_config_get(config, cat, janus_config_type_item, "rtp_forward_port");
	uint16_t port = 0;
	if(port_item != NULL && port_item->value != NULL && janus_string_to_uint16(port_item->value, &port) < 0) {
		JANUS_LOG(LOG_ERR, "Invalid port (%s)\n", port_item->value);
		return 0;
	}
	if(port == 0) {
		return 0;
	}

	janus_config_item *host_item = janus_config_get(config, cat, janus_config_type_item, "rtp_forward_host");
	if(host_item == NULL || host_item->value == NULL || strlen(host_item->value) == 0) {
		return 0;
	}
	const char *host = host_item->value, *resolved_host = NULL;
	int family = 0;
	janus_config_item *host_family_item = janus_config_get(config, cat, janus_config_type_item, "rtp_forward_host_family");
	if(host_family_item != NULL && host_family_item->value != NULL) {
		const char *host_family = host_family_item->value;
		if(host_family) {
			if(!strcasecmp(host_family, "ipv4")) {
				family = AF_INET;
			} else if(!strcasecmp(host_family, "ipv6")) {
				family = AF_INET6;
			} else {
				JANUS_LOG(LOG_ERR, "Unsupported protocol family (%s)\n", host_family);
				return 0;
			}
		}
	}
	/* Check if we need to resolve this host address */
	struct addrinfo *res = NULL, *start = NULL;
	janus_network_address addr;
	janus_network_address_string_buffer addr_buf;
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	if(family != 0)
		hints.ai_family = family;
	if(getaddrinfo(host, NULL, family != 0 ? &hints : NULL, &res) == 0) {
		start = res;
		while(res != NULL) {
			if(janus_network_address_from_sockaddr(res->ai_addr, &addr) == 0 &&
					janus_network_address_to_string_buffer(&addr, &addr_buf) == 0) {
				/* Resolved */
				resolved_host = janus_network_address_string_from_buffer(&addr_buf);
				freeaddrinfo(start);
				start = NULL;
				break;
			}
			res = res->ai_next;
		}
	}
	if(resolved_host == NULL) {
		if(start)
			freeaddrinfo(start);
		JANUS_LOG(LOG_ERR, "Could not resolve address (%s)...\n", host);
		return 0;
	}
	host = resolved_host;

	/* We may need to SRTP-encrypt this stream */
	int srtp_suite = 0;
	const char *srtp_crypto = NULL;
	janus_config_item *s_suite = janus_config_get(config, cat, janus_config_type_item, "rtp_forward_srtp_suite");
	janus_config_item *s_crypto = janus_config_get(config, cat, janus_config_type_item, "rtp_forward_srtp_crypto");
	if(s_suite && s_suite->value) {
		srtp_suite = atoi(s_suite->value);
		if(srtp_suite != 32 && srtp_suite != 80) {
			JANUS_LOG(LOG_ERR, "Can't add static RTP forwarder for room %s, invalid SRTP suite...\n", audiobridge->room_id_str);
			return 0;
		}
		if(s_crypto && s_crypto->value)
			srtp_crypto = s_crypto->value;
	}

	janus_config_item *always_on_item = janus_config_get(config, cat, janus_config_type_item, "rtp_forward_always_on");
	gboolean always_on = FALSE;
	if(always_on_item != NULL && always_on_item->value != NULL && strlen(always_on_item->value) > 0) {
		always_on = janus_is_true(always_on_item->value);
	}

	/* Update room */
	janus_mutex_lock(&rooms_mutex);
	janus_mutex_lock(&audiobridge->mutex);

	if(janus_audiobridge_create_udp_socket_if_needed(audiobridge)) {
		janus_mutex_unlock(&audiobridge->mutex);
		janus_mutex_unlock(&rooms_mutex);
		return -1;
	}

	if(janus_audiobridge_create_opus_encoder_if_needed(audiobridge)) {
		janus_mutex_unlock(&audiobridge->mutex);
		janus_mutex_unlock(&rooms_mutex);
		return -1;
	}

	janus_audiobridge_rtp_forwarder_add_helper(audiobridge, group,
		host, port, ssrc_value, ptype, codec, srtp_suite, srtp_crypto,
		always_on, forwarder_id);

	janus_mutex_unlock(&audiobridge->mutex);
	janus_mutex_unlock(&rooms_mutex);

	return 0;
}

/* Plugin implementation */
int janus_audiobridge_init(janus_callbacks *callback, const char *config_path) {
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
	g_snprintf(filename, 255, "%s/%s.jcfg", config_path, JANUS_AUDIOBRIDGE_PACKAGE);
	JANUS_LOG(LOG_VERB, "Configuration file: %s\n", filename);
	config = janus_config_parse(filename);
	if(config == NULL) {
		JANUS_LOG(LOG_WARN, "Couldn't find .jcfg configuration file (%s), trying .cfg\n", JANUS_AUDIOBRIDGE_PACKAGE);
		g_snprintf(filename, 255, "%s/%s.cfg", config_path, JANUS_AUDIOBRIDGE_PACKAGE);
		JANUS_LOG(LOG_VERB, "Configuration file: %s\n", filename);
		config = janus_config_parse(filename);
	}
	config_folder = config_path;
	if(config != NULL)
		janus_config_print(config);

	sessions = g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify)janus_audiobridge_session_destroy);
	messages = g_async_queue_new_full((GDestroyNotify) janus_audiobridge_message_free);
	/* This is the callback we'll need to invoke to contact the Janus core */
	gateway = callback;

	/* Parse configuration to populate the rooms list */
	if(config != NULL) {
		janus_config_category *config_general = janus_config_get_create(config, NULL, janus_config_type_category, "general");
		/* Any admin key to limit who can "create"? */
		janus_config_item *key = janus_config_get(config, config_general, janus_config_type_item, "admin_key");
		if(key != NULL && key->value != NULL)
			admin_key = g_strdup(key->value);
		janus_config_item *lrf = janus_config_get(config, config_general, janus_config_type_item, "lock_rtp_forward");
		if(admin_key && lrf != NULL && lrf->value != NULL)
			lock_rtpfwd = janus_is_true(lrf->value);
		janus_config_item *lpf = janus_config_get(config, config_general, janus_config_type_item, "lock_play_file");
		if(admin_key && lpf != NULL && lpf->value != NULL)
			lock_playfile = janus_is_true(lpf->value);
		janus_config_item *ext = janus_config_get(config, config_general, janus_config_type_item, "record_tmp_ext");
		if(ext != NULL && ext->value != NULL)
			rec_tempext = g_strdup(ext->value);
		janus_config_item *events = janus_config_get(config, config_general, janus_config_type_item, "events");
		if(events != NULL && events->value != NULL)
			notify_events = janus_is_true(events->value);
		if(!notify_events && callback->events_is_enabled()) {
			JANUS_LOG(LOG_WARN, "Notification of events to handlers disabled for %s\n", JANUS_AUDIOBRIDGE_NAME);
		}
		janus_config_item *ids = janus_config_get(config, config_general, janus_config_type_item, "string_ids");
		if(ids != NULL && ids->value != NULL)
			string_ids = janus_is_true(ids->value);
		if(string_ids) {
			JANUS_LOG(LOG_INFO, "AudioBridge will use alphanumeric IDs, not numeric\n");
		}
		janus_config_item *lip = janus_config_get(config, config_general, janus_config_type_item, "local_ip");
		if(lip && lip->value) {
			/* Verify that the address is valid */
			struct ifaddrs *ifas = NULL;
			janus_network_address iface;
			janus_network_address_string_buffer ibuf;
			if(getifaddrs(&ifas) == -1) {
				JANUS_LOG(LOG_ERR, "Unable to acquire list of network devices/interfaces; some configurations may not work as expected... %d (%s)\n",
					errno, g_strerror(errno));
			} else {
				if(janus_network_lookup_interface(ifas, lip->value, &iface) != 0) {
					JANUS_LOG(LOG_WARN, "Error setting local IP address to %s, falling back to detecting IP address...\n", lip->value);
				} else {
					if(janus_network_address_to_string_buffer(&iface, &ibuf) != 0 || janus_network_address_string_buffer_is_null(&ibuf)) {
						JANUS_LOG(LOG_WARN, "Error getting local IP address from %s, falling back to detecting IP address...\n", lip->value);
					} else {
						local_ip = g_strdup(janus_network_address_string_from_buffer(&ibuf));
					}
				}
				freeifaddrs(ifas);
			}
		}
		janus_config_item *rpr = janus_config_get(config, config_general, janus_config_type_item, "rtp_port_range");
		if(rpr && rpr->value) {
			/* Split in min and max port */
			char *maxport = strrchr(rpr->value, '-');
			if(maxport != NULL) {
				*maxport = '\0';
				maxport++;
				if(janus_string_to_uint16(rpr->value, &rtp_range_min) < 0)
					JANUS_LOG(LOG_WARN, "Invalid RTP min port value: %s (assuming 0)\n", rpr->value);
				if(janus_string_to_uint16(maxport, &rtp_range_max) < 0)
					JANUS_LOG(LOG_WARN, "Invalid RTP max port value: %s (assuming 0)\n", maxport);
				maxport--;
				*maxport = '-';
			}
			if(rtp_range_min > rtp_range_max) {
				uint16_t temp_port = rtp_range_min;
				rtp_range_min = rtp_range_max;
				rtp_range_max = temp_port;
			}
			if(rtp_range_min % 2)
				rtp_range_min++;	/* Pick an even port for RTP */
			if(rtp_range_min > rtp_range_max) {
				JANUS_LOG(LOG_WARN, "Incorrect port range (%u -- %u), switching min and max\n", rtp_range_min, rtp_range_max);
				uint16_t range_temp = rtp_range_max;
				rtp_range_max = rtp_range_min;
				rtp_range_min = range_temp;
			}
			if(rtp_range_max == 0)
				rtp_range_max = 65535;
			rtp_range_slider = rtp_range_min;
			JANUS_LOG(LOG_VERB, "AudioBridge RTP port range: %u -- %u\n", rtp_range_min, rtp_range_max);
		}
	}
	if(local_ip == NULL) {
		local_ip = janus_network_detect_local_ip_as_string(janus_network_query_options_any_ip);
		if(local_ip == NULL) {
			JANUS_LOG(LOG_WARN, "Couldn't find any address! using 127.0.0.1 as the local IP... (which is NOT going to work out of your machine)\n");
			local_ip = g_strdup("127.0.0.1");
		}
	}
	JANUS_LOG(LOG_VERB, "Local IP set to %s\n", local_ip);

	/* Iterate on all rooms */
	rooms = g_hash_table_new_full(string_ids ? g_str_hash : g_int64_hash, string_ids ? g_str_equal : g_int64_equal,
		(GDestroyNotify)g_free, (GDestroyNotify)janus_audiobridge_room_destroy);
	if(config != NULL) {
		GList *clist = janus_config_get_categories(config, NULL), *cl = clist;
		while(cl != NULL) {
			janus_config_category *cat = (janus_config_category *)cl->data;
			if(cat->name == NULL || !strcasecmp(cat->name, "general")) {
				cl = cl->next;
				continue;
			}
			JANUS_LOG(LOG_VERB, "Adding AudioBridge room '%s'\n", cat->name);
			janus_config_item *desc = janus_config_get(config, cat, janus_config_type_item, "description");
			janus_config_item *priv = janus_config_get(config, cat, janus_config_type_item, "is_private");
			janus_config_item *sampling = janus_config_get(config, cat, janus_config_type_item, "sampling_rate");
			janus_config_item *spatial = janus_config_get(config, cat, janus_config_type_item, "spatial_audio");
			janus_config_item *audiolevel_ext = janus_config_get(config, cat, janus_config_type_item, "audiolevel_ext");
			janus_config_item *audiolevel_event = janus_config_get(config, cat, janus_config_type_item, "audiolevel_event");
			janus_config_item *audio_active_packets = janus_config_get(config, cat, janus_config_type_item, "audio_active_packets");
			janus_config_item *audio_level_average = janus_config_get(config, cat, janus_config_type_item, "audio_level_average");
			janus_config_item *default_prebuffering = janus_config_get(config, cat, janus_config_type_item, "default_prebuffering");
			janus_config_item *default_expectedloss = janus_config_get(config, cat, janus_config_type_item, "default_expectedloss");
			janus_config_item *default_bitrate = janus_config_get(config, cat, janus_config_type_item, "default_bitrate");
			janus_config_item *secret = janus_config_get(config, cat, janus_config_type_item, "secret");
			janus_config_item *pin = janus_config_get(config, cat, janus_config_type_item, "pin");
			janus_config_array *groups = janus_config_get(config, cat, janus_config_type_array, "groups");
			janus_config_item *record = janus_config_get(config, cat, janus_config_type_item, "record");
			janus_config_item *recfile = janus_config_get(config, cat, janus_config_type_item, "record_file");
			janus_config_item *recdir = janus_config_get(config, cat, janus_config_type_item, "record_dir");
			janus_config_item *mjrs = janus_config_get(config, cat, janus_config_type_item, "mjrs");
			janus_config_item *mjrsdir = janus_config_get(config, cat, janus_config_type_item, "mjrs_dir");
			janus_config_item *allowrtp = janus_config_get(config, cat, janus_config_type_item, "allow_rtp_participants");
			if(sampling == NULL || sampling->value == NULL) {
				JANUS_LOG(LOG_ERR, "Can't add the AudioBridge room, missing mandatory information...\n");
				cl = cl->next;
				continue;
			}
			/* Create the AudioBridge room */
			janus_audiobridge_room *audiobridge = g_malloc0(sizeof(janus_audiobridge_room));
			janus_refcount_init(&audiobridge->ref, janus_audiobridge_room_free);
			const char *room_num = cat->name;
			if(strstr(room_num, "room-") == room_num)
				room_num += 5;
			if(!string_ids) {
				audiobridge->room_id = g_ascii_strtoull(room_num, NULL, 0);
				if(audiobridge->room_id == 0) {
					JANUS_LOG(LOG_ERR, "Can't add the AudioBridge room, invalid ID 0...\n");
					janus_audiobridge_room_destroy(audiobridge);
					cl = cl->next;
					continue;
				}
				/* Make sure the ID is completely numeric */
				char room_id_str[30];
				g_snprintf(room_id_str, sizeof(room_id_str), "%"SCNu64, audiobridge->room_id);
				if(strcmp(room_num, room_id_str)) {
					JANUS_LOG(LOG_ERR, "Can't add the AudioBridge room, ID '%s' is not numeric...\n", room_num);
					janus_audiobridge_room_destroy(audiobridge);
					cl = cl->next;
					continue;
				}
			}
			/* Let's make sure the room doesn't exist already */
			janus_mutex_lock(&rooms_mutex);
			if(g_hash_table_lookup(rooms, string_ids ? (gpointer)room_num : (gpointer)&audiobridge->room_id) != NULL) {
				/* It does... */
				janus_mutex_unlock(&rooms_mutex);
				JANUS_LOG(LOG_ERR, "Can't add the AudioBridge room, room %s already exists...\n", room_num);
				janus_audiobridge_room_destroy(audiobridge);
				cl = cl->next;
				continue;
			}
			janus_mutex_unlock(&rooms_mutex);
			audiobridge->room_id_str = g_strdup(room_num);
			char *description = NULL;
			if(desc != NULL && desc->value != NULL && strlen(desc->value) > 0)
				description = g_strdup(desc->value);
			else
				description = g_strdup(cat->name);
			audiobridge->room_name = description;
			audiobridge->is_private = priv && priv->value && janus_is_true(priv->value);
			audiobridge->sampling_rate = atol(sampling->value);
			switch(audiobridge->sampling_rate) {
				case 8000:
				case 12000:
				case 16000:
				case 24000:
				case 48000:
					JANUS_LOG(LOG_VERB, "Sampling rate for mixing: %"SCNu32"\n", audiobridge->sampling_rate);
					break;
				default:
					JANUS_LOG(LOG_ERR, "Unsupported sampling rate %"SCNu32"...\n", audiobridge->sampling_rate);
					janus_audiobridge_room_destroy(audiobridge);
					cl = cl->next;
					continue;
			}
			audiobridge->spatial_audio = spatial && spatial->value && janus_is_true(spatial->value);
			audiobridge->audiolevel_ext = TRUE;
			if(audiolevel_ext != NULL && audiolevel_ext->value != NULL)
				audiobridge->audiolevel_ext = janus_is_true(audiolevel_ext->value);
			audiobridge->audiolevel_event = FALSE;
			if(audiolevel_event != NULL && audiolevel_event->value != NULL)
				audiobridge->audiolevel_event = janus_is_true(audiolevel_event->value);
			if(audiobridge->audiolevel_event) {
				audiobridge->audio_active_packets = 100;
				if(audio_active_packets != NULL && audio_active_packets->value != NULL){
					if(atoi(audio_active_packets->value) > 0) {
						audiobridge->audio_active_packets = atoi(audio_active_packets->value);
					} else {
						JANUS_LOG(LOG_WARN, "Invalid audio_active_packets value provided, using default: %d\n", audiobridge->audio_active_packets);
					}
				}
				audiobridge->audio_level_average = 25;
				if(audio_level_average != NULL && audio_level_average->value != NULL) {
					if(atoi(audio_level_average->value) > 0) {
						audiobridge->audio_level_average = atoi(audio_level_average->value);
					} else {
						JANUS_LOG(LOG_WARN, "Invalid audio_level_average value provided, using default: %d\n", audiobridge->audio_level_average);
					}
				}
			}
			audiobridge->default_prebuffering = DEFAULT_PREBUFFERING;
			if(default_prebuffering != NULL && default_prebuffering->value != NULL) {
				int prebuffering = atoi(default_prebuffering->value);
				if(prebuffering < 0 || prebuffering > MAX_PREBUFFERING) {
					JANUS_LOG(LOG_WARN, "Invalid default_prebuffering value provided, using default: %d\n", audiobridge->default_prebuffering);
				} else {
					audiobridge->default_prebuffering = prebuffering;
				}
			}
			audiobridge->default_expectedloss = 0;
			if(default_expectedloss != NULL && default_expectedloss->value != NULL) {
				int expectedloss = atoi(default_expectedloss->value);
				if(expectedloss < 0 || expectedloss > 20) {
					JANUS_LOG(LOG_WARN, "Invalid expectedloss value provided, using default: 0\n");
				} else {
					audiobridge->default_expectedloss = expectedloss;
				}
			}
			audiobridge->default_bitrate = 0;
			if(default_bitrate != NULL && default_bitrate->value != NULL) {
				audiobridge->default_bitrate = atoi(default_bitrate->value);
				if(audiobridge->default_bitrate < 500 || audiobridge->default_bitrate > 512000) {
					JANUS_LOG(LOG_WARN, "Invalid bitrate %"SCNi32", falling back to auto\n", audiobridge->default_bitrate);
					audiobridge->default_bitrate = 0;
				}
			}
			audiobridge->room_ssrc = janus_random_uint32();
			if(secret != NULL && secret->value != NULL) {
				audiobridge->room_secret = g_strdup(secret->value);
			}
			if(pin != NULL && pin->value != NULL) {
				audiobridge->room_pin = g_strdup(pin->value);
			}
			g_atomic_int_set(&audiobridge->record, 0);
			if(record && record->value && janus_is_true(record->value))
				g_atomic_int_set(&audiobridge->record, 1);
			if(recfile && recfile->value)
				audiobridge->record_file = g_strdup(recfile->value);
			if(recdir && recdir->value) {
				audiobridge->record_dir = g_strdup(recdir->value);
				if(janus_mkdir(audiobridge->record_dir, 0755) < 0) {
					/* FIXME Should this be fatal, when creating a room? */
					JANUS_LOG(LOG_WARN, "AudioBridge mkdir (%s) error: %d (%s)\n", audiobridge->record_dir, errno, g_strerror(errno));
				}
			}
			audiobridge->recording = NULL;
			if(mjrs && mjrs->value && janus_is_true(mjrs->value))
				audiobridge->mjrs = TRUE;
			if(mjrsdir && mjrsdir->value)
				audiobridge->mjrs_dir = g_strdup(mjrsdir->value);
			audiobridge->allow_plainrtp = FALSE;
			if(allowrtp && allowrtp->value)
				audiobridge->allow_plainrtp = janus_is_true(allowrtp->value);
			audiobridge->destroy = 0;
			audiobridge->participants = g_hash_table_new_full(
				string_ids ? g_str_hash : g_int64_hash, string_ids ? g_str_equal : g_int64_equal,
				(GDestroyNotify)g_free, (GDestroyNotify)janus_audiobridge_participant_unref);
			audiobridge->anncs = g_hash_table_new_full(g_str_hash, g_str_equal,
				(GDestroyNotify)g_free, (GDestroyNotify)janus_audiobridge_participant_unref);
			audiobridge->check_tokens = FALSE;	/* Static rooms can't have an "allowed" list yet, no hooks to the configuration file */
			audiobridge->allowed = g_hash_table_new_full(g_str_hash, g_str_equal, (GDestroyNotify)g_free, NULL);
			if(groups != NULL) {
				/* Populate the group hashtable, and create the related indexes */
				GList *gl = groups->list;
				if(g_list_length(gl) > JANUS_AUDIOBRIDGE_MAX_GROUPS) {
					JANUS_LOG(LOG_ERR, "Too many groups specified in room %s (max %d allowed)\n", room_num, JANUS_AUDIOBRIDGE_MAX_GROUPS);
					janus_refcount_decrease(&audiobridge->ref);
					cl = cl->next;
				}
				int count = 0;
				audiobridge->groups = g_hash_table_new_full(g_str_hash, g_str_equal, (GDestroyNotify)g_free, NULL);
				audiobridge->groups_byid = g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify)g_free);
				while(gl) {
					janus_config_item *g = (janus_config_item *)gl->data;
					if(g == NULL || g->type != janus_config_type_item || g->name != NULL || g->value == NULL) {
						JANUS_LOG(LOG_WARN, "  -- Invalid group item (not a string?), skipping in '%s'...\n", cat->name);
						gl = gl->next;
						continue;
					}
					const char *name = g->value;
					if(g_hash_table_lookup(audiobridge->groups, name)) {
						JANUS_LOG(LOG_WARN, "Duplicated group name '%s', skipping\n", name);
					} else {
						count++;
						g_hash_table_insert(audiobridge->groups, g_strdup(name), GUINT_TO_POINTER(count));
						g_hash_table_insert(audiobridge->groups_byid, GUINT_TO_POINTER(count), g_strdup(name));
					}
					gl = gl->next;
				}
				if(count == 0) {
					JANUS_LOG(LOG_WARN, "Empty or invalid groups array provided, groups will be disabled in '%s'...\n", cat->name);
					g_hash_table_destroy(audiobridge->groups);
					g_hash_table_destroy(audiobridge->groups_byid);
					audiobridge->groups = NULL;
					audiobridge->groups_byid = NULL;
				}
			}
			g_atomic_int_set(&audiobridge->destroyed, 0);
			janus_mutex_init(&audiobridge->mutex);
			audiobridge->rtp_forwarders = g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify)janus_audiobridge_rtp_forwarder_destroy);
			audiobridge->rtp_encoder = NULL;
			audiobridge->rtp_udp_sock = -1;
			janus_mutex_init(&audiobridge->rtp_mutex);
			JANUS_LOG(LOG_VERB, "Created AudioBridge room: %s (%s, %s, secret: %s, pin: %s)\n",
				audiobridge->room_id_str, audiobridge->room_name,
				audiobridge->is_private ? "private" : "public",
				audiobridge->room_secret ? audiobridge->room_secret : "no secret",
				audiobridge->room_pin ? audiobridge->room_pin : "no pin");

			if(janus_audiobridge_create_static_rtp_forwarder(cat, audiobridge)) {
				JANUS_LOG(LOG_ERR, "Error creating static RTP forwarder (room %s)\n", audiobridge->room_id_str);
			}

			/* We need a thread for the mix */
			GError *error = NULL;
			char tname[16];
			g_snprintf(tname, sizeof(tname), "mixer %s", audiobridge->room_id_str);
			janus_refcount_increase(&audiobridge->ref);
			audiobridge->thread = g_thread_try_new(tname, &janus_audiobridge_mixer_thread, audiobridge, &error);
			if(error != NULL) {
				/* FIXME We should clear some resources... */
				janus_refcount_decrease(&audiobridge->ref);
				JANUS_LOG(LOG_ERR, "Got error %d (%s) trying to launch the mixer thread...\n",
					error->code, error->message ? error->message : "??");
				g_error_free(error);
			} else {
				janus_mutex_lock(&rooms_mutex);
				g_hash_table_insert(rooms,
					string_ids ? (gpointer)g_strdup(audiobridge->room_id_str) : (gpointer)janus_uint64_dup(audiobridge->room_id),
					audiobridge);
				janus_mutex_unlock(&rooms_mutex);
			}
			cl = cl->next;
		}
		g_list_free(clist);
		/* Done: we keep the configuration file open in case we get a "create" or "destroy" with permanent=true */
	}

	/* Show available rooms */
	janus_mutex_lock(&rooms_mutex);
	GHashTableIter iter;
	gpointer value;
	g_hash_table_iter_init(&iter, rooms);
	while(g_hash_table_iter_next(&iter, NULL, &value)) {
		janus_audiobridge_room *ar = value;
		JANUS_LOG(LOG_VERB, "  ::: [%s][%s] %"SCNu32" (%s be recorded)\n",
			ar->room_id_str, ar->room_name, ar->sampling_rate, g_atomic_int_get(&ar->record) ? "will" : "will NOT");
	}
	janus_mutex_unlock(&rooms_mutex);

	/* Finally, let's check if IPv6 is disabled, as we may need to know for forwarders */
	int fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if(fd < 0) {
		ipv6_disabled = TRUE;
	} else {
		int v6only = 0;
		if(setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) != 0)
			ipv6_disabled = TRUE;
	}
	if(fd >= 0)
		close(fd);
	if(ipv6_disabled) {
		JANUS_LOG(LOG_WARN, "IPv6 disabled, will only create VideoRoom forwarders to IPv4 addresses\n");
	}

	g_atomic_int_set(&initialized, 1);

	/* Launch the thread that will handle incoming messages */
	GError *error = NULL;
	handler_thread = g_thread_try_new("audiobridge handler", janus_audiobridge_handler, NULL, &error);
	if(error != NULL) {
		g_atomic_int_set(&initialized, 0);
		JANUS_LOG(LOG_ERR, "Got error %d (%s) trying to launch the AudioBridge handler thread...\n",
			error->code, error->message ? error->message : "??");
		g_error_free(error);
		janus_config_destroy(config);
		return -1;
	}
	JANUS_LOG(LOG_INFO, "%s initialized!\n", JANUS_AUDIOBRIDGE_NAME);
	return 0;
}

void janus_audiobridge_destroy(void) {
	if(!g_atomic_int_get(&initialized))
		return;
	g_atomic_int_set(&stopping, 1);

	g_async_queue_push(messages, &exit_message);
	if(handler_thread != NULL) {
		g_thread_join(handler_thread);
		handler_thread = NULL;
	}
	/* FIXME We should destroy the sessions cleanly */
	janus_mutex_lock(&sessions_mutex);
	g_hash_table_destroy(sessions);
	sessions = NULL;
	janus_mutex_unlock(&sessions_mutex);
	janus_mutex_lock(&rooms_mutex);
	g_hash_table_destroy(rooms);
	rooms = NULL;
	janus_mutex_unlock(&rooms_mutex);
	g_async_queue_unref(messages);
	messages = NULL;

	janus_config_destroy(config);
	g_free(admin_key);
	g_free(rec_tempext);

	g_atomic_int_set(&initialized, 0);
	g_atomic_int_set(&stopping, 0);
	JANUS_LOG(LOG_INFO, "%s destroyed!\n", JANUS_AUDIOBRIDGE_NAME);
}

int janus_audiobridge_get_api_compatibility(void) {
	/* Important! This is what your plugin MUST always return: don't lie here or bad things will happen */
	return JANUS_PLUGIN_API_VERSION;
}

int janus_audiobridge_get_version(void) {
	return JANUS_AUDIOBRIDGE_VERSION;
}

const char *janus_audiobridge_get_version_string(void) {
	return JANUS_AUDIOBRIDGE_VERSION_STRING;
}

const char *janus_audiobridge_get_description(void) {
	return JANUS_AUDIOBRIDGE_DESCRIPTION;
}

const char *janus_audiobridge_get_name(void) {
	return JANUS_AUDIOBRIDGE_NAME;
}

const char *janus_audiobridge_get_author(void) {
	return JANUS_AUDIOBRIDGE_AUTHOR;
}

const char *janus_audiobridge_get_package(void) {
	return JANUS_AUDIOBRIDGE_PACKAGE;
}

static janus_audiobridge_session *janus_audiobridge_lookup_session(janus_plugin_session *handle) {
	janus_audiobridge_session *session = NULL;
	if(g_hash_table_contains(sessions, handle)) {
		session = (janus_audiobridge_session *)handle->plugin_handle;
	}
	return session;
}

void janus_audiobridge_create_session(janus_plugin_session *handle, int *error) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized)) {
		*error = -1;
		return;
	}
	janus_audiobridge_session *session = g_malloc0(sizeof(janus_audiobridge_session));
	session->handle = handle;
	g_atomic_int_set(&session->started, 0);
	g_atomic_int_set(&session->hangingup, 0);
	g_atomic_int_set(&session->destroyed, 0);
	handle->plugin_handle = session;
	janus_refcount_init(&session->ref, janus_audiobridge_session_free);

	janus_mutex_lock(&sessions_mutex);
	g_hash_table_insert(sessions, handle, session);
	janus_mutex_unlock(&sessions_mutex);

	return;
}

void janus_audiobridge_destroy_session(janus_plugin_session *handle, int *error) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized)) {
		*error = -1;
		return;
	}
	janus_mutex_lock(&sessions_mutex);
	janus_audiobridge_session *session = janus_audiobridge_lookup_session(handle);
	if(!session) {
		janus_mutex_unlock(&sessions_mutex);
		JANUS_LOG(LOG_ERR, "No AudioBridge session associated with this handle...\n");
		*error = -2;
		return;
	}
	JANUS_LOG(LOG_VERB, "Removing AudioBridge session...\n");
	janus_audiobridge_hangup_media_internal(handle);
	g_hash_table_remove(sessions, handle);
	janus_mutex_unlock(&sessions_mutex);

	return;
}

static void janus_audiobridge_notify_participants(janus_audiobridge_participant *participant, json_t *msg, gboolean notify_source_participant) {
	/* participant->room->participants_mutex has to be locked. */
	GHashTableIter iter;
	gpointer value;
	g_hash_table_iter_init(&iter, participant->room->participants);
	while(!participant->room->destroyed && g_hash_table_iter_next(&iter, NULL, &value)) {
		janus_audiobridge_participant *p = value;
		if(p && p->session && (p != participant || notify_source_participant)) {
			JANUS_LOG(LOG_VERB, "Notifying participant %s (%s)\n", p->user_id_str, p->display ? p->display : "??");
			int ret = gateway->push_event(p->session->handle, &janus_audiobridge_plugin, NULL, msg, NULL);
			JANUS_LOG(LOG_VERB, "  >> %d (%s)\n", ret, janus_get_api_error(ret));
		}
	}
}

json_t *janus_audiobridge_query_session(janus_plugin_session *handle) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized)) {
		return NULL;
	}
	janus_mutex_lock(&sessions_mutex);
	janus_audiobridge_session *session = janus_audiobridge_lookup_session(handle);
	if(!session) {
		janus_mutex_unlock(&sessions_mutex);
		JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
		return NULL;
	}
	janus_refcount_increase(&session->ref);
	janus_mutex_unlock(&sessions_mutex);
	/* Show the participant/room info, if any */
	json_t *info = json_object();
	janus_audiobridge_participant *participant = (janus_audiobridge_participant *)session->participant;
	json_object_set_new(info, "state", json_string(participant && participant->room ? "inroom" : "idle"));
	if(participant) {
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *room = participant->room;
		if(room != NULL)
			json_object_set_new(info, "room", string_ids ? json_string(room->room_id_str) : json_integer(room->room_id));
		janus_mutex_unlock(&rooms_mutex);
		json_object_set_new(info, "id", string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
		if(room && participant->group > 0 && room->groups_byid != NULL) {
			char *name = g_hash_table_lookup(room->groups_byid, GUINT_TO_POINTER(participant->group));
			if(name != NULL)
				json_object_set_new(info, "group", json_string(name));
		}
		if(participant->display)
			json_object_set_new(info, "display", json_string(participant->display));
		if(participant->admin)
			json_object_set_new(info, "admin", json_true());
		json_object_set_new(info, "muted", participant->muted ? json_true() : json_false());
		json_object_set_new(info, "active", g_atomic_int_get(&participant->active) ? json_true() : json_false());
		json_object_set_new(info, "pre-buffering", participant->prebuffering ? json_true() : json_false());
		json_object_set_new(info, "prebuffer-count", json_integer(participant->prebuffer_count));
		if(participant->inbuf) {
			janus_mutex_lock(&participant->qmutex);
			json_object_set_new(info, "queue-in", json_integer(g_list_length(participant->inbuf)));
			janus_mutex_unlock(&participant->qmutex);
		}
		if(participant->outbuf)
			json_object_set_new(info, "queue-out", json_integer(g_async_queue_length(participant->outbuf)));
		if(participant->last_drop > 0)
			json_object_set_new(info, "last-drop", json_integer(participant->last_drop));
		if(participant->stereo)
			json_object_set_new(info, "spatial_position", json_integer(participant->spatial_position));
		if(participant->arc && participant->arc->filename)
			json_object_set_new(info, "audio-recording", json_string(participant->arc->filename));
		if(participant->extmap_id > 0) {
			json_object_set_new(info, "audio-level-dBov", json_integer(participant->dBov_level));
			json_object_set_new(info, "talking", participant->talking ? json_true() : json_false());
		}
		json_object_set_new(info, "fec", participant->fec ? json_true() : json_false());
		if(participant->fec)
			json_object_set_new(info, "expected-loss", json_integer(participant->expected_loss));
		if(participant->opus_bitrate)
			json_object_set_new(info, "opus-bitrate", json_integer(participant->opus_bitrate));
		if(participant->plainrtp_media.audio_rtp_fd != -1) {
			json_t *rtp = json_object();
			if(local_ip)
				json_object_set_new(rtp, "local-ip", json_string(local_ip));
			if(participant->plainrtp_media.local_audio_rtp_port)
				json_object_set_new(rtp, "local-port", json_integer(participant->plainrtp_media.local_audio_rtp_port));
			if(participant->plainrtp_media.remote_audio_ip)
				json_object_set_new(rtp, "remote-ip", json_string(participant->plainrtp_media.remote_audio_ip));
			if(participant->plainrtp_media.remote_audio_rtp_port)
				json_object_set_new(rtp, "remote-port", json_integer(participant->plainrtp_media.remote_audio_rtp_port));
			if(participant->plainrtp_media.audio_ssrc)
				json_object_set_new(rtp, "local-ssrc", json_integer(participant->plainrtp_media.audio_ssrc));
			if(participant->plainrtp_media.audio_ssrc_peer)
				json_object_set_new(rtp, "remote-ssrc", json_integer(participant->plainrtp_media.audio_ssrc_peer));
			json_object_set_new(info, "plain-rtp", rtp);
		}
	}
	if(session->plugin_offer)
		json_object_set_new(info, "plugin-offer", json_true());
	json_object_set_new(info, "started", g_atomic_int_get(&session->started) ? json_true() : json_false());
	json_object_set_new(info, "hangingup", g_atomic_int_get(&session->hangingup) ? json_true() : json_false());
	json_object_set_new(info, "destroyed", g_atomic_int_get(&session->destroyed) ? json_true() : json_false());
	janus_refcount_decrease(&session->ref);
	return info;
}

static int janus_audiobridge_access_room(json_t *root, gboolean check_modify, janus_audiobridge_room **audiobridge, char *error_cause, int error_cause_size) {
	/* rooms_mutex has to be locked */
	int error_code = 0;
	json_t *room = json_object_get(root, "room");
	guint64 room_id = 0;
	char room_id_num[30], *room_id_str = NULL;
	if(!string_ids) {
		room_id = json_integer_value(room);
		g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
		room_id_str = room_id_num;
	} else {
		room_id_str = (char *)json_string_value(room);
	}
	*audiobridge = g_hash_table_lookup(rooms,
		string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
	if(*audiobridge == NULL) {
		JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
		error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
		if(error_cause)
			g_snprintf(error_cause, error_cause_size, "No such room (%s)", room_id_str);
		return error_code;
	}
	if(g_atomic_int_get(&((*audiobridge)->destroyed))) {
		JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
		error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
		if(error_cause)
			g_snprintf(error_cause, error_cause_size, "No such room (%s)", room_id_str);
		return error_code;
	}
	if(check_modify) {
		char error_cause2[100];
		JANUS_CHECK_SECRET((*audiobridge)->room_secret, root, "secret", error_code, error_cause2,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
		if(error_code != 0) {
			g_strlcpy(error_cause, error_cause2, error_cause_size);
			return error_code;
		}
	}
	return 0;
}


/* Helper method to process synchronous requests */
static json_t *janus_audiobridge_process_synchronous_request(janus_audiobridge_session *session, json_t *message) {
	json_t *request = json_object_get(message, "request");
	const char *request_text = json_string_value(request);

	/* Parse the message */
	int error_code = 0;
	char error_cause[512];
	json_t *root = message;
	json_t *response = NULL;

	if(!strcasecmp(request_text, "create")) {
		/* Create a new AudioBridge */
		JANUS_LOG(LOG_VERB, "Creating a new AudioBridge room\n");
		JANUS_VALIDATE_JSON_OBJECT(root, create_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, roomopt_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstropt_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		if(admin_key != NULL) {
			/* An admin key was specified: make sure it was provided, and that it's valid */
			JANUS_VALIDATE_JSON_OBJECT(root, adminkey_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			if(error_code != 0)
				goto prepare_response;
			JANUS_CHECK_SECRET(admin_key, root, "admin_key", error_code, error_cause,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
			if(error_code != 0)
				goto prepare_response;
		}
		json_t *desc = json_object_get(root, "description");
		json_t *secret = json_object_get(root, "secret");
		json_t *pin = json_object_get(root, "pin");
		json_t *is_private = json_object_get(root, "is_private");
		json_t *allowed = json_object_get(root, "allowed");
		json_t *sampling = json_object_get(root, "sampling_rate");
		if(sampling == NULL)
			sampling = json_object_get(root, "sampling");
		json_t *spatial = json_object_get(root, "spatial_audio");
		json_t *audiolevel_ext = json_object_get(root, "audiolevel_ext");
		json_t *audiolevel_event = json_object_get(root, "audiolevel_event");
		json_t *audio_active_packets = json_object_get(root, "audio_active_packets");
		json_t *audio_level_average = json_object_get(root, "audio_level_average");
		json_t *default_prebuffering = json_object_get(root, "default_prebuffering");
		json_t *default_expectedloss = json_object_get(root, "default_expectedloss");
		json_t *default_bitrate = json_object_get(root, "default_bitrate");
		json_t *groups = json_object_get(root, "groups");
		json_t *record = json_object_get(root, "record");
		json_t *recfile = json_object_get(root, "record_file");
		json_t *recdir = json_object_get(root, "record_dir");
		json_t *mjrs = json_object_get(root, "mjrs");
		json_t *mjrsdir = json_object_get(root, "mjrs_dir");
		json_t *allowrtp = json_object_get(root, "allow_rtp_participants");
		json_t *permanent = json_object_get(root, "permanent");
		if(allowed) {
			/* Make sure the "allowed" array only contains strings */
			gboolean ok = TRUE;
			if(json_array_size(allowed) > 0) {
				size_t i = 0;
				for(i=0; i<json_array_size(allowed); i++) {
					json_t *a = json_array_get(allowed, i);
					if(!a || !json_is_string(a)) {
						ok = FALSE;
						break;
					}
				}
			}
			if(!ok) {
				JANUS_LOG(LOG_ERR, "Invalid element in the allowed array (not a string)\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
				g_snprintf(error_cause, 512, "Invalid element in the allowed array (not a string)");
				goto prepare_response;
			}
		}
		if(groups) {
			/* Make sure the "groups" array only contains strings */
			gboolean ok = TRUE;
			if(json_array_size(groups) > 0) {
				if(json_array_size(groups) > JANUS_AUDIOBRIDGE_MAX_GROUPS) {
					JANUS_LOG(LOG_ERR, "Too many groups specified (max %d allowed)\n", JANUS_AUDIOBRIDGE_MAX_GROUPS);
					error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
					g_snprintf(error_cause, 512, "Too many groups specified (max %d allowed)", JANUS_AUDIOBRIDGE_MAX_GROUPS);
					goto prepare_response;
				}
				size_t i = 0;
				for(i=0; i<json_array_size(groups); i++) {
					json_t *a = json_array_get(groups, i);
					if(!a || !json_is_string(a)) {
						ok = FALSE;
						break;
					}
				}
			}
			if(!ok) {
				JANUS_LOG(LOG_ERR, "Invalid element in the groups array (not a string)\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
				g_snprintf(error_cause, 512, "Invalid element in the groups array (not a string)");
				goto prepare_response;
			}
		}
		gboolean save = permanent ? json_is_true(permanent) : FALSE;
		if(save && config == NULL) {
			JANUS_LOG(LOG_ERR, "No configuration file, can't create permanent room\n");
			error_code = JANUS_AUDIOBRIDGE_ERROR_UNKNOWN_ERROR;
			g_snprintf(error_cause, 512, "No configuration file, can't create permanent room");
			goto prepare_response;
		}
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		json_t *room = json_object_get(root, "room");
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		if(room_id == 0 && room_id_str == NULL) {
			JANUS_LOG(LOG_WARN, "Desired room ID is empty, which is not allowed... picking random ID instead\n");
		}
		janus_mutex_lock(&rooms_mutex);
		if(room_id > 0 || room_id_str != NULL) {
			/* Let's make sure the room doesn't exist already */
			if(g_hash_table_lookup(rooms, string_ids ? (gpointer)room_id_str : (gpointer)&room_id) != NULL) {
				/* It does... */
				janus_mutex_unlock(&rooms_mutex);
				error_code = JANUS_AUDIOBRIDGE_ERROR_ROOM_EXISTS;
				JANUS_LOG(LOG_ERR, "Room %s already exists!\n", room_id_str);
				g_snprintf(error_cause, 512, "Room %s already exists", room_id_str);
				goto prepare_response;
			}
		}
		/* Create the AudioBridge room */
		janus_audiobridge_room *audiobridge = g_malloc0(sizeof(janus_audiobridge_room));
		janus_refcount_init(&audiobridge->ref, janus_audiobridge_room_free);
		/* Generate a random ID, if needed */
		gboolean room_id_allocated = FALSE;
		if(!string_ids && room_id == 0) {
			while(room_id == 0) {
				room_id = janus_random_uint64();
				if(g_hash_table_lookup(rooms, &room_id) != NULL) {
					/* Room ID already taken, try another one */
					room_id = 0;
				}
			}
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else if(string_ids && room_id_str == NULL) {
			while(room_id_str == NULL) {
				room_id_str = janus_random_uuid();
				if(g_hash_table_lookup(rooms, room_id_str) != NULL) {
					/* Room ID already taken, try another one */
					g_clear_pointer(&room_id_str, g_free);
				}
			}
			room_id_allocated = TRUE;
		}
		audiobridge->room_id = room_id;
		audiobridge->room_id_str = room_id_str ? g_strdup(room_id_str) : NULL;
		char *description = NULL;
		if(desc != NULL && strlen(json_string_value(desc)) > 0) {
			description = g_strdup(json_string_value(desc));
		} else {
			char roomname[255];
			g_snprintf(roomname, 255, "Room %s", audiobridge->room_id_str);
			description = g_strdup(roomname);
		}
		audiobridge->room_name = description;
		audiobridge->is_private = is_private ? json_is_true(is_private) : FALSE;
		if(secret)
			audiobridge->room_secret = g_strdup(json_string_value(secret));
		if(pin)
			audiobridge->room_pin = g_strdup(json_string_value(pin));
		if(sampling)
			audiobridge->sampling_rate = json_integer_value(sampling);
		else
			audiobridge->sampling_rate = 16000;
		audiobridge->spatial_audio = spatial ? json_is_true(spatial) : FALSE;
		audiobridge->audiolevel_ext = audiolevel_ext ? json_is_true(audiolevel_ext) : TRUE;
		audiobridge->audiolevel_event = audiolevel_event ? json_is_true(audiolevel_event) : FALSE;
		if(audiobridge->audiolevel_event) {
			audiobridge->audio_active_packets = 100;
			if(json_integer_value(audio_active_packets) > 0) {
				audiobridge->audio_active_packets = json_integer_value(audio_active_packets);
			} else {
				JANUS_LOG(LOG_WARN, "Invalid audio_active_packets value provided, using default: %d\n",
					audiobridge->audio_active_packets);
			}
			audiobridge->audio_level_average = 25;
			if(json_integer_value(audio_level_average) > 0) {
				audiobridge->audio_level_average = json_integer_value(audio_level_average);
			} else {
				JANUS_LOG(LOG_WARN, "Invalid audio_level_average value provided, using default: %d\n",
					audiobridge->audio_level_average);
			}
		}
		audiobridge->default_prebuffering = default_prebuffering ?
			json_integer_value(default_prebuffering) : DEFAULT_PREBUFFERING;
		if(audiobridge->default_prebuffering > MAX_PREBUFFERING) {
			audiobridge->default_prebuffering = DEFAULT_PREBUFFERING;
			JANUS_LOG(LOG_WARN, "Invalid default_prebuffering value provided (too high), using default: %d\n",
				audiobridge->default_prebuffering);
		}
		audiobridge->default_expectedloss = 0;
		if(default_expectedloss != NULL) {
			int expectedloss = json_integer_value(default_expectedloss);
			if(expectedloss > 20) {
				JANUS_LOG(LOG_WARN, "Invalid expectedloss value provided, using default: 0\n");
			} else {
				audiobridge->default_expectedloss = expectedloss;
			}
		}
		audiobridge->default_bitrate = 0;
		if(default_bitrate != NULL) {
			audiobridge->default_bitrate = json_integer_value(default_bitrate);
			if(audiobridge->default_bitrate < 500 || audiobridge->default_bitrate > 512000) {
				JANUS_LOG(LOG_WARN, "Invalid bitrate %"SCNi32", falling back to auto\n", audiobridge->default_bitrate);
				audiobridge->default_bitrate = 0;
			}
		}
		switch(audiobridge->sampling_rate) {
			case 8000:
			case 12000:
			case 16000:
			case 24000:
			case 48000:
				JANUS_LOG(LOG_VERB, "Sampling rate for mixing: %"SCNu32"\n", audiobridge->sampling_rate);
				break;
			default:
				if(room_id_allocated)
					g_free(room_id_str);
				janus_audiobridge_room_destroy(audiobridge);
				janus_mutex_unlock(&rooms_mutex);
				JANUS_LOG(LOG_ERR, "Unsupported sampling rate %"SCNu32"...\n", audiobridge->sampling_rate);
				error_code = JANUS_AUDIOBRIDGE_ERROR_UNKNOWN_ERROR;
				g_snprintf(error_cause, 512, "Unsupported sampling rate %"SCNu32, audiobridge->sampling_rate);
				janus_audiobridge_room_destroy(audiobridge);
				goto prepare_response;
		}
		audiobridge->room_ssrc = janus_random_uint32();
		g_atomic_int_set(&audiobridge->record, 0);
		if(record && json_is_true(record))
			g_atomic_int_set(&audiobridge->record, 1);
		if(recfile)
			audiobridge->record_file = g_strdup(json_string_value(recfile));
		if(recdir) {
			audiobridge->record_dir = g_strdup(json_string_value(recdir));
			if(janus_mkdir(audiobridge->record_dir, 0755) < 0) {
				/* FIXME Should this be fatal, when creating a room? */
				JANUS_LOG(LOG_WARN, "AudioBridge mkdir (%s) error: %d (%s)\n", audiobridge->record_dir, errno, g_strerror(errno));
			}
		}
		audiobridge->recording = NULL;
		if(mjrs && json_is_true(mjrs))
			audiobridge->mjrs = TRUE;
		if(mjrsdir)
			audiobridge->mjrs_dir = g_strdup(json_string_value(mjrsdir));
		audiobridge->allow_plainrtp = FALSE;
		if(allowrtp && json_is_true(allowrtp))
			audiobridge->allow_plainrtp = TRUE;
		audiobridge->destroy = 0;
		audiobridge->participants = g_hash_table_new_full(
			string_ids ? g_str_hash : g_int64_hash, string_ids ? g_str_equal : g_int64_equal,
			(GDestroyNotify)g_free, (GDestroyNotify)janus_audiobridge_participant_unref);
		audiobridge->anncs = g_hash_table_new_full(g_str_hash, g_str_equal,
			(GDestroyNotify)g_free, (GDestroyNotify)janus_audiobridge_participant_unref);
		audiobridge->allowed = g_hash_table_new_full(g_str_hash, g_str_equal, (GDestroyNotify)g_free, NULL);
		if(allowed != NULL) {
			/* Populate the "allowed" list as an ACL for people trying to join */
			if(json_array_size(allowed) > 0) {
				size_t i = 0;
				for(i=0; i<json_array_size(allowed); i++) {
					const char *token = json_string_value(json_array_get(allowed, i));
					if(!g_hash_table_lookup(audiobridge->allowed, token))
						g_hash_table_insert(audiobridge->allowed, g_strdup(token), GINT_TO_POINTER(TRUE));
				}
			}
			audiobridge->check_tokens = TRUE;
		}
		g_atomic_int_set(&audiobridge->destroyed, 0);
		janus_mutex_init(&audiobridge->mutex);
		if(groups != NULL && json_array_size(groups) > 0) {
			/* Populate the group hashtable, and create the related indexes */
			audiobridge->groups = g_hash_table_new_full(g_str_hash, g_str_equal, (GDestroyNotify)g_free, NULL);
			audiobridge->groups_byid = g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify)g_free);
			size_t i = 0;
			int count = 0;
			for(i=0; i<json_array_size(groups); i++) {
				const char *name = json_string_value(json_array_get(groups, i));
				if(g_hash_table_lookup(audiobridge->groups, name)) {
					JANUS_LOG(LOG_WARN, "Duplicated group name '%s', skipping\n", name);
				} else {
					count++;
					g_hash_table_insert(audiobridge->groups, g_strdup(name), GUINT_TO_POINTER(count));
					g_hash_table_insert(audiobridge->groups_byid, GUINT_TO_POINTER(count), g_strdup(name));
				}
			}
			if(count == 0) {
				JANUS_LOG(LOG_WARN, "Empty or invalid groups array provided, groups will be disabled\n");
				g_hash_table_destroy(audiobridge->groups);
				g_hash_table_destroy(audiobridge->groups_byid);
				audiobridge->groups = NULL;
				audiobridge->groups_byid = NULL;
			}
		}
		audiobridge->rtp_forwarders = g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify)janus_audiobridge_rtp_forwarder_destroy);
		audiobridge->rtp_encoder = NULL;
		audiobridge->rtp_udp_sock = -1;
		janus_mutex_init(&audiobridge->rtp_mutex);
		g_hash_table_insert(rooms,
			string_ids ? (gpointer)g_strdup(audiobridge->room_id_str) : (gpointer)janus_uint64_dup(audiobridge->room_id),
			audiobridge);
		JANUS_LOG(LOG_VERB, "Created AudioBridge: %s (%s, %s, secret: %s, pin: %s)\n",
			audiobridge->room_id_str, audiobridge->room_name,
			audiobridge->is_private ? "private" : "public",
			audiobridge->room_secret ? audiobridge->room_secret : "no secret",
			audiobridge->room_pin ? audiobridge->room_pin : "no pin");
		/* We need a thread for the mix */
		GError *error = NULL;
		char tname[16];
		g_snprintf(tname, sizeof(tname), "mixer %s", audiobridge->room_id_str);
		janus_refcount_increase(&audiobridge->ref);
		audiobridge->thread = g_thread_try_new(tname, &janus_audiobridge_mixer_thread, audiobridge, &error);
		if(error != NULL) {
			JANUS_LOG(LOG_ERR, "Got error %d (%s) trying to launch the mixer thread...\n",
				error->code, error->message ? error->message : "??");
			error_code = JANUS_AUDIOBRIDGE_ERROR_UNKNOWN_ERROR;
			g_snprintf(error_cause, 512, "Got error %d (%s) trying to launch the mixer thread",
				error->code, error->message ? error->message : "??");
			g_error_free(error);
			janus_refcount_decrease(&audiobridge->ref);
			g_hash_table_remove(rooms, string_ids ? (gpointer)audiobridge->room_id_str : (gpointer)&audiobridge->room_id);
			janus_mutex_unlock(&rooms_mutex);
			if(room_id_allocated)
				g_free(room_id_str);
			goto prepare_response;
		}
		if(save) {
			/* This room is permanent: save to the configuration file too
			 * FIXME: We should check if anything fails... */
			JANUS_LOG(LOG_VERB, "Saving room %s permanently in config file\n", audiobridge->room_id_str);
			janus_mutex_lock(&config_mutex);
			char cat[BUFSIZ], value[BUFSIZ];
			/* The room ID is the category (prefixed by "room-") */
			g_snprintf(cat, BUFSIZ, "room-%s", audiobridge->room_id_str);
			janus_config_category *c = janus_config_get_create(config, NULL, janus_config_type_category, cat);
			/* Now for the values */
			janus_config_add(config, c, janus_config_item_create("description", audiobridge->room_name));
			if(audiobridge->is_private)
				janus_config_add(config, c, janus_config_item_create("is_private", "yes"));
			g_snprintf(value, BUFSIZ, "%"SCNu32, audiobridge->sampling_rate);
			janus_config_add(config, c, janus_config_item_create("sampling_rate", value));
			if(audiobridge->room_secret)
				janus_config_add(config, c, janus_config_item_create("secret", audiobridge->room_secret));
			if(audiobridge->room_pin)
				janus_config_add(config, c, janus_config_item_create("pin", audiobridge->room_pin));
			if(audiobridge->audiolevel_ext) {
				janus_config_add(config, c, janus_config_item_create("audiolevel_ext", "yes"));
				if(audiobridge->audiolevel_event)
					janus_config_add(config, c, janus_config_item_create("audiolevel_event", "yes"));
				if(audiobridge->audio_active_packets > 0) {
					g_snprintf(value, BUFSIZ, "%d", audiobridge->audio_active_packets);
					janus_config_add(config, c, janus_config_item_create("audio_active_packets", value));
				}
				if(audiobridge->audio_level_average > 0) {
					g_snprintf(value, BUFSIZ, "%d", audiobridge->audio_level_average);
					janus_config_add(config, c, janus_config_item_create("audio_level_average", value));
				}
			}
			if(audiobridge->default_prebuffering != DEFAULT_PREBUFFERING) {
				g_snprintf(value, BUFSIZ, "%d", audiobridge->default_prebuffering);
				janus_config_add(config, c, janus_config_item_create("default_prebuffering", value));
			}
			if(audiobridge->allow_plainrtp)
				janus_config_add(config, c, janus_config_item_create("allow_rtp_participants", "yes"));
			if(audiobridge->groups) {
				/* Save array of groups */
				janus_config_array *gl = janus_config_array_create("groups");
				janus_config_add(config, c, gl);
				GHashTableIter iter;
				gpointer key;
				g_hash_table_iter_init(&iter, audiobridge->groups);
				while(g_hash_table_iter_next(&iter, &key, NULL)) {
					janus_config_add(config, gl, janus_config_item_create(NULL, (char *)key));
				}
			}
			if(audiobridge->record_file) {
				janus_config_add(config, c, janus_config_item_create("record", "yes"));
				janus_config_add(config, c, janus_config_item_create("record_file", audiobridge->record_file));
			}
			if(audiobridge->record_dir)
				janus_config_add(config, c, janus_config_item_create("record_dir", audiobridge->record_dir));
			if(audiobridge->mjrs)
				janus_config_add(config, c, janus_config_item_create("mjrs", "yes"));
			if(audiobridge->mjrs_dir)
				janus_config_add(config, c, janus_config_item_create("mjrs_dir", audiobridge->mjrs_dir));
			if(audiobridge->spatial_audio)
				janus_config_add(config, c, janus_config_item_create("spatial_audio", "yes"));
			/* Save modified configuration */
			if(janus_config_save(config, config_folder, JANUS_AUDIOBRIDGE_PACKAGE) < 0)
				save = FALSE;	/* This will notify the user the room is not permanent */
			janus_mutex_unlock(&config_mutex);
		}
		/* Send info back */
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("created"));
		json_object_set_new(response, "room",
			string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
		json_object_set_new(response, "permanent", save ? json_true() : json_false());
		/* Also notify event handlers */
		if(notify_events && gateway->events_is_enabled()) {
			json_t *info = json_object();
			json_object_set_new(info, "event", json_string("created"));
			json_object_set_new(info, "room",
				string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
			gateway->notify_event(&janus_audiobridge_plugin, session ? session->handle : NULL, info);
		}
		if(room_id_allocated)
			g_free(room_id_str);
		janus_mutex_unlock(&rooms_mutex);
		goto prepare_response;
	} else if(!strcasecmp(request_text, "edit")) {
		JANUS_LOG(LOG_VERB, "Attempt to edit an existing AudioBridge room\n");
		JANUS_VALIDATE_JSON_OBJECT(root, edit_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		/* We only allow for a limited set of properties to be edited */
		json_t *room = json_object_get(root, "room");
		json_t *desc = json_object_get(root, "new_description");
		json_t *secret = json_object_get(root, "new_secret");
		json_t *pin = json_object_get(root, "new_pin");
		json_t *is_private = json_object_get(root, "new_is_private");
		json_t *recdir = json_object_get(root, "new_record_dir");
		json_t *mjrsdir = json_object_get(root, "new_mjrs_dir");
		json_t *permanent = json_object_get(root, "permanent");
		gboolean save = permanent ? json_is_true(permanent) : FALSE;
		if(save && config == NULL) {
			JANUS_LOG(LOG_ERR, "No configuration file, can't edit room permanently\n");
			error_code = JANUS_AUDIOBRIDGE_ERROR_UNKNOWN_ERROR;
			g_snprintf(error_cause, 512, "No configuration file, can't edit room permanently");
			goto prepare_response;
		}
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
			string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(audiobridge == NULL) {
			janus_mutex_unlock(&rooms_mutex);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		janus_mutex_lock(&audiobridge->mutex);
		/* A secret may be required for this action */
		JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
		if(error_code != 0) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
			goto prepare_response;
		}
		/* Edit the room properties that were provided */
		if(desc != NULL && strlen(json_string_value(desc)) > 0) {
			char *old_description = audiobridge->room_name;
			char *new_description = g_strdup(json_string_value(desc));
			audiobridge->room_name = new_description;
			g_free(old_description);
		}
		if(is_private)
			audiobridge->is_private = json_is_true(is_private);
		if(secret && strlen(json_string_value(secret)) > 0) {
			char *old_secret = audiobridge->room_secret;
			char *new_secret = g_strdup(json_string_value(secret));
			audiobridge->room_secret = new_secret;
			g_free(old_secret);
		}
		if(pin) {
			char *old_pin = audiobridge->room_pin;
			if(strlen(json_string_value(pin)) > 0) {
				char *new_pin = g_strdup(json_string_value(pin));
				audiobridge->room_pin = new_pin;
			} else {
				audiobridge->room_pin = NULL;
			}
			g_free(old_pin);
		}
		if(recdir) {
			char *old_record_dir = audiobridge->record_dir;
			char *new_record_dir = g_strdup(json_string_value(recdir));
			audiobridge->record_dir = new_record_dir;
			g_free(old_record_dir);
		}
		if(mjrsdir) {
			char *old_mjrs_dir = audiobridge->mjrs_dir;
			char *new_mjrs_dir = g_strdup(json_string_value(mjrsdir));
			audiobridge->mjrs_dir = new_mjrs_dir;
			g_free(old_mjrs_dir);
		}
		if(save) {
			/* This change is permanent: save to the configuration file too
			 * FIXME: We should check if anything fails... */
			JANUS_LOG(LOG_VERB, "Modifying room %s permanently in config file\n", room_id_str);
			janus_mutex_lock(&config_mutex);
			char cat[BUFSIZ], value[BUFSIZ];
			/* The room ID is the category (prefixed by "room-") */
			g_snprintf(cat, BUFSIZ, "room-%s", room_id_str);
			/* Remove the old category first */
			janus_config_remove(config, NULL, cat);
			/* Now write the room details again */
			janus_config_category *c = janus_config_get_create(config, NULL, janus_config_type_category, cat);
			janus_config_add(config, c, janus_config_item_create("description", audiobridge->room_name));
			if(audiobridge->is_private)
				janus_config_add(config, c, janus_config_item_create("is_private", "yes"));
			g_snprintf(value, BUFSIZ, "%"SCNu32, audiobridge->sampling_rate);
			janus_config_add(config, c, janus_config_item_create("sampling_rate", value));
			if(audiobridge->room_secret)
				janus_config_add(config, c, janus_config_item_create("secret", audiobridge->room_secret));
			if(audiobridge->room_pin)
				janus_config_add(config, c, janus_config_item_create("pin", audiobridge->room_pin));
			if(audiobridge->audiolevel_ext) {
				janus_config_add(config, c, janus_config_item_create("audiolevel_ext", "yes"));
				if(audiobridge->audiolevel_event)
					janus_config_add(config, c, janus_config_item_create("audiolevel_event", "yes"));
				if(audiobridge->audio_active_packets > 0) {
					g_snprintf(value, BUFSIZ, "%d", audiobridge->audio_active_packets);
					janus_config_add(config, c, janus_config_item_create("audio_active_packets", value));
				}
				if(audiobridge->audio_level_average > 0) {
					g_snprintf(value, BUFSIZ, "%d", audiobridge->audio_level_average);
					janus_config_add(config, c, janus_config_item_create("audio_level_average", value));
				}
			}
			if(audiobridge->default_prebuffering != DEFAULT_PREBUFFERING) {
				g_snprintf(value, BUFSIZ, "%d", audiobridge->default_prebuffering);
				janus_config_add(config, c, janus_config_item_create("default_prebuffering", value));
			}
			if(audiobridge->allow_plainrtp)
				janus_config_add(config, c, janus_config_item_create("allow_rtp_participants", "yes"));
			if(audiobridge->groups) {
				/* Save array of groups */
				janus_config_array *gl = janus_config_array_create("groups");
				janus_config_add(config, c, gl);
				GHashTableIter iter;
				gpointer key;
				g_hash_table_iter_init(&iter, audiobridge->groups);
				while(g_hash_table_iter_next(&iter, &key, NULL)) {
					janus_config_add(config, gl, janus_config_item_create(NULL, (char *)key));
				}
			}
			if(audiobridge->record_file) {
				janus_config_add(config, c, janus_config_item_create("record", "yes"));
				janus_config_add(config, c, janus_config_item_create("record_file", audiobridge->record_file));
			}
			if(audiobridge->record_dir)
				janus_config_add(config, c, janus_config_item_create("record_dir", audiobridge->record_dir));
			if(audiobridge->mjrs)
				janus_config_add(config, c, janus_config_item_create("mjrs", "yes"));
			if(audiobridge->mjrs_dir)
				janus_config_add(config, c, janus_config_item_create("mjrs_dir", audiobridge->mjrs_dir));
			if(audiobridge->spatial_audio)
				janus_config_add(config, c, janus_config_item_create("spatial_audio", "yes"));
			/* Save modified configuration */
			if(janus_config_save(config, config_folder, JANUS_AUDIOBRIDGE_PACKAGE) < 0)
				save = FALSE;	/* This will notify the user the room changes are not permanent */
			janus_mutex_unlock(&config_mutex);
		}
		/* Prepare response/notification */
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("edited"));
		json_object_set_new(response, "room",
			string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
		json_object_set_new(response, "permanent", save ? json_true() : json_false());
		/* Also notify event handlers */
		if(notify_events && gateway->events_is_enabled()) {
			json_t *info = json_object();
			json_object_set_new(info, "event", json_string("edited"));
			json_object_set_new(info, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
			gateway->notify_event(&janus_audiobridge_plugin, session ? session->handle : NULL, info);
		}
		janus_mutex_unlock(&audiobridge->mutex);
		janus_mutex_unlock(&rooms_mutex);
		/* Done */
		JANUS_LOG(LOG_VERB, "Audiobridge room edited\n");
		goto prepare_response;
	} else if(!strcasecmp(request_text, "destroy")) {
		JANUS_LOG(LOG_VERB, "Attempt to destroy an existing AudioBridge room\n");
		JANUS_VALIDATE_JSON_OBJECT(root, destroy_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		json_t *room = json_object_get(root, "room");
		json_t *permanent = json_object_get(root, "permanent");
		gboolean save = permanent ? json_is_true(permanent) : FALSE;
		if(save && config == NULL) {
			JANUS_LOG(LOG_ERR, "No configuration file, can't destroy room permanently\n");
			error_code = JANUS_AUDIOBRIDGE_ERROR_UNKNOWN_ERROR;
			g_snprintf(error_cause, 512, "No configuration file, can't destroy room permanently");
			goto prepare_response;
		}
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
			string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(audiobridge == NULL) {
			janus_mutex_unlock(&rooms_mutex);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		janus_mutex_lock(&audiobridge->mutex);
		/* A secret may be required for this action */
		JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
		if(error_code != 0) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
			goto prepare_response;
		}
		/* Remove room */
		janus_refcount_increase(&audiobridge->ref);
		g_hash_table_remove(rooms, string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(save) {
			/* This change is permanent: save to the configuration file too
			 * FIXME: We should check if anything fails... */
			JANUS_LOG(LOG_VERB, "Destroying room %s permanently in config file\n", room_id_str);
			janus_mutex_lock(&config_mutex);
			char cat[BUFSIZ];
			/* The room ID is the category (prefixed by "room-") */
			g_snprintf(cat, BUFSIZ, "room-%s", room_id_str);
			janus_config_remove(config, NULL, cat);
			/* Save modified configuration */
			if(janus_config_save(config, config_folder, JANUS_AUDIOBRIDGE_PACKAGE) < 0)
				save = FALSE;	/* This will notify the user the room destruction is not permanent */
			janus_mutex_unlock(&config_mutex);
		}
		/* Prepare response/notification */
		json_t *destroyed = json_object();
		json_object_set_new(destroyed, "audiobridge", json_string("destroyed"));
		json_object_set_new(destroyed, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
		/* Notify all participants that the fun is over, and that they'll be kicked */
		JANUS_LOG(LOG_VERB, "Notifying all participants\n");
		GHashTableIter iter;
		gpointer value;
		g_hash_table_iter_init(&iter, audiobridge->participants);
		while(g_hash_table_iter_next(&iter, NULL, &value)) {
			janus_audiobridge_participant *p = value;
			if(p && p->session) {
				if(p->room) {
					p->room = NULL;
					janus_refcount_decrease(&audiobridge->ref);
				}
				int ret = gateway->push_event(p->session->handle, &janus_audiobridge_plugin, NULL, destroyed, NULL);
				JANUS_LOG(LOG_VERB, "  >> %d (%s)\n", ret, janus_get_api_error(ret));
				/* Get rid of queued packets */
				janus_mutex_lock(&p->qmutex);
				g_atomic_int_set(&p->active, 0);
				while(p->inbuf) {
					GList *first = g_list_first(p->inbuf);
					janus_audiobridge_rtp_relay_packet *pkt = (janus_audiobridge_rtp_relay_packet *)first->data;
					p->inbuf = g_list_delete_link(p->inbuf, first);
					first = NULL;
					if(pkt == NULL)
						continue;
					g_free(pkt->data);
					pkt->data = NULL;
					g_free(pkt);
					pkt = NULL;
				}
				janus_mutex_unlock(&p->qmutex);
				/* Request a WebRTC hangup */
				gateway->close_pc(p->session->handle);
			}
		}
		json_decref(destroyed);
		/* Also notify event handlers */
		if(notify_events && gateway->events_is_enabled()) {
			json_t *info = json_object();
			json_object_set_new(info, "event", json_string("destroyed"));
			json_object_set_new(info, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
			gateway->notify_event(&janus_audiobridge_plugin, session ? session->handle : NULL, info);
		}
		janus_mutex_unlock(&audiobridge->mutex);
		janus_mutex_unlock(&rooms_mutex);
		janus_refcount_decrease(&audiobridge->ref);
		/* Done */
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("destroyed"));
		json_object_set_new(response, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
		json_object_set_new(response, "permanent", save ? json_true() : json_false());
		JANUS_LOG(LOG_VERB, "Audiobridge room destroyed\n");
		goto prepare_response;
	} else if(!strcasecmp(request_text, "enable_recording")) {
		JANUS_VALIDATE_JSON_OBJECT(root, record_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		json_t *record = json_object_get(root, "record");
		json_t *recfile = json_object_get(root, "record_file");
		json_t *recdir = json_object_get(root, "record_dir");
		gboolean recording_active = json_is_true(record);
		/* Lookup room */
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = NULL;
		error_code = janus_audiobridge_access_room(root, TRUE, &audiobridge, error_cause, sizeof(error_cause));
		if(error_code != 0) {
			janus_mutex_unlock(&rooms_mutex);
			JANUS_LOG(LOG_ERR, "Failed to access room\n");
			goto prepare_response;
		}
		/* Set recording status */
		gint room_prev_recording_active = recording_active ? 1 : 0;
		/* Check if we need to create a folder */
		if(recording_active && recdir != NULL) {
			if(janus_mkdir(json_string_value(recdir), 0755) < 0) {
				/* FIXME Should this be fatal, when creating a room? */
				janus_mutex_unlock(&rooms_mutex);
				JANUS_LOG(LOG_ERR, "AudioBridge mkdir (%s) error: %d (%s)\n", audiobridge->record_dir, errno, g_strerror(errno));
				error_code = JANUS_AUDIOBRIDGE_ERROR_UNKNOWN_ERROR;
				g_snprintf(error_cause, 512, "mkdir error: %d (%s)", errno, g_strerror(errno));
				goto prepare_response;
			}
		}
		if(room_prev_recording_active != g_atomic_int_get(&audiobridge->record)) {
			/* Room recording state has changed */
			JANUS_LOG(LOG_VERB, "Recording status changed: prev=%d, curr=%d\n", g_atomic_int_get(&audiobridge->record), recording_active);
			g_atomic_int_set(&audiobridge->record, room_prev_recording_active);
			if(recfile && recording_active) {
				g_free(audiobridge->record_file);
				audiobridge->record_file = g_strdup(json_string_value(recfile));
				JANUS_LOG(LOG_VERB, "Recording file: %s\n", audiobridge->record_file);
			}
			if(recdir && recording_active) {
				g_free(audiobridge->record_dir);
				audiobridge->record_dir = g_strdup(json_string_value(recdir));
				JANUS_LOG(LOG_VERB, "Recording folder: %s\n", audiobridge->record_dir);
			}
		}
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		json_object_set_new(response, "record", json_boolean(g_atomic_int_get(&audiobridge->record)));
		janus_mutex_unlock(&rooms_mutex);
		goto prepare_response;
	} else if(!strcasecmp(request_text, "enable_mjrs")) {
		JANUS_VALIDATE_JSON_OBJECT(root, mjrs_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		json_t *mjrs = json_object_get(root, "mjrs");
		json_t *mjrsdir = json_object_get(root, "mjrs_dir");
		gboolean mjrs_active = json_is_true(mjrs);
		JANUS_LOG(LOG_VERB, "Enable MJR recording: %d\n", (mjrs_active ? 1 : 0));
		/* Lookup room */
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = NULL;
		error_code = janus_audiobridge_access_room(root, TRUE, &audiobridge, error_cause, sizeof(error_cause));
		if(error_code != 0) {
			JANUS_LOG(LOG_ERR, "Failed to access room\n");
			janus_mutex_unlock(&rooms_mutex);
			goto prepare_response;
		}
		janus_refcount_increase(&audiobridge->ref);
		janus_mutex_unlock(&rooms_mutex);
		janus_mutex_lock(&audiobridge->mutex);
		/* Set MJR recording status */
		if(mjrsdir) {
			/* Update the path where to save the MJR files */
			char *old_mjrs_dir = audiobridge->mjrs_dir;
			char *new_mjrs_dir = g_strdup(json_string_value(mjrsdir));
			audiobridge->mjrs_dir = new_mjrs_dir;
			g_free(old_mjrs_dir);
		}
		if(mjrs_active != audiobridge->mjrs) {
			/* Room recording state has changed */
			audiobridge->mjrs = mjrs_active;
			/* Iterate over all participants */
			gpointer value;
			GHashTableIter iter;
			g_hash_table_iter_init(&iter, audiobridge->participants);
			while(g_hash_table_iter_next(&iter, NULL, &value)) {
				janus_audiobridge_participant *participant = value;
				if(participant && participant->session) {
					janus_mutex_lock(&participant->rec_mutex);
					gboolean prev_mjrs_active = participant->mjr_active;
					participant->mjr_active = mjrs_active;
					JANUS_LOG(LOG_VERB, "Setting MJR recording property: %s (room %s, user %s)\n",
						participant->mjr_active ? "true" : "false", audiobridge->room_id_str, participant->user_id_str);
					/* Do we need to do something with the recordings right now? */
					if(participant->mjr_active != prev_mjrs_active) {
						/* Something changed */
						if(!participant->mjr_active) {
							/* Not recording (anymore?) */
							janus_audiobridge_recorder_close(participant);
						} else {
							/* We've started recording, send a PLI/FIR and go on */
							janus_audiobridge_recorder_create(participant);
						}
					}
					janus_mutex_unlock(&participant->rec_mutex);
				}
			}
		}
		janus_mutex_unlock(&audiobridge->mutex);
		janus_refcount_decrease(&audiobridge->ref);
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		json_object_set_new(response, "mjrs", json_boolean(mjrs_active));
		goto prepare_response;
	} else if(!strcasecmp(request_text, "list")) {
		/* List all rooms (but private ones) and their details (except for the secret, of course...) */
		JANUS_LOG(LOG_VERB, "Request for the list for all audiobridge rooms\n");
		gboolean lock_room_list = TRUE;
		if(admin_key != NULL) {
			json_t *admin_key_json = json_object_get(root, "admin_key");
			/* Verify admin_key if it was provided */
			if(admin_key_json != NULL && json_is_string(admin_key_json) && strlen(json_string_value(admin_key_json)) > 0) {
				JANUS_CHECK_SECRET(admin_key, root, "admin_key", error_code, error_cause,
					JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
				if(error_code != 0) {
					goto prepare_response;
				} else {
					lock_room_list = FALSE;
				}
			}
		}
		json_t *list = json_array();
		janus_mutex_lock(&rooms_mutex);
		GHashTableIter iter;
		gpointer value;
		g_hash_table_iter_init(&iter, rooms);
		while(g_hash_table_iter_next(&iter, NULL, &value)) {
			janus_audiobridge_room *room = value;
			if(!room || g_atomic_int_get(&room->destroyed))
				continue;
			janus_refcount_increase(&room->ref);
			if(room->is_private && lock_room_list) {
				/* Skip private room if no valid admin_key was provided */
				JANUS_LOG(LOG_VERB, "Skipping private room '%s'\n", room->room_name);
				janus_refcount_decrease(&room->ref);
				continue;
			}
			json_t *rl = json_object();
			json_object_set_new(rl, "room", string_ids ? json_string(room->room_id_str) : json_integer(room->room_id));
			json_object_set_new(rl, "description", json_string(room->room_name));
			json_object_set_new(rl, "sampling_rate", json_integer(room->sampling_rate));
			json_object_set_new(rl, "spatial_audio", room->spatial_audio ? json_true() : json_false());
			json_object_set_new(rl, "pin_required", room->room_pin ? json_true() : json_false());
			json_object_set_new(rl, "record", g_atomic_int_get(&room->record) ? json_true() : json_false());
			json_object_set_new(rl, "muted", room->muted ? json_true() : json_false());
			json_object_set_new(rl, "num_participants", json_integer(g_hash_table_size(room->participants)));
			json_array_append_new(list, rl);
			janus_refcount_decrease(&room->ref);
		}
		janus_mutex_unlock(&rooms_mutex);
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		json_object_set_new(response, "list", list);
		goto prepare_response;
	} else if(!strcasecmp(request_text, "exists")) {
		/* Check whether a given room exists or not, returns true/false */
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		json_t *room = json_object_get(root, "room");
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		janus_mutex_lock(&rooms_mutex);
		gboolean room_exists = g_hash_table_contains(rooms, string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		janus_mutex_unlock(&rooms_mutex);
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		json_object_set_new(response, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
		json_object_set_new(response, "exists", room_exists ? json_true() : json_false());
		goto prepare_response;
	} else if(!strcasecmp(request_text, "allowed")) {
		JANUS_LOG(LOG_VERB, "Attempt to edit the list of allowed participants in an existing AudioBridge room\n");
		JANUS_VALIDATE_JSON_OBJECT(root, allowed_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		json_t *action = json_object_get(root, "action");
		json_t *room = json_object_get(root, "room");
		json_t *allowed = json_object_get(root, "allowed");
		const char *action_text = json_string_value(action);
		if(strcasecmp(action_text, "enable") && strcasecmp(action_text, "disable") &&
				strcasecmp(action_text, "add") && strcasecmp(action_text, "remove")) {
			JANUS_LOG(LOG_ERR, "Unsupported action '%s' (allowed)\n", action_text);
			error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
			g_snprintf(error_cause, 512, "Unsupported action '%s' (allowed)", action_text);
			goto prepare_response;
		}
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
			string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(audiobridge == NULL) {
			janus_mutex_unlock(&rooms_mutex);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		janus_mutex_lock(&audiobridge->mutex);
		/* A secret may be required for this action */
		JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
		if(error_code != 0) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
			goto prepare_response;
		}
		if(!strcasecmp(action_text, "enable")) {
			JANUS_LOG(LOG_VERB, "Enabling the check on allowed authorization tokens for room %s\n", room_id_str);
			audiobridge->check_tokens = TRUE;
		} else if(!strcasecmp(action_text, "disable")) {
			JANUS_LOG(LOG_VERB, "Disabling the check on allowed authorization tokens for room %s (free entry)\n", room_id_str);
			audiobridge->check_tokens = FALSE;
		} else {
			gboolean add = !strcasecmp(action_text, "add");
			if(allowed) {
				/* Make sure the "allowed" array only contains strings */
				gboolean ok = TRUE;
				if(json_array_size(allowed) > 0) {
					size_t i = 0;
					for(i=0; i<json_array_size(allowed); i++) {
						json_t *a = json_array_get(allowed, i);
						if(!a || !json_is_string(a)) {
							ok = FALSE;
							break;
						}
					}
				}
				if(!ok) {
					JANUS_LOG(LOG_ERR, "Invalid element in the allowed array (not a string)\n");
					error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
					g_snprintf(error_cause, 512, "Invalid element in the allowed array (not a string)");
					janus_mutex_unlock(&audiobridge->mutex);
					janus_mutex_unlock(&rooms_mutex);
					goto prepare_response;
				}
				size_t i = 0;
				for(i=0; i<json_array_size(allowed); i++) {
					const char *token = json_string_value(json_array_get(allowed, i));
					if(add) {
						if(!g_hash_table_lookup(audiobridge->allowed, token))
							g_hash_table_insert(audiobridge->allowed, g_strdup(token), GINT_TO_POINTER(TRUE));
					} else {
						g_hash_table_remove(audiobridge->allowed, token);
					}
				}
			}
		}
		/* Prepare response */
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		json_object_set_new(response, "room",
			string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
		json_t *list = json_array();
		if(strcasecmp(action_text, "disable")) {
			if(g_hash_table_size(audiobridge->allowed) > 0) {
				GHashTableIter iter;
				gpointer key;
				g_hash_table_iter_init(&iter, audiobridge->allowed);
				while(g_hash_table_iter_next(&iter, &key, NULL)) {
					char *token = key;
					json_array_append_new(list, json_string(token));
				}
			}
			json_object_set_new(response, "allowed", list);
		}
		/* Done */
		janus_mutex_unlock(&audiobridge->mutex);
		janus_mutex_unlock(&rooms_mutex);
		JANUS_LOG(LOG_VERB, "Audiobridge room allowed list updated\n");
		goto prepare_response;
	} else if(!strcasecmp(request_text, "mute") || !strcasecmp(request_text, "unmute")) {
		JANUS_LOG(LOG_VERB, "Attempt to mute a participant from an existing AudioBridge room\n");
		JANUS_VALIDATE_JSON_OBJECT(root, secret_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, id_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, idstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		json_t *room = json_object_get(root, "room");
		json_t *id = json_object_get(root, "id");
		gboolean muted = (!strcasecmp(request_text, "mute")) ? TRUE : FALSE;
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
			string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(audiobridge == NULL) {
			janus_mutex_unlock(&rooms_mutex);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		janus_refcount_increase(&audiobridge->ref);
		janus_mutex_lock(&audiobridge->mutex);
		janus_mutex_unlock(&rooms_mutex);

		/* A secret may be required for this action */
		JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
		if(error_code != 0) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_refcount_decrease(&audiobridge->ref);
			goto prepare_response;
		}

		guint64 user_id = 0;
		char user_id_num[30], *user_id_str = NULL;
		if(!string_ids) {
			user_id = json_integer_value(id);
			g_snprintf(user_id_num, sizeof(user_id_num), "%"SCNu64, user_id);
			user_id_str = user_id_num;
		} else {
			user_id_str = (char *)json_string_value(id);
		}
		janus_audiobridge_participant *participant = g_hash_table_lookup(audiobridge->participants,
			string_ids ? (gpointer)user_id_str : (gpointer)&user_id);
		if(participant == NULL) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_refcount_decrease(&audiobridge->ref);
			JANUS_LOG(LOG_ERR, "No such user %s in room %s\n", user_id_str, room_id_str);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_USER;
			g_snprintf(error_cause, 512, "No such user %s in room %s", user_id_str, room_id_str);
			goto prepare_response;
		}

		if(participant->muted == muted) {
			/* If someone trying to mute an already muted user, or trying to unmute a user that is not mute),
			then we should do nothing */

			/* Nothing to do, just prepare response */
			response = json_object();
			json_object_set_new(response, "audiobridge", json_string("success"));

			/* Done */
			janus_mutex_unlock(&audiobridge->mutex);
			janus_refcount_decrease(&audiobridge->ref);
			goto prepare_response;
		}

		participant->muted = muted;
		JANUS_LOG(LOG_VERB, "Setting muted property: %s (room %s, user %s)\n",
			participant->muted ? "true" : "false", participant->room->room_id_str, participant->user_id_str);
		if(participant->muted) {
			/* Clear the queued packets waiting to be handled */
			janus_mutex_lock(&participant->qmutex);
			while(participant->inbuf) {
				GList *first = g_list_first(participant->inbuf);
				janus_audiobridge_rtp_relay_packet *pkt = (janus_audiobridge_rtp_relay_packet *)first->data;
				participant->inbuf = g_list_delete_link(participant->inbuf, first);
				first = NULL;
				if(pkt == NULL)
					continue;
				g_free(pkt->data);
				pkt->data = NULL;
				g_free(pkt);
				pkt = NULL;
			}
			janus_mutex_unlock(&participant->qmutex);
		}

		json_t *list = json_array();
		json_t *pl = json_object();
		json_object_set_new(pl, "id",
			string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
		if(participant->display)
			json_object_set_new(pl, "display", json_string(participant->display));
		json_object_set_new(pl, "setup", g_atomic_int_get(&participant->session->started) ? json_true() : json_false());
		json_object_set_new(pl, "muted", participant->muted ? json_true() : json_false());
		if(audiobridge->spatial_audio)
			json_object_set_new(pl, "spatial_position", json_integer(participant->spatial_position));
		json_array_append_new(list, pl);
		json_t *pub = json_object();
		json_object_set_new(pub, "audiobridge", json_string("event"));
		json_object_set_new(pub, "room",
			string_ids ? json_string(room_id_str) : json_integer(room_id));
		json_object_set_new(pub, "participants", list);
		GHashTableIter iter;
		gpointer value;
		g_hash_table_iter_init(&iter, audiobridge->participants);
		while(g_hash_table_iter_next(&iter, NULL, &value)) {
			janus_audiobridge_participant *p = value;
			JANUS_LOG(LOG_VERB, "Notifying participant %s (%s)\n", p->user_id_str, p->display ? p->display : "??");
			int ret = gateway->push_event(p->session->handle, &janus_audiobridge_plugin, NULL, pub, NULL);
			JANUS_LOG(LOG_VERB, "  >> %d (%s)\n", ret, janus_get_api_error(ret));
		}
		json_decref(pub);

		/* Also notify event handlers */
		if(notify_events && gateway->events_is_enabled()) {
			json_t *info = json_object();
			json_object_set_new(info, "event", json_string(request_text));
			json_object_set_new(info, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
			json_object_set_new(info, "id", string_ids ? json_string(user_id_str) : json_integer(user_id));
			gateway->notify_event(&janus_audiobridge_plugin, session ? session->handle : NULL, info);
		}

		JANUS_LOG(LOG_VERB, "%s user %s in room %s\n",
			muted ? "Muted" : "Unmuted", user_id_str, room_id_str);

		/* Prepare response */
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		json_object_set_new(response, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));

		/* Done */
		janus_mutex_unlock(&audiobridge->mutex);
		janus_refcount_decrease(&audiobridge->ref);
		goto prepare_response;
	} else if(!strcasecmp(request_text, "mute_room") || !strcasecmp(request_text, "unmute_room")) {
		JANUS_LOG(LOG_VERB, "Attempt to mute all participants in an existing AudioBridge room\n");
		JANUS_VALIDATE_JSON_OBJECT(root, secret_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		json_t *room = json_object_get(root, "room");
		gboolean muted = (!strcasecmp(request_text, "mute_room")) ? TRUE : FALSE;
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
			string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(audiobridge == NULL) {
			janus_mutex_unlock(&rooms_mutex);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		janus_refcount_increase(&audiobridge->ref);
		janus_mutex_lock(&audiobridge->mutex);
		janus_mutex_unlock(&rooms_mutex);

		/* A secret may be required for this action */
		JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
		if(error_code != 0) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_refcount_decrease(&audiobridge->ref);
			goto prepare_response;
		}

		if(audiobridge->muted == muted) {
			/* If we're already in the right state, just prepare the response */
			response = json_object();
			json_object_set_new(response, "audiobridge", json_string("success"));

			/* Done */
			janus_mutex_unlock(&audiobridge->mutex);
			janus_refcount_decrease(&audiobridge->ref);
			goto prepare_response;
		}
		audiobridge->muted = muted;

		/* Prepare an event to notify all participants */
		json_t *event = json_object();
		json_object_set_new(event, "audiobridge", json_string("event"));
		json_object_set_new(event, "room",
			string_ids ? json_string(room_id_str) : json_integer(room_id));
		json_object_set_new(event, "muted", audiobridge->muted ? json_true() : json_false());
		GHashTableIter iter;
		gpointer value;
		g_hash_table_iter_init(&iter, audiobridge->participants);
		while(g_hash_table_iter_next(&iter, NULL, &value)) {
			janus_audiobridge_participant *p = value;
			JANUS_LOG(LOG_VERB, "Notifying participant %s (%s)\n", p->user_id_str, p->display ? p->display : "??");
			int ret = gateway->push_event(p->session->handle, &janus_audiobridge_plugin, NULL, event, NULL);
			JANUS_LOG(LOG_VERB, "  >> %d (%s)\n", ret, janus_get_api_error(ret));
		}
		json_decref(event);

		/* Also notify event handlers */
		if(notify_events && gateway->events_is_enabled()) {
			json_t *info = json_object();
			json_object_set_new(info, "event", json_string(request_text));
			json_object_set_new(info, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
			gateway->notify_event(&janus_audiobridge_plugin, session ? session->handle : NULL, info);
		}

		JANUS_LOG(LOG_VERB, "%s all users in room %s\n", muted ? "Muted" : "Unmuted", room_id_str);

		/* Prepare response */
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		json_object_set_new(response, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));

		/* Done */
		janus_mutex_unlock(&audiobridge->mutex);
		janus_refcount_decrease(&audiobridge->ref);
		goto prepare_response;
	} else if(!strcasecmp(request_text, "kick")) {
		JANUS_LOG(LOG_VERB, "Attempt to kick a participant from an existing AudioBridge room\n");
		JANUS_VALIDATE_JSON_OBJECT(root, secret_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, id_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, idstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		json_t *room = json_object_get(root, "room");
		json_t *id = json_object_get(root, "id");
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
			string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(audiobridge == NULL) {
			janus_mutex_unlock(&rooms_mutex);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		janus_refcount_increase(&audiobridge->ref);
		janus_mutex_unlock(&rooms_mutex);
		janus_mutex_lock(&audiobridge->mutex);
		/* A secret may be required for this action */
		JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
		if(error_code != 0) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_refcount_decrease(&audiobridge->ref);
			goto prepare_response;
		}
		guint64 user_id = 0;
		char user_id_num[30], *user_id_str = NULL;
		if(!string_ids) {
			user_id = json_integer_value(id);
			g_snprintf(user_id_num, sizeof(user_id_num), "%"SCNu64, user_id);
			user_id_str = user_id_num;
		} else {
			user_id_str = (char *)json_string_value(id);
		}
		janus_audiobridge_participant *participant = g_hash_table_lookup(audiobridge->participants,
			string_ids ? (gpointer)user_id_str : (gpointer)&user_id);
		if(participant == NULL) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_refcount_decrease(&audiobridge->ref);
			JANUS_LOG(LOG_ERR, "No such user %s in room %s\n", user_id_str, room_id_str);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_USER;
			g_snprintf(error_cause, 512, "No such user %s in room %s", user_id_str, room_id_str);
			goto prepare_response;
		}
		/* Notify all participants about the kick */
		json_t *event = json_object();
		json_object_set_new(event, "audiobridge", json_string("event"));
		json_object_set_new(event, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
		json_object_set_new(event, "kicked", string_ids ? json_string(user_id_str) : json_integer(user_id));
		GHashTableIter iter;
		gpointer value;
		g_hash_table_iter_init(&iter, audiobridge->participants);
		while(g_hash_table_iter_next(&iter, NULL, &value)) {
			janus_audiobridge_participant *p = value;
			JANUS_LOG(LOG_VERB, "Notifying participant %s (%s)\n", p->user_id_str, p->display ? p->display : "??");
			int ret = gateway->push_event(p->session->handle, &janus_audiobridge_plugin, NULL, event, NULL);
			JANUS_LOG(LOG_VERB, "  >> %d (%s)\n", ret, janus_get_api_error(ret));
		}
		json_decref(event);
		/* Also notify event handlers */
		if(notify_events && gateway->events_is_enabled()) {
			json_t *info = json_object();
			json_object_set_new(info, "event", json_string("kicked"));
			json_object_set_new(info, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
			json_object_set_new(info, "id", string_ids ? json_string(user_id_str) : json_integer(user_id));
			gateway->notify_event(&janus_audiobridge_plugin, session ? session->handle : NULL, info);
		}
		/* Tell the core to tear down the PeerConnection, hangup_media will do the rest */
		if(participant && participant->session)
			gateway->close_pc(participant->session->handle);
		JANUS_LOG(LOG_VERB, "Kicked user %s from room %s\n", user_id_str, room_id_str);
		/* Prepare response */
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		/* Done */
		janus_mutex_unlock(&audiobridge->mutex);
		janus_refcount_decrease(&audiobridge->ref);
		goto prepare_response;
	} else if(!strcasecmp(request_text, "kick_all")) {
		JANUS_LOG(LOG_VERB, "Attempt to kick all participants from an existing AudioBridge room\n");
		JANUS_VALIDATE_JSON_OBJECT(root, secret_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		json_t *room = json_object_get(root, "room");
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
			string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(audiobridge == NULL) {
			janus_mutex_unlock(&rooms_mutex);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		janus_refcount_increase(&audiobridge->ref);
		janus_mutex_unlock(&rooms_mutex);
		janus_mutex_lock(&audiobridge->mutex);
		/* A secret may be required for this action */
		JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
		if(error_code != 0) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_refcount_decrease(&audiobridge->ref);
			goto prepare_response;
		}
		GHashTableIter kick_iter;
		gpointer kick_value;
		g_hash_table_iter_init(&kick_iter, audiobridge->participants);
		while(g_hash_table_iter_next(&kick_iter, NULL, &kick_value)) {
			janus_audiobridge_participant *participant = kick_value;
			JANUS_LOG(LOG_VERB, "Kicking participant %s (%s)\n",
					participant->user_id_str, participant->display ? participant->display : "??");
			guint64 user_id = 0;
			char user_id_num[30], *user_id_str = NULL;
			if(string_ids) {
				user_id_str = participant->user_id_str;
			} else {
				user_id = participant->user_id;
				g_snprintf(user_id_num, sizeof(user_id_num), "%"SCNu64, user_id);
				user_id_str = user_id_num;
			}
			/* Notify all participants about the kick */
			json_t *event = json_object();
			json_object_set_new(event, "audiobridge", json_string("event"));
			json_object_set_new(event, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
			json_object_set_new(event, "kicked_all", string_ids ? json_string(user_id_str) : json_integer(user_id));
			JANUS_LOG(LOG_VERB, "Notifying participant %s (%s)\n", participant->user_id_str, participant->display ? participant->display : "??");
			int ret = gateway->push_event(participant->session->handle, &janus_audiobridge_plugin, NULL, event, NULL);
			JANUS_LOG(LOG_VERB, "  >> %d (%s)\n", ret, janus_get_api_error(ret));
			json_decref(event);
			/* Also notify event handlers */
			if(notify_events && gateway->events_is_enabled()) {
				json_t *info = json_object();
				json_object_set_new(info, "event", json_string("kicked_all"));
				json_object_set_new(info, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
				json_object_set_new(info, "id", string_ids ? json_string(user_id_str) : json_integer(user_id));
				gateway->notify_event(&janus_audiobridge_plugin, session ? session->handle : NULL, info);
			}
			/* Tell the core to tear down the PeerConnection, hangup_media will do the rest */
			if(participant && participant->session)
				gateway->close_pc(participant->session->handle);
			JANUS_LOG(LOG_VERB, "Kicked user %s from room %s\n", user_id_str, room_id_str);
		}
		/* Prepare response */
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		/* Done */
		janus_mutex_unlock(&audiobridge->mutex);
		janus_refcount_decrease(&audiobridge->ref);
		goto prepare_response;
	} else if(!strcasecmp(request_text, "listparticipants")) {
		/* List all participants in a room */
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		json_t *room = json_object_get(root, "room");
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
			string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(audiobridge == NULL) {
			janus_mutex_unlock(&rooms_mutex);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		janus_refcount_increase(&audiobridge->ref);
		/* Return a list of all participants */
		json_t *list = json_array();
		GHashTableIter iter;
		gpointer value;
		g_hash_table_iter_init(&iter, audiobridge->participants);
		while(!g_atomic_int_get(&audiobridge->destroyed) && g_hash_table_iter_next(&iter, NULL, &value)) {
			janus_audiobridge_participant *p = value;
			json_t *pl = json_object();
			json_object_set_new(pl, "id", string_ids ? json_string(p->user_id_str) : json_integer(p->user_id));
			if(p->display)
				json_object_set_new(pl, "display", json_string(p->display));
			json_object_set_new(pl, "setup", g_atomic_int_get(&p->session->started) ? json_true() : json_false());
			json_object_set_new(pl, "muted", p->muted ? json_true() : json_false());
			if(p->extmap_id > 0)
				json_object_set_new(pl, "talking", p->talking ? json_true() : json_false());
			if(audiobridge->spatial_audio)
				json_object_set_new(pl, "spatial_position", json_integer(p->spatial_position));
			json_array_append_new(list, pl);
		}
		janus_refcount_decrease(&audiobridge->ref);
		janus_mutex_unlock(&rooms_mutex);
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("participants"));
		json_object_set_new(response, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
		json_object_set_new(response, "participants", list);
		goto prepare_response;
	} else if(!strcasecmp(request_text, "resetdecoder")) {
		/* Mark the Opus decoder for the participant invalid and recreate it */
		janus_audiobridge_participant *participant = (janus_audiobridge_participant *)(session ? session->participant : NULL);
		if(participant == NULL || participant->room == NULL) {
			JANUS_LOG(LOG_ERR, "Can't reset (not in a room)\n");
			error_code = JANUS_AUDIOBRIDGE_ERROR_NOT_JOINED;
			g_snprintf(error_cause, 512, "Can't reset (not in a room)");
			goto prepare_response;
		}
		participant->reset = TRUE;
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		goto prepare_response;
	} else if(!strcasecmp(request_text, "rtp_forward")) {
		JANUS_VALIDATE_JSON_OBJECT(root, rtp_forward_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		if(lock_rtpfwd && admin_key != NULL) {
			/* An admin key was specified: make sure it was provided, and that it's valid */
			JANUS_VALIDATE_JSON_OBJECT(root, adminkey_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			if(error_code != 0)
				goto prepare_response;
			JANUS_CHECK_SECRET(admin_key, root, "admin_key", error_code, error_cause,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
			if(error_code != 0)
				goto prepare_response;
		}
		/* Parse arguments */
		json_t *room = json_object_get(root, "room");
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		guint32 ssrc_value = 0;
		json_t *ssrc = json_object_get(root, "ssrc");
		if(ssrc)
			ssrc_value = json_integer_value(ssrc);
		janus_audiocodec codec = JANUS_AUDIOCODEC_OPUS;
		json_t *rfc = json_object_get(root, "codec");
		if(rfc) {
			codec = janus_audiocodec_from_name(json_string_value(rfc));
			if(codec != JANUS_AUDIOCODEC_OPUS && codec != JANUS_AUDIOCODEC_PCMA && codec != JANUS_AUDIOCODEC_PCMU) {
				JANUS_LOG(LOG_ERR, "Unsupported codec (%s)\n", json_string_value(rfc));
				error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
				g_snprintf(error_cause, 512, "Unsupported codec (%s)", json_string_value(rfc));
				goto prepare_response;
			}
		}
		int ptype = 100;
		json_t *pt = json_object_get(root, "ptype");
		if(pt)
			ptype = json_integer_value(pt);
		uint16_t port = json_integer_value(json_object_get(root, "port"));
		if(port == 0) {
			JANUS_LOG(LOG_ERR, "Invalid port number (%d)\n", port);
			error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
			g_snprintf(error_cause, 512, "Invalid port number (%d)", port);
			goto prepare_response;
		}
		json_t *json_host = json_object_get(root, "host");
		const char *host = json_string_value(json_host), *resolved_host = NULL;
		json_t *json_host_family = json_object_get(root, "host_family");
		const char *host_family = json_string_value(json_host_family);
		int family = 0;
		if(host_family) {
			if(!strcasecmp(host_family, "ipv4")) {
				family = AF_INET;
			} else if(!strcasecmp(host_family, "ipv6")) {
				family = AF_INET6;
			} else {
				JANUS_LOG(LOG_ERR, "Unsupported protocol family (%s)\n", host_family);
				error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
				g_snprintf(error_cause, 512, "Unsupported protocol family (%s)", host_family);
				goto prepare_response;
			}
		}
		/* Check if we need to resolve this host address */
		struct addrinfo *res = NULL, *start = NULL;
		janus_network_address addr;
		janus_network_address_string_buffer addr_buf;
		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		if(family != 0)
			hints.ai_family = family;
		if(getaddrinfo(host, NULL, family != 0 ? &hints : NULL, &res) == 0) {
			start = res;
			while(res != NULL) {
				if(janus_network_address_from_sockaddr(res->ai_addr, &addr) == 0 &&
						janus_network_address_to_string_buffer(&addr, &addr_buf) == 0) {
					/* Resolved */
					resolved_host = janus_network_address_string_from_buffer(&addr_buf);
					freeaddrinfo(start);
					start = NULL;
					break;
				}
				res = res->ai_next;
			}
		}
		if(resolved_host == NULL) {
			if(start)
				freeaddrinfo(start);
			JANUS_LOG(LOG_ERR, "Could not resolve address (%s)...\n", host);
			error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
			g_snprintf(error_cause, 512, "Could not resolve address (%s)...", host);
			goto prepare_response;
		}
		host = resolved_host;
		if(ipv6_disabled && strstr(host, ":") != NULL) {
			JANUS_LOG(LOG_ERR, "Attempt to create an IPv6 forwarder, but IPv6 networking is not available\n");
			error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
			g_snprintf(error_cause, 512, "Attempt to create an IPv6 forwarder, but IPv6 networking is not available");
			goto prepare_response;
		}
		json_t *always = json_object_get(root, "always_on");
		gboolean always_on = always ? json_is_true(always) : FALSE;
		/* Besides, we may need to SRTP-encrypt this stream */
		int srtp_suite = 0;
		const char *srtp_crypto = NULL;
		json_t *s_suite = json_object_get(root, "srtp_suite");
		json_t *s_crypto = json_object_get(root, "srtp_crypto");
		if(s_suite && s_crypto) {
			srtp_suite = json_integer_value(s_suite);
			if(srtp_suite != 32 && srtp_suite != 80) {
				JANUS_LOG(LOG_ERR, "Invalid SRTP suite (%d)\n", srtp_suite);
				error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
				g_snprintf(error_cause, 512, "Invalid SRTP suite (%d)", srtp_suite);
				goto prepare_response;
			}
			srtp_crypto = json_string_value(s_crypto);
		}
		/* Update room */
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
			string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(audiobridge == NULL) {
			janus_mutex_unlock(&rooms_mutex);
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			g_snprintf(error_cause, 512, "No such room (%s", room_id_str);
			goto prepare_response;
		}
		/* A secret may be required for this action */
		JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
		if(error_code != 0) {
			janus_mutex_unlock(&rooms_mutex);
			goto prepare_response;
		}
		janus_mutex_lock(&audiobridge->mutex);
		if(audiobridge->destroyed) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		/* If this room uses groups, check if a valid group name was provided */
		uint group = 0;
		const char *group_name = NULL;
		if(audiobridge->groups != NULL) {
			group_name = json_string_value(json_object_get(root, "group"));
			if(group_name != NULL) {
				group = GPOINTER_TO_UINT(g_hash_table_lookup(audiobridge->groups, group_name));
				if(group == 0) {
					janus_mutex_unlock(&audiobridge->mutex);
					janus_mutex_unlock(&rooms_mutex);
					error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_GROUP;
					g_snprintf(error_cause, 512, "No such group");
					goto prepare_response;
				}
			}
		}

		if(janus_audiobridge_create_udp_socket_if_needed(audiobridge)) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
			error_code = JANUS_AUDIOBRIDGE_ERROR_UNKNOWN_ERROR;
			g_snprintf(error_cause, 512, "Could not open UDP socket for RTP forwarder");
			goto prepare_response;
		}

		if(janus_audiobridge_create_opus_encoder_if_needed(audiobridge)) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
			error_code = JANUS_AUDIOBRIDGE_ERROR_LIBOPUS_ERROR;
			g_snprintf(error_cause, 512, "Error creating Opus encoder for RTP forwarder");
			goto prepare_response;
		}

		guint32 stream_id = janus_audiobridge_rtp_forwarder_add_helper(audiobridge, group,
			host, port, ssrc_value, ptype, codec, srtp_suite, srtp_crypto, always_on, 0);
		janus_mutex_unlock(&audiobridge->mutex);
		janus_mutex_unlock(&rooms_mutex);

		/* Done, prepare response */
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		json_object_set_new(response, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
		if(group_name != NULL)
			json_object_set_new(response, "group", json_string(group_name));
		json_object_set_new(response, "stream_id", json_integer(stream_id));
		json_object_set_new(response, "host", json_string(host));
		json_object_set_new(response, "port", json_integer(port));
		goto prepare_response;
	} else if(!strcasecmp(request_text, "stop_rtp_forward")) {
		JANUS_VALIDATE_JSON_OBJECT(root, stop_rtp_forward_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		if(lock_rtpfwd && admin_key != NULL) {
			/* An admin key was specified: make sure it was provided, and that it's valid */
			JANUS_VALIDATE_JSON_OBJECT(root, adminkey_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			if(error_code != 0)
				goto prepare_response;
			JANUS_CHECK_SECRET(admin_key, root, "admin_key", error_code, error_cause,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
			if(error_code != 0)
				goto prepare_response;
		}
		/* Parse parameters */
		json_t *room = json_object_get(root, "room");
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		guint32 stream_id = json_integer_value(json_object_get(root, "stream_id"));
		/* Update room */
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
			string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(audiobridge == NULL) {
			janus_mutex_unlock(&rooms_mutex);
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		/* A secret may be required for this action */
		JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
		if(error_code != 0) {
			janus_mutex_unlock(&rooms_mutex);
			goto prepare_response;
		}
		janus_mutex_lock(&audiobridge->mutex);
		if(audiobridge->destroyed) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		janus_mutex_lock(&audiobridge->rtp_mutex);
		g_hash_table_remove(audiobridge->rtp_forwarders, GUINT_TO_POINTER(stream_id));
		janus_mutex_unlock(&audiobridge->rtp_mutex);
		janus_mutex_unlock(&audiobridge->mutex);
		janus_mutex_unlock(&rooms_mutex);
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		json_object_set_new(response, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
		json_object_set_new(response, "stream_id", json_integer(stream_id));
		goto prepare_response;
	} else if(!strcasecmp(request_text, "listforwarders")) {
		/* List all forwarders in a room */
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		json_t *room = json_object_get(root, "room");
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
			string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(audiobridge == NULL) {
			janus_mutex_unlock(&rooms_mutex);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		if(audiobridge->destroyed) {
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			janus_mutex_unlock(&rooms_mutex);
			goto prepare_response;
		}
		/* A secret may be required for this action */
		JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
		if(error_code != 0) {
			janus_mutex_unlock(&rooms_mutex);
			goto prepare_response;
		}
		/* Return a list of all forwarders */
		json_t *list = json_array();
		GHashTableIter iter;
		gpointer key, value;
		janus_mutex_lock(&audiobridge->rtp_mutex);
		g_hash_table_iter_init(&iter, audiobridge->rtp_forwarders);
		while(g_hash_table_iter_next(&iter, &key, &value)) {
			guint32 stream_id = GPOINTER_TO_UINT(key);
			janus_audiobridge_rtp_forwarder *rf = (janus_audiobridge_rtp_forwarder *)value;
			json_t *fl = json_object();
			json_object_set_new(fl, "stream_id", json_integer(stream_id));
			if(rf->group > 0 && audiobridge->groups_byid != NULL) {
				char *name = g_hash_table_lookup(audiobridge->groups_byid, GUINT_TO_POINTER(rf->group));
				if(name != NULL)
					json_object_set_new(fl, "group", json_string(name));
			}
			char address[100];
			if(rf->serv_addr.sin_family == AF_INET) {
				json_object_set_new(fl, "ip", json_string(
					inet_ntop(AF_INET, &rf->serv_addr.sin_addr, address, sizeof(address))));
			} else {
				json_object_set_new(fl, "ip", json_string(
					inet_ntop(AF_INET6, &rf->serv_addr6.sin6_addr, address, sizeof(address))));
			}
			json_object_set_new(fl, "port", json_integer(ntohs(rf->serv_addr.sin_port)));
			json_object_set_new(fl, "ssrc", json_integer(rf->ssrc ? rf->ssrc : stream_id));
			json_object_set_new(fl, "codec", json_string(janus_audiocodec_name(rf->codec)));
			json_object_set_new(fl, "ptype", json_integer(rf->payload_type));
			if(rf->is_srtp)
				json_object_set_new(fl, "srtp", json_true());
			json_object_set_new(fl, "always_on", rf->always_on ? json_true() : json_false());
			json_array_append_new(list, fl);
		}
		janus_mutex_unlock(&audiobridge->rtp_mutex);
		janus_mutex_unlock(&rooms_mutex);
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("forwarders"));
		json_object_set_new(response, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
		json_object_set_new(response, "rtp_forwarders", list);
		goto prepare_response;
	} else if(!strcasecmp(request_text, "play_file")) {
#ifndef HAVE_LIBOGG
		JANUS_LOG(LOG_VERB, "Playing files unsupported in this instance\n");
		error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_REQUEST;
		g_snprintf(error_cause, 512, "Playing files unsupported in this instance");
		goto prepare_response;
#else
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		JANUS_VALIDATE_JSON_OBJECT(root, play_file_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		if(lock_playfile && admin_key != NULL) {
			/* An admin key was specified: make sure it was provided, and that it's valid */
			JANUS_VALIDATE_JSON_OBJECT(root, adminkey_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			if(error_code != 0)
				goto prepare_response;
			JANUS_CHECK_SECRET(admin_key, root, "admin_key", error_code, error_cause,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
			if(error_code != 0)
				goto prepare_response;
		}
		/* Parse parameters */
		json_t *room = json_object_get(root, "room");
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		/* Update room */
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
			string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(audiobridge == NULL) {
			janus_mutex_unlock(&rooms_mutex);
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		/* A secret may be required for this action */
		JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
		if(error_code != 0) {
			janus_mutex_unlock(&rooms_mutex);
			goto prepare_response;
		}
		janus_mutex_lock(&audiobridge->mutex);
		if(audiobridge->destroyed) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		/* If this room uses groups, make sure a valid group name was provided */
		uint group = 0;
		if(audiobridge->groups != NULL) {
			JANUS_VALIDATE_JSON_OBJECT(root, group_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			if(error_code != 0) {
				janus_mutex_unlock(&audiobridge->mutex);
				janus_mutex_unlock(&rooms_mutex);
				goto prepare_response;
			}
			const char *group_name = json_string_value(json_object_get(root, "group"));
			group = GPOINTER_TO_UINT(g_hash_table_lookup(audiobridge->groups, group_name));
			if(group == 0) {
				janus_mutex_unlock(&audiobridge->mutex);
				janus_mutex_unlock(&rooms_mutex);
				JANUS_LOG(LOG_ERR, "No such group (%s)\n", group_name);
				error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_GROUP;
				g_snprintf(error_cause, 512, "No such group (%s)", group_name);
				goto prepare_response;
			}
		}
		/* Check if an announcement ID has been provided, or generate a random one */
		json_t *id = json_object_get(root, "file_id");
		char *file_id = (char *)json_string_value(id);
		gboolean file_id_allocated = FALSE;
		if(file_id == NULL) {
			/* Generate a random ID */
			while(file_id == NULL) {
				file_id = janus_random_uuid();
				if(g_hash_table_lookup(audiobridge->anncs, file_id) != NULL) {
					/* ID already taken, try another one */
					g_clear_pointer(&file_id, g_free);
				}
			}
			file_id_allocated = TRUE;
			JANUS_LOG(LOG_VERB, "  -- Announcement ID: %s\n", file_id);
		}
		if(g_hash_table_lookup(audiobridge->anncs, file_id) != NULL) {
			/* ID already taken */
			janus_mutex_unlock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
			JANUS_LOG(LOG_ERR, "File ID exists (%s)\n", file_id);
			error_code = JANUS_AUDIOBRIDGE_ERROR_ID_EXISTS;
			g_snprintf(error_cause, 512, "File ID exists (%s)", file_id);
			goto prepare_response;
		}
		/* We "abuse" the participant struct for announcements too */
		janus_audiobridge_participant *p = g_malloc0(sizeof(janus_audiobridge_participant));
		janus_refcount_init(&p->ref, janus_audiobridge_participant_free);
		p->user_id_str = g_strdup(file_id);
		p->codec = JANUS_AUDIOCODEC_OPUS;
		p->volume_gain = 100;
		/* Open the file and check it's usable */
		p->annc = g_malloc0(sizeof(janus_audiobridge_file));
		p->annc->id = g_strdup(file_id);
		p->room = audiobridge;
		p->group = group;
		const char *filename = json_string_value(json_object_get(root, "filename"));
		p->annc->filename = g_strdup(filename);
		p->annc->file = fopen(filename, "rb");
		if(p->annc->file == NULL || janus_audiobridge_file_init(p->annc) < 0) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
			if(file_id_allocated)
				g_free(file_id);
			janus_refcount_decrease(&p->ref);
			JANUS_LOG(LOG_ERR, "Error opening file\n");
			error_code = JANUS_AUDIOBRIDGE_ERROR_UNKNOWN_ERROR;
			g_snprintf(error_cause, 512, "Error opening file");
			goto prepare_response;
		}
		p->annc->loop = json_is_true(json_object_get(root, "loop"));
		/* Setup the opus decoder */
		int opuserror = 0;
		p->stereo = audiobridge->spatial_audio;
		p->spatial_position = 50;
		p->decoder = opus_decoder_create(audiobridge->sampling_rate,
			audiobridge->spatial_audio ? 2 : 1, &opuserror);
		if(opuserror != OPUS_OK) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
			if(file_id_allocated)
				g_free(file_id);
			janus_refcount_decrease(&p->ref);
			JANUS_LOG(LOG_ERR, "Error creating Opus decoder\n");
			error_code = JANUS_AUDIOBRIDGE_ERROR_LIBOPUS_ERROR;
			g_snprintf(error_cause, 512, "Error creating Opus decoder");
			goto prepare_response;
		}
		/* We're done, add the announcement to the room */
		g_hash_table_insert(audiobridge->anncs, g_strdup(p->user_id_str), p);
		janus_mutex_unlock(&audiobridge->mutex);
		janus_mutex_unlock(&rooms_mutex);

		/* Done, prepare response */
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		json_object_set_new(response, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
		json_object_set_new(response, "file_id", json_string(file_id));
		if(file_id_allocated)
			g_free(file_id);
		goto prepare_response;
#endif
	} else if(!strcasecmp(request_text, "is_playing")) {
#ifndef HAVE_LIBOGG
		JANUS_LOG(LOG_VERB, "Playing files unsupported in this instance\n");
		error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_REQUEST;
		g_snprintf(error_cause, 512, "Playing files unsupported in this instance");
		goto prepare_response;
#else
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		JANUS_VALIDATE_JSON_OBJECT(root, checkstop_file_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		if(lock_playfile && admin_key != NULL) {
			/* An admin key was specified: make sure it was provided, and that it's valid */
			JANUS_VALIDATE_JSON_OBJECT(root, adminkey_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			if(error_code != 0)
				goto prepare_response;
			JANUS_CHECK_SECRET(admin_key, root, "admin_key", error_code, error_cause,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
			if(error_code != 0)
				goto prepare_response;
		}
		/* Parse parameters */
		json_t *room = json_object_get(root, "room");
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		/* Update room */
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
			string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(audiobridge == NULL) {
			janus_mutex_unlock(&rooms_mutex);
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		/* A secret may be required for this action */
		JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
		if(error_code != 0) {
			janus_mutex_unlock(&rooms_mutex);
			goto prepare_response;
		}
		janus_mutex_lock(&audiobridge->mutex);
		if(audiobridge->destroyed) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		/* Check if there is such an announcement */
		json_t *id = json_object_get(root, "file_id");
		char *file_id = (char *)json_string_value(id);
		janus_audiobridge_participant *p = g_hash_table_lookup(audiobridge->anncs, file_id);
		gboolean playing = (p && p->annc && p->annc->started);
		janus_mutex_unlock(&audiobridge->mutex);
		janus_mutex_unlock(&rooms_mutex);

		/* Done, prepare response */
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		json_object_set_new(response, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
		json_object_set_new(response, "file_id", json_string(file_id));
		json_object_set_new(response, "playing", playing ? json_true() : json_false());
		goto prepare_response;
#endif
	} else if(!strcasecmp(request_text, "stop_file")) {
#ifndef HAVE_LIBOGG
		JANUS_LOG(LOG_VERB, "Playing files unsupported in this instance\n");
		error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_REQUEST;
		g_snprintf(error_cause, 512, "Playing files unsupported in this instance");
		goto prepare_response;
#else
		if(!string_ids) {
			JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		} else {
			JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		}
		if(error_code != 0)
			goto prepare_response;
		JANUS_VALIDATE_JSON_OBJECT(root, checkstop_file_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto prepare_response;
		if(lock_playfile && admin_key != NULL) {
			/* An admin key was specified: make sure it was provided, and that it's valid */
			JANUS_VALIDATE_JSON_OBJECT(root, adminkey_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			if(error_code != 0)
				goto prepare_response;
			JANUS_CHECK_SECRET(admin_key, root, "admin_key", error_code, error_cause,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
			if(error_code != 0)
				goto prepare_response;
		}
		/* Parse parameters */
		json_t *room = json_object_get(root, "room");
		guint64 room_id = 0;
		char room_id_num[30], *room_id_str = NULL;
		if(!string_ids) {
			room_id = json_integer_value(room);
			g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
			room_id_str = room_id_num;
		} else {
			room_id_str = (char *)json_string_value(room);
		}
		/* Update room */
		janus_mutex_lock(&rooms_mutex);
		janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
			string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
		if(audiobridge == NULL) {
			janus_mutex_unlock(&rooms_mutex);
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		/* A secret may be required for this action */
		JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
		if(error_code != 0) {
			janus_mutex_unlock(&rooms_mutex);
			goto prepare_response;
		}
		janus_mutex_lock(&audiobridge->mutex);
		if(audiobridge->destroyed) {
			janus_mutex_unlock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
			JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
			g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
			goto prepare_response;
		}
		/* Get rid of the announcement: a notification will be sent by the mixer, if needed */
		json_t *id = json_object_get(root, "file_id");
		char *file_id = (char *)json_string_value(id);
		janus_audiobridge_participant *p = g_hash_table_lookup(audiobridge->anncs, file_id);
		gboolean started = (p && p->annc && p->annc->started);
		if(p)
			janus_refcount_increase(&p->ref);
		if(g_hash_table_remove(audiobridge->anncs, file_id) && started) {
			/* Send a notification that this announcement is over */
			JANUS_LOG(LOG_INFO, "[%s] Announcement stopped (%s)\n", audiobridge->room_id_str, file_id);
			json_t *event = json_object();
			json_object_set_new(event, "audiobridge", json_string("announcement-stopped"));
			json_object_set_new(event, "room",
				string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
			json_object_set_new(event, "file_id", json_string(file_id));
			janus_audiobridge_notify_participants(p, event, TRUE);
			json_decref(event);
			/* Also notify event handlers */
			if(notify_events && gateway->events_is_enabled()) {
				json_t *info = json_object();
				json_object_set_new(info, "event", json_string("announcement-stopped"));
				json_object_set_new(info, "room",
					string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
				json_object_set_new(info, "file_id", json_string(file_id));
				gateway->notify_event(&janus_audiobridge_plugin, NULL, info);
			}
		}
		if(p)
			janus_refcount_decrease(&p->ref);
		janus_mutex_unlock(&audiobridge->mutex);
		janus_mutex_unlock(&rooms_mutex);

		/* Done, prepare response */
		response = json_object();
		json_object_set_new(response, "audiobridge", json_string("success"));
		json_object_set_new(response, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
		json_object_set_new(response, "file_id", json_string(file_id));
		goto prepare_response;
#endif
	} else {
		/* Not a request we recognize, don't do anything */
		return NULL;
	}

prepare_response:
		{
			if(error_code == 0 && !response) {
				error_code = JANUS_AUDIOBRIDGE_ERROR_UNKNOWN_ERROR;
				g_snprintf(error_cause, 512, "Invalid response");
			}
			if(error_code != 0) {
				/* Prepare JSON error event */
				response = json_object();
				json_object_set_new(response, "audiobridge", json_string("event"));
				json_object_set_new(response, "error_code", json_integer(error_code));
				json_object_set_new(response, "error", json_string(error_cause));
			}
			return response;
		}

}

struct janus_plugin_result *janus_audiobridge_handle_message(janus_plugin_session *handle, char *transaction, json_t *message, json_t *jsep) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return janus_plugin_result_new(JANUS_PLUGIN_ERROR, g_atomic_int_get(&stopping) ? "Shutting down" : "Plugin not initialized", NULL);

	/* Pre-parse the message */
	int error_code = 0;
	char error_cause[512];
	json_t *root = message;
	json_t *response = NULL;

	janus_mutex_lock(&sessions_mutex);
	janus_audiobridge_session *session = janus_audiobridge_lookup_session(handle);
	if(!session) {
		janus_mutex_unlock(&sessions_mutex);
		JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
		error_code = JANUS_AUDIOBRIDGE_ERROR_UNKNOWN_ERROR;
		g_snprintf(error_cause, 512, "%s", "No session associated with this handle...");
		goto plugin_response;
	}
	/* Increase the reference counter for this session: we'll decrease it after we handle the message */
	janus_refcount_increase(&session->ref);
	janus_mutex_unlock(&sessions_mutex);
	if(g_atomic_int_get(&session->destroyed)) {
		JANUS_LOG(LOG_ERR, "Session has already been marked as destroyed...\n");
		error_code = JANUS_AUDIOBRIDGE_ERROR_UNKNOWN_ERROR;
		g_snprintf(error_cause, 512, "%s", "Session has already been marked as destroyed...");
		goto plugin_response;
	}

	if(message == NULL) {
		JANUS_LOG(LOG_ERR, "No message??\n");
		error_code = JANUS_AUDIOBRIDGE_ERROR_NO_MESSAGE;
		g_snprintf(error_cause, 512, "%s", "No message??");
		goto plugin_response;
	}
	if(!json_is_object(root)) {
		JANUS_LOG(LOG_ERR, "JSON error: not an object\n");
		error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_JSON;
		g_snprintf(error_cause, 512, "JSON error: not an object");
		goto plugin_response;
	}
	/* Get the request first */
	JANUS_VALIDATE_JSON_OBJECT(root, request_parameters,
		error_code, error_cause, TRUE,
		JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
	if(error_code != 0)
		goto plugin_response;
	json_t *request = json_object_get(root, "request");
	/* Some requests ('create', 'destroy', 'exists', 'list') can be handled synchronously */
	const char *request_text = json_string_value(request);
	/* We have a separate method to process synchronous requests, as those may
	 * arrive from the Admin API as well, and so we handle them the same way */
	response = janus_audiobridge_process_synchronous_request(session, root);
	if(response != NULL) {
		/* We got a response, send it back */
		goto plugin_response;
	} else if(!strcasecmp(request_text, "join") || !strcasecmp(request_text, "configure")
			|| !strcasecmp(request_text, "changeroom") || !strcasecmp(request_text, "leave")
			|| !strcasecmp(request_text, "hangup")) {
		/* These messages are handled asynchronously */
		janus_audiobridge_message *msg = g_malloc(sizeof(janus_audiobridge_message));
		msg->handle = handle;
		msg->transaction = transaction;
		msg->message = root;
		msg->jsep = jsep;

		g_async_queue_push(messages, msg);

		return janus_plugin_result_new(JANUS_PLUGIN_OK_WAIT, NULL, NULL);
	} else {
		JANUS_LOG(LOG_VERB, "Unknown request '%s'\n", request_text);
		error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_REQUEST;
		g_snprintf(error_cause, 512, "Unknown request '%s'", request_text);
	}

plugin_response:
		{
			if(error_code == 0 && !response) {
				error_code = JANUS_AUDIOBRIDGE_ERROR_UNKNOWN_ERROR;
				g_snprintf(error_cause, 512, "Invalid response");
			}
			if(error_code != 0) {
				/* Prepare JSON error event */
				json_t *event = json_object();
				json_object_set_new(event, "audiobridge", json_string("event"));
				json_object_set_new(event, "error_code", json_integer(error_code));
				json_object_set_new(event, "error", json_string(error_cause));
				response = event;
			}
			if(root != NULL)
				json_decref(root);
			if(jsep != NULL)
				json_decref(jsep);
			g_free(transaction);

			if(session != NULL)
				janus_refcount_decrease(&session->ref);
			return janus_plugin_result_new(JANUS_PLUGIN_OK, NULL, response);
		}

}

json_t *janus_audiobridge_handle_admin_message(json_t *message) {
	/* Some requests (e.g., 'create' and 'destroy') can be handled via Admin API */
	int error_code = 0;
	char error_cause[512];
	json_t *response = NULL;

	JANUS_VALIDATE_JSON_OBJECT(message, request_parameters,
		error_code, error_cause, TRUE,
		JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
	if(error_code != 0)
		goto admin_response;
	json_t *request = json_object_get(message, "request");
	const char *request_text = json_string_value(request);
	if((response = janus_audiobridge_process_synchronous_request(NULL, message)) != NULL) {
		/* We got a response, send it back */
		goto admin_response;
	} else {
		JANUS_LOG(LOG_VERB, "Unknown request '%s'\n", request_text);
		error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_REQUEST;
		g_snprintf(error_cause, 512, "Unknown request '%s'", request_text);
	}

admin_response:
		{
			if(!response) {
				/* Prepare JSON error event */
				response = json_object();
				json_object_set_new(response, "audiobridge", json_string("event"));
				json_object_set_new(response, "error_code", json_integer(error_code));
				json_object_set_new(response, "error", json_string(error_cause));
			}
			return response;
		}

}

void janus_audiobridge_setup_media(janus_plugin_session *handle) {
	JANUS_LOG(LOG_INFO, "[%s-%p] WebRTC media is now available\n", JANUS_AUDIOBRIDGE_PACKAGE, handle);
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return;
	janus_mutex_lock(&sessions_mutex);
	janus_audiobridge_session *session = janus_audiobridge_lookup_session(handle);
	if(!session) {
		janus_mutex_unlock(&sessions_mutex);
		JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
		return;
	}
	if(g_atomic_int_get(&session->destroyed)) {
		janus_mutex_unlock(&sessions_mutex);
		return;
	}
	janus_audiobridge_participant *participant = (janus_audiobridge_participant *)session->participant;
	if(!participant) {
		janus_mutex_unlock(&sessions_mutex);
		return;
	}
	g_atomic_int_set(&session->hangingup, 0);
	/* FIXME Only send this peer the audio mix when we get this event */
	g_atomic_int_set(&session->started, 1);
	janus_mutex_unlock(&sessions_mutex);
	/* Notify all other participants that there's a new boy in town */
	janus_mutex_lock(&rooms_mutex);
	janus_audiobridge_room *audiobridge = participant->room;
	if(audiobridge == NULL) {
		/* No room..? Shouldn't happen */
		janus_mutex_unlock(&rooms_mutex);
		JANUS_LOG(LOG_WARN, "PeerConnection created, but AudioBridge participant not in a room...\n");
		return;
	}
	janus_mutex_lock(&audiobridge->mutex);
	json_t *list = json_array();
	json_t *pl = json_object();
	json_object_set_new(pl, "id",
		string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
	if(participant->display)
		json_object_set_new(pl, "display", json_string(participant->display));
	json_object_set_new(pl, "setup", json_true());
	json_object_set_new(pl, "muted", participant->muted ? json_true() : json_false());
	if(audiobridge->spatial_audio)
		json_object_set_new(pl, "spatial_position", json_integer(participant->spatial_position));
	json_array_append_new(list, pl);
	json_t *pub = json_object();
	json_object_set_new(pub, "audiobridge", json_string("event"));
	json_object_set_new(pub, "room",
		string_ids ? json_string(participant->room->room_id_str) : json_integer(participant->room->room_id));
	json_object_set_new(pub, "participants", list);
	GHashTableIter iter;
	gpointer value;
	g_hash_table_iter_init(&iter, audiobridge->participants);
	while(g_hash_table_iter_next(&iter, NULL, &value)) {
		janus_audiobridge_participant *p = value;
		if(p == participant) {
			continue;	/* Skip the new participant itself */
		}
		JANUS_LOG(LOG_VERB, "Notifying participant %s (%s)\n", p->user_id_str, p->display ? p->display : "??");
		int ret = gateway->push_event(p->session->handle, &janus_audiobridge_plugin, NULL, pub, NULL);
		JANUS_LOG(LOG_VERB, "  >> %d (%s)\n", ret, janus_get_api_error(ret));
	}
	json_decref(pub);
	g_atomic_int_set(&participant->active, 1);
	janus_mutex_unlock(&audiobridge->mutex);
	janus_mutex_unlock(&rooms_mutex);
}

void janus_audiobridge_incoming_rtp(janus_plugin_session *handle, janus_plugin_rtp *packet) {
	if(handle == NULL || g_atomic_int_get(&handle->stopped) || g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return;
	janus_audiobridge_session *session = (janus_audiobridge_session *)handle->plugin_handle;
	if(!session || g_atomic_int_get(&session->destroyed) || !session->participant)
		return;
	janus_audiobridge_participant *participant = (janus_audiobridge_participant *)session->participant;
	if(!g_atomic_int_get(&participant->active) || participant->muted ||
			(participant->codec == JANUS_AUDIOCODEC_OPUS && !participant->decoder) || !participant->room)
		return;
	if(participant->room && participant->room->muted && !participant->admin)
		return;
	char *buf = packet->buffer;
	uint16_t len = packet->length;
	/* Save the frame if we're recording this leg */
	janus_recorder_save_frame(participant->arc, buf, len);
	if(g_atomic_int_get(&participant->active) && (participant->codec != JANUS_AUDIOCODEC_OPUS ||
			(participant->codec == JANUS_AUDIOCODEC_OPUS && participant->decoder))) {
		/* First of all, check if a reset on the decoder is due */
		if(participant->reset && participant->codec == JANUS_AUDIOCODEC_OPUS) {
			/* Create a new decoder and get rid of the old one */
			int error = 0;
			OpusDecoder *decoder = opus_decoder_create(participant->room->sampling_rate,
				participant->stereo ? 2 : 1, &error);
			if(error != OPUS_OK) {
				JANUS_LOG(LOG_ERR, "Error resetting Opus decoder...\n");
			} else {
				if(participant->decoder)
					opus_decoder_destroy(participant->decoder);
				participant->decoder = decoder;
				JANUS_LOG(LOG_VERB, "Opus decoder reset\n");
			}
			participant->reset = FALSE;
		}
		/* Decode frame (Opus/G.711 -> slinear) */
		janus_rtp_header *rtp = (janus_rtp_header *)buf;
		if((participant->codec == JANUS_AUDIOCODEC_PCMA && rtp->type != 8) ||
				(participant->codec == JANUS_AUDIOCODEC_PCMU && rtp->type != 0)) {
			JANUS_LOG(LOG_WARN, "Wrong payload type (%d != %d), skipping audio packet\n",
				rtp->type, participant->codec == JANUS_AUDIOCODEC_PCMA ? 8 : 0);
			return;
		}
		janus_audiobridge_rtp_relay_packet *pkt = g_malloc(sizeof(janus_audiobridge_rtp_relay_packet));
		pkt->data = g_malloc0(BUFFER_SAMPLES*sizeof(opus_int16));
		pkt->ssrc = 0;
		pkt->timestamp = ntohl(rtp->timestamp);
		pkt->seq_number = ntohs(rtp->seq_number);
		/* We might check the audio level extension to see if this is silence */
		pkt->silence = FALSE;
		pkt->length = 0;

		/* First check if probation period */
		if(participant->probation == MIN_SEQUENTIAL) {
			participant->probation--;
			participant->expected_seq = pkt->seq_number + 1;
			JANUS_LOG(LOG_VERB, "Probation started with ssrc = %"SCNu32", seq = %"SCNu16" \n", ntohl(rtp->ssrc), pkt->seq_number);
			g_free(pkt->data);
			g_free(pkt);
			return;
		} else if(participant->probation != 0) {
			/* Decrease probation */
			participant->probation--;
			/* TODO: Reset probation if sequence number is incorrect and DSSRC also; must have a correct sequence */
			if(!participant->probation){
				/* Probation is ended */
				JANUS_LOG(LOG_VERB, "Probation ended with ssrc = %"SCNu32", seq = %"SCNu16" \n", ntohl(rtp->ssrc), pkt->seq_number);
			}
			participant->expected_seq = pkt->seq_number + 1;
			g_free(pkt->data);
			g_free(pkt);
			return;
		}

		if(participant->extmap_id > 0) {
			/* Check the audio levels, in case we need to notify participants about who's talking */
			int level = packet->extensions.audio_level;
			if(level != -1) {
				/* Is this silence? */
				pkt->silence = (level == 127);
				if(participant->room && participant->room->audiolevel_event) {
					/* We also need to detect who's talking: update our monitoring stuff */
					int audio_active_packets = participant->room ? participant->room->audio_active_packets : 100;
					int audio_level_average = participant->room ? participant->room->audio_level_average : 25;
					/* Check if we need to override those with user specific properties */
					if(participant->user_audio_active_packets > 0)
						audio_active_packets = participant->user_audio_active_packets;
					if(participant->user_audio_level_average > 0)
						audio_level_average = participant->user_audio_level_average;
					participant->audio_dBov_sum += level;
					participant->audio_active_packets++;
					participant->dBov_level = level;
					if(participant->audio_active_packets > 0 && participant->audio_active_packets == audio_active_packets) {
						gboolean notify_talk_event = FALSE;
						if((float) participant->audio_dBov_sum / (float) participant->audio_active_packets < audio_level_average) {
							/* Participant talking, should we notify all participants? */
							if(!participant->talking)
								notify_talk_event = TRUE;
							participant->talking = TRUE;
						} else {
							/* Participant not talking anymore, should we notify all participants? */
							if(participant->talking)
								notify_talk_event = TRUE;
							participant->talking = FALSE;
						}
						participant->audio_active_packets = 0;
						participant->audio_dBov_sum = 0;
						/* Only notify in case of state changes */
						if(participant->room && notify_talk_event) {
							janus_mutex_lock(&participant->room->mutex);
							json_t *event = json_object();
							json_object_set_new(event, "audiobridge", json_string(participant->talking ? "talking" : "stopped-talking"));
							json_object_set_new(event, "room",
								string_ids ? json_string(participant->room ? participant->room->room_id_str : NULL) :
									json_integer(participant->room ? participant->room->room_id : 0));
							json_object_set_new(event, "id",
								string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
							/* Notify the speaker this event is related to as well */
							janus_audiobridge_notify_participants(participant, event, TRUE);
							json_decref(event);
							janus_mutex_unlock(&participant->room->mutex);
							/* Also notify event handlers */
							if(notify_events && gateway->events_is_enabled()) {
								json_t *info = json_object();
								json_object_set_new(info, "audiobridge", json_string(participant->talking ? "talking" : "stopped-talking"));
								json_object_set_new(info, "room",
									string_ids ? json_string(participant->room ? participant->room->room_id_str : NULL) :
										json_integer(participant->room ? participant->room->room_id : 0));
								json_object_set_new(info, "id",
									string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
								gateway->notify_event(&janus_audiobridge_plugin, session->handle, info);
							}
						}
					}
				}
			}
		}
		if(!g_atomic_int_compare_and_exchange(&participant->decoding, 0, 1)) {
			/* This means we're cleaning up, so don't try to decode */
			g_free(pkt->data);
			g_free(pkt);
			return;
		}
		int plen = 0;
		const unsigned char *payload = (const unsigned char *)janus_rtp_payload(buf, len, &plen);
		if(!payload) {
			g_atomic_int_set(&participant->decoding, 0);
			JANUS_LOG(LOG_ERR, "[%s] Ops! got an error accessing the RTP payload\n",
				participant->codec == JANUS_AUDIOCODEC_OPUS ? "Opus" : "G.711");
			g_free(pkt->data);
			g_free(pkt);
			return;
		}
		/* Check sequence number received, verify if it's relevant to the expected one */
		if(pkt->seq_number == participant->expected_seq) {
			/* Regular decode */
			if(participant->codec == JANUS_AUDIOCODEC_OPUS) {
				/* Opus */
				pkt->length = opus_decode(participant->decoder, payload, plen, (opus_int16 *)pkt->data, BUFFER_SAMPLES, 0);
			} else if(participant->codec == JANUS_AUDIOCODEC_PCMA || participant->codec == JANUS_AUDIOCODEC_PCMU) {
				/* G.711 */
				if(plen != 160) {
					g_atomic_int_set(&participant->decoding, 0);
					JANUS_LOG(LOG_WARN, "[G.711] Wrong packet size (expected 160, got %d), skipping audio packet\n", plen);
					g_free(pkt->data);
					g_free(pkt);
					return;
				}
				int i = 0;
				uint16_t *samples = (uint16_t *)pkt->data;
				if(rtp->type == 0) {
					/* mu-law */
					for(i=0; i<plen; i++)
						*(samples+i) = janus_audiobridge_g711_ulaw_dectable[*(payload+i)];
				} else if(rtp->type == 8) {
					/* a-law */
					for(i=0; i<plen; i++)
						*(samples+i) = janus_audiobridge_g711_alaw_dectable[*(payload+i)];
				}
				pkt->length = 320;
			}
			/* Update last_timestamp */
			participant->last_timestamp = pkt->timestamp;
			/* Increment according to previous seq_number */
			participant->expected_seq = pkt->seq_number + 1;
		} else if(pkt->seq_number > participant->expected_seq) {
			/* Sequence(s) losts */
			uint16_t gap = pkt->seq_number - participant->expected_seq;
			JANUS_LOG(LOG_HUGE, "%"SCNu16" sequence(s) lost, sequence = %"SCNu16", expected seq = %"SCNu16"\n",
				gap, pkt->seq_number, participant->expected_seq);

			/* Use FEC if sequence lost < DEFAULT_PREBUFFERING (or any custom value) */
			uint16_t start_lost_seq = participant->expected_seq;
			if(participant->codec == JANUS_AUDIOCODEC_OPUS && participant->fec && gap < participant->prebuffer_count) {
				uint8_t i=0;
				for(i=1; i<=gap ; i++) {
					int32_t output_samples;
					janus_audiobridge_rtp_relay_packet *lost_pkt = g_malloc(sizeof(janus_audiobridge_rtp_relay_packet));
					lost_pkt->data = g_malloc0(BUFFER_SAMPLES*sizeof(opus_int16));
					lost_pkt->ssrc = 0;
					lost_pkt->timestamp = participant->last_timestamp + (i * OPUS_SAMPLES);
					lost_pkt->seq_number = start_lost_seq++;
					lost_pkt->silence = FALSE;
					lost_pkt->length = 0;
					if(i == gap) {
						/* Attempt to decode with in-band FEC from next packet */
						opus_decoder_ctl(participant->decoder, OPUS_GET_LAST_PACKET_DURATION(&output_samples));
						lost_pkt->length = opus_decode(participant->decoder, payload, plen, (opus_int16 *)lost_pkt->data, output_samples, 1);
					} else {
						opus_decoder_ctl(participant->decoder, OPUS_GET_LAST_PACKET_DURATION(&output_samples));
						lost_pkt->length = opus_decode(participant->decoder, NULL, plen, (opus_int16 *)lost_pkt->data, output_samples, 1);
					}
					if(lost_pkt->length < 0) {
						g_atomic_int_set(&participant->decoding, 0);
						JANUS_LOG(LOG_ERR, "[Opus] Ops! got an error decoding the Opus frame: %d (%s)\n", lost_pkt->length, opus_strerror(lost_pkt->length));
						g_free(lost_pkt->data);
						g_free(lost_pkt);
						g_free(pkt->data);
						g_free(pkt);
						return;
					}
					/* Enqueue the decoded frame */
					janus_mutex_lock(&participant->qmutex);
					/* Insert packets sorting by sequence number */
					participant->inbuf = g_list_insert_sorted(participant->inbuf, lost_pkt, &janus_audiobridge_rtp_sort);
					janus_mutex_unlock(&participant->qmutex);
				}
			}
			/* Then go with the regular decode (no FEC) */
			if(participant->codec == JANUS_AUDIOCODEC_OPUS) {
				/* Opus */
				pkt->length = opus_decode(participant->decoder, payload, plen, (opus_int16 *)pkt->data, BUFFER_SAMPLES, 0);
			} else if(participant->codec == JANUS_AUDIOCODEC_PCMA || participant->codec == JANUS_AUDIOCODEC_PCMU) {
				/* G.711 */
				if(plen != 160) {
					g_atomic_int_set(&participant->decoding, 0);
					JANUS_LOG(LOG_WARN, "[G.711] Wrong packet size (expected 160, got %d), skipping audio packet\n", plen);
					g_free(pkt->data);
					g_free(pkt);
					return;
				}
				int i = 0;
				uint16_t *samples = (uint16_t *)pkt->data;
				if(rtp->type == 0) {
					/* mu-law */
					for(i=0; i<plen; i++)
						*(samples+i) = janus_audiobridge_g711_ulaw_dectable[*(payload+i)];
				} else if(rtp->type == 8) {
					/* a-law */
					for(i=0; i<plen; i++)
						*(samples+i) = janus_audiobridge_g711_alaw_dectable[*(payload+i)];
				}
				pkt->length = 320;
			}
			/* Increment according to previous seq_number */
			participant->expected_seq = pkt->seq_number + 1;
		} else {
			/* In late sequence or sequence wrapped */
			g_atomic_int_set(&participant->decoding, 0);
			if((participant->expected_seq - pkt->seq_number) > MAX_MISORDER){
				JANUS_LOG(LOG_HUGE, "SN WRAPPED seq =  %"SCNu16", expected_seq = %"SCNu16"\n", pkt->seq_number, participant->expected_seq);
				participant->expected_seq = pkt->seq_number + 1;
			} else {
				JANUS_LOG(LOG_WARN, "IN LATE SN seq =  %"SCNu16", expected_seq = %"SCNu16"\n", pkt->seq_number, participant->expected_seq);
			}
			g_free(pkt->data);
			g_free(pkt);
			return;
		}
		g_atomic_int_set(&participant->decoding, 0);
		if(pkt->length < 0) {
			if(participant->codec == JANUS_AUDIOCODEC_OPUS) {
				JANUS_LOG(LOG_ERR, "[Opus] Ops! got an error decoding the Opus frame: %d (%s)\n", pkt->length, opus_strerror(pkt->length));
			} else {
				JANUS_LOG(LOG_ERR, "[G.711] Ops! got an error decoding the audio frame\n");
			}
			g_free(pkt->data);
			g_free(pkt);
			return;
		}
		/* Enqueue the decoded frame */
		janus_mutex_lock(&participant->qmutex);
		/* Insert packets sorting by sequence number */
		participant->inbuf = g_list_insert_sorted(participant->inbuf, pkt, &janus_audiobridge_rtp_sort);
		if(participant->prebuffering) {
			/* Still pre-buffering: do we have enough packets now? */
			if(g_list_length(participant->inbuf) > participant->prebuffer_count) {
				participant->prebuffering = FALSE;
				JANUS_LOG(LOG_VERB, "Prebuffering done! Finally adding the user to the mix\n");
			} else {
				JANUS_LOG(LOG_VERB, "Still prebuffering (got %d packets), not adding the user to the mix yet\n", g_list_length(participant->inbuf));
			}
		} else {
			/* Make sure we're not queueing too many packets: if so, get rid of the older ones */
			if(g_list_length(participant->inbuf) >= participant->prebuffer_count*2) {
				gint64 now = janus_get_monotonic_time();
				if(now - participant->last_drop > 5*G_USEC_PER_SEC) {
					JANUS_LOG(LOG_VERB, "Too many packets in queue (%d > %d), removing older ones\n",
						g_list_length(participant->inbuf), participant->prebuffer_count*2);
					participant->last_drop = now;
				}
				while(g_list_length(participant->inbuf) > participant->prebuffer_count) {
					/* Remove this packet: it's too old */
					GList *first = g_list_first(participant->inbuf);
					janus_audiobridge_rtp_relay_packet *pkt = (janus_audiobridge_rtp_relay_packet *)first->data;
					JANUS_LOG(LOG_VERB, "List length = %d, Remove sequence = %d\n",
						g_list_length(participant->inbuf), pkt->seq_number);
					participant->inbuf = g_list_delete_link(participant->inbuf, first);
					first = NULL;
					if(pkt == NULL)
						continue;
					g_free(pkt->data);
					pkt->data = NULL;
					g_free(pkt);
					pkt = NULL;
				}
			}
		}
		janus_mutex_unlock(&participant->qmutex);
	}
}

void janus_audiobridge_incoming_rtcp(janus_plugin_session *handle, janus_plugin_rtcp *packet) {
	if(handle == NULL || g_atomic_int_get(&handle->stopped) || g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return;
	/* FIXME Should we care? */
}

static void janus_audiobridge_recorder_create(janus_audiobridge_participant *participant) {
	if(participant == NULL || participant->room == NULL)
		return;
	janus_audiobridge_room *audiobridge = participant->room;
	char filename[255];
	janus_recorder *rc = NULL;
	gint64 now = janus_get_real_time();
	if(participant->arc == NULL) {
		memset(filename, 0, 255);
		if(participant->mjr_base) {
			/* Use the filename and path we have been provided */
			g_snprintf(filename, 255, "%s-audio", participant->mjr_base);
			rc = janus_recorder_create(audiobridge->mjrs_dir,
				janus_audiocodec_name(participant->codec), filename);
			if(rc == NULL) {
				JANUS_LOG(LOG_ERR, "Couldn't open an audio recording file for this participant!\n");
			}
		} else {
			/* Build a filename */
			g_snprintf(filename, 255, "audiobridge-%s-user-%s-%"SCNi64"-audio",
				audiobridge->room_id_str, participant->user_id_str, now);
			rc = janus_recorder_create(audiobridge->mjrs_dir,
				janus_audiocodec_name(participant->codec), filename);
			if(rc == NULL) {
				JANUS_LOG(LOG_ERR, "Couldn't open an audio recording file for this participant!\n");
			}
		}
		if(participant->extmap_id > 0)
			janus_recorder_add_extmap(rc, participant->extmap_id, JANUS_RTP_EXTMAP_AUDIO_LEVEL);
		participant->arc = rc;
	}
}

static void janus_audiobridge_recorder_close(janus_audiobridge_participant *participant) {
	if(participant->arc) {
		janus_recorder *rc = participant->arc;
		participant->arc = NULL;
		janus_recorder_close(rc);
		JANUS_LOG(LOG_INFO, "Closed user's audio recording %s\n", rc->filename ? rc->filename : "??");
		janus_recorder_destroy(rc);
	}
}

void janus_audiobridge_hangup_media(janus_plugin_session *handle) {
	JANUS_LOG(LOG_INFO, "[%s-%p] No WebRTC media anymore\n", JANUS_AUDIOBRIDGE_PACKAGE, handle);
	janus_mutex_lock(&sessions_mutex);
	janus_audiobridge_hangup_media_internal(handle);
	janus_mutex_unlock(&sessions_mutex);
}

static void janus_audiobridge_hangup_media_internal(janus_plugin_session *handle) {
	JANUS_LOG(LOG_INFO, "No WebRTC media anymore\n");
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
		return;
	janus_audiobridge_session *session = janus_audiobridge_lookup_session(handle);
	if(!session) {
		JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
		return;
	}
	g_atomic_int_set(&session->started, 0);
	if(session->participant == NULL)
		return;
	if(!g_atomic_int_compare_and_exchange(&session->hangingup, 0, 1))
		return;
	/* Get rid of participant */
	janus_audiobridge_participant *participant = (janus_audiobridge_participant *)session->participant;
	/* If this was a plain RTP participant, notify the thread that it's time to go */
	if(participant->plainrtp_media.pipefd[1] > 0) {
		int code = 1;
		ssize_t res = 0;
		do {
			res = write(participant->plainrtp_media.pipefd[1], &code, sizeof(int));
		} while(res == -1 && errno == EINTR);
	}
	janus_mutex_lock(&rooms_mutex);
	janus_audiobridge_room *audiobridge = participant->room;
	gboolean removed = FALSE;
	if(audiobridge != NULL) {
		janus_mutex_lock(&audiobridge->mutex);
		participant->room = NULL;
		json_t *event = json_object();
		json_object_set_new(event, "audiobridge", json_string("event"));
		json_object_set_new(event, "room",
			string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
		json_object_set_new(event, "leaving",
			string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
		removed = g_hash_table_remove(audiobridge->participants,
			string_ids ? (gpointer)participant->user_id_str : (gpointer)&participant->user_id);
		GHashTableIter iter;
		gpointer value;
		g_hash_table_iter_init(&iter, audiobridge->participants);
		while(g_hash_table_iter_next(&iter, NULL, &value)) {
			janus_audiobridge_participant *p = value;
			if(p == participant) {
				continue;	/* Skip the leaving participant itself */
			}
			JANUS_LOG(LOG_VERB, "Notifying participant %s (%s)\n", p->user_id_str, p->display ? p->display : "??");
			int ret = gateway->push_event(p->session->handle, &janus_audiobridge_plugin, NULL, event, NULL);
			JANUS_LOG(LOG_VERB, "  >> %d (%s)\n", ret, janus_get_api_error(ret));
		}
		json_decref(event);
		/* Also notify event handlers */
		if(notify_events && gateway->events_is_enabled()) {
			json_t *info = json_object();
			json_object_set_new(info, "event", json_string("left"));
			json_object_set_new(info, "room",
				string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
			json_object_set_new(info, "id",
				string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
			json_object_set_new(info, "display", json_string(participant->display));
			gateway->notify_event(&janus_audiobridge_plugin, NULL, info);
		}
	}
	/* Get rid of the recorders, if available */
	janus_mutex_lock(&participant->rec_mutex);
	janus_audiobridge_recorder_close(participant);
	participant->mjr_active = FALSE;
	janus_mutex_unlock(&participant->rec_mutex);
	/* Free the participant resources */
	janus_mutex_lock(&participant->qmutex);
	g_atomic_int_set(&participant->active, 0);
	participant->muted = TRUE;
	g_free(participant->display);
	participant->display = NULL;
	participant->prebuffering = TRUE;
	/* Make sure we're not using the encoder/decoder right now, we're going to destroy them */
	while(!g_atomic_int_compare_and_exchange(&participant->encoding, 0, 1))
		g_usleep(5000);
	if(participant->encoder)
		opus_encoder_destroy(participant->encoder);
	participant->encoder = NULL;
	g_atomic_int_set(&participant->encoding, 0);
	while(!g_atomic_int_compare_and_exchange(&participant->decoding, 0, 1))
		g_usleep(5000);
	if(participant->decoder)
		opus_decoder_destroy(participant->decoder);
	participant->decoder = NULL;
	g_atomic_int_set(&participant->decoding, 0);
	participant->reset = FALSE;
	participant->audio_active_packets = 0;
	participant->audio_dBov_sum = 0;
	participant->talking = FALSE;
	g_free(participant->mjr_base);
	participant->mjr_base = NULL;
	/* Get rid of queued packets */
	while(participant->inbuf) {
		GList *first = g_list_first(participant->inbuf);
		janus_audiobridge_rtp_relay_packet *pkt = (janus_audiobridge_rtp_relay_packet *)first->data;
		participant->inbuf = g_list_delete_link(participant->inbuf, first);
		first = NULL;
		if(pkt == NULL)
			continue;
		g_free(pkt->data);
		pkt->data = NULL;
		g_free(pkt);
		pkt = NULL;
	}
	participant->last_drop = 0;
	janus_mutex_unlock(&participant->qmutex);
	if(audiobridge != NULL) {
		janus_mutex_unlock(&audiobridge->mutex);
		if(removed) {
			janus_refcount_decrease(&audiobridge->ref);
		}
	}
	janus_mutex_unlock(&rooms_mutex);
	session->plugin_offer = FALSE;
	g_atomic_int_set(&session->hangingup, 0);
}

/* Thread to handle incoming messages */
static void *janus_audiobridge_handler(void *data) {
	JANUS_LOG(LOG_VERB, "Joining AudioBridge handler thread\n");
	janus_audiobridge_message *msg = NULL;
	int error_code = 0;
	char error_cause[512];
	json_t *root = NULL;
	while(g_atomic_int_get(&initialized) && !g_atomic_int_get(&stopping)) {
		msg = g_async_queue_pop(messages);
		if(msg == &exit_message)
			break;
		if(msg->handle == NULL) {
			janus_audiobridge_message_free(msg);
			continue;
		}
		janus_mutex_lock(&sessions_mutex);
		janus_audiobridge_session *session = janus_audiobridge_lookup_session(msg->handle);
		if(!session) {
			janus_mutex_unlock(&sessions_mutex);
			JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
			janus_audiobridge_message_free(msg);
			continue;
		}
		if(g_atomic_int_get(&session->destroyed)) {
			janus_mutex_unlock(&sessions_mutex);
			janus_audiobridge_message_free(msg);
			continue;
		}
		janus_mutex_unlock(&sessions_mutex);
		/* Handle request */
		error_code = 0;
		root = NULL;
		if(msg->message == NULL) {
			JANUS_LOG(LOG_ERR, "No message??\n");
			error_code = JANUS_AUDIOBRIDGE_ERROR_NO_MESSAGE;
			g_snprintf(error_cause, 512, "%s", "No message??");
			goto error;
		}
		root = msg->message;
		/* Get the request first */
		JANUS_VALIDATE_JSON_OBJECT(root, request_parameters,
			error_code, error_cause, TRUE,
			JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
		if(error_code != 0)
			goto error;
		json_t *request = json_object_get(root, "request");
		const char *request_text = json_string_value(request);
		json_t *event = NULL;
		gboolean sdp_update = FALSE;
		if(json_object_get(msg->jsep, "update") != NULL)
			sdp_update = json_is_true(json_object_get(msg->jsep, "update"));
		gboolean got_offer = FALSE, got_answer = FALSE, generate_offer = FALSE;
		const char *msg_sdp_type = json_string_value(json_object_get(msg->jsep, "type"));
		const char *msg_sdp = json_string_value(json_object_get(msg->jsep, "sdp"));
		if(msg_sdp_type != NULL) {
			got_offer = !strcasecmp(msg_sdp_type, "offer");
			got_answer = !strcasecmp(msg_sdp_type, "answer");
			if(!got_offer && !got_answer) {
				JANUS_LOG(LOG_ERR, "Unsupported SDP type '%s'\n", msg_sdp_type);
				error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_SDP;
				g_snprintf(error_cause, 512, "Unsupported SDP type '%s'\n", msg_sdp_type);
				goto error;
			}
		}
		if(!strcasecmp(request_text, "join")) {
			JANUS_LOG(LOG_VERB, "Configuring new participant\n");
			janus_audiobridge_participant *participant = session->participant;
			if(participant != NULL && participant->room != NULL) {
				JANUS_LOG(LOG_ERR, "Already in a room (use changeroom to join another one)\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_ALREADY_JOINED;
				g_snprintf(error_cause, 512, "Already in a room (use changeroom to join another one)");
				goto error;
			}
			JANUS_VALIDATE_JSON_OBJECT(root, join_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			if(error_code != 0)
				goto error;
			json_t *rtp = json_object_get(root, "rtp");
			if(rtp != NULL) {
				JANUS_VALIDATE_JSON_OBJECT(root, rtp_parameters,
					error_code, error_cause, TRUE,
					JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
				if(error_code != 0)
					goto error;
				if(msg_sdp != NULL) {
					JANUS_LOG(LOG_WARN, "Added plain RTP details but negotiating a WebRTC PeerConnection: plain RTP will be ignored\n");
					rtp = NULL;
					json_object_del(root, "rtp");
				}
			}
			if(!string_ids) {
				JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
					error_code, error_cause, TRUE,
					JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			} else {
				JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
					error_code, error_cause, TRUE,
					JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			}
			if(error_code != 0)
				goto error;
			if(!string_ids) {
				JANUS_VALIDATE_JSON_OBJECT(root, idopt_parameters,
					error_code, error_cause, TRUE,
					JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			} else {
				JANUS_VALIDATE_JSON_OBJECT(root, idstropt_parameters,
					error_code, error_cause, TRUE,
					JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			}
			if(error_code != 0)
				goto error;
			json_t *room = json_object_get(root, "room");
			guint64 room_id = 0;
			char room_id_num[30], *room_id_str = NULL;
			if(!string_ids) {
				room_id = json_integer_value(room);
				g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
				room_id_str = room_id_num;
			} else {
				room_id_str = (char *)json_string_value(room);
			}
			janus_mutex_lock(&rooms_mutex);
			janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
				string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
			if(audiobridge == NULL) {
				janus_mutex_unlock(&rooms_mutex);
				error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
				JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
				g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
				goto error;
			}
			janus_refcount_increase(&audiobridge->ref);
			janus_mutex_lock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
			if(rtp != NULL && !audiobridge->allow_plainrtp) {
				/* Plain RTP participants are not allowed in this room */
				janus_mutex_unlock(&audiobridge->mutex);
				janus_refcount_decrease(&audiobridge->ref);
				error_code = JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED;
				JANUS_LOG(LOG_ERR, "Plain RTP participants not allowed in this room\n");
				g_snprintf(error_cause, 512, "Plain RTP participants not allowed in this room");
				goto error;
			}
			/* A pin may be required for this action */
			JANUS_CHECK_SECRET(audiobridge->room_pin, root, "pin", error_code, error_cause,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
			if(error_code != 0) {
				janus_mutex_unlock(&audiobridge->mutex);
				janus_refcount_decrease(&audiobridge->ref);
				goto error;
			}
			/* A token might be required too */
			if(audiobridge->check_tokens) {
				json_t *token = json_object_get(root, "token");
				const char *token_text = token ? json_string_value(token) : NULL;
				if(token_text == NULL || g_hash_table_lookup(audiobridge->allowed, token_text) == NULL) {
					JANUS_LOG(LOG_ERR, "Unauthorized (not in the allowed list)\n");
					error_code = JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED;
					g_snprintf(error_cause, 512, "Unauthorized (not in the allowed list)");
					janus_mutex_unlock(&audiobridge->mutex);
					janus_refcount_decrease(&audiobridge->ref);
					goto error;
				}
			}
			gboolean admin = FALSE;
			if(json_object_get(root, "secret") != NULL) {
				/* The user is trying to present themselves as an admin */
				JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
					JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
				if(error_code != 0) {
					janus_mutex_unlock(&audiobridge->mutex);
					janus_refcount_decrease(&audiobridge->ref);
					goto error;
				}
				admin = TRUE;
			}
			/* If this room uses groups, make sure a valid group name was provided */
			uint group = 0;
			if(audiobridge->groups != NULL) {
				JANUS_VALIDATE_JSON_OBJECT(root, group_parameters,
					error_code, error_cause, TRUE,
					JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
				if(error_code != 0) {
					janus_mutex_unlock(&audiobridge->mutex);
					janus_refcount_decrease(&audiobridge->ref);
					goto error;
				}
				const char *group_name = json_string_value(json_object_get(root, "group"));
				group = GPOINTER_TO_UINT(g_hash_table_lookup(audiobridge->groups, group_name));
				if(group == 0) {
					janus_mutex_unlock(&audiobridge->mutex);
					janus_refcount_decrease(&audiobridge->ref);
					JANUS_LOG(LOG_ERR, "No such group (%s)\n", group_name);
					error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_GROUP;
					g_snprintf(error_cause, 512, "No such group (%s)", group_name);
					goto error;
				}
			}
			json_t *display = json_object_get(root, "display");
			const char *display_text = display ? json_string_value(display) : NULL;
			json_t *muted = json_object_get(root, "muted");
			json_t *prebuffer = json_object_get(root, "prebuffer");
			json_t *gain = json_object_get(root, "volume");
			json_t *spatial = json_object_get(root, "spatial_position");
			json_t *bitrate = json_object_get(root, "bitrate");
			json_t *quality = json_object_get(root, "quality");
			json_t *exploss = json_object_get(root, "expected_loss");
			json_t *acodec = json_object_get(root, "codec");
			json_t *user_audio_level_average = json_object_get(root, "audio_level_average");
			json_t *user_audio_active_packets = json_object_get(root, "audio_active_packets");
			json_t *record = json_object_get(root, "record");
			json_t *recfile = json_object_get(root, "filename");
			json_t *gen_offer = json_object_get(root, "generate_offer");
			uint prebuffer_count = prebuffer ? json_integer_value(prebuffer) : audiobridge->default_prebuffering;
			if(prebuffer_count > MAX_PREBUFFERING) {
				prebuffer_count = audiobridge->default_prebuffering;
				JANUS_LOG(LOG_WARN, "Invalid prebuffering value provided (too high), using room default: %d\n",
					audiobridge->default_prebuffering);
			}
			int volume = gain ? json_integer_value(gain) : 100;
			int spatial_position = spatial ? json_integer_value(spatial) : 50;
			int32_t opus_bitrate = audiobridge->default_bitrate;
			if(bitrate) {
				opus_bitrate = json_integer_value(bitrate);
				if(opus_bitrate < 500 || opus_bitrate > 512000) {
					JANUS_LOG(LOG_WARN, "Invalid bitrate %"SCNi32", falling back to default/auto\n", opus_bitrate);
					opus_bitrate = audiobridge->default_bitrate;
				}
			}
			int complexity = quality ? json_integer_value(quality) : DEFAULT_COMPLEXITY;
			if(complexity < 1 || complexity > 10) {
				janus_mutex_unlock(&audiobridge->mutex);
				janus_refcount_decrease(&audiobridge->ref);
				JANUS_LOG(LOG_ERR, "Invalid element (quality should be a positive integer between 1 and 10)\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
				g_snprintf(error_cause, 512, "Invalid element (quality should be a positive integer between 1 and 10)");
				goto error;
			}
			int expected_loss = exploss ? json_integer_value(exploss) : audiobridge->default_expectedloss;
			if(expected_loss > 20) {
				janus_mutex_unlock(&audiobridge->mutex);
				janus_refcount_decrease(&audiobridge->ref);
				JANUS_LOG(LOG_ERR, "Invalid element (expected_loss should be a positive integer between 0 and 20)\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
				g_snprintf(error_cause, 512, "Invalid element (expected_loss should be a positive integer between 0 and 20)");
				goto error;
			}
			janus_audiocodec codec = JANUS_AUDIOCODEC_OPUS;
			if(acodec != NULL) {
				codec = janus_audiocodec_from_name(json_string_value(acodec));
				if(codec != JANUS_AUDIOCODEC_OPUS && codec != JANUS_AUDIOCODEC_PCMA && codec != JANUS_AUDIOCODEC_PCMU) {
					janus_mutex_unlock(&audiobridge->mutex);
					janus_refcount_decrease(&audiobridge->ref);
					JANUS_LOG(LOG_ERR, "Invalid element (codec must opus, pcmu or pcma)\n");
					error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
					g_snprintf(error_cause, 512, "Invalid element (codec must opus, pcmu or pcma)");
					goto error;
				}
			}
			guint64 user_id = 0;
			char user_id_num[30], *user_id_str = NULL;
			gboolean user_id_allocated = FALSE;
			json_t *id = json_object_get(root, "id");
			if(id) {
				if(!string_ids) {
					user_id = json_integer_value(id);
					g_snprintf(user_id_num, sizeof(user_id_num), "%"SCNu64, user_id);
					user_id_str = user_id_num;
				} else {
					user_id_str = (char *)json_string_value(id);
				}
				if(g_hash_table_lookup(audiobridge->participants,
						string_ids ? (gpointer)user_id_str : (gpointer)&user_id) != NULL) {
					/* User ID already taken */
					janus_mutex_unlock(&audiobridge->mutex);
					janus_refcount_decrease(&audiobridge->ref);
					error_code = JANUS_AUDIOBRIDGE_ERROR_ID_EXISTS;
					JANUS_LOG(LOG_ERR, "User ID %s already exists\n", user_id_str);
					g_snprintf(error_cause, 512, "User ID %s already exists", user_id_str);
					goto error;
				}
			}
			if(!string_ids) {
				if(user_id == 0) {
					/* Generate a random ID */
					while(user_id == 0) {
						user_id = janus_random_uint64();
						if(g_hash_table_lookup(audiobridge->participants, &user_id) != NULL) {
							/* User ID already taken, try another one */
							user_id = 0;
						}
					}
					g_snprintf(user_id_num, sizeof(user_id_num), "%"SCNu64, user_id);
					user_id_str = user_id_num;
				}
				JANUS_LOG(LOG_VERB, "  -- Participant ID: %"SCNu64"\n", user_id);
			} else {
				if(user_id_str == NULL) {
					/* Generate a random ID */
					while(user_id_str == NULL) {
						user_id_str = janus_random_uuid();
						if(g_hash_table_lookup(audiobridge->participants, user_id_str) != NULL) {
							/* User ID already taken, try another one */
							g_clear_pointer(&user_id_str, g_free);
						}
					}
					user_id_allocated = TRUE;
				}
				JANUS_LOG(LOG_VERB, "  -- Participant ID: %s\n", user_id_str);
			}
			if(participant == NULL) {
				participant = g_malloc0(sizeof(janus_audiobridge_participant));
				janus_refcount_init(&participant->ref, janus_audiobridge_participant_free);
				g_atomic_int_set(&participant->active, 0);
				participant->codec = codec;
				participant->prebuffering = TRUE;
				participant->display = NULL;
				participant->inbuf = NULL;
				participant->outbuf = NULL;
				participant->last_drop = 0;
				participant->encoder = NULL;
				participant->decoder = NULL;
				participant->reset = FALSE;
				participant->fec = FALSE;
				participant->expected_seq = 0;
				participant->probation = 0;
				participant->last_timestamp = 0;
				janus_mutex_init(&participant->qmutex);
				participant->arc = NULL;
				janus_audiobridge_plainrtp_media_cleanup(&participant->plainrtp_media);
				janus_mutex_init(&participant->pmutex);
				janus_mutex_init(&participant->rec_mutex);
			}
			participant->session = session;
			participant->room = audiobridge;
			participant->user_id = user_id;
			participant->user_id_str = user_id_str ? g_strdup(user_id_str) : NULL;
			participant->group = group;
			g_free(participant->display);
			participant->admin = admin;
			participant->display = display_text ? g_strdup(display_text) : NULL;
			participant->muted = muted ? json_is_true(muted) : FALSE;	/* By default, everyone's unmuted when joining */
			participant->prebuffer_count = prebuffer_count;
			participant->volume_gain = volume;
			participant->opus_complexity = complexity;
			participant->opus_bitrate = opus_bitrate;
			participant->expected_loss = expected_loss;
			participant->stereo = audiobridge->spatial_audio;
			if(participant->stereo) {
				if(spatial_position > 100)
					spatial_position = 100;
				participant->spatial_position = spatial_position;
			}
			participant->user_audio_active_packets = json_integer_value(user_audio_active_packets);
			participant->user_audio_level_average = json_integer_value(user_audio_level_average);
			if(participant->outbuf == NULL)
				participant->outbuf = g_async_queue_new();
			g_atomic_int_set(&participant->active, g_atomic_int_get(&session->started));
			if(!g_atomic_int_get(&session->started)) {
				/* Initialize the RTP context only if we're renegotiating */
				janus_rtp_switching_context_reset(&participant->context);
				participant->opus_pt = 0;
				participant->extmap_id = 0;
				participant->dBov_level = 0;
				participant->talking = FALSE;
			}
			JANUS_LOG(LOG_VERB, "Creating Opus encoder/decoder (sampling rate %d)\n", audiobridge->sampling_rate);
			/* Opus encoder */
			int error = 0;
			if(participant->encoder == NULL) {
				participant->encoder = opus_encoder_create(audiobridge->sampling_rate,
					audiobridge->spatial_audio ? 2 : 1, OPUS_APPLICATION_VOIP, &error);
				if(error != OPUS_OK) {
					if(user_id_allocated) {
						g_free(user_id_str);
						g_free(participant->user_id_str);
					}
					janus_mutex_unlock(&audiobridge->mutex);
					janus_refcount_decrease(&audiobridge->ref);
					g_free(participant->display);
					g_free(participant);
					JANUS_LOG(LOG_ERR, "Error creating Opus encoder\n");
					error_code = JANUS_AUDIOBRIDGE_ERROR_LIBOPUS_ERROR;
					g_snprintf(error_cause, 512, "Error creating Opus encoder");
					goto error;
				}
				if(audiobridge->sampling_rate == 8000) {
					opus_encoder_ctl(participant->encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_NARROWBAND));
				} else if(audiobridge->sampling_rate == 12000) {
					opus_encoder_ctl(participant->encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_MEDIUMBAND));
				} else if(audiobridge->sampling_rate == 16000) {
					opus_encoder_ctl(participant->encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
				} else if(audiobridge->sampling_rate == 24000) {
					opus_encoder_ctl(participant->encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_SUPERWIDEBAND));
				} else if(audiobridge->sampling_rate == 48000) {
					opus_encoder_ctl(participant->encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
				} else {
					JANUS_LOG(LOG_WARN, "Unsupported sampling rate %d, setting 16kHz\n", audiobridge->sampling_rate);
					audiobridge->sampling_rate = 16000;
					opus_encoder_ctl(participant->encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
				}
				opus_encoder_ctl(participant->encoder, OPUS_SET_INBAND_FEC(participant->fec));
				opus_encoder_ctl(participant->encoder, OPUS_SET_PACKET_LOSS_PERC(participant->expected_loss));
			}
			opus_encoder_ctl(participant->encoder, OPUS_SET_COMPLEXITY(participant->opus_complexity));
			if(participant->opus_bitrate > 0)
				opus_encoder_ctl(participant->encoder, OPUS_SET_BITRATE(participant->opus_bitrate));
			if(participant->decoder == NULL) {
				/* Opus decoder */
				error = 0;
				participant->decoder = opus_decoder_create(audiobridge->sampling_rate,
					audiobridge->spatial_audio ? 2 : 1, &error);
				if(error != OPUS_OK) {
					if(user_id_allocated) {
						g_free(user_id_str);
						g_free(participant->user_id_str);
					}
					janus_mutex_unlock(&audiobridge->mutex);
					janus_refcount_decrease(&audiobridge->ref);
					g_free(participant->display);
					if(participant->encoder)
						opus_encoder_destroy(participant->encoder);
					participant->encoder = NULL;
					if(participant->decoder)
						opus_decoder_destroy(participant->decoder);
					participant->decoder = NULL;
					g_free(participant);
					JANUS_LOG(LOG_ERR, "Error creating Opus decoder\n");
					error_code = JANUS_AUDIOBRIDGE_ERROR_LIBOPUS_ERROR;
					g_snprintf(error_cause, 512, "Error creating Opus decoder");
					goto error;
				}
			}
			participant->reset = FALSE;
			/* If this is a plain RTP participant, create the socket */
			if(rtp != NULL) {
				gen_offer = NULL;
				const char *ip = json_string_value(json_object_get(rtp, "ip"));
				uint16_t port = json_integer_value(json_object_get(rtp, "port"));
				if(participant->codec == JANUS_AUDIOCODEC_OPUS) {
					int pt = json_integer_value(json_object_get(rtp, "payload_type"));
					if(pt == 0)
						pt = 100;
					participant->opus_pt = pt;
				}
				int audiolevel_ext_id = json_integer_value(json_object_get(rtp, "audiolevel_ext"));
				if(audiolevel_ext_id > 0)
					participant->extmap_id = audiolevel_ext_id;
				gboolean fec = json_is_true(json_object_get(rtp, "fec"));
				if(participant->codec == JANUS_AUDIOCODEC_OPUS && fec) {
					participant->fec = TRUE;
					opus_encoder_ctl(participant->encoder, OPUS_SET_INBAND_FEC(participant->fec));
					opus_encoder_ctl(participant->encoder, OPUS_SET_PACKET_LOSS_PERC(participant->expected_loss));
				}
				/* Create the socket */
				janus_mutex_lock(&participant->pmutex);
				janus_audiobridge_plainrtp_media_cleanup(&participant->plainrtp_media);
				if(janus_audiobridge_plainrtp_allocate_port(&participant->plainrtp_media) < 0) {
					JANUS_LOG(LOG_ERR, "[AudioBridge-%p] Couldn't bind to local port\n", session);
				} else if(ip != NULL && port > 0) {
					/* Connect the socket, if there's a remote address */
					g_free(participant->plainrtp_media.remote_audio_ip);
					participant->plainrtp_media.remote_audio_ip = g_strdup(ip);
					participant->plainrtp_media.remote_audio_rtp_port = port;
					/* Resolve the address */
					gboolean have_audio_server_ip = FALSE;
					struct sockaddr_storage audio_server_addr = { 0 };
					if(janus_network_resolve_address(participant->plainrtp_media.remote_audio_ip, &audio_server_addr) < 0) {
						JANUS_LOG(LOG_ERR, "[AudioBridge-%p] Couldn't get host '%s'\n", session,
							participant->plainrtp_media.remote_audio_ip);
					} else {
						/* Address resolved */
						have_audio_server_ip = TRUE;
						if(audio_server_addr.ss_family == AF_INET6) {
							struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&audio_server_addr;
							addr6->sin6_port = htons(port);
						} else if(audio_server_addr.ss_family == AF_INET) {
							struct sockaddr_in *addr = (struct sockaddr_in *)&audio_server_addr;
							addr->sin_port = htons(port);
						}
					}
					if(have_audio_server_ip) {
						if(connect(participant->plainrtp_media.audio_rtp_fd, (struct sockaddr *)&audio_server_addr, sizeof(audio_server_addr)) == -1) {
							JANUS_LOG(LOG_ERR, "[AudioBridge-%p] Couldn't connect audio RTP? (%s:%d)\n", session,
								participant->plainrtp_media.remote_audio_ip, participant->plainrtp_media.remote_audio_rtp_port);
							JANUS_LOG(LOG_ERR, "[AudioBridge-%p]   -- %d (%s)\n", session, errno, g_strerror(errno));
						} else {
							participant->plainrtp_media.audio_send = TRUE;
						}
					}
				}
				janus_mutex_unlock(&participant->pmutex);
			}
			/* Check if we need to record this participant right away */
			janus_mutex_lock(&participant->rec_mutex);
			const char *recording_base = json_string_value(recfile);
			if(recording_base) {
				g_free(participant->mjr_base);
				participant->mjr_base = g_strdup(recording_base);
			}
			if(audiobridge->mjrs || record) {
				if(audiobridge->mjrs || json_is_true(record)) {
					/* Start recording (ignore if recording already) */
					if(participant->arc != NULL) {
						JANUS_LOG(LOG_WARN, "Already recording participant's audio (room %s, user %s)\n",
							participant->room->room_id_str, participant->user_id_str);
					} else {
						JANUS_LOG(LOG_INFO, "Starting recording of participant's audio (room %s, user %s)\n",
							participant->room->room_id_str, participant->user_id_str);
						janus_audiobridge_recorder_create(participant);
						participant->mjr_active = TRUE;
					}
				} else {
					/* Stop recording (ignore if not recording) */
					janus_audiobridge_recorder_close(participant);
					participant->mjr_active = FALSE;
				}
			}
			janus_mutex_unlock(&participant->rec_mutex);
			/* Finally, start the encoding thread if it hasn't already */
			if(participant->thread == NULL) {
				GError *error = NULL;
				char roomtrunc[5], parttrunc[5];
				g_snprintf(roomtrunc, sizeof(roomtrunc), "%s", audiobridge->room_id_str);
				g_snprintf(parttrunc, sizeof(parttrunc), "%s", participant->user_id_str);
				char tname[16];
				g_snprintf(tname, sizeof(tname), "mixer %s %s", roomtrunc, parttrunc);
				janus_refcount_increase(&session->ref);
				janus_refcount_increase(&participant->ref);
				participant->thread = g_thread_try_new(tname, &janus_audiobridge_participant_thread, participant, &error);
				if(error != NULL) {
					janus_refcount_decrease(&participant->ref);
					janus_refcount_decrease(&session->ref);
					/* FIXME We should fail here... */
					JANUS_LOG(LOG_ERR, "Got error %d (%s) trying to launch the participant thread...\n",
						error->code, error->message ? error->message : "??");
					g_error_free(error);
				}
			}
			if(participant->plainrtp_media.audio_rtp_fd != -1 && participant->plainrtp_media.thread == NULL) {
				/* Spawn a thread for incoming plain RTP traffic too */
				GError *error = NULL;
				char roomtrunc[5], parttrunc[5];
				g_snprintf(roomtrunc, sizeof(roomtrunc), "%s", audiobridge->room_id_str);
				g_snprintf(parttrunc, sizeof(parttrunc), "%s", participant->user_id_str);
				char tname[16];
				g_snprintf(tname, sizeof(tname), "rtp %s %s", roomtrunc, parttrunc);
				janus_refcount_increase(&session->ref);
				janus_refcount_increase(&participant->ref);
				participant->plainrtp_media.thread = g_thread_try_new(tname, &janus_audiobridge_plainrtp_relay_thread, participant, &error);
				if(error != NULL) {
					janus_refcount_decrease(&participant->ref);
					janus_refcount_decrease(&session->ref);
					/* FIXME We should fail here... */
					JANUS_LOG(LOG_ERR, "Got error %d (%s) trying to launch the plain RTP participant thread...\n",
						error->code, error->message ? error->message : "??");
					g_error_free(error);
				}
			}
			/* If a PeerConnection exists, make sure to update the RTP headers */
			if(g_atomic_int_get(&session->started) == 1)
				participant->context.last_ssrc = 0;

			/* Done */
			session->participant = participant;
			janus_refcount_increase(&participant->ref);
			g_hash_table_insert(audiobridge->participants,
				string_ids ? (gpointer)g_strdup(participant->user_id_str) : (gpointer)janus_uint64_dup(participant->user_id),
				participant);
			/* Notify the other participants */
			json_t *newuser = json_object();
			json_object_set_new(newuser, "audiobridge", json_string("joined"));
			json_object_set_new(newuser, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
			json_t *newuserlist = json_array();
			json_t *pl = json_object();
			json_object_set_new(pl, "id",
				string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
			if(participant->display)
				json_object_set_new(pl, "display", json_string(participant->display));
			/* Clarify we're still waiting for the user to negotiate a PeerConnection */
			json_object_set_new(pl, "setup", json_false());
			json_object_set_new(pl, "muted", participant->muted ? json_true() : json_false());
			if(audiobridge->spatial_audio)
				json_object_set_new(pl, "spatial_position", json_integer(participant->spatial_position));
			json_array_append_new(newuserlist, pl);
			json_object_set_new(newuser, "participants", newuserlist);
			GHashTableIter iter;
			gpointer value;
			g_hash_table_iter_init(&iter, audiobridge->participants);
			while(g_hash_table_iter_next(&iter, NULL, &value)) {
				janus_audiobridge_participant *p = value;
				if(p == participant) {
					continue;
				}
				JANUS_LOG(LOG_VERB, "Notifying participant %s (%s)\n", p->user_id_str, p->display ? p->display : "??");
				int ret = gateway->push_event(p->session->handle, &janus_audiobridge_plugin, NULL, newuser, NULL);
				JANUS_LOG(LOG_VERB, "  >> %d (%s)\n", ret, janus_get_api_error(ret));
			}
			json_decref(newuser);
			/* Return a list of all available participants for the new participant now */
			json_t *list = json_array();
			g_hash_table_iter_init(&iter, audiobridge->participants);
			while(g_hash_table_iter_next(&iter, NULL, &value)) {
				janus_audiobridge_participant *p = value;
				if(p == participant) {
					continue;
				}
				json_t *pl = json_object();
				json_object_set_new(pl, "id", string_ids ? json_string(p->user_id_str) : json_integer(p->user_id));
				if(p->display)
					json_object_set_new(pl, "display", json_string(p->display));
				json_object_set_new(pl, "setup", g_atomic_int_get(&p->session->started) ? json_true() : json_false());
				json_object_set_new(pl, "muted", p->muted ? json_true() : json_false());
				if(p->extmap_id > 0)
					json_object_set_new(pl, "talking", p->talking ? json_true() : json_false());
				if(audiobridge->spatial_audio)
					json_object_set_new(pl, "spatial_position", json_integer(p->spatial_position));
				json_array_append_new(list, pl);
			}
			janus_mutex_unlock(&audiobridge->mutex);
			event = json_object();
			json_object_set_new(event, "audiobridge", json_string("joined"));
			json_object_set_new(event, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
			json_object_set_new(event, "id", string_ids ? json_string(user_id_str) : json_integer(user_id));
			json_object_set_new(event, "participants", list);
			if(participant->plainrtp_media.local_audio_rtp_port > 0) {
				json_t *details = json_object();
				json_object_set_new(details, "ip", json_string(local_ip));
				json_object_set_new(details, "port", json_integer(participant->plainrtp_media.local_audio_rtp_port));
				if(participant->codec == JANUS_AUDIOCODEC_OPUS)
					json_object_set_new(details, "payload_type", json_integer(participant->opus_pt));
				else
					json_object_set_new(details, "payload_type", json_integer(participant->codec == JANUS_AUDIOCODEC_PCMA ? 8 : 0));
				json_object_set_new(event, "rtp", details);
			}
			/* Also notify event handlers */
			if(notify_events && gateway->events_is_enabled()) {
				json_t *info = json_object();
				json_object_set_new(info, "event", json_string("joined"));
				json_object_set_new(info, "room", string_ids ? json_string(room_id_str) : json_integer(room_id));
				json_object_set_new(info, "id", string_ids ? json_string(user_id_str) : json_integer(user_id));
				json_object_set_new(info, "display", json_string(participant->display));
				json_object_set_new(info, "setup", g_atomic_int_get(&participant->session->started) ? json_true() : json_false());
				json_object_set_new(info, "muted", participant->muted ? json_true() : json_false());
				if(participant->stereo)
					json_object_set_new(info, "spatial_position", json_integer(participant->spatial_position));
				gateway->notify_event(&janus_audiobridge_plugin, session->handle, info);
			}
			if(user_id_allocated)
				g_free(user_id_str);
			/* If we need to generate an offer ourselves, do that */
			if(gen_offer != NULL)
				generate_offer = json_is_true(gen_offer);
			if(generate_offer)
				session->plugin_offer = generate_offer;
		} else if(!strcasecmp(request_text, "configure")) {
			/* Handle this participant */
			janus_audiobridge_participant *participant = (janus_audiobridge_participant *)session->participant;
			if(participant == NULL || participant->room == NULL) {
				JANUS_LOG(LOG_ERR, "Can't configure (not in a room)\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_NOT_JOINED;
				g_snprintf(error_cause, 512, "Can't configure (not in a room)");
				goto error;
			}
			/* Configure settings for this participant */
			JANUS_VALIDATE_JSON_OBJECT(root, configure_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			if(error_code != 0)
				goto error;
			json_t *muted = json_object_get(root, "muted");
			json_t *prebuffer = json_object_get(root, "prebuffer");
			json_t *bitrate = json_object_get(root, "bitrate");
			json_t *quality = json_object_get(root, "quality");
			json_t *exploss = json_object_get(root, "expected_loss");
			json_t *gain = json_object_get(root, "volume");
			json_t *spatial = json_object_get(root, "spatial_position");
			json_t *record = json_object_get(root, "record");
			json_t *recfile = json_object_get(root, "filename");
			json_t *display = json_object_get(root, "display");
			json_t *group = json_object_get(root, "group");
			json_t *gen_offer = json_object_get(root, "generate_offer");
			json_t *update = json_object_get(root, "update");
			if(prebuffer) {
				uint prebuffer_count = json_integer_value(prebuffer);
				if(prebuffer_count > MAX_PREBUFFERING) {
					JANUS_LOG(LOG_WARN, "Invalid prebuffering value provided (too high), keeping previous value: %d\n",
						participant->prebuffer_count);
				} else if(prebuffer_count != participant->prebuffer_count) {
					janus_mutex_lock(&participant->qmutex);
					if(prebuffer_count < participant->prebuffer_count) {
						/* We're switching to a shorter prebuffer, trim the incoming buffer */
						while(g_list_length(participant->inbuf) > prebuffer_count) {
							GList *first = g_list_first(participant->inbuf);
							janus_audiobridge_rtp_relay_packet *pkt = (janus_audiobridge_rtp_relay_packet *)first->data;
							participant->inbuf = g_list_delete_link(participant->inbuf, first);
							if(pkt == NULL)
								continue;
							g_free(pkt->data);
							g_free(pkt);
						}
					} else {
						/* Reset the prebuffering state */
						participant->prebuffering = TRUE;
					}
					participant->prebuffer_count = prebuffer_count;
					janus_mutex_unlock(&participant->qmutex);
				}
			}
			if(gain)
				participant->volume_gain = json_integer_value(gain);
			if(bitrate) {
				int32_t opus_bitrate = bitrate ? json_integer_value(bitrate) : 0;
				if(opus_bitrate < 500 || opus_bitrate > 512000) {
					JANUS_LOG(LOG_WARN, "Invalid bitrate %"SCNi32", falling back to auto\n", opus_bitrate);
					opus_bitrate = 0;
				}
				participant->opus_bitrate = opus_bitrate;
				if(participant->encoder)
					opus_encoder_ctl(participant->encoder, OPUS_SET_BITRATE(participant->opus_bitrate ? participant->opus_bitrate : OPUS_AUTO));
			}
			if(quality) {
				int complexity = json_integer_value(quality);
				if(complexity < 1 || complexity > 10) {
					JANUS_LOG(LOG_ERR, "Invalid element (quality should be a positive integer between 1 and 10)\n");
					error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
					g_snprintf(error_cause, 512, "Invalid element (quality should be a positive integer between 1 and 10)");
					goto error;
				}
				participant->opus_complexity = complexity;
				if(participant->encoder)
					opus_encoder_ctl(participant->encoder, OPUS_SET_COMPLEXITY(participant->opus_complexity));
			}
			if(exploss) {
				int expected_loss = json_integer_value(exploss);
				if(expected_loss > 20) {
					JANUS_LOG(LOG_ERR, "Invalid element (expected_loss should be a positive integer between 0 and 20)\n");
					error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
					g_snprintf(error_cause, 512, "Invalid element (expected_loss should be a positive integer between 0 and 20)");
					goto error;
				}
				participant->expected_loss = expected_loss;
				if(participant->encoder)
					opus_encoder_ctl(participant->encoder, OPUS_SET_PACKET_LOSS_PERC(participant->expected_loss));
			}
			if(group && participant->room && participant->room->groups != NULL) {
				const char *group_name = json_string_value(group);
				uint group_id = GPOINTER_TO_UINT(g_hash_table_lookup(participant->room->groups, group_name));
				if(group_id == 0) {
					JANUS_LOG(LOG_ERR, "No such group (%s)\n", group_name);
					error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_GROUP;
					g_snprintf(error_cause, 512, "No such group (%s)", group_name);
					goto error;
				}
				participant->group = group_id;
			}
			if(muted || display || (participant->stereo && spatial)) {
				if(muted) {
					participant->muted = json_is_true(muted);
					JANUS_LOG(LOG_VERB, "Setting muted property: %s (room %s, user %s)\n",
						participant->muted ? "true" : "false", participant->room->room_id_str, participant->user_id_str);
					if(participant->muted) {
						/* Clear the queued packets waiting to be handled */
						janus_mutex_lock(&participant->qmutex);
						while(participant->inbuf) {
							GList *first = g_list_first(participant->inbuf);
							janus_audiobridge_rtp_relay_packet *pkt = (janus_audiobridge_rtp_relay_packet *)first->data;
							participant->inbuf = g_list_delete_link(participant->inbuf, first);
							first = NULL;
							if(pkt == NULL)
								continue;
							if(pkt->data)
								g_free(pkt->data);
							pkt->data = NULL;
							g_free(pkt);
							pkt = NULL;
						}
						janus_mutex_unlock(&participant->qmutex);
					}
				}
				if(display) {
					char *old_display = participant->display;
					char *new_display = g_strdup(json_string_value(display));
					participant->display = new_display;
					g_free(old_display);
					JANUS_LOG(LOG_VERB, "Setting display property: %s (room %s, user %s)\n",
						participant->display, participant->room->room_id_str, participant->user_id_str);
				}
				if(participant->stereo && spatial) {
					int spatial_position = json_integer_value(spatial);
					if(spatial_position > 100)
						spatial_position = 100;
					participant->spatial_position = spatial_position;
				}
				/* Notify all other participants */
				janus_mutex_lock(&rooms_mutex);
				janus_audiobridge_room *audiobridge = participant->room;
				if(audiobridge != NULL) {
					janus_mutex_lock(&audiobridge->mutex);
					json_t *list = json_array();
					json_t *pl = json_object();
					json_object_set_new(pl, "id",
						string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
					if(participant->display)
						json_object_set_new(pl, "display", json_string(participant->display));
					json_object_set_new(pl, "setup", g_atomic_int_get(&participant->session->started) ? json_true() : json_false());
					json_object_set_new(pl, "muted", participant->muted ? json_true() : json_false());
					if(audiobridge->spatial_audio)
						json_object_set_new(pl, "spatial_position", json_integer(participant->spatial_position));
					json_array_append_new(list, pl);
					json_t *pub = json_object();
					json_object_set_new(pub, "audiobridge", json_string("event"));
					json_object_set_new(pub, "room",
						string_ids ? json_string(participant->room->room_id_str) : json_integer(participant->room->room_id));
					json_object_set_new(pub, "participants", list);
					GHashTableIter iter;
					gpointer value;
					g_hash_table_iter_init(&iter, audiobridge->participants);
					while(g_hash_table_iter_next(&iter, NULL, &value)) {
						janus_audiobridge_participant *p = value;
						if(p == participant) {
							continue;	/* Skip the new participant itself */
						}
						JANUS_LOG(LOG_VERB, "Notifying participant %s (%s)\n",
							p->user_id_str, p->display ? p->display : "??");
						int ret = gateway->push_event(p->session->handle, &janus_audiobridge_plugin, NULL, pub, NULL);
						JANUS_LOG(LOG_VERB, "  >> %d (%s)\n", ret, janus_get_api_error(ret));
					}
					json_decref(pub);
					janus_mutex_unlock(&audiobridge->mutex);
				}
				janus_mutex_unlock(&rooms_mutex);
			}
			janus_mutex_lock(&participant->rec_mutex);
			const char *recording_base = json_string_value(recfile);
			if(recording_base) {
				g_free(participant->mjr_base);
				participant->mjr_base = g_strdup(recording_base);
			}
			if(record) {
				if(json_is_true(record)) {
					/* Start recording (ignore if recording already) */
					if(participant->arc != NULL) {
						JANUS_LOG(LOG_WARN, "Already recording participant's audio (room %s, user %s)\n",
							participant->room->room_id_str, participant->user_id_str);
					} else {
						JANUS_LOG(LOG_INFO, "Starting recording of participant's audio (room %s, user %s)\n",
							participant->room->room_id_str, participant->user_id_str);
						janus_audiobridge_recorder_create(participant);
						participant->mjr_active = TRUE;
					}
				} else {
					/* Stop recording (ignore if not recording) */
					janus_audiobridge_recorder_close(participant);
					participant->mjr_active = FALSE;
				}
			}
			janus_mutex_unlock(&participant->rec_mutex);
			gboolean do_update = update ? json_is_true(update) : FALSE;
			if(do_update && (!sdp_update || !session->plugin_offer)) {
				JANUS_LOG(LOG_WARN, "Got a 'update' request, but no SDP update? Ignoring...\n");
			}
			/* Done */
			event = json_object();
			json_object_set_new(event, "audiobridge", json_string("event"));
			json_object_set_new(event, "result", json_string("ok"));
			/* Also notify event handlers */
			if(notify_events && gateway->events_is_enabled()) {
				janus_audiobridge_room *audiobridge = participant->room;
				json_t *info = json_object();
				json_object_set_new(info, "event", json_string("configured"));
				json_object_set_new(info, "room",
					string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
				json_object_set_new(info, "id",
					string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
				json_object_set_new(info, "display", json_string(participant->display));
				json_object_set_new(info, "muted", participant->muted ? json_true() : json_false());
				if(participant->opus_bitrate > 0)
					json_object_set_new(info, "bitrate", json_integer(participant->opus_bitrate));
				json_object_set_new(info, "quality", json_integer(participant->opus_complexity));
				if(participant->stereo)
					json_object_set_new(info, "spatial_position", json_integer(participant->spatial_position));
				gateway->notify_event(&janus_audiobridge_plugin, session->handle, info);
			}
			/* If we need to generate an offer ourselves, do that */
			if(do_update && session->plugin_offer) {
				/* We need an update and we originated an offer before, let's do it again */
				generate_offer = TRUE;
			} else if(gen_offer != NULL) {
				generate_offer = json_is_true(gen_offer);
			}
			if(generate_offer) {
				/* We should check if this conflicts with a user-generated offer from before */
				session->plugin_offer = generate_offer;
			}
		} else if(!strcasecmp(request_text, "changeroom")) {
			/* The participant wants to leave the current room and join another one without reconnecting (e.g., a sidebar) */
			JANUS_VALIDATE_JSON_OBJECT(root, join_parameters,
				error_code, error_cause, TRUE,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			if(error_code != 0)
				goto error;
			if(!string_ids) {
				JANUS_VALIDATE_JSON_OBJECT(root, room_parameters,
					error_code, error_cause, TRUE,
					JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			} else {
				JANUS_VALIDATE_JSON_OBJECT(root, roomstr_parameters,
					error_code, error_cause, TRUE,
					JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
			}
			if(error_code != 0)
				goto error;
			json_t *room = json_object_get(root, "room");
			guint64 room_id = 0;
			char room_id_num[30], *room_id_str = NULL;
			if(!string_ids) {
				room_id = json_integer_value(room);
				g_snprintf(room_id_num, sizeof(room_id_num), "%"SCNu64, room_id);
				room_id_str = room_id_num;
			} else {
				room_id_str = (char *)json_string_value(room);
			}
			janus_mutex_lock(&rooms_mutex);
			janus_audiobridge_participant *participant = (janus_audiobridge_participant *)session->participant;
			if(participant == NULL || participant->room == NULL) {
				janus_mutex_unlock(&rooms_mutex);
				JANUS_LOG(LOG_ERR, "Can't change room (not in a room in the first place)\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_NOT_JOINED;
				g_snprintf(error_cause, 512, "Can't change room (not in a room in the first place)");
				goto error;
			}
			/* Is this the same room we're in? */
			if(participant->room && ((!string_ids && participant->room->room_id == room_id) ||
					(string_ids && participant->room->room_id_str && !strcmp(participant->room->room_id_str, room_id_str)))) {
				janus_mutex_unlock(&rooms_mutex);
				JANUS_LOG(LOG_ERR, "Already in this room\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_ALREADY_JOINED;
				g_snprintf(error_cause, 512, "Already in this room");
				goto error;
			}
			janus_audiobridge_room *audiobridge = g_hash_table_lookup(rooms,
				string_ids ? (gpointer)room_id_str : (gpointer)&room_id);
			if(audiobridge == NULL) {
				janus_mutex_unlock(&rooms_mutex);
				error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_ROOM;
				JANUS_LOG(LOG_ERR, "No such room (%s)\n", room_id_str);
				g_snprintf(error_cause, 512, "No such room (%s)", room_id_str);
				goto error;
			}
			janus_refcount_increase(&audiobridge->ref);
			janus_mutex_lock(&audiobridge->mutex);
			/* A pin may be required for this action */
			JANUS_CHECK_SECRET(audiobridge->room_pin, root, "pin", error_code, error_cause,
				JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
			if(error_code != 0) {
				janus_mutex_unlock(&audiobridge->mutex);
				janus_refcount_decrease(&audiobridge->ref);
				janus_mutex_unlock(&rooms_mutex);
				goto error;
			}
			/* A token might be required too */
			if(audiobridge->check_tokens) {
				json_t *token = json_object_get(root, "token");
				const char *token_text = token ? json_string_value(token) : NULL;
				if(token_text == NULL || g_hash_table_lookup(audiobridge->allowed, token_text) == NULL) {
					JANUS_LOG(LOG_ERR, "Unauthorized (not in the allowed list)\n");
					error_code = JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED;
					g_snprintf(error_cause, 512, "Unauthorized (not in the allowed list)");
					janus_mutex_unlock(&audiobridge->mutex);
					janus_refcount_decrease(&audiobridge->ref);
					janus_mutex_unlock(&rooms_mutex);
					goto error;
				}
			}
			gboolean admin = FALSE;
			if(json_object_get(root, "secret") != NULL) {
				/* The user is trying to present themselves as an admin */
				JANUS_CHECK_SECRET(audiobridge->room_secret, root, "secret", error_code, error_cause,
					JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_UNAUTHORIZED);
				if(error_code != 0) {
					janus_mutex_unlock(&audiobridge->mutex);
					janus_refcount_decrease(&audiobridge->ref);
					goto error;
				}
				admin = TRUE;
			}
			/* If this room uses groups, make sure a valid group name was provided */
			uint group = 0;
			if(audiobridge->groups != NULL) {
				JANUS_VALIDATE_JSON_OBJECT(root, group_parameters,
					error_code, error_cause, TRUE,
					JANUS_AUDIOBRIDGE_ERROR_MISSING_ELEMENT, JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT);
				if(error_code != 0) {
					janus_mutex_unlock(&audiobridge->mutex);
					janus_refcount_decrease(&audiobridge->ref);
					goto error;
				}
				const char *group_name = json_string_value(json_object_get(root, "group"));
				group = GPOINTER_TO_UINT(g_hash_table_lookup(audiobridge->groups, group_name));
				if(group == 0) {
					janus_mutex_unlock(&audiobridge->mutex);
					janus_refcount_decrease(&audiobridge->ref);
					JANUS_LOG(LOG_ERR, "No such group (%s)\n", group_name);
					error_code = JANUS_AUDIOBRIDGE_ERROR_NO_SUCH_GROUP;
					g_snprintf(error_cause, 512, "No such group (%s)", group_name);
					goto error;
				}
			}
			json_t *display = json_object_get(root, "display");
			const char *display_text = display ? json_string_value(display) : NULL;
			json_t *muted = json_object_get(root, "muted");
			json_t *gain = json_object_get(root, "volume");
			json_t *spatial = json_object_get(root, "spatial_position");
			json_t *bitrate = json_object_get(root, "bitrate");
			json_t *quality = json_object_get(root, "quality");
			json_t *exploss = json_object_get(root, "expected_loss");
			int volume = gain ? json_integer_value(gain) : 100;
			int spatial_position = spatial ? json_integer_value(spatial) : 64;
			int32_t opus_bitrate = audiobridge->default_bitrate;
			if(bitrate) {
				opus_bitrate = json_integer_value(bitrate);
				if(opus_bitrate < 500 || opus_bitrate > 512000) {
					JANUS_LOG(LOG_WARN, "Invalid bitrate %"SCNi32", falling back to default/auto\n", opus_bitrate);
					opus_bitrate = audiobridge->default_bitrate;
				}
			}
			int complexity = quality ? json_integer_value(quality) : DEFAULT_COMPLEXITY;
			if(complexity < 1 || complexity > 10) {
				janus_mutex_unlock(&audiobridge->mutex);
				janus_refcount_decrease(&audiobridge->ref);
				janus_mutex_unlock(&rooms_mutex);
				JANUS_LOG(LOG_ERR, "Invalid element (quality should be a positive integer between 1 and 10)\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
				g_snprintf(error_cause, 512, "Invalid element (quality should be a positive integer between 1 and 10)");
				goto error;
			}
			int expected_loss = exploss ? json_integer_value(exploss) : audiobridge->default_expectedloss;
			if(expected_loss > 20) {
				janus_mutex_unlock(&audiobridge->mutex);
				janus_refcount_decrease(&audiobridge->ref);
				janus_mutex_unlock(&rooms_mutex);
				JANUS_LOG(LOG_ERR, "Invalid element (expected_loss should be a positive integer between 0 and 20)\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
				g_snprintf(error_cause, 512, "Invalid element (expected_loss should be a positive integer between 0 and 20)");
				goto error;
			}
			guint64 user_id = 0;
			char user_id_num[30], *user_id_str = NULL;
			gboolean user_id_allocated = FALSE;
			json_t *id = json_object_get(root, "id");
			if(id) {
				if(!string_ids) {
					user_id = json_integer_value(id);
					g_snprintf(user_id_num, sizeof(user_id_num), "%"SCNu64, user_id);
					user_id_str = user_id_num;
				} else {
					user_id_str = (char *)json_string_value(id);
				}
				if(g_hash_table_lookup(audiobridge->participants,
						string_ids ? (gpointer)user_id_str : (gpointer)&user_id) != NULL) {
					/* User ID already taken */
					janus_mutex_unlock(&audiobridge->mutex);
					janus_refcount_decrease(&audiobridge->ref);
					janus_mutex_unlock(&rooms_mutex);
					error_code = JANUS_AUDIOBRIDGE_ERROR_ID_EXISTS;
					JANUS_LOG(LOG_ERR, "User ID %s already exists\n", user_id_str);
					g_snprintf(error_cause, 512, "User ID %s already exists", user_id_str);
					goto error;
				}
			}
			if(!string_ids) {
				if(user_id == 0) {
					/* Generate a random ID */
					while(user_id == 0) {
						user_id = janus_random_uint64();
						if(g_hash_table_lookup(audiobridge->participants, &user_id) != NULL) {
							/* User ID already taken, try another one */
							user_id = 0;
						}
					}
					g_snprintf(user_id_num, sizeof(user_id_num), "%"SCNu64, user_id);
					user_id_str = user_id_num;
				}
				JANUS_LOG(LOG_VERB, "  -- Participant ID in new room %"SCNu64": %"SCNu64"\n", room_id, user_id);
			} else {
				if(user_id_str == NULL) {
					/* Generate a random ID */
					while(user_id_str == NULL) {
						user_id_str = janus_random_uuid();
						if(g_hash_table_lookup(audiobridge->participants, user_id_str) != NULL) {
							/* User ID already taken, try another one */
							g_clear_pointer(&user_id_str, g_free);
						}
					}
					user_id_allocated = TRUE;
				}
				JANUS_LOG(LOG_VERB, "  -- Participant ID in new room %s: %s\n", room_id_str, user_id_str);
			}
			participant->prebuffering = TRUE;
			participant->audio_active_packets = 0;
			participant->audio_dBov_sum = 0;
			participant->talking = FALSE;
			/* Is the sampling rate of the new room the same as the one in the old room, or should we update the decoder/encoder? */
			janus_audiobridge_room *old_audiobridge = participant->room;
			/* Leave the old room first... */
			janus_refcount_increase(&participant->ref);
			janus_mutex_lock(&old_audiobridge->mutex);
			g_hash_table_remove(old_audiobridge->participants,
				string_ids ? (gpointer)participant->user_id_str : (gpointer)&participant->user_id);
			if(old_audiobridge->sampling_rate != audiobridge->sampling_rate ||
					old_audiobridge->spatial_audio != audiobridge->spatial_audio) {
				/* Create a new one that takes into account the sampling rate we want now */
				participant->stereo = audiobridge->spatial_audio;
				participant->spatial_position = 50;
				int error = 0;
				OpusEncoder *new_encoder = opus_encoder_create(audiobridge->sampling_rate,
					audiobridge->spatial_audio ? 2 : 1, OPUS_APPLICATION_VOIP, &error);
				if(error != OPUS_OK) {
					if(user_id_allocated)
						g_free(user_id_str);
					janus_refcount_decrease(&audiobridge->ref);
					if(new_encoder)
						opus_encoder_destroy(new_encoder);
					new_encoder = NULL;
					JANUS_LOG(LOG_ERR, "Error creating Opus encoder\n");
					error_code = JANUS_AUDIOBRIDGE_ERROR_LIBOPUS_ERROR;
					g_snprintf(error_cause, 512, "Error creating Opus encoder");
					/* Join the old room again... */
					g_hash_table_insert(audiobridge->participants,
						string_ids ? (gpointer)g_strdup(participant->user_id_str) : (gpointer)janus_uint64_dup(participant->user_id),
						participant);
					janus_mutex_unlock(&old_audiobridge->mutex);
					janus_mutex_unlock(&audiobridge->mutex);
					janus_mutex_unlock(&rooms_mutex);
					goto error;
				}
				if(audiobridge->sampling_rate == 8000) {
					opus_encoder_ctl(new_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_NARROWBAND));
				} else if(audiobridge->sampling_rate == 12000) {
					opus_encoder_ctl(new_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_MEDIUMBAND));
				} else if(audiobridge->sampling_rate == 16000) {
					opus_encoder_ctl(new_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
				} else if(audiobridge->sampling_rate == 24000) {
					opus_encoder_ctl(new_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_SUPERWIDEBAND));
				} else if(audiobridge->sampling_rate == 48000) {
					opus_encoder_ctl(new_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
				} else {
					JANUS_LOG(LOG_WARN, "Unsupported sampling rate %d, setting 16kHz\n", audiobridge->sampling_rate);
					audiobridge->sampling_rate = 16000;
					opus_encoder_ctl(new_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
				}
				opus_encoder_ctl(new_encoder, OPUS_SET_INBAND_FEC(participant->fec));
				/* Opus decoder */
				error = 0;
				OpusDecoder *new_decoder = opus_decoder_create(audiobridge->sampling_rate,
					audiobridge->spatial_audio ? 2 : 1, &error);
				if(error != OPUS_OK) {
					if(user_id_allocated)
						g_free(user_id_str);
					janus_refcount_decrease(&audiobridge->ref);
					if(new_encoder)
						opus_encoder_destroy(new_encoder);
					new_encoder = NULL;
					if(new_decoder)
						opus_decoder_destroy(new_decoder);
					new_decoder = NULL;
					JANUS_LOG(LOG_ERR, "Error creating Opus decoder\n");
					error_code = JANUS_AUDIOBRIDGE_ERROR_LIBOPUS_ERROR;
					g_snprintf(error_cause, 512, "Error creating Opus decoder");
					/* Join the old room again... */
					g_hash_table_insert(audiobridge->participants,
						string_ids ? (gpointer)g_strdup(participant->user_id_str) : (gpointer)janus_uint64_dup(participant->user_id),
						participant);
					janus_mutex_unlock(&old_audiobridge->mutex);
					janus_mutex_unlock(&audiobridge->mutex);
					janus_mutex_unlock(&rooms_mutex);
					goto error;
				}
				participant->reset = FALSE;
				/* Destroy the previous encoder/decoder and update the references */
				if(participant->encoder)
					opus_encoder_destroy(participant->encoder);
				participant->encoder = new_encoder;
				if(participant->decoder)
					opus_decoder_destroy(participant->decoder);
				participant->decoder = new_decoder;
			}
			if(quality)
				opus_encoder_ctl(participant->encoder, OPUS_SET_COMPLEXITY(participant->opus_complexity));
			/* Everything looks fine, start by telling the folks in the old room this participant is going away */
			event = json_object();
			json_object_set_new(event, "audiobridge", json_string("event"));
			json_object_set_new(event, "room",
				string_ids ? json_string(old_audiobridge->room_id_str) : json_integer(old_audiobridge->room_id));
			json_object_set_new(event, "leaving",
				string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
			GHashTableIter iter;
			gpointer value;
			g_hash_table_iter_init(&iter, old_audiobridge->participants);
			while(g_hash_table_iter_next(&iter, NULL, &value)) {
				janus_audiobridge_participant *p = value;
				if(p == participant) {
					continue;	/* Skip the new participant itself */
				}
				JANUS_LOG(LOG_VERB, "Notifying participant %s (%s)\n", p->user_id_str, p->display ? p->display : "??");
				int ret = gateway->push_event(p->session->handle, &janus_audiobridge_plugin, NULL, event, NULL);
				JANUS_LOG(LOG_VERB, "  >> %d (%s)\n", ret, janus_get_api_error(ret));
			}
			json_decref(event);
			/* Also notify event handlers */
			if(notify_events && gateway->events_is_enabled()) {
				json_t *info = json_object();
				json_object_set_new(info, "event", json_string("left"));
				json_object_set_new(info, "room",
					string_ids ? json_string(old_audiobridge->room_id_str) : json_integer(old_audiobridge->room_id));
				json_object_set_new(info, "id",
					string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
				json_object_set_new(info, "display", json_string(participant->display));
				gateway->notify_event(&janus_audiobridge_plugin, session->handle, info);
			}
			janus_mutex_unlock(&old_audiobridge->mutex);
			/* Stop recording, if we were (since this is a new room, a new recording would be required, so a new configure) */
			janus_mutex_lock(&participant->rec_mutex);
			janus_audiobridge_recorder_close(participant);
			participant->mjr_active = FALSE;
			janus_mutex_unlock(&participant->rec_mutex);
			janus_refcount_decrease(&old_audiobridge->ref);
			/* Done, join the new one */
			participant->user_id = user_id;
			g_free(participant->user_id_str);
			participant->user_id_str = user_id_str ? g_strdup(user_id_str) : NULL;
			participant->group = group;
			participant->admin = admin;
			g_free(participant->display);
			participant->display = display_text ? g_strdup(display_text) : NULL;
			participant->room = audiobridge;
			participant->muted = muted ? json_is_true(muted) : FALSE;	/* When switching to a new room, you're unmuted by default */
			participant->audio_active_packets = 0;
			participant->audio_dBov_sum = 0;
			participant->talking = FALSE;
			participant->volume_gain = volume;
			participant->stereo = audiobridge->spatial_audio;
			participant->spatial_position = spatial_position;
			if(participant->spatial_position < 0)
				participant->spatial_position = 0;
			else if(participant->spatial_position > 100)
				participant->spatial_position = 100;
			participant->opus_bitrate = opus_bitrate;
			if(participant->encoder)
				opus_encoder_ctl(participant->encoder, OPUS_SET_BITRATE(participant->opus_bitrate ? participant->opus_bitrate : OPUS_AUTO));
			if(quality) {
				participant->opus_complexity = complexity;
				if(participant->encoder)
					opus_encoder_ctl(participant->encoder, OPUS_SET_COMPLEXITY(participant->opus_complexity));
			}
			if(exploss) {
				participant->expected_loss = expected_loss;
				opus_encoder_ctl(participant->encoder, OPUS_SET_PACKET_LOSS_PERC(participant->expected_loss));
			}
			g_hash_table_insert(audiobridge->participants,
				string_ids ? (gpointer)g_strdup(participant->user_id_str) : (gpointer)janus_uint64_dup(participant->user_id),
				participant);
			/* Notify the other participants */
			json_t *newuser = json_object();
			json_object_set_new(newuser, "audiobridge", json_string("joined"));
			json_object_set_new(newuser, "room",
				string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
			json_t *newuserlist = json_array();
			json_t *pl = json_object();
			json_object_set_new(pl, "id",
				string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
			if(participant->display)
				json_object_set_new(pl, "display", json_string(participant->display));
			json_object_set_new(pl, "setup", g_atomic_int_get(&participant->session->started) ? json_true() : json_false());
			json_object_set_new(pl, "muted", participant->muted ? json_true() : json_false());
			if(audiobridge->spatial_audio)
				json_object_set_new(pl, "spatial_position", json_integer(participant->spatial_position));
			json_array_append_new(newuserlist, pl);
			json_object_set_new(newuser, "participants", newuserlist);
			g_hash_table_iter_init(&iter, audiobridge->participants);
			while(g_hash_table_iter_next(&iter, NULL, &value)) {
				janus_audiobridge_participant *p = value;
				if(p == participant) {
					continue;
				}
				JANUS_LOG(LOG_VERB, "Notifying participant %s (%s)\n", p->user_id_str, p->display ? p->display : "??");
				int ret = gateway->push_event(p->session->handle, &janus_audiobridge_plugin, NULL, newuser, NULL);
				JANUS_LOG(LOG_VERB, "  >> %d (%s)\n", ret, janus_get_api_error(ret));
			}
			json_decref(newuser);
			/* Return a list of all available participants for the new participant now */
			json_t *list = json_array();
			g_hash_table_iter_init(&iter, audiobridge->participants);
			while(g_hash_table_iter_next(&iter, NULL, &value)) {
				janus_audiobridge_participant *p = value;
				if(p == participant) {
					continue;
				}
				json_t *pl = json_object();
				json_object_set_new(pl, "id", string_ids ? json_string(p->user_id_str) : json_integer(p->user_id));
				if(p->display)
					json_object_set_new(pl, "display", json_string(p->display));
				json_object_set_new(pl, "setup", g_atomic_int_get(&p->session->started) ? json_true() : json_false());
				json_object_set_new(pl, "muted", p->muted ? json_true() : json_false());
				if(p->extmap_id > 0)
					json_object_set_new(pl, "talking", p->talking ? json_true() : json_false());
				if(audiobridge->spatial_audio)
					json_object_set_new(pl, "spatial_position", json_integer(p->spatial_position));
				json_array_append_new(list, pl);
			}
			event = json_object();
			json_object_set_new(event, "audiobridge", json_string("roomchanged"));
			json_object_set_new(event, "room",
				string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
			json_object_set_new(event, "id", string_ids ? json_string(user_id_str) : json_integer(user_id));
			json_object_set_new(event, "participants", list);
			/* Also notify event handlers */
			if(notify_events && gateway->events_is_enabled()) {
				json_t *info = json_object();
				json_object_set_new(info, "event", json_string("joined"));
				json_object_set_new(info, "room",
					string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
				json_object_set_new(info, "id",
					string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
				json_object_set_new(info, "display", json_string(participant->display));
				json_object_set_new(info, "muted", participant->muted ? json_true() : json_false());
				if(participant->stereo)
					json_object_set_new(info, "spatial_position", json_integer(participant->spatial_position));
				gateway->notify_event(&janus_audiobridge_plugin, session->handle, info);
			}
			if(user_id_allocated)
				g_free(user_id_str);
			janus_mutex_unlock(&audiobridge->mutex);
			janus_mutex_unlock(&rooms_mutex);
		} else if(!strcasecmp(request_text, "hangup")) {
			/* Get rid of an ongoing session */
			gateway->close_pc(session->handle);
			event = json_object();
			json_object_set_new(event, "audiobridge", json_string("hangingup"));
		} else if(!strcasecmp(request_text, "leave")) {
			/* This participant is leaving */
			janus_audiobridge_participant *participant = (janus_audiobridge_participant *)session->participant;
			if(participant == NULL || participant->room == NULL) {
				JANUS_LOG(LOG_ERR, "Can't leave (not in a room)\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_NOT_JOINED;
				g_snprintf(error_cause, 512, "Can't leave (not in a room)");
				goto error;
			}
			/* Tell everybody */
			janus_mutex_lock(&rooms_mutex);
			janus_audiobridge_room *audiobridge = participant->room;
			gboolean removed = FALSE;
			if(audiobridge != NULL) {
				janus_refcount_increase(&audiobridge->ref);
				janus_mutex_lock(&audiobridge->mutex);
				event = json_object();
				json_object_set_new(event, "audiobridge", json_string("event"));
				json_object_set_new(event, "room",
					string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
				json_object_set_new(event, "leaving",
					string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
				GHashTableIter iter;
				gpointer value;
				g_hash_table_iter_init(&iter, audiobridge->participants);
				while(g_hash_table_iter_next(&iter, NULL, &value)) {
					janus_audiobridge_participant *p = value;
					if(p == participant) {
						continue;	/* Skip the new participant itself */
					}
					JANUS_LOG(LOG_VERB, "Notifying participant %s (%s)\n", p->user_id_str, p->display ? p->display : "??");
					int ret = gateway->push_event(p->session->handle, &janus_audiobridge_plugin, NULL, event, NULL);
					JANUS_LOG(LOG_VERB, "  >> %d (%s)\n", ret, janus_get_api_error(ret));
				}
				json_decref(event);
				/* Actually leave the room... */
				removed = g_hash_table_remove(audiobridge->participants,
					string_ids ? (gpointer)participant->user_id_str : (gpointer)&participant->user_id);
				participant->room = NULL;
			}
			/* Get rid of queued packets */
			janus_mutex_lock(&participant->qmutex);
			g_atomic_int_set(&participant->active, 0);
			participant->prebuffering = TRUE;
			while(participant->inbuf) {
				GList *first = g_list_first(participant->inbuf);
				janus_audiobridge_rtp_relay_packet *pkt = (janus_audiobridge_rtp_relay_packet *)first->data;
				participant->inbuf = g_list_delete_link(participant->inbuf, first);
				first = NULL;
				if(pkt == NULL)
					continue;
				g_free(pkt->data);
				pkt->data = NULL;
				g_free(pkt);
				pkt = NULL;
			}
			janus_mutex_unlock(&participant->qmutex);
			/* Stop recording, if we were */
			janus_mutex_lock(&participant->rec_mutex);
			janus_audiobridge_recorder_close(participant);
			participant->mjr_active = FALSE;
			janus_mutex_unlock(&participant->rec_mutex);
			/* Also notify event handlers */
			if(notify_events && gateway->events_is_enabled()) {
				json_t *info = json_object();
				json_object_set_new(info, "event", json_string("left"));
				json_object_set_new(info, "room",
					string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
				json_object_set_new(info, "id",
					string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
				json_object_set_new(info, "display", json_string(participant->display));
				gateway->notify_event(&janus_audiobridge_plugin, session->handle, info);
			}
			/* Done */
			event = json_object();
			json_object_set_new(event, "audiobridge", json_string("left"));
			if(audiobridge != NULL) {
				json_object_set_new(event, "room",
					string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
				janus_mutex_unlock(&audiobridge->mutex);
				janus_refcount_decrease(&audiobridge->ref);
			}
			json_object_set_new(event, "id",
				string_ids ? json_string(participant->user_id_str) : json_integer(participant->user_id));
			janus_mutex_unlock(&rooms_mutex);
			if(removed) {
				/* Only decrease the counter if we were still there */
				janus_refcount_decrease(&audiobridge->ref);
			}
		} else {
			JANUS_LOG(LOG_ERR, "Unknown request '%s'\n", request_text);
			error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_REQUEST;
			g_snprintf(error_cause, 512, "Unknown request '%s'", request_text);
			goto error;
		}

		/* Prepare JSON event */
		JANUS_LOG(LOG_VERB, "Preparing JSON event as a reply\n");
		/* Any SDP to handle? */
		if(!msg_sdp && !generate_offer) {
			int ret = gateway->push_event(msg->handle, &janus_audiobridge_plugin, msg->transaction, event, NULL);
			JANUS_LOG(LOG_VERB, "  >> %d (%s)\n", ret, janus_get_api_error(ret));
			json_decref(event);
		} else {
			if(msg_sdp) {
				JANUS_LOG(LOG_VERB, "This is involving a negotiation (%s) as well:\n%s\n", msg_sdp_type, msg_sdp);
			} else {
				JANUS_LOG(LOG_VERB, "This is involving a negotiation: generating offer\n");
			}
			/* Prepare an SDP offer or answer */
			if(msg_sdp && json_is_true(json_object_get(msg->jsep, "e2ee"))) {
				/* Media is encrypted, but we need unencrypted media frames to decode and mix */
				json_decref(event);
				JANUS_LOG(LOG_ERR, "Media encryption unsupported by this plugin\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_ELEMENT;
				g_snprintf(error_cause, 512, "Media encryption unsupported by this plugin");
				goto error;
			}
			/* We answer by default, unless the user asked the plugin for an offer */
			if(msg_sdp && got_offer && session->plugin_offer) {
				json_decref(event);
				JANUS_LOG(LOG_ERR, "Received an offer on a plugin-offered session\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_SDP;
				g_snprintf(error_cause, 512, "Received an offer on a plugin-offered session");
				goto error;
			} else if(msg_sdp && got_answer && !session->plugin_offer) {
				json_decref(event);
				JANUS_LOG(LOG_ERR, "Received an answer when we didn't send an offer\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_SDP;
				g_snprintf(error_cause, 512, "Received an answer when we didn't send an offer");
				goto error;
			}
			const char *type = session->plugin_offer ? "offer" : "answer";
			char error_str[512];
			janus_sdp *sdp = NULL;
			if(msg_sdp != NULL) {
				sdp = janus_sdp_parse(msg_sdp, error_str, sizeof(error_str));
				if(sdp == NULL) {
					json_decref(event);
					JANUS_LOG(LOG_ERR, "Error parsing %s: %s\n", msg_sdp, error_str);
					error_code = JANUS_AUDIOBRIDGE_ERROR_INVALID_SDP;
					g_snprintf(error_cause, 512, "Error parsing %s: %s", msg_sdp, error_str);
					goto error;
				}
			}
			if(got_offer) {
				if(sdp_update) {
					/* Renegotiation */
					JANUS_LOG(LOG_VERB, "Request to update existing connection\n");
					session->sdp_version++;		/* This needs to be increased when it changes */
				} else {
					/* New PeerConnection */
					session->sdp_version = 1;	/* This needs to be increased when it changes */
					session->sdp_sessid = janus_get_real_time();
				}
			}
			/* What is the Opus payload type? */
			janus_audiobridge_participant *participant = (janus_audiobridge_participant *)session->participant;
			if(sdp != NULL) {
				participant->opus_pt = janus_sdp_get_codec_pt(sdp, -1, "opus");
				if(participant->opus_pt > 0 && strstr(msg_sdp, "useinbandfec=1")){
					/* Opus codec, inband FEC setted */
					participant->fec = TRUE;
					participant->probation = MIN_SEQUENTIAL;
					opus_encoder_ctl(participant->encoder, OPUS_SET_INBAND_FEC(participant->fec));
				}
				JANUS_LOG(LOG_VERB, "Opus payload type is %d, FEC %s\n", participant->opus_pt, participant->fec ? "enabled" : "disabled");
			}
			/* Check if the audio level extension was offered */
			int extmap_id = generate_offer ? 2 : -1;
			if(sdp != NULL) {
				GList *temp = sdp->m_lines;
				while(temp) {
					janus_sdp_mline *m = (janus_sdp_mline *)temp->data;
					if(m->type == JANUS_SDP_AUDIO) {
						GList *ma = m->attributes;
						while(ma) {
							janus_sdp_attribute *a = (janus_sdp_attribute *)ma->data;
							if(a->value) {
								if(strstr(a->value, JANUS_RTP_EXTMAP_AUDIO_LEVEL)) {
									extmap_id = atoi(a->value);
									if(extmap_id < 0)
										extmap_id = 0;
								}
							}
							ma = ma->next;
						}
					}
					temp = temp->next;
				}
			}
			/* If we're just processing an answer, we're done */
			if(got_answer) {
				gint64 start = janus_get_monotonic_time();
				int res = gateway->push_event(msg->handle, &janus_audiobridge_plugin, msg->transaction, event, NULL);
				JANUS_LOG(LOG_VERB, "  >> Pushing event: %d (took %"SCNu64" us)\n", res, janus_get_monotonic_time()-start);
				json_decref(event);
				janus_sdp_destroy(sdp);
				if(msg)
					janus_audiobridge_message_free(msg);
				msg = NULL;
				continue;
			}
			if(participant == NULL || participant->room == NULL) {
				JANUS_LOG(LOG_ERR, "Can't handle SDP (not in a room)\n");
				error_code = JANUS_AUDIOBRIDGE_ERROR_NOT_JOINED;
				g_snprintf(error_cause, 512, "Can't handle SDP (not in a room)");
				if(sdp)
					janus_sdp_destroy(sdp);
				goto error;
			}
			/* We use a custom session name in the SDP */
			char s_name[100];
			g_snprintf(s_name, sizeof(s_name), "AudioBridge %s", participant->room->room_id_str);
			/* Prepare a fmtp string too */
			char fmtp[100];
			g_snprintf(fmtp, sizeof(fmtp), "%d maxplaybackrate=%"SCNu32"; stereo=%d; sprop-stereo=%d; useinbandfec=%d\r\n",
				participant->opus_pt, participant->room->sampling_rate,
				participant->stereo ? 1 : 0, participant->stereo ? 1 : 0, participant->fec ? 1 : 0);
			/* If we got an offer, we need to answer */
			janus_sdp *offer = NULL, *answer = NULL;
			if(got_offer) {
				answer = janus_sdp_generate_answer(sdp);
				/* Only accept the first audio line, and reject everything else if offered */
				GList *temp = sdp->m_lines;
				gboolean accepted = FALSE;
				while(temp) {
					janus_sdp_mline *m = (janus_sdp_mline *)temp->data;
					if(m->type == JANUS_SDP_AUDIO && !accepted) {
						accepted = TRUE;
						janus_sdp_generate_answer_mline(sdp, answer, m,
							JANUS_SDP_OA_MLINE, JANUS_SDP_AUDIO,
							JANUS_SDP_OA_CODEC, janus_audiocodec_name(participant->codec),
							JANUS_SDP_OA_ACCEPT_EXTMAP, JANUS_RTP_EXTMAP_MID,
							JANUS_SDP_OA_ACCEPT_EXTMAP, JANUS_RTP_EXTMAP_AUDIO_LEVEL,
							JANUS_SDP_OA_DONE);
					}
					temp = temp->next;
				}
				/* Replace the session name */
				g_free(answer->s_name);
				answer->s_name = g_strdup(s_name);
				/* Add an fmtp attribute if this is Opus */
				if(participant->codec == JANUS_AUDIOCODEC_OPUS) {
					janus_sdp_attribute *a = janus_sdp_attribute_create("fmtp", "%s", fmtp);
					janus_sdp_attribute_add_to_mline(janus_sdp_mline_find(answer, JANUS_SDP_AUDIO), a);
				}
				/* Let's overwrite a couple o= fields, in case this is a renegotiation */
				answer->o_sessid = session->sdp_sessid;
				answer->o_version = session->sdp_version;
			} else if(generate_offer) {
				/* We need to generate an offer ourselves */
				int pt = 100;
				if(participant->codec == JANUS_AUDIOCODEC_PCMU)
					pt = 0;
				else if(participant->codec == JANUS_AUDIOCODEC_PCMA)
					pt = 8;
				offer = janus_sdp_generate_offer(
					s_name, "1.1.1.1",
					JANUS_SDP_OA_MLINE, JANUS_SDP_AUDIO,
						JANUS_SDP_OA_CODEC, janus_audiocodec_name(participant->codec),
						JANUS_SDP_OA_PT, pt,
						JANUS_SDP_OA_FMTP, (participant->codec == JANUS_AUDIOCODEC_OPUS ? fmtp : NULL),
						JANUS_SDP_OA_DIRECTION, JANUS_SDP_SENDRECV,
						JANUS_SDP_OA_EXTENSION, JANUS_RTP_EXTMAP_MID, 1,
						JANUS_SDP_OA_EXTENSION, JANUS_RTP_EXTMAP_AUDIO_LEVEL, extmap_id,
					JANUS_SDP_OA_DONE);
				/* Let's overwrite a couple o= fields, in case this is a renegotiation */
				if(session->sdp_version == 1) {
					session->sdp_sessid = offer->o_sessid;
				} else {
					offer->o_sessid = session->sdp_sessid;
					offer->o_version = session->sdp_version;
				}
			}
			/* Was the audio level extension negotiated? */
			participant->extmap_id = 0;
			participant->dBov_level = 0;
			if(extmap_id > -1 && participant->room && participant->room->audiolevel_ext) {
				/* Add an extmap attribute too */
				participant->extmap_id = extmap_id;
				/* If there's a recording, add the extension there */
				janus_mutex_lock(&participant->rec_mutex);
				if(participant->arc != NULL)
					janus_recorder_add_extmap(participant->arc, participant->extmap_id, JANUS_RTP_EXTMAP_AUDIO_LEVEL);
				janus_mutex_unlock(&participant->rec_mutex);
			}
			/* Prepare the response */
			char *new_sdp = janus_sdp_write(answer ? answer : offer);
			janus_sdp_destroy(sdp);
			janus_sdp_destroy(answer ? answer : offer);
			json_t *jsep = json_pack("{ssss}", "type", type, "sdp", new_sdp);
			/* How long will the Janus core take to push the event? */
			g_atomic_int_set(&session->hangingup, 0);
			gint64 start = janus_get_monotonic_time();
			int res = gateway->push_event(msg->handle, &janus_audiobridge_plugin, msg->transaction, event, jsep);
			JANUS_LOG(LOG_VERB, "  >> Pushing event: %d (took %"SCNu64" us)\n", res, janus_get_monotonic_time()-start);
			json_decref(event);
			json_decref(jsep);
			g_free(new_sdp);
			if(res != JANUS_OK) {
				/* TODO Failed to negotiate? We should remove this participant */
			} else {
				/* We'll notify all other participants when the PeerConnection has been established */
			}
		}
		if(msg)
			janus_audiobridge_message_free(msg);
		msg = NULL;

		continue;

error:
		{
			/* Prepare JSON error event */
			json_t *event = json_object();
			json_object_set_new(event, "audiobridge", json_string("event"));
			json_object_set_new(event, "error_code", json_integer(error_code));
			json_object_set_new(event, "error", json_string(error_cause));
			int ret = gateway->push_event(msg->handle, &janus_audiobridge_plugin, msg->transaction, event, NULL);
			JANUS_LOG(LOG_VERB, "  >> Pushing event: %d (%s)\n", ret, janus_get_api_error(ret));
			json_decref(event);
			janus_audiobridge_message_free(msg);
		}
	}
	JANUS_LOG(LOG_VERB, "Leaving AudioBridge handler thread\n");
	return NULL;
}
static void janus_audiobridge_rec_add_wav_header(janus_audiobridge_room *audiobridge) {
	/* Do we need to record the mix? */
	char filename[255];
	gint64 now = janus_get_real_time();
	audiobridge->rec_start_time = now;
	if(audiobridge->record_file) {
		g_snprintf(filename, 255, "%s%s%s%s%s",
			audiobridge->record_dir ? audiobridge->record_dir : "",
			audiobridge->record_dir ? "/" : "",
			audiobridge->record_file,
			rec_tempext ? "." : "", rec_tempext ? rec_tempext : "");
	} else {
		g_snprintf(filename, 255, "%s%sjanus-audioroom-%s-%"SCNi64".wav%s%s",
			audiobridge->record_dir ? audiobridge->record_dir : "",
			audiobridge->record_dir ? "/" : "",
			audiobridge->room_id_str, now,
			rec_tempext ? "." : "", rec_tempext ? rec_tempext : "");
	}
	audiobridge->recording = fopen(filename, "wb");
	if(audiobridge->recording == NULL) {
		JANUS_LOG(LOG_WARN, "Recording requested, but could NOT open file %s for writing, giving up...\n", filename);
		g_atomic_int_set(&audiobridge->record, 0);
		g_atomic_int_set(&audiobridge->wav_header_added, 0);
		g_free(audiobridge->record_file);
		audiobridge->record_file = NULL;
	} else {
		JANUS_LOG(LOG_VERB, "Recording requested, opened file %s for writing\n", filename);
		/* Write WAV header */
		wav_header header = {
			{'R', 'I', 'F', 'F'},
			0,
			{'W', 'A', 'V', 'E'},
			{'f', 'm', 't', ' '},
			16,
			1,
			audiobridge->spatial_audio ? 2 : 1,
			audiobridge->sampling_rate,
			audiobridge->sampling_rate * 2 * (audiobridge->spatial_audio ? 2 : 1),
			2,
			16,
			{'d', 'a', 't', 'a'},
			0
		};
		if(fwrite(&header, 1, sizeof(header), audiobridge->recording) != sizeof(header)) {
			JANUS_LOG(LOG_ERR, "Error writing WAV header...\n");
		}
		fflush(audiobridge->recording);
		audiobridge->record_lastupdate = janus_get_monotonic_time();
		g_atomic_int_set(&audiobridge->wav_header_added, 1);
	}
}
static void janus_audiobridge_update_wav_header(janus_audiobridge_room *audiobridge) {
	/* Update the length in the header */
	if(audiobridge->recording == NULL)
		return;
	fseek(audiobridge->recording, 0, SEEK_END);
	long int size = ftell(audiobridge->recording);
	if(size >= 8) {
		size -= 8;
		fseek(audiobridge->recording, 4, SEEK_SET);
		fwrite(&size, sizeof(uint32_t), 1, audiobridge->recording);
		size += 8;
		fseek(audiobridge->recording, 40, SEEK_SET);
		fwrite(&size, sizeof(uint32_t), 1, audiobridge->recording);
		fflush(audiobridge->recording);
	}
	fclose(audiobridge->recording);
	audiobridge->recording = NULL;
	g_atomic_int_set(&audiobridge->wav_header_added, 0);

	char filename[255];
	if(audiobridge->record_file) {
		g_snprintf(filename, 255, "%s%s%s",
			audiobridge->record_dir ? audiobridge->record_dir : "",
			audiobridge->record_dir ? "/" : "",
			audiobridge->record_file);
	} else {
		g_snprintf(filename, 255, "%s%sjanus-audioroom-%s-%"SCNi64".wav",
			audiobridge->record_dir ? audiobridge->record_dir : "",
			audiobridge->record_dir ? "/" : "",
			audiobridge->room_id_str, audiobridge->rec_start_time);
	}
	if(rec_tempext) {
		/* We need to rename the file, to remove the temporary extension */
		char extfilename[255];
		if(audiobridge->record_file) {
			g_snprintf(extfilename, 255, "%s%s%s.%s",
				audiobridge->record_dir ? audiobridge->record_dir : "",
				audiobridge->record_dir ? "/" : "",
				audiobridge->record_file, rec_tempext);
		} else {
			g_snprintf(extfilename, 255, "%s%sjanus-audioroom-%s-%"SCNi64".wav.%s",
				audiobridge->record_dir ? audiobridge->record_dir : "",
				audiobridge->record_dir ? "/" : "",
				audiobridge->room_id_str,
				audiobridge->rec_start_time, rec_tempext);
		}
		if(rename(extfilename, filename) != 0) {
			JANUS_LOG(LOG_ERR, "Error renaming %s to %s...\n", extfilename, filename);
		} else {
			JANUS_LOG(LOG_INFO, "Recording renamed: %s\n", filename);
		}
	}
	/* Also notify event handlers */
	if(notify_events && gateway->events_is_enabled()) {
		json_t *info = json_object();
		json_object_set_new(info, "event", json_string("recordingdone"));
		json_object_set_new(info, "room",
			string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
		json_object_set_new(info, "record_file", json_string(filename));
		gateway->notify_event(&janus_audiobridge_plugin, NULL, info);
	}
}

/* Thread to mix the contributions from all participants */
static void *janus_audiobridge_mixer_thread(void *data) {
	JANUS_LOG(LOG_VERB, "Audio bridge thread starting...\n");
	janus_audiobridge_room *audiobridge = (janus_audiobridge_room *)data;
	if(!audiobridge) {
		JANUS_LOG(LOG_ERR, "Invalid room!\n");
		return NULL;
	}
	JANUS_LOG(LOG_VERB, "Thread is for mixing room %s (%s) at rate %"SCNu32"...\n",
		audiobridge->room_id_str, audiobridge->room_name, audiobridge->sampling_rate);

	/* Buffer (we allocate assuming 48kHz, although we'll likely use less than that) */
	int samples = audiobridge->sampling_rate/50;
	if(audiobridge->spatial_audio)
		samples = samples*2;
	opus_int32 buffer[audiobridge->spatial_audio ? OPUS_SAMPLES*2 : OPUS_SAMPLES],
		sumBuffer[audiobridge->spatial_audio ? OPUS_SAMPLES*2 : OPUS_SAMPLES];
	opus_int16 outBuffer[audiobridge->spatial_audio ? OPUS_SAMPLES*2 : OPUS_SAMPLES],
		resampled[audiobridge->spatial_audio ? OPUS_SAMPLES*2 : OPUS_SAMPLES], *curBuffer = NULL;
	memset(buffer, 0, OPUS_SAMPLES*(audiobridge->spatial_audio ? 8 : 4));
	memset(sumBuffer, 0, OPUS_SAMPLES*(audiobridge->spatial_audio ? 8 : 4));
	memset(outBuffer, 0, OPUS_SAMPLES*(audiobridge->spatial_audio ? 4 : 2));
	memset(resampled, 0, OPUS_SAMPLES*(audiobridge->spatial_audio ? 4 : 2));

	/* In case forwarding groups are enabled, we need additional buffers */
	uint groups_num = audiobridge->groups ? g_hash_table_size(audiobridge->groups) : 0, index = 0;
	opus_int32 *groupBuffers = NULL;
	uint32_t groupBufferSize = 0, groupBuffersSize = 0;
	OpusEncoder **groupEncoders = NULL;
	if(groups_num > 0) {
		/* Create buffers */
		groupBufferSize = (audiobridge->spatial_audio ? OPUS_SAMPLES*2 : OPUS_SAMPLES) * sizeof(opus_int32);
		groupBuffersSize = groups_num * groupBufferSize;
		groupBuffers = g_malloc(groupBuffersSize);
		groupEncoders = g_malloc(groups_num * sizeof(OpusEncoder *));
		/* Create separate encoders */
		for(index=0; index<groups_num; index++) {
			int error = 0;
			OpusEncoder *rtp_encoder = opus_encoder_create(audiobridge->sampling_rate,
				audiobridge->spatial_audio ? 2 : 1, OPUS_APPLICATION_VOIP, &error);
			if(audiobridge->sampling_rate == 8000) {
				opus_encoder_ctl(rtp_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_NARROWBAND));
			} else if(audiobridge->sampling_rate == 12000) {
				opus_encoder_ctl(rtp_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_MEDIUMBAND));
			} else if(audiobridge->sampling_rate == 16000) {
				opus_encoder_ctl(rtp_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
			} else if(audiobridge->sampling_rate == 24000) {
				opus_encoder_ctl(rtp_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_SUPERWIDEBAND));
			} else if(audiobridge->sampling_rate == 48000) {
				opus_encoder_ctl(rtp_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
			} else {
				JANUS_LOG(LOG_WARN, "Unsupported sampling rate %d, setting 16kHz\n", audiobridge->sampling_rate);
				opus_encoder_ctl(rtp_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
			}
			/* Check if we need FEC */
			if(audiobridge->default_expectedloss > 0) {
				opus_encoder_ctl(rtp_encoder, OPUS_SET_INBAND_FEC(TRUE));
				opus_encoder_ctl(rtp_encoder, OPUS_SET_PACKET_LOSS_PERC(audiobridge->default_expectedloss));
			}
			groupEncoders[index] = rtp_encoder;
		}
	}

	/* Base RTP packets, in case there are forwarders involved */
	gboolean have_opus[JANUS_AUDIOBRIDGE_MAX_GROUPS+1],
		have_alaw[JANUS_AUDIOBRIDGE_MAX_GROUPS+1],
		have_ulaw[JANUS_AUDIOBRIDGE_MAX_GROUPS+1];
	unsigned char *rtpbuffer = g_malloc0(1500 * (groups_num+1));
	janus_rtp_header *rtph = NULL;
	/* In case we need G.711 forwarders */
	uint8_t *rtpalaw = g_malloc0((12+G711_SAMPLES) * (groups_num+1)),
			*rtpulaw = g_malloc0((12+G711_SAMPLES) * (groups_num+1));

	/* Timer */
	struct timeval now, before;
	gettimeofday(&before, NULL);
	now.tv_sec = before.tv_sec;
	now.tv_usec = before.tv_usec;
	time_t passed, d_s, d_us;

	/* RTP */
	guint16 seq = 0;
	guint32 ts = 0;
	/* SRTP buffer, if needed */
	char sbuf[1500];

	g_atomic_int_set(&audiobridge->wav_header_added, 0);
	/* Loop */
	int i=0;
	int count = 0, rf_count = 0, pf_count = 0, prev_count = 0;
	int lgain = 0, rgain = 0, diff = 0;
	while(!g_atomic_int_get(&stopping) && !g_atomic_int_get(&audiobridge->destroyed)) {
		/* See if it's time to prepare a frame */
		gettimeofday(&now, NULL);
		d_s = now.tv_sec - before.tv_sec;
		d_us = now.tv_usec - before.tv_usec;
		if(d_us < 0) {
			d_us += 1000000;
			--d_s;
		}
		passed = d_s*1000000 + d_us;
		if(passed < 15000) {	/* Let's wait about 15ms at max */
			g_usleep(5000);
			continue;
		}
		/* If we're recording to a wav file, update the info */
		if(g_atomic_int_get(&audiobridge->record) && !g_atomic_int_get(&audiobridge->wav_header_added)) {
			JANUS_LOG(LOG_VERB, "Adding WAV header for recording %s (%s)...\n", audiobridge->room_id_str, audiobridge->room_name);
			janus_audiobridge_rec_add_wav_header(audiobridge);
		}
		if(!g_atomic_int_get(&audiobridge->record) && g_atomic_int_get(&audiobridge->wav_header_added)) {
			JANUS_LOG(LOG_VERB, "Updating WAV header for recording %s (%s)...\n", audiobridge->room_id_str, audiobridge->room_name);
			janus_audiobridge_update_wav_header(audiobridge);
		}
		/* Update the reference time */
		before.tv_usec += 20000;
		if(before.tv_usec > 1000000) {
			before.tv_sec++;
			before.tv_usec -= 1000000;
		}
		/* Do we need to mix at all? */
		janus_mutex_lock_nodebug(&audiobridge->mutex);
		count = g_hash_table_size(audiobridge->participants);
		rf_count = g_hash_table_size(audiobridge->rtp_forwarders);
		pf_count = g_hash_table_size(audiobridge->anncs);
		if((count+rf_count+pf_count) == 0) {
			janus_mutex_unlock_nodebug(&audiobridge->mutex);
			/* No participant and RTP forwarders, do nothing */
			if(prev_count > 0) {
				JANUS_LOG(LOG_INFO, "Last user/forwarder/file just left room %s, going idle...\n", audiobridge->room_id_str);
				prev_count = 0;
			}
			continue;
		}
		if(prev_count == 0) {
			JANUS_LOG(LOG_INFO, "First user/forwarder/file just joined room %s, waking it up...\n", audiobridge->room_id_str);
		}
		prev_count = count+rf_count+pf_count;
		/* Update RTP header information */
		seq++;
		ts += OPUS_SAMPLES;
		/* Mix all contributions */
		GList *participants_list = g_hash_table_get_values(audiobridge->participants);
		/* Add a reference to all these participants, in case some leave while we're mixing */
		GList *ps = participants_list;
		while(ps) {
			janus_audiobridge_participant *p = (janus_audiobridge_participant *)ps->data;
			janus_refcount_increase(&p->ref);
			ps = ps->next;
		}
		/* Do the same for announcements */
		GList *anncs_list = g_hash_table_get_values(audiobridge->anncs);
		ps = anncs_list;
		while(ps) {
			janus_audiobridge_participant *annc = (janus_audiobridge_participant *)ps->data;
			janus_refcount_increase(&annc->ref);
			ps = ps->next;
		}
		janus_mutex_unlock_nodebug(&audiobridge->mutex);
		for(i=0; i<samples; i++)
			buffer[i] = 0;
		if(groups_num > 0)
			memset(groupBuffers, 0, groupBuffersSize);
		ps = participants_list;
		while(ps) {
			janus_audiobridge_participant *p = (janus_audiobridge_participant *)ps->data;
			janus_mutex_lock(&p->qmutex);
			if(g_atomic_int_get(&p->destroyed) || !p->session || !g_atomic_int_get(&p->session->started) || !g_atomic_int_get(&p->active) || p->muted || p->prebuffering || !p->inbuf) {
				janus_mutex_unlock(&p->qmutex);
				ps = ps->next;
				continue;
			}
			GList *peek = g_list_first(p->inbuf);
			janus_audiobridge_rtp_relay_packet *pkt = (janus_audiobridge_rtp_relay_packet *)(peek ? peek->data : NULL);
			if(pkt != NULL && !pkt->silence) {
				if(p->codec != JANUS_AUDIOCODEC_OPUS && audiobridge->sampling_rate != 8000) {
					/* Upsample this to whatever the mixer needs */
					pkt->length = janus_audiobridge_resample((opus_int16 *)pkt->data, 160, 8000, resampled, audiobridge->sampling_rate);
					if(pkt->length == 0) {
						JANUS_LOG(LOG_WARN, "[G.711] Error upsampling to %d, skipping audio packet\n", audiobridge->sampling_rate);
						janus_mutex_unlock(&p->qmutex);
						ps = ps->next;
						continue;
					}
					memcpy(pkt->data, resampled, pkt->length*2);
				}
				curBuffer = (opus_int16 *)pkt->data;
				if(groups_num == 0) {
					/* Add to the main mix */
					if(!p->stereo) {
						for(i=0; i<samples; i++) {
							if(p->volume_gain == 100) {
								buffer[i] += curBuffer[i];
							} else {
								buffer[i] += (curBuffer[i]*p->volume_gain)/100;
							}
						}
					} else {
						diff = 50 - p->spatial_position;
						lgain = 50 + diff;
						rgain = 50 - diff;
						for(i=0; i<samples; i++) {
							if(i%2 == 0) {
								if(lgain == 100) {
									if(p->volume_gain == 100) {
										buffer[i] += curBuffer[i];
									} else {
										buffer[i] += (curBuffer[i]*p->volume_gain)/100;
									}
								} else {
									if(p->volume_gain == 100) {
										buffer[i] += (curBuffer[i]*lgain)/100;
									} else {
										buffer[i] += (((curBuffer[i]*lgain)/100)*p->volume_gain)/100;
									}
								}
							} else {
								if(rgain == 100) {
									if(p->volume_gain == 100) {
										buffer[i] += curBuffer[i];
									} else {
										buffer[i] += (curBuffer[i]*p->volume_gain)/100;
									}
								} else {
									if(p->volume_gain == 100) {
										buffer[i] += (curBuffer[i]*rgain)/100;
									} else {
										buffer[i] += (((curBuffer[i]*rgain)/100)*p->volume_gain)/100;
									}
								}
							}
						}
					}
				} else {
					/* Add to the group submix */
					int index = p->group-1;
					if(!p->stereo) {
						for(i=0; i<samples; i++) {
							if(p->volume_gain == 100) {
								*(groupBuffers + index*samples + i) += curBuffer[i];
							} else {
								*(groupBuffers + index*samples + i) += (curBuffer[i]*p->volume_gain)/100;
							}
						}
					} else {
						diff = 50 - p->spatial_position;
						lgain = 50 + diff;
						rgain = 50 - diff;
						for(i=0; i<samples; i++) {
							if(i%2 == 0) {
								if(lgain == 100) {
									if(p->volume_gain == 100) {
										*(groupBuffers + index*samples + i) += curBuffer[i];
									} else {
										*(groupBuffers + index*samples + i) += (curBuffer[i]*p->volume_gain)/100;
									}
								} else {
									if(p->volume_gain == 100) {
										*(groupBuffers + index*samples + i) += (curBuffer[i]*lgain)/100;
									} else {
										*(groupBuffers + index*samples + i) += (((curBuffer[i]*lgain)/100)*p->volume_gain)/100;
									}
								}
							} else {
								if(rgain == 100) {
									if(p->volume_gain == 100) {
										*(groupBuffers + index*samples + i) += curBuffer[i];
									} else {
										*(groupBuffers + index*samples + i) += (curBuffer[i]*p->volume_gain)/100;
									}
								} else {
									if(p->volume_gain == 100) {
										*(groupBuffers + index*samples + i) += (curBuffer[i]*rgain)/100;
									} else {
										*(groupBuffers + index*samples + i) += (((curBuffer[i]*rgain)/100)*p->volume_gain)/100;
									}
								}
							}
						}
					}
				}
			}
			janus_mutex_unlock(&p->qmutex);
			ps = ps->next;
		}
#ifdef HAVE_LIBOGG
		/* If there are announcements playing, mix those too */
		if(anncs_list != NULL) {
			ps = anncs_list;
			while(ps) {
				janus_audiobridge_participant *p = (janus_audiobridge_participant *)ps->data;
				if(p->annc == NULL || g_atomic_int_get(&p->destroyed)) {
					ps = ps->next;
					continue;
				}
				int read = janus_audiobridge_file_read(p->annc, p->decoder, resampled, sizeof(resampled));
				if(read <= 0) {
					/* Playback over or broken */
					if(p->annc->started) {
						/* Send a notification that this announcement is over */
						JANUS_LOG(LOG_INFO, "[%s] Announcement stopped (%s)\n", audiobridge->room_id_str, p->user_id_str);
						janus_mutex_lock_nodebug(&audiobridge->mutex);
						json_t *event = json_object();
						json_object_set_new(event, "audiobridge", json_string("announcement-stopped"));
						json_object_set_new(event, "room",
							string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
						json_object_set_new(event, "file_id", json_string(p->user_id_str));
						janus_audiobridge_notify_participants(p, event, TRUE);
						json_decref(event);
						/* Also notify event handlers */
						if(notify_events && gateway->events_is_enabled()) {
							json_t *info = json_object();
							json_object_set_new(info, "event", json_string("announcement-stopped"));
							json_object_set_new(info, "room",
								string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
							json_object_set_new(info, "file_id", json_string(p->user_id_str));
							gateway->notify_event(&janus_audiobridge_plugin, NULL, info);
						}
						/* Remove the announcement */
						g_hash_table_remove(audiobridge->anncs, p->user_id_str);
						janus_mutex_unlock_nodebug(&audiobridge->mutex);
					}
					ps = ps->next;
					continue;
				}
				if(!p->annc->started) {
					/* This announcement just started, notify the participants */
					p->annc->started = TRUE;
					JANUS_LOG(LOG_INFO, "[%s] Announcement started (%s)\n", audiobridge->room_id_str, p->user_id_str);
					janus_mutex_lock_nodebug(&audiobridge->mutex);
					json_t *event = json_object();
					json_object_set_new(event, "audiobridge", json_string("announcement-started"));
					json_object_set_new(event, "room",
						string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
					json_object_set_new(event, "file_id", json_string(p->user_id_str));
					janus_audiobridge_notify_participants(p, event, TRUE);
					json_decref(event);
					janus_mutex_unlock_nodebug(&audiobridge->mutex);
					/* Also notify event handlers */
					if(notify_events && gateway->events_is_enabled()) {
						json_t *info = json_object();
						json_object_set_new(info, "event", json_string("announcement-started"));
						json_object_set_new(info, "room",
							string_ids ? json_string(audiobridge->room_id_str) : json_integer(audiobridge->room_id));
						json_object_set_new(info, "file_id", json_string(p->user_id_str));
						gateway->notify_event(&janus_audiobridge_plugin, NULL, info);
					}
				}
				if(groups_num == 0) {
					/* Add to the main mix */
					for(i=0; i<samples; i++) {
						if(p->volume_gain == 100) {
							buffer[i] += resampled[i];
						} else {
							buffer[i] += (resampled[i]*p->volume_gain)/100;
						}
					}
				} else {
					/* Add to the group submix */
					index = p->group-1;
					for(i=0; i<samples; i++) {
						if(p->volume_gain == 100) {
							*(groupBuffers + index*samples + i) += resampled[i];
						} else {
							*(groupBuffers + index*samples + i) += (resampled[i]*p->volume_gain)/100;
						}
					}
				}
				ps = ps->next;
			}
			g_list_free_full(anncs_list, (GDestroyNotify)janus_audiobridge_participant_unref);
		}
#endif
		/* If groups are in use, put them together in the main mix */
		if(groups_num > 0) {
			/* Mix all submixes */
			for(index=0; index<groups_num; index++) {
				for(i=0; i<samples; i++)
					buffer[i] += *(groupBuffers + index*samples + i);
			}
		}
		/* Are we recording the mix? (only do it if there's someone in, though...) */
		if(audiobridge->recording != NULL && g_list_length(participants_list) > 0) {
			for(i=0; i<samples; i++) {
				/* FIXME Smoothen/Normalize instead of truncating? */
				outBuffer[i] = buffer[i];
			}
			fwrite(outBuffer, sizeof(opus_int16), samples, audiobridge->recording);
			/* Every 5 seconds we update the wav header */
			gint64 now = janus_get_monotonic_time();
			if(now - audiobridge->record_lastupdate >= 5*G_USEC_PER_SEC) {
				audiobridge->record_lastupdate = now;
				/* Update the length in the header */
				fseek(audiobridge->recording, 0, SEEK_END);
				long int size = ftell(audiobridge->recording);
				if(size >= 8) {
					size -= 8;
					fseek(audiobridge->recording, 4, SEEK_SET);
					fwrite(&size, sizeof(uint32_t), 1, audiobridge->recording);
					size += 8;
					fseek(audiobridge->recording, 40, SEEK_SET);
					fwrite(&size, sizeof(uint32_t), 1, audiobridge->recording);
					fflush(audiobridge->recording);
					fseek(audiobridge->recording, 0, SEEK_END);
				}
			}
		}
		/* Send proper packet to each participant (remove own contribution) */
		ps = participants_list;
		while(ps) {
			janus_audiobridge_participant *p = (janus_audiobridge_participant *)ps->data;
			if(g_atomic_int_get(&p->destroyed) || !p->session || !g_atomic_int_get(&p->session->started)) {
				janus_refcount_decrease(&p->ref);
				ps = ps->next;
				continue;
			}
			janus_audiobridge_rtp_relay_packet *pkt = NULL;
			janus_mutex_lock(&p->qmutex);
			if(g_atomic_int_get(&p->active) && !p->muted && !p->prebuffering && p->inbuf) {
				GList *first = g_list_first(p->inbuf);
				pkt = (janus_audiobridge_rtp_relay_packet *)(first ? first->data : NULL);
				p->inbuf = g_list_delete_link(p->inbuf, first);
			}
			janus_mutex_unlock(&p->qmutex);
			/* Remove the participant's own contribution */
			curBuffer = (opus_int16 *)((pkt && pkt->length && !pkt->silence) ? pkt->data : NULL);
			if(!p->stereo) {
				for(i=0; i<samples; i++) {
					if(p->volume_gain == 100)
						sumBuffer[i] = buffer[i] - (curBuffer ? (curBuffer[i]) : 0);
					else
						sumBuffer[i] = buffer[i] - (curBuffer ? (curBuffer[i]*p->volume_gain)/100 : 0);
				}
			} else {
				diff = 50 - p->spatial_position;
				lgain = 50 + diff;
				rgain = 50 - diff;
				for(i=0; i<samples; i++) {
					if(i%2 == 0) {
						if(lgain == 100) {
							sumBuffer[i] = buffer[i] - (curBuffer ? (curBuffer[i]) : 0);
						} else {
							sumBuffer[i] = buffer[i] - (curBuffer ? (curBuffer[i]*lgain)/100 : 0);
						}
					} else {
						if(rgain == 100) {
							sumBuffer[i] = buffer[i] - (curBuffer ? (curBuffer[i]) : 0);
						} else {
							sumBuffer[i] = buffer[i] - (curBuffer ? (curBuffer[i]*rgain)/100 : 0);
						}
					}
				}
			}
			for(i=0; i<samples; i++)
				/* FIXME Smoothen/Normalize instead of truncating? */
				outBuffer[i] = sumBuffer[i];
			/* Enqueue this mixed frame for encoding in the participant thread */
			janus_audiobridge_rtp_relay_packet *mixedpkt = g_malloc(sizeof(janus_audiobridge_rtp_relay_packet));
			mixedpkt->data = g_malloc(samples*2);
			if(p->codec != JANUS_AUDIOCODEC_OPUS && audiobridge->sampling_rate != 8000) {
				/* Downsample this from whatever the mixer uses */
				i = janus_audiobridge_resample(outBuffer, samples, audiobridge->sampling_rate, (int16_t *)mixedpkt->data, 8000);
				if(i == 0) {
					JANUS_LOG(LOG_WARN, "[G.711] Error downsampling from %d, skipping audio packet\n", audiobridge->sampling_rate);
					g_free(mixedpkt->data);
					g_free(mixedpkt);
					janus_refcount_decrease(&p->ref);
					ps = ps->next;
					continue;
				}
			} else {
				/* Just copy */
				memcpy(mixedpkt->data, outBuffer, samples*2);
			}
			mixedpkt->length = samples;	/* We set the number of samples here, not the data length */
			mixedpkt->timestamp = ts;
			mixedpkt->seq_number = seq;
			mixedpkt->ssrc = audiobridge->room_ssrc;
			mixedpkt->silence = FALSE;
			g_async_queue_push(p->outbuf, mixedpkt);
			if(pkt) {
				g_free(pkt->data);
				pkt->data = NULL;
				g_free(pkt);
				pkt = NULL;
			}
			janus_refcount_decrease(&p->ref);
			ps = ps->next;
		}
		g_list_free(participants_list);
		/* Forward the mixed packet as RTP to any RTP forwarder that may be listening */
		janus_mutex_lock(&audiobridge->rtp_mutex);
		if(g_hash_table_size(audiobridge->rtp_forwarders) > 0 && audiobridge->rtp_encoder) {
			/* If the room is empty, check if there's any RTP forwarder with an "always on" option */
			gboolean go_on = FALSE;
			if(count == 0 && pf_count == 0) {
				GHashTableIter iter;
				gpointer value;
				g_hash_table_iter_init(&iter, audiobridge->rtp_forwarders);
				while(g_hash_table_iter_next(&iter, NULL, &value)) {
					janus_audiobridge_rtp_forwarder *forwarder = (janus_audiobridge_rtp_forwarder *)value;
					if(forwarder->always_on) {
						go_on = TRUE;
						break;
					}
				}
			} else {
				go_on = TRUE;
			}
			if(go_on) {
				/* By default, let's send the mixed frame to everybody */
				if(groups_num == 0) {
					for(i=0; i<samples; i++)
						outBuffer[i] = buffer[i];
					have_opus[0] = FALSE;
					have_alaw[0] = FALSE;
					have_ulaw[0] = FALSE;
				} else {
					for(index=0; index <= groups_num; index++) {
						have_opus[index] = FALSE;
						have_alaw[index] = FALSE;
						have_ulaw[index] = FALSE;
					}
				}
				GHashTableIter iter;
				gpointer key, value;
				g_hash_table_iter_init(&iter, audiobridge->rtp_forwarders);
				opus_int32 length = 0;
				while(audiobridge->rtp_udp_sock > 0 && g_hash_table_iter_next(&iter, &key, &value)) {
					guint32 stream_id = GPOINTER_TO_UINT(key);
					janus_audiobridge_rtp_forwarder *forwarder = (janus_audiobridge_rtp_forwarder *)value;
					if(count == 0 && pf_count == 0 && !forwarder->always_on)
						continue;
					/* Check if we're forwarding the main mix or a specific group */
					if(groups_num > 0) {
						if(forwarder->group == 0) {
							/* We're forwarding the main mix */
							for(i=0; i<samples; i++)
								outBuffer[i] = buffer[i];
						} else {
							/* We're forwarding a group mix */
							index = forwarder->group-1;
							for(i=0; i<samples; i++)
								outBuffer[i] = *(groupBuffers + index*samples + i);
						}
					}
					if(forwarder->codec == JANUS_AUDIOCODEC_OPUS) {
						/* This is an Opus forwarder, check if we have a version for that already */
						if(!have_opus[forwarder->group]) {
							/* We don't, encode now */
							OpusEncoder *rtp_encoder = (forwarder->group == 0 ? audiobridge->rtp_encoder : groupEncoders[forwarder->group-1]);
							length = opus_encode(rtp_encoder, outBuffer,
								audiobridge->spatial_audio ? samples/2 : samples,
								rtpbuffer + forwarder->group*1500 + 12, 1500-12);
							if(length < 0) {
								JANUS_LOG(LOG_ERR, "[Opus] Ops! got an error encoding the Opus frame: %d (%s)\n", length, opus_strerror(length));
								continue;
							}
							have_opus[forwarder->group] = TRUE;
						}
						rtph = (janus_rtp_header *)(rtpbuffer + forwarder->group*1500);
						rtph->version = 2;
					} else if(forwarder->codec == JANUS_AUDIOCODEC_PCMA || forwarder->codec == JANUS_AUDIOCODEC_PCMU) {
						/* This is a G.711 forwarder, check if we have a version for that already */
						if((forwarder->codec == JANUS_AUDIOCODEC_PCMA && !have_alaw[forwarder->group]) ||
								(forwarder->codec == JANUS_AUDIOCODEC_PCMU && !have_ulaw[forwarder->group])) {
							/* We don't, encode now */
							if(audiobridge->sampling_rate != 8000) {
								/* Downsample this from whatever the mixer uses */
								i = janus_audiobridge_resample(outBuffer, samples, audiobridge->sampling_rate, resampled, 8000);
								if(i == 0) {
									JANUS_LOG(LOG_WARN, "[G.711] Error downsampling from %d, skipping audio packet\n", audiobridge->sampling_rate);
									continue;
								}
							} else {
								/* Just copy */
								memcpy(resampled, outBuffer, samples*2);
							}
							int i = 0;
							if(forwarder->codec == JANUS_AUDIOCODEC_PCMA) {
								uint8_t *rtpalaw_buffer = rtpalaw + forwarder->group*G711_SAMPLES + 12;
								for(i=0; i<160; i++)
									rtpalaw_buffer[i] = janus_audiobridge_g711_alaw_encode(resampled[i]);
								have_alaw[forwarder->group] = TRUE;
							} else {
								uint8_t *rtpulaw_buffer = rtpulaw + forwarder->group*G711_SAMPLES + 12;
								for(i=0; i<160; i++)
									rtpulaw_buffer[i] = janus_audiobridge_g711_ulaw_encode(resampled[i]);
								have_ulaw[forwarder->group] = TRUE;
							}
						}
						rtph = (janus_rtp_header *)(forwarder->codec == JANUS_AUDIOCODEC_PCMA ?
							(rtpalaw + forwarder->group*G711_SAMPLES) : (rtpulaw + forwarder->group*G711_SAMPLES));
						rtph->version = 2;
						length = 160;
					}
					/* Update header */
					rtph->type = forwarder->payload_type;
					rtph->ssrc = htonl(forwarder->ssrc ? forwarder->ssrc : stream_id);
					forwarder->seq_number++;
					rtph->seq_number = htons(forwarder->seq_number);
					forwarder->timestamp += (forwarder->codec == JANUS_AUDIOCODEC_OPUS ? OPUS_SAMPLES : G711_SAMPLES);
					rtph->timestamp = htonl(forwarder->timestamp);
					/* Check if this packet needs to be encrypted */
					char *payload = (char *)rtph;
					int plen = length+12;
					if(forwarder->is_srtp) {
						memcpy(sbuf, payload, plen);
						int protected = plen;
						int res = srtp_protect(forwarder->srtp_ctx, sbuf, &protected);
						if(res != srtp_err_status_ok) {
							janus_rtp_header *header = (janus_rtp_header *)sbuf;
							guint32 timestamp = ntohl(header->timestamp);
							guint16 seq = ntohs(header->seq_number);
							JANUS_LOG(LOG_ERR, "Error encrypting RTP packet for room %s... %s (len=%d-->%d, ts=%"SCNu32", seq=%"SCNu16")...\n",
								audiobridge->room_id_str, janus_srtp_error_str(res), plen, protected, timestamp, seq);
						} else {
							payload = (char *)&sbuf;
							plen = protected;
						}
					}
					/* No encryption, send the RTP packet as it is */
					struct sockaddr *address = (forwarder->serv_addr.sin_family == AF_INET ?
						(struct sockaddr *)&forwarder->serv_addr : (struct sockaddr *)&forwarder->serv_addr6);
					size_t addrlen = (forwarder->serv_addr.sin_family == AF_INET ? sizeof(forwarder->serv_addr) : sizeof(forwarder->serv_addr6));
					if(sendto(audiobridge->rtp_udp_sock, payload, plen, 0, address, addrlen) < 0) {
						JANUS_LOG(LOG_HUGE, "Error forwarding mixed RTP packet for room %s... %s (len=%d)...\n",
							audiobridge->room_id_str, g_strerror(errno), plen);
					}
				}
			}
		}
		janus_mutex_unlock(&audiobridge->rtp_mutex);
	}
	/* Close the recording file */
	if(audiobridge->recording != NULL && g_atomic_int_get(&audiobridge->wav_header_added)) {
		JANUS_LOG(LOG_VERB, "Update wave header for recording %s (%s)...\n", audiobridge->room_id_str, audiobridge->room_name);
		janus_audiobridge_update_wav_header(audiobridge);
	}
	g_free(rtpbuffer);
	g_free(rtpalaw);
	g_free(rtpulaw);
	g_free(groupBuffers);
	if(groupEncoders) {
		for(index=0; index<groups_num; index++) {
			if(groupEncoders[index])
				opus_encoder_destroy(groupEncoders[index]);
		}
		g_free(groupEncoders);
	}
	JANUS_LOG(LOG_VERB, "Leaving mixer thread for room %s (%s)...\n", audiobridge->room_id_str, audiobridge->room_name);

	janus_refcount_decrease(&audiobridge->ref);

	return NULL;
}

/* Thread to encode a mixed frame and send it to a specific participant */
static void *janus_audiobridge_participant_thread(void *data) {
	JANUS_LOG(LOG_VERB, "AudioBridge Participant thread starting...\n");
	janus_audiobridge_participant *participant = (janus_audiobridge_participant *)data;
	if(!participant) {
		JANUS_LOG(LOG_ERR, "Invalid participant!\n");
		g_thread_unref(g_thread_self());
		return NULL;
	}
	JANUS_LOG(LOG_VERB, "Thread is for participant %s (%s)\n",
		participant->user_id_str, participant->display ? participant->display : "??");
	janus_audiobridge_session *session = participant->session;

	/* Output buffer */
	janus_audiobridge_rtp_relay_packet *outpkt = g_malloc(sizeof(janus_audiobridge_rtp_relay_packet));
	outpkt->data = g_malloc0(1500);
	outpkt->ssrc = 0;
	outpkt->timestamp = 0;
	outpkt->seq_number = 0;
	outpkt->length = 0;
	outpkt->silence = FALSE;
	uint8_t *payload = (uint8_t *)outpkt->data;

	janus_audiobridge_rtp_relay_packet *mixedpkt = NULL;

	/* Start working: check the outgoing queue for packets, then encode and send them */
	while(!g_atomic_int_get(&stopping) && g_atomic_int_get(&session->destroyed) == 0) {
		mixedpkt = g_async_queue_timeout_pop(participant->outbuf, 100000);
		if(mixedpkt != NULL && g_atomic_int_get(&session->destroyed) == 0 && g_atomic_int_get(&session->started)) {
			if(g_atomic_int_get(&participant->active) && (participant->codec == JANUS_AUDIOCODEC_PCMA ||
					participant->codec == JANUS_AUDIOCODEC_PCMU) && g_atomic_int_compare_and_exchange(&participant->encoding, 0, 1)) {
				/* Encode using G.711 */
				if(mixedpkt->length != 320) {
					/* TODO Resample */
				}
				int i = 0;
				opus_int16 *outBuffer = (opus_int16 *)mixedpkt->data;
				if(participant->codec == JANUS_AUDIOCODEC_PCMA) {
					/* A-law */
					for(i=0; i<160; i++)
						*(payload+12+i) = janus_audiobridge_g711_alaw_encode(outBuffer[i]);
				} else {
					/* Mu-Law */
					for(i=0; i<160; i++)
						*(payload+12+i) = janus_audiobridge_g711_ulaw_encode(outBuffer[i]);
				}
				g_atomic_int_set(&participant->encoding, 0);
				outpkt->length = 172;	/* Take the RTP header into consideration */
				/* Update RTP header */
				outpkt->data->version = 2;
				outpkt->data->markerbit = 0;	/* FIXME Should be 1 for the first packet */
				outpkt->data->seq_number = htons(mixedpkt->seq_number);
				outpkt->data->timestamp = htonl(mixedpkt->timestamp/6);
				outpkt->data->ssrc = htonl(mixedpkt->ssrc);	/* The Janus core will fix this anyway */
				/* Backup the actual timestamp and sequence number set by the audiobridge, in case a room is changed */
				outpkt->ssrc = mixedpkt->ssrc;
				outpkt->timestamp = mixedpkt->timestamp/6;
				outpkt->seq_number = mixedpkt->seq_number;
				janus_audiobridge_relay_rtp_packet(participant->session, outpkt);
			} else if(g_atomic_int_get(&participant->active) && participant->encoder &&
					g_atomic_int_compare_and_exchange(&participant->encoding, 0, 1)) {
				/* Encode raw frame to Opus */
				opus_int16 *outBuffer = (opus_int16 *)mixedpkt->data;
				outpkt->length = opus_encode(participant->encoder, outBuffer,
					participant->stereo ? mixedpkt->length/2 : mixedpkt->length, payload+12, 1500-12);
				g_atomic_int_set(&participant->encoding, 0);
				if(outpkt->length < 0) {
					JANUS_LOG(LOG_ERR, "[Opus] Ops! got an error encoding the Opus frame: %d (%s)\n", outpkt->length, opus_strerror(outpkt->length));
				} else {
					outpkt->length += 12;	/* Take the RTP header into consideration */
					/* Update RTP header */
					outpkt->data->version = 2;
					outpkt->data->markerbit = 0;	/* FIXME Should be 1 for the first packet */
					outpkt->data->seq_number = htons(mixedpkt->seq_number);
					outpkt->data->timestamp = htonl(mixedpkt->timestamp);
					outpkt->data->ssrc = htonl(mixedpkt->ssrc);	/* The Janus core will fix this anyway */
					/* Backup the actual timestamp and sequence number set by the audiobridge, in case a room is changed */
					outpkt->ssrc = mixedpkt->ssrc;
					outpkt->timestamp = mixedpkt->timestamp;
					outpkt->seq_number = mixedpkt->seq_number;
					janus_audiobridge_relay_rtp_packet(participant->session, outpkt);
				}
			}
		}
		if(mixedpkt) {
			g_free(mixedpkt->data);
			g_free(mixedpkt);
		}
	}
	/* We're done, get rid of the resources */
	g_free(outpkt->data);
	g_free(outpkt);
	JANUS_LOG(LOG_VERB, "AudioBridge Participant thread leaving...\n");

	janus_refcount_decrease(&participant->ref);
	janus_refcount_decrease(&session->ref);
	g_thread_unref(g_thread_self());
	return NULL;
}

static void janus_audiobridge_relay_rtp_packet(gpointer data, gpointer user_data) {
	janus_audiobridge_rtp_relay_packet *packet = (janus_audiobridge_rtp_relay_packet *)user_data;
	if(!packet || !packet->data || packet->length < 1) {
		JANUS_LOG(LOG_ERR, "Invalid packet...\n");
		return;
	}
	janus_audiobridge_session *session = (janus_audiobridge_session *)data;
	if(!session || !session->handle) {
		/* JANUS_LOG(LOG_ERR, "Invalid session...\n"); */
		return;
	}
	if(!g_atomic_int_get(&session->started)) {
		/* JANUS_LOG(LOG_ERR, "Streaming not started yet for this session...\n"); */
		return;
	}
	janus_audiobridge_participant *participant = session->participant;
	/* Set the payload type */
	if(participant->codec == JANUS_AUDIOCODEC_OPUS)
		packet->data->type = participant->opus_pt;
	else
		packet->data->type = (participant->codec == JANUS_AUDIOCODEC_PCMA ? 8 : 0);
	/* Fix sequence number and timestamp (room switching may be involved) */
	janus_rtp_header_update(packet->data, &participant->context, FALSE, 0);
	if(participant->plainrtp_media.audio_rtp_fd > 0) {
		if(participant->plainrtp_media.audio_ssrc == 0)
			participant->plainrtp_media.audio_ssrc = ntohl(packet->ssrc);
		if(participant->plainrtp_media.audio_send) {
			int ret = send(participant->plainrtp_media.audio_rtp_fd, (char *)packet->data, packet->length, 0);
			if(ret < 0) {
				JANUS_LOG(LOG_WARN, "Error sending plain RTP packet: %d (%s)\n", errno, g_strerror(errno));
			}
		}
	} else if(gateway != NULL) {
		janus_plugin_rtp rtp = { .mindex = -1, .video = FALSE, .buffer = (char *)packet->data, .length = packet->length };
		janus_plugin_rtp_extensions_reset(&rtp.extensions);
		/* FIXME Should we add our own audio level extension? */
		gateway->relay_rtp(session->handle, &rtp);
	}
	/* Restore the timestamp and sequence number to what the mixer set them to */
	packet->data->timestamp = htonl(packet->timestamp);
	packet->data->seq_number = htons(packet->seq_number);
}

/* Plain RTP stuff */
static void janus_audiobridge_plainrtp_media_cleanup(janus_audiobridge_plainrtp_media *media) {
	if(media == NULL)
		return;
	media->ready = FALSE;
	media->audio_pt = -1;
	media->audio_send = FALSE;
	if(media->audio_rtp_fd > 0)
		close(media->audio_rtp_fd);
	media->audio_rtp_fd = -1;
	media->local_audio_rtp_port = 0;
	media->remote_audio_rtp_port = 0;
	g_free(media->remote_audio_ip);
	media->remote_audio_ip = NULL;
	media->audio_ssrc = 0;
	media->audio_ssrc_peer = 0;
	if(media->pipefd[0] > 0)
		close(media->pipefd[0]);
	media->pipefd[0] = -1;
	if(media->pipefd[1] > 0)
		close(media->pipefd[1]);
	media->pipefd[1] = -1;
}
static int janus_audiobridge_plainrtp_allocate_port(janus_audiobridge_plainrtp_media *media) {
	/* Read global slider */
	uint16_t rtp_port_next = rtp_range_slider;
	uint16_t rtp_port_start = rtp_port_next;
	gboolean rtp_port_wrap = FALSE;
	/* Find a port we can use */
	int rtp_fd = -1;
	while(1) {
		if(rtp_port_wrap && rtp_port_next >= rtp_port_start) {
			/* Full range scanned */
			JANUS_LOG(LOG_ERR, "No ports available in range: %u -- %u\n", rtp_range_min, rtp_range_max);
			break;
		}
		if(rtp_fd == -1)
			rtp_fd = socket(AF_INET6, SOCK_DGRAM, 0);
		if(rtp_fd == -1) {
			JANUS_LOG(LOG_ERR, "Error creating socket... %d (%s)\n", errno, g_strerror(errno));
			break;
		}
		int v6only = 0;
		if(setsockopt(rtp_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) != 0) {
			JANUS_LOG(LOG_ERR, "setsockopt on socket failed... %d (%s)\n", errno, g_strerror(errno));
			break;
		}
		int rtp_port = rtp_port_next;
		if((uint32_t)(rtp_port_next + 2UL) < rtp_range_max) {
			/* Advance to next value */
			rtp_port_next += 2;
		} else {
			rtp_port_next = rtp_range_min;
			rtp_port_wrap = TRUE;
		}
		struct sockaddr_in6 rtp_address = { 0 };
		rtp_address.sin6_family = AF_INET6;
		rtp_address.sin6_port = htons(rtp_port);
		rtp_address.sin6_addr = in6addr_any;
		if(bind(rtp_fd, (struct sockaddr *)(&rtp_address), sizeof(rtp_address)) < 0) {
			/* rtp_fd still unbound, reuse it in the next iteration */
		} else {
			media->audio_rtp_fd = rtp_fd;
			media->local_audio_rtp_port = rtp_port;
			rtp_range_slider = rtp_port_next;		/* Update global slider */
			return 0;
		}
	}
	if(rtp_fd != -1) {
		close(rtp_fd);
	}
	return -1;
}
/* Thread to relay RTP/RTCP frames coming from the peer */
static void *janus_audiobridge_plainrtp_relay_thread(void *data) {
	janus_audiobridge_participant *participant = (janus_audiobridge_participant *)data;
	if(!participant) {
		JANUS_LOG(LOG_ERR, "Invalid participant!\n");
		g_thread_unref(g_thread_self());
		return NULL;
	}
	janus_audiobridge_session *session = participant->session;
	JANUS_LOG(LOG_INFO, "[AudioBridge-%p] Starting Plain RTP participant thread\n", session);

	/* File descriptors */
	socklen_t addrlen;
	struct sockaddr_storage remote = { 0 };
	int resfd = 0, bytes = 0, pollerrs = 0;
	struct pollfd fds[2];
	int pipe_fd = participant->plainrtp_media.pipefd[0];
	char buffer[1500];
	memset(buffer, 0, 1500);
	/* Loop */
	int num = 0;
	gboolean first = TRUE, goon = TRUE;

	/* Fake RTP packet */
	janus_plugin_rtp packet = { .video = FALSE, .buffer = buffer, .length = 0 };
	janus_plugin_rtp_extensions_reset(&packet.extensions);

	while(goon && session != NULL && !g_atomic_int_get(&session->destroyed) && !g_atomic_int_get(&session->hangingup)) {
		/* Prepare poll */
		num = 0;
		if(participant->plainrtp_media.audio_rtp_fd != -1) {
			fds[num].fd = participant->plainrtp_media.audio_rtp_fd;
			fds[num].events = POLLIN;
			fds[num].revents = 0;
			num++;
		}
		if(pipe_fd != -1) {
			fds[num].fd = pipe_fd;
			fds[num].events = POLLIN;
			fds[num].revents = 0;
			num++;
		}
		/* Wait for some data */
		resfd = poll(fds, num, 1000);
		if(resfd < 0) {
			if(errno == EINTR) {
				JANUS_LOG(LOG_HUGE, "[AudioBridge-%p] Got an EINTR (%s), ignoring...\n", session, g_strerror(errno));
				continue;
			}
			JANUS_LOG(LOG_ERR, "[AudioBridge-%p] Error polling...\n", session);
			JANUS_LOG(LOG_ERR, "[AudioBridge-%p]   -- %d (%s)\n", session, errno, g_strerror(errno));
			break;
		} else if(resfd == 0) {
			/* No data, keep going */
			continue;
		}
		if(session == NULL || g_atomic_int_get(&session->destroyed))
			break;
		int i = 0;
		for(i=0; i<num; i++) {
			if(fds[i].revents & (POLLERR | POLLHUP)) {
				/* Check the socket error */
				int error = 0;
				socklen_t errlen = sizeof(error);
				getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, (void *)&error, &errlen);
				if(error == 0) {
					/* Maybe not a breaking error after all? */
					continue;
				}
				/* FIXME Should we be more tolerant of ICMP errors on RTP sockets as well? */
				pollerrs++;
				if(pollerrs < 100)
					continue;
				JANUS_LOG(LOG_ERR, "[AudioBridge-%p] Too many errors polling %d (socket #%d): %s...\n", session,
					fds[i].fd, i, fds[i].revents & POLLERR ? "POLLERR" : "POLLHUP");
				JANUS_LOG(LOG_ERR, "[AudioBridge-%p]   -- %d (%s)\n", session, error, g_strerror(error));
				/* Can we assume it's pretty much over, after a POLLERR? */
				goon = FALSE;
				/* Close the channel */
				janus_audiobridge_hangup_media(session->handle);
				break;
			} else if(fds[i].revents & POLLIN) {
				if(pipe_fd != -1 && fds[i].fd == pipe_fd) {
					/* Poll interrupted for a reason, go on */
					int code = 0;
					(void)read(pipe_fd, &code, sizeof(int));
					break;
				}
				/* Got an RTP packet */
				addrlen = sizeof(remote);
				bytes = recvfrom(fds[i].fd, buffer, 1500, 0, (struct sockaddr *)&remote, &addrlen);
				if(bytes < 0) {
					/* Failed to read? */
					continue;
				}
				/* Audio RTP */
				if(!janus_is_rtp(buffer, bytes)) {
					/* Not an RTP packet? */
					continue;
				}
				/* If this is the first packet we receive, simulate a setup_media event */
				if(first) {
					first = FALSE;
					janus_audiobridge_setup_media(session->handle);
				}
				/* Handle the packet */
				pollerrs = 0;
				rtp_header *header = (rtp_header *)buffer;
				if(participant->plainrtp_media.audio_ssrc_peer != ntohl(header->ssrc)) {
					participant->plainrtp_media.audio_ssrc_peer = ntohl(header->ssrc);
					JANUS_LOG(LOG_VERB, "[AudioBridge-%p] Got peer audio SSRC: %"SCNu32"\n",
						session, participant->plainrtp_media.audio_ssrc_peer);
				}
				/* Check if the SSRC changed (e.g., after a re-INVITE or UPDATE) */
				janus_rtp_header_update(header, &participant->plainrtp_media.context, FALSE, 0);
				/* Handle as a WebRTC RTP packet */
				packet.length = bytes;
				janus_audiobridge_incoming_rtp(session->handle, &packet);
				continue;
			}
		}
	}
	/* Cleanup the media session */
	participant->plainrtp_media.thread = NULL;
	janus_mutex_lock(&participant->pmutex);
	janus_audiobridge_plainrtp_media_cleanup(&participant->plainrtp_media);
	janus_mutex_unlock(&participant->pmutex);
	/* Done */
	JANUS_LOG(LOG_INFO, "[AudioBridge-%p] Leaving Plain RTP participant thread\n", session);
	janus_refcount_decrease(&participant->ref);
	janus_refcount_decrease(&session->ref);
	g_thread_unref(g_thread_self());
	return NULL;
}
