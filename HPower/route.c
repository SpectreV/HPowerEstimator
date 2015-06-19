/*
 * route.c
 *
 *  Created on: 3 Apr, 2014
 *      Author: liang
 */
#include <time.h>

#include "vpr_types.h"
#include "power.h"
#include "rr_graph.h"
#include "place_and_route.h"
#include "ReadLine.h"
#include "rr_graph_util.h"
#include "rr_graph2.h"

static void build_route(int width_fac);

void build_route(int width_fac) {
	clock_t begin, end;
	t_graph_type graph_type;
	int Warnings;
	enum e_base_cost_type base_cost_type;

	graph_type = GRAPH_UNIDIR;
	base_cost_type = DELAY_NORMALIZED;

	init_chan(width_fac, Arch.Chans);

	begin = clock();

	/* Set up the routing resource graph defined by this FPGA architecture. */

	build_rr_graph(graph_type, num_types, type_descriptors, nx, ny, grid,
			chan_width_x[0], NULL, Arch.SBType, Arch.Fs, Arch.num_segments,
			RoutingArch->num_switch, Arch.Segments,
			RoutingArch->global_route_switch, RoutingArch->delayless_switch,
			*Timing, RoutingArch->wire_to_ipin_switch, base_cost_type,
			&Warnings);

	end = clock();
#ifdef CLOCKS_PER_SEC
	printf("build rr_graph took %g seconds\n",
			(float) (end - begin) / CLOCKS_PER_SEC);
#else
	printf("build rr_graph took %g seconds\n",
			(float) (end - begin) / CLK_PER_SEC);

#endif

}

void read_trace_file(char *trace_file) {

	FILE *infile;
	char **tokens, *coord;
	int line, i, error;
	int cur_net, skip_cur_net;
	struct s_trace *trace, *next;
	char str[256];
	t_rr_type rr_type, prev_rr_type;

	int x, y, pin, class, pad, track;

	trace_head = (struct s_trace **) my_malloc(
			num_nets * sizeof(struct s_trace *));
	trace_tail = (struct s_trace **) my_malloc(
			num_nets * sizeof(struct s_trace *));

	infile = fopen(trace_file, "r");
	line = 0;

	/* Check first line match */
	tokens = ReadLineTokens(infile, &line);
	error = 0;
	if (NULL == tokens) {
		error = 1;
	}
	for (i = 0; i < 7; ++i) {
		if (!error) {
			if (NULL == tokens[i]) {
				error = 1;
				break;
			}
		}
	}
	if (!error) {
		if ((0 != strcmp(tokens[0], "Array"))
				|| (0 != strcmp(tokens[1], "size:"))
				|| (0 != strcmp(tokens[3], "x"))
				|| (0 != strcmp(tokens[5], "logic"))
				|| (0 != strcmp(tokens[6], "blocks."))) {
			error = 1;
		};
		if (nx != atoi(tokens[2]) || ny != atoi(tokens[4]))
			error = 1;
	}

	tokens = ReadLineTokens(infile, &line);
	if (NULL == tokens) {
		error = 1;
	}
	for (i = 0; i < 4; ++i) {
		if (!error) {
			if (NULL == tokens[i]) {
				error = 1;
				break;
			}
		}
	}
	if (!error) {
		if ((0 != strcmp(tokens[0], "T_crit:"))
				|| (0 != strcmp(tokens[2], "Width_fac:"))) {
			error = 1;
		};
		T_critical_path = atof(tokens[1]);
		fixed_chan_width = atoi(tokens[3]);
	}

	tokens = ReadLineTokens(infile, &line);
	if (NULL == tokens || 0 != strcmp(tokens[0], "Routing:"))
		error = 1;

	if (error) {
		printf(
				ERRTAG
				"'%s' - Bad FPGA size/T_crit/width specification line or header line in routing file\n",
				trace_file);
		exit(1);
	}

	build_route(fixed_chan_width);

	/* Begin parsing traces
	 * */
	cur_net = OPEN;
	while (1) {
		tokens = ReadLineTokens(infile, &line);
		if (tokens == NULL) {
			/* Set last net trace_tail */
			if (cur_net != OPEN) {
				trace->next = NULL;
				trace_tail[cur_net] = trace;
			}
			break;
		}
		if (0 == strcmp(tokens[0], "Net")) {
			skip_cur_net = 0;
			/* Set previous net trace_tail */
			if (cur_net != OPEN) {
				trace->next = NULL;
				trace_tail[cur_net] = trace;
			}

			cur_net = atoi(tokens[1]);

			trace = (struct s_trace *) my_malloc(sizeof(struct s_trace));
			trace_head[cur_net] = trace;

			strcpy(str, "(");
			strcat(str, clb_net[cur_net].name);
			strcat(str, ")\0");
			if (0 != strcmp(tokens[2], str)
					&& 0 == strcmp(tokens[3], "global")) {
				skip_cur_net = 1;
				trace_head[cur_net] = NULL;
				trace_tail[cur_net] = NULL;
				cur_net = OPEN;
				free(trace);
			}
		} else if (!skip_cur_net) {
			/* rr_resource node of the current net */
			if (0 == strcmp(tokens[0], "SOURCE")) {

				/* SOURCE (x,y) Class: class*/
				/* tokens[1] gives coordinates of the routing resource*/
				rr_type = SOURCE;
				coord = strtok(tokens[1], "(,)");
				x = atoi(coord);
				coord = strtok(NULL, "(,)");
				y = atoi(coord);

				if (0 == (strcmp(tokens[2], "Class:"))) {
					class = atoi(tokens[3]);
					trace->index = get_rr_node_index(x, y, rr_type, class,
							rr_node_indices);
				} else if (0 == (strcmp(tokens[2], "Pad:"))) {
					pad = atoi(tokens[3]);
					trace->index = get_rr_node_index(x, y, rr_type, pad,
							rr_node_indices);
				} else if (0 == (strcmp(tokens[4], "Class:"))) {
					class = atoi(tokens[5]);
					trace->index = get_rr_node_index(x, y, rr_type, class,
							rr_node_indices);
				} else {
					assert(0 == (strcmp(tokens[4], "Pad:")));
					pad = atoi(tokens[5]);
					trace->index = get_rr_node_index(x, y, rr_type, pad,
							rr_node_indices);
				}
				prev_rr_type = SOURCE;
				continue;

			} else if (0 == strcmp(tokens[0], "OPIN")) {
				/* OPIN (x,y) Pin/Pad: pin/pad */
				rr_type = OPIN;
				coord = strtok(tokens[1], "(,)");
				x = atoi(coord);
				coord = strtok(NULL, "(,)");
				y = atoi(coord);

				next = (struct s_trace *) my_malloc(sizeof(struct s_trace));

				if (0 == (strcmp(tokens[2], "Pin:"))) {
					pin = atoi(tokens[3]);
					next->index = get_rr_node_index(x, y, rr_type, pin,
							rr_node_indices);
				} else if (0 == (strcmp(tokens[2], "Pad:"))) {
					pad = atoi(tokens[3]);
					next->index = get_rr_node_index(x, y, rr_type, pad,
							rr_node_indices);
				} else if (0 == (strcmp(tokens[4], "Pin:"))) {
					pin = atoi(tokens[5]);
					next->index = get_rr_node_index(x, y, rr_type, pin,
							rr_node_indices);
				} else {
					assert(0 == (strcmp(tokens[4], "Pad:")));
					pad = atoi(tokens[5]);
					next->index = get_rr_node_index(x, y, rr_type, pad,
							rr_node_indices);
				}

			} else if (0 == strcmp(tokens[0], "CHANX")) {
				/* CHANX (x,y) to (xhigh, yhigh) Track: track */
				rr_type = CHANX;
				coord = strtok(tokens[1], "(,)");
				x = atoi(coord);
				coord = strtok(NULL, "(,)");
				y = atoi(coord);

				next = (struct s_trace *) my_malloc(sizeof(struct s_trace));

				if (0 != strcmp(tokens[2], "to")) {
					assert(0 == strcmp(tokens[2], "Track:"));
					track = atoi(tokens[3]);
				} else {
					assert(0 == strcmp(tokens[4], "Track:"));
					track = atoi(tokens[5]);
				}

				next->index = get_rr_node_index(x, y, rr_type, track,
						rr_node_indices);

			} else if (0 == strcmp(tokens[0], "CHANY")) {
				/* CHANY (x,y) to (xhigh, yhigh) Track: track */
				rr_type = CHANY;
				coord = strtok(tokens[1], "(,)");
				x = atoi(coord);
				coord = strtok(NULL, "(,)");
				y = atoi(coord);

				next = (struct s_trace *) my_malloc(sizeof(struct s_trace));

				if (0 != strcmp(tokens[2], "to")) {
					assert(0 == strcmp(tokens[2], "Track:"));
					track = atoi(tokens[3]);
				} else {
					assert(0 == strcmp(tokens[4], "Track:"));
					track = atoi(tokens[5]);
				}

				next->index = get_rr_node_index(x, y, rr_type, track,
						rr_node_indices);

			} else if (0 == strcmp(tokens[0], "IPIN")) {
				/* IPIN (x,y) Pin/Pad: pin/pad */
				rr_type = IPIN;
				coord = strtok(tokens[1], "(,)");
				x = atoi(coord);
				coord = strtok(NULL, "(,)");
				y = atoi(coord);

				next = (struct s_trace *) my_malloc(sizeof(struct s_trace));

				if (0 == (strcmp(tokens[2], "Pin:"))) {
					pin = atoi(tokens[3]);
					next->index = get_rr_node_index(x, y, rr_type, pin,
							rr_node_indices);
				} else if (0 == (strcmp(tokens[2], "Pad:"))) {
					pad = atoi(tokens[3]);
					next->index = get_rr_node_index(x, y, rr_type, pad,
							rr_node_indices);
				} else if (0 == (strcmp(tokens[4], "Pin:"))) {
					pin = atoi(tokens[5]);
					next->index = get_rr_node_index(x, y, rr_type, pin,
							rr_node_indices);
				} else {
					assert(0 == (strcmp(tokens[4], "Pad:")));
					pad = atoi(tokens[5]);
					next->index = get_rr_node_index(x, y, rr_type, pad,
							rr_node_indices);
				}

			} else if (0 == strcmp(tokens[0], "SINK")) {
				/* SINK (x,y) Class/Pad: class/pad*/
				/* tokens[1] gives coordinates of the routing resource*/
				rr_type = SINK;
				coord = strtok(tokens[1], "(,)");
				x = atoi(coord);
				coord = strtok(NULL, "(,)");
				y = atoi(coord);

				next = (struct s_trace *) my_malloc(sizeof(struct s_trace));

				if (0 == (strcmp(tokens[2], "Class:"))) {
					class = atoi(tokens[3]);
					next->index = get_rr_node_index(x, y, rr_type, class,
							rr_node_indices);
				} else if (0 == (strcmp(tokens[2], "Pad:"))) {
					pad = atoi(tokens[3]);
					next->index = get_rr_node_index(x, y, rr_type, pad,
							rr_node_indices);
				} else if (0 == (strcmp(tokens[4], "Class:"))) {
					class = atoi(tokens[5]);
					next->index = get_rr_node_index(x, y, rr_type, class,
							rr_node_indices);
				} else {
					assert(0 == (strcmp(tokens[4], "Pad:")));
					pad = atoi(tokens[5]);
					next->index = get_rr_node_index(x, y, rr_type, pad,
							rr_node_indices);
				}
				next->iswitch = OPEN;

			}

			/* Previous trace switch handling */
			trace->iswitch = OPEN;
			for (i = 0; i < rr_node[trace->index].num_edges; i++) {
				if (rr_node[trace->index].edges[i] == next->index) {
					trace->iswitch = rr_node[trace->index].switches[i];
				}
			}
			if (trace->iswitch == OPEN && prev_rr_type != SINK) {
				printf("Prev node: #%d, next node: #%d\n", trace->index,
						next->index);
				exit(1);

			}

			trace->next = next;
			trace = next;
			prev_rr_type = rr_type;

		}
	}

	printf("\nTrace file successfully loaded at line #%d!\n", line);

	fclose(infile);

}
