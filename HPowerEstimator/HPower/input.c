/*
 * arch.c
 *
 *  Created on: 2 Apr, 2014
 *      Author: liang
 */

#include <string.h>

#include "power.h"
#include "read_xml_arch_file.h"
#include "pb_type_graph.h"

static void setup_routing_and_timing_info();
static void build_interc_nodes(t_pb_graph_node *pb_graph_node,
		t_pb_graph_node *parent_pb_graph_node, const t_pb_type *pb_type);
static void alloc_and_load_mode_interconnect(
		INOUTP t_pb_graph_node *pb_graph_parent_node,
		INOUTP t_pb_graph_node **pb_graph_children_nodes,
		INP const t_mode * mode);

/*static struct s_linked_vptr *edges_head;
static struct s_linked_vptr *num_edges_head;*/

void build_arch(char *ArchFile, int TimingEnabled, t_arch *Arch) {

	int i, j;

	printf("Read Xml Architecture file...\n");
	XmlReadArch(ArchFile, TimingEnabled, Arch, &type_descriptors, &num_types);
	printf("Arch file %s loaded successfully!\n", ArchFile);

	user_models = Arch->models;
	library_models = Arch->model_library;

	EMPTY_TYPE = NULL;
	FILL_TYPE = NULL;
	IO_TYPE = NULL;
	for (i = 0; i < num_types; i++) {
		if (strcmp(type_descriptors[i].name, "<EMPTY>") == 0) {
			EMPTY_TYPE = &type_descriptors[i];
		} else if (strcmp(type_descriptors[i].name, "io") == 0) {
			IO_TYPE = &type_descriptors[i];
		} else {
			for (j = 0; j < type_descriptors[i].num_grid_loc_def; j++) {
				if (type_descriptors[i].grid_loc_def[j].grid_loc_type == FILL) {
					assert(FILL_TYPE == NULL);
					FILL_TYPE = &type_descriptors[i];
				}
			}
		}

	}
	assert(EMPTY_TYPE != NULL && FILL_TYPE != NULL && IO_TYPE != NULL);

	setup_routing_and_timing_info();

	alloc_and_load_all_pb_graphs();
	printf("Building complex block graph \n");

	for (i = 0; i < num_types; ++i) {
		if (type_descriptors[i].pb_type) {
			build_interc_nodes(type_descriptors[i].pb_graph_head, NULL,
					type_descriptors[i].pb_type);
		} else {
			assert(&type_descriptors[i] == EMPTY_TYPE);
		}
	}
}

static void build_interc_nodes(t_pb_graph_node *pb_graph_node,
		t_pb_graph_node *parent_pb_graph_node, const t_pb_type *pb_type) {

	int i, j, k;

	for (i = 0; i < pb_type->num_modes; ++i) {
		alloc_and_load_mode_interconnect(pb_graph_node,
				pb_graph_node->child_pb_graph_nodes[i], &pb_type->modes[i]);
		for (j = 0; j < pb_type->modes[i].num_pb_type_children; j++) {
			for (k = 0; k < pb_type->modes[i].pb_type_children[j].num_pb; k++) {
				build_interc_nodes(
						&pb_graph_node->child_pb_graph_nodes[i][j][k],
						pb_graph_node, &pb_type->modes[i].pb_type_children[j]);
			}
		}
	}

}

/**
 * Generate interconnect associated with a mode of operation
 * pb_graph_parent_node: parent node of pb in mode
 * pb_graph_children_nodes: [0..num_pb_type_in_mode-1][0..num_pb]
 * mode: mode of operation
 */
static void alloc_and_load_mode_interconnect(
		INOUTP t_pb_graph_node *pb_graph_parent_node,
		INOUTP t_pb_graph_node **pb_graph_children_nodes,
		INP const t_mode * mode) {

	int i, j;

	for (i = 0; i < mode->num_interconnect; i++) {
		/* determine the interconnect input and output pins */
		mode->interconnect[i].input_pb_graph_node_pins =
				alloc_and_load_port_pin_ptrs_from_string(pb_graph_parent_node,
						pb_graph_children_nodes,
						mode->interconnect[i].input_string,
						&mode->interconnect[i].num_input_pb_graph_node_pins,
						&mode->interconnect[i].num_input_pb_graph_node_sets,
						TRUE, TRUE);

		mode->interconnect[i].output_pb_graph_node_pins =
				alloc_and_load_port_pin_ptrs_from_string(pb_graph_parent_node,
						pb_graph_children_nodes,
						mode->interconnect[i].output_string,
						&mode->interconnect[i].num_output_pb_graph_node_pins,
						&mode->interconnect[i].num_output_pb_graph_node_sets,
						FALSE, TRUE);

	}
}

static void setup_routing_and_timing_info() {

	RoutingArch = (struct s_det_routing_arch *) my_malloc(
			sizeof(struct s_det_routing_arch));
	Timing = (struct s_timing_inf *) my_malloc(sizeof(struct s_timing_inf));

	RoutingArch->num_switch = Arch.num_switches;

	/* Depends on RoutingArch->num_switch */
	RoutingArch->wire_to_ipin_switch = RoutingArch->num_switch;
	++RoutingArch->num_switch;

	/* Depends on RoutingArch->num_switch */
	RoutingArch->delayless_switch = RoutingArch->num_switch;
	RoutingArch->global_route_switch = RoutingArch->delayless_switch;
	++RoutingArch->num_switch;

	/* Alloc the list now that we know the final num_switch value */
	switch_inf = (struct s_switch_inf *) my_malloc(
			sizeof(struct s_switch_inf) * RoutingArch->num_switch);

	/* Copy the switch data from architecture file */
	memcpy(switch_inf, Arch.Switches,
			sizeof(struct s_switch_inf) * Arch.num_switches);

	/* Delayless switch for connecting sinks and sources with their pins. */
	switch_inf[RoutingArch->delayless_switch].buffered = TRUE;
	switch_inf[RoutingArch->delayless_switch].R = 0.;
	switch_inf[RoutingArch->delayless_switch].Cin = 0.;
	switch_inf[RoutingArch->delayless_switch].Cout = 0.;
	switch_inf[RoutingArch->delayless_switch].Tdel = 0.;

	/* The wire to ipin switch for all types. Curently all types
	 * must share ipin switch. Some of the timing code would
	 * need to be changed otherwise. */
	switch_inf[RoutingArch->wire_to_ipin_switch].buffered = TRUE;
	switch_inf[RoutingArch->wire_to_ipin_switch].R = 0.;
	switch_inf[RoutingArch->wire_to_ipin_switch].Cin = Arch.C_ipin_cblock;
	switch_inf[RoutingArch->wire_to_ipin_switch].Cout = 0.;
	switch_inf[RoutingArch->wire_to_ipin_switch].Tdel = Arch.T_ipin_cblock;

	/* Don't do anything if they don't want timing */
	if (FALSE == TimingEnabled) {
		memset(Timing, 0, sizeof(t_timing_inf));
		Timing->timing_analysis_enabled = FALSE;
		return;
	}

	Timing->C_ipin_cblock = Arch.C_ipin_cblock;
	Timing->T_ipin_cblock = Arch.T_ipin_cblock;
	Timing->timing_analysis_enabled = TimingEnabled;
}

void build_place(const char *place_file, const char *arch_file,
		const char *net_file, int num_blocks, struct s_block block_list[]) {

	FILE *infile;
	char **tokens;
	int line;
	int i;
	int error;
	struct s_block *cur_blk;

	infile = fopen(place_file, "r");

	/* Check filenames in first line match */
	tokens = ReadLineTokens(infile, &line);
	error = 0;
	if (NULL == tokens) {
		error = 1;
	}
	for (i = 0; i < 6; ++i) {
		if (!error) {
			if (NULL == tokens[i]) {
				error = 1;
			}
		}
	}
	if (!error) {
		if ((0 != strcmp(tokens[0], "Netlist"))
				|| (0 != strcmp(tokens[1], "file:"))
				|| (0 != strcmp(tokens[3], "Architecture"))
				|| (0 != strcmp(tokens[4], "file:"))) {
			error = 1;
		};
	}
	if (error) {
		printf(ERRTAG
		"'%s' - Bad filename specification line in placement file\n",
				place_file);
		exit(1);
	}
	/*if (0 != strcmp(tokens[2], arch_file)) {
		printf(ERRTAG
		"'%s' - Architecture file that generated placement (%s) does "
		"not match current architecture file (%s)\n", place_file, tokens[2],
				arch_file);
		exit(1);
	}
	if (0 != strcmp(tokens[5], net_file)) {
		printf(ERRTAG
		"'%s' - Netlist file that generated placement (%s) does "
		"not match current netlist file (%s)\n", place_file, tokens[5],
				net_file);
		exit(1);
	}*/

	/* Check array size in second line matches */
	tokens = ReadLineTokens(infile, &line);
	error = 0;
	if (NULL == tokens) {
		error = 1;
	}
	for (i = 0; i < 7; ++i) {
		if (!error) {
			if (NULL == tokens[i]) {
				error = 1;
			}
		}
	}
	if (!error) {
		if ((0 != strcmp(tokens[0], "Array"))
				|| (0 != strcmp(tokens[1], "size:"))
				|| (0 != strcmp(tokens[3], "x"))
				|| (0 != strcmp(tokens[5], "logic"))
				|| (0 != strcmp(tokens[6], "blocks"))) {
			error = 1;
		};
	}
	if (error) {
		printf(ERRTAG
		"'%s' - Bad fpga size specification line in placement file\n",
				place_file);
		exit(1);
	}

	nx = my_atoi(tokens[2]);
	ny = my_atoi(tokens[4]);

	tokens = ReadLineTokens(infile, &line);
	while (tokens) {
		/* Linear search to match pad to netlist */
		cur_blk = NULL;
		for (i = 0; i < num_blocks; ++i) {
			if (0 == strcmp(block_list[i].name, tokens[0])) {
				cur_blk = (block_list + i);
				break;
			}
		}

		/* Error if invalid block */
		if (NULL == cur_blk) {
			printf(ERRTAG "'%s':%d - Block in placement file does "
			"not exist in netlist\n", place_file, line);
			exit(1);
		}

		/* Set pad coords */
		cur_blk->x = my_atoi(tokens[1]);
		cur_blk->y = my_atoi(tokens[2]);
		cur_blk->z = my_atoi(tokens[3]);

		/* Get next line */
		assert(*tokens);
		free(*tokens);
		free(tokens);
		tokens = ReadLineTokens(infile, &line);
	}

	fclose(infile);
}

/**
 * Creates edges to connect all input pins to output pins
 */
/*
static void alloc_and_load_complete_interc_edges(
		INP t_interconnect *interconnect,
		INOUTP t_pb_graph_pin *** input_pb_graph_node_pin_ptrs,
		INP int num_input_sets, INP int *num_input_ptrs,
		INOUTP t_pb_graph_pin *** output_pb_graph_node_pin_ptrs,
		INP int num_output_sets, INP int *num_output_ptrs) {
	int i_inset, i_outset, i_inpin, i_outpin;
	int in_count, out_count;
	t_pb_graph_edge *edges;
	int i_edge;
	struct s_linked_vptr *cur;

	 Allocate memory for edges, and reallocate more memory for pins connecting to those edges
	in_count = out_count = 0;

	for (i_inset = 0; i_inset < num_input_sets; i_inset++) {
		in_count += num_input_ptrs[i_inset];
	}
	for (i_outset = 0; i_outset < num_output_sets; i_outset++) {
		out_count += num_output_ptrs[i_outset];
	}

	edges = my_calloc(in_count * out_count, sizeof(t_pb_graph_edge));
	cur = my_malloc(sizeof(struct s_linked_vptr));
	cur->next = edges_head;
	edges_head = cur;
	cur->data_vptr = (void *) edges;
	cur = my_malloc(sizeof(struct s_linked_vptr));
	cur->next = num_edges_head;
	num_edges_head = cur;
	cur->data_vptr = (void *) ((long) in_count * out_count);

	for (i_inset = 0; i_inset < num_input_sets; i_inset++) {
		for (i_inpin = 0; i_inpin < num_input_ptrs[i_inset]; i_inpin++) {
			input_pb_graph_node_pin_ptrs[i_inset][i_inpin]->output_edges =
					my_realloc(
							input_pb_graph_node_pin_ptrs[i_inset][i_inpin]->output_edges,
							(input_pb_graph_node_pin_ptrs[i_inset][i_inpin]->num_output_edges
									+ out_count) * sizeof(t_pb_graph_edge *));
		}
	}

	for (i_outset = 0; i_outset < num_output_sets; i_outset++) {
		for (i_outpin = 0; i_outpin < num_output_ptrs[i_outset]; i_outpin++) {
			output_pb_graph_node_pin_ptrs[i_outset][i_outpin]->input_edges =
					my_realloc(
							output_pb_graph_node_pin_ptrs[i_outset][i_outpin]->input_edges,
							(output_pb_graph_node_pin_ptrs[i_outset][i_outpin]->num_input_edges
									+ in_count) * sizeof(t_pb_graph_edge *));
		}
	}

	i_edge = 0;

	 Load connections between pins and record these updates in the edges
	for (i_inset = 0; i_inset < num_input_sets; i_inset++) {
		for (i_inpin = 0; i_inpin < num_input_ptrs[i_inset]; i_inpin++) {
			for (i_outset = 0; i_outset < num_output_sets; i_outset++) {
				for (i_outpin = 0; i_outpin < num_output_ptrs[i_outset];
						i_outpin++) {

					input_pb_graph_node_pin_ptrs[i_inset][i_inpin]->output_edges[input_pb_graph_node_pin_ptrs[i_inset][i_inpin]->num_output_edges] =
							&edges[i_edge];
					input_pb_graph_node_pin_ptrs[i_inset][i_inpin]->num_output_edges++;
					output_pb_graph_node_pin_ptrs[i_outset][i_outpin]->input_edges[output_pb_graph_node_pin_ptrs[i_outset][i_outpin]->num_input_edges] =
							&edges[i_edge];
					output_pb_graph_node_pin_ptrs[i_outset][i_outpin]->num_input_edges++;

					edges[i_edge].num_input_pins = 1;
					edges[i_edge].input_pins = my_malloc(
							sizeof(t_pb_graph_pin *));
					edges[i_edge].input_pins[0] =
							input_pb_graph_node_pin_ptrs[i_inset][i_inpin];
					edges[i_edge].num_output_pins = 1;
					edges[i_edge].output_pins = my_malloc(
							sizeof(t_pb_graph_pin *));
					edges[i_edge].output_pins[0] =
							output_pb_graph_node_pin_ptrs[i_outset][i_outpin];

					edges[i_edge].interconnect = interconnect;
					edges[i_edge].driver_set = i_inset;
					edges[i_edge].driver_pin = i_inpin;

					i_edge++;
				}
			}
		}
	}
	assert(i_edge == in_count * out_count);
}

static void alloc_and_load_direct_interc_edges(
		INP t_interconnect *interconnect,
		INOUTP t_pb_graph_pin *** input_pb_graph_node_pin_ptrs,
		INP int num_input_sets, INP int *num_input_ptrs,
		INOUTP t_pb_graph_pin *** output_pb_graph_node_pin_ptrs,
		INP int num_output_sets, INP int *num_output_ptrs) {

	int i;
	t_pb_graph_edge *edges;
	struct s_linked_vptr *cur;

	 Allocate memory for edges
	assert(num_input_sets == 1 && num_output_sets == 1);
	if (num_input_ptrs[0] != num_output_ptrs[0]) {
		printf(
				"input ptrs %d output ptrs %d input pin %s output pin %s input_pb_type %s output_pb_type %s\n",
				num_input_ptrs[0], num_output_ptrs[0],
				input_pb_graph_node_pin_ptrs[0][0]->port->name,
				output_pb_graph_node_pin_ptrs[0][0]->port->name,
				input_pb_graph_node_pin_ptrs[0][0]->parent_node->pb_type->name,
				output_pb_graph_node_pin_ptrs[0][0]->parent_node->pb_type->name);
	}
	assert(num_input_ptrs[0] == num_output_ptrs[0]);

	edges = my_calloc(num_input_ptrs[0], sizeof(t_pb_graph_edge));
	cur = my_malloc(sizeof(struct s_linked_vptr));
	cur->next = edges_head;
	edges_head = cur;
	cur->data_vptr = (void *) edges;
	cur = my_malloc(sizeof(struct s_linked_vptr));
	cur->next = num_edges_head;
	num_edges_head = cur;
	cur->data_vptr = (void *) ((long) num_input_ptrs[0]);

	 Reallocate memory for pins and load connections between pins and record these updates in the edges
	for (i = 0; i < num_input_ptrs[0]; i++) {
		input_pb_graph_node_pin_ptrs[0][i]->output_edges = my_realloc(
				input_pb_graph_node_pin_ptrs[0][i]->output_edges,
				(input_pb_graph_node_pin_ptrs[0][i]->num_output_edges + 1)
						* sizeof(t_pb_graph_edge *));
		input_pb_graph_node_pin_ptrs[0][i]->output_edges[input_pb_graph_node_pin_ptrs[0][i]->num_output_edges] =
				&edges[i];
		input_pb_graph_node_pin_ptrs[0][i]->num_output_edges++;

		output_pb_graph_node_pin_ptrs[0][i]->input_edges = my_realloc(
				output_pb_graph_node_pin_ptrs[0][i]->input_edges,
				(output_pb_graph_node_pin_ptrs[0][i]->num_input_edges + 1)
						* sizeof(t_pb_graph_edge *));
		output_pb_graph_node_pin_ptrs[0][i]->input_edges[output_pb_graph_node_pin_ptrs[0][i]->num_input_edges] =
				&edges[i];
		output_pb_graph_node_pin_ptrs[0][i]->num_input_edges++;

		edges[i].num_input_pins = 1;
		edges[i].input_pins = my_malloc(sizeof(t_pb_graph_pin *));
		edges[i].input_pins[0] = input_pb_graph_node_pin_ptrs[0][i];
		edges[i].num_output_pins = 1;
		edges[i].output_pins = my_malloc(sizeof(t_pb_graph_pin *));
		edges[i].output_pins[0] = output_pb_graph_node_pin_ptrs[0][i];

		edges[i].interconnect = interconnect;
		edges[i].driver_set = 0;
		edges[i].driver_pin = i;
	}
}

static void alloc_and_load_mux_interc_edges( INP t_interconnect * interconnect,
		INOUTP t_pb_graph_pin *** input_pb_graph_node_pin_ptrs,
		INP int num_input_sets, INP int *num_input_ptrs,
		INOUTP t_pb_graph_pin *** output_pb_graph_node_pin_ptrs,
		INP int num_output_sets, INP int *num_output_ptrs) {
	int i_inset, i_inpin, i_outpin;
	t_pb_graph_edge *edges;
	struct s_linked_vptr *cur;

	 Allocate memory for edges, and reallocate more memory for pins connecting to those edges
	assert(num_output_sets == 1);

	edges = my_calloc(num_input_sets, sizeof(t_pb_graph_edge));
	cur = my_malloc(sizeof(struct s_linked_vptr));
	cur->next = edges_head;
	edges_head = cur;
	cur->data_vptr = (void *) edges;
	cur = my_malloc(sizeof(struct s_linked_vptr));
	cur->next = num_edges_head;
	num_edges_head = cur;
	cur->data_vptr = (void *) ((long) num_input_sets);

	for (i_inset = 0; i_inset < num_input_sets; i_inset++) {
		for (i_inpin = 0; i_inpin < num_input_ptrs[i_inset]; i_inpin++) {
			input_pb_graph_node_pin_ptrs[i_inset][i_inpin]->output_edges =
					my_realloc(
							input_pb_graph_node_pin_ptrs[i_inset][i_inpin]->output_edges,
							(input_pb_graph_node_pin_ptrs[i_inset][i_inpin]->num_output_edges
									+ 1) * sizeof(t_pb_graph_edge *));
		}
	}

	for (i_outpin = 0; i_outpin < num_output_ptrs[0]; i_outpin++) {
		output_pb_graph_node_pin_ptrs[0][i_outpin]->input_edges = my_realloc(
				output_pb_graph_node_pin_ptrs[0][i_outpin]->input_edges,
				(output_pb_graph_node_pin_ptrs[0][i_outpin]->num_input_edges
						+ num_input_sets) * sizeof(t_pb_graph_edge *));
	}

	 Load connections between pins and record these updates in the edges
	for (i_inset = 0; i_inset < num_input_sets; i_inset++) {
		assert(num_output_ptrs[0] == num_input_ptrs[i_inset]);
		edges[i_inset].input_pins = my_calloc(num_output_ptrs[0],
				sizeof(t_pb_graph_pin *));
		edges[i_inset].output_pins = my_calloc(num_output_ptrs[0],
				sizeof(t_pb_graph_pin *));
		edges[i_inset].num_input_pins = num_output_ptrs[0];
		edges[i_inset].num_output_pins = num_output_ptrs[0];
		for (i_inpin = 0; i_inpin < num_input_ptrs[i_inset]; i_inpin++) {
			input_pb_graph_node_pin_ptrs[i_inset][i_inpin]->output_edges[input_pb_graph_node_pin_ptrs[i_inset][i_inpin]->num_output_edges] =
					&edges[i_inset];
			input_pb_graph_node_pin_ptrs[i_inset][i_inpin]->num_output_edges++;
			output_pb_graph_node_pin_ptrs[0][i_inpin]->input_edges[output_pb_graph_node_pin_ptrs[0][i_inpin]->num_input_edges] =
					&edges[i_inset];
			output_pb_graph_node_pin_ptrs[0][i_inpin]->num_input_edges++;

			edges[i_inset].input_pins[i_inpin] =
					input_pb_graph_node_pin_ptrs[i_inset][i_inpin];
			edges[i_inset].output_pins[i_inpin] =
					output_pb_graph_node_pin_ptrs[0][i_inpin];

			assert(i_inpin == 0);  current does not support bus-based routing, TODO: Support bus based routing
			edges[i_inset].interconnect = interconnect;
			edges[i_inset].driver_set = i_inset;
			edges[i_inset].driver_pin = i_inpin;
		}
	}
}

*/
