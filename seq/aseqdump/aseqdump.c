/*
 * aseqdump.c - show the events received at an ALSA sequencer port
 *
 * Copyright (c) 2005 Clemens Ladisch <clemens@ladisch.de>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <poll.h>
#include <alsa/asoundlib.h>
#include "aconfig.h"
#include "version.h"
#ifdef HAVE_SEQ_CLIENT_INFO_GET_MIDI_VERSION
#include <alsa/ump_msg.h>
#endif

static snd_seq_t *seq;
static int port_count;
static snd_seq_addr_t *ports;
static volatile sig_atomic_t stop = 0;
#ifdef HAVE_SEQ_CLIENT_INFO_GET_MIDI_VERSION
static int ump_version;
#else
#define ump_version	0
#endif

/* prints an error message to stderr, and dies */
static void fatal(const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(EXIT_FAILURE);
}

/* memory allocation error handling */
static void check_mem(void *p)
{
	if (!p)
		fatal("Out of memory");
}

/* error handling for ALSA functions */
static void check_snd(const char *operation, int err)
{
	if (err < 0)
		fatal("Cannot %s - %s", operation, snd_strerror(err));
}

static void init_seq(void)
{
	int err;

	/* open sequencer */
	err = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0);
	check_snd("open sequencer", err);

	/* set our client's name */
	err = snd_seq_set_client_name(seq, "aseqdump");
	check_snd("set client name", err);
}

/* parses one or more port addresses from the string */
static void parse_ports(const char *arg)
{
	char *buf, *s, *port_name;
	int err;

	/* make a copy of the string because we're going to modify it */
	buf = strdup(arg);
	check_mem(buf);

	for (port_name = s = buf; s; port_name = s + 1) {
		/* Assume that ports are separated by commas.  We don't use
		 * spaces because those are valid in client names. */
		s = strchr(port_name, ',');
		if (s)
			*s = '\0';

		++port_count;
		ports = realloc(ports, port_count * sizeof(snd_seq_addr_t));
		check_mem(ports);

		err = snd_seq_parse_address(seq, &ports[port_count - 1], port_name);
		if (err < 0)
			fatal("Invalid port %s - %s", port_name, snd_strerror(err));
	}

	free(buf);
}

static void create_port(void)
{
	int err;

	err = snd_seq_create_simple_port(seq, "aseqdump",
					 SND_SEQ_PORT_CAP_WRITE |
					 SND_SEQ_PORT_CAP_SUBS_WRITE,
					 SND_SEQ_PORT_TYPE_MIDI_GENERIC |
					 SND_SEQ_PORT_TYPE_APPLICATION);
	check_snd("create port", err);
}

static void connect_ports(void)
{
	int i, err;

	for (i = 0; i < port_count; ++i) {
		err = snd_seq_connect_from(seq, 0, ports[i].client, ports[i].port);
		if (err < 0)
			fatal("Cannot connect from port %d:%d - %s",
			      ports[i].client, ports[i].port, snd_strerror(err));
	}
}

static void dump_event(const snd_seq_event_t *ev)
{
	printf("%3d:%-3d ", ev->source.client, ev->source.port);

	switch (ev->type) {
	case SND_SEQ_EVENT_NOTEON:
		if (ev->data.note.velocity)
			printf("Note on                %2d, note %d, velocity %d\n",
			       ev->data.note.channel, ev->data.note.note, ev->data.note.velocity);
		else
			printf("Note off               %2d, note %d\n",
			       ev->data.note.channel, ev->data.note.note);
		break;
	case SND_SEQ_EVENT_NOTEOFF:
		printf("Note off               %2d, note %d, velocity %d\n",
		       ev->data.note.channel, ev->data.note.note, ev->data.note.velocity);
		break;
	case SND_SEQ_EVENT_KEYPRESS:
		printf("Polyphonic aftertouch  %2d, note %d, value %d\n",
		       ev->data.note.channel, ev->data.note.note, ev->data.note.velocity);
		break;
	case SND_SEQ_EVENT_CONTROLLER:
		printf("Control change         %2d, controller %d, value %d\n",
		       ev->data.control.channel, ev->data.control.param, ev->data.control.value);
		break;
	case SND_SEQ_EVENT_PGMCHANGE:
		printf("Program change         %2d, program %d\n",
		       ev->data.control.channel, ev->data.control.value);
		break;
	case SND_SEQ_EVENT_CHANPRESS:
		printf("Channel aftertouch     %2d, value %d\n",
		       ev->data.control.channel, ev->data.control.value);
		break;
	case SND_SEQ_EVENT_PITCHBEND:
		printf("Pitch bend             %2d, value %d\n",
		       ev->data.control.channel, ev->data.control.value);
		break;
	case SND_SEQ_EVENT_CONTROL14:
		printf("Control change         %2d, controller %d, value %5d\n",
		       ev->data.control.channel, ev->data.control.param, ev->data.control.value);
		break;
	case SND_SEQ_EVENT_NONREGPARAM:
		printf("Non-reg. parameter     %2d, parameter %d, value %d\n",
		       ev->data.control.channel, ev->data.control.param, ev->data.control.value);
		break;
	case SND_SEQ_EVENT_REGPARAM:
		printf("Reg. parameter         %2d, parameter %d, value %d\n",
		       ev->data.control.channel, ev->data.control.param, ev->data.control.value);
		break;
	case SND_SEQ_EVENT_SONGPOS:
		printf("Song position pointer      value %d\n",
		       ev->data.control.value);
		break;
	case SND_SEQ_EVENT_SONGSEL:
		printf("Song select                value %d\n",
		       ev->data.control.value);
		break;
	case SND_SEQ_EVENT_QFRAME:
		printf("MTC quarter frame          %02xh\n",
		       ev->data.control.value);
		break;
	case SND_SEQ_EVENT_TIMESIGN:
		// XXX how is this encoded?
		printf("SMF time signature         (%#010x)\n",
		       ev->data.control.value);
		break;
	case SND_SEQ_EVENT_KEYSIGN:
		// XXX how is this encoded?
		printf("SMF key signature          (%#010x)\n",
		       ev->data.control.value);
		break;
	case SND_SEQ_EVENT_START:
		if (ev->source.client == SND_SEQ_CLIENT_SYSTEM &&
		    ev->source.port == SND_SEQ_PORT_SYSTEM_TIMER)
			printf("Queue start                queue %d\n",
			       ev->data.queue.queue);
		else
			printf("Start\n");
		break;
	case SND_SEQ_EVENT_CONTINUE:
		if (ev->source.client == SND_SEQ_CLIENT_SYSTEM &&
		    ev->source.port == SND_SEQ_PORT_SYSTEM_TIMER)
			printf("Queue continue             queue %d\n",
			       ev->data.queue.queue);
		else
			printf("Continue\n");
		break;
	case SND_SEQ_EVENT_STOP:
		if (ev->source.client == SND_SEQ_CLIENT_SYSTEM &&
		    ev->source.port == SND_SEQ_PORT_SYSTEM_TIMER)
			printf("Queue stop                 queue %d\n",
			       ev->data.queue.queue);
		else
			printf("Stop\n");
		break;
	case SND_SEQ_EVENT_SETPOS_TICK:
		printf("Set tick queue pos.        queue %d\n", ev->data.queue.queue);
		break;
	case SND_SEQ_EVENT_SETPOS_TIME:
		printf("Set rt queue pos.          queue %d\n", ev->data.queue.queue);
		break;
	case SND_SEQ_EVENT_TEMPO:
		printf("Set queue tempo            queue %d\n", ev->data.queue.queue);
		break;
	case SND_SEQ_EVENT_CLOCK:
		printf("Clock\n");
		break;
	case SND_SEQ_EVENT_TICK:
		printf("Tick\n");
		break;
	case SND_SEQ_EVENT_QUEUE_SKEW:
		printf("Queue timer skew           queue %d\n", ev->data.queue.queue);
		break;
	case SND_SEQ_EVENT_TUNE_REQUEST:
		printf("Tune request\n");
		break;
	case SND_SEQ_EVENT_RESET:
		printf("Reset\n");
		break;
	case SND_SEQ_EVENT_SENSING:
		printf("Active Sensing\n");
		break;
	case SND_SEQ_EVENT_CLIENT_START:
		printf("Client start               client %d\n",
		       ev->data.addr.client);
		break;
	case SND_SEQ_EVENT_CLIENT_EXIT:
		printf("Client exit                client %d\n",
		       ev->data.addr.client);
		break;
	case SND_SEQ_EVENT_CLIENT_CHANGE:
		printf("Client changed             client %d\n",
		       ev->data.addr.client);
		break;
	case SND_SEQ_EVENT_PORT_START:
		printf("Port start                 %d:%d\n",
		       ev->data.addr.client, ev->data.addr.port);
		break;
	case SND_SEQ_EVENT_PORT_EXIT:
		printf("Port exit                  %d:%d\n",
		       ev->data.addr.client, ev->data.addr.port);
		break;
	case SND_SEQ_EVENT_PORT_CHANGE:
		printf("Port changed               %d:%d\n",
		       ev->data.addr.client, ev->data.addr.port);
		break;
	case SND_SEQ_EVENT_PORT_SUBSCRIBED:
		printf("Port subscribed            %d:%d -> %d:%d\n",
		       ev->data.connect.sender.client, ev->data.connect.sender.port,
		       ev->data.connect.dest.client, ev->data.connect.dest.port);
		break;
	case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
		printf("Port unsubscribed          %d:%d -> %d:%d\n",
		       ev->data.connect.sender.client, ev->data.connect.sender.port,
		       ev->data.connect.dest.client, ev->data.connect.dest.port);
		break;
	case SND_SEQ_EVENT_SYSEX:
		{
			unsigned int i;
			printf("System exclusive          ");
			for (i = 0; i < ev->data.ext.len; ++i)
				printf(" %02X", ((unsigned char*)ev->data.ext.ptr)[i]);
			printf("\n");
		}
		break;
	default:
		printf("Event type %d\n",  ev->type);
	}
}

#ifdef HAVE_SEQ_CLIENT_INFO_GET_MIDI_VERSION
static void dump_ump_midi1_event(const unsigned int *ump)
{
	const snd_ump_msg_midi1_t *m = (const snd_ump_msg_midi1_t *)ump;
	unsigned char group = m->hdr.group;
	unsigned char status = m->hdr.status;
	unsigned char channel = m->hdr.channel;

	printf("Group %2d, ", group);
	switch (status) {
	case SND_UMP_MSG_NOTE_OFF:
		printf("Note off               %2d, note %d, velocity 0x%x",
		       channel, m->note_off.note, m->note_off.velocity);
		break;
	case SND_UMP_MSG_NOTE_ON:
		printf("Note on                %2d, note %d, velocity 0x%x",
		       channel, m->note_off.note, m->note_off.velocity);
		break;
	case SND_UMP_MSG_POLY_PRESSURE:
		printf("Poly pressure          %2d, note %d, value 0x%x",
		       channel, m->poly_pressure.note, m->poly_pressure.data);
		break;
	case SND_UMP_MSG_CONTROL_CHANGE:
		printf("Control change         %2d, controller %d, value 0x%x",
		       channel, m->control_change.index, m->control_change.data);
		break;
	case SND_UMP_MSG_PROGRAM_CHANGE:
		printf("Program change         %2d, program %d",
		       channel, m->program_change.program);
	case SND_UMP_MSG_CHANNEL_PRESSURE:
		printf("Channel pressure       %2d, value 0x%x",
		       channel, m->channel_pressure.data);
		break;
	case SND_UMP_MSG_PITCHBEND:
		printf("Pitchbend              %2d, value 0x%x",
		       channel, (m->pitchbend.data_msb << 7) | m->pitchbend.data_lsb);
		break;
	default:
		printf("UMP MIDI1 event: status = %d, channel = %d, 0x%08x",
		       status, channel, *ump);
		break;
	}
	printf("\n");
}

static void dump_ump_midi2_event(const unsigned int *ump)
{
	const snd_ump_msg_midi2_t *m = (const snd_ump_msg_midi2_t *)ump;
	unsigned char group = m->hdr.group;
	unsigned char status = m->hdr.status;
	unsigned char channel = m->hdr.channel;
	unsigned int bank;

	printf("Group %2d, ", group);
	switch (status) {
	case SND_UMP_MSG_PER_NOTE_RCC:
		printf("Per-note RCC           %2u, note %u, index %u, value 0x%x",
		       channel, m->per_note_rcc.note,
		       m->per_note_rcc.index, m->per_note_rcc.data);
		break;
	case SND_UMP_MSG_PER_NOTE_ACC:
		printf("Per-note ACC           %2u, note %u, index %u, value 0x%x",
		       channel, m->per_note_acc.note,
		       m->per_note_acc.index, m->per_note_acc.data);
		break;
	case SND_UMP_MSG_RPN:
		printf("RPN                    %2u, bank %u:%u, value 0x%x",
		       channel, m->rpn.bank, m->rpn.index, m->rpn.data);
		break;
	case SND_UMP_MSG_NRPN:
		printf("NRPN                   %2u, bank %u:%u, value 0x%x",
		       channel, m->rpn.bank, m->rpn.index, m->rpn.data);
		break;
	case SND_UMP_MSG_RELATIVE_RPN:
		printf("relative RPN           %2u, bank %u:%u, value 0x%x",
		       channel, m->rpn.bank, m->rpn.index, m->rpn.data);
		break;
	case SND_UMP_MSG_RELATIVE_NRPN:
		printf("relative NRP           %2u, bank %u:%u, value 0x%x",
		       channel, m->rpn.bank, m->rpn.index, m->rpn.data);
		break;
	case SND_UMP_MSG_PER_NOTE_PITCHBEND:
		printf("Per-note pitchbend     %2d, note %d, value 0x%x",
		       channel, m->per_note_pitchbend.note,
		       m->per_note_pitchbend.data);
		break;
	case SND_UMP_MSG_NOTE_OFF:
		printf("Note off               %2d, note %d, velocity 0x%x, attr type = %d, data = 0x%x",
		       channel, m->note_off.note, m->note_off.velocity,
		       m->note_off.attr_type, m->note_off.attr_data);
		break;
	case SND_UMP_MSG_NOTE_ON:
		printf("Note on                %2d, note %d, velocity 0x%x, attr type = %d, data = 0x%x",
		       channel, m->note_off.note, m->note_off.velocity,
		       m->note_off.attr_type, m->note_off.attr_data);
		break;
	case SND_UMP_MSG_POLY_PRESSURE:
		printf("Poly pressure          %2d, note %d, value 0x%x",
		       channel, m->poly_pressure.note, m->poly_pressure.data);
		break;
	case SND_UMP_MSG_CONTROL_CHANGE:
		printf("Control change         %2d, controller %d, value 0x%x",
		       channel, m->control_change.index, m->control_change.data);
		break;
	case SND_UMP_MSG_PROGRAM_CHANGE:
		printf("Program change         %2d, program %d",
		       channel, m->program_change.program);
		if (m->program_change.bank_valid)
			printf(", Bank select %d:%d",
			       m->program_change.bank_msb,
			       m->program_change.bank_lsb);
		break;
	case SND_UMP_MSG_CHANNEL_PRESSURE:
		printf("Channel pressure       %2d, value 0x%x",
		       channel, m->channel_pressure.data);
		break;
	case SND_UMP_MSG_PITCHBEND:
		printf("Channel pressure       %2d, value 0x%x",
		       channel, m->channel_pressure.data);
		break;
	case SND_UMP_MSG_PER_NOTE_MGMT:
		printf("Per-note management    %2d, value 0x%x",
		       channel, m->per_note_mgmt.flags);
		break;
	default:
		printf("UMP MIDI2 event: status = %d, channel = %x, 0x%08x",
		       status, status, *ump);
		break;
	}
	printf("\n");
}

static void dump_ump_event(const snd_seq_ump_event_t *ev)
{
	if (!snd_seq_ev_is_ump(ev)) {
		dump_event((const snd_seq_event_t *)ev);
		return;
	}

	printf("%3d:%-3d ", ev->source.client, ev->source.port);

	switch (snd_ump_msg_type(ev->ump)) {
	case SND_UMP_MSG_TYPE_MIDI1_CHANNEL_VOICE:
		dump_ump_midi1_event(ev->ump);
		break;
	case SND_UMP_MSG_TYPE_MIDI2_CHANNEL_VOICE:
		dump_ump_midi2_event(ev->ump);
		break;
	default:
		printf("UMP event: type = %d, group = %d, status = %d, 0x%08x\n",
		       snd_ump_msg_type(ev->ump),
		       snd_ump_msg_group(ev->ump),
		       snd_ump_msg_status(ev->ump),
		       *ev->ump);
		break;
	}
}
#endif /* HAVE_SEQ_CLIENT_INFO_GET_MIDI_VERSION */

static void list_ports(void)
{
	snd_seq_client_info_t *cinfo;
	snd_seq_port_info_t *pinfo;

	snd_seq_client_info_alloca(&cinfo);
	snd_seq_port_info_alloca(&pinfo);

	puts(" Port    Client name                      Port name");

	snd_seq_client_info_set_client(cinfo, -1);
	while (snd_seq_query_next_client(seq, cinfo) >= 0) {
		int client = snd_seq_client_info_get_client(cinfo);

		snd_seq_port_info_set_client(pinfo, client);
		snd_seq_port_info_set_port(pinfo, -1);
		while (snd_seq_query_next_port(seq, pinfo) >= 0) {
			/* we need both READ and SUBS_READ */
			if ((snd_seq_port_info_get_capability(pinfo)
			     & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ))
			    != (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ))
				continue;
			printf("%3d:%-3d  %-32.32s %s\n",
			       snd_seq_port_info_get_client(pinfo),
			       snd_seq_port_info_get_port(pinfo),
			       snd_seq_client_info_get_name(cinfo),
			       snd_seq_port_info_get_name(pinfo));
		}
	}
}

static void help(const char *argv0)
{
	printf("Usage: %s [options]\n"
		"\nAvailable options:\n"
		"  -h,--help                  this help\n"
		"  -V,--version               show version\n"
		"  -l,--list                  list input ports\n"
#ifdef HAVE_SEQ_CLIENT_INFO_GET_MIDI_VERSION
		"  -u,--ump=version           set client MIDI version (0=legacy, 1= UMP MIDI 1.0, 2=UMP MIDI2.0)\n"
		"  -r,--raw                   do not convert UMP and legacy events\n"
#endif
		"  -p,--port=client:port,...  source port(s)\n",
		argv0);
}

static void version(void)
{
	puts("aseqdump version " SND_UTIL_VERSION_STR);
}

static void sighandler(int sig)
{
	stop = 1;
}

int main(int argc, char *argv[])
{
	static const char short_options[] = "hVlp:"
#ifdef HAVE_SEQ_CLIENT_INFO_GET_MIDI_VERSION
		"u:r"
#endif
		;
	static const struct option long_options[] = {
		{"help", 0, NULL, 'h'},
		{"version", 0, NULL, 'V'},
		{"list", 0, NULL, 'l'},
		{"port", 1, NULL, 'p'},
#ifdef HAVE_SEQ_CLIENT_INFO_GET_MIDI_VERSION
		{"ump", 1, NULL, 'u'},
		{"raw", 0, NULL, 'r'},
#endif
		{0}
	};

	int do_list = 0;
	struct pollfd *pfds;
	int npfds;
	int c, err;

	init_seq();

	while ((c = getopt_long(argc, argv, short_options,
				long_options, NULL)) != -1) {
		switch (c) {
		case 'h':
			help(argv[0]);
			return 0;
		case 'V':
			version();
			return 0;
		case 'l':
			do_list = 1;
			break;
		case 'p':
			parse_ports(optarg);
			break;
#ifdef HAVE_SEQ_CLIENT_INFO_GET_MIDI_VERSION
		case 'u':
			ump_version = atoi(optarg);
			snd_seq_set_client_midi_version(seq, ump_version);
			break;
		case 'r':
			snd_seq_set_client_ump_conversion(seq, 0);
			break;
#endif
		default:
			help(argv[0]);
			return 1;
		}
	}
	if (optind < argc) {
		help(argv[0]);
		return 1;
	}

	if (do_list) {
		list_ports();
		return 0;
	}

	create_port();
	connect_ports();

	err = snd_seq_nonblock(seq, 1);
	check_snd("set nonblock mode", err);
	
	if (port_count > 0)
		printf("Waiting for data.");
	else
		printf("Waiting for data at port %d:0.",
		       snd_seq_client_id(seq));
	printf(" Press Ctrl+C to end.\n");
	printf("Source  %sEvent                  Ch  Data\n",
	       ump_version ? "Group    " : "");
	
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	npfds = snd_seq_poll_descriptors_count(seq, POLLIN);
	pfds = alloca(sizeof(*pfds) * npfds);
	for (;;) {
		snd_seq_poll_descriptors(seq, pfds, npfds, POLLIN);
		if (poll(pfds, npfds, -1) < 0)
			break;
		for (;;) {
			snd_seq_event_t *event;
#ifdef HAVE_SEQ_CLIENT_INFO_GET_MIDI_VERSION
			snd_seq_ump_event_t *ump_ev;
			if (ump_version > 0) {
				err = snd_seq_ump_event_input(seq, &ump_ev);
				if (err < 0)
					break;
				if (ump_ev)
					dump_ump_event(ump_ev);
				continue;
			}
#endif

			err = snd_seq_event_input(seq, &event);
			if (err < 0)
				break;
			if (event)
				dump_event(event);
		}
		fflush(stdout);
		if (stop)
			break;
	}

	snd_seq_close(seq);
	return 0;
}
